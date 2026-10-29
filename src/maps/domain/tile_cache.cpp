// Adapted from LILYGO UI Maps; see ../NOTICE.md (MIT).
#include "maps/domain/tile_cache.hpp"

#include <png.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace meshmap {

namespace {

constexpr std::size_t kMaximumTileBytes = 1024U * 1024U;
constexpr std::array<unsigned char, 8> kPngSignature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

std::uint32_t read_big_endian(const unsigned char *value) noexcept
{
    return (static_cast<std::uint32_t>(value[0]) << 24U) | (static_cast<std::uint32_t>(value[1]) << 16U) |
           (static_cast<std::uint32_t>(value[2]) << 8U) | static_cast<std::uint32_t>(value[3]);
}

}  // namespace

TileCache::TileCache(std::filesystem::path root) : root_(std::move(root))
{
}

const std::filesystem::path &TileCache::root() const noexcept
{
    return root_;
}

std::filesystem::path TileCache::path_for(const TileId &tile) const
{
    return root_ / std::to_string(tile.zoom) / std::to_string(tile.x) / (std::to_string(tile.y) + ".png");
}

bool TileCache::find(const TileId &tile, std::filesystem::path &path) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    path = path_for(tile);
    const auto found = validated_.find(path.string());
    if (found == validated_.end()) return false;
    FileIdentity identity;
    if (file_identity(path, identity) && found->second == identity) return true;
    validated_.erase(found);
    return false;
}

bool TileCache::validate(const TileId &tile, std::filesystem::path &path, std::string &error)
{
    path = path_for(tile);
    std::uint64_t revision;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        revision = revision_;
    }
    FileIdentity before;
    if (!file_identity(path, before)) return false;
    if (before.size < 45U || before.size > kMaximumTileBytes) {
        error = "Cached map tile is not a valid 256 x 256 PNG";
        return false;
    }
    std::string bytes(static_cast<std::size_t>(before.size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !valid_tile_png(bytes)) {
        error = "Cached map tile could not be decoded";
        return false;
    }
    FileIdentity after;
    if (!file_identity(path, after) || !(before == after)) {
        error = "Cached map tile changed during validation";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision != revision_) return false;
    validated_[path.string()] = after;
    return true;
}

bool TileCache::store(const TileId &tile, std::string_view png, std::filesystem::path &path, std::string &error,
                      std::uint64_t *previous_size, bool *previous_exists)
{
    if (!valid_tile_png(png)) {
        error = "Tile server returned an invalid 256 x 256 PNG";
        return false;
    }
    return store_verified(tile, png, path, error, previous_size, previous_exists);
}

bool TileCache::store_verified(const TileId &tile, std::string_view png, std::filesystem::path &path,
                              std::string &error, std::uint64_t *previous_size, bool *previous_exists)
{
    std::lock_guard<std::mutex> lock(mutex_);
    path = path_for(tile);
    std::error_code filesystem_error;
    if (previous_size || previous_exists) {
        const auto size = std::filesystem::file_size(path, filesystem_error);
        if (previous_exists) *previous_exists = !filesystem_error;
        if (previous_size) *previous_size = filesystem_error ? 0 : size;
        filesystem_error.clear();
    }
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the map cache directory";
        return false;
    }

    auto temporary = path;
    temporary += ".part";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "Could not write a map tile";
            return false;
        }
        file.write(png.data(), static_cast<std::streamsize>(png.size()));
        if (!file.good()) {
            file.close();
            std::filesystem::remove(temporary, filesystem_error);
            error = "Could not finish writing a map tile";
            return false;
        }
    }

    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(path, filesystem_error);
        filesystem_error.clear();
        std::filesystem::rename(temporary, path, filesystem_error);
    }
    if (filesystem_error) {
        std::filesystem::remove(temporary, filesystem_error);
        error = "Could not commit a map tile to the cache";
        return false;
    }
    FileIdentity identity;
    if (file_identity(path, identity)) validated_[path.string()] = identity;
    return true;
}

CacheStats TileCache::stats() const
{
    // The root is immutable. Scanning without the cache lock keeps visible tile
    // reads responsive; TileService retries if a write overlaps this scan.
    CacheStats result;
    std::error_code error;
    if (!std::filesystem::exists(root_, error)) return result;
    for (std::filesystem::recursive_directory_iterator iterator(root_, error), end; iterator != end && !error;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != ".png") continue;
        const auto size = iterator->file_size(error);
        if (error) break;
        ++result.tiles;
        result.bytes += size;
    }
    return result;
}

