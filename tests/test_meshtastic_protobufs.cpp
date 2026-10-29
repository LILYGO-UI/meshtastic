#include <meshtastic/admin.pb.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

template <typename Message>
std::vector<std::uint8_t> serialize(const Message &message)
{
    std::vector<std::uint8_t> bytes(message.ByteSizeLong());
    assert(message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size())));
    return bytes;
}

void test_admin_lora_config_round_trip()
{
    meshtastic::AdminMessage request;
    request.set_session_passkey("session-key");
    auto *config = request.mutable_set_config();
    config->mutable_lora()->set_region(meshtastic::Config::LoRaConfig::CN);

    const auto bytes = serialize(request);
    meshtastic::AdminMessage decoded;
    assert(decoded.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())));
    assert(decoded.session_passkey() == "session-key");
    assert(decoded.payload_variant_case() == meshtastic::AdminMessage::kSetConfig);
    assert(decoded.set_config().payload_variant_case() == meshtastic::Config::kLora);
    assert(decoded.set_config().lora().region() == meshtastic::Config::LoRaConfig::CN);
}

void test_admin_device_config_and_owner_round_trip()
{
    meshtastic::AdminMessage config_request;
    config_request.set_session_passkey("session-key");
    config_request.mutable_set_config()->mutable_device()->set_role(meshtastic::Config::DeviceConfig::ROUTER);

    const auto config_bytes = serialize(config_request);
    meshtastic::AdminMessage decoded_config;
    assert(decoded_config.ParseFromArray(config_bytes.data(), static_cast<int>(config_bytes.size())));
    assert(decoded_config.set_config().payload_variant_case() == meshtastic::Config::kDevice);
    assert(decoded_config.set_config().device().role() == meshtastic::Config::DeviceConfig::ROUTER);

    meshtastic::AdminMessage owner_request;
    owner_request.set_session_passkey("session-key");
    owner_request.mutable_set_owner()->set_id("!12345678");
    owner_request.mutable_set_owner()->set_long_name("Field Radio");
    owner_request.mutable_set_owner()->set_role(meshtastic::Config::DeviceConfig::ROUTER);

    const auto owner_bytes = serialize(owner_request);
    meshtastic::AdminMessage decoded_owner;
    assert(decoded_owner.ParseFromArray(owner_bytes.data(), static_cast<int>(owner_bytes.size())));
    assert(decoded_owner.payload_variant_case() == meshtastic::AdminMessage::kSetOwner);
    assert(decoded_owner.set_owner().id() == "!12345678");
    assert(decoded_owner.set_owner().long_name() == "Field Radio");
    assert(decoded_owner.set_owner().role() == meshtastic::Config::DeviceConfig::ROUTER);
}

}  // namespace

int main()
{
    test_admin_lora_config_round_trip();
    test_admin_device_config_and_owner_round_trip();
    return 0;
}
