// Adapted from LILYGO UI Maps; see ../NOTICE.md (MIT).
#ifndef LILYGO_MESHTASTIC_MAP_DOMAIN_MAP_MODEL_HPP
#define LILYGO_MESHTASTIC_MAP_DOMAIN_MAP_MODEL_HPP

#include <vector>

namespace meshmap {

struct GeoPoint {
    double latitude  = 0.0;
    double longitude = 0.0;
};

struct TileId {
    int zoom = 0;
    int x    = 0;
    int y    = 0;

    [[nodiscard]] bool operator==(const TileId &other) const noexcept
    {
        return zoom == other.zoom && x == other.x && y == other.y;
    }
};

struct VisibleTile {
    TileId id;
    int screen_x = 0;
    int screen_y = 0;
};

struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
};

struct ScreenSegment {
    ScreenPoint first;
    ScreenPoint second;
};

class MapModel {
public:
    static constexpr int tile_size    = 256;
    static constexpr int minimum_zoom = 2;
    static constexpr int maximum_zoom = 18;

    MapModel() = default;

    [[nodiscard]] GeoPoint center() const noexcept;
    [[nodiscard]] int zoom() const noexcept;

    void center_on(GeoPoint point, int zoom) noexcept;
    void pan_pixels(double horizontal, double vertical) noexcept;
    void zoom_in() noexcept;
    void zoom_out() noexcept;
    void fit_points(const std::vector<GeoPoint> &points, int viewport_width, int viewport_height,
                    int padding = 56) noexcept;
    [[nodiscard]] ScreenPoint project(GeoPoint point, int viewport_width, int viewport_height) const noexcept;
    [[nodiscard]] std::vector<ScreenSegment> project_path(const std::vector<GeoPoint> &points,
                                                        int width, int height) const;
    [[nodiscard]] static bool valid_point(GeoPoint point) noexcept;

    [[nodiscard]] std::vector<VisibleTile> visible_tiles(int viewport_width, int viewport_height,
                                                         int margin_tiles = 1) const;

    [[nodiscard]] static double longitude_to_world_x(double longitude, int zoom) noexcept;
    [[nodiscard]] static double latitude_to_world_y(double latitude, int zoom) noexcept;
    [[nodiscard]] static double world_x_to_longitude(double world_x, int zoom) noexcept;
    [[nodiscard]] static double world_y_to_latitude(double world_y, int zoom) noexcept;

private:
    GeoPoint center_{};
    int zoom_ = 3;
};

}  // namespace meshmap

#endif
