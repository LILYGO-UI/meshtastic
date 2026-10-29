// Adapted from LILYGO UI Maps; see ../NOTICE.md (MIT).
#ifndef LILYGO_MESHTASTIC_MAP_DOMAIN_TILE_CACHE_HPP
#define LILYGO_MESHTASTIC_MAP_DOMAIN_TILE_CACHE_HPP

#include "maps/domain/map_model.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace meshmap {

struct CacheStats {
    std::uint64_t bytes = 0;
    std::uint64_t tiles = 0;
};

class TileCache {
public:
    explicit TileCache(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path &root() const noexcept;
    [[nodiscard]] std::filesystem::path path_for(const TileId &tile) const;
    [[nodiscard]] bool find(const TileId &tile, std::filesystem::path &path) const;
    // Full PNG decoding is background work. find() exposes only files which
    // passed validate() or store() and still have the same filesystem identity.
    [[nodiscard]] bool validate(const TileId &tile, std::filesystem::path &path, std::string &error);
    [[nodiscard]] bool store(const TileId &tile, std::string_view png, std::filesystem::path &path,
                             std::string &error, std::uint64_t *previous_size = nullptr,
                             bool *previous_exists = nullptr);
    [[nodiscard]] CacheStats stats() const;
    [[nodiscard]] bool clear(std::string &error);

    [[nodiscard]] static bool valid_tile_png(std::string_view bytes) noexcept;
    [[nodiscard]] static std::filesystem::path default_root();

private:
    friend class TileService;
    // TileService validates network bytes before taking its commit lock.
    [[nodiscard]] bool store_verified(const TileId &tile, std::string_view png, std::filesystem::path &path,
                                      std::string &error, std::uint64_t *previous_size, bool *previous_exists);
    struct FileIdentity {
        std::uint64_t size = 0, inode = 0, device = 0;
        std::int64_t modified_seconds = 0, modified_nanos = 0, changed_seconds = 0, changed_nanos = 0;
        bool operator==(const FileIdentity &other) const noexcept;
    };
    [[nodiscard]] static bool file_identity(const std::filesystem::path &path, FileIdentity &identity) noexcept;

    std::filesystem::path root_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, FileIdentity> validated_;
    std::uint64_t revision_ = 0;
};

}  // namespace meshmap

#endif
