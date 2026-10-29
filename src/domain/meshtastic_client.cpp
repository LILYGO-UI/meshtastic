#include "domain/meshtastic_client.hpp"
#include "persistence/meshtastic_repository.hpp"
#include "services/radio_session.hpp"
#include "transport/tcp_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace meshtastic {
namespace {

constexpr auto kReconnectDelay             = std::chrono::seconds(2);
constexpr auto kConfigSyncTimeout           = std::chrono::seconds(10);
constexpr auto kHeartbeatInterval           = std::chrono::seconds(15);

std::string socket_error(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool admin_is_get(const AdminMessage &message)
{
    return message.has_get_channel_request() || message.has_get_owner_request() ||
           message.has_get_config_request() || message.has_get_module_config_request() ||
           message.has_get_canned_message_module_messages_request() || message.has_get_device_metadata_request() ||
           message.has_get_ringtone_request() || message.has_get_device_connection_status_request() ||
           message.has_get_node_remote_hardware_pins_request() || message.has_get_ui_config_request();
}

bool admin_response_matches_request(const AdminMessage &request, const AdminMessage &response)
{
    if (request.has_get_channel_request())
        return response.has_get_channel_response() && request.get_channel_request()!=0 &&
               response.get_channel_response().index()==static_cast<std::int32_t>(request.get_channel_request()-1);
    if (request.has_get_owner_request()) return response.has_get_owner_response();
    if (request.has_get_config_request())
        return response.has_get_config_response() &&
               static_cast<int>(response.get_config_response().payload_variant_case())==
                   static_cast<int>(request.get_config_request())+1;
    if (request.has_get_module_config_request())
        return response.has_get_module_config_response() &&
               static_cast<int>(response.get_module_config_response().payload_variant_case())==
                   static_cast<int>(request.get_module_config_request())+1;
    if (request.has_get_device_metadata_request()) return response.has_get_device_metadata_response();
    if (request.has_get_canned_message_module_messages_request()) return response.has_get_canned_message_module_messages_response();
    if (request.has_get_ringtone_request()) return response.has_get_ringtone_response();
    if (request.has_get_device_connection_status_request()) return response.has_get_device_connection_status_response();
    if (request.has_get_node_remote_hardware_pins_request()) return response.has_get_node_remote_hardware_pins_response();
    if (request.has_get_ui_config_request()) return response.has_get_ui_config_response();
    return false;
}

}  // namespace

const char *connection_state_name(ConnectionState state)
{
    switch (state) {
        case ConnectionState::stopped:
            return "Disconnected";
        case ConnectionState::connecting:
            return "Connecting";
        case ConnectionState::synchronizing:
            return "Synchronizing";
        case ConnectionState::connected:
            return "Connected";
        case ConnectionState::reconnecting:
            return "Reconnecting";
    }
    return "Unknown";
}

MeshtasticClient::AdminTransactionLease::AdminTransactionLease(std::shared_ptr<SharedState> state,
                                                               std::uint64_t token,
                                                               std::uint32_t radio_number) noexcept
    : state_(std::move(state)), token_(token), radio_number_(radio_number)
{
}

MeshtasticClient::AdminTransactionLease::~AdminTransactionLease() { reset(); }

MeshtasticClient::AdminTransactionLease::AdminTransactionLease(AdminTransactionLease &&other) noexcept
    : state_(std::move(other.state_)), token_(std::exchange(other.token_, 0)),
      radio_number_(std::exchange(other.radio_number_, 0))
{
}

MeshtasticClient::AdminTransactionLease &MeshtasticClient::AdminTransactionLease::operator=(AdminTransactionLease &&other) noexcept
{
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        token_ = std::exchange(other.token_, 0);
        radio_number_ = std::exchange(other.radio_number_, 0);
    }
    return *this;
}

void MeshtasticClient::AdminTransactionLease::reset() noexcept
{
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->active == token_) {
            state_->active = 0;
            if (state_->has_writes) {
                state_->fenced = true;
                if (state_->transport) state_->transport->interrupt();
            }
            state_->has_writes = false;
        }
    }
    state_.reset();
    token_ = 0;
    radio_number_ = 0;
}

void MeshtasticClient::AdminTransactionLease::complete() noexcept
{
    if (!state_ || !token_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active == token_) {
        state_->active = 0;
        state_->has_writes = false;
    }
}

bool MeshtasticClient::AdminTransactionLease::valid() const noexcept
{
    if (!state_ || !token_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->active == token_;
}

void MeshtasticClient::AdminTransactionLease::abandon() noexcept
{
    if (!state_ || !token_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active != token_) return;
    // An unfinished write may already be in flight. Force the connection to
    // resynchronize before another transaction can take its place.
    state_->fenced = true;
    state_->active = 0;
    state_->has_writes = false;
    if (state_->transport) state_->transport->interrupt();
}

MeshtasticClient::MeshtasticClient(Endpoint endpoint, std::filesystem::path storage_path,
                                       std::chrono::milliseconds ack_timeout)
    : endpoint_(std::move(endpoint)), session_(std::make_unique<RadioSession>(ack_timeout)),
      transport_(std::make_unique<TcpTransport>())
{
    admin_transaction_state_->transport = transport_.get();
    auto seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    seed ^= static_cast<std::uint32_t>(::getpid());
    seed ^= static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this));
    next_packet_id_ = seed == 0 ? 1 : seed;
    snapshot_.endpoint = endpoint_.host + ":" + std::to_string(endpoint_.port);
    try {
        repository_ = std::make_unique<Repository>(storage_path.empty() ? Repository::default_path() : storage_path);
    } catch (const std::exception &error) {
        snapshot_.storage_error = std::string("Persistent storage unavailable: ") + error.what();
        repository_ = std::make_unique<Repository>(":memory:");
    }
    try { restore_repository(0); }
    catch(const std::exception &error) {
        snapshot_.storage_error=std::string("Unable to restore persistent data: ")+error.what();
        repository_=std::make_unique<Repository>(":memory:");session_key_.clear();restore_repository(0);
    }
}

MeshtasticClient::~MeshtasticClient()
{
    stop();
    std::lock_guard<std::mutex> lock(admin_transaction_state_->mutex);
    admin_transaction_state_->transport = nullptr;
}

