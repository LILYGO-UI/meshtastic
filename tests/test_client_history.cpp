#include "domain/meshtastic_client.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace meshtastic;
namespace {
using namespace std::chrono_literals;
void until(const std::function<bool()> &condition)
{
    const auto deadline=std::chrono::steady_clock::now()+4s;
    while(!condition() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);
    assert(condition());
}
class FakeRadio {
public:
    FakeRadio() {
        listener_=::socket(AF_INET,SOCK_STREAM,0);assert(listener_>=0);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(::bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(::listen(listener_,4)==0);
        socklen_t size=sizeof(address);assert(::getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size)==0);port_=ntohs(address.sin_port);
        worker_=std::thread([this]{run();});
    }
    ~FakeRadio(){stopping_=true;{std::lock_guard<std::mutex> lock(write_mutex_);if(peer_>=0)::shutdown(peer_,SHUT_RDWR);}worker_.join();::close(listener_);}
    std::uint16_t port()const{return port_;}
    void set_identity(std::uint32_t value){identity_=value;}
    void hold_admin_responses(bool hold){hold_admin_=hold;}
    void send(const FromRadio &message) {
        const auto bytes=message.SerializeAsString();const auto frame=frame_payload(std::vector<std::uint8_t>(bytes.begin(),bytes.end()));
        std::lock_guard<std::mutex> lock(write_mutex_);assert(peer_>=0);std::size_t at=0;
        while(at<frame.size()) {
#if defined(MSG_NOSIGNAL)
            const auto count=::send(peer_,frame.data()+at,frame.size()-at,MSG_NOSIGNAL);
#else
            const auto count=::send(peer_,frame.data()+at,frame.size()-at,0);
#endif
            assert(count>0);at+=static_cast<std::size_t>(count);
        }
    }
    void routing(std::uint32_t request,Routing::Error error,std::uint32_t sender=2,std::uint32_t destination=0,std::uint32_t channel=0) {
        Routing routing;routing.set_error_reason(error);FromRadio message;auto *packet=message.mutable_packet();packet->set_from(sender);packet->set_to(destination ? destination : identity_.load());packet->set_channel(channel);packet->set_id(next_inbound_++);
        packet->mutable_decoded()->set_portnum(ROUTING_APP);packet->mutable_decoded()->set_request_id(request);packet->mutable_decoded()->set_payload(routing.SerializeAsString());send(message);
    }
    std::vector<MeshPacket> packets()const{std::lock_guard<std::mutex> lock(observed_mutex_);return observed_;}
    MeshPacket packet(const std::function<bool(const MeshPacket&)> &match,std::size_t occurrence=1)const {
        MeshPacket result;
        until([&]{std::size_t count=0;for(const auto &packet:packets())if(match(packet)&&++count==occurrence){result=packet;return true;}return false;});return result;
    }
    std::size_t count(std::uint32_t id)const{std::size_t count=0;for(const auto &packet:packets())if(packet.id()==id)++count;return count;}
private:
    void initial(std::uint32_t config_id) {
        FromRadio info;info.mutable_my_info()->set_my_node_num(identity_);send(info);
        FromRadio node;auto *value=node.mutable_node_info();value->set_num(2);value->mutable_user()->set_id("!00000002");value->mutable_user()->set_long_name("山顶 Relay");value->mutable_position()->set_latitude_i(312000000);value->mutable_position()->set_longitude_i(1214000000);send(node);
        FromRadio lora;lora.mutable_config()->mutable_lora()->set_region(Config::LoRaConfig::CN);send(lora);
        FromRadio device;device.mutable_config()->mutable_device()->set_role(Config::DeviceConfig::CLIENT);send(device);
        FromRadio mqtt;mqtt.mutable_moduleconfig()->mutable_mqtt()->set_address("localhost");send(mqtt);
        FromRadio telemetry;telemetry.mutable_moduleconfig()->mutable_telemetry()->set_device_update_interval(600);send(telemetry);
        FromRadio channel;channel.mutable_channel()->set_index(0);channel.mutable_channel()->set_role(Channel::PRIMARY);channel.mutable_channel()->mutable_settings()->set_name("LongFast");channel.mutable_channel()->mutable_settings()->set_psk(std::string(32,'k'));send(channel);
        FromRadio complete;complete.set_config_complete_id(config_id);send(complete);
    }
    void respond_to_admin(const MeshPacket &packet) {
        if (!packet.has_decoded() || packet.decoded().portnum()!=ADMIN_APP || !packet.decoded().want_response()) return;
        if (hold_admin_) return;
        AdminMessage request;
        assert(request.ParseFromString(packet.decoded().payload()));
        AdminMessage response;
        response.set_session_passkey(packet.to()==identity_ ? "local-session-secret" : "remote-session-secret");
        if (request.has_get_channel_request()) {
            auto *channel=response.mutable_get_channel_response();channel->set_index(request.get_channel_request()-1);
            channel->set_role(Channel::PRIMARY);channel->mutable_settings()->set_name("LongFast");
        } else if (request.has_get_owner_request()) {
            response.mutable_get_owner_response()->set_long_name("Local owner");
        } else if (request.has_get_config_request()) {
            response.mutable_get_config_response()->mutable_lora()->set_region(Config::LoRaConfig::CN);
        } else if (request.has_get_module_config_request()) {
            response.mutable_get_module_config_response()->mutable_mqtt()->set_address("radio");
        } else if (request.has_get_device_metadata_request()) {
            response.mutable_get_device_metadata_response()->set_firmware_version("test");
        } else {
            return;
        }
        FromRadio incoming;
        auto *reply=incoming.mutable_packet();reply->set_from(packet.to()==identity_ ? 0 : packet.to());reply->set_to(identity_);reply->set_id(next_inbound_++);
        reply->mutable_decoded()->set_portnum(ADMIN_APP);reply->mutable_decoded()->set_request_id(packet.id());
        reply->mutable_decoded()->set_payload(response.SerializeAsString());
        send(incoming);
    }
    void run() {
        while(!stopping_) {
            pollfd listener{listener_,POLLIN,0};if(::poll(&listener,1,100)<=0)continue;
            const int peer=::accept(listener_,nullptr,nullptr);if(peer<0)continue;
#if defined(SO_NOSIGPIPE)
            int no_sigpipe=1;::setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&no_sigpipe,sizeof(no_sigpipe));
#endif
            {std::lock_guard<std::mutex> lock(write_mutex_);peer_=peer;}
            FrameParser parser;
            while(!stopping_) {
                pollfd descriptor{peer,POLLIN,0};const auto ready=::poll(&descriptor,1,100);if(ready<=0)continue;
                std::uint8_t buffer[2048];const auto size=::recv(peer,buffer,sizeof(buffer),0);if(size<=0)break;
                for(const auto &bytes:parser.feed(buffer,static_cast<std::size_t>(size))){ToRadio message;assert(message.ParseFromArray(bytes.data(),static_cast<int>(bytes.size())));if(message.has_want_config_id())initial(message.want_config_id());if(message.has_packet()){{std::lock_guard<std::mutex> lock(observed_mutex_);observed_.push_back(message.packet());}respond_to_admin(message.packet());}}
            }
            {std::lock_guard<std::mutex> lock(write_mutex_);peer_=-1;::close(peer);}
        }
    }
    int listener_=-1,peer_=-1;std::uint16_t port_=0;std::atomic<bool> stopping_{false},hold_admin_{false};std::atomic<std::uint32_t> identity_{1};std::atomic<std::uint32_t> next_inbound_{10000};
    mutable std::mutex observed_mutex_;std::mutex write_mutex_;std::vector<MeshPacket> observed_;std::thread worker_;
};
Message message(const MeshtasticClient &client,const std::string &text)
{
    for(const auto &message:client.query_messages(true,2,0,0,100))if(message.text==text)return message;
    return {};
}
void exercise_client()
{
    char pattern[]="/tmp/lilygo-meshtastic-client-XXXXXX";const auto *directory=mkdtemp(pattern);assert(directory);const auto path=std::filesystem::path(directory)/"history.sqlite3";
    FakeRadio radio;const Endpoint endpoint{"127.0.0.1",radio.port()};
    std::uint32_t ack_id=0;
    {
        MeshtasticClient client(endpoint,path,180ms);client.start();until([&]{return client.snapshot().state==ConnectionState::connected;});
        auto state=client.snapshot();assert(state.device_configs.size()==2&&state.module_configs.size()==2&&state.channel_configs.at(0).settings().psk().size()==32);
        AdminMessage remote_admin;remote_admin.set_session_passkey("remote-session-secret");
        remote_admin.mutable_get_config_response()->mutable_lora()->set_region(Config::LoRaConfig::EU_868);
        FromRadio remote_response;auto *admin_packet=remote_response.mutable_packet();admin_packet->set_from(2);admin_packet->set_to(1);admin_packet->set_id(500);
        admin_packet->mutable_decoded()->set_portnum(ADMIN_APP);admin_packet->mutable_decoded()->set_payload(remote_admin.SerializeAsString());radio.send(remote_response);
        until([&]{return client.snapshot().last_admin.has_value();});
        state=client.snapshot();assert(state.session_passkey.empty());assert(state.lora_config->region()==Config::LoRaConfig::CN);
        assert(state.device_configs.at(Config::kLora).lora().region()==Config::LoRaConfig::CN);
        client.set_session_passkey("local-explicit-key");
        Config remote_config;remote_config.mutable_lora()->set_region(Config::LoRaConfig::EU_868);
        Channel remote_channel;remote_channel.set_index(2);remote_channel.set_role(Channel::SECONDARY);
        User remote_owner;remote_owner.set_long_name("Remote");
        ModuleConfig remote_module;remote_module.mutable_mqtt()->set_enabled(true);
        assert(!client.set_config(remote_config,2));
        assert(!client.set_channel(remote_channel,2));
        assert(!client.set_owner(remote_owner,2));
        assert(!client.set_module_config(remote_module,2));
        assert(!client.begin_edit_settings(2));
        assert(!client.commit_edit_settings(2));
        assert(client.request_session_key(2));
        until([&]{return client.set_config(remote_config,2);});
        assert(client.set_channel(remote_channel,2));
        const auto remote_write=radio.packet([](const MeshPacket &packet){
            if(packet.to()!=2||!packet.has_decoded()||packet.decoded().portnum()!=ADMIN_APP)return false;
            AdminMessage admin;return admin.ParseFromString(packet.decoded().payload())&&admin.has_set_config();
        });
        AdminMessage remote_write_body;assert(remote_write_body.ParseFromString(remote_write.decoded().payload()));
        assert(remote_write_body.session_passkey()=="remote-session-secret");
        const auto remote_channel_write=radio.packet([](const MeshPacket &packet){
            if(packet.to()!=2||!packet.has_decoded()||packet.decoded().portnum()!=ADMIN_APP)return false;
            AdminMessage admin;return admin.ParseFromString(packet.decoded().payload())&&admin.has_set_channel();
        });
        AdminMessage remote_channel_body;assert(remote_channel_body.ParseFromString(remote_channel_write.decoded().payload()));
        assert(remote_channel_body.session_passkey()=="remote-session-secret");
        assert(client.snapshot().session_passkey=="local-explicit-key");
        assert(!client.set_remote_session_passkey(1,"must-not-override-local"));
        assert(client.set_remote_session_passkey(3,"explicit-remote-key"));
        assert(client.set_owner(remote_owner,3));
        const auto explicit_write=radio.packet([](const MeshPacket &packet){
            if(packet.to()!=3||!packet.has_decoded()||packet.decoded().portnum()!=ADMIN_APP)return false;
            AdminMessage admin;return admin.ParseFromString(packet.decoded().payload())&&admin.has_set_owner();
        });
        AdminMessage explicit_body;assert(explicit_body.ParseFromString(explicit_write.decoded().payload()));
        assert(explicit_body.session_passkey()=="explicit-remote-key");
        Config local_config;local_config.mutable_lora()->set_region(Config::LoRaConfig::CN);
        assert(client.set_config(local_config));
        const auto local_write=radio.packet([](const MeshPacket &packet){
            if(packet.to()!=1||!packet.has_decoded()||packet.decoded().portnum()!=ADMIN_APP)return false;
            AdminMessage admin;return admin.ParseFromString(packet.decoded().payload())&&admin.has_set_config();
        });
        AdminMessage local_body;assert(local_body.ParseFromString(local_write.decoded().payload()));
        assert(local_body.session_passkey()=="local-explicit-key");
        assert(client.request_session_key());
        until([&]{return client.snapshot().session_passkey=="local-session-secret";});
        assert(client.request_config(AdminMessage::LORA_CONFIG));
        assert(client.request_module_config(AdminMessage::MQTT_CONFIG));
        assert(client.request_channel(0));
        assert(client.request_owner());
        assert(client.request_metadata());
        until([&]{const auto current=client.snapshot();return current.owner && current.metadata &&
            current.channel_revisions.count(0) && current.device_configs.at(Config::kLora).lora().region()==Config::LoRaConfig::CN &&
            current.module_configs.at(ModuleConfig::kMqtt).mqtt().address()=="radio";});
        const auto snapshot_after_admin = client.snapshot();
        assert(std::none_of(snapshot_after_admin.nodes.begin(), snapshot_after_admin.nodes.end(),
                            [](const Node &node) { return node.number == 0; }));
        std::size_t admin_reads=0;
        for(const auto &packet:radio.packets()) {
            if(!packet.has_decoded() || packet.decoded().portnum()!=ADMIN_APP)continue;
            AdminMessage admin;assert(admin.ParseFromString(packet.decoded().payload()));
            if(admin.has_get_config_request()||admin.has_get_module_config_request()||admin.has_get_channel_request()||
               admin.has_get_owner_request()||admin.has_get_device_metadata_request()) {
                assert(packet.decoded().want_response());++admin_reads;
            }
        }
        assert(admin_reads==7);
        Config update;update.mutable_lora()->set_region(Config::LoRaConfig::CN);assert(client.set_config(update));
        const auto write=radio.packet([](const MeshPacket &packet){
            if(!packet.has_decoded()||packet.decoded().portnum()!=ADMIN_APP)return false;
            AdminMessage admin;return admin.ParseFromString(packet.decoded().payload())&&admin.has_set_config();
        });
        assert(!write.decoded().want_response());
        radio.hold_admin_responses(true);
        AdminMessage tracked_get;tracked_get.set_get_config_request(AdminMessage::LORA_CONFIG);
        const auto first_get=client.queue_admin_tracked(tracked_get);
        const auto second_get=client.queue_admin_tracked(tracked_get);
        assert(first_get&&second_get&&*first_get!=*second_get);
        radio.packet([&](const MeshPacket &packet){return packet.id()==*first_get;});
        radio.packet([&](const MeshPacket &packet){return packet.id()==*second_get;});
        auto reply_to=[&](std::uint32_t request_id,std::uint32_t sender,bool lora) {
            AdminMessage response;
            if(lora)response.mutable_get_config_response()->mutable_lora()->set_region(Config::LoRaConfig::CN);
            else response.mutable_get_config_response()->mutable_device()->set_role(Config::DeviceConfig::ROUTER);
            FromRadio incoming;auto *packet=incoming.mutable_packet();packet->set_from(sender);packet->set_to(1);packet->set_id(100000+request_id);
            packet->mutable_decoded()->set_portnum(ADMIN_APP);packet->mutable_decoded()->set_request_id(request_id);
            packet->mutable_decoded()->set_payload(response.SerializeAsString());radio.send(incoming);
        };
        reply_to(*first_get,2,true); // A remote node cannot satisfy a local GET.
        reply_to(*first_get,0,false); // A different config category cannot satisfy the request.
        reply_to(*first_get+1000000,0,true); // Unknown request id.
        std::this_thread::sleep_for(30ms);
        assert(!client.snapshot().admin_responses.count(*first_get));
        reply_to(*second_get,0,true);
        until([&]{return client.snapshot().admin_responses.count(*second_get)!=0;});
        assert(!client.snapshot().admin_responses.count(*first_get));
        reply_to(*first_get,0,true);
        until([&]{return client.snapshot().admin_responses.count(*first_get)!=0;});
        radio.hold_admin_responses(false);
        assert(client.send_text_to("hello",2));const auto hello=radio.packet([](const MeshPacket &packet){return packet.decoded().payload()=="hello";});ack_id=hello.id();
        until([&]{return message(client,"hello").status==MessageStatus::sent;});assert(hello.want_ack());
        radio.routing(hello.id()+100,Routing::NONE);std::this_thread::sleep_for(20ms);assert(message(client,"hello").status==MessageStatus::sent);
        radio.routing(hello.id(),Routing::NONE,3);std::this_thread::sleep_for(10ms);assert(message(client,"hello").status==MessageStatus::sent);
        radio.routing(hello.id(),Routing::NONE,2,9);std::this_thread::sleep_for(10ms);assert(message(client,"hello").status==MessageStatus::sent);
        radio.routing(hello.id(),Routing::NONE,2,1,1);std::this_thread::sleep_for(10ms);assert(message(client,"hello").status==MessageStatus::sent);
        radio.routing(hello.id(),Routing::NONE);until([&]{return message(client,"hello").status==MessageStatus::acknowledged;});
        FromRadio incoming;auto *text=incoming.mutable_packet();text->set_from(2);text->set_to(1);text->set_id(42);text->mutable_decoded()->set_portnum(TEXT_MESSAGE_APP);text->mutable_decoded()->set_payload("incoming");radio.send(incoming);radio.send(incoming);
        until([&]{return client.query_messages(true,2,0,0,50).size()==2;});assert(client.snapshot().conversations[0].unread_count==1);assert(client.mark_conversation_read(true,2,0));
        const auto revision=client.snapshot().revision;assert(!client.mark_conversation_read(true,2,0));assert(client.snapshot().revision==revision);
        assert(client.send_text_to("reject",2));const auto rejected=radio.packet([](const MeshPacket &packet){return packet.decoded().payload()=="reject";});radio.routing(rejected.id(),Routing::NO_ROUTE);until([&]{return message(client,"reject").status==MessageStatus::rejected;});
        assert(client.send_text_to("timeout",2));const auto timeout=radio.packet([](const MeshPacket &packet){return packet.decoded().payload()=="timeout";});until([&]{return message(client,"timeout").status==MessageStatus::timed_out;});
        assert(!client.send_data(TEXT_MESSAGE_APP,"different",2,0,3,MeshPacket::RELIABLE,timeout.id()));
        std::this_thread::sleep_for(230ms);assert(radio.count(timeout.id())==1);assert(client.retry_message(timeout.id()));radio.packet([&](const MeshPacket &packet){return packet.id()==timeout.id();},2);radio.routing(timeout.id(),Routing::NONE);until([&]{return message(client,"timeout").status==MessageStatus::acknowledged;});assert(message(client,"timeout").attempts==2);
        assert(client.request_traceroute(2));const auto trace=radio.packet([](const MeshPacket &packet){return packet.decoded().portnum()==TRACEROUTE_APP;});assert(trace.decoded().want_response()&&trace.decoded().dest()==2);
        RouteDiscovery route;route.add_route(3);route.add_route_back(4);FromRadio trace_reply;auto *rp=trace_reply.mutable_packet();rp->set_from(2);rp->set_to(1);rp->set_id(501);rp->mutable_decoded()->set_portnum(TRACEROUTE_APP);rp->mutable_decoded()->set_request_id(trace.id());rp->mutable_decoded()->set_payload(route.SerializeAsString());radio.send(trace_reply);until([&]{return client.query_routes(2).size()==2;});assert(client.query_routes(2).back().request_id==trace.id());
        assert(client.request_telemetry(2));const auto request=radio.packet([](const MeshPacket &packet){return packet.decoded().portnum()==TELEMETRY_APP;});assert(request.decoded().want_response()&&request.decoded().payload().empty());
        Telemetry telemetry;telemetry.mutable_environment_metrics()->set_temperature(23.4F);FromRadio metrics;auto *mp=metrics.mutable_packet();mp->set_from(2);mp->set_to(1);mp->set_id(502);mp->mutable_decoded()->set_portnum(TELEMETRY_APP);mp->mutable_decoded()->set_request_id(request.id());mp->mutable_decoded()->set_payload(telemetry.SerializeAsString());radio.send(metrics);until([&]{return client.query_telemetry(2).size()==1;});assert(client.query_telemetry(2)[0].telemetry.has_environment_metrics());
        Waypoint waypoint;waypoint.set_name("Camp");waypoint.set_latitude_i(312000000);waypoint.set_longitude_i(1214000000);assert(client.send_waypoint(waypoint));const auto shared=radio.packet([](const MeshPacket &packet){return packet.decoded().portnum()==WAYPOINT_APP;});Waypoint decoded;assert(decoded.ParseFromString(shared.decoded().payload())&&decoded.id()!=0);assert(client.query_waypoints().size()==1);
        Position position;position.set_latitude_i(313000000);position.set_longitude_i(1215000000);FromRadio moved;auto *pp=moved.mutable_packet();pp->set_from(2);pp->set_to(kBroadcastNode);pp->set_id(503);pp->mutable_decoded()->set_portnum(POSITION_APP);pp->mutable_decoded()->set_payload(position.SerializeAsString());radio.send(moved);until([&]{return client.query_positions(2).size()==2;});assert(client.snapshot().nodes[0].latitude && *client.snapshot().nodes[0].latitude>31.29);
        assert(client.send_text_to("interrupted",2));radio.packet([](const MeshPacket &packet){return packet.decoded().payload()=="interrupted";});client.stop();
        assert(client.snapshot().admin_responses.empty());
    }
    const auto before=radio.packets().size();
    {
        MeshtasticClient restored(endpoint,path,180ms);assert(restored.snapshot().nodes.size()==1);assert(message(restored,"hello").status==MessageStatus::acknowledged);assert(message(restored,"interrupted").status==MessageStatus::failed);assert(restored.query_waypoints().size()==1&&restored.query_routes(2).size()==2&&restored.query_telemetry(2).size()==1);
        restored.start();until([&]{return restored.snapshot().state==ConnectionState::connected;});std::this_thread::sleep_for(80ms);assert(radio.packets().size()==before);assert(!restored.retry_message(ack_id));restored.stop();
    }
    radio.set_identity(9);
    {
        MeshtasticClient different(endpoint,path,180ms);different.start();until([&]{return different.snapshot().state==ConnectionState::connected;});assert(different.snapshot().my_node_number==9);assert(different.query_messages(true,2,0).empty());assert(different.query_waypoints().empty()&&different.query_routes().empty());assert(!different.retry_message(ack_id));different.stop();
    }
    std::filesystem::remove_all(path.parent_path());
}
}
int main(){exercise_client();return 0;}
