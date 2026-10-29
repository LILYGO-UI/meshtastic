#include "configuration/configuration_catalog.hpp"
#include "admin_test_radio.hpp"
#include "domain/meshtastic_protocol.hpp"
#include "pages/device_tools/device_tools_view_model.hpp"
#include "pages/device_tools/device_tools_view.hpp"
#include "pages/channels/channels_view_model.hpp"
#include "persistence/meshtastic_repository.hpp"
#include "app.hpp"

#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>

namespace {
using namespace meshtastic::configuration;

const Category &category(Scope scope, int payload_case)
{
    const auto &all = catalog();
    const auto found = std::find_if(all.begin(), all.end(), [&](const Category &item) {
        return item.scope == scope && item.payload_case == payload_case;
    });
    assert(found != all.end());
    return *found;
}
std::size_t field_index(const Category &item, const std::string &id)
{
    for (std::size_t index = 0; index < item.fields.size(); ++index) if (item.fields[index].id == id) return index;
    assert(false);
    return 0;
}
void append_varint(std::string &bytes, std::uint64_t value)
{
    do {
        std::uint8_t byte = value & 0x7fU;
        value >>= 7;
        if (value) byte |= 0x80U;
        bytes += static_cast<char>(byte);
    } while (value);
}
std::string empty_category(int payload_case)
{
    std::string bytes;
    append_varint(bytes, (static_cast<std::uint64_t>(payload_case) << 3U) | 2U);
    bytes += '\0';
    return bytes;
}
meshtastic::AdminMessage decode_admin(const std::vector<std::uint8_t> &bytes)
{
    meshtastic::ToRadio radio;
    std::string error;
    assert(meshtastic::decode_to_radio(bytes, radio, error));
    assert(radio.has_packet());
    assert(radio.packet().decoded().portnum() == meshtastic::ADMIN_APP);
    meshtastic::AdminMessage admin;
    assert(admin.ParseFromString(radio.packet().decoded().payload()));
    return admin;
}
void test_all_categories_and_wire_requests()
{
    std::set<int> devices, modules;
    for (const auto &item : catalog()) {
        assert(!item.fields.empty());
        assert(item.admin_type == item.payload_case - 1);
        Draft draft;
        if (item.scope == Scope::device) {
            meshtastic::Config value;
            assert(value.ParseFromString(empty_category(item.payload_case)));
            assert(draft.load(item, value));
            assert(devices.insert(item.payload_case).second);
        } else {
            meshtastic::ModuleConfig value;
            assert(value.ParseFromString(empty_category(item.payload_case)));
            assert(draft.load(item, value));
            assert(modules.insert(item.payload_case).second);
        }
        for (std::size_t index = 0; index < item.fields.size(); ++index) {
            const auto &field = item.fields[index];
            std::string value;
            switch (field.type) {
                case FieldType::boolean: value = "true"; break;
                case FieldType::text: value = "Test"; break;
                case FieldType::enumeration:
                    assert(!field.choices.empty());
                    value = std::to_string(field.choices.back().value);
                    break;
                case FieldType::decimal: value = "1.25"; break;
                case FieldType::integer: value = std::to_string(static_cast<std::int64_t>(field.minimum)); break;
            }
            std::string error;
            assert(draft.update(index, value, error));
            assert(error.empty());
            assert(draft.value(index) == value);
        }
        const std::string key("\x00\x10\x20\xff", 4);
        if (item.scope == Scope::device) {
            const auto admin = decode_admin(meshtastic::encode_admin_set_config(draft.device_config(), key));
            assert(admin.has_set_config());
            assert(admin.set_config().payload_variant_case() == item.payload_case);
            assert(admin.session_passkey() == key);
            assert(admin.set_config().SerializeAsString() == draft.device_config().SerializeAsString());
            const auto request = decode_admin(meshtastic::encode_admin_get_config(
                static_cast<meshtastic::AdminMessage::ConfigType>(item.admin_type), key));
            assert(request.has_get_config_request());
            assert(request.get_config_request() == item.admin_type);
        } else {
            const auto admin = decode_admin(meshtastic::encode_admin_set_module_config(draft.module_config(), key));
            assert(admin.has_set_module_config());
            assert(admin.set_module_config().payload_variant_case() == item.payload_case);
            assert(admin.session_passkey() == key);
            assert(admin.set_module_config().SerializeAsString() == draft.module_config().SerializeAsString());
            const auto request = decode_admin(meshtastic::encode_admin_get_module_config(
                static_cast<meshtastic::AdminMessage::ModuleConfigType>(item.admin_type), key));
            assert(request.has_get_module_config_request());
            assert(request.get_module_config_request() == item.admin_type);
        }
    }
    assert(devices == std::set<int>({1, 2, 3, 4, 5, 6, 7, 8, 10}));
    assert(modules.size() == 17);
    assert(*modules.begin() == 1 && *modules.rbegin() == 17);
}
void test_validation_and_unknown_roundtrip()
{
    const auto &lora = category(Scope::device, meshtastic::Config::kLora);
    meshtastic::Config original;
    original.mutable_lora()->set_region(meshtastic::Config::LoRaConfig::US);
    original.mutable_lora()->set_hop_limit(3);
    original.mutable_lora()->set_use_preset(true);
    original.mutable_lora()->add_ignore_incoming(0x12345678);
    std::string unknown;
    append_varint(unknown, 500U << 3U);
    append_varint(unknown, 12345);
    assert(original.mutable_lora()->ParseFromString(original.lora().SerializeAsString() + unknown));
    std::string outer_unknown;
    append_varint(outer_unknown, 501U << 3U);
    append_varint(outer_unknown, 6789);
    assert(original.ParseFromString(original.SerializeAsString() + outer_unknown));
    Draft draft;
    assert(draft.load(lora, original));
    std::string error;
    const auto hops = field_index(lora, "hop_limit");
    assert(!draft.update(hops, "8", error));
    assert(error.find("between") != std::string::npos);
    assert(!draft.update(hops, "-1", error));
    assert(!draft.update(hops, "1.5", error));
    assert(!draft.update(hops, " 2", error));
    assert(!draft.update(hops, "999999999999999999999", error));
    assert(!draft.dirty());
    assert(draft.update(hops, "6", error));
    assert(draft.dirty());
    const auto admin = decode_admin(meshtastic::encode_admin_set_config(draft.device_config(), "key"));
    const auto &saved = admin.set_config();
    assert(saved.has_lora() && !saved.has_device());
    assert(saved.lora().hop_limit() == 6);
    assert(saved.lora().region() == meshtastic::Config::LoRaConfig::US);
    assert(saved.lora().ignore_incoming_size() == 1);
    assert(saved.lora().SerializeAsString().find(unknown) != std::string::npos);
    assert(saved.SerializeAsString().find(outer_unknown) != std::string::npos);
    assert(!draft.update(field_index(lora, "region"), "12345", error));
    assert(!draft.update(field_index(lora, "frequency_offset"), "nan", error));
    assert(!draft.update(field_index(lora, "frequency_offset"), "inf", error));
    draft.discard();
    assert(!draft.dirty());
    assert(draft.device_config().SerializeAsString() == original.SerializeAsString());

    const auto &mqtt = category(Scope::module, meshtastic::ModuleConfig::kMqtt);
    meshtastic::ModuleConfig config;
    config.mutable_mqtt()->set_password("private value");
    config.mutable_mqtt()->mutable_map_report_settings()->set_position_precision(14);
    assert(draft.load(mqtt, config));
    assert(draft.update(field_index(mqtt, "enabled"), "true", error));
    assert(!draft.update(field_index(mqtt, "enabled"), "yes", error));
    assert(!draft.update(field_index(mqtt, "address"), std::string(129, 'a'), error));
    assert(!draft.update(field_index(mqtt, "address"), "line\nbreak", error));
    const auto module_admin = decode_admin(meshtastic::encode_admin_set_module_config(draft.module_config(), "key"));
    assert(module_admin.set_module_config().mqtt().password() == "private value");
    assert(module_admin.set_module_config().mqtt().map_report_settings().position_precision() == 14);
    const auto &neighbor = category(Scope::module, meshtastic::ModuleConfig::kNeighborInfo);
    assert(!validate(neighbor.fields[field_index(neighbor, "update_interval")], "14399", error));
    assert(validate(neighbor.fields[field_index(neighbor, "update_interval")], "14400", error));
    assert(validate(neighbor.fields[field_index(neighbor, "update_interval")], "0", error));
}

void test_device_action_requires_session_key()
{
    meshtastic::MeshtasticClient client({"127.0.0.1", 1});
    DeviceToolsViewModel model(&client);
    meshtastic::ClientSnapshot state;
    state.state = meshtastic::ConnectionState::connected;
    state.config_complete = true;
    model.sync(state, true);
    assert(!model.perform_device_action(DeviceToolsViewModel::DeviceAction::reboot));
}

void test_precise_transaction_order_and_failures()
{
    char temporary[] = "/tmp/meshtastic-admin-identity-XXXXXX";
    assert(::mkdtemp(temporary));
    const auto database = std::filesystem::path(temporary) / "transaction.sqlite3";
    {
        AdminTestRadio radio;
        meshtastic::MeshtasticClient client(radio.endpoint(), database);
        radio.connect(client);
        meshtastic::ClientSnapshot snapshot;
        snapshot.my_node_number = 101;
        snapshot.state = meshtastic::ConnectionState::connected;
        snapshot.config_complete = true;
        snapshot.session_passkey = "session";
        meshtastic::AdminMessage write, get, expected;
        write.mutable_set_config()->mutable_device()->set_tzdef("CST-8");
        get.set_get_config_request(meshtastic::AdminMessage::DEVICE_CONFIG);
        expected.mutable_get_config_response()->CopyFrom(write.set_config());
        AdminTransaction transaction;
        std::string error;
        assert(transaction.start(client, {write}, {{get, expected}}, error));
        const auto write_id = transaction.packet_id();
        snapshot.deliveries = client.snapshot().deliveries;
        assert(snapshot.deliveries.size() == 1 && snapshot.deliveries.back().packet_id == write_id);
        snapshot.admin_responses[write_id + 999] = expected;
        assert(transaction.advance(client, snapshot, error) == AdminTransaction::State::waiting);
        assert(client.snapshot().deliveries.size() == 1); // no GET before write acknowledgement
        snapshot.deliveries.back().status = meshtastic::MessageStatus::acknowledged;
        assert(transaction.advance(client, snapshot, error) == AdminTransaction::State::waiting);
        const auto read_id = transaction.packet_id();
        assert(read_id != write_id && client.snapshot().deliveries.size() == 2);
        snapshot.deliveries = client.snapshot().deliveries;
        assert(transaction.advance(client, snapshot, error) == AdminTransaction::State::waiting); // old response ignored
        snapshot.admin_responses[read_id] = expected;
        for (auto &delivery : snapshot.deliveries)
            if (delivery.packet_id == read_id) delivery.status = meshtastic::MessageStatus::timed_out;
        assert(transaction.advance(client, snapshot, error) == AdminTransaction::State::confirmed); // response supersedes GET ACK

        transaction.reset();
        assert(transaction.start(client, {write}, {{get, expected}}, error));
        snapshot.deliveries = client.snapshot().deliveries;
        snapshot.deliveries.back().status = meshtastic::MessageStatus::rejected;
        const auto rejected_connection = radio.connections();
        assert(transaction.advance(client, snapshot, error) == AdminTransaction::State::failed);
        assert(error.find("rejected") != std::string::npos);
        radio.wait_for_reconnect(client, rejected_connection);
        snapshot = client.snapshot();
        transaction.reset();
        assert(transaction.start(client, {write}, {{get, expected}}, error));
        snapshot.deliveries = client.snapshot().deliveries;
        assert(transaction.advance(client, snapshot, error, std::chrono::steady_clock::now() + std::chrono::seconds(31)) == AdminTransaction::State::failed);
        assert(error.find("timed out") != std::string::npos);
    }
    std::filesystem::remove_all(temporary);
}

void test_exclusive_transaction_across_views()
{
    AdminTestRadio radio;
    meshtastic::MeshtasticClient client(radio.endpoint(), ":memory:");
    // A saved radio identity alone must never authorize an administrative write.
    client.set_enabled(true);
    client.set_session_passkey("session");
    meshtastic::AdminMessage write, request, expected;
    write.mutable_set_config()->mutable_device()->set_tzdef("CST-8");
    request.set_get_config_request(meshtastic::AdminMessage::DEVICE_CONFIG);
    expected.mutable_get_config_response()->CopyFrom(write.set_config());
    std::string error;
    AdminTransaction disconnected;
    assert(!disconnected.start(client, {write}, {{request, expected}}, error));
    assert(error.find("Connect and synchronize") != std::string::npos);
    assert(client.snapshot().deliveries.empty());
    radio.connect(client);

    auto state = client.snapshot();
    state.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("UTC0");
    state.device_configs[meshtastic::Config::kPosition].mutable_position()->set_fixed_position(false);
    state.device_configs[meshtastic::Config::kLora].mutable_lora()->set_region(meshtastic::Config::LoRaConfig::US);
    state.device_config = state.device_configs[meshtastic::Config::kDevice].device();
    state.lora_config = state.device_configs[meshtastic::Config::kLora].lora();
    state.owner.emplace();
    state.owner->set_id("!00000065");
    state.owner->set_long_name("Local");
    auto &channel = state.channel_configs[1];
    channel.set_index(1);
    channel.set_role(meshtastic::Channel::SECONDARY);
    channel.mutable_settings()->set_name("Field");
    channel.mutable_settings()->set_psk(std::string(16, 'a'));
    state.device_config_revisions[meshtastic::Config::kDevice] = 1;
    state.device_config_revisions[meshtastic::Config::kPosition] = 1;
    state.device_config_revisions[meshtastic::Config::kLora] = 1;
    state.channel_revisions[1] = 1;

    DeviceToolsViewModel settings(&client);
    ChannelsViewModel channels(&client);
    settings.sync(state, true);
    channels.sync(state);
    const auto device_timezone = field_index(category(Scope::device, meshtastic::Config::kDevice), "tzdef");
    assert(settings.edit_field(device_timezone, "CST-8"));
    assert(settings.save_selected_configuration());
    const auto packets_after_first = client.snapshot().deliveries.size();
    settings.select_category(1);
    const auto fixed = field_index(category(Scope::device, meshtastic::Config::kPosition), "fixed_position");
    assert(settings.edit_field(fixed, "true"));
    assert(!settings.save_selected_configuration());
    assert(std::string(lv_subject_get_string(settings.configuration_status_subject())).find("busy") != std::string::npos);
    assert(settings.field_value(fixed) == "true");
    assert(!settings.save_configuration(meshtastic::Config::LoRaConfig::US,
                                        meshtastic::Config::LoRaConfig::LONG_FAST,
                                        meshtastic::Config::DeviceConfig::CLIENT, "Local"));
    assert(std::string(lv_subject_get_string(settings.edit_status_subject())).find("busy") != std::string::npos);
    assert(channels.edit_channel(1));
    assert(channels.stage_channel("Changed", meshtastic::Channel::SECONDARY,
                                  ChannelsViewModel::KeyMode::keep_existing, "", false, false));
    assert(!channels.save_channel());
    assert(std::string(lv_subject_get_string(channels.status_subject())).find("busy") != std::string::npos);
    assert(channels.draft_channel()->settings().name() == "Changed");
    assert(client.snapshot().deliveries.size() == packets_after_first);
    const auto write_id = client.snapshot().deliveries.back().packet_id;
    assert(client.request_owner()); // Existing raw GET APIs are not governed by the UI lease.

    // A write ACK is followed by an exact GET response before the lease moves.
    state.deliveries = client.snapshot().deliveries;
    const auto first_write = std::find_if(state.deliveries.begin(), state.deliveries.end(),
        [&](const meshtastic::DeliveryRecord &delivery) { return delivery.packet_id == write_id; });
    assert(first_write != state.deliveries.end());
    first_write->status = meshtastic::MessageStatus::acknowledged;
    settings.sync(state, true);
    const auto get_id = client.snapshot().deliveries.back().packet_id;
    state.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("CST-8");
    state.device_config_revisions[meshtastic::Config::kDevice] = 2;
    state.admin_responses[get_id] = expected;
    settings.sync(state, true);
    assert(channels.save_channel());
    assert(!settings.save_selected_configuration());
    assert(std::string(lv_subject_get_string(settings.configuration_status_subject())).find("busy") != std::string::npos);

    // A failed begin forces reconnection before another category may start.
    state.deliveries = client.snapshot().deliveries;
    state.deliveries.back().status = meshtastic::MessageStatus::timed_out;
    const auto previous_connection = radio.connections();
    channels.sync(state);
    assert(std::string(lv_subject_get_string(channels.status_subject())).find("could not receive") != std::string::npos);
    radio.wait_for_reconnect(client, previous_connection);
    state.state = meshtastic::ConnectionState::connected;
    state.config_complete = true;
    state.session_passkey = "session";
    state.deliveries = client.snapshot().deliveries;
    settings.sync(state, true);
    assert(settings.field_value(fixed) == "true");
    assert(settings.save_selected_configuration());

    // Separate client instances never share the same lease.
    AdminTestRadio other_radio(202);
    meshtastic::MeshtasticClient other_client(other_radio.endpoint(), ":memory:");
    other_radio.connect(other_client);
    AdminTransaction independent;
    assert(independent.start(other_client, {write}, {{request, expected}}, error));
    assert(!settings.save_configuration(meshtastic::Config::LoRaConfig::US,
                                        meshtastic::Config::LoRaConfig::LONG_FAST,
                                        meshtastic::Config::DeviceConfig::CLIENT, "Local"));
    assert(std::string(lv_subject_get_string(settings.edit_status_subject())).find("busy") != std::string::npos);
}

void test_active_delivery_retained_when_many_requests_are_queued()
{
    AdminTestRadio radio;
    meshtastic::MeshtasticClient client(radio.endpoint(), ":memory:");
    radio.connect(client);
    meshtastic::AdminMessage write, get, expected;
    write.mutable_set_config()->mutable_device()->set_tzdef("UTC0");
    get.set_get_config_request(meshtastic::AdminMessage::DEVICE_CONFIG);
    expected.mutable_get_config_response()->CopyFrom(write.set_config());
    AdminTransaction transaction;
    std::string error;
    assert(transaction.start(client, {write}, {{get, expected}}, error, 101));
    const auto id = transaction.packet_id();
    for (int index = 0; index < 210; ++index) assert(client.request_owner());
    auto state = client.snapshot();
    assert(state.deliveries.size() >= 211);
    const auto first = std::find_if(state.deliveries.begin(), state.deliveries.end(),
        [&](const meshtastic::DeliveryRecord &delivery) { return delivery.packet_id == id; });
    assert(first != state.deliveries.end());
    assert(first->status == meshtastic::MessageStatus::queued || first->status == meshtastic::MessageStatus::sent);
    first->status = meshtastic::MessageStatus::acknowledged;
    assert(transaction.advance(client, state, error) == AdminTransaction::State::waiting);
    assert(transaction.packet_id() != id);
}

void test_lease_lifetime_and_view_closure()
{
    AdminTestRadio radio;
    meshtastic::MeshtasticClient client(radio.endpoint(), ":memory:");
    radio.connect(client);
    std::string error;
    assert(!client.acquire_admin_transaction(error, 202));
    assert(error.find("identity changed") != std::string::npos);
    auto first = client.acquire_admin_transaction(error, 101);
    assert(first && first->valid());
    auto moved = std::move(*first);
    assert(moved.valid() && !first->valid());
    assert(!client.acquire_admin_transaction(error, 101));
    assert(error.find("busy") != std::string::npos);
    moved.reset();
    auto replacement = client.acquire_admin_transaction(error, 101);
    assert(replacement && replacement->valid());
    first->reset();
    assert(replacement->valid()); // stale tokens cannot release a newer owner
    replacement.reset();

    auto state = client.snapshot();
    state.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("UTC0");
    state.device_config_revisions[meshtastic::Config::kDevice] = 1;
    DeviceToolsViewModel model(&client);
    model.sync(state, true);
    const auto timezone = field_index(category(Scope::device, meshtastic::Config::kDevice), "tzdef");
    assert(model.edit_field(timezone, "CST-8"));
    auto *display = lv_display_create(320, 568);
    assert(display);
    auto *root = lv_obj_create(lv_screen_active());
    DeviceToolsView view(model, {});
    assert(view.create(root));
    assert(model.save_selected_configuration());
    view.destroy();
    assert(!client.acquire_admin_transaction(error, 101));
    assert(error.find("busy") != std::string::npos);
    state.deliveries = client.snapshot().deliveries;
    state.deliveries.back().status = meshtastic::MessageStatus::acknowledged;
    model.sync(state, true);
    const auto get_id = client.snapshot().deliveries.back().packet_id;
    meshtastic::AdminMessage response;
    response.mutable_get_config_response()->mutable_device()->set_tzdef("CST-8");
    state.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("CST-8");
    state.device_config_revisions[meshtastic::Config::kDevice] = 2;
    state.admin_responses[get_id] = response;
    model.sync(state, true);
    assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("confirmed") != std::string::npos);
    assert(client.acquire_admin_transaction(error, 101));
    lv_obj_delete(root);
    lv_display_delete(display);

