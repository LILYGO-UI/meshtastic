#include "pages/connection/connection_view_model.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace {

const meshtastic::Node *find_node(const meshtastic::ClientSnapshot &snapshot, std::uint32_t number)
{
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [number](const meshtastic::Node &item) { return item.number == number; });
    return node == snapshot.nodes.end() ? nullptr : &*node;
}

std::string local_node_name(const meshtastic::ClientSnapshot &snapshot)
{
    if (snapshot.my_node_number == 0) return "Meshtastic radio";
    const auto *node = find_node(snapshot, snapshot.my_node_number);
    if (node && !node->long_name.empty()) return node->long_name;
    return "Meshtastic radio";
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
      primary_channel_("Waiting for channel config"),
      tcp_state_("Stopped")
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
    message_count_.set(static_cast<std::int32_t>(snapshot.messages.size()));
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

    std::string detail = is_connected ? "Configuration synchronized" : "Waiting for meshtasticd over TCP";
    if (!snapshot.error.empty()) detail = snapshot.error;
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
SUBJECT_GETTER(endpoint)
SUBJECT_GETTER(last_activity_detail)
SUBJECT_GETTER(last_activity_title)
SUBJECT_GETTER(primary_channel)
SUBJECT_GETTER(status)
SUBJECT_GETTER(tcp_state)

#undef SUBJECT_GETTER
