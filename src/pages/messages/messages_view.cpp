#include "pages/messages/messages_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <string>
#include <unordered_set>

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

    lv_obj_t *compose_label = make_header(page_, "Messages", "+", compose_event, this, false);
    if (compose_label) {
        lv_obj_t *compose = lv_obj_get_parent(compose_label);
        lv_obj_set_name_static(compose, "messages_compose");

        // The compose action is the primary action on this page. Give it a
        // filled, circular treatment so the affordance remains easy to find
        // at a glance on both narrow and wide layouts.
        lv_obj_set_size(compose, 48, 48);
        lv_obj_set_style_min_width(compose, 48, 0);
        lv_obj_set_style_pad_all(compose, 0, 0);
        // make_header() gives action buttons horizontal padding so text
        // actions have a comfortable hit target.  The compose action is a
        // fixed circular button, however, and that inherited padding shrinks
        // the LVGL content box below the 28 px plus glyph.  Clear the
        // directional padding explicitly so the centered label stays inside
        // the button on portrait displays.
        lv_obj_set_style_pad_hor(compose, 0, 0);
        lv_obj_set_style_pad_ver(compose, 0, 0);
        lv_obj_set_style_bg_color(compose, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_set_style_bg_opa(compose, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(compose, LV_RADIUS_CIRCLE, 0);

        lv_obj_set_width(compose_label, LV_PCT(100));
        lv_obj_set_height(compose_label, LV_PCT(100));
        lv_obj_set_style_text_align(compose_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(compose_label, lv_color_hex(COLOR_SURFACE), 0);
        if (const auto *font = lilygo_ui_font_get(28)) lv_obj_set_style_text_font(compose_label, font, 0);
    }

    search_.create(page_, "messages_search", "Search conversations", view_model_.query(),
                   [this](std::string query) { view_model_.set_query(std::move(query)); });
    make_section_label(page_, "CONVERSATIONS");
    list_ = make_card(page_);
    lv_obj_set_name_static(list_, "messages_list");

    lv_obj_t *channels = make_text_button(page_, "Channels", COLOR_BLUE, COLOR_PAGE, channels_event, this);
    lv_obj_set_name_static(channels, "messages_channels");
    lv_obj_set_width(channels, LV_PCT(100));

    footer_ = make_label(page_, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(footer_, view_model_.footer_subject(), nullptr);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, list_, this);
    render_rows();
    return page_;
}

void MessagesView::destroy() noexcept
{
    search_.destroy();
    destroy_page_body(page_);
    list_   = nullptr;
    footer_ = nullptr;
    conversation_contexts_.clear();
}

void MessagesView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<MessagesView *>(lv_observer_get_user_data(observer));
    if (view) view->render_rows();
}

void MessagesView::compose_event(lv_event_t *event)
{
    auto *view = static_cast<MessagesView *>(lv_event_get_user_data(event));
    if (!view) return;
    view->view_model_.clear_selection();
    view->navigation_.request(AppPage::chat);
}

void MessagesView::channels_event(lv_event_t *event)
{
    static_cast<MessagesView *>(lv_event_get_user_data(event))->navigation_.request(AppPage::channels);
}

void MessagesView::conversation_event(lv_event_t *event)
{
    auto *context = static_cast<ConversationContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    context->view->view_model_.select_conversation(context->direct, context->peer, context->channel, context->name);
    context->view->navigation_.request(AppPage::chat);
}

void MessagesView::render_rows()
{
    if (!page_ || !list_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(list_);
    conversation_contexts_.clear();
    if (view_model_.rows().empty()) {
        make_wrapped_label(
            list_, view_model_.connected() ? "No matching conversations." : "Connect to receive mesh messages.", 14,
            COLOR_SECONDARY);
    }
    std::unordered_set<std::string> seen_conversations;
    conversation_contexts_.reserve(view_model_.rows().size());
    for (const auto &row : view_model_.rows()) {
        const auto key = std::string(row.direct ? "dm:" : "channel:") +
                         std::to_string(row.direct ? row.peer : 0) + ":" + std::to_string(row.channel);
        if (!seen_conversations.insert(key).second) continue;
        std::string preview = row.sender + ": " + row.text;
        if (!row.detail.empty()) preview = row.detail + "\n" + preview;
        const auto title = row.conversation + (row.unread_count ? "  (" + std::to_string(row.unread_count) + " unread)" : "");
        conversation_contexts_.push_back({this, row.direct, row.peer, row.channel, row.conversation});
        lv_obj_t *message = make_info_row(list_, row.direct ? COLOR_PURPLE : (row.outgoing ? COLOR_BLUE : COLOR_PALE_BLUE),
                                          title.c_str(), preview.c_str(), conversation_event,
                                          &conversation_contexts_.back());
        lv_obj_set_name_static(message, "message_conversation");
    }
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
