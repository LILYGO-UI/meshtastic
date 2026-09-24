#include "pages/nodes/nodes_view_model.hpp"

#include <ctime>
#include <cstdint>
#include <utility>

namespace {

std::string last_heard_text(std::uint32_t timestamp)
{
    if (timestamp == 0) return "Last heard unknown";
    const auto now = std::time(nullptr);
    if (now <= static_cast<std::time_t>(timestamp)) return "Just now";
    const auto seconds = static_cast<std::uint64_t>(now - static_cast<std::time_t>(timestamp));
    if (seconds < 60) return "Just now";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    if (seconds < 86400) return std::to_string(seconds / 3600) + " hr ago";
    return std::to_string(seconds / 86400) + " d ago";
}

}  // namespace

NodesViewModel::NodesViewModel()
    : connected_(0, 0, 1), content_revision_(0, 0), count_("ALL 0"), footer_("0 nodes | From meshtasticd NodeDB")
{
}

void NodesViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    const bool was_connected = connected();
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);

    std::vector<NodeRowViewModel> rows;
    rows.reserve(snapshot.nodes.size());
    std::size_t online_count   = 0;
    std::size_t position_count = 0;
    std::size_t one_hop_count  = 0;
    const auto now             = std::time(nullptr);
    for (const auto &node : snapshot.nodes) {
        std::string detail = last_heard_text(node.last_heard);
        if (node.hops_away) detail += " | " + std::to_string(*node.hops_away) + " hops";
        if (node.battery_level) detail += " | " + std::to_string(*node.battery_level) + "%";
        rows.push_back({node.number, node.long_name, std::move(detail), node.number == snapshot.my_node_number});
        if (node.last_heard != 0 && now - static_cast<std::time_t>(node.last_heard) < 15 * 60) ++online_count;
        if (node.latitude && node.longitude) ++position_count;
        if (node.hops_away && *node.hops_away == 1) ++one_hop_count;
    }

    bool changed = rows.size() != rows_.size();
    for (std::size_t index = 0; !changed && index < rows.size(); ++index) {
        changed = rows[index].number != rows_[index].number || rows[index].name != rows_[index].name ||
                  rows[index].detail != rows_[index].detail || rows[index].local != rows_[index].local;
    }
    if (changed) rows_ = std::move(rows);

    const auto count = "ALL " + std::to_string(rows_.size()) + "     ONLINE " + std::to_string(online_count) +
                       "     POSITION " + std::to_string(position_count) + "     1 HOP " +
                       std::to_string(one_hop_count);
    count_.set(count.c_str());
    const auto footer = std::to_string(rows_.size()) + " nodes  |  Sorted by last heard";
    footer_.set(footer.c_str());
    if (changed || was_connected != connected()) publish_content();
}

void NodesViewModel::select_node(std::uint32_t number) noexcept
{
    selected_node_ = number;
}

bool NodesViewModel::connected() const noexcept
{
    return connected_.value() != 0;
}

std::uint32_t NodesViewModel::selected_node() const noexcept
{
    return selected_node_;
}

const std::vector<NodeRowViewModel> &NodesViewModel::rows() const noexcept
{
    return rows_;
}

lv_subject_t *NodesViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

lv_subject_t *NodesViewModel::count_subject() noexcept
{
    return count_.get();
}

lv_subject_t *NodesViewModel::footer_subject() noexcept
{
    return footer_.get();
}

void NodesViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
