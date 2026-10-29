#ifndef LILYGO_UI_MESHTASTIC_MAP_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_MAP_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"
#include "maps/services/tile_service.hpp"

#include <functional>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct MapMarkerViewModel {
    std::string name;
    double latitude = 0.0, longitude = 0.0;
    bool local = false;
    std::uint32_t number = 0;
};
struct MapWaypointViewModel {
    std::uint32_t id = 0;
    std::string name;
    std::string description;
    meshmap::GeoPoint point;
    std::uint32_t expire = 0;
};
struct MapPathViewModel {
    std::uint32_t node_number = 0;
    bool route = false;
    std::vector<meshmap::GeoPoint> points;
};

class MapViewModel {
public:
    using LocationProvider = std::function<std::optional<meshmap::GeoPoint>()>;
    explicit MapViewModel(std::filesystem::path cache_root = meshmap::TileCache::default_root(),
                          LocationProvider location_provider = {});
    ~MapViewModel();
    MapViewModel(const MapViewModel &) = delete;
    MapViewModel &operator=(const MapViewModel &) = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);
    // The caller owns the client and must keep it alive until this model is
    // destroyed; the history worker can retain it while processing a request.
    void set_client(meshtastic::MeshtasticClient *client) noexcept;
    void set_active(bool active);
    void select_node(std::uint32_t number);
    void select_waypoint(std::uint32_t id);
    void follow_local();
    void follow_selected();
    void fit_nodes();
    void pan_pixels(double horizontal, double vertical);
    void zoom_in();
    void zoom_out();
    void search(std::string query);
    void clear_cache();
    std::vector<meshmap::VisibleTile> prepare_viewport(int width, int height, bool download = true);
    bool poll(meshmap::MapEvent &event);
    bool find_tile(const meshmap::TileId &tile, std::filesystem::path &path) const;

    [[nodiscard]] const std::vector<MapMarkerViewModel> &markers() const noexcept;
    [[nodiscard]] const std::vector<MapWaypointViewModel> &waypoints() const noexcept;
    [[nodiscard]] const std::vector<MapPathViewModel> &paths() const noexcept;
    [[nodiscard]] const meshmap::MapModel &model() const noexcept;
    [[nodiscard]] std::uint64_t viewport_generation() const noexcept;
    [[nodiscard]] std::uint32_t selected_node() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] bool following() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *viewport_subject() noexcept;
    [[nodiscard]] lv_subject_t *status_subject() noexcept;
    [[nodiscard]] lv_subject_t *selection_subject() noexcept;
    [[nodiscard]] lv_subject_t *cache_subject() noexcept;

private:
    enum class Follow { none, local, selected };
    struct HistoryJob {
        std::uint64_t generation = 0;
        std::string endpoint;
        std::uint32_t radio = 0;
        std::int64_t position_id = 0, waypoint_id = 0, route_id = 0;
    };
    struct HistoryResult {
        HistoryJob job;
        bool valid = false;
        std::vector<MapWaypointViewModel> waypoints;
        std::vector<MapPathViewModel> tracks;
        std::vector<meshtastic::RouteRecord> routes;
    };
    void publish_content();
    void viewport_changed();
    void update_status();
    void update_selection();
    void expire_waypoints();
    void center_follow(bool reset_zoom);
    void history_worker();
    void request_history(const meshtastic::ClientSnapshot &snapshot);
    bool consume_history();
    const MapMarkerViewModel *find_node(std::uint32_t number) const;

    meshmap::MapModel model_;
    meshmap::TileService service_;
    meshtastic::MeshtasticClient *client_ = nullptr;
    LocationProvider location_provider_;
    IntSubject content_revision_{0, 0};
    IntSubject viewport_revision_{0, 0};
    StringSubject<192> status_{"Offline map ready | No positioned nodes"};
    StringSubject<256> selection_;
    StringSubject<96> cache_;
    std::vector<MapMarkerViewModel> markers_;
    std::vector<MapWaypointViewModel> waypoints_;
    std::vector<MapPathViewModel> paths_;
    std::uint32_t local_node_ = 0, selected_node_ = 0, selected_waypoint_ = 0;
    std::uint64_t viewport_generation_ = 0, search_id_ = 0;
    int width_ = 568, height_ = 700;
    Follow follow_ = Follow::local;
    bool initialized_ = false, fit_ = false;
    bool pending_selected_center_ = false;
    bool active_ = false;
    bool history_dirty_ = false;
    bool history_loaded_ = false;
    std::string history_endpoint_;
    std::uint32_t history_radio_ = 0;
    std::int64_t latest_position_id_ = 0, latest_waypoint_id_ = 0, latest_route_id_ = 0;
    meshtastic::ClientSnapshot latest_snapshot_;
    std::vector<MapPathViewModel> history_tracks_;
    std::vector<MapWaypointViewModel> history_waypoints_;
    std::vector<meshtastic::RouteRecord> history_routes_;
    std::mutex history_mutex_;
    std::condition_variable history_changed_;
    std::optional<HistoryJob> history_job_;
    std::optional<HistoryResult> history_result_;
    std::thread history_thread_;
    std::uint64_t history_generation_ = 0;
    bool history_requested_ = false;
    bool history_stopping_ = false;
};
#endif
