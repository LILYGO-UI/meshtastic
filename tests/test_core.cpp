#include "domain/meshtastic_client.hpp"
#include "domain/meshtastic_protocol.hpp"

#include <meshtastic/mesh.pb.h>

#include <arpa/inet.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

template <typename MessageType>
std::vector<std::uint8_t> serialize(const MessageType &message)
{
    std::vector<std::uint8_t> payload(message.ByteSizeLong());
    assert(message.SerializeToArray(payload.data(), static_cast<int>(payload.size())));
    return payload;
}

void test_encoding()
{
    meshtastic::ToRadio decoded;
    const auto want_config = meshtastic::encode_want_config(1);
    assert(decoded.ParseFromArray(want_config.data(), static_cast<int>(want_config.size())));
    assert(decoded.payload_variant_case() == meshtastic::ToRadio::kWantConfigId);
    assert(decoded.want_config_id() == 1);

    const auto message = meshtastic::encode_text_message("hello", 0x12345678U, 0, 3);
    decoded.Clear();
    assert(decoded.ParseFromArray(message.data(), static_cast<int>(message.size())));
    assert(decoded.payload_variant_case() == meshtastic::ToRadio::kPacket);
    assert(decoded.packet().to() == meshtastic::kBroadcastNode);
    assert(decoded.packet().id() == 0x12345678U);
    assert(decoded.packet().hop_limit() == 3);
    assert(decoded.packet().priority() == meshtastic::MeshPacket::RELIABLE);
    assert(decoded.packet().decoded().portnum() == meshtastic::TEXT_MESSAGE_APP);
    assert(decoded.packet().decoded().payload() == "hello");
    assert(meshtastic::encode_text_message("", 1).empty());
    assert(meshtastic::encode_text_message(std::string(meshtastic::kMaxTextPayload + 1, 'x'), 1).empty());
}

void test_framing()
{
    meshtastic::FrameParser parser;
    const std::vector<std::uint8_t> first{0x01, 0x02, 0x94};
    assert(parser.feed(first.data(), first.size()).empty());
    const std::vector<std::uint8_t> second{0xc3, 0x00, 0x02, 0x18, 0x01, 0x94, 0xc3, 0x00};
    const auto frames = parser.feed(second.data(), second.size());
    assert(frames.size() == 1);
    assert(frames.front() == std::vector<std::uint8_t>({0x18, 0x01}));
    const std::vector<std::uint8_t> third{0x01, 0x20};
    const auto trailing = parser.feed(third.data(), third.size());
    assert(trailing.size() == 1);
    assert(trailing.front() == std::vector<std::uint8_t>({0x20}));

    parser.reset();
    const std::vector<std::uint8_t> invalid_then_valid{0x94, 0xc3, 0x02, 0x01, 0x94, 0xc3, 0x00, 0x01, 0x20};
    const auto recovered = parser.feed(invalid_then_valid.data(), invalid_then_valid.size());
    assert(recovered.size() == 1);
    assert(recovered.front() == std::vector<std::uint8_t>({0x20}));

    const auto framed = meshtastic::frame_payload({0x18, 0x01});
    assert(framed == std::vector<std::uint8_t>({0x94, 0xc3, 0x00, 0x02, 0x18, 0x01}));
}

void test_node_decode()
{
    meshtastic::FromRadio from_radio;
    auto *node = from_radio.mutable_node_info();
    node->set_num(0x12345678U);
    node->mutable_user()->set_id("!12345678");
    node->mutable_user()->set_long_name("Ridge Relay");
    node->mutable_user()->set_short_name("RR");
    node->mutable_position()->set_latitude_i(473769000);
    node->mutable_position()->set_longitude_i(85417000);
    node->mutable_position()->set_altitude(1482);
    node->mutable_device_metrics()->set_battery_level(82);
    node->mutable_device_metrics()->set_voltage(4.08F);
    node->set_snr(5.5F);
    node->set_last_heard(1700000000U);
    node->set_hops_away(2);

    meshtastic::RadioUpdate decoded;
    std::string error;
    assert(meshtastic::decode_from_radio(serialize(from_radio), decoded, error));
    assert(error.empty() && decoded.node);
    assert(decoded.node->number == 0x12345678U);
    assert(decoded.node->long_name == "Ridge Relay");
    assert(decoded.node->battery_level == 82);
    assert(decoded.node->hops_away == 2);
    assert(std::fabs(decoded.node->snr - 5.5F) < 0.001F);
    assert(std::fabs(*decoded.node->latitude - 47.3769) < 0.00001);
    assert(std::fabs(*decoded.node->longitude - 8.5417) < 0.00001);
    assert(decoded.node->altitude == 1482);
}

