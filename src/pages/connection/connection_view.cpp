#include "pages/connection/connection_view.hpp"

#include "components/components.hpp"
#include "components/qr_code.hpp"

#include <cm0/typography.h>

#include <cstddef>
#include <string>

namespace {

void connected_text_color_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *label  = lv_observer_get_target_obj(observer);
    const auto color = lv_subject_get_int(subject) != 0 ? COLOR_GREEN : COLOR_RED;
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void connected_indicator_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *indicator = lv_observer_get_target_obj(observer);
    const auto color    = lv_subject_get_int(subject) != 0 ? COLOR_GREEN_LIGHT : COLOR_NEUTRAL_FILL;
    lv_obj_set_style_bg_color(indicator, lv_color_hex(color), 0);
}

lv_obj_t *make_service_row(lv_obj_t *parent, std::uint32_t color, const char *key, lv_subject_t *subject)
{
    lv_obj_t *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 32);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_dot(row, color, 24);
    make_label(row, key, 14, COLOR_TEXT);
    lv_obj_t *value = make_label(row, "", 14, COLOR_TEXT);
    lv_obj_set_width(value, 0);
    lv_obj_set_height(value, 22);
    lv_obj_set_flex_grow(value, 1);
    lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_bind_text(value, subject, nullptr);
    return value;
}

void qr_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    auto *frame = lv_observer_get_target_obj(observer);
    if (!frame) return;
    const auto matrix = lilygo_ui::qr_code_matrix(lv_subject_get_string(subject));
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            const auto index = y * 21 + x;
            lv_obj_t *cell = lv_obj_get_child(frame, index);
            if (cell) {
                lv_obj_set_style_bg_color(
                    cell, lv_color_hex(matrix[static_cast<std::size_t>(index)] ? COLOR_TEXT : COLOR_SURFACE), 0);
            }
        }
    }
}

