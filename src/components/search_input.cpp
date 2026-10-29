#include "components/search_input.hpp"
#include "components/components.hpp"
#include <cm0/input.h>
#include <cm0/typography.h>
#include <utility>

lv_obj_t *SearchInput::create(lv_obj_t *parent, const char *name, const char *placeholder,
                             const std::string &value, Changed changed)
{
    destroy();
    input_ = lv_textarea_create(parent);
    lv_obj_set_name(input_, name);
    lv_obj_set_width(input_, LV_PCT(100));
    lv_obj_set_height(input_, 48);
    style_container(input_, COLOR_NEUTRAL_FILL, 12);
    lv_textarea_set_one_line(input_, true);
    lv_textarea_set_max_length(input_, 256);
    lv_textarea_set_placeholder_text(input_, placeholder);
    lv_textarea_set_text(input_, value.c_str());
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(input_, font, 0);
    changed_ = std::move(changed);
    keyboard_ = lv_keyboard_create(lv_obj_get_parent(parent));
    lv_obj_set_name_static(keyboard_, "search_keyboard");
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_height(keyboard_, LV_PCT(35));
    lv_obj_set_style_min_height(keyboard_, 120, 0);
    lv_obj_set_style_max_height(keyboard_, 280, 0);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(keyboard_, font, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard_, input_);
    lv_obj_clear_state(input_, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(input_, input_event, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_CANCEL, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_DELETE, this);
    return input_;
}

void SearchInput::destroy() noexcept
{
    changed_ = {};
    if (keyboard_ && lv_obj_is_valid(keyboard_)) {
        lv_keyboard_set_textarea(keyboard_, nullptr);
        lv_obj_delete(keyboard_);
    }
    keyboard_ = nullptr;
    if (input_ && lv_obj_is_valid(input_)) lv_obj_delete(input_);
    input_ = nullptr;
}

void SearchInput::input_event(lv_event_t *event)
{
    auto *self = static_cast<SearchInput *>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
        case LV_EVENT_VALUE_CHANGED:
            if (self->changed_) self->changed_(lv_textarea_get_text(self->input_));
            break;
        case LV_EVENT_FOCUSED:
        case LV_EVENT_CLICKED:
            if (self->keyboard_ && !cm0_input_hardware_keyboard_present()) {
                lv_keyboard_set_textarea(self->keyboard_, self->input_);
                lv_obj_clear_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(self->keyboard_);
            }
            break;
        case LV_EVENT_READY:
            if (self->keyboard_) lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
            break;
        case LV_EVENT_CANCEL:
        case LV_EVENT_DEFOCUSED:
            if (self->keyboard_) {
                lv_keyboard_set_textarea(self->keyboard_, nullptr);
                lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case LV_EVENT_KEY:
            // Physical keyboards and keypad navigation do not need the
            // on-screen keyboard. Keep the editor focused for continued typing.
            if (self->keyboard_) {
                lv_keyboard_set_textarea(self->keyboard_, nullptr);
                lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
            }
            if (auto *group = lv_obj_get_group(self->input_);
                group && lv_group_get_focused(group) == self->input_)
                lv_obj_add_state(self->input_, LV_STATE_FOCUSED);
            break;
        case LV_EVENT_DELETE:
            if (self->keyboard_ && lv_obj_is_valid(self->keyboard_)) {
                lv_keyboard_set_textarea(self->keyboard_, nullptr);
                lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
            }
            self->input_ = nullptr;
            break;
        default: break;
    }
}

void SearchInput::keyboard_event(lv_event_t *event)
{
    auto *self = static_cast<SearchInput *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        self->keyboard_ = nullptr;
        return;
    }
    if (!self->keyboard_) return;
    lv_keyboard_set_textarea(self->keyboard_, nullptr);
    lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
}