void test_text_and_handshake_decode()
{
    meshtastic::FromRadio from_radio;
    auto *packet = from_radio.mutable_packet();
    packet->set_from(0x12345678U);
    packet->set_to(meshtastic::kBroadcastNode);
    packet->set_id(42);
    packet->set_rx_time(1700000000U);
    packet->set_rx_snr(8.5F);
    packet->set_rx_rssi(-73);
    packet->mutable_decoded()->set_portnum(meshtastic::TEXT_MESSAGE_APP);
    packet->mutable_decoded()->set_payload("mesh hello");

    meshtastic::RadioUpdate decoded;
    std::string error;
    assert(meshtastic::decode_from_radio(serialize(from_radio), decoded, error));
    assert(decoded.text_message);
    assert(decoded.text_message->text == "mesh hello");
    assert(decoded.text_message->from == 0x12345678U);
    assert(decoded.text_message->rssi == -73);
    assert(std::fabs(decoded.text_message->snr - 8.5F) < 0.001F);

    meshtastic::FromRadio complete;
    complete.set_config_complete_id(1234);
    assert(meshtastic::decode_from_radio(serialize(complete), decoded, error));
    assert(decoded.config_complete_id == 1234);

    const std::vector<std::uint8_t> truncated{0x22, 0x05, 0x08};
    assert(!meshtastic::decode_from_radio(truncated, decoded, error));
    assert(!error.empty());
}

void test_channel_decode()
{
    meshtastic::FromRadio from_radio;
    auto *channel = from_radio.mutable_channel();
    channel->set_index(2);
    channel->set_role(meshtastic::Channel::SECONDARY);
    channel->mutable_settings()->set_name("Operations");
    channel->mutable_settings()->set_psk("\x01", 1);

    meshtastic::RadioUpdate decoded;
    std::string error;
    assert(meshtastic::decode_from_radio(serialize(from_radio), decoded, error));
    assert(error.empty() && decoded.channel);
    assert(decoded.channel->index == 2);
    assert(decoded.channel->role == meshtastic::Channel::SECONDARY);
    assert(decoded.channel->name == "Operations");
    assert(decoded.channel->encrypted);
}

bool send_all(int socket, const std::vector<std::uint8_t> &data)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto sent = ::send(socket, data.data() + offset, data.size() - offset, 0);
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

std::vector<std::uint8_t> receive_frame(int socket, meshtastic::FrameParser &parser)
{
    std::uint8_t buffer[256]{};
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
        if (count <= 0) return {};
        auto frames = parser.feed(buffer, static_cast<std::size_t>(count));
        if (!frames.empty()) return std::move(frames.front());
    }
    return {};
}