void MeshtasticClient::start(bool enabled)
{
    if (!enabled) invalidate_admin_transaction();
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (worker_.joinable()) {
        enabled_ = enabled;
        control_changed_.notify_all();
        return;
    }
    enabled_        = enabled;
    stop_requested_ = false;
    worker_         = std::thread(&MeshtasticClient::worker, this);
}

void MeshtasticClient::stop()
{
    {
        // Publish the wait predicate under its mutex so an idle worker cannot
        // miss the notification between checking the predicate and waiting.
        std::lock_guard<std::mutex> lock(control_mutex_);
        stop_requested_ = true;
    }
    invalidate_admin_transaction();
    control_changed_.notify_all();
    // Closing the TCP session also disconnects the client. Do not wait for a
    // protocol write here: a stalled peer may have filled the send buffer.
    interrupt_socket();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
    {
        std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
        {
            std::lock_guard<std::mutex> control_lock(control_mutex_);
            pending_radio_.clear();
        }
        apply_deliveries(session_->interrupt("Client stopped before acknowledgement"));
    }
    set_state(ConnectionState::stopped);
}

void MeshtasticClient::set_enabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        enabled_ = enabled;
        if (!enabled) pending_radio_.clear();
    }
    control_changed_.notify_all();
    if (!enabled) {
        invalidate_admin_transaction();
        interrupt_socket();
        std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
        apply_deliveries(session_->interrupt("Connection disabled before acknowledgement"));
    }
}

bool MeshtasticClient::enabled() const
{
    std::lock_guard<std::mutex> lock(control_mutex_);
    return enabled_;
}

ClientSnapshot MeshtasticClient::snapshot() const
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
}

bool MeshtasticClient::send_text(std::string text, std::uint32_t channel)
{
    return send_text_to(std::move(text), kBroadcastNode, channel);
}

bool MeshtasticClient::send_text_to(std::string text, std::uint32_t destination, std::uint32_t channel)
{
    if (text.empty() || text.size() > kMaxTextPayload || destination == kLocalNode) return false;
    MeshPacket packet;
    packet.set_to(destination); packet.set_channel(channel); packet.set_id(next_id());
    packet.set_hop_limit(3); packet.set_priority(MeshPacket::RELIABLE); packet.set_want_ack(true);
    packet.mutable_decoded()->set_portnum(TEXT_MESSAGE_APP); packet.mutable_decoded()->set_payload(std::move(text));
    return send_packet(std::move(packet));
}

bool MeshtasticClient::request_full_config(std::uint32_t config_id)
{
    ToRadio request;
    request.set_want_config_id(config_id == 0 ? next_id() : config_id);
    return send_to_radio(request);
}

bool MeshtasticClient::send_heartbeat(std::uint32_t nonce)
{
    ToRadio request;
    request.mutable_heartbeat()->set_nonce(nonce);
    return send_to_radio(request);
}

bool MeshtasticClient::send_disconnect()
{
    ToRadio request;
    request.set_disconnect(true);
    return send_to_radio(request);
}

bool MeshtasticClient::send_xmodem(const XModem &message)
{
    ToRadio request;
    request.mutable_xmodempacket()->CopyFrom(message);
    return send_to_radio(request);
}

bool MeshtasticClient::send_mqtt_proxy(const MqttClientProxyMessage &message)
{
    ToRadio request;
    request.mutable_mqttclientproxymessage()->CopyFrom(message);
    return send_to_radio(request);
}

bool MeshtasticClient::enqueue_to_radio(ToRadio message)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    if (message.has_packet()) {
        if (!radio_number_) return false;
        auto *packet = message.mutable_packet();
        if (!packet->id()) packet->set_id(next_id());
        if (!packet->from()) packet->set_from(radio_number_);
        if (packet->has_decoded()) packet->set_want_ack(true);
    }
    if (encode_to_radio(message).empty()) return false;
    std::unique_lock<std::mutex> lock(control_mutex_);
    if (!enabled_ || stop_requested_ || pending_radio_.size() >= 4096) return false;
    std::optional<Message> queued_text;
    std::optional<DeliveryRecord> queued_delivery;
    try {
        if (message.has_packet()) {
            const auto &packet = message.packet();
            const auto previous=repository_->find_outbound(session_key_,packet.id());
            if(previous&&(previous->delivery.status==MessageStatus::queued||previous->delivery.status==MessageStatus::sent||previous->delivery.status==MessageStatus::acknowledged))return false;
            // A retry reuses the packet ID, but must not silently replace a
            // different historical packet and its delivery record.
            if(previous && previous->packet.SerializeAsString()!=packet.SerializeAsString()) return false;
            const auto delivery = session_->queued(packet,previous?previous->delivery.attempts:0);
            queued_delivery = delivery;
            queued_text=repository_->queue_packet(session_key_,packet,delivery);
        }
        pending_radio_.push_back(std::move(message));
        lock.unlock();
        if (queued_text || queued_delivery) {
            std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);
            if (queued_text) {
                auto &messages=snapshot_.messages;
                const auto existing=std::find_if(messages.begin(),messages.end(),[&](const Message &value){return value.local_id==queued_text->local_id;});
                if(existing==messages.end())messages.push_back(*queued_text);else *existing=*queued_text;
                if(messages.size()>200)messages.erase(messages.begin());
                const bool direct=queued_text->to!=0 && queued_text->to!=kBroadcastNode;
                const auto peer=direct?queued_text->to:0;
                auto &conversations=snapshot_.conversations;
                const auto found=std::find_if(conversations.begin(),conversations.end(),[&](const Conversation &value){return value.direct==direct&&value.peer==peer&&value.channel==queued_text->channel;});
                if(found==conversations.end())conversations.insert(conversations.begin(),{direct,peer,queued_text->channel,*queued_text,0});
                else if(found->latest.local_id<=queued_text->local_id)found->latest=*queued_text;
            }
            if(queued_delivery){
                const auto existing=std::find_if(snapshot_.deliveries.begin(),snapshot_.deliveries.end(),[&](const DeliveryRecord &value){return value.packet_id==queued_delivery->packet_id;});
                if(existing==snapshot_.deliveries.end())snapshot_.deliveries.push_back(*queued_delivery);else *existing=*queued_delivery;
                while (snapshot_.deliveries.size() > 200) {
                    const auto completed = std::find_if(snapshot_.deliveries.begin(), snapshot_.deliveries.end(),
                        [](const DeliveryRecord &value) {
                            return value.status != MessageStatus::queued && value.status != MessageStatus::sent;
                        });
                    if (completed == snapshot_.deliveries.end()) break;
                    snapshot_.deliveries.erase(completed);
                }
            }
            ++snapshot_.revision;
        }
        if(!queued_text && queued_delivery && (queued_delivery->portnum==WAYPOINT_APP||queued_delivery->portnum==TRACEROUTE_APP))refresh_persistent_snapshot();
    } catch (const std::exception &error) {
        if (lock.owns_lock()) lock.unlock();
        std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);
        snapshot_.storage_error=error.what(); ++snapshot_.revision; return false;
    }
    control_changed_.notify_all(); return true;
}

