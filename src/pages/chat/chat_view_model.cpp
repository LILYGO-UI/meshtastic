#include "pages/chat/chat_view_model.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
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
    channel_ = index < 0 ? 0U : static_cast<std::uint32_t>(index);
    if (name.empty()) name = "Primary channel";
    channel_name_.set(name.c_str());
}

void ChatViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    const auto status = std::to_string(snapshot.nodes.size()) + " nodes synchronized";
    node_status_.set(status.c_str());

    std::vector<ChatMessageViewModel> messages;
    const auto first = snapshot.messages.size() > 10 ? snapshot.messages.size() - 10 : 0;
    messages.reserve(snapshot.messages.size() - first);
    for (std::size_t index = first; index < snapshot.messages.size(); ++index) {
        const auto &message = snapshot.messages[index];
        messages.push_back(
            {message.id, message.outgoing ? "You" : node_name(snapshot, message.from), message.text, message.outgoing});
    }

    bool changed = messages.size() != messages_.size();
    for (std::size_t index = 0; !changed && index < messages.size(); ++index) {
        changed = messages[index].id != messages_[index].id || messages[index].sender != messages_[index].sender ||
                  messages[index].text != messages_[index].text ||
                  messages[index].outgoing != messages_[index].outgoing;
    }
    if (!changed) return;
    messages_ = std::move(messages);
    publish_content();
}

bool ChatViewModel::send_text(const char *text)
{
    if (!client_ || !connected() || !text || !*text || !client_->send_text(text, channel_)) return false;
    sync(client_->snapshot());
    return true;
}

bool ChatViewModel::connected() const noexcept
{
    return connected_.value() != 0;
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
