#ifndef LILYGO_UI_MESHTASTIC_ADMIN_TRANSACTION_HPP
#define LILYGO_UI_MESHTASTIC_ADMIN_TRANSACTION_HPP

#include "domain/meshtastic_client.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace meshtastic::configuration {

// Serialize administrative transactions: each operation is acknowledged before
// the next one is sent, and readback is tied to the exact GET request id.
class AdminTransaction {
public:
    AdminTransaction() = default;
    ~AdminTransaction();
    AdminTransaction(const AdminTransaction &) = delete;
    AdminTransaction &operator=(const AdminTransaction &) = delete;
    AdminTransaction(AdminTransaction &&other) noexcept;
    AdminTransaction &operator=(AdminTransaction &&other) noexcept;
    enum class State { idle, waiting, confirmed, failed };
    using Matcher = std::function<bool(const AdminMessage &, const AdminMessage &)>;
    struct Readback { AdminMessage request, expected; Matcher match = {}; };
    bool start(MeshtasticClient &client, std::vector<AdminMessage> writes,
               std::vector<Readback> reads, std::string &error, std::uint32_t expected_radio = 0);
    State advance(MeshtasticClient &client, const ClientSnapshot &snapshot, std::string &error,
                  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void reset() noexcept;
    State state() const noexcept { return state_; }
    std::uint32_t packet_id() const noexcept { return packet_id_; }
    static bool responses_match(const AdminMessage &expected, const AdminMessage &received);

private:
    bool send_current(MeshtasticClient &client, std::string &error);
    void fail() noexcept;
    std::vector<AdminMessage> writes_;
    std::vector<Readback> reads_;
    std::optional<MeshtasticClient::AdminTransactionLease> lease_;
    MeshtasticClient *owner_ = nullptr;
    bool sent_any_ = false;
    std::size_t index_ = 0;
    bool reading_ = false;
    std::uint32_t packet_id_ = 0;
    State state_ = State::idle;
    std::chrono::steady_clock::time_point deadline_{};
};
}  // namespace meshtastic::configuration
#endif
