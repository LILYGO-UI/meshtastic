#include "services/meshtasticd_service.hpp"

#include <cassert>

int main()
{
    meshtastic::Endpoint local{"127.0.0.1", meshtastic::kDefaultTcpPort};
    meshtastic::Endpoint hostname{"localhost", meshtastic::kDefaultTcpPort};
    meshtastic::Endpoint ipv6{"::1", meshtastic::kDefaultTcpPort};
    meshtastic::Endpoint remote{"192.0.2.1", meshtastic::kDefaultTcpPort};
    meshtastic::Endpoint alternate_port{"127.0.0.1", 4404};

    assert(meshtastic::is_local_meshtasticd_endpoint(local));
    assert(meshtastic::is_local_meshtasticd_endpoint(hostname));
    assert(meshtastic::is_local_meshtasticd_endpoint(ipv6));
    assert(!meshtastic::is_local_meshtasticd_endpoint(remote));
    assert(!meshtastic::is_local_meshtasticd_endpoint(alternate_port));
    assert(meshtastic::ensure_local_meshtasticd(remote) == meshtastic::MeshtasticdStartResult::not_needed);
    assert(meshtastic::ensure_local_meshtasticd(local) == meshtastic::MeshtasticdStartResult::unsupported);
    return 0;
}
