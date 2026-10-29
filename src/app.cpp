#include "app.hpp"

#include "app_identity.h"
#include "app_router.hpp"
#include "components/components.hpp"
#include "domain/meshtastic_client.hpp"
#include "pages/channels/channels_view.hpp"
#include "pages/chat/chat_view.hpp"
#include "pages/connection/connection_view.hpp"
#include "pages/device_tools/device_tools_view.hpp"
#include "pages/map/map_view.hpp"
#include "pages/map/map_view_model.hpp"
#include "pages/messages/messages_view.hpp"
#include "pages/node_detail/node_detail_view.hpp"
#include "pages/nodes/nodes_view.hpp"
#include "pages/page_view.hpp"

#include <cm0/typography.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

enum class PendingNavigation {
    none,
    navigate,
    back,
    direct_chat,
    node_detail,
    map_node,
};

struct AppViewState {
    AppRouter router;
    std::unique_ptr<meshtastic::MeshtasticClient> client;
    std::unique_ptr<ConnectionViewModel> connection_view_model;
    std::unique_ptr<MessagesViewModel> messages_view_model;
    std::unique_ptr<NodesViewModel> nodes_view_model;
    std::unique_ptr<ChannelsViewModel> channels_view_model;
    std::unique_ptr<ChatViewModel> chat_view_model;
    std::unique_ptr<NodeDetailViewModel> node_detail_view_model;
    std::unique_ptr<DeviceToolsViewModel> device_tools_view_model;
    std::unique_ptr<MapViewModel> map_view_model;
    std::unique_ptr<PageView> active_view;
    meshtastic::ClientSnapshot snapshot;
    std::uint64_t rendered_revision      = 0;
    std::uint32_t last_radio_number = 0;
    lv_obj_t *surface                    = nullptr;
    lv_obj_t *content                    = nullptr;
    lv_obj_t *page                       = nullptr;
    lv_obj_t *navigation                 = nullptr;
    lv_timer_t *refresh_timer            = nullptr;
    PendingNavigation pending_navigation = PendingNavigation::none;
    AppPage pending_page                 = AppPage::connect;
    AppPage detail_return_page           = AppPage::nodes;
    std::uint32_t pending_direct_peer    = 0;
    std::uint32_t pending_node = 0;
    std::string pending_direct_name;
    bool navigation_scheduled            = false;
    std::int32_t page_scroll_y           = 0;
    std::int32_t layout_width            = 0;
    bool applying_layout                 = false;
    bool ignore_geometry_scroll_reset    = false;
};

struct NavigationItem {
    AppPage page;
    const char *icon;
    const char *label;
    const char *name;
};

AppViewState app_state;
meshtastic::Endpoint configured_endpoint;
bool configured_auto_connect = true;

constexpr NavigationItem nav_items[] = {
    {AppPage::messages, LV_SYMBOL_ENVELOPE, "Messages", "nav_messages"},
    {AppPage::nodes, LV_SYMBOL_DIRECTORY, "Nodes", "nav_nodes"},
    {AppPage::map, LV_SYMBOL_GPS, "Map", "nav_map"},
    {AppPage::connect, LV_SYMBOL_WIFI, "Status", "nav_connect"},
};

void apply_layout();
void render_page();
void process_navigation(void *user_data);

void page_scroll_event(lv_event_t *event)
{
    if (app_state.applying_layout) return;
    if (!app_state.surface || lv_obj_get_content_width(app_state.surface) != app_state.layout_width) return;
    lv_obj_t *page = lv_event_get_target_obj(event);
    if (page == app_state.page) {
        const auto scroll_y = lv_obj_get_scroll_y(page);
        if (app_state.ignore_geometry_scroll_reset && scroll_y == 0) {
            app_state.ignore_geometry_scroll_reset = false;
            return;
        }
        app_state.page_scroll_y = scroll_y;
    }
}

