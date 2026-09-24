#include "pages/device_tools/device_tools_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

namespace {

lv_obj_t *make_tool_row(lv_obj_t *parent, const char *title, const char *value, std::uint32_t color)
{
    lv_obj_t *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 38);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = make_label(row, title, 14, color);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    return make_label(row, value, 14, color);
}

}  // namespace

DeviceToolsView::DeviceToolsView(DeviceToolsViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

DeviceToolsView::~DeviceToolsView()
{
    destroy();
}

lv_obj_t *DeviceToolsView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Device Tools", nullptr, nullptr, nullptr, true, nullptr);
    lv_obj_t *header = lv_obj_get_child(page_, 0);
    lv_obj_t *back   = header ? lv_obj_get_child(header, 0) : nullptr;
    if (back) lv_obj_add_event_cb(back, back_event, LV_EVENT_CLICKED, this);

    lv_obj_t *health = make_card(page_);
    lv_obj_set_name_static(health, "device_tools");
    lv_obj_t *row       = make_info_row(health, COLOR_NEUTRAL_FILL, "", "", nullptr, nullptr);
    lv_obj_t *indicator = lv_obj_get_child(row, 0);
    lv_obj_t *text      = lv_obj_get_child(row, 1);
    lv_obj_t *state     = text ? lv_obj_get_child(text, 0) : nullptr;
    lv_obj_t *detail    = text ? lv_obj_get_child(text, 1) : nullptr;
    if (state) lv_label_bind_text(state, view_model_.state_subject(), nullptr);
    if (detail) lv_label_bind_text(detail, view_model_.detail_subject(), nullptr);
    if (indicator) {
        lv_subject_add_observer_obj(view_model_.connected_subject(), connected_indicator_observer, indicator, nullptr);
    }

    make_section_label(page_, "MAINTENANCE");
    lv_obj_t *maintenance = make_card(page_);
    make_tool_row(maintenance, "Screen calibration", LV_SYMBOL_RIGHT, COLOR_TEXT);
    make_tool_row(maintenance, "Screen lock", "Off  " LV_SYMBOL_RIGHT, COLOR_TEXT);
    make_tool_row(maintenance, "Offline maps", "128 MB  " LV_SYMBOL_RIGHT, COLOR_TEXT);
    make_tool_row(maintenance, "TCP diagnostics", "0x94 0xC3  " LV_SYMBOL_RIGHT, COLOR_BLUE);
    lv_obj_t *endpoint = make_key_value(maintenance, "Endpoint", "", COLOR_TEXT);
    lv_label_bind_text(endpoint, view_model_.endpoint_subject(), nullptr);
    lv_obj_t *config = make_key_value(maintenance, "Configuration", "", COLOR_TEXT);
    lv_label_bind_text(config, view_model_.config_subject(), nullptr);
    lv_obj_t *reconnect = make_key_value(maintenance, "Auto reconnect", "", COLOR_TEXT);
    lv_label_bind_text(reconnect, view_model_.auto_reconnect_subject(), nullptr);
    make_tool_row(maintenance, "Import / export channels & keys", LV_SYMBOL_RIGHT, COLOR_TEXT);

    make_section_label(page_, "POWER");
    lv_obj_t *power = make_card(page_);
    make_tool_row(power, "Reboot device", "", COLOR_BLUE);
    make_tool_row(power, "Shut down device", "", COLOR_RED);
    make_tool_row(power, "Reset NodeDB / Factory reset", "", COLOR_RED);

    lv_obj_t *notice = make_card(page_);
    lv_obj_set_style_bg_color(notice, lv_color_hex(COLOR_NOTICE), 0);
    make_wrapped_label(notice, "Destructive actions require confirmation and keep the user in control.", 14,
                       COLOR_NOTICE_TEXT);
    return page_;
}

void DeviceToolsView::destroy() noexcept
{
    destroy_page_body(page_);
}

void DeviceToolsView::back_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void DeviceToolsView::connected_indicator_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *indicator = lv_observer_get_target_obj(observer);
    const auto color    = lv_subject_get_int(subject) != 0 ? COLOR_GREEN_LIGHT : COLOR_NEUTRAL_FILL;
    lv_obj_set_style_bg_color(indicator, lv_color_hex(color), 0);
}