bool MeshtasticClient::send_to_radio(const ToRadio &message)
{
    return enqueue_to_radio(message);
}

bool MeshtasticClient::send_packet(MeshPacket packet)
{
    ToRadio message;
    message.mutable_packet()->CopyFrom(packet);
    return send_to_radio(message);
}

bool MeshtasticClient::send_data(PortNum port, std::string payload, std::uint32_t destination,
                                 std::uint32_t channel, std::uint32_t hop_limit, MeshPacket::Priority priority,
                                 std::uint32_t packet_id)
{
    if (payload.size() > kMaxRadioPayload) return false;
    MeshPacket packet;
    packet.set_to(destination);
    packet.set_channel(channel);
    packet.set_id(packet_id);
    packet.set_hop_limit(hop_limit);
    packet.set_priority(priority);
    packet.mutable_decoded()->set_portnum(port);
    packet.mutable_decoded()->set_payload(std::move(payload));
    return send_packet(std::move(packet));
}

bool MeshtasticClient::send_admin(const AdminMessage &message, std::uint32_t destination, std::uint32_t channel,
                                  std::uint32_t hop_limit, std::uint32_t packet_id)
{
    std::string payload;
    if (!message.SerializeToString(&payload) || payload.empty() || payload.size() > kMaxRadioPayload) return false;
    MeshPacket packet;
    packet.set_to(resolve_destination(destination));
    packet.set_channel(channel);
    packet.set_hop_limit(hop_limit);
    packet.set_priority(MeshPacket::RELIABLE);
    packet.set_id(packet_id == 0 ? next_id() : packet_id);
    auto *data = packet.mutable_decoded();
    data->set_portnum(ADMIN_APP);
    data->set_payload(std::move(payload));
    data->set_want_response(admin_is_get(message));
    return send_packet(std::move(packet));
}

std::optional<std::uint32_t> MeshtasticClient::queue_admin_tracked(const AdminMessage &source,
                                                                  std::uint32_t destination)
{
    AdminMessage message=source;
    if (admin_is_get(message)) message.clear_session_passkey();
    if (!admin_is_get(message) && !attach_session_passkey(message,destination)) return std::nullopt;
    const auto packet_id=next_id();
    return send_admin(message,destination,0,3,packet_id) ? std::optional<std::uint32_t>(packet_id) : std::nullopt;
}

std::optional<MeshtasticClient::AdminTransactionLease>
MeshtasticClient::acquire_admin_transaction(std::string &error, std::uint32_t expected_radio)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    std::lock_guard<std::mutex> transaction_lock(admin_transaction_state_->mutex);
    std::scoped_lock connection_lock(control_mutex_, snapshot_mutex_);
    if (admin_transaction_state_->active != 0) {
        error = "Radio settings are busy; another update is in progress";
        return std::nullopt;
    }
    if (admin_transaction_state_->fenced) {
        error = "The radio is reconnecting after an interrupted settings update";
        return std::nullopt;
    }
    if (!enabled_ || stop_requested_ || !identity_confirmed_ || radio_number_ == 0 ||
        snapshot_.state != ConnectionState::connected || !snapshot_.config_complete ||
        snapshot_.my_node_number != radio_number_ || snapshot_.session_passkey.empty()) {
        error = "Connect and synchronize the radio, then obtain an admin key before saving";
        return std::nullopt;
    }
    if (expected_radio && expected_radio != radio_number_) {
        error = "Radio identity changed; read its current settings before saving";
        return std::nullopt;
    }
    auto &next = admin_transaction_state_->next;
    if (++next == 0) ++next;
    admin_transaction_state_->active = next;
    admin_transaction_state_->has_writes = false;
    error.clear();
    return AdminTransactionLease(admin_transaction_state_, next, radio_number_);
}

std::optional<std::uint32_t> MeshtasticClient::queue_admin_tracked(const AdminMessage &message,
                                                                  const AdminTransactionLease &lease)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    if (lease.state_ != admin_transaction_state_) return std::nullopt;
    std::lock_guard<std::mutex> transaction_lock(admin_transaction_state_->mutex);
    if (!lease.token_ || admin_transaction_state_->active != lease.token_) return std::nullopt;
    {
        std::scoped_lock connection_lock(control_mutex_, snapshot_mutex_);
        if (!enabled_ || stop_requested_ || !identity_confirmed_ || radio_number_ != lease.radio_number_ ||
            snapshot_.state != ConnectionState::connected || !snapshot_.config_complete ||
            snapshot_.my_node_number != lease.radio_number_ || snapshot_.session_passkey.empty())
            return std::nullopt;
    }
    const auto queued = queue_admin_tracked(message, kLocalNode);
    if (queued && !admin_is_get(message)) admin_transaction_state_->has_writes = true;
    return queued;
}

void MeshtasticClient::invalidate_admin_transaction() noexcept
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    std::lock_guard<std::mutex> transaction_lock(admin_transaction_state_->mutex);
    admin_transaction_state_->active = 0;
    admin_transaction_state_->has_writes = false;
    admin_transaction_state_->fenced = false;
}

bool MeshtasticClient::request_channel(std::uint32_t index, std::uint32_t destination)
{
    AdminMessage request;
    if (index == std::numeric_limits<std::uint32_t>::max()) return false;
    request.set_get_channel_request(index + 1U);
    return send_admin(request, destination);
}

bool MeshtasticClient::request_owner(std::uint32_t destination)
{
    AdminMessage request;
    request.set_get_owner_request(true);
    return send_admin(request, destination);
}

