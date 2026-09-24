#ifndef LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_HPP

#include "app_router.hpp"
#include "pages/device_tools/device_tools_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

class DeviceToolsView final : public PageView {
public:
    DeviceToolsView(DeviceToolsViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~DeviceToolsView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    static void back_event(lv_event_t *event);
    static void connected_indicator_observer(lv_observer_t *observer, lv_subject_t *subject);

    DeviceToolsViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_ = nullptr;
};

#endif
