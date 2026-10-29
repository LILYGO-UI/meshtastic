#include "pages/channels/channels_view_model.hpp"
#include "admin_test_radio.hpp"
#include "domain/meshtastic_protocol.hpp"
#include "persistence/meshtastic_repository.hpp"
#include <lvgl.h>
#include <cassert>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {
meshtastic::ClientSnapshot snapshot()
{
    meshtastic::ClientSnapshot value;
    value.state = meshtastic::ConnectionState::connected;
    value.config_complete = true;
    value.session_passkey = "session";
    for (int index = 0; index < 8; ++index) {
        auto &channel = value.channel_configs[index];
        channel.set_index(index);
        channel.set_role(index == 0 ? meshtastic::Channel::PRIMARY : meshtastic::Channel::DISABLED);
        channel.mutable_settings()->set_name(index == 0 ? "Primary" : "");
        channel.mutable_settings()->set_psk(std::string(16, static_cast<char>(index + 1)));
        channel.mutable_settings()->mutable_module_settings()->set_position_precision(14);
        value.channel_revisions[index] = 1;
    }
    value.device_configs[meshtastic::Config::kLora].mutable_lora()->set_region(meshtastic::Config::LoRaConfig::US);
    value.device_configs[meshtastic::Config::kLora].mutable_lora()->set_use_preset(true);
    value.device_config_revisions[meshtastic::Config::kLora] = 1;
    return value;
}
void test_key_validation()
{
    std::string key, error;
    assert(ChannelsViewModel::decode_key("AAECAwQFBgcICQoLDA0ODw==", key, error));
    assert(key.size() == 16);
    for (int index = 0; index < 16; ++index) assert(static_cast<unsigned char>(key[index]) == index);
    assert(ChannelsViewModel::decode_key("AAECAwQFBgcICQoLDA0ODw", key, error));
    assert(!ChannelsViewModel::decode_key("AAECAwQFBgcICQoLDA0ODx==", key, error));
    assert(!ChannelsViewModel::decode_key("private words", key, error));
    assert(!ChannelsViewModel::decode_key("AQ==", key, error));
    assert(!ChannelsViewModel::decode_key("", key, error));
}
void test_channel_edit_preserves_fields()
{
    auto state = snapshot();
    // Lite protobuf keeps future fields as opaque bytes; no reflection needed.
    const std::string unknown("\xa0\x1f\x7b", 3);
    state.channel_configs[1].mutable_unknown_fields()->append(unknown);
    state.channel_configs[1].mutable_settings()->mutable_unknown_fields()->append(unknown);
    ChannelsViewModel model;
    model.sync(state);
    assert(model.edit_channel(1));
    assert(model.stage_channel("Operations", meshtastic::Channel::SECONDARY,
        ChannelsViewModel::KeyMode::keep_existing, "", true, false));
    const auto *draft = model.draft_channel();
    assert(draft && draft->index() == 1);
    assert(draft->settings().name() == "Operations");
    assert(draft->settings().psk() == std::string(16, '\x02'));
    assert(draft->settings().module_settings().position_precision() == 14);
    assert(draft->unknown_fields() == unknown);
    assert(draft->settings().unknown_fields() == unknown);
    assert(draft->settings().uplink_enabled());
    assert(!draft->settings().downlink_enabled());
    assert(!model.stage_channel("TwelveBytes!", 2, ChannelsViewModel::KeyMode::keep_existing, "", false, false));
    assert(!model.stage_channel("Name\nbreak", 2, ChannelsViewModel::KeyMode::keep_existing, "", false, false));
    assert(!model.stage_channel("Valid", 99, ChannelsViewModel::KeyMode::keep_existing, "", false, false));
    assert(model.stage_channel("Valid", 2, ChannelsViewModel::KeyMode::public_default, "", false, true));
    assert(model.draft_channel()->settings().psk() == std::string(1, '\x01'));
    assert(model.stage_channel("Valid", 2, ChannelsViewModel::KeyMode::unencrypted, "", false, true));
    assert(model.draft_channel()->settings().psk().empty());
    assert(model.stage_channel("Valid", 2, ChannelsViewModel::KeyMode::custom_base64, "AAECAwQFBgcICQoLDA0ODw==", false, false));
    assert(model.draft_channel()->settings().psk().size() == 16);
    state.channel_configs[1].mutable_settings()->set_name("RemoteEdit");
    state.channel_revisions[1] = 2;
    model.sync(state);
    assert(model.draft_channel()->settings().name() == "Valid");
    assert(!model.save_channel());  // no client cannot report a queued save
    model.discard_channel();
    assert(model.draft_channel()->settings().name().empty());
    assert(model.begin_new_channel());
    assert(model.draft_channel()->index() == 1);
    assert(model.draft_channel()->role() == meshtastic::Channel::SECONDARY);
    assert(model.edit_channel(0));
    assert(!model.stage_channel("Primary", meshtastic::Channel::DISABLED,
        ChannelsViewModel::KeyMode::keep_existing, "", false, false));

    meshtastic::AdminMessage admin;
    admin.mutable_set_channel()->CopyFrom(state.channel_configs[1]);
    admin.set_session_passkey("key");
    const auto bytes = meshtastic::encode_admin_message(admin, 1, 42);
    meshtastic::ToRadio radio;
    std::string error;
    assert(meshtastic::decode_to_radio(bytes, radio, error));
    assert(radio.packet().id() == 42);
    assert(radio.packet().decoded().portnum() == meshtastic::ADMIN_APP);
    meshtastic::AdminMessage decoded;
    assert(decoded.ParseFromString(radio.packet().decoded().payload()));
    assert(decoded.set_channel().settings().unknown_fields() == unknown);
    assert(decoded.session_passkey() == "key");
}
void test_links_and_preview()
{
    auto state = snapshot();
    state.channel_configs[3].set_role(meshtastic::Channel::SECONDARY);
    state.channel_configs[3].mutable_settings()->set_name("Secondary");
    ChannelsViewModel model;
    model.sync(state);
    assert(model.export_channels());
    const auto link = model.share_link();
    assert(link.rfind("https://meshtastic.org/e/#", 0) == 0);
    meshtastic::ChannelSet decoded;
    std::string error;
    assert(ChannelsViewModel::parse_channel_link(link, decoded, error));
    assert(decoded.settings_size() == 2);
    assert(decoded.settings(0).name() == "Primary");
    assert(decoded.settings(1).name() == "Secondary");
    assert(decoded.settings(1).psk() == std::string(16, '\x04'));
    assert(decoded.has_lora_config());
    assert(decoded.lora_config().region() == meshtastic::Config::LoRaConfig::US);
    assert(model.preview_import(link));
    assert(model.import_summary().find("slots 0-1") != std::string::npos);
    assert(model.import_has_lora());
    assert(!model.apply_import(false));  // preview never writes to the device
    assert(!ChannelsViewModel::parse_channel_link("https://example.org/e/#AA", decoded, error));
    assert(!ChannelsViewModel::parse_channel_link("https://meshtastic.org/e/#not?base64", decoded, error));
    meshtastic::ChannelSet invalid;
    invalid.add_settings()->set_psk("bad");
    assert(!ChannelsViewModel::parse_channel_link(ChannelsViewModel::make_channel_link(invalid), decoded, error));
    invalid.Clear();
    for (int index = 0; index < 9; ++index) invalid.add_settings();
    assert(!ChannelsViewModel::parse_channel_link(ChannelsViewModel::make_channel_link(invalid), decoded, error));
    invalid.Clear();
    invalid.add_settings()->set_name("morethan11bytes");
    assert(!ChannelsViewModel::parse_channel_link(ChannelsViewModel::make_channel_link(invalid), decoded, error));
}

