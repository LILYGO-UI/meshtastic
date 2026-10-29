#ifndef LILYGO_UI_MESHTASTIC_PROTOCOL_HPP
#define LILYGO_UI_MESHTASTIC_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <meshtastic/admin.pb.h>
#include <meshtastic/config.pb.h>
#include <meshtastic/mesh.pb.h>
#include <meshtastic/module_config.pb.h>
#include <meshtastic/telemetry.pb.h>

namespace meshtastic {

constexpr std::uint8_t kFrameStart1      = 0x94;
constexpr std::uint8_t kFrameStart2      = 0xc3;
constexpr std::size_t kMaxRadioPayload   = 512;
constexpr std::size_t kMaxTextPayload    = 233;
constexpr std::uint32_t kBroadcastNode    = 0xffffffffU;
// A zero destination lets the local Client API endpoint resolve the connected
// node. It is useful for local AdminMessage requests.
constexpr std::uint32_t kLocalNode        = 0;
constexpr std::uint32_t kTextMessagePort  = 1;
constexpr std::uint32_t kAdminMessagePort = 6;

struct Node {
    std::uint32_t number = 0;
    std::string id;
    std::string long_name;
    std::string short_name;
    // Config.DeviceConfig.Role is kept as an integer at the app boundary so
    // UI code does not depend on generated enum names.
    std::uint32_t role = 0;
    std::uint32_t last_heard = 0;
    float snr                = 0.0F;
    std::optional<std::uint32_t> battery_level;
    std::optional<float> voltage;
    std::optional<std::uint32_t> uptime_seconds;
    std::optional<std::uint32_t> hops_away;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<std::int32_t> altitude;
    bool via_mqtt = false;
};

struct ChannelInfo {
    std::int32_t index = 0;
    std::string name;
    std::uint32_t role = 0;
    bool encrypted     = false;
};

enum class MessageStatus { received, queued, sent, acknowledged, rejected, timed_out, failed };
const char *message_status_name(MessageStatus status) noexcept;

struct Message {
    std::uint32_t from        = 0;
    std::uint32_t to          = 0;
    std::uint32_t id          = 0;
    std::uint32_t channel     = 0;
    std::uint32_t received_at = 0;
    std::int32_t rssi         = 0;
    float snr                 = 0.0F;
    std::string text;
    bool outgoing = false;
    std::int64_t local_id = 0;
    MessageStatus status = MessageStatus::received;
    std::string status_detail;
    std::uint32_t attempts = 0;
    bool read = false;
};

struct Conversation {
    bool direct = false;
    std::uint32_t peer = 0;
    std::uint32_t channel = 0;
    Message latest;
    std::size_t unread_count = 0;
};

struct DeliveryRecord {
    std::uint32_t packet_id = 0;
    MessageStatus status = MessageStatus::queued;
    std::string detail;
    std::uint32_t sent_at = 0;
    std::uint32_t attempts = 0;
    int portnum = 0;
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    std::uint32_t channel = 0;
};

struct PositionRecord {
    std::int64_t local_id = 0;
    std::uint32_t node_number = 0, packet_id = 0, received_at = 0;
    Position position;
};
struct TelemetryRecord {
    std::int64_t local_id = 0;
    std::uint32_t node_number = 0, packet_id = 0, received_at = 0;
    Telemetry telemetry;
};
struct WaypointRecord {
    std::int64_t local_id = 0;
    std::uint32_t sender = 0, packet_id = 0, received_at = 0;
    Waypoint waypoint;
    bool outgoing = false;
};
struct RouteRecord {
    std::int64_t local_id = 0;
    std::uint32_t from = 0, to = 0, packet_id = 0, request_id = 0, received_at = 0;
    RouteDiscovery route;
    bool outgoing = false;
};

// A decoded packet is retained alongside its convenient high-level view. This
// keeps the Client API lossless for applications that need protocol fields not
// represented by Message (routing, acknowledgements, telemetry, and so on).
struct PacketUpdate {
    MeshPacket packet;
    std::optional<AdminMessage> admin;
    std::optional<Routing> routing;
    std::optional<Position> position;
    std::optional<Telemetry> telemetry;
    std::optional<Waypoint> waypoint;
    std::optional<RouteDiscovery> route;
};

struct RadioUpdate {
    // Complete FromRadio envelope for callers that need a variant not yet
    // projected into the convenience fields below.
    std::optional<FromRadio> raw;
    std::optional<std::uint32_t> my_node_number;
    std::optional<MyNodeInfo> my_info;
    std::optional<Node> node;
    std::optional<ChannelInfo> channel;
    std::optional<Message> text_message;
    std::optional<PacketUpdate> packet;
    std::optional<Config> config;
    std::optional<ModuleConfig> module_config;
    std::optional<DeviceMetadata> metadata;
    std::optional<LocalStats> local_stats;
    std::optional<std::uint32_t> config_complete_id;
    bool rebooted = false;
};

class FrameParser {
public:
    std::vector<std::vector<std::uint8_t>> feed(const std::uint8_t *data, std::size_t size);
    void reset();

private:
    std::vector<std::uint8_t> buffer_;
};

std::vector<std::uint8_t> frame_payload(const std::vector<std::uint8_t> &payload);
std::vector<std::uint8_t> encode_to_radio(const ToRadio &message);
bool decode_to_radio(const std::vector<std::uint8_t> &payload, ToRadio &message, std::string &error);
std::vector<std::uint8_t> encode_want_config(std::uint32_t config_id);
std::vector<std::uint8_t> encode_heartbeat(std::uint32_t nonce);
std::vector<std::uint8_t> encode_text_message(const std::string &text, std::uint32_t packet_id,
                                              std::uint32_t channel = 0, std::uint32_t hop_limit = 3);
std::vector<std::uint8_t> encode_packet(const MeshPacket &packet);
std::vector<std::uint8_t> encode_data_message(PortNum port, const std::string &payload,
                                              std::uint32_t destination = kBroadcastNode,
                                              std::uint32_t packet_id = 0, std::uint32_t channel = 0,
                                              std::uint32_t hop_limit = 3,
                                              MeshPacket::Priority priority = MeshPacket::RELIABLE);
std::vector<std::uint8_t> encode_admin_message(const AdminMessage &message,
                                               std::uint32_t destination = kBroadcastNode,
                                               std::uint32_t packet_id = 0, std::uint32_t channel = 0,
                                               std::uint32_t hop_limit = 3);
std::vector<std::uint8_t> encode_admin_get_channel(std::uint32_t index,
                                                   const std::string &session_passkey = {});
std::vector<std::uint8_t> encode_admin_get_config(AdminMessage::ConfigType type,
                                                  const std::string &session_passkey = {});
std::vector<std::uint8_t> encode_admin_get_module_config(AdminMessage::ModuleConfigType type,
                                                         const std::string &session_passkey = {});
std::vector<std::uint8_t> encode_admin_set_config(const Config &config, const std::string &session_passkey = {});
std::vector<std::uint8_t> encode_admin_set_module_config(const ModuleConfig &config,
                                                         const std::string &session_passkey = {});
bool decode_from_radio(const std::vector<std::uint8_t> &payload, RadioUpdate &message, std::string &error);

}  // namespace meshtastic

#endif
