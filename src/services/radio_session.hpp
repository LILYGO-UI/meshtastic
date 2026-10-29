#ifndef LILYGO_UI_MESHTASTIC_RADIO_SESSION_HPP
#define LILYGO_UI_MESHTASTIC_RADIO_SESSION_HPP
#include "domain/meshtastic_protocol.hpp"
#include <chrono>
#include <map>
#include <mutex>

namespace meshtastic {
// Tracks one radio session. It never retransmits automatically; retry is explicit.
class RadioSession {
public:
    explicit RadioSession(std::chrono::milliseconds timeout);
    void reset(const std::vector<DeliveryRecord> &records = {});
    DeliveryRecord queued(const MeshPacket &packet, std::uint32_t previous_attempts = 0);
    std::optional<DeliveryRecord> sent(std::uint32_t packet_id, std::uint32_t epoch);
    std::optional<DeliveryRecord> routing(const MeshPacket &packet, const Routing &routing);
    std::vector<DeliveryRecord> expire();
    std::vector<DeliveryRecord> interrupt(const std::string &reason);
    std::vector<DeliveryRecord> records() const;
private:
    struct Pending {
        DeliveryRecord delivery;
        std::chrono::steady_clock::time_point deadline;
        std::uint64_t sequence = 0;
    };
    std::chrono::milliseconds timeout_;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, Pending> records_;
    std::uint64_t next_sequence_ = 0;
};
}
#endif
