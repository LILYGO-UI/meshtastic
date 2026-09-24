#include "pages/device_tools/device_tools_view_model.hpp"

DeviceToolsViewModel::DeviceToolsViewModel()
    : connected_(0, 0, 1),
      auto_reconnect_("Off"),
      config_("Pending"),
      detail_("Waiting for meshtasticd"),
      endpoint_("Not configured"),
      state_("Stopped")
{
}

void DeviceToolsViewModel::sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled)
{
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    auto_reconnect_.set(connection_enabled ? "On" : "Off");
    config_.set(snapshot.config_complete ? "Complete" : "Pending");
    detail_.set(snapshot.error.empty() ? snapshot.endpoint.c_str() : snapshot.error.c_str());
    endpoint_.set(snapshot.endpoint.c_str());
    state_.set(meshtastic::connection_state_name(snapshot.state));
}

#define SUBJECT_GETTER(name)                                      \
    lv_subject_t *DeviceToolsViewModel::name##_subject() noexcept \
    {                                                             \
        return name##_.get();                                     \
    }

SUBJECT_GETTER(connected)
SUBJECT_GETTER(auto_reconnect)
SUBJECT_GETTER(config)
SUBJECT_GETTER(detail)
SUBJECT_GETTER(endpoint)
SUBJECT_GETTER(state)

#undef SUBJECT_GETTER
