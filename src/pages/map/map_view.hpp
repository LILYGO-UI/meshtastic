#ifndef LILYGO_UI_MESHTASTIC_MAP_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_MAP_VIEW_HPP

#include "pages/map/map_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

class MapView final : public PageView {
public:
    explicit MapView(MapViewModel &view_model) noexcept;
    ~MapView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    void render_markers();

    MapViewModel &view_model_;
    lv_obj_t *page_    = nullptr;
    lv_obj_t *markers_ = nullptr;
};

#endif
