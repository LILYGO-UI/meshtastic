#include "persistence/meshtastic_repository.hpp"
#include "services/radio_session.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace meshtastic;
namespace {
std::filesystem::path temporary_path()
{
    char pattern[]="/tmp/lilygo-meshtastic-repository-XXXXXX";
    const auto *directory=mkdtemp(pattern);assert(directory);return std::filesystem::path(directory)/"test.sqlite3";
}
void repository_round_trip()
{
    const auto path=temporary_path();
    std::int64_t newest_id=0;
    {
        Repository repository(path);
        const auto session=repository.activate("127.0.0.1:4403",1);
        Node node;node.number=2;node.long_name="山顶 Relay";node.latitude=31.2;node.longitude=121.4;node.battery_level=82;
        repository.store_node(session,node);
        Config lora;lora.mutable_lora()->set_region(Config::LoRaConfig::CN);repository.store_config(session,lora);
        Config device;device.mutable_device()->set_role(Config::DeviceConfig::ROUTER);repository.store_config(session,device);
        Config power;power.mutable_power()->set_is_power_saving(true);repository.store_config(session,power);
        ModuleConfig telemetry;telemetry.mutable_telemetry()->set_device_update_interval(600);repository.store_module_config(session,telemetry);
        ModuleConfig mqtt;mqtt.mutable_mqtt()->set_enabled(true);mqtt.mutable_mqtt()->set_address("localhost");repository.store_module_config(session,mqtt);
        Channel channel;channel.set_index(1);channel.set_role(Channel::SECONDARY);channel.mutable_settings()->set_name("Field");channel.mutable_settings()->set_psk(std::string(32,'k'));repository.store_channel_config(session,channel);
        for(std::uint32_t index=1;index<=320;++index){Message message;message.from=2;message.to=kBroadcastNode;message.id=index;message.text="history "+std::to_string(index);newest_id=repository.store_message(session,message).local_id;}
        Message duplicate;duplicate.from=2;duplicate.to=kBroadcastNode;duplicate.id=320;duplicate.text="history 320";
        assert(repository.store_message(session,duplicate).local_id==newest_id);
        assert(repository.recent_messages(session).size()==200);
        const auto latest=repository.query_messages(session,false,0,0,0,50);assert(latest.size()==50&&latest.front().id==271&&latest.back().id==320);
        const auto previous=repository.query_messages(session,false,0,0,latest.front().local_id,50);assert(previous.front().id==221&&previous.back().id==270);
        assert(repository.conversations(session).size()==1&&repository.conversations(session)[0].unread_count==320);
        assert(repository.mark_read(session,false,0,0));assert(!repository.mark_read(session,false,0,0));
        Message direct;direct.from=3;direct.to=1;direct.id=777;direct.channel=0;direct.text="private";repository.store_message(session,direct);
        assert(repository.conversations(session).size()==2);
        assert(repository.query_messages(session,true,3,0,0,50).size()==1);
        MeshPacket packet;packet.set_id(900);packet.set_from(1);packet.set_to(2);packet.mutable_decoded()->set_portnum(TEXT_MESSAGE_APP);packet.mutable_decoded()->set_payload("unconfirmed");
        DeliveryRecord delivery;delivery.packet_id=900;delivery.status=MessageStatus::sent;delivery.attempts=1;delivery.portnum=TEXT_MESSAGE_APP;
        repository.store_outbound(session,packet,delivery);
        Message outgoing;outgoing.from=1;outgoing.to=2;outgoing.id=900;outgoing.text="unconfirmed";outgoing.outgoing=true;outgoing.status=MessageStatus::sent;repository.store_message(session,outgoing);
        PositionRecord position;position.node_number=2;position.packet_id=1;position.position.set_latitude_i(312000000);position.position.set_longitude_i(1214000000);repository.store_position(session,position);repository.store_position(session,position);
        TelemetryRecord metric;metric.node_number=2;metric.packet_id=2;metric.telemetry.mutable_environment_metrics()->set_temperature(23.4F);repository.store_telemetry(session,metric);
        WaypointRecord waypoint;waypoint.sender=2;waypoint.packet_id=3;waypoint.waypoint.set_id(33);waypoint.waypoint.set_name("Camp");waypoint.waypoint.set_latitude_i(312000000);waypoint.waypoint.set_longitude_i(1214000000);repository.store_waypoint(session,waypoint);
        RouteRecord route;route.from=2;route.to=1;route.packet_id=4;route.request_id=901;route.route.add_route(3);route.route.add_route_back(4);repository.store_route(session,route);
        const auto other_radio=repository.activate("127.0.0.1:4403",2);
        assert(repository.recent_messages(other_radio).empty());assert(repository.positions(other_radio,0,0,100).empty());
        const auto other_endpoint=repository.activate("10.0.0.1:4403",1);assert(repository.load_state(other_endpoint).nodes.empty());
    }
    {
        Repository repository(path);const auto session=repository.activate("127.0.0.1:4403",1);repository.recover_interrupted(session);
        const auto state=repository.load_state(session);assert(state.radio_number==1&&state.nodes.size()==1);assert(state.nodes[0].long_name=="山顶 Relay");
        assert(state.device_configs.size()==3&&state.module_configs.size()==2);assert(state.channel_configs.at(1).settings().psk()==std::string(32,'k'));
        assert(state.device_configs.at(Config::kLora).lora().region()==Config::LoRaConfig::CN);
        assert(state.device_configs.at(Config::kDevice).device().role()==Config::DeviceConfig::ROUTER);
        const auto messages=repository.query_messages(session,false,0,0,0,400);assert(messages.size()==320&&messages.back().local_id==newest_id&&messages.back().read);
        const auto outgoing=repository.query_messages(session,true,2,0,0,50);assert(outgoing.size()==1&&outgoing[0].status==MessageStatus::failed);
        const auto restored=repository.find_outbound(session,900);
        assert(restored && restored->delivery.status==MessageStatus::failed);
        assert(restored->delivery.source==1 && restored->delivery.destination==2 && restored->delivery.channel==0);
        assert(repository.positions(session,2,0,100).size()==1);
        assert(repository.telemetry(session,2,0,100)[0].telemetry.has_environment_metrics());
        assert(repository.waypoints(session,100)[0].waypoint.name()=="Camp");
        assert(repository.routes(session,2,100)[0].route.route_back(0)==4);
    }
    // Restore the earlier schema shape and verify its rows survive migration.
    sqlite3 *database=nullptr;assert(sqlite3_open(path.c_str(),&database)==SQLITE_OK);
    assert(sqlite3_exec(database,"DROP INDEX message_dedup_window;ALTER TABLE messages DROP COLUMN observed_at;DROP TABLE events;PRAGMA user_version=1",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(database);
    {Repository migrated(path);const auto session=migrated.activate("127.0.0.1:4403",1);assert(migrated.recent_messages(session).size()==200);assert(migrated.positions(session,0,0,100).empty());}
    std::filesystem::remove_all(path.parent_path());
}

void packet_id_reuse_and_deduplication()
{
    const auto path=temporary_path();
    {
        Repository repository(path);const auto scope=repository.activate("mesh:4403",1);
        Message original;original.from=2;original.to=kBroadcastNode;original.id=55;original.channel=0;original.text="hello";original.received_at=1;
        const auto first=repository.store_message(scope,original);
        Message duplicate=original;duplicate.received_at=4102444800U;
        assert(repository.store_message(scope,duplicate).local_id==first.local_id);
        Message different_channel=original;different_channel.channel=1;
        assert(repository.store_message(scope,different_channel).local_id!=first.local_id);
        Message different_destination=original;different_destination.to=1;
        assert(repository.store_message(scope,different_destination).local_id!=first.local_id);
        Message different_text=original;different_text.text="different";
        assert(repository.store_message(scope,different_text).local_id!=first.local_id);
        assert(repository.recent_messages(scope,20).size()==4);

        PositionRecord position;position.node_number=2;position.packet_id=77;position.position.set_latitude_i(1);
        repository.store_position(scope,position);repository.store_position(scope,position);
        position.position.set_latitude_i(2);repository.store_position(scope,position);
        assert(repository.positions(scope,2,0,20).size()==2);
        position.packet_id=0;position.received_at=100;repository.store_position(scope,position);
        position.received_at=101;repository.store_position(scope,position);
        assert(repository.positions(scope,2,0,20).size()==4);

        sqlite3 *raw=nullptr;assert(sqlite3_open(path.c_str(),&raw)==SQLITE_OK);
        assert(sqlite3_exec(raw,"UPDATE messages SET observed_at=observed_at-120; UPDATE events SET observed_at=observed_at-120",nullptr,nullptr,nullptr)==SQLITE_OK);
        sqlite3_close(raw);
        const auto later=repository.store_message(scope,original);
        assert(later.local_id!=first.local_id);
        assert(repository.store_message(scope,original).local_id==later.local_id);
        assert(repository.recent_messages(scope,20).size()==5);
        position.packet_id=77;position.position.set_latitude_i(1);repository.store_position(scope,position);
        assert(repository.positions(scope,2,0,20).size()==5);

        Message outgoing=original;outgoing.from=1;outgoing.to=2;outgoing.outgoing=true;outgoing.id=99;
        const auto first_send=repository.store_message(scope,outgoing);
        assert(repository.store_message(scope,outgoing).local_id==first_send.local_id);
    }
    {
        Repository reopened(path);const auto scope=reopened.activate("mesh:4403",1);
        assert(reopened.recent_messages(scope,20).size()==6);
        assert(reopened.positions(scope,2,0,20).size()==5);
    }
    std::filesystem::remove_all(path.parent_path());
}
void delivery_state_machine()
{
    RadioSession session(std::chrono::milliseconds(15));MeshPacket packet;packet.set_id(42);packet.set_from(1);packet.set_to(2);packet.set_channel(1);packet.mutable_decoded()->set_portnum(TEXT_MESSAGE_APP);
    assert(session.queued(packet).status==MessageStatus::queued);
    MeshPacket ack;ack.set_from(2);ack.set_to(1);ack.set_channel(1);ack.mutable_decoded()->set_portnum(ROUTING_APP);ack.mutable_decoded()->set_request_id(42);Routing routing;routing.set_error_reason(Routing::NONE);
    assert(!session.routing(ack,routing));assert(session.sent(42,123)->attempts==1);
    ack.mutable_decoded()->set_request_id(999);assert(!session.routing(ack,routing));ack.mutable_decoded()->set_request_id(42);
    ack.set_from(3);assert(!session.routing(ack,routing));ack.set_from(2);
    ack.set_to(9);assert(!session.routing(ack,routing));ack.set_to(1);
    ack.mutable_decoded()->set_portnum(TEXT_MESSAGE_APP);assert(!session.routing(ack,routing));ack.mutable_decoded()->set_portnum(ROUTING_APP);
    ack.set_channel(2);assert(!session.routing(ack,routing));ack.set_channel(1);
    ack.set_from(0);assert(!session.routing(ack,routing));ack.set_from(2);
    assert(session.routing(ack,routing)->status==MessageStatus::acknowledged);
    routing.set_error_reason(Routing::NO_ROUTE);assert(!session.routing(ack,routing));
    packet.set_id(43);session.queued(packet);session.sent(43,123);ack.mutable_decoded()->set_request_id(43);routing.set_error_reason(Routing::NO_ROUTE);
    assert(session.routing(ack,routing)->status==MessageStatus::rejected);
    packet.set_id(44);session.queued(packet);session.sent(44,123);std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto expired=session.expire();assert(expired.size()==1&&expired[0].packet_id==44&&expired[0].status==MessageStatus::timed_out);assert(session.expire().empty());
    assert(session.queued(packet).status==MessageStatus::queued);assert(session.sent(44,124)->attempts==2);
    const auto interrupted=session.interrupt("Disconnected");assert(interrupted.size()==1&&interrupted[0].status==MessageStatus::failed);
    assert(!session.sent(44,125));

    packet.set_id(45);packet.set_to(kBroadcastNode);session.queued(packet);session.sent(45,123);
    routing.set_error_reason(Routing::NONE);ack.mutable_decoded()->set_request_id(45);ack.set_from(2);
    assert(!session.routing(ack,routing));ack.set_from(0);
    assert(session.routing(ack,routing)->status==MessageStatus::acknowledged);
    packet.set_id(46);packet.set_to(2);session.queued(packet);session.sent(46,123);
    ack.mutable_decoded()->set_request_id(46);ack.set_channel(0);routing.set_error_reason(Routing::NO_CHANNEL);
    assert(session.routing(ack,routing)->status==MessageStatus::rejected);

    packet.set_id(48);packet.set_to(1);packet.set_channel(0);packet.mutable_decoded()->set_portnum(ADMIN_APP);
    session.queued(packet);session.sent(48,123);ack.mutable_decoded()->set_request_id(48);
    routing.set_error_reason(Routing::NONE);ack.set_from(0);ack.set_to(1);ack.set_channel(0);
    assert(session.routing(ack,routing)->status==MessageStatus::acknowledged);

    DeliveryRecord restored;restored.packet_id=47;restored.status=MessageStatus::sent;restored.source=1;restored.destination=2;restored.channel=1;
    session.reset({restored});routing.set_error_reason(Routing::NONE);ack.mutable_decoded()->set_request_id(47);ack.set_from(3);ack.set_channel(1);
    assert(!session.routing(ack,routing));ack.set_from(2);
    assert(session.routing(ack,routing)->status==MessageStatus::acknowledged);

    RadioSession crowded(std::chrono::seconds(30));
    MeshPacket pending;pending.set_from(1);pending.set_to(2);pending.mutable_decoded()->set_portnum(ADMIN_APP);
    for(std::uint32_t id=1;id<=260;++id){pending.set_id(id);crowded.queued(pending);}
    assert(crowded.records().size()==260);
    MeshPacket response;response.set_from(2);response.set_to(1);response.mutable_decoded()->set_portnum(ROUTING_APP);
    Routing accepted;accepted.set_error_reason(Routing::NONE);
    for(std::uint32_t id=2;id<=260;++id){crowded.sent(id,123);response.mutable_decoded()->set_request_id(id);assert(crowded.routing(response,accepted));}
    const auto retained=crowded.records();
    assert(retained.size()==201);
    assert(std::any_of(retained.begin(),retained.end(),[](const DeliveryRecord &value){return value.packet_id==1&&value.status==MessageStatus::queued;}));
}
}
int main(){repository_round_trip();packet_id_reuse_and_deduplication();delivery_state_machine();return 0;}