bool TileCache::clear(std::string &error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto normalized_root = root_.lexically_normal();
    if (normalized_root.filename().empty()) normalized_root = normalized_root.parent_path();
    if (normalized_root.filename() != "openstreetmap") {
        error = "Map cache path must end with openstreetmap before it can be cleared";
        return false;
    }
    ++revision_;
    validated_.clear();
    std::error_code filesystem_error;
    std::filesystem::remove_all(root_, filesystem_error);
    if (filesystem_error) {
        error = "Could not clear the map cache";
        return false;
    }
    std::filesystem::create_directories(root_, filesystem_error);
    if (filesystem_error) {
        error = "Could not recreate the map cache directory";
        return false;
    }
    return true;
}

bool TileCache::valid_tile_png(std::string_view bytes) noexcept
{
    if (bytes.size() < 45U || bytes.size() > kMaximumTileBytes) return false;
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
    for (std::size_t index = 0; index < kPngSignature.size(); ++index) {
        if (data[index] != kPngSignature[index]) return false;
    }
    const auto end = bytes.size() - 12U;
    if (read_big_endian(data + 8) != 13U || data[12] != 'I' || data[13] != 'H' || data[14] != 'D' ||
        data[15] != 'R' || read_big_endian(data + end) != 0U || data[end + 4U] != 'I' || data[end + 5U] != 'E' ||
        data[end + 6U] != 'N' || data[end + 7U] != 'D') {
        return false;
    }
    if (read_big_endian(data + 16) != MapModel::tile_size || read_big_endian(data + 20) != MapModel::tile_size)
        return false;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size())) {
        png_image_free(&image);
        return false;
    }
    const bool dimensions = image.width == MapModel::tile_size && image.height == MapModel::tile_size;
    image.format = PNG_FORMAT_RGBA;
    // A tile has a fixed output size, so decoding cannot allocate an image of
    // dimensions supplied by untrusted network data. Preserve alpha as RGBA.
    bool decoded = false;
    if (dimensions) {
        try {
            std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
            decoded = png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0;
        } catch (...) {
            decoded = false;
        }
    }
    png_image_free(&image);
    return decoded;
}

std::filesystem::path TileCache::default_root()
{
    if (const char *configured = std::getenv("LILYGO_MESHTASTIC_MAP_CACHE_DIR"); configured && *configured) return configured;
    if (const char *xdg_cache = std::getenv("XDG_CACHE_HOME"); xdg_cache && *xdg_cache) {
        return std::filesystem::path(xdg_cache) / "lilygo-ui-meshtastic/maps/openstreetmap";
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".cache/lilygo-ui-meshtastic/maps/openstreetmap";
    }
    return "/tmp/lilygo-ui-meshtastic/maps/openstreetmap";
}

bool TileCache::FileIdentity::operator==(const FileIdentity &other) const noexcept
{
    return size == other.size && inode == other.inode && device == other.device &&
           modified_seconds == other.modified_seconds && modified_nanos == other.modified_nanos &&
           changed_seconds == other.changed_seconds && changed_nanos == other.changed_nanos;
}

bool TileCache::file_identity(const std::filesystem::path &path, FileIdentity &identity) noexcept
{
    struct stat value{};
    if (::stat(path.c_str(), &value) != 0 || !S_ISREG(value.st_mode) || value.st_size < 0) return false;
    identity.size = static_cast<std::uint64_t>(value.st_size);
    identity.inode = static_cast<std::uint64_t>(value.st_ino);
    identity.device = static_cast<std::uint64_t>(value.st_dev);
#if defined(__APPLE__)
    identity.modified_seconds = value.st_mtimespec.tv_sec;
    identity.modified_nanos = value.st_mtimespec.tv_nsec;
    identity.changed_seconds = value.st_ctimespec.tv_sec;
    identity.changed_nanos = value.st_ctimespec.tv_nsec;
#else
    identity.modified_seconds = value.st_mtim.tv_sec;
    identity.modified_nanos = value.st_mtim.tv_nsec;
    identity.changed_seconds = value.st_ctim.tv_sec;
    identity.changed_nanos = value.st_ctim.tv_nsec;
#endif
    return true;
}

}  // namespace meshmap
