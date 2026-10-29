#include "pages/channels/channels_view_model.hpp"

#include <algorithm>
#include <climits>
#include <cctype>
#include <sstream>
#include <utility>

namespace {
std::string base64_encode(const std::string &bytes)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    unsigned accumulator = 0, bits = 0;
    for (unsigned char byte : bytes) {
        accumulator = (accumulator << 8U) | byte;
        bits += 8;
        while (bits >= 6) { bits -= 6; result += alphabet[(accumulator >> bits) & 63U]; }
    }
    if (bits) result += alphabet[(accumulator << (6U - bits)) & 63U];
    return result;
}
bool base64_decode(const std::string &text, std::string &bytes)
{
    if (text.empty() || text.size() % 4 == 1) return false;
    unsigned accumulator = 0, bits = 0;
    std::size_t padding = 0;
    bytes.clear();
    for (const unsigned char character : text) {
        if (character == '=') { if (++padding > 2) return false; continue; }
        if (padding) return false;
        int value = -1;
        if (character >= 'A' && character <= 'Z') value = character - 'A';
        else if (character >= 'a' && character <= 'z') value = character - 'a' + 26;
        else if (character >= '0' && character <= '9') value = character - '0' + 52;
        else if (character == '+' || character == '-') value = 62;
        else if (character == '/' || character == '_') value = 63;
        if (value < 0) return false;
        accumulator = (accumulator << 6U) | static_cast<unsigned>(value);
        bits += 6;
        if (bits >= 8) { bits -= 8; bytes += static_cast<char>((accumulator >> bits) & 255U); }
    }
    if (bits && (accumulator & ((1U << bits) - 1U)) != 0) return false;
    if (padding && (text.size() % 4 || (padding == 1 && bits != 2) || (padding == 2 && bits != 4))) return false;
    return !bytes.empty();
}
bool valid_name(const std::string &name)
{
    return name.size() < 12 && std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool valid_key(const std::string &key)
{
    return key.empty() || (key.size() == 1 && static_cast<unsigned char>(key[0]) <= 10) || key.size() == 16 || key.size() == 32;
}
bool channel_matches(const meshtastic::Channel &expected, const meshtastic::Channel &actual)
{
    return expected.index() == actual.index() && expected.role() == actual.role() &&
           expected.settings().name() == actual.settings().name() && expected.settings().psk() == actual.settings().psk() &&
           expected.settings().uplink_enabled() == actual.settings().uplink_enabled() &&
           expected.settings().downlink_enabled() == actual.settings().downlink_enabled();
}
}  // namespace

