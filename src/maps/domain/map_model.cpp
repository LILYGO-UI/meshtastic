// Adapted from LILYGO UI Maps; see ../NOTICE.md (MIT).
#include "maps/domain/map_model.hpp"

#include <algorithm>
#include <cmath>

namespace meshmap {

namespace {

constexpr double kMaximumLatitude = 85.05112878;
constexpr double kPi              = 3.14159265358979323846;

double map_size(int zoom) noexcept
{
    return static_cast<double>(MapModel::tile_size) * static_cast<double>(1U << zoom);
}

double wrap_world_x(double value, double size) noexcept
{
    value = std::fmod(value, size);
    return value < 0.0 ? value + size : value;
}

int positive_modulo(int value, int divisor) noexcept
{
    const int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

}  // namespace

GeoPoint MapModel::center() const noexcept
{
    return center_;
}

int MapModel::zoom() const noexcept
{
    return zoom_;
}

void MapModel::center_on(GeoPoint point, int zoom) noexcept
{
    if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude)) return;
    center_.latitude = std::clamp(point.latitude, -kMaximumLatitude, kMaximumLatitude);
    double longitude = std::fmod(point.longitude + 180.0, 360.0);
    if (longitude < 0.0) longitude += 360.0;
    center_.longitude = longitude - 180.0;
    zoom_             = std::clamp(zoom, minimum_zoom, maximum_zoom);
}

void MapModel::pan_pixels(double horizontal, double vertical) noexcept
{
    const double size    = map_size(zoom_);
    const double world_x = wrap_world_x(longitude_to_world_x(center_.longitude, zoom_) + horizontal, size);
    const double world_y = std::clamp(latitude_to_world_y(center_.latitude, zoom_) + vertical, 0.0, size - 1.0);
    center_              = {world_y_to_latitude(world_y, zoom_), world_x_to_longitude(world_x, zoom_)};
}

void MapModel::zoom_in() noexcept
{
    zoom_ = std::min(zoom_ + 1, maximum_zoom);
}

void MapModel::zoom_out() noexcept
{
    zoom_ = std::max(zoom_ - 1, minimum_zoom);
}

bool MapModel::valid_point(GeoPoint point) noexcept
{
    return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
           point.latitude >= -90.0 && point.latitude <= 90.0 &&
           point.longitude >= -180.0 && point.longitude <= 180.0;
}

ScreenPoint MapModel::project(GeoPoint point, int width, int height) const noexcept
{
    const double size = map_size(zoom_);
    double dx = longitude_to_world_x(point.longitude, zoom_) - longitude_to_world_x(center_.longitude, zoom_);
    if (dx > size * 0.5) dx -= size;
    if (dx < -size * 0.5) dx += size;
    return {width * 0.5 + dx, height * 0.5 + latitude_to_world_y(point.latitude, zoom_) -
                                              latitude_to_world_y(center_.latitude, zoom_)};
}

void MapModel::fit_points(const std::vector<GeoPoint> &points, int width, int height, int padding) noexcept
{
    std::vector<GeoPoint> valid;
    for (const auto point : points) if (valid_point(point)) valid.push_back(point);
    if (valid.empty()) return;
    if (valid.size() == 1) { center_on(valid.front(), 14); return; }
    // Remove the largest longitude gap so nodes on either side of the date line
    // occupy a small continuous interval instead of almost the entire world.
    std::vector<double> xs;
    for (const auto point : valid) xs.push_back(longitude_to_world_x(point.longitude, 0) / tile_size);
    std::sort(xs.begin(), xs.end());
    double largest_gap = -1.0;
    double start = xs.front();
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const double next = i + 1 < xs.size() ? xs[i + 1] : xs.front() + 1.0;
        if (next - xs[i] > largest_gap) { largest_gap = next - xs[i]; start = std::fmod(next, 1.0); }
    }
    const double span_x = 1.0 - largest_gap;
    double min_y = 1.0, max_y = 0.0;
    for (const auto point : valid) {
        const double y = latitude_to_world_y(point.latitude, 0) / tile_size;
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    int target = maximum_zoom;
    const double available_w = std::max(1, width - padding * 2);
    const double available_h = std::max(1, height - padding * 2);
    while (target > minimum_zoom && (span_x * map_size(target) > available_w ||
                                    (max_y - min_y) * map_size(target) > available_h)) --target;
    center_on({world_y_to_latitude((min_y + max_y) * 0.5 * tile_size, 0),
               world_x_to_longitude((start + span_x * 0.5) * tile_size, 0)}, std::min(16, target));
}

