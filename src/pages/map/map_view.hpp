#ifndef LILYGO_UI_MESHTASTIC_MAP_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_MAP_VIEW_HPP

#include "app_router.hpp"
#include "pages/map/map_view_model.hpp"
#include "pages/page_view.hpp"

#include <array>
#include <deque>
#include <string>

class MapView final : public PageView {
public:
    explicit MapView(MapViewModel &view_model, AppNavigationCommands navigation = {}) noexcept;
    ~MapView() override;
    lv_obj_t *create(lv_obj_t *parent) override;
    void update_layout() override;
    void destroy() noexcept override;

private:
    struct TileSlot { meshmap::TileId tile; lv_obj_t *container = nullptr; std::string source; };
    struct MarkerContext { MapView *view = nullptr; std::uint32_t number = 0; bool waypoint = false; };
    struct LinePoints { std::array<lv_point_precise_t, 2> points; };
    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void viewport_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void size_event(lv_event_t *event);
    static void timer_event(lv_timer_t *timer);
    static void pan_event(lv_event_t *event);
    static void marker_event(lv_event_t *event);
    static void search_input_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    static void search_event(lv_event_t *event);
    static void zoom_in_event(lv_event_t *event);
    static void zoom_out_event(lv_event_t *event);
    static void local_event(lv_event_t *event);
    static void fit_event(lv_event_t *event);
    static void follow_event(lv_event_t *event);
    static void detail_event(lv_event_t *event);
    static void show_clear_event(lv_event_t *event);
    static void cancel_clear_event(lv_event_t *event);
    static void confirm_clear_event(lv_event_t *event);
    void apply_layout();
    void submit_search();
    void show_keyboard();
    void hide_keyboard();
    void process_updates();
    void render_tiles(bool download = true);
    void render_overlays();
    void set_tile_source(const meshmap::TileId &tile, const std::filesystem::path &path);

    MapViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_ = nullptr, *viewport_ = nullptr, *tiles_ = nullptr, *markers_ = nullptr;
    lv_obj_t *search_ = nullptr, *keyboard_ = nullptr, *selection_ = nullptr;
    lv_obj_t *selection_text_ = nullptr, *selection_actions_ = nullptr, *controls_ = nullptr;
    lv_obj_t *details_ = nullptr, *follow_ = nullptr, *confirmation_ = nullptr, *dialog_ = nullptr;
    lv_obj_t *status_ = nullptr, *attribution_ = nullptr;
    lv_timer_t *timer_ = nullptr;
    std::deque<TileSlot> tile_slots_;
    std::deque<MarkerContext> marker_contexts_;
    std::deque<LinePoints> lines_;
    lv_point_t press_point_{};
    int drag_x_ = 0, drag_y_ = 0, width_ = 0, height_ = 0;
    bool dragging_ = false, tiles_dirty_ = true, overlays_dirty_ = true;
};
#endif
