#include "pages/chat/chat_view_model.hpp"
#include "components/utf8.hpp"
#include "pages/messages/messages_view_model.hpp"
#include "pages/nodes/nodes_view_model.hpp"
#include "pages/node_detail/node_detail_view_model.hpp"
#include "pages/map/map_view_model.hpp"
#include "persistence/meshtastic_repository.hpp"
#include <cassert>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace {
void test_conversations_and_pagination()
{
    meshtastic::ClientSnapshot snapshot;
    snapshot.my_node_number = 1;
    snapshot.channels.push_back({0, "Operations", 1, true});
    meshtastic::Node relay; relay.number = 2; relay.long_name = "山脊中继站"; snapshot.nodes.push_back(relay);
    for (int i = 0; i < 126; ++i) {
        meshtastic::Message message;
        message.id = static_cast<std::uint32_t>(i + 1); message.local_id = i + 1;
        message.from = 2; message.to = meshtastic::kBroadcastNode; message.text = "Report " + std::to_string(i);
        snapshot.messages.push_back(message);
    }
    auto direct = snapshot.messages.back(); direct.id = 200; direct.local_id = 200; direct.to = 1; direct.text = "Private report";
    snapshot.messages.push_back(direct);
    MessagesViewModel list; list.sync(snapshot);
    assert(list.rows().size() == 2 && list.rows()[0].direct);
    assert(list.rows()[0].unread_count == 1 && list.rows()[1].unread_count == 126);
    list.set_query("Private"); assert(list.rows().size() == 1 && list.rows()[0].direct);
    list.set_query("operations"); assert(list.rows().size() == 1 && !list.rows()[0].direct);
    list.set_query("missing"); assert(list.rows().empty());
    list.set_query(""); assert(list.rows().size() == 2);
    ChatViewModel chat; chat.select_channel(0, "Operations"); chat.sync(snapshot);
    assert(chat.messages().size() == 50 && chat.messages().front().local_id == 77);
    assert(chat.has_older()); assert(chat.load_older());
    assert(chat.messages().size() == 100 && chat.messages().front().local_id == 27);
    assert(chat.load_older()); assert(chat.messages().size() == 126 && !chat.has_older());
    chat.set_draft("未发送 draft"); chat.select_conversation(true, 2, 0, "Relay"); assert(chat.draft().empty());
    chat.set_draft("Private draft"); chat.select_channel(0, "Operations"); assert(chat.draft() == "未发送 draft");
    auto outgoing = snapshot.messages.back(); outgoing.to = meshtastic::kBroadcastNode; outgoing.outgoing = true;
    outgoing.status = meshtastic::MessageStatus::timed_out; snapshot.messages.push_back(outgoing);
    chat.sync(snapshot); assert(chat.messages().back().retryable);
    assert(chat.messages().back().detail.find("No acknowledgement") != std::string::npos);
}

void test_persistent_ui_read_boundary()
{
    char temporary[] = "/tmp/meshtastic-workflows-XXXXXX";
    assert(mkdtemp(temporary));
    const auto database = std::filesystem::path(temporary) / "mesh.sqlite3";
    {
        meshtastic::Repository repository(database);
        const auto session = repository.activate("mesh.test:4403", 1);
        meshtastic::Message message; message.id = 1; message.from = 2; message.to = 1; message.text = "Saved privately";
        repository.store_message(session, message);
        meshtastic::Message channel; channel.id = 2; channel.from = 2; channel.to = meshtastic::kBroadcastNode;
        channel.text = "Channel message";
        repository.store_message(session, channel);
        meshtastic::Message other_peer; other_peer.id = 3; other_peer.from = 3; other_peer.to = 1;
        other_peer.text = "Other private conversation";
        repository.store_message(session, other_peer);
        meshtastic::Message other_channel = message;
        other_channel.id = 4; other_channel.channel = 1; other_channel.text = "Private on channel one";
        repository.store_message(session, other_channel);
    }
    {
        meshtastic::MeshtasticClient client({"mesh.test", 4403}, database);
        ChatViewModel chat(&client);
        chat.select_conversation(true, 2, 0, "Relay"); chat.sync(client.snapshot());
        assert(chat.messages().size() == 1);
        assert(chat.messages().front().text == "Saved privately");
        auto unread_for = [](const meshtastic::ClientSnapshot &snapshot, bool direct, std::uint32_t peer, std::uint32_t channel) {
            for (const auto &conversation : snapshot.conversations)
                if (conversation.direct == direct && conversation.peer == peer && conversation.channel == channel)
                    return conversation.unread_count;
            return std::size_t{0};
        };
        assert(unread_for(client.snapshot(), true, 2, 0) == 1); // inactive screens never read messages
        chat.set_active(true);
        assert(unread_for(client.snapshot(), true, 2, 0) == 0);
        assert(unread_for(client.snapshot(), true, 2, 1) == 1);
        assert(unread_for(client.snapshot(), true, 3, 0) == 1);
        assert(unread_for(client.snapshot(), false, 0, 0) == 1);
        chat.sync(client.snapshot());
        assert(chat.messages().size() == 1 && chat.messages().front().text == "Saved privately");
        chat.set_draft("Draft channel zero");
        chat.select_conversation(true, 2, 1, "Relay");
        chat.sync(client.snapshot());
        assert(chat.messages().size() == 1 && chat.messages().front().text == "Private on channel one");
        assert(chat.draft().empty());
        chat.set_draft("Draft channel one");
        chat.select_conversation(true, 2, 0, "Relay");
        assert(chat.draft() == "Draft channel zero");
        chat.set_active(false);
    }
    std::filesystem::remove_all(temporary);
}