std::vector<ScreenSegment> MapModel::project_path(const std::vector<GeoPoint> &points, int width, int height) const
{
    std::vector<ScreenSegment> result;
    if (width <= 0 || height <= 0) return result;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!valid_point(points[i - 1]) || !valid_point(points[i])) continue;
        auto a = project(points[i - 1], width, height);
        auto b = project(points[i], width, height);
        // Keep a path crossing the date line continuous within this viewport.
        const double size = map_size(zoom_);
        if (b.x - a.x > size * 0.5) b.x -= size;
        if (b.x - a.x < -size * 0.5) b.x += size;
        const double dx = b.x - a.x, dy = b.y - a.y;
        double enter = 0.0, leave = 1.0;
        bool visible = true;
        const double p[] = {-dx, dx, -dy, dy};
        const double q[] = {a.x, width - 1.0 - a.x, a.y, height - 1.0 - a.y};
        for (int edge = 0; edge < 4 && visible; ++edge) {
            if (p[edge] == 0.0) { visible = q[edge] >= 0.0; continue; }
            const double t = q[edge] / p[edge];
            if (p[edge] < 0.0) enter = std::max(enter, t); else leave = std::min(leave, t);
            visible = enter <= leave;
        }
        if (visible) result.push_back({{a.x + enter * dx, a.y + enter * dy},
                                       {a.x + leave * dx, a.y + leave * dy}});
    }
    return result;
}

std::vector<VisibleTile> MapModel::visible_tiles(int viewport_width, int viewport_height, int margin_tiles) const
{
    if (viewport_width <= 0 || viewport_height <= 0) return {};

    margin_tiles             = std::max(0, margin_tiles);
    const int tiles_per_axis = 1 << zoom_;
    const double center_x    = longitude_to_world_x(center_.longitude, zoom_);
    const double center_y    = latitude_to_world_y(center_.latitude, zoom_);
    const double origin_x    = center_x - static_cast<double>(viewport_width) / 2.0;
    const double origin_y    = center_y - static_cast<double>(viewport_height) / 2.0;
    const int first_x        = static_cast<int>(std::floor(origin_x / tile_size)) - margin_tiles;
    const int last_x = static_cast<int>(std::floor((origin_x + viewport_width - 1.0) / tile_size)) + margin_tiles;
    const int first_y = std::max(0, static_cast<int>(std::floor(origin_y / tile_size)) - margin_tiles);
    const int last_y  = std::min(tiles_per_axis - 1,
                                static_cast<int>(std::floor((origin_y + viewport_height - 1.0) / tile_size)) +
                                    margin_tiles);

    std::vector<VisibleTile> result;
    result.reserve(static_cast<std::size_t>(last_x - first_x + 1) * static_cast<std::size_t>(last_y - first_y + 1));
    for (int raw_y = first_y; raw_y <= last_y; ++raw_y) {
        for (int raw_x = first_x; raw_x <= last_x; ++raw_x) {
            result.push_back({{zoom_, positive_modulo(raw_x, tiles_per_axis), raw_y},
                              static_cast<int>(std::lround(raw_x * tile_size - origin_x)),
                              static_cast<int>(std::lround(raw_y * tile_size - origin_y))});
        }
    }
    return result;
}

double MapModel::longitude_to_world_x(double longitude, int zoom) noexcept
{
    return (longitude + 180.0) / 360.0 * map_size(zoom);
}

double MapModel::latitude_to_world_y(double latitude, int zoom) noexcept
{
    latitude           = std::clamp(latitude, -kMaximumLatitude, kMaximumLatitude);
    const double angle = latitude * kPi / 180.0;
    return (1.0 - std::asinh(std::tan(angle)) / kPi) * 0.5 * map_size(zoom);
}

double MapModel::world_x_to_longitude(double world_x, int zoom) noexcept
{
    return wrap_world_x(world_x, map_size(zoom)) / map_size(zoom) * 360.0 - 180.0;
}

double MapModel::world_y_to_latitude(double world_y, int zoom) noexcept
{
    const double normalized = 1.0 - 2.0 * std::clamp(world_y / map_size(zoom), 0.0, 1.0);
    return std::atan(std::sinh(kPi * normalized)) * 180.0 / kPi;
}

}  // namespace meshmap
