#ifndef LILYGO_UI_MESHTASTIC_ADMIN_TEST_RADIO_HPP
#define LILYGO_UI_MESHTASTIC_ADMIN_TEST_RADIO_HPP

#include "domain/meshtastic_client.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>

// A real local TCP handshake keeps transaction tests honest about connection
// readiness while individual ACK/response order is controlled by the tests.
class AdminTestRadio {
public:
    explicit AdminTestRadio(std::uint32_t node = 101) : node_(node)
    {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(listener_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
        assert(::listen(listener_, 1) == 0);
        socklen_t size = sizeof(address);
        assert(::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { run(); });
    }
    ~AdminTestRadio()
    {
        stopping_ = true;
        worker_.join();
        ::close(listener_);
    }
    AdminTestRadio(const AdminTestRadio &) = delete;
    AdminTestRadio &operator=(const AdminTestRadio &) = delete;

    meshtastic::Endpoint endpoint() const { return {"127.0.0.1", port_}; }
    unsigned connections() const { return connections_; }

    void connect(meshtastic::MeshtasticClient &client) const
    {
        client.start(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto state = client.snapshot();
            if (state.state == meshtastic::ConnectionState::connected && state.config_complete &&
                state.my_node_number == node_) {
                client.set_session_passkey("session");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(false && "fake radio did not synchronize");
    }

    void wait_for_reconnect(meshtastic::MeshtasticClient &client, unsigned previous_connections) const
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto state = client.snapshot();
            if (connections_ > previous_connections && state.state == meshtastic::ConnectionState::connected &&
                state.config_complete && state.my_node_number == node_) {
                client.set_session_passkey("session");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(false && "fake radio did not reconnect");
    }

private:
    static bool send_message(int socket, const meshtastic::FromRadio &message)
    {
        const auto payload = message.SerializeAsString();
        const auto frame = meshtastic::frame_payload(std::vector<std::uint8_t>(payload.begin(), payload.end()));
        for (std::size_t offset = 0; offset < frame.size();) {
#if defined(MSG_NOSIGNAL)
            const auto count = ::send(socket, frame.data() + offset, frame.size() - offset, MSG_NOSIGNAL);
#else
            const auto count = ::send(socket, frame.data() + offset, frame.size() - offset, 0);
#endif
            if (count <= 0) return false;
            offset += static_cast<std::size_t>(count);
        }
        return true;
    }
    void run()
    {
        while (!stopping_) {
            pollfd listener{listener_, POLLIN, 0};
            if (::poll(&listener, 1, 25) <= 0) continue;
            const int socket = ::accept(listener_, nullptr, nullptr);
            if (socket < 0) continue;
#if defined(SO_NOSIGPIPE)
            int no_sigpipe = 1;
            ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
            meshtastic::FrameParser parser;
            while (!stopping_) {
                pollfd input{socket, POLLIN, 0};
                if (::poll(&input, 1, 25) <= 0) continue;
                std::uint8_t buffer[2048];
                const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
                if (count <= 0) break;
                for (const auto &frame : parser.feed(buffer, static_cast<std::size_t>(count))) {
                    meshtastic::ToRadio request;
                    if (!request.ParseFromArray(frame.data(), static_cast<int>(frame.size())) || !request.has_want_config_id()) continue;
                    meshtastic::FromRadio info, complete;
                    info.mutable_my_info()->set_my_node_num(node_);
                    complete.set_config_complete_id(request.want_config_id());
                    if (!send_message(socket, info) || !send_message(socket, complete)) break;
                    ++connections_;
                }
            }
            ::close(socket);
        }
    }

    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::uint32_t node_;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> connections_{0};
    std::thread worker_;
};
#endif
