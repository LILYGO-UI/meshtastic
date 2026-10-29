#include "domain/meshtastic_client.hpp"

#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    meshtastic::Endpoint endpoint;
    if (argc > 1) endpoint.host = argv[1];
    if (argc > 2) {
        unsigned parsed = 0;
        const auto *begin = argv[2];
        const auto *end = begin + std::strlen(begin);
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end || parsed == 0 || parsed > 65535) return 2;
        endpoint.port = static_cast<std::uint16_t>(parsed);
    }
    meshtastic::MeshtasticClient client(endpoint);
    client.start();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    meshtastic::ClientSnapshot snapshot;
    while (std::chrono::steady_clock::now() < deadline) {
        snapshot = client.snapshot();
        if (snapshot.state == meshtastic::ConnectionState::connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    snapshot = client.snapshot();
    client.stop();
    if (snapshot.state != meshtastic::ConnectionState::connected) {
        std::fprintf(stderr, "Meshtastic live smoke failed: state=%s error=%s\n",
                     meshtastic::connection_state_name(snapshot.state), snapshot.error.c_str());
        return 1;
    }

    std::printf("Connected to %s: node=%08x nodes=%zu channels=%zu messages=%zu\n", snapshot.endpoint.c_str(),
                snapshot.my_node_number, snapshot.nodes.size(), snapshot.channels.size(), snapshot.messages.size());
    return 0;
}
