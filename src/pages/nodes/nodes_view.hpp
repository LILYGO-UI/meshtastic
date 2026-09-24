#ifndef LILYGO_UI_MESHTASTIC_NODES_VIEW_HPP
#define LILYGO_UI_MESHTASTIC_NODES_VIEW_HPP

#include "app_router.hpp"
#include "pages/nodes/nodes_view_model.hpp"
#include "pages/page_view.hpp"

#include <lvgl.h>

#include <cstdint>
#include <vector>

class NodesView final : public PageView {
public:
    NodesView(NodesViewModel &view_model, AppNavigationCommands navigation) noexcept;
    ~NodesView() override;

    lv_obj_t *create(lv_obj_t *parent) override;
    void destroy() noexcept override;

private:
    struct RowContext {
        NodesView *view      = nullptr;
        std::uint32_t number = 0;
    };

    static void content_observer(lv_observer_t *observer, lv_subject_t *subject);
    static void select_node_event(lv_event_t *event);

    void render_rows();

    NodesViewModel &view_model_;
    AppNavigationCommands navigation_;
    lv_obj_t *page_ = nullptr;
    lv_obj_t *list_ = nullptr;
    std::vector<RowContext> row_contexts_;
};

#endif
