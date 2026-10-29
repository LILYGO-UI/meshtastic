#include "pages/chat/chat_view.hpp"

#include "app.hpp"
#include "components/components.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <algorithm>

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
    // Reserve space for the keyboard without covering the active editor.
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_set_height(page_, 0);
    lv_obj_set_flex_grow(page_, 1);
    // Keep the conversation editor anchored to the bottom of the page. The
    // page itself must not scroll; only the message history can grow and
    // scroll inside the space left between the header and composer.
    lv_obj_clear_flag(page_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page_, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(page_, LV_SCROLLBAR_MODE_OFF);
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
    lv_obj_t *date = make_label(page_, "MESSAGE HISTORY", 14, COLOR_SECONDARY);
    lv_obj_set_width(date, LV_PCT(100));
    lv_obj_set_style_text_align(date, LV_TEXT_ALIGN_CENTER, 0);

    messages_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(messages_, "chat_messages");
    lv_obj_set_width(messages_, LV_PCT(100));
    lv_obj_set_height(messages_, 0);
    lv_obj_set_flex_grow(messages_, 1);
    lv_obj_add_flag(messages_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(messages_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(messages_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_min_height(messages_, 0, 0);
    lv_obj_set_style_pad_row(messages_, 12, 0);

    lv_obj_t *offline = make_label(page_, "Connect before sending", 14, COLOR_RED);
    lv_obj_bind_flag_if_eq(offline, view_model_.connected_subject(), LV_OBJ_FLAG_HIDDEN, 1);

    lv_obj_t *composer = make_flex(page_, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(composer, LV_PCT(100));
    lv_obj_set_height(composer, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(composer, 62, 0);
    style_container(composer, COLOR_SURFACE, 27);
    lv_obj_set_style_pad_all(composer, 7, 0);
    lv_obj_set_style_pad_column(composer, 6, 0);
    lv_obj_set_flex_align(composer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    composer_ = lv_textarea_create(composer);
    lv_obj_set_name_static(composer_, "chat_composer_input");
    lv_obj_set_flex_grow(composer_, 1);
    lv_obj_set_height(composer_, 48);
    lv_textarea_set_one_line(composer_, true);
    lv_obj_add_event_cb(composer_, send_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(composer_, composer_focus_event, LV_EVENT_DEFOCUSED, this);
    lv_obj_add_event_cb(composer_, composer_focus_event, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(composer_, composer_focus_event, LV_EVENT_DELETE, this);
    lv_textarea_set_placeholder_text(composer_, "Message");
    lv_textarea_set_text(composer_, view_model_.draft().c_str());
    lv_obj_add_event_cb(composer_, draft_event, LV_EVENT_VALUE_CHANGED, this);
    if (const auto *font = lilygo_ui_font_get(22)) lv_obj_set_style_text_font(composer_, font, 0);
    lv_obj_set_style_border_width(composer_, 0, 0);
    lv_obj_set_style_bg_opa(composer_, LV_OPA_TRANSP, 0);
    lv_obj_t *send = make_text_button(composer, LV_SYMBOL_UP, COLOR_SURFACE, COLOR_BLUE, send_event, this);
    lv_obj_set_name_static(send, "chat_send");
    lv_obj_set_size(send, 44, 44);
    lv_obj_set_style_pad_all(send, 0, 0);
    lv_obj_bind_state_if_eq(send, view_model_.connected_subject(), LV_STATE_DISABLED, 0);

    lv_obj_t *feedback = make_wrapped_label(page_, "", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(feedback, "chat_feedback");
    lv_label_bind_text(feedback, view_model_.feedback_subject(), nullptr);

    keyboard_ = lv_keyboard_create(parent);
    lv_obj_set_name_static(keyboard_, "chat_software_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    // Keep enough room for the fixed header and composer even in a short
    // landscape viewport after the software keyboard opens.
    lv_obj_set_height(keyboard_, LV_PCT(25));
    lv_obj_set_style_min_height(keyboard_, 80, 0);
    lv_obj_set_style_max_height(keyboard_, 280, 0);
    lv_keyboard_set_textarea(keyboard_, composer_);
    lv_obj_clear_state(composer_, LV_STATE_FOCUSED);
    lv_keyboard_set_popovers(keyboard_, true);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(keyboard_, font, 0);
    // A hidden flex child can still contribute its minimum size during an
    // intermediate layout pass. Ignore it completely while the editor is not
    // active so the page keeps the full viewport height.
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_CANCEL, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_DELETE, this);

    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, messages_, this);
    render_messages();
    return page_;
}

void ChatView::update_layout()
{
    lv_async_call_cancel(reveal_editor, this);
    lv_async_call_cancel(restore_messages_scroll, this);
    lv_async_call_cancel(clear_keyboard_suppression, this);
    if (messages_ && messages_scroll_y_ > 0) lv_async_call(restore_messages_scroll, this);
    if (!keyboard_visible_ && keyboard_ && page_) {
        suppress_keyboard_ = true;
        hide_keyboard();
        lv_obj_update_layout(lv_obj_get_parent(page_));
    }
    if (suppress_keyboard_) lv_async_call(clear_keyboard_suppression, this);
    if (keyboard_ && composer_ && !lv_obj_has_flag(keyboard_, LV_OBJ_FLAG_HIDDEN))
        lv_async_call(reveal_editor, this);
}

void ChatView::prepare_layout()
{
    const auto width = lv_display_get_horizontal_resolution(nullptr);
    const auto height = lv_display_get_vertical_resolution(nullptr);
    if (layout_width_ != 0 && (layout_width_ != width || layout_height_ != height)) suppress_keyboard_ = true;
    layout_width_ = width;
    layout_height_ = height;
    if (messages_ && lv_obj_is_valid(messages_)) messages_scroll_y_ = lv_obj_get_scroll_y(messages_);
}

void ChatView::reveal_editor(void *context)
{
    auto *view = static_cast<ChatView *>(context);
    if (view->keyboard_ && view->composer_ && !lv_obj_has_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN))
        lv_obj_scroll_to_view_recursive(lv_obj_get_parent(view->composer_), LV_ANIM_OFF);
}

void ChatView::restore_messages_scroll(void *context)
{
    auto *view = static_cast<ChatView *>(context);
    if (view->messages_ && lv_obj_is_valid(view->messages_))
        lv_obj_scroll_to_y(view->messages_, view->messages_scroll_y_, LV_ANIM_OFF);
}

void ChatView::clear_keyboard_suppression(void *context)
{
    static_cast<ChatView *>(context)->suppress_keyboard_ = false;
}

void ChatView::destroy() noexcept
{
    lv_async_call_cancel(process_message_action, this);
    pending_retry_ = 0;
    pending_load_older_ = false;
    if (composer_ && lv_obj_is_valid(composer_)) view_model_.set_draft(lv_textarea_get_text(composer_));
    lv_async_call_cancel(reveal_editor, this);
    lv_async_call_cancel(restore_messages_scroll, this);
    lv_async_call_cancel(clear_keyboard_suppression, this);
    if (keyboard_ && lv_obj_is_valid(keyboard_)) {
        lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_obj_delete(keyboard_);
    }
    keyboard_ = nullptr;
    keyboard_visible_ = false;
    suppress_keyboard_ = false;
    layout_width_ = 0;
    layout_height_ = 0;
    destroy_page_body(page_);
    messages_ = nullptr;
    composer_ = nullptr;
    messages_scroll_y_ = 0;
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
    view->hide_keyboard();
}

void ChatView::composer_focus_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (!view) return;
    auto *active_input = lv_indev_active();
    switch (lv_event_get_code(event)) {
        case LV_EVENT_CLICKED:
            // Ignore synthetic click notifications emitted while LVGL is
            // relaying focus during a resize. A real pointer click has an
            // active indev; tests and keyboard navigation can mark PRESSED
            // explicitly when they intentionally synthesize the action.
            if (view->suppress_keyboard_ || lilygo_ui_meshtastic_layout_in_progress() ||
                (!active_input && !lv_obj_has_state(view->composer_, LV_STATE_PRESSED)))
                break;
            view->show_keyboard();
            break;
        case LV_EVENT_DEFOCUSED:
            view->hide_keyboard();
            break;
        case LV_EVENT_DELETE:
            if (view->keyboard_) lv_keyboard_set_textarea(view->keyboard_, nullptr);
            view->composer_ = nullptr;
            break;
        default:
            break;
    }
}

void ChatView::keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (!view) return;
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        view->keyboard_ = nullptr;
        return;
    }
    // Touch OK sends READY to the textarea too; AppKit's physical Enter
    // notifies only the keyboard. Forward only the latter to submit once.
    auto *input = lv_indev_active();
    if (lv_event_get_code(event) == LV_EVENT_READY && input &&
        lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD)
        send_event(event);
    view->hide_keyboard();
}

void ChatView::show_keyboard()
{
    if (suppress_keyboard_ || !keyboard_ || !composer_ || cm0_input_hardware_keyboard_present()) return;
    keyboard_visible_ = true;
    lv_obj_clear_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(keyboard_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_update_layout(lv_obj_get_parent(page_));
    lv_obj_scroll_to_view_recursive(lv_obj_get_parent(composer_), LV_ANIM_OFF);
}

void ChatView::hide_keyboard()
{
    keyboard_visible_ = false;
    if (keyboard_ && lv_obj_is_valid(keyboard_)) {
        lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
}

void ChatView::draft_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (view && view->composer_) view->view_model_.set_draft(lv_textarea_get_text(view->composer_));
}

void ChatView::load_older_event(lv_event_t *event)
{
    auto *view = static_cast<ChatView *>(lv_event_get_user_data(event));
    if (!view) return;
    view->pending_load_older_ = true;
    lv_async_call_cancel(process_message_action, view);
    lv_async_call(process_message_action, view);
}

void ChatView::retry_event(lv_event_t *event)
{
    const auto *retry = static_cast<RetryContext *>(lv_event_get_user_data(event));
    if (!retry || !retry->view) return;
    auto *view = retry->view;
    view->pending_retry_ = retry->id;
    lv_async_call_cancel(process_message_action, view);
    lv_async_call(process_message_action, view);
}

void ChatView::process_message_action(void *context)
{
    auto *view = static_cast<ChatView *>(context);
    if (!view->messages_) return;
    if (view->pending_load_older_) {
        view->pending_load_older_ = false;
        const auto y = lv_obj_get_scroll_y(view->messages_);
        const auto old_max = y + lv_obj_get_scroll_bottom(view->messages_);
        view->view_model_.load_older();
        lv_obj_update_layout(view->messages_);
        const auto new_max = lv_obj_get_scroll_y(view->messages_) + lv_obj_get_scroll_bottom(view->messages_);
        lv_obj_scroll_to_y(view->messages_, y + std::max(0, new_max - old_max), LV_ANIM_OFF);
    }
    if (view->pending_retry_) {
        const auto id = view->pending_retry_;
        view->pending_retry_ = 0;
        view->view_model_.retry_message(id);
    }
}

void ChatView::render_messages()
{
    if (!page_ || !messages_) return;
    const auto scroll_y = lv_obj_get_scroll_y(messages_);
    const bool at_end = lv_obj_get_scroll_bottom(messages_) <= 4;
    const bool had_messages = lv_obj_get_child_count(messages_) > 0;
    lv_obj_clean(messages_);
    retry_contexts_.clear();
    retry_contexts_.reserve(view_model_.messages().size());
    if (view_model_.has_older()) {
        auto *older = make_text_button(messages_, "Load older messages", COLOR_BLUE, COLOR_SURFACE,
                                        load_older_event, this);
        lv_obj_set_name_static(older, "chat_load_older");
        lv_obj_set_width(older, LV_PCT(100));
    }
    if (view_model_.messages().empty()) {
        make_wrapped_label(messages_, "No messages in this conversation", 14, COLOR_SECONDARY);
    }
    for (const auto &message : view_model_.messages()) {
        auto *row = make_flex(messages_, LV_FLEX_FLOW_ROW);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(row, message.outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                               LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        auto *stack = make_flex(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(stack, LV_PCT(86));
        lv_obj_set_height(stack, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(stack, 4, 0);
        if (!message.outgoing) make_wrapped_label(stack, message.sender.c_str(), 14, COLOR_GREEN_DARK);
        auto *bubble = make_card(stack);
        lv_obj_set_name_static(bubble, "chat_message");
        if (message.outgoing) lv_obj_set_style_bg_color(bubble, lv_color_hex(COLOR_BLUE), 0);
        make_wrapped_label(bubble, message.text.c_str(), 14, message.outgoing ? COLOR_SURFACE : COLOR_TEXT);
        if (!message.detail.empty()) make_wrapped_label(stack, message.detail.c_str(), 14, COLOR_SECONDARY);
        if (message.retryable) {
            retry_contexts_.push_back({this, message.local_id});
            auto *retry = make_text_button(stack, "Retry message", COLOR_BLUE, COLOR_SURFACE,
                                            retry_event, &retry_contexts_.back());
            lv_obj_set_name_static(retry, "chat_retry");
            lv_obj_set_width(retry, LV_PCT(100));
            lv_obj_bind_state_if_eq(retry, view_model_.connected_subject(), LV_STATE_DISABLED, 0);
        }
    }
    lv_obj_update_layout(messages_);
    if ((at_end || !had_messages) && !pending_load_older_) lv_obj_scroll_to_y(messages_, LV_COORD_MAX, LV_ANIM_OFF);
    else lv_obj_scroll_to_y(messages_, scroll_y, LV_ANIM_OFF);
}
