#include "pages/messages/messages_view_model.hpp"

#include <algorithm>
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

MessagesViewModel::MessagesViewModel()
    : connected_(0, 0, 1), content_revision_(0, 0), footer_("0 messages | This session")
{
}

void MessagesViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    const bool was_connected = connected();
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);

    std::vector<MessageRowViewModel> rows;
    const auto first = snapshot.messages.size() > 8 ? snapshot.messages.size() - 8 : 0;
    rows.reserve(snapshot.messages.size() - first);
    for (std::size_t index = snapshot.messages.size(); index > first; --index) {
        const auto &message = snapshot.messages[index - 1];
        rows.push_back({message.outgoing ? "You" : node_name(snapshot, message.from), message.text, message.outgoing});
    }

    bool changed = rows.size() != rows_.size();
    for (std::size_t index = 0; !changed && index < rows.size(); ++index) {
        changed = rows[index].sender != rows_[index].sender || rows[index].text != rows_[index].text ||
                  rows[index].outgoing != rows_[index].outgoing;
    }
    if (changed) rows_ = std::move(rows);

    const auto footer = std::to_string(rows_.size()) + " messages | This session";
    footer_.set(footer.c_str());
    if (changed || was_connected != connected()) publish_content();
}

bool MessagesViewModel::connected() const noexcept
{
    return connected_.value() != 0;
}

const std::vector<MessageRowViewModel> &MessagesViewModel::rows() const noexcept
{
    return rows_;
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
