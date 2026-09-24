#ifndef LILYGO_UI_MESHTASTIC_PROTOCOL_HPP
#define LILYGO_UI_MESHTASTIC_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace meshtastic {

constexpr std::uint8_t kFrameStart1      = 0x94;
constexpr std::uint8_t kFrameStart2      = 0xc3;
constexpr std::size_t kMaxRadioPayload   = 512;
constexpr std::size_t kMaxTextPayload    = 233;
constexpr std::uint32_t kBroadcastNode   = 0xffffffffU;
constexpr std::uint32_t kTextMessagePort = 1;

struct Node {
    std::uint32_t number = 0;
    std::string id;
    std::string long_name;
    std::string short_name;
    std::uint32_t last_heard = 0;
    float snr                = 0.0F;
    std::optional<std::uint32_t> battery_level;
    std::optional<float> voltage;
    std::optional<std::uint32_t> hops_away;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<std::int32_t> altitude;
};

struct ChannelInfo {
    std::int32_t index = 0;
    std::string name;
    std::uint32_t role = 0;
    bool encrypted     = false;
};

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
};

struct RadioUpdate {
    std::optional<std::uint32_t> my_node_number;
    std::optional<Node> node;
    std::optional<ChannelInfo> channel;
    std::optional<Message> text_message;
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
std::vector<std::uint8_t> encode_want_config(std::uint32_t config_id);
std::vector<std::uint8_t> encode_text_message(const std::string &text, std::uint32_t packet_id,
                                              std::uint32_t channel = 0, std::uint32_t hop_limit = 3);
bool decode_from_radio(const std::vector<std::uint8_t> &payload, RadioUpdate &message, std::string &error);

}  // namespace meshtastic

#endif
