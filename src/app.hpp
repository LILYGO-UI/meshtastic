#ifndef LILYGO_UI_MESHTASTIC_APP_HPP
#define LILYGO_UI_MESHTASTIC_APP_HPP

#include <cm0/app.h>

#include <cstdint>
#include <string>

void lilygo_ui_meshtastic_configure(std::string host, std::uint16_t port, bool auto_connect = true);
bool lilygo_ui_meshtastic_layout_in_progress() noexcept;
extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor();

#endif
