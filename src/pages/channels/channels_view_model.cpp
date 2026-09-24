#include "pages/channels/channels_view_model.hpp"

#include <climits>
#include <utility>

ChannelsViewModel::ChannelsViewModel() : content_revision_(0, 0)
{
}

void ChannelsViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    std::vector<ChannelRowViewModel> rows;
    rows.reserve(snapshot.channels.size());
    for (const auto &channel : snapshot.channels) {
        if (channel.role != 1 && channel.role != 2) continue;
        rows.push_back({channel.index, channel.name.empty() ? "Unnamed channel" : channel.name,
                        channel.encrypted ? "Encrypted" : "Unencrypted", channel.role});
    }

    bool changed = rows.size() != rows_.size();
    for (std::size_t index = 0; !changed && index < rows.size(); ++index) {
        changed = rows[index].index != rows_[index].index || rows[index].name != rows_[index].name ||
                  rows[index].detail != rows_[index].detail || rows[index].role != rows_[index].role;
    }
    if (!changed) return;

    rows_               = std::move(rows);
    const auto selected = selected_channel_;
    select_channel(selected);
    publish_content();
}

void ChannelsViewModel::select_channel(std::int32_t index)
{
    for (const auto &row : rows_) {
        if (row.index != index) continue;
        selected_channel_      = row.index;
        selected_channel_name_ = row.name;
        return;
    }
    select_primary();
}

void ChannelsViewModel::select_primary()
{
    for (const auto &row : rows_) {
        if (row.role != 1) continue;
        selected_channel_      = row.index;
        selected_channel_name_ = row.name;
        return;
    }
    selected_channel_      = 0;
    selected_channel_name_ = "Primary channel";
}

const std::vector<ChannelRowViewModel> &ChannelsViewModel::rows() const noexcept
{
    return rows_;
}

std::int32_t ChannelsViewModel::selected_channel() const noexcept
{
    return selected_channel_;
}

const std::string &ChannelsViewModel::selected_channel_name() const noexcept
{
    return selected_channel_name_;
}

lv_subject_t *ChannelsViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

void ChannelsViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
