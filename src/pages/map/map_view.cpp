// Raster tile presentation adapted from LILYGO UI Maps; see maps/NOTICE.md.
#include "pages/map/map_view.hpp"

#include "components/components.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <algorithm>
#include <cmath>

namespace {
void plain(lv_obj_t *object, std::uint32_t color = COLOR_SURFACE, lv_opa_t opacity = LV_OPA_TRANSP)
{
    style_container(object, color, 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *icon_button(lv_obj_t *parent, const char *text, const char *name, lv_event_cb_t callback, void *context)
{
    auto *button = make_text_button(parent, text, COLOR_BLUE, COLOR_SURFACE, callback, context);
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, 44, 44);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, 10, 0);
    return button;
}
}  // namespace

MapView::MapView(MapViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation) {}
MapView::~MapView() { destroy(); }

lv_obj_t *MapView::create(lv_obj_t *parent)
{
    destroy();
    if (!parent) return nullptr;
    page_ = create_page_body(parent);
    lv_obj_clear_flag(page_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(page_, 12, 0);
    lv_obj_set_style_pad_row(page_, 8, 0);
    lv_obj_add_event_cb(page_, size_event, LV_EVENT_SIZE_CHANGED, this);
    make_header(page_, "Map", "Clear cache", show_clear_event, this, false);

    auto *search_band = make_flex(page_, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(search_band, LV_PCT(100), 44);
    lv_obj_set_style_pad_column(search_band, 8, 0);
    search_ = lv_textarea_create(search_band);
    lv_obj_set_name_static(search_, "map_search_input");
    lv_obj_set_size(search_, 0, 44); lv_obj_set_flex_grow(search_, 1);
    style_container(search_, COLOR_SURFACE, 10);
    const auto *font = lilygo_ui_font_get(14);
    if (font) lv_obj_set_style_text_font(search_, font, 0);
    lv_obj_set_style_pad_ver(search_, 10, 0);
    lv_textarea_set_one_line(search_, true);
    lv_textarea_set_max_length(search_, 160);
    lv_textarea_set_placeholder_text(search_, "Search address");
    lv_obj_add_event_cb(search_, search_input_event, LV_EVENT_ALL, this);
    icon_button(search_band, LV_SYMBOL_RIGHT, "map_search_button", search_event, this);
    auto *fit = make_text_button(search_band, "Fit", COLOR_BLUE, COLOR_SURFACE, fit_event, this);
    lv_obj_set_name_static(fit, "map_fit_nodes");
    lv_obj_set_size(fit, 52, 44); lv_obj_set_style_pad_all(fit, 0, 0);
    if (font) lv_obj_set_style_text_font(lv_obj_get_child(fit, 0), font, 0);

    viewport_ = lv_obj_create(page_);
    lv_obj_set_name_static(viewport_, "map_canvas");
    plain(viewport_, COLOR_MAP_BACKGROUND, LV_OPA_COVER);
    lv_obj_set_size(viewport_, LV_PCT(100), 0); lv_obj_set_flex_grow(viewport_, 1);
    lv_obj_add_flag(viewport_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(viewport_, pan_event, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(viewport_, pan_event, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(viewport_, pan_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(viewport_, pan_event, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(viewport_, size_event, LV_EVENT_SIZE_CHANGED, this);
    tiles_ = lv_obj_create(viewport_);
    lv_obj_set_name_static(tiles_, "map_tiles"); plain(tiles_);
    lv_obj_set_size(tiles_, LV_PCT(100), LV_PCT(100)); lv_obj_clear_flag(tiles_, LV_OBJ_FLAG_CLICKABLE);
    markers_ = lv_obj_create(viewport_);
    lv_obj_set_name_static(markers_, "map_markers"); plain(markers_);
    lv_obj_set_size(markers_, LV_PCT(100), LV_PCT(100)); lv_obj_clear_flag(markers_, LV_OBJ_FLAG_CLICKABLE);

    auto *controls = controls_ = make_flex(viewport_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(controls, "map_controls");
    lv_obj_set_size(controls, 44, LV_SIZE_CONTENT); lv_obj_set_style_pad_row(controls, 6, 0);
    lv_obj_align(controls, LV_ALIGN_TOP_RIGHT, -8, 8);
    icon_button(controls, "+", "map_zoom_in", zoom_in_event, this);
    icon_button(controls, "−", "map_zoom_out", zoom_out_event, this);
    icon_button(controls, LV_SYMBOL_GPS, "map_locate", local_event, this);
    attribution_ = make_label(viewport_, "© OpenStreetMap contributors", 14, COLOR_TITLE);
    style_container(attribution_, COLOR_SURFACE, 0);
    lv_obj_set_style_pad_hor(attribution_, 4, 0); lv_obj_set_style_pad_ver(attribution_, 3, 0);
    lv_obj_align(attribution_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    selection_ = make_card(page_);
    lv_obj_set_name_static(selection_, "map_selection");
    lv_obj_set_style_pad_all(selection_, 8, 0); lv_obj_set_style_pad_row(selection_, 4, 0);
    auto *selected_text = selection_text_ = make_label(selection_, "", 14, COLOR_TITLE);
    lv_obj_set_size(selected_text, LV_PCT(100), 38);
    lv_label_set_long_mode(selected_text, LV_LABEL_LONG_MODE_DOTS);
    lv_label_bind_text(selected_text, view_model_.selection_subject(), nullptr);
    auto *actions = selection_actions_ = make_flex(selection_, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(actions, LV_PCT(100), 44); lv_obj_set_style_pad_column(actions, 8, 0);
    follow_ = make_text_button(actions, "Follow node", COLOR_BLUE, COLOR_PALE_BLUE, follow_event, this);
    lv_obj_set_name_static(follow_, "map_follow_selected");
    details_ = make_text_button(actions, "Details", COLOR_BLUE, COLOR_SURFACE, detail_event, this);
    lv_obj_set_name_static(details_, "map_node_details");
    for (auto *button : {follow_, details_}) {
        lv_obj_set_flex_grow(button, 1); lv_obj_set_width(button, 0); lv_obj_set_height(button, 44);
        if (font) lv_obj_set_style_text_font(lv_obj_get_child(button, 0), font, 0);
    }
    lv_obj_add_flag(selection_, LV_OBJ_FLAG_HIDDEN);

    auto *information = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(information, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_style_pad_row(information, 0, 0);
    status_ = make_label(information, "", 14, COLOR_TITLE);
    lv_obj_set_size(status_, LV_PCT(100), 22); lv_label_set_long_mode(status_, LV_LABEL_LONG_MODE_DOTS);
    lv_label_bind_text(status_, view_model_.status_subject(), nullptr);
    auto *cache = make_label(information, "", 14, COLOR_SECONDARY);
    lv_obj_set_size(cache, LV_PCT(100), 22); lv_label_set_long_mode(cache, LV_LABEL_LONG_MODE_DOTS);
    lv_label_bind_text(cache, view_model_.cache_subject(), nullptr);

    keyboard_ = lv_keyboard_create(page_);
    lv_obj_set_name_static(keyboard_, "map_keyboard");
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_FLOATING); lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(keyboard_, LV_PCT(100)); lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (font) lv_obj_set_style_text_font(keyboard_, font, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_CANCEL, this);

    confirmation_ = lv_obj_create(page_);
    lv_obj_set_name_static(confirmation_, "map_cache_confirmation");
    plain(confirmation_, COLOR_TEXT, LV_OPA_40);
    lv_obj_set_size(confirmation_, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(confirmation_, LV_OBJ_FLAG_FLOATING); lv_obj_add_flag(confirmation_, LV_OBJ_FLAG_HIDDEN);
    cm0_input_set_focus_scope(confirmation_);
    dialog_ = make_card(confirmation_);
    lv_obj_set_width(dialog_, LV_PCT(94)); lv_obj_set_style_max_width(dialog_, 480, 0); lv_obj_center(dialog_);
    make_wrapped_label(dialog_, "Clear cached maps?", 22, COLOR_TITLE);
    make_wrapped_label(dialog_, "Downloaded tiles will be removed. Node and message history is kept.", 14, COLOR_SECONDARY);
    auto *dialog_actions = make_flex(dialog_, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(dialog_actions, LV_PCT(100), 44); lv_obj_set_style_pad_column(dialog_actions, 8, 0);
    auto *cancel = make_text_button(dialog_actions, "Cancel", COLOR_BLUE, COLOR_PAGE, cancel_clear_event, this);
    auto *clear = make_text_button(dialog_actions, "Clear", COLOR_SURFACE, COLOR_RED, confirm_clear_event, this);
    lv_obj_set_name_static(clear, "map_cache_confirm");
    for (auto *button : {cancel, clear}) { lv_obj_set_width(button, 0); lv_obj_set_flex_grow(button, 1); }

    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, page_, this);
    lv_subject_add_observer_obj(view_model_.viewport_subject(), viewport_observer, page_, this);
    timer_ = lv_timer_create(timer_event, 60, this);
    lv_obj_update_layout(page_); apply_layout(); render_tiles(); render_overlays();
    return page_;
}
void MapView::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    destroy_page_body(page_);
    viewport_ = tiles_ = markers_ = search_ = keyboard_ = selection_ = nullptr;
    details_ = follow_ = confirmation_ = dialog_ = status_ = attribution_ = nullptr;
    selection_text_ = selection_actions_ = controls_ = nullptr;
    tile_slots_.clear(); marker_contexts_.clear(); lines_.clear();
    width_ = height_ = 0; dragging_ = false; tiles_dirty_ = overlays_dirty_ = true;
}
void MapView::content_observer(lv_observer_t *observer, lv_subject_t *)
{
    if (auto *view = static_cast<MapView *>(lv_observer_get_user_data(observer))) view->overlays_dirty_ = true;
}
void MapView::viewport_observer(lv_observer_t *observer, lv_subject_t *)
{
    if (auto *view = static_cast<MapView *>(lv_observer_get_user_data(observer))) view->tiles_dirty_ = true;
}
void MapView::size_event(lv_event_t *event)
{
    if (auto *view = static_cast<MapView *>(lv_event_get_user_data(event))) view->update_layout();
}
void MapView::update_layout()
{
    apply_layout();
    if (!viewport_) return;
    const int w = lv_obj_get_content_width(viewport_), h = lv_obj_get_content_height(viewport_);
    if (w != width_ || h != height_) tiles_dirty_ = true;
}
void MapView::apply_layout()
{
    if (!page_ || !keyboard_) return;
    const int height = lv_obj_get_content_height(page_);
    lv_obj_set_height(keyboard_, std::min(280, std::max(160, height / 2)));
    const bool short_view = height < 400 && lv_obj_get_content_width(page_) >= 440;
    if (selection_ && selection_text_ && selection_actions_) {
        lv_obj_set_flex_flow(selection_, short_view ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(selection_text_, short_view ? 0 : LV_PCT(100));
        lv_obj_set_flex_grow(selection_text_, short_view ? 1 : 0);
        lv_obj_set_width(selection_actions_, short_view ? 176 : LV_PCT(100));
    }
    if (controls_) {
        lv_obj_set_flex_flow(controls_, short_view ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(controls_, short_view ? 144 : 44, short_view ? 44 : LV_SIZE_CONTENT);
        lv_obj_set_style_pad_column(controls_, short_view ? 6 : 0, 0);
    }
}
void MapView::timer_event(lv_timer_t *timer)
{
    if (auto *view = static_cast<MapView *>(lv_timer_get_user_data(timer))) view->process_updates();
}
void MapView::process_updates()
{
    for (int i = 0; i < 16; ++i) {
        meshmap::MapEvent event;
        if (!view_model_.poll(event)) break;
        if (event.kind == meshmap::EventKind::tile_ready && event.generation == view_model_.viewport_generation())
            set_tile_source(event.tile, event.path);
        if (event.kind == meshmap::EventKind::cache_cleared) { render_tiles(false); overlays_dirty_ = true; }
    }
    if (tiles_dirty_ && !dragging_) render_tiles();
    if (overlays_dirty_ && !dragging_) render_overlays();
}
void MapView::pan_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event));
    auto *input = lv_indev_active();
    if (!view || !input) return;
    lv_point_t point{}; lv_indev_get_point(input, &point);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        view->hide_keyboard(); view->press_point_ = point; view->drag_x_ = view->drag_y_ = 0; view->dragging_ = true; return;
    }
    if (!view->dragging_) return;
    view->drag_x_ = point.x - view->press_point_.x; view->drag_y_ = point.y - view->press_point_.y;
    if (lv_event_get_code(event) == LV_EVENT_PRESSING) {
        lv_obj_set_pos(view->tiles_, view->drag_x_, view->drag_y_);
        lv_obj_set_pos(view->markers_, view->drag_x_, view->drag_y_); return;
    }
    view->dragging_ = false;
    lv_obj_set_pos(view->tiles_, 0, 0); lv_obj_set_pos(view->markers_, 0, 0);
    if (std::abs(view->drag_x_) >= 4 || std::abs(view->drag_y_) >= 4)
        view->view_model_.pan_pixels(-view->drag_x_, -view->drag_y_);
}
void MapView::marker_event(lv_event_t *event)
{
    auto *context = static_cast<MarkerContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    if (context->waypoint) context->view->view_model_.select_waypoint(context->number);
    else context->view->view_model_.select_node(context->number);
}
void MapView::search_input_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event));
    if (!view) return;
    switch (lv_event_get_code(event)) {
        case LV_EVENT_CLICKED: case LV_EVENT_FOCUSED: view->show_keyboard(); break;
        case LV_EVENT_KEY: {
            view->hide_keyboard();
            auto *group = lv_obj_get_group(view->search_);
            if (group && lv_group_get_focused(group) == view->search_) lv_obj_add_state(view->search_, LV_STATE_FOCUSED);
            break;
        }
        case LV_EVENT_READY: view->submit_search(); break;
        case LV_EVENT_CANCEL: case LV_EVENT_DEFOCUSED: view->hide_keyboard(); break;
        default: break;
    }
}
void MapView::keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event));
    if (!view) return;
    auto *input = lv_indev_active();
    if (lv_event_get_code(event) == LV_EVENT_READY && (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_KEYPAD))
        view->submit_search();
    else view->hide_keyboard();
}
void MapView::search_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->submit_search(); }
void MapView::zoom_in_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->view_model_.zoom_in(); }
void MapView::zoom_out_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->view_model_.zoom_out(); }
void MapView::local_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->view_model_.follow_local(); }
void MapView::fit_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->view_model_.fit_nodes(); }
void MapView::follow_event(lv_event_t *event) { static_cast<MapView *>(lv_event_get_user_data(event))->view_model_.follow_selected(); }
void MapView::detail_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event));
    if (view->view_model_.selected_node()) view->navigation_.request_node_detail(view->view_model_.selected_node());
}
void MapView::show_clear_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event)); view->hide_keyboard();
    lv_obj_clear_flag(view->confirmation_, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(view->confirmation_);
}
void MapView::cancel_clear_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event)); lv_obj_add_flag(view->confirmation_, LV_OBJ_FLAG_HIDDEN);
}
void MapView::confirm_clear_event(lv_event_t *event)
{
    auto *view = static_cast<MapView *>(lv_event_get_user_data(event));
    lv_obj_add_flag(view->confirmation_, LV_OBJ_FLAG_HIDDEN); view->view_model_.clear_cache();
}
void MapView::submit_search() { view_model_.search(lv_textarea_get_text(search_)); hide_keyboard(); }
void MapView::show_keyboard()
{
    auto *input = lv_indev_active();
    if (cm0_input_hardware_keyboard_present() || (input && lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD)) {
        hide_keyboard(); return;
    }
    lv_keyboard_set_textarea(keyboard_, search_); lv_obj_clear_flag(keyboard_, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(keyboard_);
}
void MapView::hide_keyboard()
{
    if (keyboard_) { lv_keyboard_set_textarea(keyboard_, nullptr); lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN); }
}
void MapView::render_tiles(bool download)
{
    if (!viewport_ || !tiles_) return;
    lv_obj_update_layout(viewport_);
    width_ = lv_obj_get_content_width(viewport_); height_ = lv_obj_get_content_height(viewport_);
    if (width_ <= 0 || height_ <= 0) return;
    lv_obj_clean(tiles_); tile_slots_.clear(); lv_obj_set_pos(tiles_, 0, 0);
    for (const auto &visible : view_model_.prepare_viewport(width_, height_, download)) {
        tile_slots_.push_back({visible.id, lv_obj_create(tiles_), {}});
        auto &slot = tile_slots_.back(); plain(slot.container, (visible.id.x + visible.id.y) % 2 ? COLOR_MAP_BACKGROUND : COLOR_MAP_CONTOUR, LV_OPA_COVER);
        lv_obj_set_size(slot.container, 256, 256); lv_obj_set_pos(slot.container, visible.screen_x, visible.screen_y);
        lv_obj_set_style_border_width(slot.container, 1, 0); lv_obj_set_style_border_color(slot.container, lv_color_hex(COLOR_SEPARATOR), 0);
        lv_obj_clear_flag(slot.container, LV_OBJ_FLAG_CLICKABLE);
        std::filesystem::path path;
        if (view_model_.find_tile(visible.id, path)) set_tile_source(visible.id, path);
    }
    tiles_dirty_ = false; overlays_dirty_ = true;
}
void MapView::set_tile_source(const meshmap::TileId &tile, const std::filesystem::path &path)
{
    for (auto &slot : tile_slots_) {
        if (!(slot.tile == tile) || !slot.source.empty()) continue;
        slot.source = "A:" + path.string();
        auto *image = lv_image_create(slot.container); lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(image, slot.source.c_str()); lv_obj_set_size(image, 256, 256);
    }
}
void MapView::render_overlays()
{
    if (!markers_) return;
    lv_obj_clean(markers_); marker_contexts_.clear(); lines_.clear(); lv_obj_set_pos(markers_, 0, 0);
    const auto &model = view_model_.model();
    for (const auto &path : view_model_.paths()) {
        for (const auto segment : model.project_path(path.points, width_, height_)) {
            lines_.push_back({{{{static_cast<lv_value_precise_t>(segment.first.x), static_cast<lv_value_precise_t>(segment.first.y)},
                                {static_cast<lv_value_precise_t>(segment.second.x), static_cast<lv_value_precise_t>(segment.second.y)}}}});
            auto *line = lv_line_create(markers_);
            lv_obj_set_name_static(line, path.route ? "map_route" : "map_position_history");
            lv_line_set_points(line, lines_.back().points.data(), 2);
            lv_obj_set_style_line_width(line, path.route ? 3 : 4, 0);
            lv_obj_set_style_line_color(line, lv_color_hex(path.route ? COLOR_ORANGE : COLOR_PURPLE), 0);
            lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    std::vector<lv_area_t> occupied_labels;
    lv_area_t viewport_area{}; lv_obj_get_coords(viewport_, &viewport_area);
    for (auto *reserved : {controls_, attribution_}) {
        lv_area_t area{}; lv_obj_get_coords(reserved, &area);
        area.x1 -= viewport_area.x1; area.x2 -= viewport_area.x1;
        area.y1 -= viewport_area.y1; area.y2 -= viewport_area.y1;
        occupied_labels.push_back(area);
    }
    auto marker = [this, &model, &occupied_labels](meshmap::GeoPoint point, const std::string &name, std::uint32_t number,
                                  bool waypoint, std::uint32_t color, bool selected) {
        const auto p = model.project(point, width_, height_);
        if (p.x < -22 || p.y < -22 || p.x > width_ + 22 || p.y > height_ + 22) return;
        marker_contexts_.push_back({this, number, waypoint});
        auto *button = lv_button_create(markers_);
        lv_obj_set_name_static(button, waypoint ? "map_waypoint" : "map_node_marker");
        plain(button); lv_obj_set_size(button, 44, 44);
        lv_obj_set_pos(button, static_cast<int>(std::lround(p.x)) - 22, static_cast<int>(std::lround(p.y)) - 22);
        lv_obj_add_event_cb(button, marker_event, LV_EVENT_CLICKED, &marker_contexts_.back());
        auto *dot = make_dot(button, color, selected ? 34 : 26);
        lv_obj_set_style_border_width(dot, 3, 0); lv_obj_set_style_border_color(dot, lv_color_hex(COLOR_SURFACE), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE); lv_obj_center(dot);
        if (waypoint) { auto *symbol = make_label(dot, LV_SYMBOL_GPS, 14, COLOR_SURFACE); lv_obj_center(symbol); }
        if (height_ < 24) return;
        const int label_width = std::min(180, std::max(60, width_ / 3));
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        const std::array<lv_point_t, 6> candidates{{{x + 16, y + 15}, {x + 16, y - 38},
            {x - label_width - 16, y + 15}, {x - label_width - 16, y - 38},
            {x - label_width / 2, y + 30}, {x - label_width / 2, y - 52}}};
        std::optional<lv_area_t> placement;
        for (const auto candidate : candidates) {
            const int px = std::clamp(static_cast<int>(candidate.x), 0, std::max(0, width_ - label_width));
            const int py = std::clamp(static_cast<int>(candidate.y), 0, std::max(0, height_ - 24));
            const lv_area_t area{px, py, px + label_width - 1, py + 23};
            const bool overlaps = std::any_of(occupied_labels.begin(), occupied_labels.end(), [&area](const auto &other) {
                return area.x1 <= other.x2 + 3 && other.x1 <= area.x2 + 3 &&
                       area.y1 <= other.y2 + 3 && other.y1 <= area.y2 + 3;
            });
            if (!overlaps) { placement = area; break; }
        }
        // Dense clusters retain every tappable marker. Labels are omitted only
        // when no clear position is available beside the marker.
        if (!placement) return;
        occupied_labels.push_back(*placement);
        auto *label = make_label(markers_, name.c_str(), 14, COLOR_TITLE);
        style_container(label, COLOR_SURFACE, 6); lv_obj_set_style_pad_hor(label, 5, 0);
        lv_obj_set_size(label, label_width, 24); lv_obj_set_style_pad_ver(label, 2, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(label, placement->x1, placement->y1);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    };
    std::vector<const MapMarkerViewModel *> nodes;
    for (const auto &node : view_model_.markers()) nodes.push_back(&node);
    std::stable_sort(nodes.begin(), nodes.end(), [this](const auto *left, const auto *right) {
        const auto priority = [this](const auto *node) { return node->number == view_model_.selected_node() ? 2 : node->local ? 1 : 0; };
        return priority(left) > priority(right);
    });
    for (const auto *node : nodes) marker({node->latitude, node->longitude}, node->name, node->number,
                                          false, node->local ? COLOR_GREEN_DARK : COLOR_BLUE,
                                          node->number == view_model_.selected_node());
    for (const auto &waypoint : view_model_.waypoints()) marker(waypoint.point, waypoint.name, waypoint.id, true, COLOR_ORANGE, false);
    if (view_model_.markers().empty() && view_model_.waypoints().empty()) {
        auto *empty = make_label(markers_, "No positioned nodes", 14, COLOR_SECONDARY); lv_obj_center(empty);
    }
    if (view_model_.has_selection()) lv_obj_clear_flag(selection_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(selection_, LV_OBJ_FLAG_HIDDEN);
    if (view_model_.selected_node()) lv_obj_clear_flag(lv_obj_get_parent(follow_), LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lv_obj_get_parent(follow_), LV_OBJ_FLAG_HIDDEN);
    overlays_dirty_ = false;
}
