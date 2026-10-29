#include "services/radio_session.hpp"
#include <algorithm>
namespace meshtastic {
RadioSession::RadioSession(std::chrono::milliseconds timeout) : timeout_(timeout) {}
void RadioSession::reset(const std::vector<DeliveryRecord> &records)
{
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
    next_sequence_ = 0;
    // Repository returns the most recently inserted records first.
    for (auto iterator=records.rbegin();iterator!=records.rend();++iterator) {
        auto &pending=records_[iterator->packet_id];
        pending.delivery=*iterator;
        pending.sequence=++next_sequence_;
    }
}
DeliveryRecord RadioSession::queued(const MeshPacket &packet, std::uint32_t previous_attempts)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(records_.size()>8192) {
        for(auto iterator=records_.begin();iterator!=records_.end()&&records_.size()>4096;) {
            const auto status=iterator->second.delivery.status;
            if(status!=MessageStatus::queued&&status!=MessageStatus::sent)iterator=records_.erase(iterator);else ++iterator;
        }
    }
    auto &pending = records_[packet.id()];
    pending.sequence=++next_sequence_;
    pending.delivery.attempts=std::max(pending.delivery.attempts,previous_attempts);
    pending.delivery.packet_id = packet.id();
    pending.delivery.portnum = packet.has_decoded() ? packet.decoded().portnum() : 0;
    pending.delivery.source = packet.from();
    pending.delivery.destination = packet.to();
    pending.delivery.channel = packet.channel();
    pending.delivery.status = MessageStatus::queued;
    pending.delivery.detail = "Waiting for the TCP session to write this packet";
    return pending.delivery;
}
std::optional<DeliveryRecord> RadioSession::sent(std::uint32_t id, std::uint32_t epoch)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = records_.find(id);
    if (found == records_.end() || found->second.delivery.status != MessageStatus::queued) return std::nullopt;
    auto &pending = found->second;
    pending.delivery.status = MessageStatus::sent;
    pending.delivery.sent_at = epoch;
    ++pending.delivery.attempts;
    pending.delivery.detail = "Written to radio transport; awaiting a routing acknowledgement";
    pending.deadline = std::chrono::steady_clock::now() + timeout_;
    return pending.delivery;
}
std::optional<DeliveryRecord> RadioSession::routing(const MeshPacket &packet, const Routing &routing)
{
    if (!packet.has_decoded() || packet.decoded().portnum()!=ROUTING_APP ||
        !packet.decoded().request_id() || !routing.has_error_reason()) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = records_.find(packet.decoded().request_id());
    if (found == records_.end() ||
        (found->second.delivery.status != MessageStatus::sent &&
         found->second.delivery.status != MessageStatus::timed_out)) return std::nullopt;
    auto &delivery = found->second.delivery;
    if (packet.to() != 0 && packet.to() != delivery.source) return std::nullopt;
    const bool local_sender = packet.from() == 0 || packet.from() == delivery.source;
    const bool target_sender = packet.from() == delivery.destination;
    const bool success = routing.error_reason() == Routing::NONE;
    const bool broadcast = delivery.destination == kBroadcastNode;
    // Local broadcast ACKs report that the radio observed a relay. For a direct
    // packet a relay ACK is not proof that the final destination received it.
    if (success) {
        const bool local_destination = delivery.destination == delivery.source;
        if (broadcast ? !local_sender : !(target_sender || (local_destination && local_sender)))
            return std::nullopt;
    } else if (!local_sender && !target_sender) {
        return std::nullopt;
    }
    // The firmware emits some local errors on channel zero, even when the
    // originating packet used a secondary channel.
    if (packet.channel() != delivery.channel && !(local_sender && !success && packet.channel() == 0))
        return std::nullopt;
    delivery.status = routing.error_reason() == Routing::NONE ? MessageStatus::acknowledged : MessageStatus::rejected;
    delivery.detail = routing.error_reason() == Routing::NONE ? "Routing ACK received" : Routing_Error_Name(routing.error_reason());
    return delivery;
}
std::vector<DeliveryRecord> RadioSession::expire()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeliveryRecord> changed;
    for (auto &[id, pending] : records_) {
        (void)id;
        if (pending.delivery.status == MessageStatus::sent && std::chrono::steady_clock::now() >= pending.deadline) {
            pending.delivery.status = MessageStatus::timed_out;
            pending.delivery.detail = "No routing ACK before the deadline; delivery is unknown. Retry explicitly.";
            changed.push_back(pending.delivery);
        }
    }
    return changed;
}
std::vector<DeliveryRecord> RadioSession::interrupt(const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeliveryRecord> changed;
    for (auto &[id, pending] : records_) {
        (void)id;
        if (pending.delivery.status == MessageStatus::queued || pending.delivery.status == MessageStatus::sent) {
            pending.delivery.status = MessageStatus::failed;
            pending.delivery.detail = reason + "; delivery is unknown. Retry explicitly.";
            changed.push_back(pending.delivery);
        }
    }
    return changed;
}
std::vector<DeliveryRecord> RadioSession::records() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const Pending *> ordered;
    ordered.reserve(records_.size());
    for(const auto &[id,pending]:records_) { (void)id; ordered.push_back(&pending); }
    std::sort(ordered.begin(),ordered.end(),[](const Pending *left,const Pending *right) {
        return left->sequence>right->sequence;
    });
    std::vector<DeliveryRecord> result;
    std::size_t completed=0;
    for(const auto *pending:ordered) {
        const auto status=pending->delivery.status;
        const bool active=status==MessageStatus::queued || status==MessageStatus::sent;
        if(active || completed++<200)result.push_back(pending->delivery);
    }
    std::reverse(result.begin(),result.end());
    return result;
}
}