void test_radio_identity_isolates_channel_state()
{
    char temporary[] = "/tmp/meshtastic-channel-identity-XXXXXX";
    assert(::mkdtemp(temporary));
    const auto database = std::filesystem::path(temporary) / "channels.sqlite3";
    {
        AdminTestRadio radio;
        meshtastic::MeshtasticClient client(radio.endpoint(), database);
        radio.connect(client);
        ChannelsViewModel model(&client);
        auto radio_a = snapshot();
        radio_a.my_node_number = 101;
        model.sync(radio_a);
        assert(model.edit_channel(1));
        assert(model.stage_channel("A-draft", meshtastic::Channel::SECONDARY,
                                   ChannelsViewModel::KeyMode::keep_existing, "", false, false));
        assert(model.export_channels());
        assert(model.preview_import(model.share_link()));
        assert(model.save_channel());
        assert(!model.save_channel());
        assert(std::string(lv_subject_get_string(model.status_subject())).find("awaiting") != std::string::npos);
        auto old_readback = radio_a;
        old_readback.channel_configs[1].mutable_settings()->set_name("old-refresh");
        old_readback.channel_revisions[1] = 2;
        old_readback.deliveries = client.snapshot().deliveries;
        assert(!old_readback.deliveries.empty());
        if (!old_readback.deliveries.empty()) old_readback.deliveries.back().status = meshtastic::MessageStatus::sent;
        model.sync(old_readback);
        assert(std::string(lv_subject_get_string(model.status_subject())).find("queued") != std::string::npos ||
               std::string(lv_subject_get_string(model.status_subject())).find("waiting") != std::string::npos);
        if (!old_readback.deliveries.empty()) old_readback.deliveries.back().status = meshtastic::MessageStatus::timed_out;
        const auto timed_out_connection = radio.connections();
        model.sync(old_readback);
        assert(std::string(lv_subject_get_string(model.status_subject())) ==
               "Device rejected or could not receive the write");
        radio.wait_for_reconnect(client, timed_out_connection);
        assert(model.save_channel());

        auto disconnected = radio_a;
        disconnected.my_node_number = 0;
        disconnected.state = meshtastic::ConnectionState::reconnecting;
        disconnected.config_complete = false;
        disconnected.session_passkey.clear();
        const auto disconnected_connection = radio.connections();
        model.sync(disconnected);
        assert(model.draft_channel() && model.draft_channel()->settings().name() == "A-draft");
        assert(!model.share_link().empty() && !model.import_summary().empty());
        assert(std::string(lv_subject_get_string(model.status_subject())).find("Connection lost") != std::string::npos);
        radio.wait_for_reconnect(client, disconnected_connection);
        model.sync(radio_a);
        assert(model.draft_channel()->settings().name() == "A-draft");
        assert(model.save_channel());
        assert(!model.save_channel());
        assert(std::string(lv_subject_get_string(model.status_subject())).find("awaiting") != std::string::npos);

        client.set_enabled(false);
        auto radio_b = radio_a;
        radio_b.my_node_number = 202;
        radio_b.channel_configs[1].mutable_settings()->set_name("B");
        radio_b.channel_configs[1].mutable_settings()->set_psk(std::string(16, 'B'));
        radio_b.channel_revisions[1] = 2;
        model.sync(radio_b);
        assert(!model.draft_channel());
        assert(model.share_link().empty() && model.import_summary().empty());
        assert(!model.apply_import(false));
        assert(model.edit_channel(1));
        assert(model.draft_channel()->settings().name() == "B");
        assert(model.draft_channel()->settings().psk() == std::string(16, 'B'));
        assert(!model.save_channel());
        assert(std::string(lv_subject_get_string(model.status_subject())).find("Connect and synchronize") != std::string::npos);
        assert(model.stage_channel("B-draft", meshtastic::Channel::SECONDARY,
                                   ChannelsViewModel::KeyMode::keep_existing, "", false, false));
        model.sync(radio_a);
        assert(!model.draft_channel());
        assert(model.edit_channel(1));
        assert(model.draft_channel()->settings().name().empty());
        client.stop();
    }
    std::filesystem::remove_all(temporary);
}
}  // namespace
int main()
{
    lv_init();
    test_key_validation();
    test_channel_edit_preserves_fields();
    test_links_and_preview();
    test_radio_identity_isolates_channel_state();
    lv_deinit();
    return 0;
}
