#ifndef LILYGO_UI_MESHTASTIC_COMPONENTS_HPP
#define LILYGO_UI_MESHTASTIC_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

inline constexpr std::uint32_t COLOR_PAGE           = 0xf2f2f7;
inline constexpr std::uint32_t COLOR_SURFACE        = 0xffffff;
inline constexpr std::uint32_t COLOR_TITLE          = 0x1a1a1a;
inline constexpr std::uint32_t COLOR_TEXT           = 0x000000;
inline constexpr std::uint32_t COLOR_SEPARATOR      = 0xd8dde3;
inline constexpr std::uint32_t COLOR_PRIMARY_ACTION = 0x20262d;
inline constexpr std::uint32_t COLOR_SECONDARY      = 0x6e6e73;
inline constexpr std::uint32_t COLOR_BLUE           = 0x007aff;
inline constexpr std::uint32_t COLOR_GREEN          = 0x34c759;
inline constexpr std::uint32_t COLOR_GREEN_DARK     = 0x248a3d;
inline constexpr std::uint32_t COLOR_GREEN_LIGHT    = 0xe8f7ec;
inline constexpr std::uint32_t COLOR_RED            = 0xff3b30;
inline constexpr std::uint32_t COLOR_ORANGE         = 0xff9500;
inline constexpr std::uint32_t COLOR_PURPLE         = 0xaf52de;
inline constexpr std::uint32_t COLOR_PALE_BLUE      = 0xd9ecff;
inline constexpr std::uint32_t COLOR_NEUTRAL_FILL   = 0xe5e5ea;
inline constexpr std::uint32_t COLOR_MUTED_ICON     = 0x8e8e93;
inline constexpr std::uint32_t COLOR_AVATAR         = 0xdff5e6;
inline constexpr std::uint32_t COLOR_MAP_BACKGROUND = 0xe7f0e9;
inline constexpr std::uint32_t COLOR_MAP_CONTOUR    = 0xbed9c5;
inline constexpr std::uint32_t COLOR_MAP_WATER      = 0x7ec4f2;
inline constexpr std::uint32_t COLOR_NOTICE         = 0xfff4e5;
inline constexpr std::uint32_t COLOR_NOTICE_TEXT    = 0x8a4b00;

void style_container(lv_obj_t *object, std::uint32_t color, int radius);
lv_obj_t *make_label(lv_obj_t *parent, const char *text, int font_size, std::uint32_t color);
lv_obj_t *make_wrapped_label(lv_obj_t *parent, const char *text, int font_size, std::uint32_t color);
lv_obj_t *make_flex(lv_obj_t *parent, lv_flex_flow_t flow);
lv_obj_t *make_card(lv_obj_t *parent);
lv_obj_t *make_section_label(lv_obj_t *parent, const char *text);
lv_obj_t *make_search_field(lv_obj_t *parent, const char *placeholder);
lv_obj_t *make_dot(lv_obj_t *parent, std::uint32_t color, int size);
lv_obj_t *make_text_button(lv_obj_t *parent, const char *text, std::uint32_t text_color, std::uint32_t background,
                           lv_event_cb_t callback, void *user_data);
lv_obj_t *make_header(lv_obj_t *parent, const char *title, const char *action, lv_event_cb_t action_cb,
                      void *action_data, bool show_back, lv_event_cb_t back_cb = nullptr);
lv_obj_t *make_info_row(lv_obj_t *parent, std::uint32_t dot_color, const char *title, const char *detail,
                        lv_event_cb_t callback, void *user_data);
lv_obj_t *make_key_value(lv_obj_t *parent, const char *key, const char *value, std::uint32_t value_color);

#endif
