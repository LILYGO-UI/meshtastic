#ifndef LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_HPP

#include "app_router.hpp"
#include "pages/channels/channels_view_model.hpp"
#include "pages/page_view.hpp"
#include <lvgl.h>
#include <cstdint>
#include <string>
#include <vector>

class ChannelsView final : public PageView {
public:
    ChannelsView(ChannelsViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~ChannelsView() override;
    lv_obj_t *create(lv_obj_t *parent) override;
    void update_layout() override;
    void destroy() noexcept override;
private:
    struct RowContext { ChannelsView *view = nullptr; std::int32_t channel = 0; };
    static void content_observer(lv_observer_t *, lv_subject_t *);
    static void editor_observer(lv_observer_t *, lv_subject_t *);
    static void radio_identity_observer(lv_observer_t *, lv_subject_t *);
    static void render_async(void *context);
    static void editor_async(void *context);
    static void radio_identity_async(void *context);
    static void reveal_async(void *context);
    static void back_event(lv_event_t *);
    static void channel_event(lv_event_t *);
    static void add_event(lv_event_t *);
    static void edit_event(lv_event_t *);
    static void refresh_event(lv_event_t *);
    static void save_event(lv_event_t *);
    static void discard_event(lv_event_t *);
    static void preview_import_event(lv_event_t *);
    static void apply_import_event(lv_event_t *);
    static void share_event(lv_event_t *);
    static void confirm_event(lv_event_t *);
    static void cancel_event(lv_event_t *);
    static void input_event(lv_event_t *);
    static void keyboard_event(lv_event_t *);
    static void key_mode_event(lv_event_t *);
    void render_channels();
    void render_editor();
    void show_confirmation(bool import);
    void hide_keyboard();
    ChannelsViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_ = nullptr, *content_ = nullptr, *editor_ = nullptr;
    lv_obj_t *slot_ = nullptr, *name_ = nullptr, *role_ = nullptr, *key_mode_ = nullptr, *key_ = nullptr;
    lv_obj_t *uplink_ = nullptr, *downlink_ = nullptr;
    lv_obj_t *import_link_ = nullptr, *import_summary_ = nullptr, *include_lora_ = nullptr, *shared_link_ = nullptr;
    lv_obj_t *keyboard_ = nullptr, *active_input_ = nullptr, *confirmation_ = nullptr;
    std::string input_before_edit_;
    bool confirm_import_ = false;
    bool confirm_lora_ = false;
    std::vector<RowContext> row_contexts_;
};
#endif
