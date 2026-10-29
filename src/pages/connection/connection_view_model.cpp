#include "pages/connection/connection_view_model.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <ctime>

namespace {

const meshtastic::Node *find_node(const meshtastic::ClientSnapshot &snapshot, std::uint32_t number)
{
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [number](const meshtastic::Node &item) { return item.number == number; });
    return node == snapshot.nodes.end() ? nullptr : &*node;
}

std::string local_node_name(const meshtastic::ClientSnapshot &snapshot)
{
    if (snapshot.owner && !snapshot.owner->long_name().empty()) return snapshot.owner->long_name();
    if (snapshot.my_node_number == 0) return "Meshtastic radio";
    const auto *node = find_node(snapshot, snapshot.my_node_number);
    if (node && !node->long_name.empty()) return node->long_name;
    return "Meshtastic radio";
}

std::string local_node_id(const meshtastic::ClientSnapshot &snapshot)
{
    if (snapshot.owner && !snapshot.owner->id().empty()) return snapshot.owner->id();
    if (snapshot.my_node_number != 0) {
        char id[12]{};
        std::snprintf(id, sizeof(id), "!%08x", snapshot.my_node_number);
        return id;
    }
    return "Unavailable";
}

std::string role_name(const meshtastic::ClientSnapshot &snapshot)
{
    if (const auto *node = find_node(snapshot, snapshot.my_node_number); node && node->role != 0) {
        return meshtastic::Config_DeviceConfig_Role_Name(
            static_cast<meshtastic::Config_DeviceConfig_Role>(node->role));
    }
    if (snapshot.device_config) {
        return meshtastic::Config_DeviceConfig_Role_Name(snapshot.device_config->role());
    }
    if (snapshot.config && snapshot.config->has_device()) {
        return meshtastic::Config_DeviceConfig_Role_Name(snapshot.config->device().role());
    }
    if (snapshot.owner) return meshtastic::Config_DeviceConfig_Role_Name(snapshot.owner->role());
    if (snapshot.metadata) return meshtastic::Config_DeviceConfig_Role_Name(snapshot.metadata->role());
    return "CLIENT";
}

std::string lora_band_name(const meshtastic::ClientSnapshot &snapshot)
{
    const auto *lora = snapshot.lora_config ? &*snapshot.lora_config
                                             : (snapshot.config && snapshot.config->has_lora()
                                                    ? &snapshot.config->lora()
                                                    : nullptr);
    if (!lora) return "Unavailable";
    const auto region = meshtastic::Config_LoRaConfig_RegionCode_Name(lora->region());
    const auto modem  = meshtastic::Config_LoRaConfig_ModemPreset_Name(lora->modem_preset());
    if (region.empty()) return modem;
    return region + " / " + modem;
}

std::string format_uptime(std::uint64_t seconds)
{
    const auto days    = seconds / 86400;
    const auto hours   = (seconds / 3600) % 24;
    const auto minutes = (seconds / 60) % 60;
    const auto secs    = seconds % 60;
    if (days > 0) return std::to_string(days) + "d " + std::to_string(hours) + "h";
    if (hours > 0) return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    if (minutes > 0) return std::to_string(minutes) + "m " + std::to_string(secs) + "s";
    return std::to_string(secs) + "s";
}

}  // namespace

ConnectionViewModel::ConnectionViewModel(meshtastic::MeshtasticClient *client)
    : client_(client),
      brightness_(72, 0, 100),
      connected_(0, 0, 1),
      config_complete_(0, 0, 1),
      has_activity_(0, 0, 1),
      message_count_(0, 0),
      node_count_(0, 0),
      action_("Connect"),
      auto_reconnect_("AUTO-RECONNECT OFF"),
      config_status_("Not synchronized"),
      device_detail_("Waiting for meshtasticd over TCP"),
      device_name_("Meshtastic radio"),
      device_role_("CLIENT"),
      firmware_("Unavailable"),
      primary_channel_("Waiting for channel config"),
      tcp_state_("Stopped"),
      uptime_("Unavailable"),
      online_nodes_("0"),
      lora_band_("Unavailable"),
      node_id_("Unavailable")
{
}

void ConnectionViewModel::set_client(meshtastic::MeshtasticClient *client) noexcept
{
    client_ = client;
}

