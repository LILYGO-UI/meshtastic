#include "app_router.hpp"
#include "pages/channels/channels_view_model.hpp"
#include "pages/chat/chat_view_model.hpp"
#include "pages/connection/connection_view_model.hpp"
#include "pages/messages/messages_view_model.hpp"
#include "pages/map/map_view_model.hpp"
#include "pages/nodes/nodes_view_model.hpp"

#include <cassert>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace {

void assert_subject_text(lv_subject_t *subject, const char *expected)
{
    assert(std::strcmp(lv_subject_get_string(subject), expected) == 0);
}

void test_snapshot_mapping()
{
    ConnectionViewModel view_model;
    assert(view_model.brightness() == 72);
    assert(!view_model.connected());

    meshtastic::ClientSnapshot snapshot;
    snapshot.state           = meshtastic::ConnectionState::connected;
    snapshot.endpoint        = "radio.local:4403";
    snapshot.my_node_number  = 0x12345678U;
    snapshot.config_complete = true;

    meshtastic::Node local;
    local.number    = snapshot.my_node_number;
    local.long_name = "Field Radio";
    local.last_heard = static_cast<std::uint32_t>(std::time(nullptr));
    local.role = meshtastic::Config::DeviceConfig::ROUTER;
    snapshot.nodes.push_back(local);

    meshtastic::Node remote;
    remote.number    = 0x87654321U;
    remote.long_name = "Ridge Relay";
    snapshot.nodes.push_back(remote);

    meshtastic::ChannelInfo channel;
    channel.name = "LongFast";
    channel.role = 1;
    snapshot.channels.push_back(channel);

    snapshot.lora_config.emplace();
    snapshot.lora_config->set_region(meshtastic::Config::LoRaConfig::US);
    snapshot.lora_config->set_modem_preset(meshtastic::Config::LoRaConfig::LONG_FAST);
    snapshot.config.emplace();
    snapshot.config->mutable_device()->set_role(meshtastic::Config::DeviceConfig::ROUTER);
    snapshot.device_config = snapshot.config->device();
    snapshot.metadata.emplace();
    snapshot.metadata->set_firmware_version("2.8.0");
    snapshot.local_stats.emplace();
    snapshot.local_stats->set_uptime_seconds(9000U);
    snapshot.local_stats->set_num_total_nodes(2U);
    snapshot.local_stats->set_num_online_nodes(7U);
    snapshot.local_stats->set_num_packets_rx(91U);

    meshtastic::Message message;
    message.from = remote.number;
    message.text = "Mesh online";
    snapshot.messages.push_back(message);

    view_model.sync(snapshot, true);

    assert(view_model.connected());
    assert(lv_subject_get_int(view_model.connected_subject()) == 1);
    assert(lv_subject_get_int(view_model.config_complete_subject()) == 1);
    assert(lv_subject_get_int(view_model.has_activity_subject()) == 1);
    assert(lv_subject_get_int(view_model.message_count_subject()) == 91);
    assert(lv_subject_get_int(view_model.node_count_subject()) == 2);
    assert_subject_text(view_model.action_subject(), "Disconnect");
    assert_subject_text(view_model.auto_reconnect_subject(), "AUTO-RECONNECT ON");
    assert_subject_text(view_model.config_status_subject(), "Synchronized");
    assert_subject_text(view_model.device_detail_subject(), "Configuration synchronized");
    assert_subject_text(view_model.device_name_subject(), "Field Radio");
    assert_subject_text(view_model.endpoint_subject(), "radio.local:4403");
    assert_subject_text(view_model.last_activity_detail_subject(), "Mesh online");
    assert_subject_text(view_model.last_activity_title_subject(), "Last text from Ridge Relay");
    assert_subject_text(view_model.primary_channel_subject(), "LongFast");
    assert_subject_text(view_model.device_role_subject(), "ROUTER");
    assert_subject_text(view_model.firmware_subject(), "2.8.0");
    assert_subject_text(view_model.uptime_subject(), "2h 30m");
    assert_subject_text(view_model.online_nodes_subject(), "7");
    assert_subject_text(view_model.lora_band_subject(), "US / LONG_FAST");
    assert_subject_text(view_model.node_id_subject(), "!12345678");
    assert_subject_text(view_model.status_subject(), "Connected  |  radio.local:4403");
    assert_subject_text(view_model.tcp_state_subject(), "Connected");

    lv_subject_set_int(view_model.brightness_subject(), 120);
    assert(view_model.brightness() == 100);

    snapshot.state           = meshtastic::ConnectionState::stopped;
    snapshot.config_complete = false;
    snapshot.error           = "Connection refused";
    snapshot.messages.clear();
    view_model.sync(snapshot, false);
    assert(!view_model.connected());
    assert(lv_subject_get_int(view_model.has_activity_subject()) == 0);
    assert_subject_text(view_model.action_subject(), "Connect");
    assert_subject_text(view_model.auto_reconnect_subject(), "AUTO-RECONNECT OFF");
    assert_subject_text(view_model.config_status_subject(), "Not synchronized");
    assert_subject_text(view_model.device_detail_subject(), "Connection refused");
}