void test_untruncated_persistent_history_consumers()
{
    char temporary[] = "/tmp/meshtastic-history-ui-XXXXXX";
    assert(mkdtemp(temporary));
    const auto database = std::filesystem::path(temporary) / "mesh.sqlite3";
    {
        meshtastic::Repository repository(database);
        const auto session = repository.activate("history.test:4403", 1);
        meshtastic::Node local; local.number = 1; local.long_name = "Base";
        local.latitude = 0.0; local.longitude = 0.0;
        meshtastic::Node remote; remote.number = 2; remote.long_name = "Ridge";
        remote.latitude = 0.0; remote.longitude = 0.001;
        repository.store_node(session, local); repository.store_node(session, remote);
        for (std::uint32_t index = 1; index <= 205; ++index) {
            meshtastic::PositionRecord position;
            position.node_number = 2; position.packet_id = index; position.received_at = index;
            position.position.set_latitude_i(0);
            position.position.set_longitude_i(static_cast<std::int32_t>(index * 10));
            repository.store_position(session, position);
            if (index <= 125) {
                meshtastic::TelemetryRecord telemetry;
                telemetry.node_number = 2; telemetry.packet_id = index; telemetry.received_at = index;
                telemetry.telemetry.mutable_environment_metrics()->set_temperature(20.0F + index);
                repository.store_telemetry(session, telemetry);
            }
        }
        meshtastic::RouteRecord route;
        route.from = 2; route.to = 1; route.packet_id = 900; route.request_id = 901;
        route.route.add_route(1); route.route.add_snr_towards(12);
        repository.store_route(session, route);
    }
    {
        meshtastic::MeshtasticClient client({"history.test", 4403}, database);
        const auto snapshot = client.snapshot();
        assert(snapshot.positions.size() == 200);
        NodeDetailViewModel detail(&client);
        detail.select_node(2, snapshot);
        assert(detail.presentation().position_count == 205);
        assert(detail.presentation().telemetry_history.size() == 12);
        assert(detail.presentation().telemetry_history.front().find("145.0") != std::string::npos);
        assert(detail.presentation().route_history.size() == 1);
        MapViewModel map;
        map.set_client(&client);
        map.set_active(true);
        map.sync(snapshot);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        auto track = map.paths().end();
        while (std::chrono::steady_clock::now() < deadline) {
            meshmap::MapEvent event;
            map.poll(event);
            map.sync(snapshot);
            track = std::find_if(map.paths().begin(), map.paths().end(),
                                 [](const auto &path) { return !path.route && path.node_number == 2; });
            if (track != map.paths().end() && track->points.size() == 205) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(track != map.paths().end() && track->points.size() == 205);
    }
    std::filesystem::remove_all(temporary);
}

void test_nodes_and_history()
{
    assert(utf8_prefix_bytes("山脊中继站 Alpha", 2).empty());
    assert(utf8_prefix_bytes("山脊中继站 Alpha", 8) == "山脊");
    assert(utf8_prefix_characters("山脊中继站", 2) == "山脊");
    meshtastic::ClientSnapshot snapshot;
    snapshot.my_node_number = 1;
    meshtastic::Node local; local.number = 1; local.long_name = "Base";
    meshtastic::Node remote; remote.number = 2; remote.long_name = "Ridge";
    remote.latitude = 0; remote.longitude = 0; remote.hops_away = 1; remote.last_heard = static_cast<std::uint32_t>(std::time(nullptr));
    snapshot.nodes = {local, remote};
    NodesViewModel nodes; nodes.sync(snapshot); nodes.set_filter(2); assert(nodes.rows().size() == 1);
    nodes.set_query("BASE"); assert(nodes.rows().empty()); nodes.set_filter(0); assert(nodes.rows().size() == 1);
    nodes.set_query(""); nodes.set_filter(1); assert(nodes.rows().size() == 1 && nodes.rows()[0].number == 2);
    meshtastic::TelemetryRecord sample; sample.node_number = 2; sample.telemetry.mutable_environment_metrics()->set_temperature(21.5F);
    snapshot.telemetry.push_back(sample);
    meshtastic::PositionRecord position; position.node_number = 2; snapshot.positions.push_back(position);
    meshtastic::RouteRecord route; route.from = 2; route.to = 1; route.request_id = 100;
    snapshot.routes.push_back(route);
    NodeDetailViewModel detail; detail.select_node(2, snapshot);
    assert(detail.presentation().has_position);
    assert(detail.presentation().telemetry_history.front().find("21.5") != std::string::npos);
    assert(detail.presentation().position_count == 1);
    assert(detail.presentation().route_history.front().find("Base → Ridge") != std::string::npos);
    assert(!detail.share_waypoint("", "", 100, 0, 3600, 0));
    assert(!detail.request_traceroute() && !detail.request_telemetry());
    snapshot.nodes.back().short_name.clear();
    snapshot.nodes.back().long_name = "山脊中继站";
    detail.sync(snapshot);
    assert(detail.presentation().initials == "山脊");
}
}
int main()
{
    setenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK", "1", 1);
    lv_init();
    test_conversations_and_pagination();
    test_persistent_ui_read_boundary();
    test_untruncated_persistent_history_consumers();
    test_nodes_and_history();
    lv_deinit();
}
