#ifndef LILYGO_UI_MESHTASTIC_MESHTASTICD_SERVICE_HPP
#define LILYGO_UI_MESHTASTIC_MESHTASTICD_SERVICE_HPP

#include "domain/meshtastic_client.hpp"

namespace meshtastic {

enum class MeshtasticdStartResult {
    not_needed,
    already_running,
    start_requested,
    unavailable,
    failed,
    unsupported,
};

[[nodiscard]] bool is_local_meshtasticd_endpoint(const Endpoint &endpoint) noexcept;
[[nodiscard]] MeshtasticdStartResult ensure_local_meshtasticd(const Endpoint &endpoint) noexcept;

}  // namespace meshtastic

#endif
