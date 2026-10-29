#include "pages/chat/chat_view_model.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <ctime>
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

}  // namespace

ChatViewModel::ChatViewModel(meshtastic::MeshtasticClient *client)
    : client_(client),
      connected_(0, 0, 1),
      content_revision_(0, 0),
      channel_name_("Primary channel"),
      node_status_("0 nodes synchronized")
{
}

void ChatViewModel::set_client(meshtastic::MeshtasticClient *client) noexcept
{
    client_ = client;
}

void ChatViewModel::select_channel(std::int32_t index, std::string name)
{
    select_conversation(false, 0, index < 0 ? 0U : static_cast<std::uint32_t>(index), std::move(name));
}

void ChatViewModel::select_conversation(bool direct, std::uint32_t peer, std::uint32_t channel, std::string name)
{
    if (direct) {
        if (name.empty()) name = "Private chat";
    } else if (name.empty()) {
        name = channel == 0 ? "Primary channel" : "Channel";
    }
    const bool changed = !selection_set_ || direct_ != direct || peer_ != peer || channel_ != channel ||
                         selection_name_ != name;
    if (changed) {
        message_limit_ = 50;
        loaded_history_.clear();
        feedback_.set("");
    }
    direct_        = direct;
    peer_          = peer;
    channel_       = channel;
    selection_name_ = name;
    selection_set_ = true;
    channel_name_.set(name.c_str());
    if (changed) publish_content();
}

void ChatViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    const auto scope = snapshot.my_node_number == 0 && snapshot.endpoint == snapshot_.endpoint && !scope_.empty()
                           ? scope_ : snapshot.endpoint + "#" + std::to_string(snapshot.my_node_number);
    if (scope_ != scope) {
        loaded_history_.clear();
        message_limit_ = 50;
        drafts_.clear();
        scope_ = scope;
    }
    snapshot_ = snapshot;
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    const auto status = std::to_string(snapshot.nodes.size()) + " nodes synchronized";
    node_status_.set(status.c_str());
    if (active_ && selection_set_ && client_) client_->mark_conversation_read(direct_, peer_, channel_);

    std::vector<meshtastic::Message> selected;
    if (client_ && selection_set_) {
        const auto recent = client_->query_messages(direct_, peer_, channel_, 0, 50);
        for (const auto &message : recent) loaded_history_[message.local_id] = message;
        for (const auto &message : snapshot.messages) {
            // A snapshot spans every conversation. Only update records already
            // loaded for this conversation; do not import unrelated messages
            // or bypass the page limit.
            const auto loaded = loaded_history_.find(message.local_id);
            if (loaded != loaded_history_.end()) loaded->second = message;
        }
        // A retry may belong to a previously loaded page, outside the latest
        // window. Refresh outstanding records until their delivery is final.
        for (auto &[local_id, message] : loaded_history_) {
            if (local_id <= 0 || (message.status != meshtastic::MessageStatus::queued &&
                                  message.status != meshtastic::MessageStatus::sent)) continue;
            const auto current = client_->query_messages(direct_, peer_, channel_, local_id + 1, 1);
            if (!current.empty() && current.front().local_id == local_id) message = current.front();
        }
        for (const auto &entry : loaded_history_) selected.push_back(entry.second);
        has_older_ = !selected.empty() && !client_->query_messages(direct_, peer_, channel_, selected.front().local_id, 1).empty();
    }
    if (!client_) {
        for (const auto &message : snapshot.messages) {
            const bool direct = message.to != meshtastic::kBroadcastNode && message.to != 0;
            const auto peer = direct ? (message.outgoing ? message.to : message.from) : 0;
            if (direct != direct_ || message.channel != channel_ || (direct && peer != peer_)) continue;
            selected.push_back(message);
        }
    }
    if (!client_) {
        has_older_ = selected.size() > message_limit_;
        if (has_older_) selected.erase(selected.begin(), selected.end() - static_cast<std::ptrdiff_t>(message_limit_));
    }
    std::vector<ChatMessageViewModel> messages;
    messages.reserve(selected.size());
    for (const auto &message : selected) {
        std::string detail;
        if (message.received_at) {
            const auto timestamp = static_cast<std::time_t>(message.received_at);
            char time[32]{};
            if (std::strftime(time, sizeof(time), "%m-%d %H:%M", std::localtime(&timestamp))) detail = time;
        }
        bool retryable = false;
        if (message.outgoing) {
            const char *state = "Queued";
            switch (message.status) {
                case meshtastic::MessageStatus::sent: state = "Sent to radio"; break;
                case meshtastic::MessageStatus::acknowledged: state = "Acknowledged"; break;
                case meshtastic::MessageStatus::rejected: state = "Rejected"; retryable = true; break;
                case meshtastic::MessageStatus::timed_out: state = "No acknowledgement"; retryable = true; break;
                case meshtastic::MessageStatus::failed: state = "Failed"; retryable = true; break;
                default: break;
            }
            if (!detail.empty()) detail += " | ";
            detail += state;
            if (!message.status_detail.empty()) detail += " · " + message.status_detail;
        }
        messages.push_back({message.id, message.outgoing ? "You" : node_name(snapshot, message.from),
                            message.text, message.outgoing, message.local_id, std::move(detail), retryable});
    }
    bool changed = messages.size() != messages_.size();
    for (std::size_t index = 0; !changed && index < messages.size(); ++index) {
        const auto &left = messages[index];
        const auto &right = messages_[index];
        changed = left.id != right.id || left.local_id != right.local_id || left.sender != right.sender ||
                  left.text != right.text || left.outgoing != right.outgoing || left.detail != right.detail ||
                  left.retryable != right.retryable;
    }
    if (!changed) return;
    messages_ = std::move(messages);
    publish_content();
}

