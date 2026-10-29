#ifndef LILYGO_UI_MESHTASTIC_CHAT_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_CHAT_VIEW_HPP

#include "app_router.hpp"
#include "pages/chat/chat_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

#include <cstdint>
#include <vector>

class ChatView final : public PageView {
public:
    ChatView(ChatViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~ChatView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void prepare_layout() override;
    void update_layout() override;
    void destroy() noexcept override;

private:
    static void reveal_editor(void *context);
    static void restore_messages_scroll(void *context);
    static void clear_keyboard_suppression(void *context);
    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void back_event(lv_event_t *event);
    static void send_event(lv_event_t *event);
    static void composer_focus_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    static void draft_event(lv_event_t *event);
    static void load_older_event(lv_event_t *event);
    static void retry_event(lv_event_t *event);
    static void process_message_action(void *context);

    void render_messages();
    void show_keyboard();
    void hide_keyboard();

    ChatViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_     = nullptr;
    lv_obj_t *messages_ = nullptr;
    lv_obj_t *composer_ = nullptr;
    lv_obj_t *keyboard_ = nullptr;
    std::int32_t messages_scroll_y_ = 0;
    bool keyboard_visible_ = false;
    bool suppress_keyboard_ = false;
    std::int32_t layout_width_ = 0;
    std::int32_t layout_height_ = 0;
    struct RetryContext { ChatView *view; std::int64_t id; };
    std::vector<RetryContext> retry_contexts_;
    std::int64_t pending_retry_ = 0;
    bool pending_load_older_ = false;
};

#endif
