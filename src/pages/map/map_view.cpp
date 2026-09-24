#include "pages/map/map_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>

namespace {

lv_obj_t *make_map_line(lv_obj_t *parent, int width, int y, std::uint32_t color, int rotation)
{
    lv_obj_t *line = lv_obj_create(parent);
    style_container(line, color, 2);
    lv_obj_set_size(line, LV_PCT(width), 3);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_transform_rotation(line, rotation, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

}  // namespace

MapView::MapView(MapViewModel &view_model) noexcept : view_model_(view_model)
{
}

MapView::~MapView()
{
    destroy();
}

lv_obj_t *MapView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Map", "Options", nullptr, nullptr, false);

    lv_obj_t *map = lv_obj_create(page_);
    lv_obj_set_name_static(map, "map_canvas");
    style_container(map, COLOR_MAP_BACKGROUND, 0);
    lv_obj_set_width(map, LV_PCT(100));
    lv_obj_set_height(map, 0);
    lv_obj_set_flex_grow(map, 1);
    lv_obj_set_style_pad_all(map, 0, 0);
    lv_obj_clear_flag(map, LV_OBJ_FLAG_SCROLLABLE);
    make_map_line(map, 95, 80, COLOR_MAP_CONTOUR, 35);
    make_map_line(map, 110, 180, COLOR_MAP_CONTOUR, -28);
    make_map_line(map, 105, 300, COLOR_MAP_WATER, 12);
    make_map_line(map, 92, 420, COLOR_MAP_CONTOUR, -18);

    lv_obj_t *zoom = make_flex(map, LV_FLEX_FLOW_COLUMN);
    style_container(zoom, COLOR_SURFACE, 14);
    lv_obj_set_size(zoom, 50, 102);
    lv_obj_set_style_pad_all(zoom, 0, 0);
    lv_obj_set_style_pad_row(zoom, 0, 0);
    lv_obj_align(zoom, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_t *zoom_in = make_text_button(zoom, "+", COLOR_TITLE, COLOR_SURFACE, nullptr, nullptr);
    lv_obj_set_size(zoom_in, 50, 50);
    lv_obj_t *zoom_out = make_text_button(zoom, "-", COLOR_TITLE, COLOR_SURFACE, nullptr, nullptr);
    lv_obj_set_size(zoom_out, 50, 50);

    lv_obj_t *locate = make_text_button(map, LV_SYMBOL_GPS, COLOR_BLUE, COLOR_SURFACE, nullptr, nullptr);
    lv_obj_set_size(locate, 50, 50);
    lv_obj_align(locate, LV_ALIGN_TOP_RIGHT, -12, 126);

    markers_ = lv_obj_create(map);
    lv_obj_set_name_static(markers_, "map_markers");
    style_container(markers_, COLOR_MAP_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(markers_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(markers_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(markers_, 0, 0);
    lv_obj_clear_flag(markers_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = make_card(map);
    lv_obj_set_width(status, LV_PCT(94));
    lv_obj_set_style_bg_opa(status, LV_OPA_90, 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -14);
    make_label(status, "Offline map ready", 14, COLOR_TITLE);
    lv_obj_t *detail = make_wrapped_label(status, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(detail, view_model_.status_subject(), nullptr);

    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, markers_, this);
    return page_;
}

void MapView::destroy() noexcept
{
    destroy_page_body(page_);
    markers_ = nullptr;
}

void MapView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<MapView *>(lv_observer_get_user_data(observer));
    if (view) view->render_markers();
}

void MapView::render_markers()
{
    if (!markers_) return;
    lv_obj_clean(markers_);
    const auto &items = view_model_.markers();
    if (items.empty()) {
        lv_obj_t *empty = make_label(markers_, "No positioned nodes", 14, COLOR_SECONDARY);
        lv_obj_center(empty);
        return;
    }

    double min_lat = items.front().latitude;
    double max_lat = items.front().latitude;
    double min_lon = items.front().longitude;
    double max_lon = items.front().longitude;
    for (const auto &item : items) {
        min_lat = std::min(min_lat, item.latitude);
        max_lat = std::max(max_lat, item.latitude);
        min_lon = std::min(min_lon, item.longitude);
        max_lon = std::max(max_lon, item.longitude);
    }
    const double lat_span = std::max(0.000001, max_lat - min_lat);
    const double lon_span = std::max(0.000001, max_lon - min_lon);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto &item = items[index];
        int x            = static_cast<int>(12.0 + ((item.longitude - min_lon) / lon_span) * 64.0);
        int y            = static_cast<int>(14.0 + ((max_lat - item.latitude) / lat_span) * 56.0);
        if (items.size() == 1) {
            x = 44;
            y = 38;
        }
        lv_obj_t *marker = make_dot(markers_, item.local ? COLOR_GREEN : (index == 0 ? COLOR_RED : COLOR_BLUE), 34);
        lv_obj_set_pos(marker, LV_PCT(x), LV_PCT(y));
        lv_obj_t *core = make_dot(marker, COLOR_SURFACE, 8);
        lv_obj_center(core);
        lv_obj_t *label = make_label(markers_, item.name.c_str(), 14, COLOR_TITLE);
        style_container(label, COLOR_SURFACE, 8);
        lv_obj_set_style_pad_hor(label, 8, 0);
        lv_obj_set_style_pad_ver(label, 4, 0);
        lv_obj_set_width(label, std::min(180, std::max(90, static_cast<int>(item.name.size()) * 8 + 16)));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(label, LV_PCT(std::min(x, 68)), LV_PCT(std::min(y + 8, 76)));
    }
}
