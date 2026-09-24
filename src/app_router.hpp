#ifndef LILYGO_UI_MESHTASTIC_APP_ROUTER_HPP
#define LILYGO_UI_MESHTASTIC_APP_ROUTER_HPP

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

    void *context     = nullptr;
    Navigate navigate = nullptr;
    Back back         = nullptr;

    void request(AppPage page) const
    {
        if (navigate) navigate(context, page);
    }

    void request_back() const
    {
        if (back) back(context);
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