void ConnectionViewModel::sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled)
{
    const bool is_connected = snapshot.state == meshtastic::ConnectionState::connected;
    connected_.set(is_connected ? 1 : 0);
    config_complete_.set(snapshot.config_complete ? 1 : 0);
    message_count_.set(snapshot.local_stats ? static_cast<std::int32_t>(snapshot.local_stats->num_packets_rx()) : 0);
    node_count_.set(static_cast<std::int32_t>(snapshot.nodes.size()));

    endpoint_.set(snapshot.endpoint.c_str());
    const auto *state_name = meshtastic::connection_state_name(snapshot.state);
    tcp_state_.set(state_name);
    const auto status = std::string(state_name) + "  |  " + snapshot.endpoint;
    status_.set(status.c_str());
    action_.set(connection_enabled ? "Disconnect" : "Connect");
    auto_reconnect_.set(connection_enabled ? "AUTO-RECONNECT ON" : "AUTO-RECONNECT OFF");
    config_status_.set(snapshot.config_complete ? "Synchronized" : "Not synchronized");
    device_name_.set(local_node_name(snapshot).c_str());
    device_role_.set(role_name(snapshot).c_str());
    firmware_.set(snapshot.metadata && !snapshot.metadata->firmware_version().empty()
                      ? snapshot.metadata->firmware_version().c_str()
                      : "Unavailable");
    node_id_.set(local_node_id(snapshot).c_str());
    lora_band_.set(lora_band_name(snapshot).c_str());

    // LocalStats is emitted by the connected Meshtastic device and defines
    // "online" as heard within the protocol's two-hour window.  Prefer that
    // authoritative count whenever the device has reported a node database;
    // the timestamp fallback is only for older firmware that does not expose
    // LocalStats yet.
    std::size_t online = 0;
    if (snapshot.local_stats &&
        (snapshot.local_stats->num_total_nodes() != 0 || snapshot.local_stats->num_online_nodes() != 0)) {
        online = snapshot.local_stats->num_online_nodes();
    } else {
        const auto now = static_cast<std::uint64_t>(std::time(nullptr));
        for (const auto &node : snapshot.nodes) {
            if (node.last_heard != 0 && now >= node.last_heard && now - node.last_heard <= 7200) ++online;
        }
    }
    online_nodes_.set(std::to_string(online).c_str());
    if (snapshot.local_stats && snapshot.local_stats->uptime_seconds() != 0) {
        uptime_.set(format_uptime(snapshot.local_stats->uptime_seconds()).c_str());
    } else {
        const auto *local = find_node(snapshot, snapshot.my_node_number);
        if (local && local->uptime_seconds && *local->uptime_seconds != 0) {
            uptime_.set(format_uptime(*local->uptime_seconds).c_str());
        } else {
            uptime_.set("Unavailable");
        }
    }

    std::string detail = is_connected ? "Configuration synchronized" : "Waiting for meshtasticd over TCP";
    if (!snapshot.error.empty()) detail = snapshot.error;
    if (!snapshot.storage_error.empty()) detail = "History storage: " + snapshot.storage_error;
    device_detail_.set(detail.c_str());

    const auto primary =
        snapshot.channels.empty() ? std::string("Waiting for channel config") : snapshot.channels.front().name;
    primary_channel_.set(primary.c_str());

    has_activity_.set(snapshot.messages.empty() ? 0 : 1);
    if (!snapshot.messages.empty()) {
        const auto &last = snapshot.messages.back();
        std::string sender;
        if (last.outgoing) {
            sender = "You";
        } else {
            const auto *node = find_node(snapshot, last.from);
            sender           = node && !node->long_name.empty() ? node->long_name : "Unknown node";
        }
        const auto title = "Last text from " + sender;
        last_activity_title_.set(title.c_str());
        last_activity_detail_.set(last.text.c_str());
    }
}

bool ConnectionViewModel::toggle_connection()
{
    if (!client_) return false;
    const bool enabled = !client_->enabled();
    client_->set_enabled(enabled);
    sync(client_->snapshot(), enabled);
    return true;
}

int ConnectionViewModel::brightness() const
{
    return brightness_.value();
}

bool ConnectionViewModel::connected() const
{
    return connected_.value() != 0;
}

#define SUBJECT_GETTER(name)                                     \
    lv_subject_t *ConnectionViewModel::name##_subject() noexcept \
    {                                                            \
        return name##_.get();                                    \
    }

SUBJECT_GETTER(brightness)
SUBJECT_GETTER(connected)
SUBJECT_GETTER(config_complete)
SUBJECT_GETTER(has_activity)
SUBJECT_GETTER(message_count)
SUBJECT_GETTER(node_count)
SUBJECT_GETTER(action)
SUBJECT_GETTER(auto_reconnect)
SUBJECT_GETTER(config_status)
SUBJECT_GETTER(device_detail)
SUBJECT_GETTER(device_name)
SUBJECT_GETTER(device_role)
SUBJECT_GETTER(firmware)
SUBJECT_GETTER(endpoint)
SUBJECT_GETTER(last_activity_detail)
SUBJECT_GETTER(last_activity_title)
SUBJECT_GETTER(primary_channel)
SUBJECT_GETTER(status)
SUBJECT_GETTER(tcp_state)
SUBJECT_GETTER(uptime)
SUBJECT_GETTER(online_nodes)
SUBJECT_GETTER(lora_band)
SUBJECT_GETTER(node_id)

#undef SUBJECT_GETTER