void test_tcp_client()
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t address_size = sizeof(address);
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);
    const auto port = ntohs(address.sin_port);

    std::thread server([listener] {
        const int peer = ::accept(listener, nullptr, nullptr);
        assert(peer >= 0);
        meshtastic::FrameParser parser;
        const auto handshake = receive_frame(peer, parser);
        meshtastic::ToRadio handshake_message;
        assert(handshake_message.ParseFromArray(handshake.data(), static_cast<int>(handshake.size())));
        assert(handshake_message.payload_variant_case() == meshtastic::ToRadio::kWantConfigId);
        const auto config_id = handshake_message.want_config_id();
        assert(config_id != 0);

        meshtastic::FromRadio my_info;
        my_info.mutable_my_info()->set_my_node_num(0x12345678U);
        meshtastic::FromRadio complete;
        complete.set_config_complete_id(config_id);
        auto first  = meshtastic::frame_payload(serialize(my_info));
        auto second = meshtastic::frame_payload(serialize(complete));
        first.insert(first.end(), second.begin(), second.end());
        assert(send_all(peer, first));

        const auto outbound  = receive_frame(peer, parser);
        const auto packet_id = config_id == 0xffffffffU ? 1U : config_id + 1U;
        meshtastic::ToRadio outbound_message;
        assert(outbound_message.ParseFromArray(outbound.data(), static_cast<int>(outbound.size())));
        assert(outbound_message.payload_variant_case() == meshtastic::ToRadio::kPacket);
        assert(outbound_message.packet().id() == packet_id);
        assert(outbound_message.packet().decoded().portnum() == meshtastic::TEXT_MESSAGE_APP);
        assert(outbound_message.packet().decoded().payload() == "outbound");

        meshtastic::FromRadio incoming;
        auto *packet = incoming.mutable_packet();
        packet->set_from(0x87654321U);
        packet->set_to(meshtastic::kBroadcastNode);
        packet->set_id(99);
        packet->mutable_decoded()->set_portnum(meshtastic::TEXT_MESSAGE_APP);
        packet->mutable_decoded()->set_payload("inbound");
        assert(send_all(peer, meshtastic::frame_payload(serialize(incoming))));

        std::uint8_t ignored[32];
        while (::recv(peer, ignored, sizeof(ignored), 0) > 0) {
        }
        ::close(peer);
        ::close(listener);
    });

    meshtastic::MeshtasticClient client({"127.0.0.1", port});
    client.start();
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (client.snapshot().state == meshtastic::ConnectionState::connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    auto snapshot = client.snapshot();
    assert(snapshot.state == meshtastic::ConnectionState::connected);
    assert(snapshot.my_node_number == 0x12345678U);
    assert(client.send_text("outbound"));
    for (int attempt = 0; attempt < 100; ++attempt) {
        snapshot           = client.snapshot();
        const auto inbound = std::find_if(snapshot.messages.begin(), snapshot.messages.end(),
                                          [](const meshtastic::Message &message) { return message.text == "inbound"; });
        if (inbound != snapshot.messages.end()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    snapshot = client.snapshot();
    assert(std::any_of(snapshot.messages.begin(), snapshot.messages.end(), [](const meshtastic::Message &message) {
        return message.text == "inbound" && !message.outgoing;
    }));
    client.stop();
    server.join();
}

void test_stop_with_unresponsive_receiver()
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    // Advertise a small receive window, then leave application traffic unread.
    const int receive_buffer = 1024;
    assert(::setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer)) == 0);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t address_size = sizeof(address);
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);

    std::mutex mutex;
    std::condition_variable changed;
    bool stopping = false;
    bool stopped = false;
    std::thread server([&] {
        const int peer = ::accept(listener, nullptr, nullptr);
        assert(peer >= 0);
        meshtastic::FrameParser parser;
        const auto handshake = receive_frame(peer, parser);
        meshtastic::ToRadio request;
        assert(request.ParseFromArray(handshake.data(), static_cast<int>(handshake.size())));
        assert(request.payload_variant_case() == meshtastic::ToRadio::kWantConfigId);
        meshtastic::FromRadio complete;
        complete.set_config_complete_id(request.want_config_id());
        assert(send_all(peer, meshtastic::frame_payload(serialize(complete))));

        std::unique_lock<std::mutex> lock(mutex);
        assert(changed.wait_for(lock, std::chrono::seconds(10), [&] { return stopping; }));
        // Release even a broken client after two seconds, so the regression
        // fails on its measured stop latency instead of hanging the test suite.
        changed.wait_for(lock, std::chrono::seconds(2), [&] { return stopped; });
        lock.unlock();
        ::shutdown(peer, SHUT_RDWR);
        ::close(peer);
        ::close(listener);
    });

    meshtastic::MeshtasticClient client({"127.0.0.1", ntohs(address.sin_port)});
    client.start();
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (client.snapshot().state == meshtastic::ConnectionState::connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(client.snapshot().state == meshtastic::ConnectionState::connected);

    std::atomic<bool> feeding{true};
    std::atomic<unsigned> submitted{0};
    std::thread producer([&] {
        // More than the default TCP send buffer on Linux/macOS. The producer
        // is separate because the old worker also held the queue mutex while
        // blocked in send(), preventing further send_text() calls.
        const std::string message(meshtastic::kMaxTextPayload, 'x');
        for (unsigned index = 0; index < 32768 && feeding; ++index) {
            if (!client.send_text(message)) break;
            ++submitted;
        }
    });
    unsigned previous = 0;
    for (int attempt = 0; attempt < 30; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const unsigned count = submitted.load();
        if (count == 32768 || (count > 0 && count == previous)) break;
        previous = count;
    }
    assert(submitted > 0);
    // Allow the worker's receive timeout to elapse and begin draining its queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    feeding = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    changed.notify_all();
    const auto start = std::chrono::steady_clock::now();
    client.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopped = true;
    }
    changed.notify_all();
    producer.join();
    server.join();
    std::fprintf(stderr, "stop with unresponsive TCP receiver: %.1f ms\n",
                 std::chrono::duration<double, std::milli>(elapsed).count());
    assert(elapsed < std::chrono::milliseconds(750));
    assert(client.snapshot().state == meshtastic::ConnectionState::stopped);
}

