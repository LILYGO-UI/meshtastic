#include "transport/tcp_transport.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace meshtastic {
TcpTransport::~TcpTransport() { interrupt(); if (socket_ >= 0) close(socket_); }
int TcpTransport::connect(const std::string &host, std::uint16_t port, const std::function<bool()> &active, std::string &error)
{
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo *addresses = nullptr;
    const auto service = std::to_string(port);
    const auto lookup = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup) { error = std::string("resolve: ") + gai_strerror(lookup); return -1; }
    int result_socket = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (auto *address = addresses; address && active(); address = address->ai_next) {
        if (std::chrono::steady_clock::now() >= deadline) { error = "connect: timed out"; break; }
        const int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0) continue;
#if defined(SO_NOSIGPIPE)
        int no_sigpipe = 1; setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        const int flags = fcntl(candidate, F_GETFL, 0);
        if (flags < 0 || fcntl(candidate, F_SETFL, flags | O_NONBLOCK) < 0) { ::close(candidate); continue; }
        int result = ::connect(candidate, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
            pollfd descriptor{candidate, POLLOUT, 0};
            while (active()) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (remaining.count() <= 0) { errno = ETIMEDOUT; result = -1; break; }
                result = ::poll(&descriptor, 1, static_cast<int>(std::min<std::int64_t>(remaining.count(), 100)));
                if (!result || (result < 0 && errno == EINTR)) continue;
                break;
            }
            if (!active()) { errno = ECANCELED; result = -1; }
            if (result > 0) {
                int pending = 0; socklen_t length = sizeof(pending);
                if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, &pending, &length) < 0 || pending) { errno = pending ? pending : errno; result = -1; }
                else result = 0;
            }
        }
        if (!result && active() && fcntl(candidate, F_SETFL, flags & ~O_NONBLOCK) == 0) {
            timeval receive_timeout{0, 100000};
            setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
            std::lock_guard<std::mutex> lock(mutex_);
            socket_ = candidate; result_socket = candidate; break;
        }
        error = std::string("connect: ") + std::strerror(errno); ::close(candidate);
    }
    freeaddrinfo(addresses);
    if (result_socket < 0 && !active()) error = "connection cancelled";
    if (result_socket < 0 && error.empty()) error = "unable to connect";
    return result_socket;
}
bool TcpTransport::write(int socket, const std::uint8_t *data, std::size_t size, const std::function<bool()> &active)
{
    while (size && active()) {
#if defined(MSG_NOSIGNAL)
        const auto count = ::send(socket, data, size, MSG_NOSIGNAL);
#else
        const auto count = ::send(socket, data, size, 0);
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count; size -= static_cast<std::size_t>(count);
    }
    return size == 0;
}
ssize_t TcpTransport::read(int socket, std::uint8_t *data, std::size_t size) { return ::recv(socket, data, size, 0); }
void TcpTransport::interrupt() { std::lock_guard<std::mutex> lock(mutex_); if (socket_ >= 0) ::shutdown(socket_, SHUT_RDWR); }
void TcpTransport::close(int socket) { if (socket < 0) return; std::lock_guard<std::mutex> lock(mutex_); if (socket_ == socket) socket_ = -1; ::shutdown(socket, SHUT_RDWR); ::close(socket); }
}
