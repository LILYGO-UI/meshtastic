#include "pages/messages/messages_view_model.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <map>
#include <utility>

namespace {

std::string node_name(const meshtastic::ClientSnapshot &snapshot, std::uint32_t number)
{
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [number](const meshtastic::Node &item) { return item.number == number; });
    if (node != snapshot.nodes.end() && !node->long_name.empty()) return node->long_name;
    char fallback[10]{};
    std::snprintf(fallback, sizeof(fallback), "!%08x", number);
    return fallback;
}

std::string channel_name(const meshtastic::ClientSnapshot &snapshot, std::uint32_t index)
{
    const auto channel = std::find_if(snapshot.channels.begin(), snapshot.channels.end(),
                                      [index](const meshtastic::ChannelInfo &item) {
                                          return item.index == static_cast<std::int32_t>(index);
                                      });
    if (channel != snapshot.channels.end() && !channel->name.empty()) return channel->name;
    return index == 0 ? "LongFast" : "Channel " + std::to_string(index);
}

std::string message_detail(const meshtastic::Message &message)
{
    if (message.received_at == 0) return message.rssi == 0 ? "" : std::to_string(message.rssi) + " dBm";
    const auto time = static_cast<std::time_t>(message.received_at);
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%H:%M", std::localtime(&time)) == 0) return {};
    if (message.rssi == 0) return buffer;
    return std::string(buffer) + "  |  " + std::to_string(message.rssi) + " dBm";
}

}  // namespace

MessagesViewModel::MessagesViewModel()
    : connected_(0, 0, 1), content_revision_(0, 0), footer_("0 conversations | 0 unread")
{
}

void MessagesViewModel::select_conversation(bool direct, std::uint32_t peer, std::uint32_t channel, std::string name)
{
    if (name.empty()) name = direct ? "Private chat" : (channel == 0 ? "Primary channel" : "Channel");
    if (selected_ && selected_direct_ == direct && selected_peer_ == peer && selected_channel_ == channel &&
        selected_name_ == name)
        return;
    selected_         = true;
    selected_direct_  = direct;
    selected_peer_    = peer;
    selected_channel_ = channel;
    selected_name_    = std::move(name);
}

void MessagesViewModel::clear_selection() noexcept
{
    selected_         = false;
    selected_direct_  = false;
    selected_peer_    = 0;
    selected_channel_ = 0;
    selected_name_.clear();
}

void MessagesViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    snapshot_ = snapshot;
    const bool was_connected = connected();
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    auto conversations = snapshot.conversations;
    if (conversations.empty()) {
        std::map<std::string, meshtastic::Conversation> grouped;
        for (const auto &message : snapshot.messages) {
            const bool direct = message.to != meshtastic::kBroadcastNode && message.to != 0;
            const auto peer = direct ? (message.outgoing ? message.to : message.from) : 0;
            const auto key = std::string(direct ? "dm:" : "ch:") + std::to_string(peer) + ":" +
                             std::to_string(message.channel);
            auto &conversation = grouped[key];
            conversation.direct = direct;
            conversation.peer = peer;
            conversation.channel = message.channel;
            conversation.latest = message;
            if (!message.outgoing && !message.read) ++conversation.unread_count;
        }
        for (auto &entry : grouped) conversations.push_back(std::move(entry.second));
    }
    std::stable_sort(conversations.begin(), conversations.end(), [](const auto &left, const auto &right) {
        if (left.latest.local_id != right.latest.local_id) return left.latest.local_id > right.latest.local_id;
        if (left.latest.received_at != right.latest.received_at) return left.latest.received_at > right.latest.received_at;
        return left.latest.id > right.latest.id;
    });
    auto lowercase = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return std::tolower(ch); });
        return value;
    };
    const auto filter = lowercase(query_);
    std::vector<MessageRowViewModel> rows;
    std::size_t unread_total = 0;
    for (const auto &conversation : conversations) {
        unread_total += conversation.unread_count;
        const auto &message = conversation.latest;
        const auto name = conversation.direct ? node_name(snapshot, conversation.peer) : channel_name(snapshot, conversation.channel);
        const auto sender = message.outgoing ? "You" : node_name(snapshot, message.from);
        if (!filter.empty() && lowercase(name + " " + sender + " " + message.text).find(filter) == std::string::npos) continue;
        auto detail = message_detail(message);
        if (conversation.direct && conversation.channel != 0) {
            detail = channel_name(snapshot, conversation.channel) + (detail.empty() ? "" : " | " + detail);
        }
        if (message.outgoing) {
            if (!detail.empty()) detail += " | ";
            detail += meshtastic::message_status_name(message.status);
        }
        rows.push_back({message.id, sender, name, message.text, detail, conversation.peer,
                        conversation.channel, message.outgoing, conversation.direct, conversation.unread_count});
    }
    bool changed = rows.size() != rows_.size();
    for (std::size_t index = 0; !changed && index < rows.size(); ++index) {
        const auto &left = rows[index];
        const auto &right = rows_[index];
        changed = left.id != right.id || left.sender != right.sender || left.conversation != right.conversation ||
                  left.text != right.text || left.detail != right.detail || left.outgoing != right.outgoing ||
                  left.direct != right.direct || left.peer != right.peer || left.channel != right.channel ||
                  left.unread_count != right.unread_count;
    }
    if (changed) rows_ = std::move(rows);
    const auto footer = std::to_string(conversations.size()) + " conversations | " + std::to_string(unread_total) + " unread";
    footer_.set(footer.c_str());
    if (changed || was_connected != connected()) publish_content();
}

void MessagesViewModel::set_query(std::string query)
{
    if (query_ == query) return;
    query_ = std::move(query);
    sync(snapshot_);
}

const std::string &MessagesViewModel::query() const noexcept
{
    return query_;
}

bool MessagesViewModel::connected() const noexcept
{
    return connected_.value() != 0;
}

const std::vector<MessageRowViewModel> &MessagesViewModel::rows() const noexcept
{
    return rows_;
}

bool MessagesViewModel::has_selected_conversation() const noexcept
{
    return selected_;
}

bool MessagesViewModel::selected_direct() const noexcept
{
    return selected_direct_;
}

std::uint32_t MessagesViewModel::selected_peer() const noexcept
{
    return selected_peer_;
}

std::uint32_t MessagesViewModel::selected_channel() const noexcept
{
    return selected_channel_;
}

const std::string &MessagesViewModel::selected_name() const noexcept
{
    return selected_name_;
}

lv_subject_t *MessagesViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

lv_subject_t *MessagesViewModel::footer_subject() noexcept
{
    return footer_.get();
}

void MessagesViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
