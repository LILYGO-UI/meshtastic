#ifndef LILYGO_UI_MESHTASTIC_PAGES_PAGE_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_PAGES_PAGE_VIEW_HPP

#include "components/components.hpp"

#include <lvgl.h>

class PageView {
public:
    virtual ~PageView() = default;

    virtual lv_obj_t *create(lv_obj_t *parent) = 0;
    virtual void prepare_layout() {}
    virtual void update_layout() {}
    virtual void destroy() noexcept            = 0;
};

inline lv_obj_t *create_page_body(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_name_static(page, "page_body");
    style_container(page, COLOR_PAGE, 0);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_set_style_pad_row(page, 12, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    return page;
}

inline void destroy_page_body(lv_obj_t *&page) noexcept
{
    if (page && lv_obj_is_valid(page)) lv_obj_delete(page);
    page = nullptr;
}

#endif
