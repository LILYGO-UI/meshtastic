#include "domain/meshtastic_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

#include <meshtastic/mesh.pb.h>

namespace meshtastic {
const char *message_status_name(MessageStatus status) noexcept
{
    switch (status) {
        case MessageStatus::received: return "Received";
        case MessageStatus::queued: return "Queued";
        case MessageStatus::sent: return "Sent; awaiting ACK";
        case MessageStatus::acknowledged: return "Acknowledged";
        case MessageStatus::rejected: return "Rejected";
        case MessageStatus::timed_out: return "ACK timed out";
        case MessageStatus::failed: return "Failed";
    }
    return "Unknown";
}
namespace {

constexpr std::array<std::uint8_t, 2> kFrameMagic{kFrameStart1, kFrameStart2};
static_assert(kMaxTextPayload == DATA_PAYLOAD_LEN);
static_assert(kTextMessagePort == TEXT_MESSAGE_APP);

std::vector<std::uint8_t> serialize_to_radio(const ToRadio &message)
{
    const auto size = message.ByteSizeLong();
    if (size > kMaxRadioPayload || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};

    std::vector<std::uint8_t> payload(size);
    if (!message.SerializeToArray(payload.data(), static_cast<int>(payload.size()))) return {};
    return payload;
}

Node convert_node(const NodeInfo &source)
{
    Node node;
    node.number = source.num();

    if (source.has_user()) {
        node.id         = source.user().id();
        node.long_name  = source.user().long_name();
        node.short_name = source.user().short_name();
        node.role       = static_cast<std::uint32_t>(source.user().role());
    }
    if (source.has_position()) {
        const auto &position = source.position();
        if (position.has_latitude_i()) node.latitude = static_cast<double>(position.latitude_i()) * 1e-7;
        if (position.has_longitude_i()) node.longitude = static_cast<double>(position.longitude_i()) * 1e-7;
        if (position.has_altitude()) node.altitude = position.altitude();
    }
    if (source.has_device_metrics()) {
        const auto &metrics = source.device_metrics();
        if (metrics.has_battery_level()) node.battery_level = metrics.battery_level();
        if (metrics.has_voltage()) node.voltage = metrics.voltage();
        if (metrics.has_uptime_seconds()) node.uptime_seconds = metrics.uptime_seconds();
    }
    if (source.has_hops_away()) node.hops_away = source.hops_away();
    node.via_mqtt = source.via_mqtt();

    node.last_heard = source.last_heard();
    node.snr        = source.snr();
    if (node.id.empty() && node.number != 0) {
        char id[10]{};
        std::snprintf(id, sizeof(id), "!%08x", node.number);
        node.id = id;
    }
    if (node.long_name.empty()) node.long_name = node.id.empty() ? "Unknown node" : node.id;
    return node;
}

ChannelInfo convert_channel(const Channel &source)
{
    ChannelInfo channel;
    channel.index = source.index();
    channel.role  = static_cast<std::uint32_t>(source.role());
    if (source.has_settings()) {
        channel.name      = source.settings().name();
        const auto &psk   = source.settings().psk();
        channel.encrypted = !psk.empty() && !(psk.size() == 1 && psk.front() == '\0');
    }
    if (channel.name.empty())
        channel.name = channel.index == 0 ? "LongFast" : "Channel " + std::to_string(channel.index);
    return channel;
}

std::optional<Message> convert_text_message(const MeshPacket &packet)
{
    if (!packet.has_decoded() || packet.decoded().portnum() != TEXT_MESSAGE_APP) return std::nullopt;

    Message message;
    message.from        = packet.from();
    message.to          = packet.to();
    message.id          = packet.id();
    message.channel     = packet.channel();
    message.received_at = packet.has_rx_time() ? packet.rx_time() : 0;
    message.rssi        = packet.has_rx_rssi() ? packet.rx_rssi() : 0;
    message.snr         = packet.rx_snr();
    message.text        = packet.decoded().payload();
    return message;
}

}  // namespace

std::vector<std::vector<std::uint8_t>> FrameParser::feed(const std::uint8_t *data, std::size_t size)
{
    if (data && size) buffer_.insert(buffer_.end(), data, data + size);

    std::vector<std::vector<std::uint8_t>> frames;
    while (true) {
        const auto start = std::search(buffer_.begin(), buffer_.end(), kFrameMagic.begin(), kFrameMagic.end());
        if (start == buffer_.end()) {
            const bool keep_start = !buffer_.empty() && buffer_.back() == kFrameStart1;
            buffer_.assign(keep_start ? 1U : 0U, kFrameStart1);
            break;
        }
        buffer_.erase(buffer_.begin(), start);
        if (buffer_.size() < 4) break;
        const auto payload_size = (static_cast<std::size_t>(buffer_[2]) << 8U) | buffer_[3];
        if (payload_size > kMaxRadioPayload) {
            buffer_.erase(buffer_.begin());
            continue;
        }
        if (buffer_.size() < payload_size + 4) break;
        frames.emplace_back(buffer_.begin() + 4, buffer_.begin() + 4 + payload_size);
        buffer_.erase(buffer_.begin(), buffer_.begin() + 4 + payload_size);
    }
    return frames;
}

void FrameParser::reset()
{
    buffer_.clear();
}

std::vector<std::uint8_t> frame_payload(const std::vector<std::uint8_t> &payload)
{
    if (payload.size() > kMaxRadioPayload) return {};
    std::vector<std::uint8_t> frame{kFrameStart1, kFrameStart2, static_cast<std::uint8_t>(payload.size() >> 8U),
                                    static_cast<std::uint8_t>(payload.size())};
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<std::uint8_t> encode_to_radio(const ToRadio &message)
{
    return serialize_to_radio(message);
}

bool decode_to_radio(const std::vector<std::uint8_t> &payload, ToRadio &message, std::string &error)
{
    message.Clear();
    if (payload.size() > kMaxRadioPayload ||
        payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "ToRadio protobuf exceeds the transport limit";
        return false;
    }
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        error = "invalid ToRadio protobuf";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_want_config(std::uint32_t config_id)
{
    ToRadio message;
    message.set_want_config_id(config_id);
    return serialize_to_radio(message);
}

std::vector<std::uint8_t> encode_heartbeat(std::uint32_t nonce)
{
    ToRadio message;
    message.mutable_heartbeat()->set_nonce(nonce);
    return serialize_to_radio(message);
}

std::vector<std::uint8_t> encode_text_message(const std::string &text, std::uint32_t packet_id, std::uint32_t channel,
                                              std::uint32_t hop_limit)
{
    if (text.empty() || text.size() > kMaxTextPayload || packet_id == 0) return {};

    ToRadio message;
    auto *packet = message.mutable_packet();
    packet->set_to(kBroadcastNode);
    packet->set_channel(channel);
    packet->set_id(packet_id);
    packet->set_hop_limit(hop_limit);
    packet->set_priority(MeshPacket::RELIABLE);
    packet->mutable_decoded()->set_portnum(TEXT_MESSAGE_APP);
    packet->mutable_decoded()->set_payload(text);
    return serialize_to_radio(message);
}

std::vector<std::uint8_t> encode_packet(const MeshPacket &packet)
{
    ToRadio message;
    message.mutable_packet()->CopyFrom(packet);
    return serialize_to_radio(message);
}

std::vector<std::uint8_t> encode_data_message(PortNum port, const std::string &payload,
                                              std::uint32_t destination, std::uint32_t packet_id,
                                              std::uint32_t channel, std::uint32_t hop_limit,
                                              MeshPacket::Priority priority)
{
    if (payload.size() > kMaxRadioPayload) return {};

    MeshPacket packet;
    packet.set_to(destination);
    packet.set_channel(channel);
    packet.set_id(packet_id);
    packet.set_hop_limit(hop_limit);
    packet.set_priority(priority);
    packet.mutable_decoded()->set_portnum(port);
    packet.mutable_decoded()->set_payload(payload);
    return encode_packet(packet);
}

std::vector<std::uint8_t> encode_admin_message(const AdminMessage &message, std::uint32_t destination,
                                               std::uint32_t packet_id, std::uint32_t channel,
                                               std::uint32_t hop_limit)
{
    const auto size = message.ByteSizeLong();
    if (size == 0 || size > kMaxRadioPayload || size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return {};
    std::string payload(size, '\0');
    if (!message.SerializeToArray(payload.data(), static_cast<int>(payload.size()))) return {};
    return encode_data_message(ADMIN_APP, payload, destination, packet_id, channel, hop_limit,
                               MeshPacket::RELIABLE);
}

namespace {

void set_admin_session_passkey(AdminMessage &message, const std::string &session_passkey)
{
    if (!session_passkey.empty()) message.set_session_passkey(session_passkey);
}

}  // namespace

std::vector<std::uint8_t> encode_admin_get_channel(std::uint32_t index, const std::string &session_passkey)
{
    if (index == std::numeric_limits<std::uint32_t>::max()) return {};
    AdminMessage message;
    message.set_get_channel_request(index + 1U);
    set_admin_session_passkey(message, session_passkey);
    return encode_admin_message(message);
}

std::vector<std::uint8_t> encode_admin_get_config(AdminMessage::ConfigType type,
                                                  const std::string &session_passkey)
{
    AdminMessage message;
    message.set_get_config_request(type);
    set_admin_session_passkey(message, session_passkey);
    return encode_admin_message(message);
}

std::vector<std::uint8_t> encode_admin_get_module_config(AdminMessage::ModuleConfigType type,
                                                         const std::string &session_passkey)
{
    AdminMessage message;
    message.set_get_module_config_request(type);
    set_admin_session_passkey(message, session_passkey);
    return encode_admin_message(message);
}

std::vector<std::uint8_t> encode_admin_set_config(const Config &config, const std::string &session_passkey)
{
    AdminMessage message;
    message.mutable_set_config()->CopyFrom(config);
    set_admin_session_passkey(message, session_passkey);
    return encode_admin_message(message);
}

std::vector<std::uint8_t> encode_admin_set_module_config(const ModuleConfig &config,
                                                         const std::string &session_passkey)
{
    AdminMessage message;
    message.mutable_set_module_config()->CopyFrom(config);
    set_admin_session_passkey(message, session_passkey);
    return encode_admin_message(message);
}

bool decode_from_radio(const std::vector<std::uint8_t> &payload, RadioUpdate &message, std::string &error)
{
    message = {};
    if (payload.size() > kMaxRadioPayload ||
        payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "FromRadio protobuf exceeds the transport limit";
        return false;
    }

    FromRadio decoded;
    if (!decoded.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        error = "invalid FromRadio protobuf";
        return false;
    }
    message.raw = decoded;

    switch (decoded.payload_variant_case()) {
        case FromRadio::kPacket: {
            PacketUpdate packet;
            packet.packet = decoded.packet();
            message.text_message = convert_text_message(decoded.packet());
            if (decoded.packet().has_decoded() && decoded.packet().decoded().portnum() == ADMIN_APP) {
                AdminMessage admin;
                const auto &bytes = decoded.packet().decoded().payload();
                if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                    !admin.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
                    error = "invalid AdminMessage payload";
                    return false;
                }
                packet.admin = std::move(admin);
            }
            if (decoded.packet().has_decoded()) {
                const auto &data = decoded.packet().decoded();
                const auto &bytes = data.payload();
                bool valid = true;
                switch (data.portnum()) {
                    case ROUTING_APP: { Routing value; valid = value.ParseFromString(bytes); if (valid) packet.routing = value; break; }
                    case POSITION_APP: { Position value; valid = value.ParseFromString(bytes); if (valid) packet.position = value; break; }
                    case TELEMETRY_APP: {
                        Telemetry value; valid = value.ParseFromString(bytes);
                        if (valid) {
                            packet.telemetry = value;
                            if (value.has_local_stats()) message.local_stats = value.local_stats();
                        }
                        break;
                    }
                    case WAYPOINT_APP: { Waypoint value; valid = value.ParseFromString(bytes); if (valid) packet.waypoint = value; break; }
                    case TRACEROUTE_APP: { RouteDiscovery value; valid = value.ParseFromString(bytes); if (valid) packet.route = value; break; }
                    default: break;
                }
                if (!valid) { error = "invalid typed Meshtastic packet payload"; return false; }
            }
            message.packet = std::move(packet);
            break;
        }
        case FromRadio::kMyInfo:
            message.my_info         = decoded.my_info();
            message.my_node_number = decoded.my_info().my_node_num();
            break;
        case FromRadio::kNodeInfo:
            message.node = convert_node(decoded.node_info());
            break;
        case FromRadio::kConfigCompleteId:
            message.config_complete_id = decoded.config_complete_id();
            break;
        case FromRadio::kConfig:
            message.config = decoded.config();
            break;
        case FromRadio::kModuleConfig:
            message.module_config = decoded.moduleconfig();
            break;
        case FromRadio::kMetadata:
            message.metadata = decoded.metadata();
            break;
        case FromRadio::kRebooted:
            message.rebooted = decoded.rebooted();
            break;
        case FromRadio::kChannel:
            message.channel = convert_channel(decoded.channel());
            break;
        default:
            break;
    }

    error.clear();
    return true;
}

}  // namespace meshtastic
