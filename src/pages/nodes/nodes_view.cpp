#include "pages/nodes/nodes_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

namespace {

lv_obj_t *make_centered_status_label(lv_obj_t *parent, const char *text, std::uint32_t color)
{
    lv_obj_t *label = make_wrapped_label(parent, text, 14, color);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(label, 8, 0);
    return label;
}

void style_node_card(lv_obj_t *card)
{
    // Keep rows visually grouped like a native device list. The card retains
    // its rounded outer padding while adjacent rows share one continuous
    // surface instead of floating apart inside the card.
    lv_obj_set_style_pad_row(card, 0, 0);
}

void style_node_row(lv_obj_t *row, bool has_next)
{
    if (!row) return;
    if (!has_next) return;
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(COLOR_SEPARATOR), 0);
}

}  // namespace

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
    make_header(page_, "Nodes", nullptr, nullptr, nullptr, false);
    search_.create(page_, "nodes_search", "Search nodes", view_model_.query(),
                   [this](std::string query) { view_model_.set_query(std::move(query)); });
    auto *filter = lv_dropdown_create(page_);
    lv_obj_set_name_static(filter, "nodes_filter");
    lv_obj_set_size(filter, LV_PCT(100), 44);
    lv_dropdown_set_options(filter, "All nodes\nRecently heard\nWith position\nOne hop");
    lv_dropdown_set_selected(filter, view_model_.filter());
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(filter, font, 0);
    lv_obj_add_event_cb(filter, filter_event, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_t *count = make_label(page_, "", 14, COLOR_BLUE);
    lv_obj_set_width(count, LV_PCT(100));
    lv_obj_set_style_text_align(count, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_bind_text(count, view_model_.count_subject(), nullptr);
    list_ = make_card(page_);
    lv_obj_set_name_static(list_, "nodes_list");
    lv_obj_t *footer = make_label(page_, "", 14, COLOR_SECONDARY);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_bind_text(footer, view_model_.footer_subject(), nullptr);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, list_, this);
    return page_;
}

void NodesView::destroy() noexcept
{
    search_.destroy();
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
        make_centered_status_label(
            list_, view_model_.connected() ? "The radio NodeDB is empty." : "Connect to synchronize the radio NodeDB.",
            COLOR_SECONDARY);
    } else {
        lv_obj_t *local_heading = make_section_label(list_, "THIS DEVICE");
        lv_obj_t *local_card    = make_card(list_);
        lv_obj_set_name_static(local_card, "nodes_local");
        style_node_card(local_card);
        bool has_local           = false;
        lv_obj_t *previous_local = nullptr;
        for (const auto &row : view_model_.rows()) {
            if (!row.local) continue;
            has_local = true;
            row_contexts_.push_back({this, row.number});
            lv_obj_t *node = make_info_row(local_card, COLOR_GREEN, row.name.c_str(), row.detail.c_str(),
                                           select_node_event, &row_contexts_.back());
            lv_obj_set_name_static(node, "node_row");
            style_node_row(previous_local, true);
            previous_local = node;
        }
        if (!has_local) {
            lv_obj_delete(local_card);
            lv_obj_delete(local_heading);
        }

        make_section_label(list_, "MESH CONTACTS");
        lv_obj_t *contacts_card = make_card(list_);
        lv_obj_set_name_static(contacts_card, "nodes_contacts");
        style_node_card(contacts_card);
        bool has_contacts          = false;
        lv_obj_t *previous_contact = nullptr;
        for (const auto &row : view_model_.rows()) {
            if (row.local) continue;
            has_contacts = true;
            row_contexts_.push_back({this, row.number});
            lv_obj_t *node = make_info_row(contacts_card, row.local ? COLOR_GREEN : COLOR_PALE_BLUE, row.name.c_str(),
                                           row.detail.c_str(), select_node_event, &row_contexts_.back());
            lv_obj_set_name_static(node, "node_row");
            style_node_row(previous_contact, true);
            previous_contact = node;
        }
        if (!has_contacts) {
            make_centered_status_label(contacts_card, "No other nodes are known yet.", COLOR_SECONDARY);
        }
    }
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}

void NodesView::filter_event(lv_event_t *event)
{
    auto *view = static_cast<NodesView *>(lv_event_get_user_data(event));
    view->view_model_.set_filter(lv_dropdown_get_selected(lv_event_get_target_obj(event)));
}