bool MeshtasticClient::request_config(AdminMessage::ConfigType type, std::uint32_t destination)
{
    AdminMessage request;
    request.set_get_config_request(type);
    return send_admin(request, resolve_destination(destination));
}

bool MeshtasticClient::request_session_key(std::uint32_t destination)
{
    return request_config(AdminMessage::SESSIONKEY_CONFIG, destination);
}

bool MeshtasticClient::request_module_config(AdminMessage::ModuleConfigType type, std::uint32_t destination)
{
    AdminMessage request;
    request.set_get_module_config_request(type);
    return send_admin(request, destination);
}

bool MeshtasticClient::request_metadata(std::uint32_t destination)
{
    AdminMessage request;
    request.set_get_device_metadata_request(true);
    return send_admin(request, destination);
}

bool MeshtasticClient::set_owner(User owner, std::uint32_t destination)
{
    AdminMessage request;
    request.mutable_set_owner()->CopyFrom(owner);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request, resolve_destination(destination));
}

bool MeshtasticClient::set_config(Config config, std::uint32_t destination)
{
    AdminMessage request;
    request.mutable_set_config()->CopyFrom(config);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request, resolve_destination(destination));
}

bool MeshtasticClient::set_lora_region(Config::LoRaConfig::RegionCode region, std::uint32_t destination)
{
    Config config;
    config.mutable_lora()->set_region(region);
    return set_config(std::move(config), destination);
}

bool MeshtasticClient::set_module_config(ModuleConfig config, std::uint32_t destination)
{
    AdminMessage request;
    request.mutable_set_module_config()->CopyFrom(config);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request, resolve_destination(destination));
}

bool MeshtasticClient::begin_edit_settings(std::uint32_t destination)
{
    AdminMessage request;
    request.set_begin_edit_settings(true);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request, resolve_destination(destination));
}

bool MeshtasticClient::commit_edit_settings(std::uint32_t destination)
{
    AdminMessage request;
    request.set_commit_edit_settings(true);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request, resolve_destination(destination));
}

std::uint32_t MeshtasticClient::resolve_destination(std::uint32_t destination) const
{
    if (destination != kLocalNode) return destination;
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_.my_node_number;
}

bool MeshtasticClient::attach_session_passkey(AdminMessage &message, std::uint32_t destination) const
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (destination == kLocalNode || (snapshot_.my_node_number && destination == snapshot_.my_node_number)) {
        if (!snapshot_.session_passkey.empty()) message.set_session_passkey(snapshot_.session_passkey);
        return true;
    }
    const auto found = remote_session_passkeys_.find(destination);
    if (found == remote_session_passkeys_.end() || found->second.empty()) return false;
    message.set_session_passkey(found->second);
    return true;
}

void MeshtasticClient::set_session_passkey(std::string passkey)
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_.session_passkey = std::move(passkey);
    ++snapshot_.revision;
}

bool MeshtasticClient::set_remote_session_passkey(std::uint32_t node_number, std::string passkey)
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (node_number == kLocalNode || node_number == kBroadcastNode ||
        node_number == snapshot_.my_node_number) return false;
    if (passkey.empty()) remote_session_passkeys_.erase(node_number);
    else remote_session_passkeys_[node_number] = std::move(passkey);
    return true;
}

std::vector<RadioUpdate> MeshtasticClient::drain_from_radio()
{
    std::lock_guard<std::mutex> lock(event_mutex_);
    std::vector<RadioUpdate> events;
    events.reserve(events_.size());
    while (!events_.empty()) {
        events.push_back(std::move(events_.front()));
        events_.pop_front();
    }
    return events;
}

void MeshtasticClient::restore_repository(std::uint32_t radio_number)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    if (!session_key_.empty() && radio_number && radio_number!=radio_number_) {
        invalidate_admin_transaction();
        { std::lock_guard<std::mutex> lock(control_mutex_); pending_radio_.clear(); }
        for (const auto &delivery:session_->interrupt("Connected radio identity changed")) repository_->update_delivery(session_key_,delivery);
    }
    const auto key=repository_->activate(endpoint_.host+":"+std::to_string(endpoint_.port),radio_number);
    repository_->recover_interrupted(key);
    const auto state=repository_->load_state(key);
    session_key_=key;radio_number_=state.radio_number;
    std::vector<DeliveryRecord> deliveries;
    for(const auto &outbound:repository_->outbound(key)) deliveries.push_back(outbound.delivery);
    session_->reset(deliveries);
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        const auto revision=snapshot_.revision+1;
        const auto connection_state=snapshot_.state;
        const auto endpoint=snapshot_.endpoint;
        const auto storage_error=snapshot_.storage_error;
        remote_session_passkeys_.clear();
        admin_response_order_.clear();
        snapshot_=ClientSnapshot{};
        snapshot_.revision=revision;snapshot_.state=connection_state;snapshot_.endpoint=endpoint;snapshot_.storage_error=storage_error;
        snapshot_.my_node_number=radio_number_;snapshot_.nodes=state.nodes;snapshot_.channels=state.channels;snapshot_.channel_configs=state.channel_configs;
        snapshot_.device_configs=state.device_configs;snapshot_.module_configs=state.module_configs;
        snapshot_.owner=state.owner;snapshot_.metadata=state.metadata;
        for(const auto &[category,config]:state.device_configs) {
            snapshot_.device_config_revisions[category]=0;
            if(config.has_lora())snapshot_.lora_config=config.lora();
            if(config.has_device())snapshot_.device_config=config.device();
        }
        for(const auto &[category,config]:state.module_configs) { (void)config;snapshot_.module_config_revisions[category]=0; }
    }
    refresh_persistent_snapshot();
}

