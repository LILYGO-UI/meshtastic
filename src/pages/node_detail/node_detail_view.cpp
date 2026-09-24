#include "pages/node_detail/node_detail_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

NodeDetailView::NodeDetailView(NodeDetailViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

NodeDetailView::~NodeDetailView()
{
    destroy();
}

lv_obj_t *NodeDetailView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Node", nullptr, nullptr, nullptr, true, nullptr);
    lv_obj_t *header = lv_obj_get_child(page_, 0);
    lv_obj_t *back   = header ? lv_obj_get_child(header, 0) : nullptr;
    if (back) lv_obj_add_event_cb(back, back_event, LV_EVENT_CLICKED, this);

    content_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(content_, "node_detail");
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(content_, 16, 0);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, content_, this);
    return page_;
}

void NodeDetailView::destroy() noexcept
{
    destroy_page_body(page_);
    content_ = nullptr;
}

void NodeDetailView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<NodeDetailView *>(lv_observer_get_user_data(observer));
    if (view) view->render_content();
}

void NodeDetailView::back_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void NodeDetailView::message_event(lv_event_t *event)
{
    static_cast<NodeDetailView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::chat);
}

void NodeDetailView::map_event(lv_event_t *event)
{
    static_cast<NodeDetailView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::map);
}

void NodeDetailView::render_content()
{
    if (!page_ || !content_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(content_);
    const auto &node = view_model_.presentation();
    if (!node.found) {
        make_wrapped_label(content_, "This node is no longer in the synchronized NodeDB.", 22, COLOR_SECONDARY);
        lv_obj_update_layout(page_);
        lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
        return;
    }

    lv_obj_t *hero = make_flex(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(hero, 156, 0);
    lv_obj_set_style_pad_row(hero, 8, 0);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *avatar   = make_dot(hero, COLOR_AVATAR, 80);
    lv_obj_t *initials = make_label(avatar, node.initials.c_str(), 28, COLOR_GREEN);
    lv_obj_center(initials);
    lv_obj_t *name = make_wrapped_label(hero, node.name.c_str(), 28, COLOR_TEXT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *presence = make_wrapped_label(hero, node.presence.c_str(), 14, COLOR_GREEN);
    lv_obj_set_style_text_align(presence, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *actions = make_flex(content_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_style_pad_row(actions, 8, 0);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *message = make_text_button(actions, "Message", COLOR_SURFACE, COLOR_PRIMARY_ACTION, message_event, this);
    lv_obj_set_name_static(message, "node_message");
    lv_obj_t *map = make_text_button(actions, "Show on Map", COLOR_BLUE, COLOR_SURFACE, map_event, this);
    lv_obj_set_name_static(map, "node_map");

    if (node.has_metrics) {
        make_section_label(content_, "TELEMETRY");
        lv_obj_t *telemetry = make_card(content_);
        lv_obj_set_flex_flow(telemetry, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(telemetry, 16, 0);
        if (!node.battery.empty()) {
            lv_obj_t *metric = make_flex(telemetry, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_height(metric, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(metric, 1);
            make_wrapped_label(metric, node.battery.c_str(), 22, COLOR_GREEN);
            make_label(metric, "Battery", 14, COLOR_SECONDARY);
        }
        if (!node.voltage.empty()) {
            lv_obj_t *metric = make_flex(telemetry, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_height(metric, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(metric, 1);
            make_wrapped_label(metric, node.voltage.c_str(), 22, COLOR_BLUE);
            make_label(metric, "Voltage", 14, COLOR_SECONDARY);
        }
    }
    if (node.has_position) {
        make_section_label(content_, "POSITION");
        lv_obj_t *position = make_card(content_);
        make_wrapped_label(position, node.coordinates.c_str(), 22, COLOR_TEXT);
        if (!node.altitude.empty()) make_wrapped_label(position, node.altitude.c_str(), 14, COLOR_SECONDARY);
    }
    make_section_label(content_, "RADIO INFORMATION");
    lv_obj_t *details = make_card(content_);
    make_key_value(details, "NODE ID", node.id.c_str(), COLOR_TEXT);
    make_key_value(details, "NODE NUMBER", node.number.c_str(), COLOR_TEXT);
    make_key_value(details, "SIGNAL", node.signal.c_str(), COLOR_TEXT);
    make_key_value(details, "LAST HEARD", node.last_heard.c_str(), COLOR_TEXT);
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