    // A client can disappear before a lease; the shared state prevents UAF.
    AdminTestRadio other_radio(202);
    auto other_client = std::make_unique<meshtastic::MeshtasticClient>(other_radio.endpoint(), ":memory:");
    other_radio.connect(*other_client);
    auto surviving = other_client->acquire_admin_transaction(error, 202);
    assert(surviving && surviving->valid());
    other_client.reset();
    assert(!surviving->valid());
    surviving.reset();
}

void test_refresh_preserves_draft()
{
    DeviceToolsViewModel model;
    meshtastic::ClientSnapshot snapshot;
    snapshot.state = meshtastic::ConnectionState::connected;
    snapshot.config_complete = true;
    auto &config = snapshot.device_configs[meshtastic::Config::kDevice];
    config.mutable_device()->set_role(meshtastic::Config::DeviceConfig::CLIENT);
    config.mutable_device()->set_tzdef("UTC0");
    snapshot.device_config_revisions[meshtastic::Config::kDevice] = 1;
    model.sync(snapshot, true);
    assert(model.selected_draft().loaded());
    const auto &device = category(Scope::device, meshtastic::Config::kDevice);
    const auto timezone = field_index(device, "tzdef");
    assert(model.edit_field(timezone, "CST-8"));
    config.mutable_device()->set_tzdef("EST5");
    snapshot.device_config_revisions[meshtastic::Config::kDevice] = 2;
    model.sync(snapshot, true);
    assert(model.selected_draft().value(timezone) == "CST-8");
    model.select_category(1);
    model.select_category(0);
    assert(model.selected_draft().value(timezone) == "CST-8");
    const auto interval = field_index(device, "node_info_broadcast_secs");
    assert(!model.edit_field(interval, "invalid number"));
    model.select_category(1);
    model.select_category(0);
    assert(model.field_value(interval) == "invalid number");
    model.discard_selected_configuration();
    model.sync(snapshot, true);
    assert(model.selected_draft().value(timezone) == "EST5");
    model.set_editor_active(true);
    config.mutable_device()->set_tzdef("UTC0");
    snapshot.device_config_revisions[meshtastic::Config::kDevice] = 3;
    model.sync(snapshot, true);
    assert(model.selected_draft().value(timezone) == "EST5");
    model.set_editor_active(false);
    model.sync(snapshot, true);
    assert(model.selected_draft().value(timezone) == "UTC0");
    // A disconnected/no-client save cannot silently report success.
    assert(!model.save_selected_configuration());
}

