#include "pages/channels/channels_view.hpp"
#include "components/components.hpp"
#include <cm0/input.h>
#include <cm0/typography.h>

namespace {

lv_obj_t *channel_button(lv_obj_t *parent, const char *text, std::uint32_t foreground,
                 std::uint32_t background, lv_event_cb_t callback, void *context)
{
    auto *button = make_text_button(parent, text, foreground, background, callback, context);
    lv_obj_set_height(button, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(button, 46, 0);
    lv_obj_set_style_pad_ver(button, 10, 0);
    auto *label = lv_obj_get_child(button, 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(label, font, 0);
    return button;
}
lv_obj_t *dropdown(lv_obj_t *parent, const char *label, const char *options, const char *name)
{
    make_wrapped_label(parent, label, 14, COLOR_TEXT);
    auto *control = lv_dropdown_create(parent);
    lv_obj_set_name_static(control, name);
    lv_obj_set_width(control, LV_PCT(100));
    lv_obj_set_height(control, 48);
    lv_dropdown_set_options(control, options);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(control, font, 0);
    return control;
}
lv_obj_t *input(lv_obj_t *parent, const char *label, const char *name, std::uint32_t limit, bool multiline = false)
{
    make_wrapped_label(parent, label, 14, COLOR_TEXT);
    auto *control = lv_textarea_create(parent);
    lv_obj_set_name_static(control, name);
    lv_obj_set_width(control, LV_PCT(100));
    lv_obj_set_height(control, multiline ? 110 : 48);
    lv_textarea_set_one_line(control, !multiline);
    lv_textarea_set_max_length(control, limit);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(control, font, 0);
    return control;
}
lv_obj_t *toggle(lv_obj_t *parent, const char *label, const char *name)
{
    auto *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 10, 0);
    auto *text = make_label(row, label, 14, COLOR_TEXT);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    auto *control = lv_switch_create(row);
    lv_obj_set_name_static(control, name);
    lv_obj_set_size(control, 64, 44);
    return control;
}
}  // namespace
ChannelsView::ChannelsView(ChannelsViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation) {}
ChannelsView::~ChannelsView() { destroy(); }
lv_obj_t *ChannelsView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_set_height(page_, 0);
    lv_obj_set_flex_grow(page_, 1);
    make_header(page_, "Channels", "+", add_event, this, false);
    content_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(content_, "channels_content");
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(content_, 12, 0);
    render_channels();
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, content_, this);
    lv_subject_add_observer_obj(view_model_.radio_identity_subject(), radio_identity_observer, content_, this);

    make_section_label(page_, "EDIT CHANNEL");
    auto *edit_card = make_card(page_);
    slot_ = dropdown(edit_card, "Radio channel slot", "0\n1\n2\n3\n4\n5\n6\n7", "channels_slot_dropdown");
    auto *open = channel_button(edit_card, "Edit selected slot", COLOR_BLUE, COLOR_SURFACE, edit_event, this);
    lv_obj_set_width(open, LV_PCT(100));
    auto *refresh = channel_button(edit_card, "Refresh channel slots", COLOR_BLUE, COLOR_SURFACE, refresh_event, this);
    lv_obj_set_width(refresh, LV_PCT(100));
    auto *status = make_wrapped_label(edit_card, "", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(status, "channels_status");
    lv_label_bind_text(status, view_model_.status_subject(), nullptr);
    editor_ = make_flex(edit_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(editor_, "channels_editor");
    lv_obj_set_width(editor_, LV_PCT(100));
    lv_obj_set_height(editor_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(editor_, 10, 0);
    render_editor();
    lv_subject_add_observer_obj(view_model_.editor_content_subject(), editor_observer, editor_, this);

    make_section_label(page_, "JOIN / IMPORT");
    auto *import_card = make_card(page_);
    import_link_ = input(import_card, "Meshtastic channel link", "channels_import_link", 8192, true);
    lv_obj_add_event_cb(import_link_, input_event, LV_EVENT_ALL, this);
    auto *preview = channel_button(import_card, "Preview channel link", COLOR_BLUE, COLOR_SURFACE, preview_import_event, this);
    lv_obj_set_width(preview, LV_PCT(100));
    import_summary_ = make_wrapped_label(import_card, "Preview a link before applying it to the radio.", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(import_summary_, "channels_import_summary");
    include_lora_ = toggle(import_card, "Also apply radio settings from the link", "channels_import_lora");
    auto *apply = channel_button(import_card, "Apply imported channels", COLOR_SURFACE, COLOR_BLUE, apply_import_event, this);
    lv_obj_set_name_static(apply, "channels_apply_import");
    lv_obj_set_width(apply, LV_PCT(100));

    make_section_label(page_, "SHARE");
    auto *share_card = make_card(page_);
    auto *share = channel_button(share_card, "Create channel share link", COLOR_BLUE, COLOR_SURFACE, share_event, this);
    lv_obj_set_width(share, LV_PCT(100));
    shared_link_ = input(share_card, "Share link includes channel keys", "channels_share_link", 8192, true);
    lv_obj_add_flag(shared_link_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(shared_link_, input_event, LV_EVENT_ALL, this);

    keyboard_ = lv_keyboard_create(parent);
    lv_obj_set_name_static(keyboard_, "channels_software_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_height(keyboard_, LV_PCT(42));
    lv_obj_set_style_min_height(keyboard_, 180, 0);
    lv_obj_set_style_max_height(keyboard_, 300, 0);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(keyboard_, font, 0);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_CANCEL, this);
    return page_;
}
void ChannelsView::update_layout()
{
    lv_async_call_cancel(reveal_async, this);
    if (keyboard_ && active_input_ && !lv_obj_has_flag(keyboard_, LV_OBJ_FLAG_HIDDEN)) lv_async_call(reveal_async, this);
}
void ChannelsView::destroy() noexcept
{
    lv_async_call_cancel(render_async, this);
    lv_async_call_cancel(editor_async, this);
    lv_async_call_cancel(radio_identity_async, this);
    lv_async_call_cancel(reveal_async, this);
    if (keyboard_ && lv_obj_is_valid(keyboard_)) { lv_keyboard_set_textarea(keyboard_, nullptr); lv_obj_delete(keyboard_); }
    keyboard_ = nullptr;
    active_input_ = nullptr;
    if (confirmation_ && lv_obj_is_valid(confirmation_)) lv_obj_delete(confirmation_);
    confirmation_ = nullptr;
    destroy_page_body(page_);
    content_ = editor_ = slot_ = name_ = role_ = key_mode_ = key_ = uplink_ = downlink_ = nullptr;
    import_link_ = import_summary_ = include_lora_ = shared_link_ = nullptr;
    row_contexts_.clear();
}
void ChannelsView::content_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<ChannelsView *>(lv_observer_get_user_data(observer));
    if (view) { lv_async_call_cancel(render_async, view); lv_async_call(render_async, view); }
}
void ChannelsView::editor_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<ChannelsView *>(lv_observer_get_user_data(observer));
    if (view) { lv_async_call_cancel(editor_async, view); lv_async_call(editor_async, view); }
}
void ChannelsView::radio_identity_observer(lv_observer_t *observer, lv_subject_t *)
{
    auto *view = static_cast<ChannelsView *>(lv_observer_get_user_data(observer));
    if (!view || !view->page_) return;
    lv_async_call_cancel(radio_identity_async, view);
    lv_async_call(radio_identity_async, view);
}
void ChannelsView::radio_identity_async(void *context)
{
    auto *view = static_cast<ChannelsView *>(context);
    if (!view || !view->page_) return;
    view->hide_keyboard();
    view->input_before_edit_.clear();
    if (view->confirmation_ && lv_obj_is_valid(view->confirmation_)) lv_obj_delete(view->confirmation_);
    view->confirmation_ = nullptr;
    if (view->import_link_) lv_textarea_set_text(view->import_link_, "");
    if (view->shared_link_) {
        lv_textarea_set_text(view->shared_link_, "");
        lv_obj_add_flag(view->shared_link_, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->import_summary_) lv_label_set_text(view->import_summary_, "Preview a link before applying it to the radio.");
    if (view->include_lora_) lv_obj_clear_state(view->include_lora_, LV_STATE_CHECKED);
}
void ChannelsView::render_async(void *context) { static_cast<ChannelsView *>(context)->render_channels(); }
void ChannelsView::editor_async(void *context) { static_cast<ChannelsView *>(context)->render_editor(); }
void ChannelsView::reveal_async(void *context)
{
    auto *view = static_cast<ChannelsView *>(context);
    if (view->active_input_ && view->keyboard_ && !lv_obj_has_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN)) lv_obj_scroll_to_view_recursive(view->active_input_, LV_ANIM_OFF);
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
void ChannelsView::add_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->slot_) return;
    view->hide_keyboard();
    view->view_model_.begin_new_channel();
}
void ChannelsView::edit_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->slot_) return;
    view->hide_keyboard();
    view->view_model_.edit_channel(lv_dropdown_get_selected(view->slot_));
}
void ChannelsView::refresh_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (view) for (int index = 0; index < 8; ++index) view->view_model_.refresh_channel(index);
}
void ChannelsView::save_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->name_ || !view->role_ || !view->key_) return;
    view->hide_keyboard();
    if (view->view_model_.stage_channel(lv_textarea_get_text(view->name_), lv_dropdown_get_selected(view->role_),
        static_cast<ChannelsViewModel::KeyMode>(lv_dropdown_get_selected(view->key_mode_)), lv_textarea_get_text(view->key_),
        lv_obj_has_state(view->uplink_, LV_STATE_CHECKED), lv_obj_has_state(view->downlink_, LV_STATE_CHECKED)))
        view->show_confirmation(false);
}
void ChannelsView::discard_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (view) { view->hide_keyboard(); view->view_model_.discard_channel(); }
}
void ChannelsView::preview_import_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->import_link_) return;
    view->hide_keyboard();
    const bool valid = view->view_model_.preview_import(lv_textarea_get_text(view->import_link_));
    lv_label_set_text(view->import_summary_, valid ? view->view_model_.import_summary().c_str() : "Link could not be imported. Check the status above.");
}
void ChannelsView::apply_import_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->import_link_) return;
    view->hide_keyboard();
    if (view->view_model_.preview_import(lv_textarea_get_text(view->import_link_))) {
        lv_label_set_text(view->import_summary_, view->view_model_.import_summary().c_str());
        view->show_confirmation(true);
    }
}
void ChannelsView::share_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->shared_link_) return;
    if (view->view_model_.export_channels()) {
        lv_textarea_set_text(view->shared_link_, view->view_model_.share_link().c_str());
        lv_obj_remove_flag(view->shared_link_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view_recursive(view->shared_link_, LV_ANIM_ON);
    }
}
void ChannelsView::key_mode_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->key_mode_ || !view->key_) return;
    if (lv_dropdown_get_selected(view->key_mode_) == static_cast<unsigned>(ChannelsViewModel::KeyMode::custom_base64)) lv_obj_remove_flag(view->key_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(view->key_, LV_OBJ_FLAG_HIDDEN);
}
void ChannelsView::render_editor()
{
    if (!editor_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    if (active_input_ == name_ || active_input_ == key_) hide_keyboard();
    lv_obj_clean(editor_);
    name_ = role_ = key_mode_ = key_ = uplink_ = downlink_ = nullptr;
    const auto *draft = view_model_.draft_channel();
    if (!draft) { make_wrapped_label(editor_, "Select a slot and read its current settings before editing.", 14, COLOR_SECONDARY); return; }
    lv_dropdown_set_selected(slot_, static_cast<std::uint16_t>(draft->index()));
    name_ = input(editor_, "Channel name (up to 11 bytes)", "channels_name_input", 11);
    lv_textarea_set_text(name_, draft->settings().name().c_str());
    lv_obj_add_event_cb(name_, input_event, LV_EVENT_ALL, this);
    role_ = dropdown(editor_, "Role", "Disabled\nPrimary\nSecondary", "channels_role_dropdown");
    const auto selected_role = draft->role() == meshtastic::Channel::DISABLED ? meshtastic::Channel::SECONDARY : draft->role();
    lv_dropdown_set_selected(role_, static_cast<std::uint16_t>(selected_role));
    key_mode_ = dropdown(editor_, "Encryption key", "Keep current key\nUnencrypted\nPublic default key\nCustom Base64 key", "channels_key_mode");
    make_wrapped_label(editor_, "The public default key is shared publicly. A custom key must contain 16 or 32 random bytes encoded as Base64.", 14, COLOR_SECONDARY);
    key_ = input(editor_, "Custom key", "channels_key_input", 64);
    lv_textarea_set_password_mode(key_, true);
    lv_obj_add_flag(key_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(key_, input_event, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(key_mode_, key_mode_event, LV_EVENT_VALUE_CHANGED, this);
    uplink_ = toggle(editor_, "Forward this channel to the internet", "channels_uplink");
    downlink_ = toggle(editor_, "Receive this channel from the internet", "channels_downlink");
    if (draft->settings().uplink_enabled()) lv_obj_add_state(uplink_, LV_STATE_CHECKED);
    if (draft->settings().downlink_enabled()) lv_obj_add_state(downlink_, LV_STATE_CHECKED);
    auto *save = channel_button(editor_, "Review channel changes", COLOR_SURFACE, COLOR_BLUE, save_event, this);
    lv_obj_set_name_static(save, "channels_save_channel");
    lv_obj_set_width(save, LV_PCT(100));
    auto *discard = channel_button(editor_, "Discard channel draft", COLOR_BLUE, COLOR_SURFACE, discard_event, this);
    lv_obj_set_width(discard, LV_PCT(100));
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
void ChannelsView::show_confirmation(bool import)
{
    if (confirmation_) return;
    confirm_import_ = import;
    confirm_lora_ = import && lv_obj_has_state(include_lora_, LV_STATE_CHECKED);
    confirmation_ = lv_msgbox_create(page_);
    lv_obj_set_name_static(confirmation_, "channels_confirmation");
    lv_obj_add_flag(confirmation_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(confirmation_, LV_PCT(94));
    lv_obj_set_style_max_height(confirmation_, LV_PCT(90), 0);
    lv_obj_center(confirmation_);
    lv_msgbox_add_title(confirmation_, import ? "Apply imported channels?" : "Apply channel changes?");
    std::string summary;
    if (import) {
        summary = view_model_.import_summary();
        summary += confirm_lora_ ? " Radio settings will also be applied." : " Current radio settings will be kept.";
    } else if (const auto *channel = view_model_.draft_channel()) {
        summary = "Update slot " + std::to_string(channel->index()) + " (" + (channel->settings().name().empty() ? std::string("Default channel") : channel->settings().name()) + "). Changing a channel name, key or role can disconnect mesh conversations. Promoting this slot to primary makes the current primary secondary.";
    }
    lv_msgbox_add_text(confirmation_, summary.c_str());
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(confirmation_, font, 0);
    auto *cancel = lv_msgbox_add_footer_button(confirmation_, "Cancel");
    lv_obj_set_height(cancel, 48);
    lv_obj_add_event_cb(cancel, cancel_event, LV_EVENT_CLICKED, this);
    auto *confirm = lv_msgbox_add_footer_button(confirmation_, "Confirm");
    lv_obj_set_name_static(confirm, "channels_confirm_changes");
    lv_obj_set_height(confirm, 48);
    lv_obj_add_event_cb(confirm, confirm_event, LV_EVENT_CLICKED, this);
}
void ChannelsView::confirm_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view || !view->confirmation_) return;
    if (view->confirm_import_) view->view_model_.apply_import(view->confirm_lora_);
    else view->view_model_.save_channel();
    lv_msgbox_close_async(view->confirmation_);
    view->confirmation_ = nullptr;
}
void ChannelsView::cancel_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (view && view->confirmation_) { lv_msgbox_close_async(view->confirmation_); view->confirmation_ = nullptr; }
}
void ChannelsView::input_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view) return;
    auto *target = lv_event_get_target_obj(event);
    switch (lv_event_get_code(event)) {
        case LV_EVENT_FOCUSED:
        case LV_EVENT_CLICKED:
            if (view->active_input_ != target) view->input_before_edit_ = lv_textarea_get_text(target);
            view->active_input_ = target;
            if (view->keyboard_) {
                lv_keyboard_set_textarea(view->keyboard_, target);
                if (!cm0_input_hardware_keyboard_present()) {
                    lv_obj_remove_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_update_layout(lv_obj_get_parent(view->page_));
                }
                lv_obj_scroll_to_view_recursive(target, LV_ANIM_OFF);
            }
            break;
        case LV_EVENT_READY:
        case LV_EVENT_DEFOCUSED: view->hide_keyboard(); break;
        case LV_EVENT_DELETE:
            if (view->active_input_ == target) view->hide_keyboard();
            break;
        default: break;
    }
}
void ChannelsView::keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<ChannelsView *>(lv_event_get_user_data(event));
    if (!view) return;
    if (lv_event_get_code(event) == LV_EVENT_CANCEL && view->active_input_) lv_textarea_set_text(view->active_input_, view->input_before_edit_.c_str());
    view->hide_keyboard();
}
void ChannelsView::hide_keyboard()
{
    if (keyboard_) { lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN); lv_keyboard_set_textarea(keyboard_, nullptr); }
    active_input_ = nullptr;
}
void ChannelsView::render_channels()
{
    if (!page_ || !content_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(content_);
    row_contexts_.clear();
    row_contexts_.reserve(view_model_.rows().size());
    make_section_label(content_, "PRIMARY");
    auto *primary = make_card(content_);
    lv_obj_set_name_static(primary, "channels_primary");
    bool has_primary = false;
    for (const auto &channel : view_model_.rows()) {
        if (channel.role != 1) continue;
        has_primary = true;
        row_contexts_.push_back({this, channel.index});
        make_info_row(primary, COLOR_BLUE, channel.name.c_str(), channel.detail.c_str(), channel_event, &row_contexts_.back());
    }
    if (!has_primary) make_wrapped_label(primary, "Waiting for primary channel", 14, COLOR_SECONDARY);
    make_section_label(content_, "SECONDARY CHANNELS");
    auto *secondary = make_card(content_);
    lv_obj_set_name_static(secondary, "channels_secondary");
    bool has_secondary = false;
    for (const auto &channel : view_model_.rows()) {
        if (channel.role != 2) continue;
        has_secondary = true;
        row_contexts_.push_back({this, channel.index});
        make_info_row(secondary, COLOR_ORANGE, channel.name.c_str(), channel.detail.c_str(), channel_event, &row_contexts_.back());
    }
    if (!has_secondary) make_wrapped_label(secondary, "No secondary channels", 14, COLOR_SECONDARY);
    make_wrapped_label(content_, "Tap a channel to chat. Edit slots below to change the radio's channels.", 14, COLOR_SECONDARY);
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