void test_router()
{
    AppRouter router;
    assert(router.page() == AppPage::connect);
    assert(!router.navigate(AppPage::connect));
    assert(router.navigate(AppPage::nodes));
    assert(router.navigate(AppPage::node_detail));
    assert(router.back());
    assert(router.page() == AppPage::nodes);
    assert(!router.back());
    assert(!router.navigate(AppPage::count));
    assert(AppRouter::page_has_navigation(AppPage::map));
    assert(AppRouter::page_has_navigation(AppPage::channels));
    assert(!AppRouter::page_has_navigation(AppPage::chat));
    assert(std::string(AppRouter::page_name(AppPage::connect)) == "Status");
    assert(router.navigate(AppPage::device_tools));
    assert(router.back());
    assert(router.page() == AppPage::connect);
}

void test_collection_view_models()
{
    constexpr char long_node_name[] =
        u8"\u534e\u4e1c\u5c71\u810a\u5e94\u6025\u901a\u4fe1\u4e2d\u7ee7\u7ad9 Alpha-Long-Name-For-Wrapping";
    constexpr char long_message[] =
        u8"\u8fd9\u662f\u4e00\u6761\u7528\u4e8e\u9a8c\u8bc1\u4e2d\u6587\u56de\u9000\u548c\u6362\u884c\u7684\u957f\u6d88"
        u8"\u606f";
    meshtastic::ClientSnapshot snapshot;
    snapshot.state          = meshtastic::ConnectionState::connected;
    snapshot.my_node_number = 1;
    meshtastic::Node local;
    local.number     = 1;
    local.id         = "!00000001";
    local.long_name  = "Local";
    local.short_name = "LC";
    snapshot.nodes.push_back(local);
    meshtastic::Node relay;
    relay.number     = 2;
    relay.id         = "!00000002";
    relay.long_name  = long_node_name;
    relay.short_name = "RY";
    relay.role       = meshtastic::Config::DeviceConfig::ROUTER;
    relay.snr        = 5.5F;
    relay.battery_level = 64;
    relay.via_mqtt   = true;
    snapshot.nodes.push_back(relay);
    snapshot.messages.push_back({2, 1, 7, 0, 0, -80, 4.0F, "Hello", false, 0, meshtastic::MessageStatus::received, "", 0, false});

    MessagesViewModel messages;
    messages.sync(snapshot);
    assert(messages.connected());
    assert(messages.rows().size() == 1);
    assert(messages.rows().front().sender == long_node_name);
    assert(messages.rows().front().text == "Hello");
    assert_subject_text(messages.footer_subject(), "1 conversations | 1 unread");
    const auto initial_message_revision = lv_subject_get_int(messages.content_subject());
    messages.sync(snapshot);
    assert(lv_subject_get_int(messages.content_subject()) == initial_message_revision);

    for (std::uint32_t index = 0; index < 10; ++index) {
        meshtastic::Message message;
        message.from = relay.number;
        message.id   = 100 + index;
        message.text = std::string(long_message) + " #" + std::to_string(index);
        snapshot.messages.push_back(std::move(message));
    }
    messages.sync(snapshot);
    assert(messages.rows().size() == 2);
    assert(messages.rows().front().sender == long_node_name);
    assert(messages.rows().front().text == std::string(long_message) + " #9");
    assert(messages.rows().back().text == "Hello");
    assert(lv_subject_get_int(messages.content_subject()) != initial_message_revision);
    assert_subject_text(messages.footer_subject(), "2 conversations | 11 unread");

    NodesViewModel nodes;
    nodes.sync(snapshot);
    assert(nodes.connected());
    assert(nodes.rows().size() == 2);
    assert(nodes.rows().front().local);
    assert(!nodes.rows().back().local);
    assert(nodes.rows().back().name == long_node_name);
    assert(nodes.rows().back().detail.find("ROUTER") != std::string::npos);
    assert(nodes.rows().back().detail.find("SNR 5.500000 dB") != std::string::npos);
    assert(nodes.rows().back().detail.find("64%") != std::string::npos);
    assert(nodes.rows().back().detail.find("MQTT") != std::string::npos);
    assert_subject_text(nodes.count_subject(), "ALL 2     ONLINE 0     POSITION 0     1 HOP 0");
    assert_subject_text(nodes.footer_subject(), "2 nodes  |  Sorted by last heard");
    const auto initial_node_revision = lv_subject_get_int(nodes.content_subject());
    nodes.sync(snapshot);
    assert(lv_subject_get_int(nodes.content_subject()) == initial_node_revision);
    nodes.select_node(relay.number);
    assert(nodes.selected_node() == relay.number);

    relay.latitude        = 31.2304;
    relay.longitude       = 121.4737;
    snapshot.nodes.back() = relay;
    MapViewModel map;
    map.sync(snapshot);
    assert(map.markers().size() == 1);
    assert(map.markers().front().name == long_node_name);
    assert(std::string(lv_subject_get_string(map.status_subject())).find("1") != std::string::npos);

    meshtastic::ChannelInfo primary;
    primary.index = 3;
    primary.name  = "Operations";
    primary.role  = 1;
    snapshot.channels.push_back(primary);
    ChannelsViewModel channels;
    ChatViewModel chat;
    channels.sync(snapshot);
    chat.select_channel(channels.selected_channel(), channels.selected_channel_name());
    assert_subject_text(chat.channel_subject(), "Operations");

    snapshot.channels.clear();
    channels.sync(snapshot);
    chat.select_channel(channels.selected_channel(), channels.selected_channel_name());
    assert_subject_text(chat.channel_subject(), "Primary channel");
}