void page_layout_event(lv_event_t *event)
{
    if (app_state.applying_layout || app_state.page_scroll_y == 0) return;
    lv_obj_t *page = lv_event_get_target_obj(event);
    if (page != app_state.page || lv_obj_get_scroll_y(page) == app_state.page_scroll_y) return;
    app_state.applying_layout = true;
    lv_obj_scroll_to_y(page, app_state.page_scroll_y, LV_ANIM_OFF);
    app_state.applying_layout = false;
}

void sync_view_models()
{
    if (!app_state.client) return;
    const bool connection_enabled = app_state.client->enabled();
    if (app_state.connection_view_model) {
        app_state.connection_view_model->sync(app_state.snapshot, connection_enabled);
    }
    if (app_state.messages_view_model) app_state.messages_view_model->sync(app_state.snapshot);
    if (app_state.nodes_view_model) app_state.nodes_view_model->sync(app_state.snapshot);
    if (app_state.channels_view_model) app_state.channels_view_model->sync(app_state.snapshot);
    if (app_state.channels_view_model && app_state.chat_view_model) {
        // Preserve the conversation chosen from Messages, including private
        // peers. The primary channel is only the initial fallback; repeatedly
        // copying channel selection here would make every direct chat snap
        // back to the channel on the next snapshot refresh.
        if (!app_state.chat_view_model->has_selection()) {
            app_state.chat_view_model->select_channel(app_state.channels_view_model->selected_channel(),
                                                      app_state.channels_view_model->selected_channel_name());
        }
        app_state.chat_view_model->sync(app_state.snapshot);
    }
    if (app_state.node_detail_view_model && app_state.router.page() == AppPage::node_detail)
        app_state.node_detail_view_model->sync(app_state.snapshot);
    if (app_state.device_tools_view_model) {
        app_state.device_tools_view_model->sync(app_state.snapshot, connection_enabled);
    }
    if (app_state.map_view_model) {
        app_state.map_view_model->set_active(app_state.router.page() == AppPage::map);
        app_state.map_view_model->sync(app_state.snapshot);
    }
}

void schedule_navigation(PendingNavigation action, AppPage page = AppPage::connect)
{
    if (!app_state.surface) return;
    app_state.pending_navigation = action;
    app_state.pending_page       = page;
    if (app_state.navigation_scheduled) return;
    if (lv_async_call(process_navigation, &app_state) == LV_RESULT_OK) {
        app_state.navigation_scheduled = true;
    } else {
        app_state.pending_navigation = PendingNavigation::none;
    }
}

void request_page(void *context, AppPage page)
{
    (void)context;
    schedule_navigation(PendingNavigation::navigate, page);
}

void request_back(void *context)
{
    (void)context;
    schedule_navigation(PendingNavigation::back);
}

void request_direct_chat(void *context, std::uint32_t peer, const char *name)
{
    (void)context;
    app_state.pending_direct_peer = peer;
    app_state.pending_direct_name = name ? name : "";
    schedule_navigation(PendingNavigation::direct_chat, AppPage::chat);
}

void request_node_detail(void *, std::uint32_t node)
{
    app_state.detail_return_page = app_state.router.page();
    app_state.pending_node = node;
    schedule_navigation(PendingNavigation::node_detail, AppPage::node_detail);
}

void request_map_node(void *, std::uint32_t node)
{
    app_state.pending_node = node;
    schedule_navigation(PendingNavigation::map_node, AppPage::map);
}

AppNavigationCommands navigation_commands()
{
    return {nullptr, request_page, request_back, request_direct_chat, request_node_detail, request_map_node};
}

void navigation_event(lv_event_t *event)
{
    const auto *item = static_cast<const NavigationItem *>(lv_event_get_user_data(event));
    if (item) schedule_navigation(PendingNavigation::navigate, item->page);
}