void MeshtasticClient::refresh_persistent_snapshot()
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    try {
        auto messages=repository_->recent_messages(session_key_,200);
        auto conversations=repository_->conversations(session_key_);
        auto positions=repository_->positions(session_key_,0,0,200);
        auto telemetry=repository_->telemetry(session_key_,0,0,100);
        auto waypoints=repository_->waypoints(session_key_,200);
        auto routes=repository_->routes(session_key_,0,100);
        auto deliveries=session_->records();
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.messages=std::move(messages);snapshot_.conversations=std::move(conversations);
        snapshot_.positions=std::move(positions);snapshot_.telemetry=std::move(telemetry);
        snapshot_.waypoints=std::move(waypoints);snapshot_.routes=std::move(routes);snapshot_.deliveries=std::move(deliveries);
        ++snapshot_.revision;
    }catch(const std::exception &error){std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.storage_error=error.what();++snapshot_.revision;}
}
void MeshtasticClient::apply_delivery(const DeliveryRecord &delivery) { apply_deliveries({delivery}); }
void MeshtasticClient::apply_deliveries(const std::vector<DeliveryRecord> &deliveries)
{
    if(deliveries.empty())return;
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    try { repository_->update_deliveries(session_key_,deliveries); }
    catch(const std::exception &error){std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.storage_error=error.what();++snapshot_.revision;}
    std::map<std::uint32_t,const DeliveryRecord*> changed;
    for(const auto &delivery:deliveries)changed[delivery.packet_id]=&delivery;
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    const auto change_message=[&](Message &message){const auto found=changed.find(message.id);if(message.outgoing&&found!=changed.end()){const auto &delivery=*found->second;message.status=delivery.status;message.status_detail=delivery.detail;message.attempts=delivery.attempts;}};
    for(auto &message:snapshot_.messages)change_message(message);
    for(auto &conversation:snapshot_.conversations)change_message(conversation.latest);
    snapshot_.deliveries=session_->records();
    ++snapshot_.revision;
}

std::vector<Message> MeshtasticClient::query_messages(bool direct,std::uint32_t peer,std::uint32_t channel,std::int64_t before,std::size_t limit) const
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    try{return repository_->query_messages(session_key_,direct,peer,channel,before,limit);}catch(const std::exception &){return {};}
}
std::vector<Conversation> MeshtasticClient::query_conversations() const
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    try{return repository_->conversations(session_key_);}catch(const std::exception &){return {};}
}
bool MeshtasticClient::mark_conversation_read(bool direct,std::uint32_t peer,std::uint32_t channel)
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    try{const bool changed=repository_->mark_read(session_key_,direct,peer,channel);if(changed)refresh_persistent_snapshot();return changed;}
    catch(const std::exception &error){std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);snapshot_.storage_error=error.what();++snapshot_.revision;return false;}
}
bool MeshtasticClient::retry_message(std::uint32_t packet_id)
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    const auto state=snapshot();
    if(state.state!=ConnectionState::connected || !state.config_complete)return false;
    try {
        const auto outgoing=repository_->find_outbound(session_key_,packet_id);
        if(!outgoing)return false;
        const auto status=outgoing->delivery.status;
        if(status!=MessageStatus::failed && status!=MessageStatus::rejected && status!=MessageStatus::timed_out)return false;
        ToRadio message;message.mutable_packet()->CopyFrom(outgoing->packet);return enqueue_to_radio(std::move(message));
    }catch(const std::exception &){return false;}
}
bool MeshtasticClient::retry_message_by_local_id(std::int64_t local_id)
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    try{const auto id=repository_->message_packet_id(session_key_,local_id);return id&&retry_message(*id);}catch(const std::exception &){return false;}
}
bool MeshtasticClient::request_traceroute(std::uint32_t destination,std::uint32_t channel)
{
    if(destination==0 || destination==kBroadcastNode)return false;
    MeshPacket packet;packet.set_to(destination);packet.set_channel(channel);packet.set_id(next_id());packet.set_hop_limit(7);packet.set_priority(MeshPacket::RELIABLE);
    auto *data=packet.mutable_decoded();data->set_portnum(TRACEROUTE_APP);data->set_dest(destination);data->set_want_response(true);data->set_payload(RouteDiscovery{}.SerializeAsString());
    return send_packet(std::move(packet));
}
bool MeshtasticClient::request_telemetry(std::uint32_t destination,std::uint32_t channel)
{
    if(destination==0 || destination==kBroadcastNode)return false;
    Telemetry telemetry;
    MeshPacket packet;packet.set_to(destination);packet.set_channel(channel);packet.set_id(next_id());packet.set_hop_limit(3);packet.set_priority(MeshPacket::RELIABLE);
    auto *data=packet.mutable_decoded();data->set_portnum(TELEMETRY_APP);data->set_want_response(true);data->set_payload(telemetry.SerializeAsString());
    return send_packet(std::move(packet));
}
bool MeshtasticClient::send_waypoint(const Waypoint &source,std::uint32_t destination,std::uint32_t channel)
{
    Waypoint value=source;
    if(!value.has_latitude_i() || !value.has_longitude_i() || value.name().empty() || value.name().size()>30 || value.description().size()>100)return false;
    if(value.latitude_i() < -900000000 || value.latitude_i()>900000000 || value.longitude_i() < -1800000000 || value.longitude_i()>1800000000)return false;
    if(!value.id())value.set_id(next_id());
    return send_data(WAYPOINT_APP,value.SerializeAsString(),destination,channel);
}
bool MeshtasticClient::set_channel(const Channel &channel,std::uint32_t destination)
{
    if(channel.index()<0 || channel.index()>7)return false;
    AdminMessage request;request.mutable_set_channel()->CopyFrom(channel);
    if (!attach_session_passkey(request, destination)) return false;
    return send_admin(request,destination);
}
std::vector<PositionRecord> MeshtasticClient::query_positions(std::uint32_t node,std::int64_t before,std::size_t limit) const
{std::lock_guard<std::recursive_mutex> lock(repository_mutex_);try{return repository_->positions(session_key_,node,before,limit);}catch(const std::exception &){return {};}}
std::vector<TelemetryRecord> MeshtasticClient::query_telemetry(std::uint32_t node,std::int64_t before,std::size_t limit) const
{std::lock_guard<std::recursive_mutex> lock(repository_mutex_);try{return repository_->telemetry(session_key_,node,before,limit);}catch(const std::exception &){return {};}}
std::vector<WaypointRecord> MeshtasticClient::query_waypoints(std::size_t limit) const
{std::lock_guard<std::recursive_mutex> lock(repository_mutex_);try{return repository_->waypoints(session_key_,limit);}catch(const std::exception &){return {};}}
std::vector<RouteRecord> MeshtasticClient::query_routes(std::uint32_t node,std::size_t limit) const
{std::lock_guard<std::recursive_mutex> lock(repository_mutex_);try{return repository_->routes(session_key_,node,limit);}catch(const std::exception &){return {};}}
std::optional<MapHistory> MeshtasticClient::query_map_history(const std::string &expected_endpoint,
                                                             std::uint32_t expected_radio) const
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    {
        std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);
        if (expected_endpoint != snapshot_.endpoint || expected_radio != radio_number_ ||
            expected_radio != snapshot_.my_node_number || session_key_.empty()) return std::nullopt;
    }
    try {
        MapHistory result;
        result.positions = repository_->positions(session_key_, 0, 0, 10000);
        result.waypoints = repository_->waypoints(session_key_, 10000);
        result.routes = repository_->routes(session_key_, 0, 100);
        return result;
    } catch (const std::exception &error) {
        std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);
        snapshot_.storage_error = std::string("Unable to load map history: ") + error.what();
        ++snapshot_.revision;
        return std::nullopt;
    }
}

