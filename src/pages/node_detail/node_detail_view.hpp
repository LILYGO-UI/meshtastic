#ifndef LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_HPP

#include "app_router.hpp"
#include "pages/node_detail/node_detail_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

class NodeDetailView final : public PageView {
public:
    NodeDetailView(NodeDetailViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~NodeDetailView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void back_event(lv_event_t *event);
    static void message_event(lv_event_t *event);
    static void map_event(lv_event_t *event);
    static void telemetry_event(lv_event_t *event);
    static void traceroute_event(lv_event_t *event);
    static void waypoint_event(lv_event_t *event);
    static void close_waypoint_event(lv_event_t *event);
    static void share_waypoint_event(lv_event_t *event);
    static void editor_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);

    void render_content();

    NodeDetailViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_    = nullptr;
    lv_obj_t *content_ = nullptr;
    lv_obj_t *overlay_ = nullptr;
    lv_obj_t *waypoint_panel_ = nullptr;
    lv_obj_t *waypoint_name_ = nullptr;
    lv_obj_t *waypoint_description_ = nullptr;
    lv_obj_t *waypoint_latitude_ = nullptr;
    lv_obj_t *waypoint_longitude_ = nullptr;
    lv_obj_t *waypoint_expiry_ = nullptr;
    lv_obj_t *waypoint_channel_ = nullptr;
    lv_obj_t *keyboard_ = nullptr;
    std::vector<std::uint32_t> channel_ids_;
    void create_waypoint_editor(lv_obj_t *parent);
};

#endif
