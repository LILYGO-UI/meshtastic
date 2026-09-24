#include "components/components.hpp"

#include <cm0/typography.h>

void style_container(lv_obj_t *object, std::uint32_t color, int radius)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, int font_size, std::uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    const lv_font_t *font = lilygo_ui_font_get(font_size);
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(label, 3, 0);
    return label;
}

lv_obj_t *make_wrapped_label(lv_obj_t *parent, const char *text, int font_size, std::uint32_t color)
{
    lv_obj_t *label = make_label(parent, text, font_size, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t *make_flex(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *container = lv_obj_create(parent);
    style_container(container, COLOR_PAGE, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, flow);
    return container;
}

lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_container(card, COLOR_SURFACE, 14);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

lv_obj_t *make_section_label(lv_obj_t *parent, const char *text)
{
    return make_label(parent, text, 14, COLOR_SECONDARY);
}

lv_obj_t *make_search_field(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *search = lv_obj_create(parent);
    style_container(search, COLOR_NEUTRAL_FILL, 11);
    lv_obj_set_width(search, LV_PCT(100));
    lv_obj_set_height(search, 42);
    lv_obj_set_style_pad_hor(search, 14, 0);
    lv_obj_set_style_pad_ver(search, 0, 0);
    lv_obj_clear_flag(search, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = make_label(search, placeholder, 14, COLOR_MUTED_ICON);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return search;
}

lv_obj_t *make_dot(lv_obj_t *parent, std::uint32_t color, int size)
{
    lv_obj_t *dot = lv_obj_create(parent);
    style_container(dot, color, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, size, size);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

lv_obj_t *make_text_button(lv_obj_t *parent, const char *text, std::uint32_t text_color, std::uint32_t background,
                           lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    style_container(button, background, 24);
    lv_obj_set_height(button, 46);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(button, 44, 0);
    lv_obj_set_style_pad_hor(button, 16, 0);
    if (background == COLOR_PAGE) lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_t *label = make_label(button, text, 22, text_color);
    lv_obj_center(label);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    return button;
}

lv_obj_t *make_header(lv_obj_t *parent, const char *title, const char *action, lv_event_cb_t action_cb,
                      void *action_data, bool show_back, lv_event_cb_t back_cb)
{
    lv_obj_t *action_label = nullptr;
    lv_obj_t *bar          = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 52);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (show_back) {
        lv_obj_t *back = make_text_button(bar, LV_SYMBOL_LEFT, COLOR_BLUE, COLOR_PAGE, back_cb, nullptr);
        lv_obj_set_width(back, 52);
    } else {
        lv_obj_t *spacer = lv_obj_create(bar);
        style_container(spacer, COLOR_PAGE, 0);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_size(spacer, 1, 1);
    }
    lv_obj_t *heading = make_label(bar, title, show_back ? 22 : 28, COLOR_TITLE);
    lv_obj_set_width(heading, 0);
    lv_obj_set_flex_grow(heading, 1);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(heading, show_back ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    if (action) {
        lv_obj_t *action_button = make_text_button(bar, action, COLOR_BLUE, COLOR_PAGE, action_cb, action_data);
        lv_obj_set_style_pad_hor(action_button, 8, 0);
        action_label          = lv_obj_get_child(action_button, 0);
        const lv_font_t *font = lilygo_ui_font_get(14);
        if (font) lv_obj_set_style_text_font(action_label, font, 0);
    } else if (show_back) {
        lv_obj_t *spacer = lv_obj_create(bar);
        style_container(spacer, COLOR_PAGE, 0);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_size(spacer, 52, 1);
    }
    return action_label;
}

lv_obj_t *make_info_row(lv_obj_t *parent, std::uint32_t dot_color, const char *title, const char *detail,
                        lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *row = lv_button_create(parent);
    style_container(row, COLOR_SURFACE, 0);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 82, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (dot_color) make_dot(row, dot_color, 46);
    lv_obj_t *text = make_flex(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(text, 0);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(text, 2, 0);
    make_wrapped_label(text, title, 14, COLOR_TEXT);
    make_wrapped_label(text, detail, 14, COLOR_SECONDARY);
    if (callback) {
        make_label(row, LV_SYMBOL_RIGHT, 22, COLOR_SEPARATOR);
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, user_data);
    }
    return row;
}

lv_obj_t *make_key_value(lv_obj_t *parent, const char *key, const char *value, std::uint32_t value_color)
{
    lv_obj_t *row = make_flex(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 36, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *key_label = make_label(row, key, 14, COLOR_TEXT);
    lv_obj_set_width(key_label, LV_PCT(38));
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_WRAP);
    lv_obj_t *value_label = make_label(row, value, 14, value_color);
    lv_obj_set_width(value_label, LV_PCT(58));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    return value_label;
}
