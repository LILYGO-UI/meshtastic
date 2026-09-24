#include "pages/messages/messages_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

MessagesView::MessagesView(MessagesViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

MessagesView::~MessagesView()
{
    destroy();
}

lv_obj_t *MessagesView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);

    lv_obj_t *compose_label = make_header(page_, "Message", "+", compose_event, this, false);
    if (compose_label) {
        lv_obj_t *compose = lv_obj_get_parent(compose_label);
        lv_obj_set_name_static(compose, "messages_compose");
    }

    make_search_field(page_, "Search conversations");
    make_section_label(page_, "RECENT");
    list_ = make_card(page_);
    lv_obj_set_name_static(list_, "messages_list");

    lv_obj_t *channels = make_text_button(page_, "Channels", COLOR_BLUE, COLOR_PAGE, channels_event, this);
    lv_obj_set_name_static(channels, "messages_channels");
    lv_obj_set_width(channels, LV_PCT(100));

    footer_ = make_label(page_, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(footer_, view_model_.footer_subject(), nullptr);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, list_, this);
    return page_;
}

void MessagesView::destroy() noexcept
{
    destroy_page_body(page_);
    list_   = nullptr;
    footer_ = nullptr;
}

void MessagesView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<MessagesView *>(lv_observer_get_user_data(observer));
    if (view) view->render_rows();
}

void MessagesView::compose_event(lv_event_t *event)
{
    static_cast<MessagesView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::chat);
}

void MessagesView::channels_event(lv_event_t *event)
{
    static_cast<MessagesView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::channels);
}

void MessagesView::conversation_event(lv_event_t *event)
{
    static_cast<MessagesView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::chat);
}

void MessagesView::render_rows()
{
    if (!page_ || !list_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(list_);
    if (view_model_.rows().empty()) {
        make_wrapped_label(
            list_, view_model_.connected() ? "No text messages received yet." : "Connect to receive mesh messages.", 14,
            COLOR_SECONDARY);
    }
    for (const auto &row : view_model_.rows()) {
        lv_obj_t *message = make_info_row(list_, row.outgoing ? COLOR_BLUE : COLOR_PALE_BLUE, row.sender.c_str(),
                                          row.text.c_str(), conversation_event, this);
        lv_obj_set_name_static(message, "message_conversation");
    }
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