void test_radio_identity_isolates_drafts_and_pending_saves()
{
    char temporary[] = "/tmp/meshtastic-settings-identity-XXXXXX";
    assert(::mkdtemp(temporary));
    const auto database = std::filesystem::path(temporary) / "settings.sqlite3";
    {
        AdminTestRadio radio;
        meshtastic::MeshtasticClient client(radio.endpoint(), database);
        radio.connect(client);
        client.set_session_passkey("local-session");
        DeviceToolsViewModel model(&client);
        meshtastic::ClientSnapshot radio_a;
        radio_a.my_node_number = 101;
        radio_a.state = meshtastic::ConnectionState::connected;
        radio_a.config_complete = true;
        radio_a.session_passkey = "local-session";
        radio_a.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("A");
        radio_a.device_config_revisions[meshtastic::Config::kDevice] = 1;
        radio_a.device_configs[meshtastic::Config::kLora].mutable_lora()->set_region(meshtastic::Config::LoRaConfig::US);
        radio_a.device_configs[meshtastic::Config::kLora].mutable_lora()->set_modem_preset(meshtastic::Config::LoRaConfig::LONG_FAST);
        radio_a.device_config_revisions[meshtastic::Config::kLora] = 1;
        radio_a.lora_config = radio_a.device_configs[meshtastic::Config::kLora].lora();
        radio_a.device_config = radio_a.device_configs[meshtastic::Config::kDevice].device();
        radio_a.owner.emplace();
        radio_a.owner->set_id("!00000065");
        radio_a.owner->set_role(meshtastic::Config::DeviceConfig::CLIENT);
        radio_a.owner->set_long_name("A owner");
        model.sync(radio_a, true);
        assert(model.save_configuration(meshtastic::Config::LoRaConfig::US,
                                        meshtastic::Config::LoRaConfig::LONG_FAST,
                                        meshtastic::Config::DeviceConfig::CLIENT,
                                        "A owner"));
        auto current = radio_a;
        for (int write = 0; write < 5; ++write) {
            current.deliveries = client.snapshot().deliveries;
            assert(!current.deliveries.empty());
            current.deliveries.back().status = meshtastic::MessageStatus::acknowledged;
            model.sync(current, true);
        }
        for (int read = 0; read < 3; ++read) {
            current.deliveries = client.snapshot().deliveries;
            assert(!current.deliveries.empty());
            const auto request = current.deliveries.back().packet_id;
            meshtastic::AdminMessage response;
            if (read == 0) response.mutable_get_config_response()->CopyFrom(radio_a.device_configs[meshtastic::Config::kLora]);
            else if (read == 1) response.mutable_get_config_response()->CopyFrom(radio_a.device_configs[meshtastic::Config::kDevice]);
            else response.mutable_get_owner_response()->CopyFrom(*radio_a.owner);
            current.admin_responses[request] = response;
            model.sync(current, true);
        }
        assert(std::string(lv_subject_get_string(model.edit_status_subject())).find("confirmed") != std::string::npos);
        const auto timezone = field_index(category(Scope::device, meshtastic::Config::kDevice), "tzdef");
        assert(model.edit_field(timezone, "A-category-draft"));
        assert(model.save_selected_configuration());
        auto old_readback = radio_a;
        old_readback.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("old-refresh");
        old_readback.device_config_revisions[meshtastic::Config::kDevice] = 2;
        old_readback.deliveries = client.snapshot().deliveries;
        assert(!old_readback.deliveries.empty());
        old_readback.deliveries.back().status = meshtastic::MessageStatus::sent;
        model.sync(old_readback, true);
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("waiting") != std::string::npos);
        old_readback.deliveries.back().status = meshtastic::MessageStatus::timed_out;
        const auto timed_out_connection = radio.connections();
        model.sync(old_readback, true);
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())) ==
               "Device rejected or could not receive the write; your draft is retained");
        radio.wait_for_reconnect(client, timed_out_connection);
        client.set_session_passkey("local-session");
        // Reopen the category from radio A for the radio identity transition.
        old_readback.deliveries.clear();
        old_readback.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("A");
        old_readback.device_config_revisions[meshtastic::Config::kDevice] = 3;
        model.sync(old_readback, true);
        assert(model.edit_field(timezone, "A-draft"));
        assert(model.save_selected_configuration());
        assert(!model.save_selected_configuration());
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("awaiting") != std::string::npos);

        auto disconnected = radio_a;
        disconnected.my_node_number = 0;
        disconnected.state = meshtastic::ConnectionState::reconnecting;
        disconnected.config_complete = false;
        disconnected.session_passkey.clear();
        const auto disconnected_connection = radio.connections();
        model.sync(disconnected, true);
        assert(model.selected_draft().value(timezone) == "A-draft");
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("Connection lost") != std::string::npos);
        assert(!model.save_selected_configuration());
        radio.wait_for_reconnect(client, disconnected_connection);
        client.set_session_passkey("local-session");
        model.sync(radio_a, true);
        assert(model.selected_draft().value(timezone) == "A-draft");
        assert(model.save_selected_configuration());
        assert(!model.save_selected_configuration());
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("awaiting") != std::string::npos);

        // No real packet is sent after changing the synthetic snapshot.
        client.set_enabled(false);
        auto radio_b = radio_a;
        radio_b.my_node_number = 202;
        radio_b.device_configs[meshtastic::Config::kDevice].mutable_device()->set_tzdef("B");
        radio_b.device_config_revisions[meshtastic::Config::kDevice] = 2;
        model.sync(radio_b, true);
        assert(model.selected_draft().value(timezone) == "B");
        assert(!model.selected_draft().dirty());
        assert(model.edit_field(timezone, "B-draft"));
        assert(!model.save_selected_configuration());
        assert(std::string(lv_subject_get_string(model.configuration_status_subject())).find("Connect and synchronize") != std::string::npos);
        model.sync(radio_a, true);
        assert(model.selected_draft().value(timezone) == "A");
        assert(!model.selected_draft().dirty());
        client.stop();
    }
    std::filesystem::remove_all(temporary);
}
}  // namespace
int main()
{
    lv_init();
    test_all_categories_and_wire_requests();
    test_validation_and_unknown_roundtrip();
    test_refresh_preserves_draft();
    test_device_action_requires_session_key();
    test_precise_transaction_order_and_failures();
    test_exclusive_transaction_across_views();
    test_active_delivery_retained_when_many_requests_are_queued();
    test_lease_lifetime_and_view_closure();
    test_radio_identity_isolates_drafts_and_pending_saves();
    lv_deinit();
    return 0;
}
