#include "pages/node_detail/node_detail_view.hpp"

#include "components/components.hpp"
#include "components/utf8.hpp"

#include <cm0/typography.h>
#include <cm0/input.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>

NodeDetailView::NodeDetailView(NodeDetailViewModel &view_model, AppNavigationCommands navigation) noexcept
    : view_model_(view_model), navigation_(navigation)
{
}

NodeDetailView::~NodeDetailView()
{
    destroy();
}

lv_obj_t *NodeDetailView::create(lv_obj_t *parent)
{
    destroy();
    page_ = create_page_body(parent);
    make_header(page_, "Node", nullptr, nullptr, nullptr, true, nullptr);
    lv_obj_t *header = lv_obj_get_child(page_, 0);
    lv_obj_t *back   = header ? lv_obj_get_child(header, 0) : nullptr;
    if (back) lv_obj_add_event_cb(back, back_event, LV_EVENT_CLICKED, this);

    content_ = make_flex(page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(content_, "node_detail");
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(content_, 16, 0);
    lv_subject_add_observer_obj(view_model_.content_subject(), content_observer, content_, this);
    create_waypoint_editor(parent);
    return page_;
}

void NodeDetailView::destroy() noexcept
{
    if (overlay_ && lv_obj_is_valid(overlay_)) lv_obj_delete(overlay_);
    overlay_ = waypoint_panel_ = waypoint_name_ = waypoint_description_ = nullptr;
    waypoint_latitude_ = waypoint_longitude_ = waypoint_expiry_ = waypoint_channel_ = keyboard_ = nullptr;
    channel_ids_.clear();
    destroy_page_body(page_);
    content_ = nullptr;
}

void NodeDetailView::content_observer(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<NodeDetailView *>(lv_observer_get_user_data(observer));
    if (view) view->render_content();
}

void NodeDetailView::back_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    if (view) view->navigation_.request_back();
}

void NodeDetailView::message_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    if (!view) return;
    const auto &node = view->view_model_.presentation();
    view->navigation_.request_direct_chat(view->view_model_.selected_node(), node.name.c_str());
}

void NodeDetailView::map_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    view->navigation_.request_map_node(view->view_model_.selected_node());
}