bool MeshtasticClient::connection_requested() const
{
    return !stop_requested_ && enabled();
}

int MeshtasticClient::connect_socket(std::string &error) const
{
    return transport_->connect(endpoint_.host, endpoint_.port, [this] { return connection_requested(); }, error);
}

bool MeshtasticClient::send_bytes(int socket, const std::uint8_t *data, std::size_t size)
{
    return transport_->write(socket, data, size, [this] { return connection_requested(); });
}

bool MeshtasticClient::send_payload(int socket, const std::vector<std::uint8_t> &payload)
{
    const auto frame = frame_payload(payload);
    return !frame.empty() && send_bytes(socket, frame.data(), frame.size());
}

bool MeshtasticClient::begin_config(int socket, std::uint32_t &config_id)
{
    {std::lock_guard<std::recursive_mutex> lock(repository_mutex_);invalidate_admin_transaction();identity_confirmed_=false;preidentity_updates_.clear();}
    const std::array<std::uint8_t, 32> wake_bytes = [] {
        std::array<std::uint8_t, 32> bytes{};
        bytes.fill(kFrameStart2);
        return bytes;
    }();
    if (!send_bytes(socket, wake_bytes.data(), wake_bytes.size())) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    config_id = next_id();
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        snapshot_.my_node_number  = 0;
        snapshot_.my_info.reset();
        snapshot_.config_complete = false;

        snapshot_.local_stats.reset();
        snapshot_.owner.reset();

        snapshot_.session_passkey.clear();
        remote_session_passkeys_.clear();
        snapshot_.admin_responses.clear();
        admin_response_order_.clear();
        snapshot_.last_admin.reset();
        snapshot_.last_from_radio.reset();
        snapshot_.error.clear();
        snapshot_.state = ConnectionState::synchronizing;
        ++snapshot_.revision;
    }
    return send_payload(socket, encode_want_config(config_id));
}

std::uint32_t MeshtasticClient::next_id()
{
    std::lock_guard<std::recursive_mutex> lock(repository_mutex_);
    for (;;) {
        const auto id=next_packet_id_.fetch_add(1);
        if (id && (!repository_ || !repository_->find_outbound(session_key_,id))) return id;
    }
}

