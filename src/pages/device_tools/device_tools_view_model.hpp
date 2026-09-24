#ifndef LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

class DeviceToolsViewModel {
public:
    DeviceToolsViewModel();

    DeviceToolsViewModel(const DeviceToolsViewModel &)            = delete;
    DeviceToolsViewModel &operator=(const DeviceToolsViewModel &) = delete;
    DeviceToolsViewModel(DeviceToolsViewModel &&)                 = delete;
    DeviceToolsViewModel &operator=(DeviceToolsViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled);

    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *auto_reconnect_subject() noexcept;
    [[nodiscard]] lv_subject_t *config_subject() noexcept;
    [[nodiscard]] lv_subject_t *detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *endpoint_subject() noexcept;
    [[nodiscard]] lv_subject_t *state_subject() noexcept;

private:
    IntSubject connected_;
    StringSubject<16> auto_reconnect_;
    StringSubject<16> config_;
    StringSubject<256> detail_;
    StringSubject<128> endpoint_;
    StringSubject<40> state_;
};

#endif
