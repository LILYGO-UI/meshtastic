#include "configuration/admin_transaction.hpp"

#include <algorithm>
#include <utility>

namespace meshtastic::configuration {

AdminTransaction::~AdminTransaction() { reset(); }

AdminTransaction::AdminTransaction(AdminTransaction &&other) noexcept
    : writes_(std::move(other.writes_)), reads_(std::move(other.reads_)), lease_(std::move(other.lease_)),
      owner_(std::exchange(other.owner_, nullptr)), sent_any_(std::exchange(other.sent_any_, false)),
      index_(other.index_), reading_(other.reading_), packet_id_(other.packet_id_),
      state_(std::exchange(other.state_, State::idle)), deadline_(other.deadline_)
{
}

AdminTransaction &AdminTransaction::operator=(AdminTransaction &&other) noexcept
{
    if (this == &other) return *this;
    reset();
    writes_ = std::move(other.writes_);
    reads_ = std::move(other.reads_);
    lease_ = std::move(other.lease_);
    owner_ = std::exchange(other.owner_, nullptr);
    sent_any_ = std::exchange(other.sent_any_, false);
    index_ = other.index_;
    reading_ = other.reading_;
    packet_id_ = other.packet_id_;
    state_ = std::exchange(other.state_, State::idle);
    deadline_ = other.deadline_;
    return *this;
}

void AdminTransaction::fail() noexcept
{
    state_ = State::failed;
    if (lease_ && sent_any_) lease_->abandon();
    lease_.reset();
    owner_ = nullptr;
}

bool AdminTransaction::responses_match(const AdminMessage &expected, const AdminMessage &received)
{
    if (expected.has_get_config_response()) {
        return received.has_get_config_response() &&
               expected.get_config_response().SerializeAsString() == received.get_config_response().SerializeAsString();
    }
    if (expected.has_get_module_config_response()) {
        return received.has_get_module_config_response() &&
               expected.get_module_config_response().SerializeAsString() == received.get_module_config_response().SerializeAsString();
    }
    if (expected.has_get_channel_response()) {
        return received.has_get_channel_response() &&
               expected.get_channel_response().SerializeAsString() == received.get_channel_response().SerializeAsString();
    }
    if (expected.has_get_owner_response()) {
        return received.has_get_owner_response() &&
               expected.get_owner_response().SerializeAsString() == received.get_owner_response().SerializeAsString();
    }
    return false;
}

bool AdminTransaction::start(MeshtasticClient &client, std::vector<AdminMessage> writes,
                             std::vector<Readback> reads, std::string &error, std::uint32_t expected_radio)
{
    if (state_ == State::waiting || writes.empty() || reads.empty()) {
        error = "An administrative save is already pending or has no operations";
        return false;
    }
    lease_ = client.acquire_admin_transaction(error, expected_radio);
    if (!lease_) {
        state_ = State::failed;
        return false;
    }
    owner_ = &client;
    sent_any_ = false;
    writes_ = std::move(writes);
    reads_ = std::move(reads);
    index_ = 0;
    reading_ = false;
    state_ = State::waiting;
    return send_current(client, error);
}

bool AdminTransaction::send_current(MeshtasticClient &client, std::string &error)
{
    const auto &request = reading_ ? reads_[index_].request : writes_[index_];
    const auto id = lease_ && owner_ == &client ? client.queue_admin_tracked(request, *lease_) : std::nullopt;
    if (!id) {
        error = reading_ ? "Unable to queue device readback; save is not confirmed"
                         : "Unable to queue administrative write; save is not confirmed";
        fail();
        return false;
    }
    packet_id_ = *id;
    sent_any_ = true;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    error.clear();
    return true;
}

AdminTransaction::State AdminTransaction::advance(MeshtasticClient &client, const ClientSnapshot &snapshot,
                                                  std::string &error, std::chrono::steady_clock::time_point now)
{
    if (state_ != State::waiting) return state_;
    if (!lease_ || !lease_->valid() || owner_ != &client ||
        (snapshot.my_node_number != 0 && snapshot.my_node_number != lease_->radio_number())) {
        error = "Radio connection changed before confirmation; the device state is unknown";
        fail();
        return state_;
    }
    if (snapshot.state != ConnectionState::connected || !snapshot.config_complete || snapshot.my_node_number == 0 ||
        snapshot.session_passkey.empty()) {
        error = "Connection lost before confirmation; the device state is unknown";
        fail();
        return state_;
    }
    const auto delivery = std::find_if(snapshot.deliveries.begin(), snapshot.deliveries.end(),
        [&](const DeliveryRecord &item) { return item.packet_id == packet_id_; });
    if (reading_) {
        const auto response = snapshot.admin_responses.find(packet_id_);
        if (response != snapshot.admin_responses.end()) {
            const bool matched = reads_[index_].match ? reads_[index_].match(reads_[index_].expected, response->second)
                                                       : responses_match(reads_[index_].expected, response->second);
            if (!matched) {
                error = "Device readback differs from the requested settings";
                fail();
                return state_;
            }
            if (++index_ == reads_.size()) {
                state_ = State::confirmed;
                lease_->complete();
                lease_.reset();
                owner_ = nullptr;
                return state_;
            }
            send_current(client, error);
            return state_;
        }
    }
    if (delivery != snapshot.deliveries.end() &&
        (delivery->status == MessageStatus::rejected || delivery->status == MessageStatus::timed_out ||
         delivery->status == MessageStatus::failed)) {
        error = reading_ ? "Device readback failed; save is not confirmed" : "Device rejected or could not receive the write";
        fail();
        return state_;
    }
    if (!reading_ && delivery != snapshot.deliveries.end() && delivery->status == MessageStatus::acknowledged) {
        if (++index_ == writes_.size()) { reading_ = true; index_ = 0; }
        send_current(client, error);
    }
    if (state_ == State::waiting && now >= deadline_) {
        error = reading_ ? "Device readback timed out; save is not confirmed" : "Device write acknowledgement timed out";
        fail();
    }
    return state_;
}

void AdminTransaction::reset() noexcept
{
    if (state_ == State::waiting && lease_ && sent_any_) lease_->abandon();
    writes_.clear();
    reads_.clear();
    index_ = 0;
    reading_ = false;
    packet_id_ = 0;
    lease_.reset();
    owner_ = nullptr;
    sent_any_ = false;
    state_ = State::idle;
}
}  // namespace meshtastic::configuration