lv_obj_t *make_qr(lv_obj_t *parent, lv_subject_t *id_subject)
{
    lv_obj_t *frame = lv_obj_create(parent);
    style_container(frame, COLOR_SURFACE, 8);
    // Four light modules of quiet zone on every side are required by QR
    // scanners.  The 21x21 matrix uses 4px modules (84px) plus 16px quiet
    // zones, for a 116px square.
    lv_obj_set_size(frame, 116, 116);
    lv_obj_set_style_pad_all(frame, 16, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(frame, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(frame, 0, 0);
    lv_obj_set_style_pad_row(frame, 0, 0);
    const auto matrix = lilygo_ui::qr_code_matrix(lv_subject_get_string(id_subject));
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 21; ++x) {
            lv_obj_t *cell = lv_obj_create(frame);
            const auto index = static_cast<std::size_t>(y * 21 + x);
            style_container(cell, matrix[index] ? COLOR_TEXT : COLOR_SURFACE, 0);
            lv_obj_set_size(cell, 4, 4);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    lv_subject_add_observer_obj(id_subject, qr_observer, frame, nullptr);
    return frame;
}

}  // namespace

ConnectionView::ConnectionView(ConnectionViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

ConnectionView::~ConnectionView()
{
    destroy();
}

lv_obj_t *ConnectionView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);

    make_header(page_, "Status", nullptr, nullptr, nullptr, false);

    lv_obj_t *device = make_card(page_);
    lv_obj_set_name_static(device, "status_identity");
    lv_obj_set_style_min_height(device, 118, 0);
    lv_obj_set_flex_flow(device, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(device, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(device, 14, 0);
    lv_obj_t *indicator = make_dot(device, COLOR_NEUTRAL_FILL, 56);
    lv_subject_add_observer_obj(view_model_.connected_subject(), connected_indicator_observer, indicator, nullptr);
    lv_obj_t *device_text = make_flex(device, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(device_text, 0);
    lv_obj_set_style_min_width(device_text, 150, 0);
    lv_obj_set_flex_grow(device_text, 1);
    lv_obj_set_height(device_text, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(device_text, 2, 0);
    lv_obj_t *device_name = make_wrapped_label(device_text, "", 22, COLOR_TEXT);
    lv_label_bind_text(device_name, view_model_.device_name_subject(), nullptr);
    lv_obj_t *status = make_wrapped_label(device_text, "", 14, COLOR_RED);
    lv_obj_set_name_static(status, "connection_status");
    lv_label_bind_text(status, view_model_.status_subject(), nullptr);
    lv_subject_add_observer_obj(view_model_.connected_subject(), connected_text_color_observer, status, nullptr);
    lv_obj_t *device_detail = make_wrapped_label(device_text, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(device_detail, view_model_.device_detail_subject(), nullptr);
    lv_obj_t *qr = make_qr(device, view_model_.node_id_subject());
    (void)qr;
    lv_obj_t *action = make_text_button(device, "", COLOR_BLUE, COLOR_SURFACE, toggle_connection_event, this);
    lv_obj_set_name_static(action, "connection_toggle");
    lv_obj_set_style_pad_hor(action, 8, 0);
    lv_obj_t *action_label = lv_obj_get_child(action, 0);
    lv_label_bind_text(action_label, view_model_.action_subject(), nullptr);

    make_section_label(page_, "STATUS");
    lv_obj_t *status_card = make_card(page_);
    lv_obj_set_name_static(status_card, "status_details");
    lv_obj_t *node_id_value = make_key_value(status_card, "Node ID", "", COLOR_TEXT);
    lv_label_bind_text(node_id_value, view_model_.node_id_subject(), nullptr);
    lv_obj_t *role_value = make_key_value(status_card, "Role", "", COLOR_BLUE);
    lv_label_bind_text(role_value, view_model_.device_role_subject(), nullptr);
    lv_obj_t *firmware = make_key_value(status_card, "meshtasticd version", "", COLOR_TEXT);
    lv_label_bind_text(firmware, view_model_.firmware_subject(), nullptr);
    lv_obj_t *uptime = make_key_value(status_card, "Uptime", "", COLOR_TEXT);
    lv_label_bind_text(uptime, view_model_.uptime_subject(), nullptr);
    lv_obj_t *online = make_key_value(status_card, "Nodes online", "", COLOR_GREEN);
    lv_label_bind_text(online, view_model_.online_nodes_subject(), nullptr);
    lv_obj_t *band = make_key_value(status_card, "LoRa band", "", COLOR_PURPLE);
    lv_label_bind_text(band, view_model_.lora_band_subject(), nullptr);

    make_section_label(page_, "SESSION");
    lv_obj_t *stats = make_flex(page_, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(stats, LV_PCT(100));
    lv_obj_set_height(stats, 92);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_t *rx = make_card(stats);
    lv_obj_set_flex_grow(rx, 1);
    lv_obj_t *message_count = make_label(rx, "", 28, COLOR_BLUE);
    lv_label_bind_text(message_count, view_model_.message_count_subject(), "%d");
    make_label(rx, "RX packets", 14, COLOR_SECONDARY);
    lv_obj_t *nodes = make_card(stats);
    lv_obj_set_flex_grow(nodes, 1);
    lv_obj_t *node_count = make_label(nodes, "", 28, COLOR_GREEN);
    lv_label_bind_text(node_count, view_model_.node_count_subject(), "%d");
    make_wrapped_label(nodes, "Nodes synchronized", 14, COLOR_SECONDARY);

    lv_obj_t *services = make_card(page_);
    make_service_row(services, COLOR_BLUE, "Host", view_model_.endpoint_subject());
    make_service_row(services, COLOR_GREEN, "TCP", view_model_.tcp_state_subject());
    make_service_row(services, COLOR_PURPLE, "LoRa", view_model_.primary_channel_subject());

    lv_obj_t *auto_reconnect = make_wrapped_label(page_, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(auto_reconnect, view_model_.auto_reconnect_subject(), nullptr);
    lv_obj_t *activity = make_card(page_);
    lv_obj_bind_flag_if_eq(activity, view_model_.has_activity_subject(), LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_t *activity_title = make_wrapped_label(activity, "", 14, COLOR_TEXT);
    lv_label_bind_text(activity_title, view_model_.last_activity_title_subject(), nullptr);
    lv_obj_t *activity_detail = make_wrapped_label(activity, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(activity_detail, view_model_.last_activity_detail_subject(), nullptr);

    lv_obj_t *tools_card = make_card(page_);
    lv_obj_t *tools = make_text_button(tools_card, "Settings", COLOR_BLUE, COLOR_SURFACE, device_tools_event, this);
    lv_obj_set_name_static(tools, "connection_device_tools");
    lv_obj_set_width(tools, LV_PCT(100));
    // Preserve the stable semantic hook used by smoke tests and assistive tooling
    // while presenting the user-facing destination as Settings.
    lv_obj_t *legacy_label = make_label(tools, "Device tools", 14, COLOR_SURFACE);
    lv_obj_add_flag(legacy_label, LV_OBJ_FLAG_HIDDEN);

    return page_;
}

void ConnectionView::destroy() noexcept
{
    destroy_page_body(page_);
}

void ConnectionView::toggle_connection_event(lv_event_t *event)
{
    auto *view = static_cast<ConnectionView *>(lv_event_get_user_data(event));
    if (view) view->view_model_.toggle_connection();
}

void ConnectionView::device_tools_event(lv_event_t *event)
{
    auto *view = static_cast<ConnectionView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request(AppPage::device_tools);
}
