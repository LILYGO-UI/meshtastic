#include "domain/meshtastic_client.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace meshtastic {
namespace {

constexpr auto kReconnectDelay             = std::chrono::seconds(2);
constexpr auto kConnectTimeout             = std::chrono::seconds(5);
constexpr auto kConnectPollInterval        = std::chrono::milliseconds(100);
constexpr std::size_t kMessageHistoryLimit = 100;

std::string socket_error(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
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

MeshtasticClient::MeshtasticClient(Endpoint endpoint) : endpoint_(std::move(endpoint))
{
    auto seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    seed ^= static_cast<std::uint32_t>(::getpid());
    seed ^= static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this));
    next_packet_id_    = seed == 0 ? 1 : seed;
    snapshot_.endpoint = endpoint_.host + ":" + std::to_string(endpoint_.port);
}

MeshtasticClient::~MeshtasticClient()
{
    stop();
}

void MeshtasticClient::start(bool enabled)
{
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
    control_changed_.notify_all();
    // Closing the TCP session also disconnects the client. Do not wait for a
    // protocol write here: a stalled peer may have filled the send buffer.
    interrupt_socket();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
    set_state(ConnectionState::stopped);
}

void MeshtasticClient::set_enabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        enabled_ = enabled;
        if (!enabled) pending_text_.clear();
    }
    control_changed_.notify_all();
    if (!enabled) interrupt_socket();
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
    if (text.empty() || text.size() > kMaxTextPayload) return false;

    const auto packet_id = next_id();
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        if (!enabled_) return false;
        pending_text_.push_back({text, channel, packet_id});
    }
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        Message message;
        message.from     = snapshot_.my_node_number;
        message.to       = kBroadcastNode;
        message.id       = packet_id;
        message.channel  = channel;
        message.text     = std::move(text);
        message.outgoing = true;
        snapshot_.messages.push_back(std::move(message));
        if (snapshot_.messages.size() > kMessageHistoryLimit) snapshot_.messages.erase(snapshot_.messages.begin());
        ++snapshot_.revision;
    }
    control_changed_.notify_all();
    return true;
}

bool MeshtasticClient::connection_requested() const
{
    return !stop_requested_ && enabled();
}

int MeshtasticClient::connect_socket(std::string &error) const
{
    addrinfo hints{};
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    addrinfo *addresses = nullptr;
    const auto port     = std::to_string(endpoint_.port);
    const int lookup    = getaddrinfo(endpoint_.host.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        error = std::string("resolve: ") + gai_strerror(lookup);
        return -1;
    }

    int connected_socket = -1;
    const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    for (auto *address = addresses; address && connection_requested(); address = address->ai_next) {
        if (std::chrono::steady_clock::now() >= deadline) {
            error = "connect: timed out";
            break;
        }
        const int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0) continue;

#if defined(SO_NOSIGPIPE)
        int no_sigpipe = 1;
        setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        const int original_flags = fcntl(candidate, F_GETFL, 0);
        if (original_flags < 0 || fcntl(candidate, F_SETFL, original_flags | O_NONBLOCK) < 0) {
            ::close(candidate);
            continue;
        }

        int result = ::connect(candidate, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
            pollfd descriptor{candidate, POLLOUT, 0};
            while (connection_requested()) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (remaining <= std::chrono::milliseconds::zero()) {
                    errno = ETIMEDOUT;
                    result = -1;
                    break;
                }
                result = ::poll(&descriptor, 1, static_cast<int>(std::min(remaining, kConnectPollInterval).count()));
                if (result == 0 || (result < 0 && errno == EINTR)) continue;
                break;
            }
            if (!connection_requested()) {
                errno = ECANCELED;
                result = -1;
            }
            if (result > 0) {
                int pending_error = 0;
                socklen_t length  = sizeof(pending_error);
                if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, &pending_error, &length) < 0 || pending_error != 0) {
                    errno  = pending_error == 0 ? errno : pending_error;
                    result = -1;
                } else {
                    result = 0;
                }
            }
        }
        if (result == 0 && fcntl(candidate, F_SETFL, original_flags & ~O_NONBLOCK) == 0) {
            timeval timeout{0, 500000};
            setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            connected_socket = candidate;
            break;
        }
        error = socket_error("connect");
        ::close(candidate);
    }
    freeaddrinfo(addresses);
    if (connected_socket < 0 && !connection_requested())
        error = "connection cancelled";
    else if (connected_socket < 0 && error.empty())
        error = socket_error("connect");
    return connected_socket;
}

