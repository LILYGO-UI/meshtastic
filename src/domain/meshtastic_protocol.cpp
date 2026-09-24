#include "domain/meshtastic_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

#include <meshtastic/mesh.pb.h>

namespace meshtastic {
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
    }
    if (source.has_hops_away()) node.hops_away = source.hops_away();

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

std::vector<std::uint8_t> encode_want_config(std::uint32_t config_id)
{
    ToRadio message;
    message.set_want_config_id(config_id);
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

    switch (decoded.payload_variant_case()) {
        case FromRadio::kPacket:
            message.text_message = convert_text_message(decoded.packet());
            break;
        case FromRadio::kMyInfo:
            message.my_node_number = decoded.my_info().my_node_num();
            break;
        case FromRadio::kNodeInfo:
            message.node = convert_node(decoded.node_info());
            break;
        case FromRadio::kConfigCompleteId:
            message.config_complete_id = decoded.config_complete_id();
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
