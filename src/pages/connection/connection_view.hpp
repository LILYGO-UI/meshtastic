#ifndef LILYGO_UI_MESHTASTIC_CONNECTION_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_CONNECTION_VIEW_HPP

#include "app_router.hpp"
#include "pages/connection/connection_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

class ConnectionView final : public PageView {
public:
    ConnectionView(ConnectionViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~ConnectionView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    static void toggle_connection_event(lv_event_t *event);
    static void device_tools_event(lv_event_t *event);

    ConnectionViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_ = nullptr;
};

#endif
