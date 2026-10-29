#include "pages/device_tools/device_tools_view.hpp"

#include "components/components.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <meshtastic/config.pb.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace {

lv_obj_t *settings_button(lv_obj_t *parent, const char *text, std::uint32_t foreground,
                 std::uint32_t background, lv_event_cb_t callback, void *context)
{
    auto *button = make_text_button(parent, text, foreground, background, callback, context);
    lv_obj_set_height(button, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(button, 46, 0);
    lv_obj_set_style_pad_ver(button, 10, 0);
    auto *label = lv_obj_get_child(button, 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(label, font, 0);
    return button;
}

template <typename Valid>
std::uint16_t enum_index(int value, int minimum, int maximum, Valid valid)
{
    std::uint16_t index = 0;
    for (int option = minimum; option <= maximum; ++option) {
        if (!valid(option)) continue;
        if (option == value) return index;
        ++index;
    }
    return 0;
}
template <typename Valid>
int enum_value(std::uint16_t index, int minimum, int maximum, Valid valid)
{
    for (int option = minimum; option <= maximum; ++option) {
        if (!valid(option)) continue;
        if (index-- == 0) return option;
    }
    return minimum;
}

std::string region_options()
{
    std::string options;
    for (int value = meshtastic::Config::LoRaConfig::RegionCode_MIN;
         value <= meshtastic::Config::LoRaConfig::RegionCode_MAX; ++value) {
        if (!meshtastic::Config::LoRaConfig::RegionCode_IsValid(value)) continue;
        if (!options.empty()) options.push_back('\n');
        options += meshtastic::Config::LoRaConfig::RegionCode_Name(value);
    }
    return options;
}

std::string modem_options()
{
    std::string options;
    for (int value = meshtastic::Config::LoRaConfig::ModemPreset_MIN;
         value <= meshtastic::Config::LoRaConfig::ModemPreset_MAX; ++value) {
        if (!meshtastic::Config::LoRaConfig::ModemPreset_IsValid(value)) continue;
        if (!options.empty()) options.push_back('\n');
        options += meshtastic::Config::LoRaConfig::ModemPreset_Name(value);
    }
    return options;
}

std::string role_options()
{
    std::string options;
    for (int value = meshtastic::Config::DeviceConfig::Role_MIN;
         value <= meshtastic::Config::DeviceConfig::Role_MAX; ++value) {
        if (!meshtastic::Config::DeviceConfig::Role_IsValid(value)) continue;
        if (!options.empty()) options.push_back('\n');
        options += meshtastic::Config::DeviceConfig::Role_Name(value);
    }
    return options;
}

lv_obj_t *make_dropdown_row(lv_obj_t *parent, const char *title, const std::string &options, int selected,
                            const char *name)
{
    lv_obj_t *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 46, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = make_label(row, title, 14, COLOR_TEXT);
    lv_obj_set_width(label, LV_PCT(38));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_t *dropdown = lv_dropdown_create(row);
    lv_obj_set_name_static(dropdown, name);
    lv_obj_set_width(dropdown, 0);
    lv_obj_set_flex_grow(dropdown, 1);
    lv_obj_set_height(dropdown, 44);
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, static_cast<std::uint16_t>(std::max(0, selected)));
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(dropdown, font, 0);
    return dropdown;
}

lv_obj_t *make_owner_input(lv_obj_t *parent, const char *value)
{
    lv_obj_t *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 48, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = make_label(row, "Node name", 14, COLOR_TEXT);
    lv_obj_set_width(label, LV_PCT(38));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_t *input = lv_textarea_create(row);
    lv_obj_set_name_static(input, "settings_node_name_input");
    lv_obj_set_width(input, 0);
    lv_obj_set_flex_grow(input, 1);
    lv_obj_set_height(input, 48);
    style_container(input, COLOR_NEUTRAL_FILL, 11);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, 24);
    lv_textarea_set_placeholder_text(input, "Node name");
    lv_textarea_set_text(input, value && value[0] ? value : "");
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(input, font, 0);
    return input;
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
    // Reserve space for the keyboard without covering the active editor.
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_set_height(page_, 0);
    lv_obj_set_flex_grow(page_, 1);
    make_header(page_, "Settings", nullptr, nullptr, nullptr, true, nullptr);
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

    make_section_label(page_, "RADIO CONFIGURATION");
    lv_obj_t *maintenance = make_card(page_);
    lv_obj_set_name_static(maintenance, "settings_radio");
    const auto regions = region_options();
    const auto modems  = modem_options();
    region_dropdown_ = make_dropdown_row(maintenance, "LoRa region", regions,
        enum_index(view_model_.selected_region(), meshtastic::Config::LoRaConfig::RegionCode_MIN,
                   meshtastic::Config::LoRaConfig::RegionCode_MAX,
                   [](int value) { return meshtastic::Config::LoRaConfig::RegionCode_IsValid(value); }),
        "settings_region_dropdown");
    modem_dropdown_ = make_dropdown_row(maintenance, "Modem preset", modems,
        enum_index(view_model_.selected_modem_preset(), meshtastic::Config::LoRaConfig::ModemPreset_MIN,
                   meshtastic::Config::LoRaConfig::ModemPreset_MAX,
                   [](int value) { return meshtastic::Config::LoRaConfig::ModemPreset_IsValid(value); }),
        "settings_modem_dropdown");
    lv_obj_t *endpoint = make_key_value(maintenance, "Endpoint", "", COLOR_TEXT);
    lv_label_bind_text(endpoint, view_model_.endpoint_subject(), nullptr);
    lv_obj_t *config = make_key_value(maintenance, "Configuration", "", COLOR_TEXT);
    lv_label_bind_text(config, view_model_.config_subject(), nullptr);
    lv_obj_t *reconnect = make_key_value(maintenance, "Auto reconnect", "", COLOR_TEXT);
    lv_label_bind_text(reconnect, view_model_.auto_reconnect_subject(), nullptr);

    make_section_label(page_, "DEVICE CONFIGURATION");
    lv_obj_t *device = make_card(page_);
    lv_obj_set_name_static(device, "settings_device");
    role_dropdown_ = make_dropdown_row(device, "Node role", role_options(),
        enum_index(view_model_.selected_device_role(), meshtastic::Config::DeviceConfig::Role_MIN,
                   meshtastic::Config::DeviceConfig::Role_MAX,
                   [](int value) { return meshtastic::Config::DeviceConfig::Role_IsValid(value); }),
        "settings_role_dropdown");
    owner_input_ = make_owner_input(device, lv_subject_get_string(view_model_.owner_name_subject()));
    lv_obj_add_event_cb(owner_input_, owner_input_event, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(owner_input_, owner_input_event, LV_EVENT_DEFOCUSED, this);
    lv_obj_add_event_cb(owner_input_, owner_input_event, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(owner_input_, owner_input_event, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(owner_input_, save_configuration_event, LV_EVENT_READY, this);
    lv_obj_t *firmware = make_key_value(device, "Firmware", "", COLOR_TEXT);
    lv_label_bind_text(firmware, view_model_.firmware_subject(), nullptr);
    lv_obj_t *edit_status = make_wrapped_label(device, "", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(edit_status, "settings_edit_status");
    lv_label_bind_text(edit_status, view_model_.edit_status_subject(), nullptr);
    lv_obj_t *request_key = settings_button(device, "Refresh admin key", COLOR_BLUE, COLOR_SURFACE,
                                             request_key_event, this);
    lv_obj_set_width(request_key, LV_PCT(100));
    lv_obj_bind_state_if_eq(request_key, view_model_.connected_subject(), LV_STATE_DISABLED, 0);
    lv_obj_t *save = settings_button(device, "Save radio and device configuration", COLOR_SURFACE, COLOR_BLUE,
                                      save_configuration_event, this);
    lv_obj_set_name_static(save, "settings_save_configuration");
    lv_obj_set_width(save, LV_PCT(100));
    lv_obj_bind_state_if_eq(save, view_model_.legacy_ready_subject(), LV_STATE_DISABLED, 0);
    for (auto *control : {region_dropdown_, modem_dropdown_, role_dropdown_, owner_input_})
        lv_obj_bind_state_if_eq(control, view_model_.legacy_ready_subject(), LV_STATE_DISABLED, 0);
    lv_subject_add_observer_obj(view_model_.radio_identity_subject(), radio_identity_observer, owner_input_, this);
    lv_subject_add_observer_obj(view_model_.legacy_form_subject(), legacy_form_observer, owner_input_, this);
    lv_obj_t *request_config = settings_button(device, "Refresh radio configuration", COLOR_BLUE, COLOR_SURFACE,
                                                request_config_event, this);
    lv_obj_set_width(request_config, LV_PCT(100));
    lv_obj_t *request_owner = settings_button(device, "Refresh owner information", COLOR_BLUE, COLOR_SURFACE,
                                                request_owner_event, this);
    lv_obj_set_width(request_owner, LV_PCT(100));

    make_section_label(page_, "DEVICE AND MODULE SETTINGS");
    lv_obj_t *category_card = make_card(page_);
    lv_obj_set_name_static(category_card, "settings_configuration_catalog");
    make_wrapped_label(category_card, "Read a category from the radio, edit its fields, then save that category. Drafts survive category changes and refreshes.", 14, COLOR_SECONDARY);
    std::string categories;
    for (const auto &category : view_model_.configuration_categories()) {
        if (!categories.empty()) categories += '\n';
        categories += category.scope == meshtastic::configuration::Scope::device ? "Device / " : "Module / ";
        categories += category.title;
    }
    category_dropdown_ = lv_dropdown_create(category_card);
    lv_obj_set_name_static(category_dropdown_, "settings_category_dropdown");
    lv_obj_set_width(category_dropdown_, LV_PCT(100));
    lv_obj_set_height(category_dropdown_, 48);
    lv_dropdown_set_options(category_dropdown_, categories.c_str());
    lv_dropdown_set_selected(category_dropdown_, static_cast<std::uint16_t>(view_model_.selected_category_index()));
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(category_dropdown_, font, 0);
    lv_obj_add_event_cb(category_dropdown_, category_event, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_t *category_status = make_wrapped_label(category_card, "", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(category_status, "settings_category_status");
    lv_label_bind_text(category_status, view_model_.configuration_status_subject(), nullptr);
    auto *refresh = settings_button(category_card, "Refresh selected category", COLOR_BLUE, COLOR_SURFACE, refresh_category_event, this);
    lv_obj_set_width(refresh, LV_PCT(100));
    lv_obj_set_name_static(refresh, "settings_refresh_category");
    configuration_fields_ = make_flex(category_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(configuration_fields_, "settings_configuration_fields");
    lv_obj_set_width(configuration_fields_, LV_PCT(100));
    lv_obj_set_height(configuration_fields_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(configuration_fields_, 16, 0);
    auto *save_category = settings_button(category_card, "Save selected category", COLOR_SURFACE, COLOR_BLUE, save_category_event, this);
    lv_obj_set_width(save_category, LV_PCT(100));
    lv_obj_set_name_static(save_category, "settings_save_category");
    lv_obj_bind_state_if_eq(save_category, view_model_.configuration_ready_subject(), LV_STATE_DISABLED, 0);
    auto *discard = settings_button(category_card, "Discard category draft", COLOR_BLUE, COLOR_SURFACE, discard_category_event, this);
    lv_obj_set_width(discard, LV_PCT(100));
    lv_obj_set_name_static(discard, "settings_discard_category");
    rebuild_configuration_fields();
    lv_subject_add_observer_obj(view_model_.configuration_content_subject(), configuration_observer,
                                configuration_fields_, this);

    owner_keyboard_ = lv_keyboard_create(parent);
    lv_obj_set_name_static(owner_keyboard_, "settings_software_keyboard");
    lv_obj_set_width(owner_keyboard_, LV_PCT(100));
    lv_obj_set_height(owner_keyboard_, LV_PCT(42));
    lv_obj_set_style_min_height(owner_keyboard_, 180, 0);
    lv_obj_set_style_max_height(owner_keyboard_, 300, 0);
    lv_keyboard_set_textarea(owner_keyboard_, owner_input_);
    lv_obj_clear_state(owner_input_, LV_STATE_FOCUSED);
    lv_keyboard_set_popovers(owner_keyboard_, true);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(owner_keyboard_, font, 0);
    lv_obj_add_flag(owner_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(owner_keyboard_, owner_keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(owner_keyboard_, owner_keyboard_event, LV_EVENT_CANCEL, this);
    lv_obj_add_event_cb(owner_keyboard_, owner_keyboard_event, LV_EVENT_DELETE, this);

    make_section_label(page_, "POWER");
    lv_obj_t *power = make_card(page_);
    reboot_button_ = settings_button(power, "Reboot device", COLOR_BLUE, COLOR_SURFACE, action_event, this);
    lv_obj_set_width(reboot_button_, LV_PCT(100));
    lv_obj_set_name_static(reboot_button_, "settings_reboot");
    shutdown_button_ = settings_button(power, "Shut down device", COLOR_RED, COLOR_SURFACE, action_event, this);
    lv_obj_set_width(shutdown_button_, LV_PCT(100));
    lv_obj_set_name_static(shutdown_button_, "settings_shutdown");
    auto *reset = settings_button(power, "Reset node database", COLOR_RED, COLOR_SURFACE, action_event, this);
    lv_obj_set_width(reset, LV_PCT(100));
    lv_obj_set_name_static(reset, "settings_reset_node_database");

    lv_obj_t *notice = make_card(page_);
    lv_obj_set_style_bg_color(notice, lv_color_hex(COLOR_NOTICE), 0);
    make_wrapped_label(notice, "Device actions ask for confirmation and require an admin key. A queued command still awaits the device response.", 14,
                       COLOR_NOTICE_TEXT);
    return page_;
}

void DeviceToolsView::update_layout()
{
    lv_async_call_cancel(reveal_editor, this);
    if (owner_keyboard_ && active_input_ && !lv_obj_has_flag(owner_keyboard_, LV_OBJ_FLAG_HIDDEN))
        lv_async_call(reveal_editor, this);
}

void DeviceToolsView::reveal_editor(void *context)
{
    auto *view = static_cast<DeviceToolsView *>(context);
    if (view->owner_keyboard_ && view->active_input_ && !lv_obj_has_flag(view->owner_keyboard_, LV_OBJ_FLAG_HIDDEN))
        lv_obj_scroll_to_view_recursive(view->active_input_, LV_ANIM_OFF);
}

void DeviceToolsView::destroy() noexcept
{
    lv_async_call_cancel(reveal_editor, this);
    lv_async_call_cancel(rebuild_configuration_async, this);
    lv_async_call_cancel(radio_identity_async, this);
    lv_async_call_cancel(legacy_form_async, this);
    view_model_.set_editor_active(false);
    active_input_ = nullptr;
    active_field_ = nullptr;
    if (confirmation_ && lv_obj_is_valid(confirmation_)) lv_obj_delete(confirmation_);
    confirmation_ = nullptr;
    if (owner_keyboard_ && lv_obj_is_valid(owner_keyboard_)) {
        lv_keyboard_set_textarea(owner_keyboard_, nullptr);
        lv_obj_delete(owner_keyboard_);
    }
    owner_keyboard_ = nullptr;
    destroy_page_body(page_);
    region_dropdown_ = nullptr;
    modem_dropdown_  = nullptr;
    role_dropdown_   = nullptr;
    owner_input_     = nullptr;
    owner_text_before_edit_.clear();
    fields_.clear();
    category_dropdown_ = nullptr;
    configuration_fields_ = nullptr;
    reboot_button_ = nullptr;
    shutdown_button_ = nullptr;
}

void DeviceToolsView::back_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void DeviceToolsView::request_config_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->view_model_.request_full_config();
}

void DeviceToolsView::request_owner_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->view_model_.request_owner();
}

void DeviceToolsView::request_key_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->view_model_.request_session_key();
}

void DeviceToolsView::save_configuration_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view || !view->region_dropdown_ || !view->modem_dropdown_ || !view->role_dropdown_ ||
        !view->owner_input_)
        return;
    const char *owner_name = lv_textarea_get_text(view->owner_input_);
    view->view_model_.save_configuration(
        enum_value(lv_dropdown_get_selected(view->region_dropdown_), meshtastic::Config::LoRaConfig::RegionCode_MIN,
                   meshtastic::Config::LoRaConfig::RegionCode_MAX,
                   [](int value) { return meshtastic::Config::LoRaConfig::RegionCode_IsValid(value); }),
        enum_value(lv_dropdown_get_selected(view->modem_dropdown_), meshtastic::Config::LoRaConfig::ModemPreset_MIN,
                   meshtastic::Config::LoRaConfig::ModemPreset_MAX,
                   [](int value) { return meshtastic::Config::LoRaConfig::ModemPreset_IsValid(value); }),
        enum_value(lv_dropdown_get_selected(view->role_dropdown_), meshtastic::Config::DeviceConfig::Role_MIN,
                   meshtastic::Config::DeviceConfig::Role_MAX,
                   [](int value) { return meshtastic::Config::DeviceConfig::Role_IsValid(value); }),
                                          owner_name ? owner_name : "");
    view->hide_owner_keyboard();
}

void DeviceToolsView::owner_input_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view || !view->owner_input_) return;
    switch (lv_event_get_code(event)) {
        case LV_EVENT_FOCUSED:
        case LV_EVENT_CLICKED: {
            if (!view->owner_keyboard_ || lv_obj_has_flag(view->owner_keyboard_, LV_OBJ_FLAG_HIDDEN)) {
                const char *text = lv_textarea_get_text(view->owner_input_);
                view->owner_text_before_edit_ = text ? text : "";
            }
            view->show_owner_keyboard();
            break;
        }
        case LV_EVENT_DEFOCUSED:
            view->hide_owner_keyboard();
            break;
        case LV_EVENT_DELETE:
            if (view->owner_keyboard_) lv_keyboard_set_textarea(view->owner_keyboard_, nullptr);
            view->owner_input_ = nullptr;
            break;
        default:
            break;
    }
}

void DeviceToolsView::owner_keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view) return;
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        view->owner_keyboard_ = nullptr;
        return;
    }
    if (view->active_field_) {
        if (lv_event_get_code(event) == LV_EVENT_CANCEL && view->active_input_) {
            lv_textarea_set_text(view->active_input_, view->field_text_before_edit_.c_str());
            view->view_model_.edit_field(view->active_field_->index, view->field_text_before_edit_);
        }
        view->hide_owner_keyboard();
        return;
    }
    auto *input = lv_indev_active();
    if (lv_event_get_code(event) == LV_EVENT_READY && input &&
        lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD)
        save_configuration_event(event);
    if (lv_event_get_code(event) == LV_EVENT_CANCEL) view->restore_owner_text();
    view->hide_owner_keyboard();
}

