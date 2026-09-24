#include "domain/meshtastic_client.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    meshtastic::MeshtasticClient client({meshtastic::kDefaultTcpHost, meshtastic::kDefaultTcpPort});
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
