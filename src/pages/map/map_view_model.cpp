#include "pages/map/map_view_model.hpp"

#include <climits>
#include <utility>

MapViewModel::MapViewModel() : content_revision_(0, 0), status_("Offline map ready | No positioned nodes")
{
}

void MapViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    std::vector<MapMarkerViewModel> markers;
    for (const auto &node : snapshot.nodes) {
        if (!node.latitude || !node.longitude) continue;
        markers.push_back({node.long_name.empty() ? node.id : node.long_name, *node.latitude, *node.longitude,
                           node.number == snapshot.my_node_number});
    }

    bool changed = markers.size() != markers_.size();
    for (std::size_t index = 0; !changed && index < markers.size(); ++index) {
        const auto &left  = markers[index];
        const auto &right = markers_[index];
        changed = left.name != right.name || left.latitude != right.latitude || left.longitude != right.longitude ||
                  left.local != right.local;
    }
    if (changed) markers_ = std::move(markers);

    const auto status = "Offline map ready | " + std::to_string(markers_.size()) + " positioned nodes";
    status_.set(status.c_str());
    if (changed) publish_content();
}

const std::vector<MapMarkerViewModel> &MapViewModel::markers() const noexcept
{
    return markers_;
}

lv_subject_t *MapViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

lv_subject_t *MapViewModel::status_subject() noexcept
{
    return status_.get();
}

void MapViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}
