#ifndef LILYGO_UI_MESHTASTIC_APP_ROUTER_HPP
#define LILYGO_UI_MESHTASTIC_APP_ROUTER_HPP

#include <cstdint>

enum class AppPage {
    messages = 0,
    nodes,
    map,
    connect,
    channels,
    chat,
    node_detail,
    device_tools,
    count,
};

struct AppNavigationCommands {
    using Navigate = void (*)(void *context, AppPage page);
    using Back     = void (*)(void *context);
    using DirectChat = void (*)(void *context, std::uint32_t peer, const char *name);
    using NodeAction = void (*)(void *context, std::uint32_t node);

    void *context       = nullptr;
    Navigate navigate   = nullptr;
    Back back           = nullptr;
    DirectChat direct_chat = nullptr;
    NodeAction node_detail = nullptr;
    NodeAction map_node = nullptr;

    void request(AppPage page) const
    {
        if (navigate) navigate(context, page);
    }

    void request_back() const
    {
        if (back) back(context);
    }

    void request_direct_chat(std::uint32_t peer, const char *name) const
    {
        if (direct_chat) {
            direct_chat(context, peer, name);
        } else {
            request(AppPage::chat);
        }
    }

    void request_node_detail(std::uint32_t node) const
    {
        if (node_detail) node_detail(context, node);
        else request(AppPage::node_detail);
    }

    void request_map_node(std::uint32_t node) const
    {
        if (map_node) map_node(context, node);
        else request(AppPage::map);
    }
};

class AppRouter {
public:
    [[nodiscard]] AppPage page() const noexcept;

    bool navigate(AppPage page) noexcept;
    bool back() noexcept;

    [[nodiscard]] static bool page_has_navigation(AppPage page) noexcept;
    [[nodiscard]] static const char *page_name(AppPage page) noexcept;

private:
    AppPage page_ = AppPage::connect;
};

#endif
