#ifndef LILYGO_UI_MESHTASTIC_CLIENT_HPP
#define LILYGO_UI_MESHTASTIC_CLIENT_HPP

#include "domain/meshtastic_protocol.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace meshtastic { class Repository; class RadioSession; class TcpTransport; }
namespace meshtastic::configuration { class AdminTransaction; }

namespace meshtastic {

inline constexpr char kDefaultTcpHost[] = "127.0.0.1";
constexpr std::uint16_t kDefaultTcpPort = 4403;

struct Endpoint {
    std::string host   = kDefaultTcpHost;
    std::uint16_t port = kDefaultTcpPort;
};

enum class ConnectionState {
    stopped,
    connecting,
    synchronizing,
    connected,
    reconnecting,
};

struct ClientSnapshot {
    std::uint64_t revision = 0;
    ConnectionState state  = ConnectionState::stopped;
    std::string endpoint;
    std::string error;
    std::uint32_t my_node_number = 0;
    std::optional<MyNodeInfo> my_info;
    bool config_complete         = false;
    std::optional<Config> config;
    std::optional<Config::LoRaConfig> lora_config;
    std::optional<Config::DeviceConfig> device_config;
    std::optional<ModuleConfig> module_config;
    std::optional<DeviceMetadata> metadata;
    std::optional<LocalStats> local_stats;
    std::optional<User> owner;
    std::string session_passkey;
    std::optional<AdminMessage> last_admin;
    // Local administrative GET responses, keyed by the request packet id.
    std::map<std::uint32_t, AdminMessage> admin_responses;
    std::optional<FromRadio> last_from_radio;
    std::vector<Node> nodes;
    std::vector<ChannelInfo> channels;
    std::map<int, Channel> channel_configs;
    std::map<int, std::uint64_t> channel_revisions;
    std::vector<Message> messages;
    std::vector<Conversation> conversations;
    std::map<int, Config> device_configs;
    std::map<int, ModuleConfig> module_configs;
    std::map<int, std::uint64_t> device_config_revisions;
    std::map<int, std::uint64_t> module_config_revisions;
    std::vector<DeliveryRecord> deliveries;
    std::vector<PositionRecord> positions;
    std::vector<TelemetryRecord> telemetry;
    std::vector<WaypointRecord> waypoints;
    std::vector<RouteRecord> routes;
    std::string storage_error;
};

struct MapHistory {
    std::vector<PositionRecord> positions;
    std::vector<WaypointRecord> waypoints;
    std::vector<RouteRecord> routes;
};

const char *connection_state_name(ConnectionState state);

class MeshtasticClient {
public:
    class AdminTransactionLease {
    public:
        AdminTransactionLease() = default;
        ~AdminTransactionLease();
        AdminTransactionLease(const AdminTransactionLease &) = delete;
        AdminTransactionLease &operator=(const AdminTransactionLease &) = delete;
        AdminTransactionLease(AdminTransactionLease &&other) noexcept;
        AdminTransactionLease &operator=(AdminTransactionLease &&other) noexcept;
        void reset() noexcept;
        void abandon() noexcept;
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] std::uint32_t radio_number() const noexcept { return radio_number_; }

    private:
        struct SharedState {
            std::mutex mutex;
            std::uint64_t active = 0;
            std::uint64_t next = 0;
            bool fenced = false;
            bool has_writes = false;
            TcpTransport *transport = nullptr;
        };
        friend class MeshtasticClient;
        friend class configuration::AdminTransaction;
        void complete() noexcept;
        AdminTransactionLease(std::shared_ptr<SharedState> state, std::uint64_t token,
                              std::uint32_t radio_number) noexcept;
        std::shared_ptr<SharedState> state_;
        std::uint64_t token_ = 0;
        std::uint32_t radio_number_ = 0;
    };

    explicit MeshtasticClient(Endpoint endpoint, std::filesystem::path storage_path = {},
                              std::chrono::milliseconds ack_timeout = std::chrono::seconds(30));
    ~MeshtasticClient();

    MeshtasticClient(const MeshtasticClient &)            = delete;
    MeshtasticClient &operator=(const MeshtasticClient &) = delete;

