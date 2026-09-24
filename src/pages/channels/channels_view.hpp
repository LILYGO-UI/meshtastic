#ifndef LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_HPP

#include "app_router.hpp"
#include "pages/channels/channels_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

#include <cstdint>
#include <vector>

class ChannelsView final : public PageView {
public:
    ChannelsView(ChannelsViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~ChannelsView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    struct RowContext {
        ChannelsView *view   = nullptr;
        std::int32_t channel = 0;
    };

    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void back_event(lv_event_t *event);
    static void channel_event(lv_event_t *event);

    void render_channels();

    ChannelsViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_    = nullptr;
    lv_obj_t *content_ = nullptr;
    std::vector<RowContext> row_contexts_;
};

#endif
