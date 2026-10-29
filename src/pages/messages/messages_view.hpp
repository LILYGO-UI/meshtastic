#ifndef LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_HPP

#include "app_router.hpp"
#include "components/search_input.hpp"
#include "pages/page_view.hpp"
#include "pages/messages/messages_view_model.hpp"

#include <lvgl.h>

#include <cstdint>
#include <vector>

class MessagesView final : public PageView {
public:
    MessagesView(MessagesViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~MessagesView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    struct ConversationContext {
        MessagesView *view = nullptr;
        bool direct = false;
        std::uint32_t peer = 0;
        std::uint32_t channel = 0;
        std::string name;
    };

    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void compose_event(lv_event_t *event);
    static void channels_event(lv_event_t *event);
    static void conversation_event(lv_event_t *event);

    void render_rows();

    MessagesViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_   = nullptr;
    lv_obj_t *list_   = nullptr;
    lv_obj_t *footer_ = nullptr;
    std::vector<ConversationContext> conversation_contexts_;
    SearchInput search_;
};

#endif