    void start(bool enabled = true);
    void stop();
    void set_enabled(bool enabled);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] ClientSnapshot snapshot() const;
    bool send_text(std::string text, std::uint32_t channel = 0);
    // Send a text message to one mesh node. A destination of kBroadcastNode
    // is equivalent to send_text(); callers use this for private chats.
    bool send_text_to(std::string text, std::uint32_t destination, std::uint32_t channel = 0);
    bool request_full_config(std::uint32_t config_id = 0);
    bool send_heartbeat(std::uint32_t nonce);
    bool send_disconnect();
    bool send_xmodem(const XModem &message);
    bool send_mqtt_proxy(const MqttClientProxyMessage &message);
    // These generic methods expose the complete protobuf surface. Callers can
    // construct any current ToRadio/MeshPacket/AdminMessage variant directly.
    bool send_to_radio(const ToRadio &message);
    bool send_packet(MeshPacket packet);
    bool send_data(PortNum port, std::string payload, std::uint32_t destination = kBroadcastNode,
                   std::uint32_t channel = 0, std::uint32_t hop_limit = 3,
                   MeshPacket::Priority priority = MeshPacket::RELIABLE, std::uint32_t packet_id = 0);
    bool send_admin(const AdminMessage &message, std::uint32_t destination = kLocalNode,
                    std::uint32_t channel = 0, std::uint32_t hop_limit = 3, std::uint32_t packet_id = 0);
    // Returns the exact queued packet id, or nullopt when the request could not be queued.
    std::optional<std::uint32_t> queue_admin_tracked(const AdminMessage &message,
                                                     std::uint32_t destination = kLocalNode);
    // Protects application-level multi-message edits. Raw admin APIs retain
    // their existing behavior for callers managing their own transactions.
    std::optional<AdminTransactionLease> acquire_admin_transaction(std::string &error,
                                                                    std::uint32_t expected_radio = 0);
    std::optional<std::uint32_t> queue_admin_tracked(const AdminMessage &message,
                                                     const AdminTransactionLease &lease);
    bool request_channel(std::uint32_t index, std::uint32_t destination = kLocalNode);
    bool request_owner(std::uint32_t destination = kLocalNode);
    bool request_config(AdminMessage::ConfigType type, std::uint32_t destination = kLocalNode);
    bool request_session_key(std::uint32_t destination = kLocalNode);
    bool request_module_config(AdminMessage::ModuleConfigType type, std::uint32_t destination = kLocalNode);
    bool request_metadata(std::uint32_t destination = kLocalNode);
    bool set_owner(User owner, std::uint32_t destination = kLocalNode);
    bool set_channel(const Channel &channel, std::uint32_t destination = kLocalNode);
    bool set_config(Config config, std::uint32_t destination = kLocalNode);
    bool set_lora_region(Config::LoRaConfig::RegionCode region, std::uint32_t destination = kLocalNode);
    bool set_module_config(ModuleConfig config, std::uint32_t destination = kLocalNode);
    bool begin_edit_settings(std::uint32_t destination = kLocalNode);
    bool commit_edit_settings(std::uint32_t destination = kLocalNode);
    void set_session_passkey(std::string passkey);
    // Override a remote radio's key without changing the local radio's key.
    bool set_remote_session_passkey(std::uint32_t node_number, std::string passkey);
    std::vector<RadioUpdate> drain_from_radio();
    std::vector<Message> query_messages(bool direct, std::uint32_t peer, std::uint32_t channel,
                                        std::int64_t before_local_id = 0, std::size_t limit = 50) const;
    std::vector<Conversation> query_conversations() const;
    bool mark_conversation_read(bool direct, std::uint32_t peer, std::uint32_t channel);
    bool retry_message(std::uint32_t packet_id);
    bool retry_message_by_local_id(std::int64_t local_id);
    bool request_traceroute(std::uint32_t destination, std::uint32_t channel = 0);
    bool request_telemetry(std::uint32_t destination, std::uint32_t channel = 0);
    bool send_waypoint(const Waypoint &waypoint, std::uint32_t destination = kBroadcastNode,
                       std::uint32_t channel = 0);
    std::vector<PositionRecord> query_positions(std::uint32_t node_number = 0,
                                               std::int64_t before_local_id = 0, std::size_t limit = 100) const;
    std::vector<TelemetryRecord> query_telemetry(std::uint32_t node_number = 0,
                                               std::int64_t before_local_id = 0, std::size_t limit = 100) const;
    std::vector<WaypointRecord> query_waypoints(std::size_t limit = 100) const;
    std::vector<RouteRecord> query_routes(std::uint32_t node_number = 0, std::size_t limit = 100) const;
    std::optional<MapHistory> query_map_history(const std::string &expected_endpoint,
                                                std::uint32_t expected_radio) const;

private:
    void worker();
    bool connection_requested() const;
    int connect_socket(std::string &error) const;
    bool send_payload(int socket, const std::vector<std::uint8_t> &payload);
    bool send_bytes(int socket, const std::uint8_t *data, std::size_t size);
    bool begin_config(int socket, std::uint32_t &config_id);
    bool enqueue_to_radio(ToRadio message);
    std::uint32_t resolve_destination(std::uint32_t destination) const;
    bool attach_session_passkey(AdminMessage &message, std::uint32_t destination) const;
    std::uint32_t next_id();
    void apply_from_radio(const RadioUpdate &message, std::uint32_t config_id);
    void set_state(ConnectionState state, std::string error = {});
    void interrupt_socket();
    void close_socket(int socket);
    void wait_to_reconnect();
    void restore_repository(std::uint32_t radio_number);
    void refresh_persistent_snapshot();
    void apply_delivery(const DeliveryRecord &delivery);
    void apply_deliveries(const std::vector<DeliveryRecord> &deliveries);
    void invalidate_admin_transaction() noexcept;

    Endpoint endpoint_;
    std::unique_ptr<Repository> repository_;
    std::unique_ptr<RadioSession> session_;
    std::unique_ptr<TcpTransport> transport_;
    std::string session_key_;
    std::uint32_t radio_number_ = 0;
    bool identity_confirmed_ = false;
    std::vector<RadioUpdate> preidentity_updates_;
    mutable std::recursive_mutex repository_mutex_;
    std::shared_ptr<AdminTransactionLease::SharedState> admin_transaction_state_ =
        std::make_shared<AdminTransactionLease::SharedState>();
    mutable std::mutex control_mutex_;
    std::condition_variable control_changed_;
    bool enabled_ = false;
    std::deque<ToRadio> pending_radio_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint32_t> next_packet_id_{1};

    mutable std::mutex snapshot_mutex_;
    mutable ClientSnapshot snapshot_;
    std::map<std::uint32_t, std::string> remote_session_passkeys_;
    std::deque<std::uint32_t> admin_response_order_;
    mutable std::mutex event_mutex_;
    std::deque<RadioUpdate> events_;
};

}  // namespace meshtastic

#endif
