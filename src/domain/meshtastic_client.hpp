#ifndef LILYGO_UI_MESHTASTIC_CLIENT_HPP
#define LILYGO_UI_MESHTASTIC_CLIENT_HPP

#include "domain/meshtastic_protocol.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace meshtastic {

inline constexpr char kDefaultTcpHost[] = "192.168.8.114";
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
    bool config_complete         = false;
    std::vector<Node> nodes;
    std::vector<ChannelInfo> channels;
    std::vector<Message> messages;
};

const char *connection_state_name(ConnectionState state);

class MeshtasticClient {
public:
    explicit MeshtasticClient(Endpoint endpoint);
    ~MeshtasticClient();

    MeshtasticClient(const MeshtasticClient &)            = delete;
    MeshtasticClient &operator=(const MeshtasticClient &) = delete;

    void start(bool enabled = true);
    void stop();
    void set_enabled(bool enabled);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] ClientSnapshot snapshot() const;
    bool send_text(std::string text, std::uint32_t channel = 0);

private:
    struct PendingText {
        std::string text;
        std::uint32_t channel   = 0;
        std::uint32_t packet_id = 0;
    };

    void worker();
    bool connection_requested() const;
    int connect_socket(std::string &error) const;
    bool send_payload(int socket, const std::vector<std::uint8_t> &payload);
    bool send_bytes(int socket, const std::uint8_t *data, std::size_t size);
    bool begin_config(int socket, std::uint32_t &config_id);
    std::uint32_t next_id();
    void apply_from_radio(const RadioUpdate &message, std::uint32_t config_id);
    void set_state(ConnectionState state, std::string error = {});
    void interrupt_socket();
    void close_socket(int socket);
    void wait_to_reconnect();

    Endpoint endpoint_;
    mutable std::mutex control_mutex_;
    std::condition_variable control_changed_;
    bool enabled_ = false;
    std::deque<PendingText> pending_text_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::mutex socket_mutex_;
    int socket_ = -1;
    std::atomic<std::uint32_t> next_packet_id_{1};

    mutable std::mutex snapshot_mutex_;
    ClientSnapshot snapshot_;
};

}  // namespace meshtastic

#endif