void test_message_conversations()
{
    meshtastic::ClientSnapshot snapshot;
    snapshot.state          = meshtastic::ConnectionState::connected;
    snapshot.my_node_number = 1;

    meshtastic::Node peer;
    peer.number    = 2;
    peer.long_name = "Ridge Relay";
    snapshot.nodes.push_back(peer);
    peer.number    = 3;
    peer.long_name = "Valley Scout";
    snapshot.nodes.push_back(peer);

    meshtastic::ChannelInfo primary;
    primary.index = 0;
    primary.name  = "LongFast";
    primary.role  = 1;
    snapshot.channels.push_back(primary);
    meshtastic::ChannelInfo operations;
    operations.index = 1;
    operations.name  = "Operations";
    operations.role  = 2;
    snapshot.channels.push_back(operations);

    snapshot.messages.push_back({2, meshtastic::kBroadcastNode, 10, 0, 0, 0, 0.0F, "channel zero", false, 0, meshtastic::MessageStatus::received, "", 0, false});
    snapshot.messages.push_back({3, meshtastic::kBroadcastNode, 11, 1, 0, 0, 0.0F, "channel one", false, 0, meshtastic::MessageStatus::received, "", 0, false});
    snapshot.messages.push_back({2, 1, 12, 0, 0, 0, 0.0F, "private from relay", false, 0, meshtastic::MessageStatus::received, "", 0, false});
    snapshot.messages.push_back({1, 3, 13, 0, 0, 0, 0.0F, "private to scout", true, 0, meshtastic::MessageStatus::received, "", 0, false});

    MessagesViewModel messages;
    messages.sync(snapshot);
    assert(messages.rows().size() == 4);
    assert(messages.rows()[0].direct);
    assert(messages.rows()[0].peer == 3);
    assert(messages.rows()[1].direct);
    assert(messages.rows()[1].peer == 2);
    assert(!messages.rows()[2].direct);
    assert(messages.rows()[2].channel == 1);

    ChatViewModel chat;
    chat.select_channel(1, "Operations");
    chat.sync(snapshot);
    assert(chat.messages().size() == 1);
    assert(chat.messages().front().text == "channel one");

    chat.select_conversation(true, 2, 0, "Ridge Relay");
    chat.sync(snapshot);
    assert(chat.has_selection());
    assert(chat.selected_direct());
    assert(chat.selected_peer() == 2);
    assert(chat.messages().size() == 1);
    assert(chat.messages().front().text == "private from relay");
}

}  // namespace

int main()
{
    lv_init();
    test_snapshot_mapping();
    test_router();
    test_collection_view_models();
    test_message_conversations();
    lv_deinit();
    return 0;
}
