#include "pages/map/map_view_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace {
void bump(IntSubject &subject) { subject.set(subject.value() == INT32_MAX ? 0 : subject.value() + 1); }
std::string coordinates(meshmap::GeoPoint point)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(5) << point.latitude << ", " << point.longitude;
    return text.str();
}
}  // namespace

MapViewModel::MapViewModel(std::filesystem::path root, LocationProvider provider)
    : service_(std::move(root)), location_provider_(std::move(provider))
{
    history_thread_ = std::thread([this] { history_worker(); });
    update_status();
}
MapViewModel::~MapViewModel()
{
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        history_stopping_ = true;
    }
    history_changed_.notify_all();
    if (history_thread_.joinable()) history_thread_.join();
}
void MapViewModel::set_client(meshtastic::MeshtasticClient *client) noexcept
{
    std::lock_guard<std::mutex> lock(history_mutex_);
    client_ = client;
}
void MapViewModel::set_active(bool active)
{
    if (active == active_) return;
    active_ = active;
    if (!active_) {
        std::lock_guard<std::mutex> lock(history_mutex_);
        ++history_generation_;
        history_requested_ = false;
        history_job_.reset();
        history_result_.reset();
    } else {
        sync(latest_snapshot_);
    }
}
void MapViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    latest_snapshot_ = snapshot;
    const bool changed_radio = (!history_endpoint_.empty() && history_endpoint_ != snapshot.endpoint) ||
                               (snapshot.my_node_number && history_radio_ &&
                                snapshot.my_node_number != history_radio_);
    if (changed_radio) {
        history_loaded_ = false;
        history_tracks_.clear();
        history_waypoints_.clear();
        history_routes_.clear();
        latest_position_id_ = latest_waypoint_id_ = latest_route_id_ = 0;
        selected_node_ = selected_waypoint_ = 0;
        initialized_ = fit_ = false;
        pending_selected_center_ = false;
        follow_ = Follow::local;
        model_ = meshmap::MapModel{};
        search_id_ = 0;
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            ++history_generation_;
            history_requested_ = false;
            history_job_.reset();
            history_result_.reset();
        }
    }
    history_endpoint_ = snapshot.endpoint;
    if (snapshot.my_node_number) history_radio_ = snapshot.my_node_number;
    if (client_ && !active_) return;
    const auto old_center = model_.center();
    const auto old_zoom = model_.zoom();
    auto previous_markers = std::move(markers_);
    markers_.clear();
    if (snapshot.my_node_number) local_node_ = snapshot.my_node_number;
    for (const auto &node : snapshot.nodes) {
        if (!node.latitude || !node.longitude ||
            !meshmap::MapModel::valid_point({*node.latitude, *node.longitude})) continue;
        markers_.push_back({node.long_name.empty() ? node.id : node.long_name, *node.latitude, *node.longitude,
                            node.number == local_node_, node.number});
    }
    const bool markers_changed = previous_markers.size() != markers_.size() ||
        !std::equal(previous_markers.begin(), previous_markers.end(), markers_.begin(),
            [](const auto &a, const auto &b) {
                return a.number == b.number && a.name == b.name && a.latitude == b.latitude &&
                       a.longitude == b.longitude && a.local == b.local;
            });
    if (client_ && active_) request_history(snapshot);
    consume_history();
    if (client_ && history_loaded_ && !history_dirty_ && !markers_changed && !changed_radio) return;
    history_dirty_ = false;
    auto previous_waypoints = std::move(waypoints_);
    auto previous_paths = std::move(paths_);
    waypoints_.clear(); paths_.clear();
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    std::map<std::uint32_t, MapWaypointViewModel> latest_waypoints;
    std::vector<meshtastic::WaypointRecord> waypoint_records = snapshot.waypoints;
    std::vector<MapPathViewModel> track_paths;
    auto route_records = snapshot.routes;
    if (client_ && history_loaded_) {
        waypoint_records.clear();
        for (const auto &item : history_waypoints_) {
            meshtastic::WaypointRecord record;
            record.waypoint.set_id(item.id);
            record.waypoint.set_name(item.name);
            record.waypoint.set_description(item.description);
            record.waypoint.set_latitude_i(static_cast<std::int32_t>(std::llround(item.point.latitude * 1e7)));
            record.waypoint.set_longitude_i(static_cast<std::int32_t>(std::llround(item.point.longitude * 1e7)));
            record.waypoint.set_expire(item.expire);
            waypoint_records.push_back(std::move(record));
        }
        track_paths = history_tracks_;
        route_records = history_routes_;
    }
    std::stable_sort(waypoint_records.begin(), waypoint_records.end(), [](const auto &a, const auto &b) {
        return a.local_id < b.local_id;
    });
    for (const auto &record : waypoint_records) {
        const auto &waypoint = record.waypoint;
        // A later deletion, expiry, or invalid update must replace its previous
        // value too; retaining an older point would display a stale waypoint.
        latest_waypoints.erase(waypoint.id());
        if (!waypoint.has_latitude_i() || !waypoint.has_longitude_i()) continue;
        if (waypoint.expire() != 0 && waypoint.expire() <= now) continue;
        const meshmap::GeoPoint point{waypoint.latitude_i() * 1e-7, waypoint.longitude_i() * 1e-7};
        if (!meshmap::MapModel::valid_point(point)) continue;
        latest_waypoints[waypoint.id()] = {waypoint.id(), waypoint.name().empty() ? "Waypoint" : waypoint.name(),
                                            waypoint.description(), point, waypoint.expire()};
    }
    for (auto &entry : latest_waypoints) waypoints_.push_back(std::move(entry.second));
    if (!track_paths.empty()) paths_.insert(paths_.end(), track_paths.begin(), track_paths.end());
    if (!client_ || !history_loaded_) {
        std::map<std::uint32_t, std::vector<meshmap::GeoPoint>> tracks;
        for (const auto &record : snapshot.positions) {
            if (!record.position.has_latitude_i() || !record.position.has_longitude_i()) continue;
            const meshmap::GeoPoint point{record.position.latitude_i() * 1e-7, record.position.longitude_i() * 1e-7};
            if (meshmap::MapModel::valid_point(point)) tracks[record.node_number].push_back(point);
        }
        for (auto &entry : tracks) if (entry.second.size() > 1) paths_.push_back({entry.first, false, std::move(entry.second)});
    }
    // Unknown traceroute hops break the line rather than inventing locations.
    auto routes = route_records;
    for (const auto &route : routes) {
        // An outgoing or unsolicited request does not establish a route. Draw
        // only a response to a previously issued trace request.
        if (route.outgoing || route.request_id == 0) continue;
        const auto origin = route.to;
        const auto target = route.from;
        MapPathViewModel path{origin, true, {}};
        std::vector<std::uint32_t> nodes{origin};
        for (const auto number : route.route.route()) nodes.push_back(number);
        nodes.push_back(target);
        for (const auto number : nodes) {
            const auto *node = find_node(number);
            if (node) path.points.push_back({node->latitude, node->longitude});
            else { if (path.points.size() > 1) paths_.push_back(path); path.points.clear(); }
        }
        if (path.points.size() > 1) paths_.push_back(std::move(path));
    }
    if (!client_) history_loaded_ = true;
    const bool waypoints_changed = previous_waypoints.size() != waypoints_.size() ||
        !std::equal(previous_waypoints.begin(), previous_waypoints.end(), waypoints_.begin(),
                    [](const auto &left, const auto &right) {
            return left.id == right.id && left.name == right.name && left.description == right.description &&
                   left.expire == right.expire && left.point.latitude == right.point.latitude &&
                   left.point.longitude == right.point.longitude;
        });
    const bool paths_changed = previous_paths.size() != paths_.size() ||
        !std::equal(previous_paths.begin(), previous_paths.end(), paths_.begin(),
                    [](const auto &left, const auto &right) {
            return left.node_number == right.node_number && left.route == right.route &&
                   left.points.size() == right.points.size() &&
                   std::equal(left.points.begin(), left.points.end(), right.points.begin(),
                              [](const auto &a, const auto &b) {
                       return a.latitude == b.latitude && a.longitude == b.longitude;
                   });
        });
    const bool changed = markers_changed || waypoints_changed || paths_changed;
    if (!initialized_ && !markers_.empty()) {
        if (follow_ == Follow::selected && selected_node_) {
            if (find_node(selected_node_)) {
                initialized_ = true;
                center_follow(true);
                pending_selected_center_ = false;
            }
        } else {
            initialized_ = true;
            if (find_node(local_node_)) { follow_ = Follow::local; center_follow(true); }
            else fit_nodes();
        }
    } else if (follow_ != Follow::none) {
        const bool center_selected = follow_ == Follow::selected && pending_selected_center_ &&
                                     find_node(selected_node_);
        center_follow(center_selected);
        if (center_selected) pending_selected_center_ = false;
    }
    update_selection();
    if (changed) { update_status(); publish_content(); }
    if (old_center.latitude != model_.center().latitude || old_center.longitude != model_.center().longitude ||
        old_zoom != model_.zoom())
        viewport_changed();
}
const MapMarkerViewModel *MapViewModel::find_node(std::uint32_t number) const
{
    const auto found = std::find_if(markers_.begin(), markers_.end(), [number](const auto &item) { return item.number == number; });
    return found == markers_.end() ? nullptr : &*found;
}
void MapViewModel::select_node(std::uint32_t number)
{
    selected_node_ = number; selected_waypoint_ = 0;
    update_selection(); publish_content();
}
void MapViewModel::select_waypoint(std::uint32_t id)
{
    selected_waypoint_ = id; selected_node_ = 0; follow_ = Follow::none; pending_selected_center_ = false;
    update_selection(); publish_content();
}
void MapViewModel::center_follow(bool reset_zoom)
{
    const auto *node = find_node(follow_ == Follow::selected ? selected_node_ : local_node_);
    if (node) { model_.center_on({node->latitude, node->longitude}, reset_zoom ? 15 : model_.zoom()); fit_ = false; return; }
    if (follow_ == Follow::local && location_provider_) {
        if (const auto point = location_provider_(); point && meshmap::MapModel::valid_point(*point)) {
            model_.center_on(*point, reset_zoom ? 15 : model_.zoom()); fit_ = false;
        }
    }
}
void MapViewModel::follow_local()
{
    follow_ = Follow::local; search_id_ = 0; pending_selected_center_ = false;
    if (!find_node(local_node_) && !location_provider_) { fit_nodes(); return; }
    center_follow(true); update_status(); viewport_changed();
}
void MapViewModel::follow_selected()
{
    if (!selected_node_) return;
    follow_ = Follow::selected; fit_ = false; search_id_ = 0;
    pending_selected_center_ = !find_node(selected_node_);
    if (pending_selected_center_) return;
    center_follow(true); update_status(); viewport_changed();
}
void MapViewModel::fit_nodes()
{
    follow_ = Follow::none; fit_ = true; search_id_ = 0; pending_selected_center_ = false;
    std::vector<meshmap::GeoPoint> points;
    for (const auto &node : markers_) points.push_back({node.latitude, node.longitude});
    for (const auto &waypoint : waypoints_) points.push_back(waypoint.point);
    model_.fit_points(points, width_, height_); update_status(); viewport_changed();
}
void MapViewModel::pan_pixels(double x, double y)
{
    follow_ = Follow::none; fit_ = false; search_id_ = 0; pending_selected_center_ = false;
    model_.pan_pixels(x, y); update_status(); viewport_changed();
}
void MapViewModel::zoom_in() { fit_ = false; search_id_ = 0; model_.zoom_in(); viewport_changed(); }
void MapViewModel::zoom_out() { fit_ = false; search_id_ = 0; model_.zoom_out(); viewport_changed(); }
void MapViewModel::search(std::string query)
{
    const auto first = query.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { status_.set("Enter an address to search"); return; }
    query = query.substr(first, query.find_last_not_of(" \t\r\n") - first + 1);
    follow_ = Follow::none; fit_ = false; pending_selected_center_ = false; status_.set("Searching for address…");
    search_id_ = service_.search(std::move(query));
}
void MapViewModel::clear_cache() { status_.set("Clearing cached tiles…"); service_.clear_cache(); }
std::vector<meshmap::VisibleTile> MapViewModel::prepare_viewport(int width, int height, bool download)
{
    if ((width_ != width || height_ != height) && fit_) {
        std::vector<meshmap::GeoPoint> points;
        for (const auto &node : markers_) points.push_back({node.latitude, node.longitude});
        for (const auto &waypoint : waypoints_) points.push_back(waypoint.point);
        model_.fit_points(points, width, height);
    }
    width_ = width; height_ = height;
    auto visible = model_.visible_tiles(width, height);
    ++viewport_generation_;
    if (download) {
        auto ordered = visible;
        std::stable_sort(ordered.begin(), ordered.end(), [width,height](const auto &a, const auto &b) {
            const double ax = a.screen_x + 128 - width / 2, ay = a.screen_y + 128 - height / 2;
            const double bx = b.screen_x + 128 - width / 2, by = b.screen_y + 128 - height / 2;
            return ax * ax + ay * ay < bx * bx + by * by;
        });
        std::vector<meshmap::TileId> tiles;
        for (const auto &tile : ordered) tiles.push_back(tile.id);
        service_.request_tiles(viewport_generation_, tiles);
    }
    return visible;
}
bool MapViewModel::poll(meshmap::MapEvent &event)
{
    if (active_ && consume_history()) {
        const auto snapshot = latest_snapshot_;
        sync(snapshot);
    }
    expire_waypoints();
    if (!service_.poll(event)) return false;
    switch (event.kind) {
        case meshmap::EventKind::search_ready:
            if (event.generation == search_id_) {
                search_id_ = 0; model_.center_on(event.point, 14); viewport_changed(); status_.set(event.label.c_str());
            }
            break;
        case meshmap::EventKind::search_error:
            if (event.generation == search_id_) { search_id_ = 0; status_.set(event.error.c_str()); }
            break;
        case meshmap::EventKind::tile_ready: update_status(); break;
        case meshmap::EventKind::cache_cleared: update_status(); status_.set("Cached tiles cleared"); break;
        case meshmap::EventKind::cache_stats_ready: update_status(); break;
        case meshmap::EventKind::tile_error: status_.set(event.error.c_str()); break;
        case meshmap::EventKind::cache_error: status_.set(event.error.c_str()); break;
    }
    return true;
}
void MapViewModel::expire_waypoints()
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const auto old_count = waypoints_.size();
    waypoints_.erase(std::remove_if(waypoints_.begin(), waypoints_.end(), [now](const auto &waypoint) {
        return waypoint.expire != 0 && waypoint.expire <= now;
    }), waypoints_.end());
    if (old_count != waypoints_.size()) { update_selection(); publish_content(); }
}
void MapViewModel::request_history(const meshtastic::ClientSnapshot &snapshot)
{
    if (!client_ || !active_ || !snapshot.my_node_number) return;
    HistoryJob job;
    job.endpoint = snapshot.endpoint;
    job.radio = snapshot.my_node_number;
    for (const auto &record : snapshot.positions) job.position_id = std::max(job.position_id, record.local_id);
    for (const auto &record : snapshot.waypoints) job.waypoint_id = std::max(job.waypoint_id, record.local_id);
    for (const auto &record : snapshot.routes) job.route_id = std::max(job.route_id, record.local_id);
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (history_job_ && history_job_->endpoint == job.endpoint && history_job_->radio == job.radio &&
        history_job_->position_id == job.position_id && history_job_->waypoint_id == job.waypoint_id &&
        history_job_->route_id == job.route_id)
        return;
    if (history_loaded_ && history_endpoint_ == job.endpoint && history_radio_ == job.radio &&
        latest_position_id_ == job.position_id && latest_waypoint_id_ == job.waypoint_id &&
        latest_route_id_ == job.route_id)
        return;
    job.generation = ++history_generation_;
    history_job_ = job;
    history_requested_ = true;
    history_changed_.notify_one();
}
bool MapViewModel::consume_history()
{
    std::optional<HistoryResult> result;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        if (history_result_) {
            result = std::move(history_result_);
            history_result_.reset();
            if (history_job_ && history_job_->generation == result->job.generation)
                history_job_.reset();
        }
    }
    if (!result || !active_) return false;
    if (result->job.generation != history_generation_ || result->job.endpoint != history_endpoint_ ||
        result->job.radio != history_radio_) return false;
    if (!result->valid) {
        // The repository can change radio between the UI snapshot and this
        // worker's read. Retry the current scope instead of leaving a completed
        // job permanently marked as in flight.
        request_history(latest_snapshot_);
        return false;
    }
    history_waypoints_ = std::move(result->waypoints);
    history_tracks_ = std::move(result->tracks);
    history_routes_ = std::move(result->routes);
    latest_position_id_ = result->job.position_id;
    latest_waypoint_id_ = result->job.waypoint_id;
    latest_route_id_ = result->job.route_id;
    history_loaded_ = true;
    history_dirty_ = true;
    return true;
}
void MapViewModel::history_worker()
{
    while (true) {
        HistoryJob job;
        meshtastic::MeshtasticClient *client = nullptr;
        {
            std::unique_lock<std::mutex> lock(history_mutex_);
            history_changed_.wait(lock, [this] { return history_stopping_ || history_requested_; });
            if (history_stopping_) return;
            // Coalesce bursts while bounding the delay for a continuously busy mesh.
            history_changed_.wait_for(lock, std::chrono::milliseconds(120), [this] { return history_stopping_; });
            if (history_stopping_) return;
            if (!history_requested_ || !history_job_) continue;
            job = *history_job_;
            history_requested_ = false;
            client = client_;
        }
        HistoryResult result;
        result.job = job;
        result.valid = client != nullptr;
        if (result.valid) {
            auto history = client->query_map_history(job.endpoint, job.radio);
            result.valid = history.has_value();
            if (!result.valid) {
                // Publish the failed scope check so the UI thread can retry.
                std::lock_guard<std::mutex> lock(history_mutex_);
                if (!history_stopping_ && result.job.generation == history_generation_)
                    history_result_ = std::move(result);
                continue;
            }
            result.routes = std::move(history->routes);
            const auto now = static_cast<std::uint64_t>(std::time(nullptr));
            std::map<std::uint32_t, MapWaypointViewModel> latest;
            for (const auto &record : history->waypoints) {
                const auto &waypoint = record.waypoint;
                latest.erase(waypoint.id());
                if (!waypoint.has_latitude_i() || !waypoint.has_longitude_i() ||
                    (waypoint.expire() && waypoint.expire() <= now))
                    continue;
                const meshmap::GeoPoint point{waypoint.latitude_i() * 1e-7, waypoint.longitude_i() * 1e-7};
                if (meshmap::MapModel::valid_point(point))
                    latest[waypoint.id()] = {waypoint.id(), waypoint.name().empty() ? "Waypoint" : waypoint.name(),
                                              waypoint.description(), point, waypoint.expire()};
            }
            for (auto &entry : latest) result.waypoints.push_back(std::move(entry.second));
            std::map<std::uint32_t, std::vector<meshmap::GeoPoint>> tracks;
            for (const auto &record : history->positions) {
                if (!record.position.has_latitude_i() || !record.position.has_longitude_i()) continue;
                const meshmap::GeoPoint point{record.position.latitude_i() * 1e-7, record.position.longitude_i() * 1e-7};
                if (meshmap::MapModel::valid_point(point)) tracks[record.node_number].push_back(point);
            }
            const auto per_track = std::max<std::size_t>(2, 2048 / std::max<std::size_t>(1, tracks.size()));
            for (auto &entry : tracks) {
                if (entry.second.size() < 2) continue;
                if (entry.second.size() > per_track) {
                    std::vector<meshmap::GeoPoint> sampled;
                    sampled.reserve(per_track);
                    for (std::size_t i = 0; i < per_track; ++i)
                        sampled.push_back(entry.second[i * (entry.second.size() - 1) / (per_track - 1)]);
                    entry.second = std::move(sampled);
                }
                result.tracks.push_back({entry.first, false, std::move(entry.second)});
            }
        }
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            if (!history_stopping_ && result.job.generation == history_generation_)
                history_result_ = std::move(result);
        }
    }
}
bool MapViewModel::find_tile(const meshmap::TileId &tile, std::filesystem::path &path) const { return service_.find_tile(tile, path); }
void MapViewModel::update_status()
{
    const auto stats = service_.cache_stats();
    std::ostringstream text;
    text << (meshmap::TileService::network_disabled() ? "Offline map ready | " : "OpenStreetMap | ")
         << markers_.size() << " positioned nodes";
    status_.set(text.str().c_str());
    text.str(""); text.clear();
    text << "Z" << model_.zoom() << " · " << stats.tiles << " cached tiles · "
         << std::fixed << std::setprecision(1) << static_cast<double>(stats.bytes) / (1024.0 * 1024.0) << " MB";
    if (follow_ == Follow::local && find_node(local_node_)) text << " · Following local";
    if (follow_ == Follow::selected) text << " · Following node";
    cache_.set(text.str().c_str());
}
void MapViewModel::update_selection()
{
    const auto *node = find_node(selected_node_);
    if (node) { selection_.set((node->name + "\n" + coordinates({node->latitude, node->longitude})).c_str()); return; }
    for (const auto &waypoint : waypoints_) if (waypoint.id == selected_waypoint_) {
        selection_.set((waypoint.name + " · " + coordinates(waypoint.point) +
                       (waypoint.description.empty() ? "" : "\n" + waypoint.description)).c_str()); return;
    }
    selection_.set("");
}
void MapViewModel::publish_content() { bump(content_revision_); }
void MapViewModel::viewport_changed() { update_status(); bump(viewport_revision_); }
const std::vector<MapMarkerViewModel> &MapViewModel::markers() const noexcept { return markers_; }
const std::vector<MapWaypointViewModel> &MapViewModel::waypoints() const noexcept { return waypoints_; }
const std::vector<MapPathViewModel> &MapViewModel::paths() const noexcept { return paths_; }
const meshmap::MapModel &MapViewModel::model() const noexcept { return model_; }
std::uint64_t MapViewModel::viewport_generation() const noexcept { return viewport_generation_; }
std::uint32_t MapViewModel::selected_node() const noexcept { return selected_node_; }
bool MapViewModel::has_selection() const noexcept { return selection_.value()[0] != '\0'; }
bool MapViewModel::following() const noexcept { return follow_ != Follow::none; }
lv_subject_t *MapViewModel::content_subject() noexcept { return content_revision_.get(); }
lv_subject_t *MapViewModel::viewport_subject() noexcept { return viewport_revision_.get(); }
lv_subject_t *MapViewModel::status_subject() noexcept { return status_.get(); }
lv_subject_t *MapViewModel::selection_subject() noexcept { return selection_.get(); }
lv_subject_t *MapViewModel::cache_subject() noexcept { return cache_.get(); }
