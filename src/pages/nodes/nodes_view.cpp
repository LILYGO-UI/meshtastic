#include "pages/nodes/nodes_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

NodesView::NodesView(NodesViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

NodesView::~NodesView()
{
    destroy();
}

lv_obj_t *NodesView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Nodes", "Filter", nullptr, nullptr, false);
    make_search_field(page_, "Search nodes");
    lv_obj_t *count = make_label(page_, "", 14, COLOR_BLUE);
    lv_label_bind_text(count, view_model_.count_subject(), nullptr);
    list_ = make_card(page_);
    lv_obj_set_name_static(list_, "nodes_list");
    lv_obj_t *footer = make_label(page_, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(footer, view_model_.footer_subject(), nullptr);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, list_, this);
    return page_;
}

void NodesView::destroy() noexcept
{
    destroy_page_body(page_);
    list_ = nullptr;
    row_contexts_.clear();
}

void NodesView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<NodesView *>(lv_observer_get_user_data(observer));
    if (view) view->render_rows();
}

void NodesView::select_node_event(lv_event_t *event)
{
    auto *context = static_cast<RowContext *>(lv_event_get_user_data(event));
    if (!context || !context->view) return;
    context->view->view_model_.select_node(context->number);
    context->view->navigation_.request(AppPage::node_detail);
}

void NodesView::render_rows()
{
    if (!page_ || !list_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(list_);
    row_contexts_.clear();
    row_contexts_.reserve(view_model_.rows().size());
    if (view_model_.rows().empty()) {
        make_wrapped_label(
            list_, view_model_.connected() ? "The radio NodeDB is empty." : "Connect to synchronize the radio NodeDB.",
            14, COLOR_SECONDARY);
    }
    for (const auto &row : view_model_.rows()) {
        row_contexts_.push_back({this, row.number});
        lv_obj_t *node = make_info_row(list_, row.local ? COLOR_GREEN_LIGHT : COLOR_PALE_BLUE, row.name.c_str(),
                                       row.detail.c_str(), select_node_event, &row_contexts_.back());
        lv_obj_set_name_static(node, "node_row");
    }
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}