void process_navigation(void *user_data)
{
    auto *state = static_cast<AppViewState *>(user_data);
    if (!state || state != &app_state) return;

    const auto action           = state->pending_navigation;
    const auto target           = state->pending_page;
    state->pending_navigation   = PendingNavigation::none;
    state->navigation_scheduled = false;
    if (!state->surface || action == PendingNavigation::none) return;

    bool changed = false;
    if (action == PendingNavigation::back) {
        changed = state->router.page() == AppPage::node_detail
                      ? state->router.navigate(state->detail_return_page)
                      : state->router.back();
    } else if (action == PendingNavigation::node_detail) {
        if (state->node_detail_view_model) state->node_detail_view_model->select_node(state->pending_node, state->snapshot);
        changed = state->router.navigate(AppPage::node_detail);
    } else if (action == PendingNavigation::map_node) {
        if (state->map_view_model) {
            state->map_view_model->select_node(state->pending_node);
            state->map_view_model->follow_selected();
        }
        changed = state->router.navigate(AppPage::map);
    } else if (action == PendingNavigation::direct_chat) {
        if (state->chat_view_model) {
            state->chat_view_model->select_conversation(true, state->pending_direct_peer, 0,
                                                        std::move(state->pending_direct_name));
        }
        state->pending_direct_peer = 0;
        changed                    = state->router.navigate(AppPage::chat);
    } else {
        if (target == AppPage::node_detail && state->nodes_view_model && state->node_detail_view_model) {
            state->detail_return_page = state->router.page();
            state->node_detail_view_model->select_node(state->nodes_view_model->selected_node(), state->snapshot);
        }
        if (target == AppPage::chat && state->channels_view_model && state->chat_view_model) {
            if (state->router.page() == AppPage::messages && state->messages_view_model &&
                state->messages_view_model->has_selected_conversation()) {
                state->chat_view_model->select_conversation(state->messages_view_model->selected_direct(),
                                                            state->messages_view_model->selected_peer(),
                                                            state->messages_view_model->selected_channel(),
                                                            state->messages_view_model->selected_name());
            } else if (state->router.page() == AppPage::node_detail && state->node_detail_view_model) {
                const auto &node = state->node_detail_view_model->presentation();
                state->chat_view_model->select_conversation(true, state->node_detail_view_model->selected_node(), 0,
                                                            node.name);
            } else {
                if (state->router.page() != AppPage::channels) state->channels_view_model->select_primary();
                state->chat_view_model->select_channel(state->channels_view_model->selected_channel(),
                                                       state->channels_view_model->selected_channel_name());
            }
        }
        changed = state->router.navigate(target);
    }
    if (state->chat_view_model) {
        state->chat_view_model->set_active(state->router.page() == AppPage::chat);
        state->chat_view_model->sync(state->client->snapshot());
    }
    if (state->map_view_model) state->map_view_model->set_active(state->router.page() == AppPage::map);
    if (changed) render_page();
}

void rebuild_navigation()
{
    if (!app_state.navigation) return;
    lv_obj_clean(app_state.navigation);
    for (std::size_t index = 0; index < std::size(nav_items); ++index) {
        const auto &item = nav_items[index];
        lv_obj_t *button = lv_button_create(app_state.navigation);
        lv_obj_t *label  = nullptr;
        style_container(button, COLOR_SURFACE, 0);
        lv_obj_set_name_static(button, item.name);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_pad_all(button, 4, 0);
        AppPage selected = app_state.router.page();
        if (selected == AppPage::channels || selected == AppPage::chat) selected = AppPage::messages;
        if (selected == AppPage::node_detail) selected = AppPage::nodes;
        if (selected == AppPage::device_tools) selected = AppPage::connect;
        label = make_label(button, item.icon, 22, selected == item.page ? COLOR_BLUE : COLOR_MUTED_ICON);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, navigation_event, LV_EVENT_CLICKED, const_cast<NavigationItem *>(&item));
    }
}