void test_stop_during_pending_connect()
{
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t address_size = sizeof(address);
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);

    // Fill the unaccepted connection backlog until another local connect is
    // demonstrably pending. This exercises cancellation without an external
    // host, firewall rules, or assumptions about a routable unused address.
    std::vector<int> fillers;
    bool pending = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int candidate = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(candidate >= 0);
        fillers.push_back(candidate);
        assert(::fcntl(candidate, F_SETFL, O_NONBLOCK) == 0);
        const int connected = ::connect(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        assert(connected == 0 || errno == EINPROGRESS);
        pollfd descriptor{candidate, POLLOUT, 0};
        const int ready = ::poll(&descriptor, 1, 50);
        assert(ready >= 0);
        if (ready == 0) {
            pending = true;
            break;
        }
        int error = 0;
        socklen_t error_size = sizeof(error);
        assert(::getsockopt(candidate, SOL_SOCKET, SO_ERROR, &error, &error_size) == 0);
        assert(error == 0);
    }
    assert(pending);

    meshtastic::MeshtasticClient client({"127.0.0.1", ntohs(address.sin_port)});
    client.start();
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (client.snapshot().state == meshtastic::ConnectionState::connecting) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(client.snapshot().state == meshtastic::ConnectionState::connecting);
    const auto disable_start = std::chrono::steady_clock::now();
    client.set_enabled(false);
    for (int attempt = 0; attempt < 75; ++attempt) {
        if (client.snapshot().state == meshtastic::ConnectionState::stopped) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(client.snapshot().state == meshtastic::ConnectionState::stopped);
    assert(std::chrono::steady_clock::now() - disable_start < std::chrono::milliseconds(750));
    client.set_enabled(true);
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (client.snapshot().state == meshtastic::ConnectionState::connecting) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(client.snapshot().state == meshtastic::ConnectionState::connecting);
    const auto start = std::chrono::steady_clock::now();
    client.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    for (const int descriptor : fillers) ::close(descriptor);
    ::close(listener);
    std::fprintf(stderr, "stop during pending TCP connect: %.1f ms\n",
                 std::chrono::duration<double, std::milli>(elapsed).count());
    assert(elapsed < std::chrono::milliseconds(750));
    assert(client.snapshot().state == meshtastic::ConnectionState::stopped);
}

}  // namespace

int main()
{
    assert(std::string(meshtastic::kDefaultTcpHost) == "192.168.8.114");
    assert(meshtastic::kDefaultTcpPort == 4403);
    test_encoding();
    test_framing();
    test_node_decode();
    test_text_and_handshake_decode();
    test_channel_decode();
    test_tcp_client();
    test_stop_with_unresponsive_receiver();
    test_stop_during_pending_connect();
    return 0;
}