void DeviceToolsView::show_owner_keyboard()
{
    active_field_ = nullptr;
    active_input_ = owner_input_;
    if (!owner_keyboard_ || !owner_input_) return;
    lv_keyboard_set_textarea(owner_keyboard_, owner_input_);
    lv_keyboard_set_mode(owner_keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    if (cm0_input_hardware_keyboard_present()) return;
    lv_obj_clear_flag(owner_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(lv_obj_get_parent(page_));
    lv_obj_scroll_to_view_recursive(owner_input_, LV_ANIM_OFF);
}

void DeviceToolsView::hide_owner_keyboard()
{
    if (owner_keyboard_ && lv_obj_is_valid(owner_keyboard_)) lv_obj_add_flag(owner_keyboard_, LV_OBJ_FLAG_HIDDEN);
    if (active_field_) view_model_.set_editor_active(false);
    active_field_ = nullptr;
    active_input_ = nullptr;
}

void DeviceToolsView::restore_owner_text()
{
    if (!owner_input_ || !lv_obj_is_valid(owner_input_)) return;
    lv_textarea_set_text(owner_input_, owner_text_before_edit_.c_str());
}

void DeviceToolsView::connected_indicator_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *indicator = lv_observer_get_target_obj(observer);
    const auto color    = lv_subject_get_int(subject) != 0 ? COLOR_GREEN_LIGHT : COLOR_NEUTRAL_FILL;
    lv_obj_set_style_bg_color(indicator, lv_color_hex(color), 0);
}

void DeviceToolsView::configuration_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<DeviceToolsView *>(lv_observer_get_user_data(observer));
    if (!view || !view->configuration_fields_) return;
    lv_async_call_cancel(rebuild_configuration_async, view);
    lv_async_call(rebuild_configuration_async, view);
}

