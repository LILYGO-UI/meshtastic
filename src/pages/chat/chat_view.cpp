#include "pages/chat/chat_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

ChatView::ChatView(ChatViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

ChatView::~ChatView()
{
    destroy();
}

lv_obj_t *ChatView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "", nullptr, nullptr, nullptr, true, nullptr);
    lv_obj_t *header  = lv_obj_get_child(page_, 0);
    lv_obj_t *back    = header ? lv_obj_get_child(header, 0) : nullptr;
    lv_obj_t *heading = header ? lv_obj_get_child(header, 1) : nullptr;
    if (back) lv_obj_add_event_cb(back, back_event, LV_EVENT_CLICKED, this);
    if (heading) lv_label_bind_text(heading, view_model_.channel_subject(), nullptr);

    lv_obj_t *online = make_label(page_, "", 14, COLOR_GREEN);
    lv_obj_set_width(online, LV_PCT(100));
    lv_obj_set_style_text_align(online, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_bind_text(online, view_model_.node_status_subject(), nullptr);
    lv_obj_t *date = make_label(page_, "TODAY", 14, COLOR_SECONDARY);
    lv_obj_set_width(date, LV_PCT(100));
    lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);

    messages_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(messages_, "chat_messages");
    lv_obj_set_width(messages_, LV_PCT(100));
    lv_obj_set_height(messages_, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(messages_, 950, 0);
    lv_obj_set_style_pad_row(messages_, 12, 0);

    lv_obj_t *spacer = lv_obj_create(page_);
    style_container(spacer, COLOR_PAGE, 0);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *composer = make_flex(page_, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(composer, LV_PCT(100));
    lv_obj_set_height(composer, 54);
    style_container(composer, COLOR_SURFACE, 27);
    lv_obj_set_style_pad_all(composer, 7, 0);
    lv_obj_set_style_pad_column(composer, 6, 0);
    lv_obj_t *attach = make_text_button(composer, "+", COLOR_SECONDARY, COLOR_NEUTRAL_FILL, nullptr, nullptr);
    lv_obj_set_size(attach, 40, 40);
    composer_ = lv_textarea_create(composer);
    lv_obj_set_name_static(composer_, "chat_composer_input");
    lv_obj_set_flex_grow(composer_, 1);
    lv_obj_set_height(composer_, 40);
    lv_textarea_set_one_line(composer_, true);
    lv_textarea_set_placeholder_text(composer_, "Message");
    if (const auto *font = lilygo_ui_font_get(22)) lv_obj_set_style_text_font(composer_, font, 0);
    lv_obj_set_style_border_width(composer_, 0, 0);
    lv_obj_set_style_bg_opa(composer_, LV_OPA_TRANSP, 0);
    lv_obj_t *send = make_text_button(composer, LV_SYMBOL_UP, COLOR_SURFACE, COLOR_BLUE, send_event, this);
    lv_obj_set_name_static(send, "chat_send");
    lv_obj_set_size(send, 40, 40);
    lv_obj_bind_state_if_eq(send, view_model_.connected_subject(), LV_STATE_DISABLED, 0);

    lv_obj_t *offline = make_label(page_, "Connect before sending", 14, COLOR_RED);
    lv_obj_bind_flag_if_eq(offline, view_model_.connected_subject(), LV_OBJ_FLAG_HIDDEN, 1);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, messages_, this);
    return page_;
}

void ChatView::destroy() noexcept
{
    destroy_page_body(page_);
    messages_ = nullptr;
    composer_ = nullptr;
}

void ChatView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<ChatView *>(lv_observer_get_user_data(observer));
    if (view) view->render_messages();
}

void ChatView::back_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void ChatView::send_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (!view || !view->composer_) return;
    const char *text = lv_textarea_get_text(view->composer_);
    if (view->view_model_.send_text(text)) lv_textarea_set_text(view->composer_, "");
}

void ChatView::render_messages()
{
    if (!page_ || !messages_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(messages_);
    if (view_model_.messages().empty()) {
        make_label(messages_, "No messages in this session", 14, COLOR_SECONDARY);
    }
    for (const auto &message : view_model_.messages()) {
        if (message.outgoing) {
            lv_obj_t *wrap = make_flex(messages_, LV_FLEX_FLOW_ROW);
            lv_obj_set_width(wrap, LV_PCT(100));
            lv_obj_set_height(wrap, LV_SIZE_CONTENT);
            lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *bubble = make_card(wrap);
            lv_obj_set_name_static(bubble, "chat_message");
            lv_obj_set_width(bubble, LV_PCT(68));
            lv_obj_set_style_bg_color(bubble, lv_color_hex(COLOR_BLUE), 0);
            make_wrapped_label(bubble, message.text.c_str(), 14, COLOR_SURFACE);
        } else {
            lv_obj_t *row = make_flex(messages_, LV_FLEX_FLOW_ROW);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_t *avatar           = make_dot(row, COLOR_GREEN_LIGHT, 40);
            const std::string initials = message.sender.size() >= 2 ? message.sender.substr(0, 2) : message.sender;
            lv_obj_t *initials_label   = make_label(avatar, initials.c_str(), 14, COLOR_GREEN_DARK);
            lv_obj_center(initials_label);
            lv_obj_t *stack = make_flex(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_width(stack, LV_PCT(72));
            lv_obj_set_height(stack, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_row(stack, 4, 0);
            make_wrapped_label(stack, message.sender.c_str(), 14, COLOR_GREEN_DARK);
            lv_obj_t *bubble = make_card(stack);
            lv_obj_set_name_static(bubble, "chat_message");
            make_wrapped_label(bubble, message.text.c_str(), 14, COLOR_TEXT);
        }
    }
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
