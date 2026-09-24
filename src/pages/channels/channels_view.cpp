#include "pages/channels/channels_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

ChannelsView::ChannelsView(ChannelsViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

ChannelsView::~ChannelsView()
{
    destroy();
}

lv_obj_t *ChannelsView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Channels", "+", nullptr, nullptr, false);

    lv_obj_t *segments = make_flex(page_, LV_FLEX_FLOW_ROW);
    style_container(segments, COLOR_NEUTRAL_FILL, 9);
    lv_obj_set_size(segments, LV_PCT(100), 38);
    lv_obj_set_style_pad_all(segments, 3, 0);
    lv_obj_t *active = make_flex(segments, LV_FLEX_FLOW_ROW);
    style_container(active, COLOR_SURFACE, 7);
    lv_obj_set_height(active, LV_PCT(100));
    lv_obj_set_flex_grow(active, 1);
    lv_obj_t *active_label = make_label(active, "My Channels", 14, COLOR_TITLE);
    lv_obj_center(active_label);
    lv_obj_t *join = make_flex(segments, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(join, LV_PCT(100));
    lv_obj_set_flex_grow(join, 1);
    lv_obj_t *join_label = make_label(join, "Join / Import", 14, COLOR_SECONDARY);
    lv_obj_center(join_label);

    content_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(content_, "channels_content");
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(content_, 12, 0);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, content_, this);
    return page_;
}

void ChannelsView::destroy() noexcept
{
    destroy_page_body(page_);
    content_ = nullptr;
    row_contexts_.clear();
}

void ChannelsView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<ChannelsView *>(lv_observer_get_user_data(observer));
    if (view) view->render_channels();
}

void ChannelsView::back_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void ChannelsView::channel_event(lv_event_t *event)
{
    auto *context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    context->view->view_model_.select_channel(context->channel);
    context->view->navigation_.request(AppPage::chat);
}

void ChannelsView::render_channels()
{
    if (!page_ || !content_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(content_);
    row_contexts_.clear();
    row_contexts_.reserve(view_model_.rows().size());

    make_section_label(content_, "PRIMARY");
    lv_obj_t *primary = make_card(content_);
    lv_obj_set_name_static(primary, "channels_primary");
    bool has_primary = false;
    for (const auto &channel : view_model_.rows()) {
        if (channel.role != 1) continue;
        has_primary = true;
        row_contexts_.push_back({this, channel.index});
        make_info_row(primary, COLOR_BLUE, channel.name.c_str(), channel.detail.c_str(), channel_event,
                      &row_contexts_.back());
    }
    if (!has_primary) make_label(primary, "Waiting for primary channel", 14, COLOR_SECONDARY);

    make_section_label(content_, "SECONDARY CHANNELS");
    lv_obj_t *secondary = make_card(content_);
    lv_obj_set_name_static(secondary, "channels_secondary");
    bool has_secondary = false;
    for (const auto &channel : view_model_.rows()) {
        if (channel.role != 2) continue;
        has_secondary = true;
        make_info_row(secondary, COLOR_ORANGE, channel.name.c_str(), channel.detail.c_str(), nullptr, nullptr);
    }
    if (!has_secondary) make_label(secondary, "No secondary channels", 14, COLOR_SECONDARY);
    make_wrapped_label(content_, "Channel data is synchronized from the radio.", 14, COLOR_SECONDARY);
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