void DeviceToolsView::radio_identity_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<DeviceToolsView *>(lv_observer_get_user_data(observer));
    if (!view || !view->page_) return;
    lv_async_call_cancel(radio_identity_async, view);
    lv_async_call(radio_identity_async, view);
}

void DeviceToolsView::radio_identity_async(void *context)
{
    auto *view = static_cast<DeviceToolsView *>(context);
    if (!view || !view->page_) return;
    view->hide_owner_keyboard();
    view->field_text_before_edit_.clear();
    view->owner_text_before_edit_.clear();
    if (view->confirmation_ && lv_obj_is_valid(view->confirmation_)) lv_obj_delete(view->confirmation_);
    view->confirmation_ = nullptr;
}

void DeviceToolsView::legacy_form_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<DeviceToolsView *>(lv_observer_get_user_data(observer));
    if (!view || !view->page_) return;
    lv_async_call_cancel(legacy_form_async, view);
    lv_async_call(legacy_form_async, view);
}

void DeviceToolsView::legacy_form_async(void *context)
{
    auto *view = static_cast<DeviceToolsView *>(context);
    if (!view || !view->region_dropdown_ || !view->modem_dropdown_ || !view->role_dropdown_ || !view->owner_input_) return;
    const bool ready = lv_subject_get_int(view->view_model_.legacy_ready_subject()) != 0;
    lv_dropdown_set_selected(view->region_dropdown_, enum_index(
        ready ? view->view_model_.selected_region() : meshtastic::Config::LoRaConfig::UNSET,
        meshtastic::Config::LoRaConfig::RegionCode_MIN, meshtastic::Config::LoRaConfig::RegionCode_MAX,
        [](int value) { return meshtastic::Config::LoRaConfig::RegionCode_IsValid(value); }));
    lv_dropdown_set_selected(view->modem_dropdown_, enum_index(
        ready ? view->view_model_.selected_modem_preset() : meshtastic::Config::LoRaConfig::LONG_FAST,
        meshtastic::Config::LoRaConfig::ModemPreset_MIN, meshtastic::Config::LoRaConfig::ModemPreset_MAX,
        [](int value) { return meshtastic::Config::LoRaConfig::ModemPreset_IsValid(value); }));
    lv_dropdown_set_selected(view->role_dropdown_, enum_index(
        ready ? view->view_model_.selected_device_role() : meshtastic::Config::DeviceConfig::CLIENT,
        meshtastic::Config::DeviceConfig::Role_MIN, meshtastic::Config::DeviceConfig::Role_MAX,
        [](int value) { return meshtastic::Config::DeviceConfig::Role_IsValid(value); }));
    lv_textarea_set_text(view->owner_input_, ready ? lv_subject_get_string(view->view_model_.owner_name_subject()) : "");
}
void DeviceToolsView::rebuild_configuration_async(void *context)
{
    auto *view = static_cast<DeviceToolsView *>(context);
    if (view && view->configuration_fields_) view->rebuild_configuration_fields();
}
void DeviceToolsView::category_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view || !view->category_dropdown_) return;
    view->hide_owner_keyboard();
    view->view_model_.select_category(lv_dropdown_get_selected(view->category_dropdown_));
}
void DeviceToolsView::refresh_category_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (view) view->view_model_.refresh_selected_configuration();
}
void DeviceToolsView::save_category_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view) return;
    view->hide_owner_keyboard();
    view->view_model_.save_selected_configuration();
}
void DeviceToolsView::discard_category_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view) return;
    view->hide_owner_keyboard();
    view->view_model_.discard_selected_configuration();
}
void DeviceToolsView::rebuild_configuration_fields()
{
    using meshtastic::configuration::FieldType;
    if (!configuration_fields_) return;
    // Refreshes never rebuild an active editor. Resize only changes geometry.
    if (active_field_) return;
    const auto scroll_y = page_ ? lv_obj_get_scroll_y(page_) : 0;
    building_fields_ = true;
    lv_obj_clean(configuration_fields_);
    fields_.clear();
    const auto &draft = view_model_.selected_draft();
    if (!draft.loaded()) {
        make_wrapped_label(configuration_fields_, "No configuration received for this category. Connect and refresh it to edit the device's current settings.", 14, COLOR_SECONDARY);
    } else {
        const auto *category = draft.category();
        for (std::size_t index = 0; index < category->fields.size(); ++index) {
            const auto &field = category->fields[index];
            const auto value = view_model_.field_value(index);
            auto context = std::make_unique<FieldContext>();
            context->view = this;
            context->index = index;
            auto *row = make_flex(configuration_fields_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_row(row, 6, 0);
            make_wrapped_label(row, field.label.c_str(), 14, COLOR_TEXT);
            if (!field.help.empty()) make_wrapped_label(row, field.help.c_str(), 14, COLOR_SECONDARY);
            lv_obj_t *control = nullptr;
            if (field.type == FieldType::boolean) {
                control = lv_switch_create(row);
                lv_obj_set_size(control, 64, 44);
                if (value == "true") lv_obj_add_state(control, LV_STATE_CHECKED);
                lv_obj_add_event_cb(control, field_event, LV_EVENT_VALUE_CHANGED, context.get());
            } else if (field.type == FieldType::enumeration) {
                control = lv_dropdown_create(row);
                std::string options;
                std::uint16_t selected = 0;
                bool found = false;
                for (std::size_t choice = 0; choice < field.choices.size(); ++choice) {
                    if (!options.empty()) options += '\n';
                    options += field.choices[choice].label;
                    if (value == std::to_string(field.choices[choice].value)) {
                        selected = static_cast<std::uint16_t>(choice);
                        found = true;
                    }
                }
                if (!found) {
                    selected = static_cast<std::uint16_t>(field.choices.size());
                    if (!options.empty()) options += '\n';
                    options += "Unknown firmware value (preserved)";
                }
                lv_dropdown_set_options(control, options.c_str());
                lv_dropdown_set_selected(control, selected);
                lv_obj_set_width(control, LV_PCT(100));
                lv_obj_set_height(control, 48);
                lv_obj_add_event_cb(control, field_event, LV_EVENT_VALUE_CHANGED, context.get());
            } else {
                control = lv_textarea_create(row);
                lv_obj_set_width(control, LV_PCT(100));
                lv_obj_set_height(control, 48);
                lv_textarea_set_one_line(control, true);
                lv_textarea_set_max_length(control, static_cast<std::uint32_t>(field.type == FieldType::text ? field.maximum_bytes : 32));
                lv_textarea_set_password_mode(control, field.secret);
                lv_textarea_set_text(control, value.c_str());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_VALUE_CHANGED, context.get());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_FOCUSED, context.get());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_CLICKED, context.get());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_DEFOCUSED, context.get());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_READY, context.get());
                lv_obj_add_event_cb(control, field_event, LV_EVENT_DELETE, context.get());
            }
            if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(control, font, 0);
            const auto name = std::string("settings_field_") + (category->scope == meshtastic::configuration::Scope::device ? "device_" : "module_") + std::to_string(category->payload_case) + "_" + field.id;
            lv_obj_set_name(control, name.c_str());
            context->control = control;
            fields_.push_back(std::move(context));
        }
    }
    building_fields_ = false;
    if (page_) {
        lv_obj_update_layout(page_);
        lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
    }
}
void DeviceToolsView::field_event(lv_event_t *event)
{
    auto *context = static_cast<FieldContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    auto *view = context->view;
    if (view->building_fields_) return;
    const auto &draft = view->view_model_.selected_draft();
    if (!draft.loaded() || context->index >= draft.category()->fields.size()) return;
    const auto &field = draft.category()->fields[context->index];
    switch (lv_event_get_code(event)) {
        case LV_EVENT_VALUE_CHANGED: {
            std::string value;
            if (field.type == meshtastic::configuration::FieldType::boolean) value = lv_obj_has_state(context->control, LV_STATE_CHECKED) ? "true" : "false";
            else if (field.type == meshtastic::configuration::FieldType::enumeration) {
                const auto selected = lv_dropdown_get_selected(context->control);
                if (selected >= field.choices.size()) return;
                value = std::to_string(field.choices[selected].value);
            } else value = lv_textarea_get_text(context->control);
            view->view_model_.edit_field(context->index, value);
            break;
        }
        case LV_EVENT_FOCUSED:
        case LV_EVENT_CLICKED: view->show_field_keyboard(context); break;
        case LV_EVENT_READY: view->hide_owner_keyboard(); break;
        case LV_EVENT_DEFOCUSED:
            if (view->active_field_ == context) view->hide_owner_keyboard();
            break;
        case LV_EVENT_DELETE:
            if (view->active_input_ == context->control) {
                if (view->owner_keyboard_) lv_keyboard_set_textarea(view->owner_keyboard_, nullptr);
                view->active_input_ = nullptr;
                view->active_field_ = nullptr;
                view->view_model_.set_editor_active(false);
            }
            break;
        default: break;
    }
}
void DeviceToolsView::show_field_keyboard(FieldContext *context)
{
    if (!context || !context->control || !owner_keyboard_) return;
    if (active_field_ != context) field_text_before_edit_ = lv_textarea_get_text(context->control);
    active_field_ = context;
    active_input_ = context->control;
    view_model_.set_editor_active(true);
    lv_keyboard_set_textarea(owner_keyboard_, active_input_);
    const auto type = view_model_.selected_draft().category()->fields[context->index].type;
    lv_keyboard_set_mode(owner_keyboard_, type == meshtastic::configuration::FieldType::text
                                               ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_NUMBER);
    if (!cm0_input_hardware_keyboard_present()) {
        lv_obj_clear_flag(owner_keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(lv_obj_get_parent(page_));
    }
    lv_obj_scroll_to_view_recursive(active_input_, LV_ANIM_OFF);
}
void DeviceToolsView::action_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view) return;
    auto *target = lv_event_get_target_obj(event);
    auto action = DeviceToolsViewModel::DeviceAction::reset_node_database;
    if (target == view->reboot_button_) action = DeviceToolsViewModel::DeviceAction::reboot;
    else if (target == view->shutdown_button_) action = DeviceToolsViewModel::DeviceAction::shutdown;
    view->show_action_confirmation(action);
}
void DeviceToolsView::show_action_confirmation(DeviceToolsViewModel::DeviceAction action)
{
    if (confirmation_) return;
    hide_owner_keyboard();
    pending_action_ = action;
    confirmation_ = lv_msgbox_create(page_);
    lv_obj_set_name_static(confirmation_, "settings_action_confirmation");
    lv_obj_add_flag(confirmation_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(confirmation_, LV_PCT(94));
    lv_obj_set_style_max_height(confirmation_, LV_PCT(90), 0);
    lv_obj_center(confirmation_);
    const char *title = action == DeviceToolsViewModel::DeviceAction::reboot ? "Reboot device?"
                         : action == DeviceToolsViewModel::DeviceAction::shutdown ? "Shut down device?" : "Reset node database?";
    const char *detail = action == DeviceToolsViewModel::DeviceAction::reset_node_database
        ? "This removes the radio's discovered node database. Nodes will be rediscovered as traffic arrives. Your channels and radio settings are kept."
        : action == DeviceToolsViewModel::DeviceAction::shutdown
        ? "The connected radio will shut down. It may need a physical power button to start again."
        : "The connected radio will restart and the connection will be interrupted.";
    lv_msgbox_add_title(confirmation_, title);
    lv_msgbox_add_text(confirmation_, detail);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(confirmation_, font, 0);
    auto *cancel = lv_msgbox_add_footer_button(confirmation_, "Cancel");
    lv_obj_set_height(cancel, 48);
    lv_obj_add_event_cb(cancel, cancel_action_event, LV_EVENT_CLICKED, this);
    auto *confirm = lv_msgbox_add_footer_button(confirmation_, "Confirm");
    lv_obj_set_name_static(confirm, "settings_confirm_action");
    lv_obj_set_height(confirm, 48);
    lv_obj_add_event_cb(confirm, confirm_action_event, LV_EVENT_CLICKED, this);
}
void DeviceToolsView::confirm_action_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view || !view->confirmation_) return;
    view->view_model_.perform_device_action(view->pending_action_);
    lv_msgbox_close_async(view->confirmation_);
    view->confirmation_ = nullptr;
}
void DeviceToolsView::cancel_action_event(lv_event_t *event)
{
    auto *view = static_cast<DeviceToolsView *>(lv_event_get_user_data(event));
    if (!view || !view->confirmation_) return;
    lv_msgbox_close_async(view->confirmation_);
    view->confirmation_ = nullptr;
}