ChannelsViewModel::ChannelsViewModel(meshtastic::MeshtasticClient *client) : client_(client) {}
void ChannelsViewModel::set_client(meshtastic::MeshtasticClient *client) noexcept
{
    if (client_ != client) transaction_.reset();
    client_ = client;
}
void ChannelsViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    if (snapshot.my_node_number != 0 && snapshot.my_node_number != radio_number_) {
        radio_number_ = snapshot.my_node_number;
        draft_.reset();
        original_.reset();
        transaction_.reset();
        import_.reset();
        share_link_.clear();
        import_summary_.clear();
        status_.set("Radio changed; read its channels before editing");
        radio_identity_.set(radio_identity_.value() + 1);
        publish_editor();
    }
    snapshot_ = snapshot;
    if (client_ && transaction_.state() == meshtastic::configuration::AdminTransaction::State::waiting) {
        std::string error;
        const auto outcome = transaction_.advance(*client_, snapshot_, error);
        if (outcome == meshtastic::configuration::AdminTransaction::State::confirmed)
            status_.set("Channel update saved and confirmed by the device");
        else if (outcome == meshtastic::configuration::AdminTransaction::State::failed)
            status_.set(error.c_str());
    }
    std::vector<ChannelRowViewModel> rows;
    rows.reserve(snapshot.channels.size());
    for (const auto &channel : snapshot.channels) {
        if (channel.role != 1 && channel.role != 2) continue;
        rows.push_back({channel.index, channel.name.empty() ? "Default channel" : channel.name,
                        channel.encrypted ? "Encrypted" : "Unencrypted", channel.role});
    }
    bool changed = rows.size() != rows_.size();
    for (std::size_t index = 0; !changed && index < rows.size(); ++index) {
        changed = rows[index].index != rows_[index].index || rows[index].name != rows_[index].name ||
                  rows[index].detail != rows_[index].detail || rows[index].role != rows_[index].role;
    }
    if (changed) {
        rows_ = std::move(rows);
        select_channel(selected_channel_);
        publish_content();
    }
    // An open editor owns its draft. Radio updates only become visible after
    // the user explicitly refreshes/reopens that editor.
}
void ChannelsViewModel::select_channel(std::int32_t index)
{
    for (const auto &row : rows_) {
        if (row.index != index) continue;
        selected_channel_ = row.index;
        selected_channel_name_ = row.name;
        return;
    }
    select_primary();
}
void ChannelsViewModel::select_primary()
{
    for (const auto &row : rows_) {
        if (row.role != 1) continue;
        selected_channel_ = row.index;
        selected_channel_name_ = row.name;
        return;
    }
    selected_channel_ = 0;
    selected_channel_name_ = "Primary channel";
}
bool ChannelsViewModel::edit_channel(std::int32_t index)
{
    if (index < 0 || index > 7) { status_.set("Select a channel slot from 0 to 7"); return false; }
    const auto found = snapshot_.channel_configs.find(index);
    if (found == snapshot_.channel_configs.end()) {
        if (client_ && snapshot_.state == meshtastic::ConnectionState::connected) client_->request_channel(index);
        status_.set("Channel requested; wait for its response, then open this slot again");
        return false;
    }
    original_ = draft_ = found->second;
    status_.set("Editing the radio's current channel. Saving requires confirmation.");
    publish_editor();
    return true;
}
bool ChannelsViewModel::begin_new_channel()
{
    for (const auto &[index, channel] : snapshot_.channel_configs) {
        if (channel.role() != meshtastic::Channel::DISABLED) continue;
        if (!edit_channel(index)) return false;
        draft_->set_role(meshtastic::Channel::SECONDARY);
        status_.set("New secondary channel draft. Enter a name and encryption key before review.");
        publish_editor();
        return true;
    }
    status_.set("No disabled channel slots remain");
    return false;
}
bool ChannelsViewModel::refresh_channel(std::int32_t index)
{
    if (!client_ || snapshot_.state != meshtastic::ConnectionState::connected || !snapshot_.config_complete) {
        status_.set("Connect and finish synchronization before refreshing channels");
        return false;
    }
    const bool queued = client_->request_channel(static_cast<std::uint32_t>(index));
    status_.set(queued ? "Channel refresh requested; an open draft is retained" : "Unable to queue channel refresh");
    return queued;
}
bool ChannelsViewModel::decode_key(const std::string &text, std::string &key, std::string &error)
{
    if (!base64_decode(text, key) || (key.size() != 16 && key.size() != 32)) {
        error = "Enter a Base64 key containing exactly 16 or 32 bytes";
        return false;
    }
    error.clear();
    return true;
}
bool ChannelsViewModel::stage_channel(std::string name, std::int32_t role, KeyMode key_mode,
                                       const std::string &key, bool uplink, bool downlink)
{
    if (!draft_) { status_.set("Read a channel from the radio before editing"); return false; }
    if (!valid_name(name)) { status_.set("Channel names must be fewer than 12 bytes and on one line"); return false; }
    if (!meshtastic::Channel::Role_IsValid(role)) { status_.set("Select a valid channel role"); return false; }
    if (original_->role() == meshtastic::Channel::PRIMARY && role != meshtastic::Channel::PRIMARY) {
        status_.set("Keep a primary channel. Promote another channel to primary to replace this one.");
        return false;
    }
    std::string psk = draft_->settings().psk(), error;
    switch (key_mode) {
        case KeyMode::keep_existing: break;
        case KeyMode::unencrypted: psk.clear(); break;
        case KeyMode::public_default: psk.assign(1, '\x01'); break;
        case KeyMode::custom_base64: if (!decode_key(key, psk, error)) { status_.set(error.c_str()); return false; } break;
    }
    auto next = *draft_;
    next.set_role(static_cast<meshtastic::Channel::Role>(role));
    next.mutable_settings()->set_name(std::move(name));
    next.mutable_settings()->set_psk(std::move(psk));
    next.mutable_settings()->set_uplink_enabled(uplink);
    next.mutable_settings()->set_downlink_enabled(downlink);
    draft_ = std::move(next);
    status_.set("Channel draft ready. Confirm to apply changes to the radio.");
    return true;
}
bool ChannelsViewModel::writable()
{
    if (!client_ || snapshot_.my_node_number == 0 || snapshot_.my_node_number != radio_number_ ||
        snapshot_.state != meshtastic::ConnectionState::connected || !snapshot_.config_complete) {
        status_.set("Connect and finish synchronization before changing channels");
        return false;
    }
    if (snapshot_.session_passkey.empty()) {
        client_->request_session_key();
        status_.set("Admin key requested; wait for the radio response, then confirm again");
        return false;
    }
    if (transaction_.state() == meshtastic::configuration::AdminTransaction::State::waiting) {
        status_.set("A channel update is awaiting device readback; refresh the affected slots");
        return false;
    }
    return true;
}
std::uint64_t ChannelsViewModel::channel_revision(int index) const
{
    const auto found = snapshot_.channel_revisions.find(index);
    return found == snapshot_.channel_revisions.end() ? 0 : found->second;
}
bool ChannelsViewModel::queue_channels(const std::vector<meshtastic::Channel> &channels,
                                        const std::optional<meshtastic::Config> &lora)
{
    if (!writable()) return false;
    using meshtastic::configuration::AdminTransaction;
    std::vector<meshtastic::AdminMessage> writes;
    std::vector<AdminTransaction::Readback> reads;
    meshtastic::AdminMessage begin;
    begin.set_begin_edit_settings(true);
    writes.push_back(begin);
    for (const auto &channel : channels) {
        meshtastic::AdminMessage write, request, expected;
        write.mutable_set_channel()->CopyFrom(channel);
        request.set_get_channel_request(static_cast<std::uint32_t>(channel.index()) + 1U);
        expected.mutable_get_channel_response()->CopyFrom(channel);
        writes.push_back(std::move(write));
        reads.push_back({std::move(request), std::move(expected),
            [](const meshtastic::AdminMessage &wanted, const meshtastic::AdminMessage &actual) {
                return actual.has_get_channel_response() && channel_matches(wanted.get_channel_response(), actual.get_channel_response());
            }});
    }
    if (lora) {
        meshtastic::AdminMessage write, request, expected;
        write.mutable_set_config()->CopyFrom(*lora);
        request.set_get_config_request(meshtastic::AdminMessage::LORA_CONFIG);
        expected.mutable_get_config_response()->CopyFrom(*lora);
        writes.push_back(std::move(write));
        reads.push_back({std::move(request), std::move(expected), {}});
    }
    meshtastic::AdminMessage commit;
    commit.set_commit_edit_settings(true);
    writes.push_back(std::move(commit));
    std::string error;
    const bool queued = transaction_.start(*client_, std::move(writes), std::move(reads), error,
                                           snapshot_.my_node_number);
    status_.set(queued ? "Channel update queued; waiting for acknowledgements and readback" : error.c_str());
    return queued;
}
bool ChannelsViewModel::save_channel()
{
    if (!draft_) { status_.set("Open a channel slot before saving"); return false; }
    std::vector<meshtastic::Channel> changes;
    if (draft_->role() == meshtastic::Channel::PRIMARY) {
        for (const auto &[index, existing] : snapshot_.channel_configs) {
            if (index == draft_->index() || existing.role() != meshtastic::Channel::PRIMARY) continue;
            auto secondary = existing;
            secondary.set_role(meshtastic::Channel::SECONDARY);
            changes.push_back(std::move(secondary));
        }
    }
    changes.push_back(*draft_);
    return queue_channels(changes);
}
void ChannelsViewModel::discard_channel() { draft_ = original_; publish_editor(); status_.set("Channel draft discarded"); }
const meshtastic::Channel *ChannelsViewModel::draft_channel() const noexcept { return draft_ ? &*draft_ : nullptr; }
std::string ChannelsViewModel::make_channel_link(const meshtastic::ChannelSet &set)
{
    return "https://meshtastic.org/e/#" + base64_encode(set.SerializeAsString());
}
bool ChannelsViewModel::parse_channel_link(const std::string &link, meshtastic::ChannelSet &set, std::string &error)
{
    const std::vector<std::string> prefixes = {"https://meshtastic.org/e/#"};
    std::string encoded;
    for (const auto &prefix : prefixes) if (link.rfind(prefix, 0) == 0) encoded = link.substr(prefix.size());
    std::string bytes;
    if (encoded.empty() || encoded.size() > 8192 || !base64_decode(encoded, bytes) || !set.ParseFromString(bytes)) {
        error = "Enter a valid Meshtastic channel link";
        return false;
    }
    if (set.settings_size() < 1 || set.settings_size() > 8) { error = "A channel link must contain 1 to 8 channels"; return false; }
    for (const auto &settings : set.settings()) {
        if (!valid_name(settings.name())) { error = "An imported channel name exceeds 11 bytes or contains a line break"; return false; }
        if (!valid_key(settings.psk())) { error = "An imported channel has an unsupported key length"; return false; }
    }
    if (set.has_lora_config() && (!meshtastic::Config::LoRaConfig::RegionCode_IsValid(set.lora_config().region()) ||
                                 !meshtastic::Config::LoRaConfig::ModemPreset_IsValid(set.lora_config().modem_preset()) ||
                                 set.lora_config().hop_limit() > 7)) {
        error = "The link contains unsupported radio settings";
        return false;
    }
    error.clear();
    return true;
}
bool ChannelsViewModel::preview_import(const std::string &link)
{
    meshtastic::ChannelSet set;
    std::string error;
    if (!parse_channel_link(link, set, error)) { status_.set(error.c_str()); import_.reset(); return false; }
    std::ostringstream summary;
    summary << "Replace slots 0-" << set.settings_size() - 1 << ": ";
    for (int index = 0; index < set.settings_size(); ++index) {
        if (index) summary << ", ";
        summary << (set.settings(index).name().empty() ? "Default channel" : set.settings(index).name());
    }
    summary << ". Slot 0 becomes primary. Other slots are retained.";
    if (set.has_lora_config()) summary << " The link also contains radio settings; applying those is optional.";
    import_summary_ = summary.str();
    import_ = std::move(set);
    status_.set("Link validated. Review the channels and confirm before applying.");
    return true;
}
bool ChannelsViewModel::apply_import(bool include_lora)
{
    if (!import_) { status_.set("Preview a valid channel link first"); return false; }
    std::vector<meshtastic::Channel> channels;
    for (int index = 0; index < import_->settings_size(); ++index) {
        const auto found = snapshot_.channel_configs.find(index);
        if (found == snapshot_.channel_configs.end()) {
            if (client_) client_->request_channel(index);
            status_.set("Read all affected channel slots before importing; refresh and preview again");
            return false;
        }
        auto channel = found->second;
        channel.set_index(index);
        channel.set_role(index == 0 ? meshtastic::Channel::PRIMARY : meshtastic::Channel::SECONDARY);
        const auto unknown = channel.settings().unknown_fields();
        const auto module_unknown = channel.settings().module_settings().unknown_fields();
        channel.mutable_settings()->CopyFrom(import_->settings(index));
        channel.mutable_settings()->mutable_unknown_fields()->append(unknown);
        channel.mutable_settings()->mutable_module_settings()->mutable_unknown_fields()->append(module_unknown);
        channels.push_back(std::move(channel));
    }
    for (const auto &[index, existing] : snapshot_.channel_configs) {
        if (index < import_->settings_size() || existing.role() != meshtastic::Channel::PRIMARY) continue;
        auto channel = existing;
        channel.set_role(meshtastic::Channel::SECONDARY);
        channels.push_back(std::move(channel));
    }
    std::optional<meshtastic::Config> lora;
    if (include_lora && import_->has_lora_config()) {
        const auto found = snapshot_.device_configs.find(meshtastic::Config::kLora);
        if (found == snapshot_.device_configs.end()) { status_.set("Read current LoRa settings before applying radio settings from a link"); return false; }
        lora = found->second;
        const auto unknown = lora->lora().unknown_fields();
        lora->mutable_lora()->CopyFrom(import_->lora_config());
        lora->mutable_lora()->mutable_unknown_fields()->append(unknown);
    }
    return queue_channels(channels, lora);
}
bool ChannelsViewModel::export_channels()
{
    meshtastic::ChannelSet set;
    bool primary = false;
    for (const auto &[index, channel] : snapshot_.channel_configs) {
        (void)index;
        if (channel.role() == meshtastic::Channel::PRIMARY) { set.add_settings()->CopyFrom(channel.settings()); primary = true; break; }
    }
    if (!primary) { status_.set("Read the primary channel from the radio before sharing"); return false; }
    for (const auto &[index, channel] : snapshot_.channel_configs) {
        (void)index;
        if (channel.role() == meshtastic::Channel::SECONDARY) set.add_settings()->CopyFrom(channel.settings());
    }
    const auto lora = snapshot_.device_configs.find(meshtastic::Config::kLora);
    if (lora != snapshot_.device_configs.end()) set.mutable_lora_config()->CopyFrom(lora->second.lora());
    share_link_ = make_channel_link(set);
    status_.set("Share link includes channel keys. Give it only to people who should join this mesh.");
    return true;
}
const std::string &ChannelsViewModel::share_link() const noexcept { return share_link_; }
const std::string &ChannelsViewModel::import_summary() const noexcept { return import_summary_; }
bool ChannelsViewModel::import_has_lora() const noexcept { return import_ && import_->has_lora_config(); }
const std::vector<ChannelRowViewModel> &ChannelsViewModel::rows() const noexcept { return rows_; }
std::int32_t ChannelsViewModel::selected_channel() const noexcept { return selected_channel_; }
const std::string &ChannelsViewModel::selected_channel_name() const noexcept { return selected_channel_name_; }
lv_subject_t *ChannelsViewModel::content_subject() noexcept { return content_revision_.get(); }
lv_subject_t *ChannelsViewModel::editor_content_subject() noexcept { return editor_revision_.get(); }
lv_subject_t *ChannelsViewModel::status_subject() noexcept { return status_.get(); }
lv_subject_t *ChannelsViewModel::radio_identity_subject() noexcept { return radio_identity_.get(); }
void ChannelsViewModel::publish_content() { content_revision_.set(content_revision_.value() == INT32_MAX ? 0 : content_revision_.value() + 1); }
void ChannelsViewModel::publish_editor() { editor_revision_.set(editor_revision_.value() == INT32_MAX ? 0 : editor_revision_.value() + 1); }
