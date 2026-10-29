#ifndef LILYGO_UI_MESHTASTIC_TCP_TRANSPORT_HPP
#define LILYGO_UI_MESHTASTIC_TCP_TRANSPORT_HPP
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <sys/types.h>
namespace meshtastic {
class TcpTransport {
public:
    ~TcpTransport();
    int connect(const std::string &host, std::uint16_t port, const std::function<bool()> &active, std::string &error);
    bool write(int socket, const std::uint8_t *data, std::size_t size, const std::function<bool()> &active);
    ssize_t read(int socket, std::uint8_t *data, std::size_t size);
    void interrupt();
    void close(int socket);
private:
    std::mutex mutex_;
    int socket_ = -1;
};
}
#endif
