#include "app_router.hpp"

#include <array>
#include <cstddef>

AppPage AppRouter::page() const noexcept
{
    return page_;
}

bool AppRouter::navigate(AppPage page) noexcept
{
    if (page < AppPage::messages || page >= AppPage::count || page == page_) return false;
    page_ = page;
    return true;
}

bool AppRouter::back() noexcept
{
    switch (page_) {
        case AppPage::channels:
        case AppPage::chat:
            page_ = AppPage::messages;
            return true;
        case AppPage::node_detail:
            page_ = AppPage::nodes;
            return true;
        case AppPage::device_tools:
            page_ = AppPage::connect;
            return true;
        default:
            return false;
    }
}

bool AppRouter::page_has_navigation(AppPage page) noexcept
{
    return page >= AppPage::messages && page <= AppPage::channels;
}

const char *AppRouter::page_name(AppPage page) noexcept
{
    static constexpr std::array names{
        "Messages", "Nodes", "Map", "Connection", "Channels", "LongFast", "Node", "Device Tools",
    };
    const auto index = static_cast<std::size_t>(page);
    return index < names.size() ? names[index] : "Meshtastic";
}