void render_page()
{
    if (!app_state.content || !app_state.connection_view_model || !app_state.messages_view_model ||
        !app_state.nodes_view_model || !app_state.channels_view_model || !app_state.chat_view_model ||
        !app_state.node_detail_view_model || !app_state.device_tools_view_model || !app_state.map_view_model) {
        return;
    }

    if (app_state.active_view) app_state.active_view->destroy();
    app_state.active_view.reset();
    app_state.page = nullptr;

    const auto navigation = navigation_commands();
    switch (app_state.router.page()) {
        case AppPage::messages:
            app_state.active_view = std::make_unique<MessagesView>(*app_state.messages_view_model, navigation);
            break;
        case AppPage::nodes:
            app_state.active_view = std::make_unique<NodesView>(*app_state.nodes_view_model, navigation);
            break;
        case AppPage::map:
            app_state.active_view = std::make_unique<MapView>(*app_state.map_view_model, navigation);
            break;
        case AppPage::connect:
            app_state.active_view = std::make_unique<ConnectionView>(*app_state.connection_view_model, navigation);
            break;
        case AppPage::channels:
            app_state.active_view = std::make_unique<ChannelsView>(*app_state.channels_view_model, navigation);
            break;
        case AppPage::chat:
            app_state.active_view = std::make_unique<ChatView>(*app_state.chat_view_model, navigation);
            break;
        case AppPage::node_detail:
            app_state.active_view = std::make_unique<NodeDetailView>(*app_state.node_detail_view_model, navigation);
            break;
        case AppPage::device_tools:
            app_state.active_view = std::make_unique<DeviceToolsView>(*app_state.device_tools_view_model, navigation);
            break;
        default:
            return;
    }

    app_state.page          = app_state.active_view->create(app_state.content);
    app_state.page_scroll_y = 0;
    lv_obj_add_event_cb(app_state.page, page_scroll_event, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(app_state.page, page_layout_event, LV_EVENT_LAYOUT_CHANGED, nullptr);
    rebuild_navigation();
    apply_layout();
}

void apply_layout()
{
    if (!app_state.surface || !app_state.content || !app_state.navigation) return;
    if (app_state.applying_layout) return;
    app_state.applying_layout              = true;
    const std::int32_t page_scroll_y       = app_state.page_scroll_y;
    const std::int32_t width               = lv_obj_get_content_width(app_state.surface);
    app_state.ignore_geometry_scroll_reset = width != app_state.layout_width && page_scroll_y != 0;
    const bool desktop                     = width >= 800;
    const bool show_navigation             = AppRouter::page_has_navigation(app_state.router.page());
    if (app_state.active_view) app_state.active_view->prepare_layout();
    lv_obj_set_flex_flow(app_state.surface, desktop ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(app_state.content, desktop ? 0 : LV_PCT(100));
    // Keep the content viewport bounded by the surface. A content-sized
    // parent grows with the chat history and pushes the composer below the
    // physical display; flex growth gives it the space left by navigation.
    lv_obj_set_height(app_state.content, desktop ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(app_state.content, 1);
    if (show_navigation) {
        lv_obj_clear_flag(app_state.navigation, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(app_state.navigation, LV_OBJ_FLAG_HIDDEN);
    }

    if (desktop) {
        lv_obj_move_to_index(app_state.navigation, 0);
        lv_obj_set_size(app_state.navigation, 180, LV_PCT(100));
        lv_obj_set_flex_flow(app_state.navigation, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(app_state.navigation, 12, 0);
        for (std::size_t index = 0; index < std::size(nav_items); ++index) {
            lv_obj_t *button = lv_obj_get_child(app_state.navigation, static_cast<std::int32_t>(index));
            if (!button) continue;
            lv_obj_set_size(button, LV_PCT(100), 64);
            lv_obj_set_flex_grow(button, 0);
            lv_obj_t *label = lv_obj_get_child(button, 0);
            if (label) lv_label_set_text_fmt(label, "%s  %s", nav_items[index].icon, nav_items[index].label);
        }
        if (app_state.page) {
            lv_obj_set_style_pad_all(app_state.page, 28, 0);
            lv_obj_set_style_pad_row(app_state.page, 18, 0);
        }
    } else {
        lv_obj_move_to_index(app_state.content, 0);
        lv_obj_set_size(app_state.navigation, LV_PCT(100), 83);
        lv_obj_set_flex_flow(app_state.navigation, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(app_state.navigation, 12, 0);
        for (std::size_t index = 0; index < std::size(nav_items); ++index) {
            lv_obj_t *button = lv_obj_get_child(app_state.navigation, static_cast<std::int32_t>(index));
            if (!button) continue;
            lv_obj_set_size(button, LV_SIZE_CONTENT, LV_PCT(100));
            lv_obj_set_flex_grow(button, 1);
            lv_obj_t *label = lv_obj_get_child(button, 0);
            if (!label) continue;
            lv_label_set_text(label, nav_items[index].icon);
        }
        if (app_state.page) {
            lv_obj_set_style_pad_all(app_state.page, width < 400 ? 12 : 16, 0);
            lv_obj_set_style_pad_row(app_state.page, 12, 0);
        }
    }
    if (app_state.page && page_scroll_y != 0) {
        lv_obj_update_layout(app_state.surface);
        lv_obj_scroll_to_y(app_state.page, page_scroll_y, LV_ANIM_OFF);
    }
    // Reveal an active editor after LVGL finishes applying the new geometry.
    if (app_state.active_view) app_state.active_view->update_layout();
    app_state.layout_width    = width;
    app_state.applying_layout = false;
}

void size_changed_event(lv_event_t *event)
{
    (void)event;
    apply_layout();
}

void refresh_snapshot(lv_timer_t *timer)
{
    (void)timer;
    if (!app_state.client) return;
    auto snapshot = app_state.client->snapshot();
    if (snapshot.revision == app_state.rendered_revision) {
        // Administrative readback has its own deadline even on a quiet link.
        if (app_state.channels_view_model) app_state.channels_view_model->sync(snapshot);
        if (app_state.device_tools_view_model)
            app_state.device_tools_view_model->sync(snapshot, app_state.client->enabled());
        return;
    }
    if (snapshot.my_node_number && app_state.last_radio_number &&
        snapshot.my_node_number != app_state.last_radio_number) {
        // Close editors while their view models still belong to the previous
        // radio, then publish the new radio's isolated state.
        if (app_state.navigation_scheduled) lv_async_call_cancel(process_navigation, &app_state);
        app_state.navigation_scheduled = false;
        app_state.pending_navigation = PendingNavigation::none;
        app_state.pending_node = 0;
        app_state.pending_direct_peer = 0;
        app_state.pending_direct_name.clear();
        app_state.chat_view_model->set_active(false);
        app_state.map_view_model->set_active(false);
        app_state.messages_view_model->clear_selection();
        app_state.nodes_view_model->select_node(0);
        if (app_state.router.navigate(AppPage::connect)) render_page();
        app_state.chat_view_model->select_channel(0, "Primary channel");
    }
    if (snapshot.my_node_number) app_state.last_radio_number = snapshot.my_node_number;
    app_state.snapshot          = std::move(snapshot);
    app_state.rendered_revision = app_state.snapshot.revision;
    sync_view_models();
}

void app_open(const cm0_app_context_t *context)
{
    if (!context || !context->root) return;
    app_state.router                  = AppRouter{};
    app_state.detail_return_page      = AppPage::nodes;
    app_state.client                  = std::make_unique<meshtastic::MeshtasticClient>(configured_endpoint);
    app_state.connection_view_model   = std::make_unique<ConnectionViewModel>(app_state.client.get());
    app_state.messages_view_model     = std::make_unique<MessagesViewModel>();
    app_state.nodes_view_model        = std::make_unique<NodesViewModel>();
    app_state.channels_view_model     = std::make_unique<ChannelsViewModel>(app_state.client.get());
    app_state.chat_view_model         = std::make_unique<ChatViewModel>(app_state.client.get());
    app_state.node_detail_view_model  = std::make_unique<NodeDetailViewModel>(app_state.client.get());
    app_state.device_tools_view_model = std::make_unique<DeviceToolsViewModel>(app_state.client.get());
    app_state.map_view_model          = std::make_unique<MapViewModel>();
    app_state.map_view_model->set_client(app_state.client.get());
    app_state.client->start(configured_auto_connect);
    app_state.snapshot          = app_state.client->snapshot();
    app_state.last_radio_number = app_state.snapshot.my_node_number;
    app_state.rendered_revision = app_state.snapshot.revision;
    sync_view_models();

    app_state.surface = lv_obj_create(context->root);
    lv_obj_set_name_static(app_state.surface, "app_surface");
    lv_obj_set_size(app_state.surface, LV_PCT(100), LV_PCT(100));
    style_container(app_state.surface, COLOR_PAGE, 0);
    lv_obj_set_style_pad_all(app_state.surface, 0, 0);
    lv_obj_clear_flag(app_state.surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_state.surface, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(app_state.surface, size_changed_event, LV_EVENT_SIZE_CHANGED, nullptr);

    app_state.content = lv_obj_create(app_state.surface);
    lv_obj_set_name_static(app_state.content, "app_content");
    style_container(app_state.content, COLOR_PAGE, 0);
    lv_obj_set_width(app_state.content, LV_PCT(100));
    lv_obj_set_flex_grow(app_state.content, 1);
    lv_obj_set_style_pad_all(app_state.content, 0, 0);
    lv_obj_clear_flag(app_state.content, LV_OBJ_FLAG_SCROLLABLE);

    app_state.navigation = lv_obj_create(app_state.surface);
    lv_obj_set_name_static(app_state.navigation, "app_navigation");
    style_container(app_state.navigation, COLOR_SURFACE, 0);
    lv_obj_set_style_pad_all(app_state.navigation, 4, 0);
    lv_obj_clear_flag(app_state.navigation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(app_state.navigation, LV_FLEX_FLOW_ROW);
    render_page();
    app_state.refresh_timer = lv_timer_create(refresh_snapshot, 250, nullptr);
}

void app_close()
{
    if (app_state.navigation_scheduled) lv_async_call_cancel(process_navigation, &app_state);
    app_state.navigation_scheduled = false;
    app_state.pending_navigation   = PendingNavigation::none;
    app_state.pending_direct_peer  = 0;
    app_state.pending_direct_name.clear();
    app_state.detail_return_page = AppPage::nodes;
    if (app_state.refresh_timer) lv_timer_delete(app_state.refresh_timer);
    app_state.refresh_timer = nullptr;
    if (app_state.client) app_state.client->stop();
    if (app_state.map_view_model) app_state.map_view_model->set_active(false);

    if (app_state.active_view) app_state.active_view->destroy();
    app_state.active_view.reset();
    app_state.page = nullptr;
    if (app_state.surface) lv_obj_delete(app_state.surface);
    app_state.surface    = nullptr;
    app_state.content    = nullptr;
    app_state.navigation = nullptr;

    app_state.device_tools_view_model.reset();
    app_state.map_view_model.reset();
    app_state.node_detail_view_model.reset();
    app_state.chat_view_model.reset();
    app_state.channels_view_model.reset();
    app_state.nodes_view_model.reset();
    app_state.messages_view_model.reset();
    app_state.connection_view_model.reset();
    app_state.client.reset();
    app_state.snapshot                     = {};
    app_state.rendered_revision            = 0;
    app_state.last_radio_number = 0;
    app_state.page_scroll_y                = 0;
    app_state.layout_width                 = 0;
    app_state.applying_layout              = false;
    app_state.ignore_geometry_scroll_reset = false;
}

}  // namespace

bool lilygo_ui_meshtastic_layout_in_progress() noexcept
{
    return app_state.applying_layout;
}

void lilygo_ui_meshtastic_configure(std::string host, std::uint16_t port, bool auto_connect)
{
    configured_endpoint.host = std::move(host);
    configured_endpoint.port = port;
    configured_auto_connect  = auto_connect;
}

static const cm0_app_descriptor_t app_descriptor = {
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_MESHTASTIC_APP_ID, app_open, app_close,
};

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