bool ChatViewModel::send_text(const char *text)
{
    if (!client_ || !connected() || !selection_set_ || !text || !*text) return false;
    if (std::string(text).size() > meshtastic::kMaxTextPayload) {
        feedback_.set("Message is too long (maximum 233 UTF-8 bytes)");
        return false;
    }
    const bool queued = direct_ ? client_->send_text_to(text, peer_, channel_) : client_->send_text(text, channel_);
    feedback_.set(queued ? "Queued for transmission" : "Unable to queue message");
    if (queued) {
        set_draft("");
        sync(client_->snapshot());
    }
    return queued;
}

bool ChatViewModel::retry_message(std::int64_t local_id)
{
    const bool queued = client_ && connected() && client_->retry_message_by_local_id(local_id);
    feedback_.set(queued ? "Retry queued" : "Unable to retry this message");
    if (queued) {
        const auto found = loaded_history_.find(local_id);
        if (found != loaded_history_.end()) found->second.status = meshtastic::MessageStatus::queued;
        sync(client_->snapshot());
    }
    return queued;
}

bool ChatViewModel::load_older()
{
    if (!has_older_) return false;
    if (client_ && !loaded_history_.empty()) {
        const auto page = client_->query_messages(direct_, peer_, channel_, loaded_history_.begin()->first, 50);
        for (const auto &message : page) loaded_history_[message.local_id] = message;
    } else message_limit_ += 50;
    sync(client_ ? client_->snapshot() : snapshot_);
    return true;
}

void ChatViewModel::set_active(bool active)
{
    active_ = active;
    if (active && client_ && selection_set_) client_->mark_conversation_read(direct_, peer_, channel_);
}

std::string ChatViewModel::conversation_key() const
{
    return scope_ + ":" + std::string(direct_ ? "dm:" : "channel:") +
           std::to_string(direct_ ? peer_ : 0) + ":" + std::to_string(channel_);
}

void ChatViewModel::set_draft(std::string text)
{
    drafts_[conversation_key()] = std::move(text);
}

std::string ChatViewModel::draft() const
{
    const auto found = drafts_.find(conversation_key());
    return found == drafts_.end() ? std::string{} : found->second;
}

bool ChatViewModel::has_older() const noexcept
{
    return has_older_;
}

lv_subject_t *ChatViewModel::feedback_subject() noexcept
{
    return feedback_.get();
}

bool ChatViewModel::connected() const noexcept
{
    return connected_.value() != 0;
}

bool ChatViewModel::has_selection() const noexcept
{
    return selection_set_;
}

bool ChatViewModel::selected_direct() const noexcept
{
    return direct_;
}

std::uint32_t ChatViewModel::selected_peer() const noexcept
{
    return peer_;
}

std::uint32_t ChatViewModel::selected_channel() const noexcept
{
    return channel_;
}

const std::vector<ChatMessageViewModel> &ChatViewModel::messages() const noexcept
{
    return messages_;
}

lv_subject_t *ChatViewModel::channel_subject() noexcept
{
    return channel_name_.get();
}

lv_subject_t *ChatViewModel::connected_subject() noexcept
{
    return connected_.get();
}

lv_subject_t *ChatViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

lv_subject_t *ChatViewModel::node_status_subject() noexcept
{
    return node_status_.get();
}

void ChatViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
