#include "pages/node_detail/node_detail_view_model.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <ctime>
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

bool equal(const NodeDetailPresentation &left, const NodeDetailPresentation &right)
{
    return left.found == right.found && left.has_metrics == right.has_metrics &&
           left.has_position == right.has_position && left.initials == right.initials && left.name == right.name &&
           left.presence == right.presence && left.battery == right.battery && left.voltage == right.voltage &&
           left.coordinates == right.coordinates && left.altitude == right.altitude && left.id == right.id &&
           left.number == right.number && left.signal == right.signal && left.last_heard == right.last_heard;
}

}  // namespace

NodeDetailViewModel::NodeDetailViewModel() : content_revision_(0, 0)
{
}

void NodeDetailViewModel::select_node(std::uint32_t number, const meshtastic::ClientSnapshot &snapshot)
{
    selected_node_ = number;
    sync(snapshot);
}

void NodeDetailViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    NodeDetailPresentation next;
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [this](const meshtastic::Node &item) { return item.number == selected_node_; });
    if (node != snapshot.nodes.end()) {
        next.found        = true;
        next.initials     = !node->short_name.empty() ? node->short_name : node->long_name.substr(0, 2);
        next.name         = node->long_name.empty() ? node->id : node->long_name;
        next.presence     = last_heard_text(node->last_heard);
        next.last_heard   = last_heard_text(node->last_heard);
        next.has_metrics  = node->battery_level.has_value() || node->voltage.has_value();
        next.has_position = node->latitude.has_value() && node->longitude.has_value();
        next.id           = node->id;
        if (node->hops_away) next.presence += " | " + std::to_string(*node->hops_away) + " hops";
        if (node->battery_level) next.battery = std::to_string(*node->battery_level) + "% battery";
        if (node->voltage) {
            char voltage[32]{};
            std::snprintf(voltage, sizeof(voltage), "%.2f V", *node->voltage);
            next.voltage = voltage;
        }
        if (next.has_position) {
            char coordinates[64]{};
            std::snprintf(coordinates, sizeof(coordinates), "%.5f, %.5f", *node->latitude, *node->longitude);
            next.coordinates = coordinates;
        }
        if (node->altitude) next.altitude = "Altitude " + std::to_string(*node->altitude) + " m";
        char number[11]{};
        std::snprintf(number, sizeof(number), "0x%08x", node->number);
        next.number = number;
        char signal[32]{};
        std::snprintf(signal, sizeof(signal), "SNR %.1f dB", node->snr);
        next.signal = signal;
    }

    if (equal(next, presentation_)) return;
    presentation_ = std::move(next);
    publish_content();
}

std::uint32_t NodeDetailViewModel::selected_node() const noexcept
{
    return selected_node_;
}

const NodeDetailPresentation &NodeDetailViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *NodeDetailViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

void NodeDetailViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