void MeshtasticClient::apply_from_radio(const RadioUpdate &message, std::uint32_t config_id)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    if(!identity_confirmed_ && !message.my_node_number) {
        if(preidentity_updates_.size()<256)preidentity_updates_.push_back(message);
        return;
    }
    if(message.my_node_number && *message.my_node_number)identity_confirmed_=true;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events_.push_back(message);
        if (events_.size() > 256) events_.pop_front();
    }
    try {
        if (message.my_node_number && *message.my_node_number != radio_number_) restore_repository(*message.my_node_number);
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (message.raw) snapshot_.last_from_radio = *message.raw;
        if (message.my_node_number) snapshot_.my_node_number = *message.my_node_number;
        if (message.my_info) snapshot_.my_info = *message.my_info;
        const auto save_config = [&](const Config &config) {
            snapshot_.config=config;
            snapshot_.device_configs[config.payload_variant_case()]=config;
            snapshot_.device_config_revisions[config.payload_variant_case()]=snapshot_.revision+1;
            if (config.has_lora()) snapshot_.lora_config=config.lora();
            if (config.has_device()) snapshot_.device_config=config.device();
            repository_->store_config(session_key_,config);
        };
        const auto save_module = [&](const ModuleConfig &config) {
            snapshot_.module_config=config;
            snapshot_.module_configs[config.payload_variant_case()]=config;
            snapshot_.module_config_revisions[config.payload_variant_case()]=snapshot_.revision+1;
            repository_->store_module_config(session_key_,config);
        };
        const auto save_channel = [&](const Channel &raw) {
            snapshot_.channel_configs[raw.index()]=raw;
            snapshot_.channel_revisions[raw.index()]=snapshot_.revision+1;
            ChannelInfo channel; channel.index=raw.index();channel.role=raw.role();channel.name=raw.settings().name();
            const auto &key=raw.settings().psk();channel.encrypted=!key.empty()&&!(key.size()==1&&key.front()==0);
            if(channel.name.empty())channel.name=channel.index==0?"LongFast":"Channel "+std::to_string(channel.index);
            const auto found=std::find_if(snapshot_.channels.begin(),snapshot_.channels.end(),[&](const ChannelInfo &value){return value.index==channel.index;});
            if(raw.role()==Channel::DISABLED) { if(found!=snapshot_.channels.end())snapshot_.channels.erase(found); }
            else if(found==snapshot_.channels.end())snapshot_.channels.push_back(channel);else *found=channel;
            repository_->store_channel_config(session_key_,raw);
        };
        const auto save_node = [&](Node node) {
            const auto found=std::find_if(snapshot_.nodes.begin(),snapshot_.nodes.end(),[&](const Node &value){return value.number==node.number;});
            if(found==snapshot_.nodes.end())snapshot_.nodes.push_back(node);else *found=node;
            repository_->store_node(session_key_,node);
        };
        if(message.config)save_config(*message.config);
        if(message.module_config)save_module(*message.module_config);
        if(message.metadata){snapshot_.metadata=*message.metadata;repository_->store_metadata(session_key_,*message.metadata);}
        if(message.local_stats)snapshot_.local_stats=*message.local_stats;
        if(message.node) {
            save_node(*message.node);
            if(message.raw && message.raw->has_node_info() && message.raw->node_info().has_position()) {
                PositionRecord record;record.node_number=message.node->number;record.received_at=message.node->last_heard;record.position=message.raw->node_info().position();repository_->store_position(session_key_,record);
            }
        }
        if(message.raw && message.raw->has_channel())save_channel(message.raw->channel());
        if(message.text_message) {
            auto text=*message.text_message;
            // A local radio echo belongs to the existing outgoing record.
            const auto outgoing=repository_->find_outbound(session_key_,text.id);
            if(!(outgoing && text.from==radio_number_)) {
                text.status=MessageStatus::received;
                if(!text.received_at)text.received_at=static_cast<std::uint32_t>(std::time(nullptr));
                repository_->store_message(session_key_,std::move(text));
            }
        }
        if(message.packet) {
            const auto &update=*message.packet;
            const auto &packet=update.packet;
            const auto time=packet.has_rx_time()?packet.rx_time():static_cast<std::uint32_t>(std::time(nullptr));
            if(update.routing) {
                if(const auto delivery=session_->routing(packet,*update.routing))repository_->update_delivery(session_key_,*delivery);
                if(update.routing->has_route_reply()) {
                    RouteRecord record;record.from=packet.from();record.to=packet.to();record.packet_id=packet.id();record.request_id=packet.decoded().request_id();record.received_at=time;record.route=update.routing->route_reply();repository_->store_route(session_key_,record);
                }
            }
            if(update.admin) {
                const auto &admin=*update.admin;snapshot_.last_admin=admin;
                if(packet.has_decoded() && packet.decoded().request_id()!=0) {
                    const auto request_id=packet.decoded().request_id();
                    const auto outbound=repository_->find_outbound(session_key_,request_id);
                    if(outbound && outbound->packet.to()==radio_number_ && outbound->packet.has_decoded() &&
                       outbound->packet.decoded().portnum()==ADMIN_APP &&
                       outbound->packet.decoded().want_response() &&
                       (packet.from()==radio_number_ ||
                        (packet.from()==0 && outbound->packet.to()==radio_number_ &&
                         (packet.to()==0 || packet.to()==radio_number_)))) {
                        AdminMessage request;
                        if(request.ParseFromString(outbound->packet.decoded().payload()) &&
                           admin_response_matches_request(request,admin)) {
                            if(snapshot_.admin_responses.count(request_id)==0)admin_response_order_.push_back(request_id);
                            snapshot_.admin_responses[request_id]=admin;
                            while(admin_response_order_.size()>128) {
                                snapshot_.admin_responses.erase(admin_response_order_.front());
                                admin_response_order_.pop_front();
                            }
                        }
                    }
                }
                const bool local_admin_response =
                    packet.from()==radio_number_ ||
                    (packet.from()==0 && (packet.to()==radio_number_ || packet.to()==0));
                if(local_admin_response) {
                    if(!admin.session_passkey().empty())snapshot_.session_passkey=admin.session_passkey();
                    if(admin.has_get_config_response())save_config(admin.get_config_response());
                    if(admin.has_get_module_config_response())save_module(admin.get_module_config_response());
                    if(admin.has_get_channel_response())save_channel(admin.get_channel_response());
                    if(admin.has_get_owner_response()){snapshot_.owner=admin.get_owner_response();repository_->store_owner(session_key_,*snapshot_.owner);}
                    if(admin.has_get_device_metadata_response()){snapshot_.metadata=admin.get_device_metadata_response();repository_->store_metadata(session_key_,*snapshot_.metadata);}
                } else if (packet.from()!=0 && !admin.session_passkey().empty() && packet.has_decoded() &&
                           packet.decoded().request_id()!=0) {
                    const auto outbound=repository_->find_outbound(session_key_,packet.decoded().request_id());
                    if (outbound && outbound->packet.to()==packet.from() && outbound->packet.has_decoded() &&
                        outbound->packet.decoded().portnum()==ADMIN_APP && outbound->packet.decoded().want_response())
                        remote_session_passkeys_[packet.from()]=admin.session_passkey();
                }
            }
            if (packet.from()!=0) {
            const auto existing=std::find_if(snapshot_.nodes.begin(),snapshot_.nodes.end(),[&](const Node &node){return node.number==packet.from();});
            Node node=existing==snapshot_.nodes.end()?Node{}:*existing;node.number=packet.from();
            if(node.id.empty()){char id[10]{};std::snprintf(id,sizeof(id),"!%08x",node.number);node.id=id;node.long_name=id;}
            node.last_heard=time;node.snr=packet.rx_snr();
            if(update.position) {
                const auto &position=*update.position;
                if(position.has_latitude_i())node.latitude=position.latitude_i()*1e-7;
                if(position.has_longitude_i())node.longitude=position.longitude_i()*1e-7;
                if(position.has_altitude())node.altitude=position.altitude();
                PositionRecord record;record.node_number=packet.from();record.packet_id=packet.id();record.received_at=time;record.position=position;repository_->store_position(session_key_,record);
                save_node(node);
            }
            if(update.telemetry) {
                TelemetryRecord record;record.node_number=packet.from();record.packet_id=packet.id();record.received_at=time;record.telemetry=*update.telemetry;repository_->store_telemetry(session_key_,record);
                if(update.telemetry->has_device_metrics()){
                    const auto &metrics=update.telemetry->device_metrics();
                    if(metrics.has_battery_level())node.battery_level=metrics.battery_level();
                    if(metrics.has_voltage())node.voltage=metrics.voltage();
                    if(metrics.has_uptime_seconds())node.uptime_seconds=metrics.uptime_seconds();
                    save_node(node);
                }
            }
            if(update.waypoint){WaypointRecord record;record.sender=packet.from();record.packet_id=packet.id();record.received_at=time;record.waypoint=*update.waypoint;repository_->store_waypoint(session_key_,record);}
            if(update.route){RouteRecord record;record.from=packet.from();record.to=packet.to();record.packet_id=packet.id();record.request_id=packet.decoded().request_id();record.received_at=time;record.route=*update.route;repository_->store_route(session_key_,record);}
            }
        }
        if(message.config_complete_id && *message.config_complete_id==config_id){snapshot_.config_complete=true;snapshot_.state=ConnectionState::connected;snapshot_.error.clear();}
        ++snapshot_.revision;
    } catch(const std::exception &error) {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);snapshot_.storage_error=error.what();++snapshot_.revision;
    }
    refresh_persistent_snapshot();
    if(identity_confirmed_ && !preidentity_updates_.empty()) {
        auto pending=std::move(preidentity_updates_);preidentity_updates_.clear();
        for(const auto &update:pending)apply_from_radio(update,config_id);
    }
}

