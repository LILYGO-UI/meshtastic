#ifndef LILYGO_MESHTASTIC_MAP_TILE_SERVICE_HPP
#define LILYGO_MESHTASTIC_MAP_TILE_SERVICE_HPP

#include "maps/domain/tile_cache.hpp"
#include "maps/services/http_client.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace meshmap {

enum class EventKind { tile_ready, tile_error, search_ready, search_error, cache_cleared, cache_error, cache_stats_ready };
struct MapEvent {
    EventKind kind = EventKind::tile_error;
    std::uint64_t generation = 0;
    TileId tile;
    std::filesystem::path path;
    GeoPoint point;
    std::string label;
    std::string error;
};

// Background work owns network and filesystem mutations. LVGL callers only poll
// completed values; no worker accesses a Subject or an LVGL object.
class TileService {
public:
    explicit TileService(std::filesystem::path cache_root = TileCache::default_root());
    ~TileService();
    TileService(const TileService &) = delete;
    TileService &operator=(const TileService &) = delete;

    void request_tiles(std::uint64_t generation, const std::vector<TileId> &tiles);
    std::uint64_t search(std::string query);
    void clear_cache();
    bool poll(MapEvent &event);
    bool find_tile(const TileId &tile, std::filesystem::path &path) const;
    CacheStats cache_stats() const;
    static bool network_disabled() noexcept;
    static std::string tile_url(const TileId &tile);
    static std::string url_encode(std::string_view query);

private:
    enum class JobKind { tile, search, clear, stats };
    struct Job {
        JobKind kind = JobKind::tile;
        std::uint64_t generation = 0;
        std::uint64_t epoch = 0;
        TileId tile;
        std::string query;
    };
    void worker();
    void run(Job job);
    void publish(MapEvent event);
    static std::string key(const TileId &tile);

    TileCache cache_;
    HttpClient http_;
    mutable std::mutex mutex_;
    std::mutex commit_mutex_;
    std::condition_variable changed_;
    std::deque<Job> jobs_;
    std::deque<MapEvent> events_;
    std::unordered_map<std::string, std::uint64_t> pending_;
    std::vector<std::thread> workers_;
    std::atomic_bool stopping_{false};
    std::atomic<std::uint64_t> search_id_{0};
    std::uint64_t cache_bytes_ = 0;
    std::uint64_t cache_tiles_ = 0;
    std::uint64_t epoch_ = 0;
    std::uint64_t cache_mutation_ = 0;
    bool clearing_ = false;
};

}  // namespace meshmap
#endif
