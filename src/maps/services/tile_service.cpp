// Tile scheduling and cache consistency adapted from LILYGO UI Maps (MIT).
// See ../NOTICE.md for source and license details.
#include "maps/services/tile_service.hpp"

#include <json-c/json.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace meshmap {

TileService::TileService(std::filesystem::path root) : cache_(std::move(root))
{
    jobs_.push_back({JobKind::stats, 0, epoch_, {}, {}});
    for (int i = 0; i < 2; ++i) workers_.emplace_back([this] { worker(); });
}
TileService::~TileService()
{
    stopping_ = true;
    http_.cancel();
    changed_.notify_all();
    for (auto &thread : workers_) if (thread.joinable()) thread.join();
}
bool TileService::network_disabled() noexcept
{
    const char *value = std::getenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK");
    return value && std::string_view(value) == "1";
}
std::string TileService::key(const TileId &tile)
{
    return std::to_string(tile.zoom) + "/" + std::to_string(tile.x) + "/" + std::to_string(tile.y);
}
std::string TileService::tile_url(const TileId &tile)
{
    return "https://tile.openstreetmap.de/" + key(tile) + ".png";
}
std::string TileService::url_encode(std::string_view query)
{
    std::ostringstream output;
    output << std::hex << std::uppercase;
    for (const unsigned char c : query) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') output << c;
        else output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return output.str();
}
void TileService::request_tiles(std::uint64_t generation, const std::vector<TileId> &tiles)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (clearing_) return;
    // Discard downloads that have not started after the viewport moves. Work in
    // progress may still populate the cache; its old generation cannot paint UI.
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->kind == JobKind::tile) { pending_.erase(key(it->tile)); it = jobs_.erase(it); }
        else ++it;
    }
    for (const auto tile : tiles) {
        std::filesystem::path path;
        if (cache_.find(tile, path)) continue;
        auto found = pending_.find(key(tile));
        if (found != pending_.end()) { found->second = generation; continue; }
        // Existing disk files still need a full decode on a worker when offline.
        pending_[key(tile)] = generation;
        jobs_.push_back({JobKind::tile, generation, epoch_, tile, {}});
    }
    changed_.notify_all();
}
std::uint64_t TileService::search(std::string query)
{
    const auto id = ++search_id_;
    std::lock_guard<std::mutex> guard(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job &job) {
        return job.kind == JobKind::search;
    }), jobs_.end());
    jobs_.push_front({JobKind::search, id, epoch_, {}, std::move(query)});
    changed_.notify_one();
    return id;
}
void TileService::clear_cache()
{
    // Serialize the epoch update with commits so a completed old download can
    // never recreate files after the user has cleared the cache.
    std::lock_guard<std::mutex> commit(commit_mutex_);
    std::lock_guard<std::mutex> guard(mutex_);
    ++epoch_;
    clearing_ = true;
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job &job) {
        return job.kind == JobKind::tile;
    }), jobs_.end());
    pending_.clear();
    events_.erase(std::remove_if(events_.begin(), events_.end(), [](const MapEvent &event) {
        return event.kind == EventKind::tile_ready || event.kind == EventKind::tile_error;
    }), events_.end());
    jobs_.push_front({JobKind::clear, 0, epoch_, {}, {}});
    changed_.notify_one();
}
bool TileService::poll(MapEvent &event)
{
    std::lock_guard<std::mutex> guard(mutex_);
    while (!events_.empty()) {
        event = std::move(events_.front());
        events_.pop_front();
        if ((event.kind == EventKind::search_ready || event.kind == EventKind::search_error) &&
            event.generation != search_id_) continue;
        return true;
    }
    return false;
}
bool TileService::find_tile(const TileId &tile, std::filesystem::path &path) const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return !clearing_ && cache_.find(tile, path);
}
CacheStats TileService::cache_stats() const
{
    std::lock_guard<std::mutex> guard(mutex_);
    return {cache_bytes_, cache_tiles_};
}
void TileService::publish(MapEvent event)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!stopping_) events_.push_back(std::move(event));
}
void TileService::worker()
{
    while (!stopping_) {
        Job job;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            changed_.wait(guard, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front()); jobs_.pop_front();
        }
        run(std::move(job));
    }
}
void TileService::run(Job job)
{
    MapEvent event;
    event.generation = job.generation;
    event.tile = job.tile;
    if (job.kind == JobKind::stats) {
        std::uint64_t before;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            before = cache_mutation_;
        }
        const auto stats = cache_.stats();
        {
            std::lock_guard<std::mutex> guard(mutex_);
            // A clear or incremental write might finish while the scan runs.
            // In that case queue another scan after pending operations settle.
            if (job.epoch != epoch_ || cache_mutation_ != before) {
                jobs_.push_back({JobKind::stats, 0, epoch_, {}, {}});
                changed_.notify_one();
                return;
            }
            cache_bytes_ = stats.bytes;
            cache_tiles_ = stats.tiles;
        }
        event.kind = EventKind::cache_stats_ready;
        publish(std::move(event)); return;
    }
    if (job.kind == JobKind::clear) {
        std::lock_guard<std::mutex> commit(commit_mutex_);
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (job.epoch != epoch_) return;
        }
        event.kind = cache_.clear(event.error) ? EventKind::cache_cleared : EventKind::cache_error;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++cache_mutation_;
            clearing_ = false;
            if (event.kind == EventKind::cache_cleared) {
                cache_bytes_ = 0; cache_tiles_ = 0;
            } else {
                // remove_all can fail after removing part of the tree.
                jobs_.push_back({JobKind::stats, 0, epoch_, {}, {}});
                changed_.notify_one();
            }
        }
        publish(std::move(event)); return;
    }
    if (job.kind == JobKind::search) {
        event.kind = EventKind::search_error;
        if (network_disabled()) event.error = "Address search needs an internet connection";
        else {
            const auto response = http_.get("https://nominatim.openstreetmap.org/search?format=json&limit=1&q=" +
                                            url_encode(job.query), "application/json", 256U * 1024U);
            if (!response) event.error = "Address search is unavailable";
            else {
                json_object *root = json_tokener_parse(response.body.c_str());
                json_object *item = root && json_object_get_type(root) == json_type_array ?
                                    json_object_array_get_idx(root, 0) : nullptr;
                json_object *lat = nullptr, *lon = nullptr, *name = nullptr;
                if (item && json_object_object_get_ex(item, "lat", &lat) &&
                    json_object_object_get_ex(item, "lon", &lon)) {
                    const char *lat_text = json_object_get_string(lat);
                    const char *lon_text = json_object_get_string(lon);
                    char *lat_end = nullptr, *lon_end = nullptr;
                    if (lat_text && lon_text) event.point = {std::strtod(lat_text, &lat_end), std::strtod(lon_text, &lon_end)};
                    if (lat_text && *lat_text && lon_text && *lon_text && lat_end && !*lat_end &&
                        lon_end && !*lon_end && MapModel::valid_point(event.point)) {
                        event.kind = EventKind::search_ready;
                        if (json_object_object_get_ex(item, "display_name", &name))
                            event.label = json_object_get_string(name);
                    }
                }
                if (root) json_object_put(root);
                if (event.kind == EventKind::search_error) event.error = "No matching address found";
            }
        }
        publish(std::move(event)); return;
    }
    event.kind = EventKind::tile_error;
    const bool cached = cache_.validate(job.tile, event.path, event.error);
    HttpResponse response;
    bool verified_download = false;
    if (!cached && !network_disabled()) {
        response = http_.get(tile_url(job.tile), "image/png", 1024U * 1024U);
        verified_download = response && TileCache::valid_tile_png(response.body);
        event.error.clear();
        if (response && !verified_download) event.error = "Tile server returned an invalid 256 x 256 PNG";
    }
    {
        std::lock_guard<std::mutex> commit(commit_mutex_);
        std::lock_guard<std::mutex> guard(mutex_);
        if (job.epoch != epoch_) return;
        auto pending = pending_.find(key(job.tile));
        if (pending != pending_.end()) { event.generation = pending->second; pending_.erase(pending); }
        std::uint64_t previous_size = 0;
        bool previous_exists = false;
        if (cached) {
            event.kind = EventKind::tile_ready;
        } else if (verified_download &&
                   cache_.store_verified(job.tile, response.body, event.path, event.error, &previous_size, &previous_exists)) {
            event.kind = EventKind::tile_ready;
            if (!previous_exists) ++cache_tiles_;
            else if (previous_size <= cache_bytes_) cache_bytes_ -= previous_size;
            cache_bytes_ += response.body.size();
            ++cache_mutation_;
        } else if (event.error.empty()) event.error = "Map tiles unavailable; cached areas remain usable";
        if (!stopping_) events_.push_back(std::move(event));
    }
}

}  // namespace meshmap
