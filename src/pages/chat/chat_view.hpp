#ifndef LILYGO_UI_MESHTASTIC_CHAT_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_CHAT_VIEW_HPP

#include "app_router.hpp"
#include "pages/chat/chat_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

class ChatView final : public PageView {
public:
    ChatView(ChatViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~ChatView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void back_event(lv_event_t *event);
    static void send_event(lv_event_t *event);

    void render_messages();

    ChatViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_     = nullptr;
    lv_obj_t *messages_ = nullptr;
    lv_obj_t *composer_ = nullptr;
};

#endif