void MeshtasticClient::set_state(ConnectionState state, std::string error)
{
    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
    std::lock_guard<std::mutex> transaction_lock(admin_transaction_state_->mutex);
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_.state = state;
    snapshot_.error = std::move(error);
    if (state == ConnectionState::stopped || state == ConnectionState::reconnecting) {
        snapshot_.config_complete = false;
        snapshot_.admin_responses.clear();
        admin_response_order_.clear();
        remote_session_passkeys_.clear();
    }
    if (state != ConnectionState::connected) {
        admin_transaction_state_->active = 0;
        admin_transaction_state_->has_writes = false;
        admin_transaction_state_->fenced = false;
    }
    ++snapshot_.revision;
}

void MeshtasticClient::interrupt_socket() { transport_->interrupt(); }
void MeshtasticClient::close_socket(int socket) { transport_->close(socket); }

void MeshtasticClient::wait_to_reconnect()
{
    std::unique_lock<std::mutex> lock(control_mutex_);
    control_changed_.wait_for(lock, kReconnectDelay,
                              [&] { return stop_requested_ || !enabled_; });
}

void MeshtasticClient::worker()
{
    while (!stop_requested_) {
        {
            std::unique_lock<std::mutex> lock(control_mutex_);
            control_changed_.wait(lock, [&] { return stop_requested_ || enabled_; });
            if (stop_requested_) break;
        }

        set_state(ConnectionState::connecting);
        std::string error;
        const int connected_socket = connect_socket(error);
        if (connected_socket < 0) {
            if (connection_requested()) {
                set_state(ConnectionState::reconnecting, std::move(error));
                wait_to_reconnect();
            } else if (!stop_requested_) {
                set_state(ConnectionState::stopped);
            }
            continue;
        }
        if (!connection_requested()) {
            close_socket(connected_socket);
            if (!stop_requested_) set_state(ConnectionState::stopped);
            continue;
        }

        FrameParser parser;
        std::uint32_t config_id = 0;
        if (!begin_config(connected_socket, config_id)) {
            close_socket(connected_socket);
            set_state(ConnectionState::reconnecting, "failed to start config sync");
            wait_to_reconnect();
            continue;
        }

        bool connection_alive = true;
        bool config_synchronized = false;
        auto config_deadline = std::chrono::steady_clock::now() + kConfigSyncTimeout;
        std::uint32_t heartbeat_nonce = 2;
        auto next_heartbeat = std::chrono::steady_clock::now() + kHeartbeatInterval;
        while (connection_alive && !stop_requested_) {
            while (!stop_requested_) {
                ToRadio pending;
                {
                    std::lock_guard<std::mutex> lock(control_mutex_);
                    if (!enabled_) {
                        connection_alive = false;
                        break;
                    }
                    if (pending_radio_.empty() || !config_synchronized) break;
                    pending = std::move(pending_radio_.front());
                    pending_radio_.pop_front();
                }
                const auto encoded = encode_to_radio(pending);
                if (encoded.empty() || !send_payload(connected_socket, encoded)) {
                    connection_alive = false;
                    error = "failed to write packet";
                    break;
                }
                if (pending.has_packet()) {
                    std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
                    if (const auto delivery=session_->sent(pending.packet().id(),static_cast<std::uint32_t>(std::time(nullptr)))) apply_delivery(*delivery);
                }
            }
            if (!connection_alive || stop_requested_) break;
            {
                std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
                apply_deliveries(session_->expire());
            }

            if (!config_synchronized && std::chrono::steady_clock::now() >= config_deadline) {
                connection_alive = false;
                error            = "timed out waiting for radio configuration";
                break;
            }

            if (std::chrono::steady_clock::now() >= next_heartbeat) {
                const auto heartbeat = encode_heartbeat(heartbeat_nonce++);
                if (heartbeat.empty() || !send_payload(connected_socket, heartbeat)) {
                    connection_alive = false;
                    error            = "failed to send heartbeat";
                    break;
                }
                next_heartbeat = std::chrono::steady_clock::now() + kHeartbeatInterval;
            }

            std::array<std::uint8_t, 1024> received{};
            const auto count = transport_->read(connected_socket, received.data(), received.size());
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
            if (count <= 0) {
                connection_alive = false;
                if (count < 0)
                    error = socket_error("receive");
                else
                    error = "connection closed by meshtasticd";
                break;
            }

            for (const auto &frame : parser.feed(received.data(), static_cast<std::size_t>(count))) {
                RadioUpdate message;
                std::string decode_error;
                if (!decode_from_radio(frame, message, decode_error)) {
                    std::fprintf(stderr, "[meshtastic] dropped frame: %s\n", decode_error.c_str());
                    continue;
                }
                apply_from_radio(message, config_id);
                {std::lock_guard<std::mutex> lock(snapshot_mutex_);config_synchronized=snapshot_.config_complete;}
                if (message.rebooted) {
                    parser.reset();
                    if (!begin_config(connected_socket, config_id)) {
                        error            = "failed to restart config sync after reboot";
                        connection_alive = false;
                        break;
                    }
                    config_synchronized = false;
                    config_deadline = std::chrono::steady_clock::now() + kConfigSyncTimeout;
                }
            }
        }

        close_socket(connected_socket);
        {
            std::lock_guard<std::recursive_mutex> repository_lock(repository_mutex_);
            { std::lock_guard<std::mutex> lock(control_mutex_); pending_radio_.clear(); }
            apply_deliveries(session_->interrupt(error.empty() ? "TCP session closed" : error));
        }
        if (connection_requested()) {
            set_state(ConnectionState::reconnecting, std::move(error));
            wait_to_reconnect();
        } else {
            set_state(ConnectionState::stopped);
        }
    }
}

}  // namespace meshtastic
