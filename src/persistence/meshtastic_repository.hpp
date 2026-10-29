#ifndef LILYGO_UI_MESHTASTIC_REPOSITORY_HPP
#define LILYGO_UI_MESHTASTIC_REPOSITORY_HPP

#include "domain/meshtastic_protocol.hpp"
#include <filesystem>
#include <map>
#include <mutex>
#include <sqlite3.h>

namespace meshtastic {
struct RepositoryState {
    std::uint32_t radio_number = 0;
    std::vector<Node> nodes;
    std::vector<ChannelInfo> channels;
    std::map<int, Channel> channel_configs;
    std::map<int, Config> device_configs;
    std::map<int, ModuleConfig> module_configs;
    std::optional<User> owner;
    std::optional<DeviceMetadata> metadata;
};
struct StoredOutbound { MeshPacket packet; DeliveryRecord delivery; };

// Owns the durable application data. Every query is scoped by endpoint AND radio.
class Repository {
public:
    explicit Repository(const std::filesystem::path &path);
    ~Repository();
    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;
    static std::filesystem::path default_path();
    std::string activate(const std::string &endpoint, std::uint32_t radio_number = 0);
    RepositoryState load_state(const std::string &session) const;
    void store_node(const std::string &session, const Node &node);
    void store_channel(const std::string &session, const ChannelInfo &channel);
    void store_channel_config(const std::string &session, const Channel &channel);
    void store_config(const std::string &session, const Config &config);
    void store_module_config(const std::string &session, const ModuleConfig &config);
    void store_owner(const std::string &session, const User &owner);
    void store_metadata(const std::string &session, const DeviceMetadata &metadata);
    Message store_message(const std::string &session, Message message);
    std::vector<Message> recent_messages(const std::string &session, std::size_t limit = 200) const;
    std::vector<Message> query_messages(const std::string &session, bool direct, std::uint32_t peer,
                                        std::uint32_t channel, std::int64_t before, std::size_t limit) const;
    std::vector<Conversation> conversations(const std::string &session) const;
    bool mark_read(const std::string &session, bool direct, std::uint32_t peer, std::uint32_t channel);
    void store_outbound(const std::string &session, const MeshPacket &packet, const DeliveryRecord &delivery);
    std::optional<Message> queue_packet(const std::string &session, const MeshPacket &packet, const DeliveryRecord &delivery);
    void update_delivery(const std::string &session, const DeliveryRecord &delivery);
    void update_deliveries(const std::string &session, const std::vector<DeliveryRecord> &deliveries);
    std::vector<StoredOutbound> outbound(const std::string &session) const;
    std::optional<StoredOutbound> find_outbound(const std::string &session, std::uint32_t packet_id) const;
    std::optional<std::uint32_t> message_packet_id(const std::string &session, std::int64_t local_id) const;
    void recover_interrupted(const std::string &session);
    void store_position(const std::string &session, const PositionRecord &record);
    void store_telemetry(const std::string &session, const TelemetryRecord &record);
    void store_waypoint(const std::string &session, const WaypointRecord &record);
    void store_route(const std::string &session, const RouteRecord &record);
    std::vector<PositionRecord> positions(const std::string &session, std::uint32_t node, std::int64_t before, std::size_t limit) const;
    std::vector<TelemetryRecord> telemetry(const std::string &session, std::uint32_t node, std::int64_t before, std::size_t limit) const;
    std::vector<WaypointRecord> waypoints(const std::string &session, std::size_t limit) const;
    std::vector<RouteRecord> routes(const std::string &session, std::uint32_t node, std::size_t limit) const;
private:
    void migrate();
    void store_attribute(const std::string &session, const char *name, const std::string &bytes);
    void store_event(const std::string &session, int kind, std::uint32_t node, std::uint32_t peer,
                     std::uint32_t packet, std::uint32_t request, std::uint32_t time, bool outgoing, const std::string &bytes);
    sqlite3 *db_ = nullptr;
    mutable std::recursive_mutex mutex_;
};
}
#endif
