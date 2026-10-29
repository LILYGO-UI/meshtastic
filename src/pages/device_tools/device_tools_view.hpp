#ifndef LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_HPP

#include "app_router.hpp"
#include "pages/device_tools/device_tools_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

#include <string>
#include <memory>
#include <vector>

class DeviceToolsView final : public PageView {
public:
    DeviceToolsView(DeviceToolsViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~DeviceToolsView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void update_layout() override;
    void destroy() noexcept override;

private:
    struct FieldContext {
        DeviceToolsView *view;
        std::size_t index;
        lv_obj_t *control = nullptr;
    };
    static void reveal_editor(void *context);
    static void rebuild_configuration_async(void *context);
    static void radio_identity_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void radio_identity_async(void *context);
    static void legacy_form_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void legacy_form_async(void *context);
    static void configuration_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void category_event(lv_event_t *event);
    static void field_event(lv_event_t *event);
    static void refresh_category_event(lv_event_t *event);
    static void save_category_event(lv_event_t *event);
    static void discard_category_event(lv_event_t *event);
    static void action_event(lv_event_t *event);
    static void confirm_action_event(lv_event_t *event);
    static void cancel_action_event(lv_event_t *event);
    void rebuild_configuration_fields();
    void show_field_keyboard(FieldContext *context);
    void show_action_confirmation(DeviceToolsViewModel::DeviceAction action);
    static void back_event(lv_event_t *event);
    static void connected_indicator_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void request_config_event(lv_event_t *event);
    static void request_owner_event(lv_event_t *event);
    static void request_key_event(lv_event_t *event);
    static void save_configuration_event(lv_event_t *event);
    static void owner_input_event(lv_event_t *event);
    static void owner_keyboard_event(lv_event_t *event);

    void show_owner_keyboard();
    void hide_owner_keyboard();
    void restore_owner_text();

    DeviceToolsViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_            = nullptr;
    lv_obj_t *region_dropdown_ = nullptr;
    lv_obj_t *modem_dropdown_  = nullptr;
    lv_obj_t *role_dropdown_   = nullptr;
    lv_obj_t *owner_input_     = nullptr;
    lv_obj_t *owner_keyboard_  = nullptr;
    std::string owner_text_before_edit_;
    lv_obj_t *category_dropdown_ = nullptr;
    lv_obj_t *configuration_fields_ = nullptr;
    lv_obj_t *active_input_ = nullptr;
    FieldContext *active_field_ = nullptr;
    lv_obj_t *confirmation_ = nullptr;
    lv_obj_t *reboot_button_ = nullptr;
    lv_obj_t *shutdown_button_ = nullptr;
    DeviceToolsViewModel::DeviceAction pending_action_ = DeviceToolsViewModel::DeviceAction::reboot;
    std::string field_text_before_edit_;
    std::vector<std::unique_ptr<FieldContext>> fields_;
    bool building_fields_ = false;
};

#endif