void NodeDetailView::render_content()
{
    if (!page_ || !content_) return;
    const auto scroll_y = lv_obj_get_scroll_y(page_);
    lv_obj_clean(content_);
    const auto &node = view_model_.presentation();
    if (!node.found) {
        make_wrapped_label(content_, "This node is no longer in the synchronized NodeDB.", 22, COLOR_SECONDARY);
        lv_obj_update_layout(page_);
        lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
        return;
    }

    lv_obj_t *hero = make_flex(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(hero, 156, 0);
    lv_obj_set_style_pad_row(hero, 8, 0);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *avatar   = make_dot(hero, COLOR_AVATAR, 80);
    lv_obj_t *initials = make_label(avatar, node.initials.c_str(), 28, COLOR_GREEN);
    lv_obj_center(initials);
    lv_obj_t *name = make_wrapped_label(hero, node.name.c_str(), 28, COLOR_TEXT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *presence = make_wrapped_label(hero, node.presence.c_str(), 14, COLOR_GREEN);
    lv_obj_set_style_text_align(presence, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *actions = make_flex(content_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_style_pad_row(actions, 8, 0);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *message = make_text_button(actions, "Message", COLOR_SURFACE, COLOR_PRIMARY_ACTION, message_event, this);
    lv_obj_set_name_static(message, "node_message");
    lv_obj_t *map = make_text_button(actions, "Show on Map", COLOR_BLUE, COLOR_SURFACE, map_event, this);
    lv_obj_set_name_static(map, "node_map");
    if (!node.has_position) lv_obj_add_state(map, LV_STATE_DISABLED);
    auto *request_metrics = make_text_button(actions, "Request telemetry", COLOR_BLUE, COLOR_SURFACE, telemetry_event, this);
    lv_obj_set_name_static(request_metrics, "node_request_telemetry");
    lv_obj_bind_state_if_eq(request_metrics, view_model_.connected_subject(), LV_STATE_DISABLED, 0);
    auto *trace = make_text_button(actions, "Trace route", COLOR_BLUE, COLOR_SURFACE, traceroute_event, this);
    lv_obj_set_name_static(trace, "node_traceroute");
    lv_obj_bind_state_if_eq(trace, view_model_.connected_subject(), LV_STATE_DISABLED, 0);
    auto *waypoint = make_text_button(actions, "Share waypoint", COLOR_BLUE, COLOR_SURFACE, waypoint_event, this);
    lv_obj_set_name_static(waypoint, "node_share_waypoint");
    lv_obj_bind_state_if_eq(waypoint, view_model_.connected_subject(), LV_STATE_DISABLED, 0);
    auto *feedback = make_wrapped_label(content_, "", 14, COLOR_SECONDARY);
    lv_obj_set_name_static(feedback, "node_action_status");
    lv_label_bind_text(feedback, view_model_.action_status_subject(), nullptr);

    if (node.has_metrics) {
        make_section_label(content_, "TELEMETRY");
        lv_obj_t *telemetry = make_card(content_);
        lv_obj_set_flex_flow(telemetry, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(telemetry, 16, 0);
        if (!node.battery.empty()) {
            lv_obj_t *metric = make_flex(telemetry, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_height(metric, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(metric, 1);
            make_wrapped_label(metric, node.battery.c_str(), 22, COLOR_GREEN);
            make_label(metric, "Battery", 14, COLOR_SECONDARY);
        }
        if (!node.voltage.empty()) {
            lv_obj_t *metric = make_flex(telemetry, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_height(metric, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(metric, 1);
            make_wrapped_label(metric, node.voltage.c_str(), 22, COLOR_BLUE);
            make_label(metric, "Voltage", 14, COLOR_SECONDARY);
        }
    }
    if (node.has_position) {
        make_section_label(content_, "POSITION");
        lv_obj_t *position = make_card(content_);
        make_wrapped_label(position, node.coordinates.c_str(), 22, COLOR_TEXT);
        if (!node.altitude.empty()) make_wrapped_label(position, node.altitude.c_str(), 14, COLOR_SECONDARY);
    }
    make_section_label(content_, "TELEMETRY HISTORY");
    auto *history = make_card(content_);
    lv_obj_set_name_static(history, "node_telemetry_history");
    if (node.telemetry_history.empty()) make_wrapped_label(history, "No recorded telemetry. Request a sample from this node.", 14, COLOR_SECONDARY);
    for (const auto &sample : node.telemetry_history) make_wrapped_label(history, sample.c_str(), 14, COLOR_TEXT);
    make_section_label(content_, "ROUTE HISTORY");
    auto *routes = make_card(content_);
    lv_obj_set_name_static(routes, "node_route_history");
    if (node.route_history.empty()) make_wrapped_label(routes, "No route reply recorded. Trace a remote node to discover its path.", 14, COLOR_SECONDARY);
    for (const auto &route : node.route_history) make_wrapped_label(routes, route.c_str(), 14, COLOR_TEXT);
    const auto position_count = std::string(node.position_history_limited ? "10,000+" : std::to_string(node.position_count)) +
                                " recorded positions; view tracks on the map";
    make_wrapped_label(routes, position_count.c_str(), 14, COLOR_SECONDARY);
    make_section_label(content_, "RADIO INFORMATION");
    lv_obj_t *details = make_card(content_);
    make_key_value(details, "NODE ID", node.id.c_str(), COLOR_TEXT);
    make_key_value(details, "NODE NUMBER", node.number.c_str(), COLOR_TEXT);
    make_key_value(details, "SIGNAL", node.signal.c_str(), COLOR_TEXT);
    make_key_value(details, "ROLE", node.role.c_str(), COLOR_TEXT);
    make_key_value(details, "ROUTE", node.route.c_str(), COLOR_TEXT);
    make_key_value(details, "LAST HEARD", node.last_heard.c_str(), COLOR_TEXT);
    lv_obj_update_layout(page_);
    lv_obj_scroll_to_y(page_, scroll_y, LV_ANIM_OFF);
}

void NodeDetailView::telemetry_event(lv_event_t *event)
{
    static_cast<NodeDetailView *>(lv_event_get_user_data(event))->view_model_.request_telemetry();
}

void NodeDetailView::traceroute_event(lv_event_t *event)
{
    static_cast<NodeDetailView *>(lv_event_get_user_data(event))->view_model_.request_traceroute();
}

void NodeDetailView::create_waypoint_editor(lv_obj_t *parent)
{
    overlay_ = make_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_name_static(overlay_, "waypoint_editor");
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    style_container(overlay_, COLOR_PAGE, 0);
    lv_obj_set_style_pad_all(overlay_, 8, 0);
    lv_obj_set_style_pad_row(overlay_, 8, 0);
    waypoint_panel_ = make_flex(overlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(waypoint_panel_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(waypoint_panel_, 1);
    lv_obj_add_flag(waypoint_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(waypoint_panel_, LV_DIR_VER);
    lv_obj_set_style_pad_row(waypoint_panel_, 8, 0);
    make_wrapped_label(waypoint_panel_, "Share a waypoint", 28, COLOR_TITLE);
    make_wrapped_label(waypoint_panel_, "Choose coordinates, expiry and the channel that will receive this waypoint.", 14, COLOR_SECONDARY);
    auto field = [this](const char *label, const char *name, std::uint32_t length) {
        make_label(waypoint_panel_, label, 14, COLOR_TEXT);
        auto *editor = lv_textarea_create(waypoint_panel_);
        lv_obj_set_name_static(editor, name);
        lv_obj_set_size(editor, LV_PCT(100), 46);
        lv_textarea_set_one_line(editor, true);
        lv_textarea_set_max_length(editor, length);
        if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(editor, font, 0);
        lv_obj_add_event_cb(editor, editor_event, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(editor, editor_event, LV_EVENT_CLICKED, this);
        return editor;
    };
    waypoint_name_ = field("Name (up to 30 UTF-8 bytes)", "waypoint_name", 30);
    waypoint_description_ = field("Description (up to 100 UTF-8 bytes)", "waypoint_description", 100);
    waypoint_latitude_ = field("Latitude (−90 to 90)", "waypoint_latitude", 24);
    waypoint_longitude_ = field("Longitude (−180 to 180)", "waypoint_longitude", 24);
    make_label(waypoint_panel_, "Expires after", 14, COLOR_TEXT);
    waypoint_expiry_ = lv_dropdown_create(waypoint_panel_);
    lv_obj_set_name_static(waypoint_expiry_, "waypoint_expiry");
    lv_obj_set_size(waypoint_expiry_, LV_PCT(100), 44);
    lv_dropdown_set_options(waypoint_expiry_, "1 hour\n8 hours\n1 day\n7 days");
    lv_dropdown_set_selected(waypoint_expiry_, 2);
    make_label(waypoint_panel_, "Channel", 14, COLOR_TEXT);
    waypoint_channel_ = lv_dropdown_create(waypoint_panel_);
    lv_obj_set_name_static(waypoint_channel_, "waypoint_channel");
    lv_obj_set_size(waypoint_channel_, LV_PCT(100), 44);
    for (auto *dropdown : {waypoint_expiry_, waypoint_channel_}) {
        if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(dropdown, font, 0);
    }
    auto *feedback = make_wrapped_label(waypoint_panel_, "", 14, COLOR_SECONDARY);
    lv_label_bind_text(feedback, view_model_.action_status_subject(), nullptr);
    auto *actions = make_flex(overlay_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(actions, 8, 0);
    auto *cancel = make_text_button(actions, "Cancel", COLOR_TEXT, COLOR_SURFACE, close_waypoint_event, this);
    auto *share = make_text_button(actions, "Share", COLOR_SURFACE, COLOR_BLUE, share_waypoint_event, this);
    lv_obj_set_name_static(share, "waypoint_send");
    lv_obj_set_flex_grow(cancel, 1); lv_obj_set_flex_grow(share, 1);
    keyboard_ = lv_keyboard_create(overlay_);
    lv_obj_set_size(keyboard_, LV_PCT(100), LV_PCT(35));
    lv_obj_set_style_min_height(keyboard_, 100, 0);
    lv_obj_set_style_max_height(keyboard_, 240, 0);
    if (const auto *font = lilygo_ui_font_get(14)) lv_obj_set_style_text_font(keyboard_, font, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void NodeDetailView::waypoint_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    if (!view->overlay_) return;
    const auto &node = view->view_model_.presentation();
    lv_textarea_set_text(view->waypoint_name_, utf8_prefix_bytes(node.name, 30).c_str());
    lv_textarea_set_text(view->waypoint_description_, "");
    char latitude[32]{}, longitude[32]{};
    if (node.has_position) {
        std::snprintf(latitude, sizeof(latitude), "%.7f", node.latitude);
        std::snprintf(longitude, sizeof(longitude), "%.7f", node.longitude);
    }
    lv_textarea_set_text(view->waypoint_latitude_, latitude);
    lv_textarea_set_text(view->waypoint_longitude_, longitude);
    view->channel_ids_.clear();
    std::string options;
    for (const auto &channel : view->view_model_.channels()) {
        if (channel.role == meshtastic::Channel::DISABLED || channel.index < 0) continue;
        if (!options.empty()) options += '\n';
        options += channel.name.empty() ? "Channel " + std::to_string(channel.index) : channel.name;
        view->channel_ids_.push_back(static_cast<std::uint32_t>(channel.index));
    }
    if (options.empty()) { options = "Primary channel"; view->channel_ids_.push_back(0); }
    lv_dropdown_set_options(view->waypoint_channel_, options.c_str());
    lv_obj_clear_flag(view->overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(view->overlay_);
}

void NodeDetailView::close_waypoint_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    lv_keyboard_set_textarea(view->keyboard_, nullptr);
    lv_obj_add_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->overlay_, LV_OBJ_FLAG_HIDDEN);
}

void NodeDetailView::share_waypoint_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    auto parse = [](const char *text) {
        std::size_t end = 0;
        const std::string value(text);
        double number = std::stod(value, &end);
        if (end != value.size() || !std::isfinite(number)) throw std::invalid_argument("coordinate");
        return number;
    };
    try {
        const double latitude = parse(lv_textarea_get_text(view->waypoint_latitude_));
        const double longitude = parse(lv_textarea_get_text(view->waypoint_longitude_));
        constexpr std::uint32_t lifetime[] = {3600, 28800, 86400, 604800};
        const auto selected = lv_dropdown_get_selected(view->waypoint_channel_);
        if (selected >= view->channel_ids_.size()) return;
        if (view->view_model_.share_waypoint(lv_textarea_get_text(view->waypoint_name_),
                lv_textarea_get_text(view->waypoint_description_), latitude, longitude,
                lifetime[lv_dropdown_get_selected(view->waypoint_expiry_) % 4], view->channel_ids_[selected]))
            close_waypoint_event(event);
    } catch (const std::exception &) {
        view->view_model_.share_waypoint("", "", 0, 0, 0, 0);
    }
}

void NodeDetailView::editor_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    if (cm0_input_hardware_keyboard_present()) return;
    lv_keyboard_set_textarea(view->keyboard_, lv_event_get_target_obj(event));
    lv_obj_clear_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view(lv_event_get_target_obj(event), LV_ANIM_OFF);
}

void NodeDetailView::keyboard_event(lv_event_t *event)
{
    auto *view = static_cast<NodeDetailView *>(lv_event_get_user_data(event));
    lv_obj_add_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(view->keyboard_, nullptr);
}