bool MeshtasticClient::send_bytes(int socket, const std::uint8_t *data, std::size_t size)
{
    // Only the worker writes to the socket. stop()/set_enabled(false) can
    // interrupt this write with shutdown without waiting for a send mutex.
    while (size > 0 && !stop_requested_) {
#if defined(MSG_NOSIGNAL)
        const auto sent = ::send(socket, data, size, MSG_NOSIGNAL);
#else
        const auto sent = ::send(socket, data, size, 0);
#endif
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return size == 0;
}

bool MeshtasticClient::send_payload(int socket, const std::vector<std::uint8_t> &payload)
{
    const auto frame = frame_payload(payload);
    return !frame.empty() && send_bytes(socket, frame.data(), frame.size());
}

bool MeshtasticClient::begin_config(int socket, std::uint32_t &config_id)
{
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
        snapshot_.config_complete = false;
        snapshot_.nodes.clear();
        snapshot_.channels.clear();
        snapshot_.error.clear();
        snapshot_.state = ConnectionState::synchronizing;
        ++snapshot_.revision;
    }
    return send_payload(socket, encode_want_config(config_id));
}

std::uint32_t MeshtasticClient::next_id()
{
    std::uint32_t id = 0;
    while (id == 0) id = next_packet_id_.fetch_add(1);
    return id;
}

void MeshtasticClient::apply_from_radio(const RadioUpdate &message, std::uint32_t config_id)
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (message.my_node_number) snapshot_.my_node_number = *message.my_node_number;
    if (message.node) {
        const auto existing = std::find_if(snapshot_.nodes.begin(), snapshot_.nodes.end(),
                                           [&](const Node &node) { return node.number == message.node->number; });
        if (existing == snapshot_.nodes.end())
            snapshot_.nodes.push_back(*message.node);
        else
            *existing = *message.node;
    }
    if (message.channel && message.channel->role != 0) {
        const auto existing =
            std::find_if(snapshot_.channels.begin(), snapshot_.channels.end(),
                         [&](const ChannelInfo &channel) { return channel.index == message.channel->index; });
        if (existing == snapshot_.channels.end())
            snapshot_.channels.push_back(*message.channel);
        else
            *existing = *message.channel;
    }
    if (message.text_message) {
        const auto duplicate =
            std::find_if(snapshot_.messages.begin(), snapshot_.messages.end(), [&](const Message &item) {
                return item.id != 0 && item.id == message.text_message->id &&
                       (item.from == message.text_message->from || item.outgoing);
            });
        if (duplicate == snapshot_.messages.end()) {
            snapshot_.messages.push_back(*message.text_message);
            if (snapshot_.messages.size() > kMessageHistoryLimit) snapshot_.messages.erase(snapshot_.messages.begin());
        }
    }
    if (message.config_complete_id && *message.config_complete_id == config_id) {
        snapshot_.config_complete = true;
        snapshot_.state           = ConnectionState::connected;
        snapshot_.error.clear();
    }
    ++snapshot_.revision;
}

void MeshtasticClient::set_state(ConnectionState state, std::string error)
{
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_.state = state;
    snapshot_.error = std::move(error);
    if (state == ConnectionState::stopped || state == ConnectionState::reconnecting) snapshot_.config_complete = false;
    ++snapshot_.revision;
}

void MeshtasticClient::interrupt_socket()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_ >= 0) ::shutdown(socket_, SHUT_RDWR);
}

void MeshtasticClient::close_socket(int socket)
{
    if (socket < 0) return;
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_ == socket) socket_ = -1;
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
}

void MeshtasticClient::wait_to_reconnect()
{
    std::unique_lock<std::mutex> lock(control_mutex_);
    control_changed_.wait_for(lock, kReconnectDelay,
                              [&] { return stop_requested_ || !enabled_ || !pending_text_.empty(); });
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
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (!connection_requested()) {
                ::close(connected_socket);
                if (!stop_requested_) set_state(ConnectionState::stopped);
                continue;
            }
            socket_ = connected_socket;
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
        while (connection_alive && !stop_requested_) {
            while (!stop_requested_) {
                PendingText pending;
                {
                    std::lock_guard<std::mutex> lock(control_mutex_);
                    if (!enabled_) {
                        connection_alive = false;
                        break;
                    }
                    if (pending_text_.empty()) break;
                    pending = std::move(pending_text_.front());
                    pending_text_.pop_front();
                }
                const auto encoded = encode_text_message(pending.text, pending.packet_id, pending.channel);
                if (encoded.empty() || !send_payload(connected_socket, encoded)) {
                    connection_alive = false;
                    break;
                }
            }
            if (!connection_alive) break;

            std::array<std::uint8_t, 1024> received{};
            const auto count = ::recv(connected_socket, received.data(), received.size(), 0);
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
                if (message.rebooted) {
                    parser.reset();
                    if (!begin_config(connected_socket, config_id)) {
                        error            = "failed to restart config sync after reboot";
                        connection_alive = false;
                        break;
                    }
                }
            }
        }

        close_socket(connected_socket);
        if (connection_requested()) {
            set_state(ConnectionState::reconnecting, std::move(error));
            wait_to_reconnect();
        } else {
            set_state(ConnectionState::stopped);
        }
    }
}

}  // namespace meshtastic
