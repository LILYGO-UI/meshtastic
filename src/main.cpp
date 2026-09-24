#include "app.hpp"

#include "app_identity.h"
#include "domain/meshtastic_client.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool parse_port(const char *value, std::uint16_t &port)
{
    if (!value || !*value) return false;
    unsigned parsed   = 0;
    const auto end    = value + std::strlen(value);
    const auto result = std::from_chars(value, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0 || parsed > 65535) return false;
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

}  // namespace

int main(int argc, char **argv)
{
    std::string host   = meshtastic::kDefaultTcpHost;
    std::uint16_t port = meshtastic::kDefaultTcpPort;
    std::vector<char *> runtime_args;
    runtime_args.reserve(static_cast<std::size_t>(argc) + 1);
    runtime_args.push_back(argv[0]);

    for (int index = 1; index < argc; ++index) {
        if (std::strncmp(argv[index], "--host=", 7) == 0) {
            host = argv[index] + 7;
        } else if (std::strcmp(argv[index], "--host") == 0) {
            if (++index >= argc) {
                std::fprintf(stderr, "%s: --host requires a value\n", LILYGO_UI_MESHTASTIC_APP_ID);
                return 2;
            }
            host = argv[index];
        } else if (std::strncmp(argv[index], "--port=", 7) == 0) {
            if (!parse_port(argv[index] + 7, port)) {
                std::fprintf(stderr, "%s: invalid TCP port '%s'\n", LILYGO_UI_MESHTASTIC_APP_ID, argv[index] + 7);
                return 2;
            }
        } else if (std::strcmp(argv[index], "--port") == 0) {
            if (++index >= argc || !parse_port(argv[index], port)) {
                std::fprintf(stderr, "%s: --port requires a value from 1 to 65535\n", LILYGO_UI_MESHTASTIC_APP_ID);
                return 2;
            }
        } else {
            runtime_args.push_back(argv[index]);
        }
    }

    if (host.empty()) {
        std::fprintf(stderr, "%s: --host must not be empty\n", LILYGO_UI_MESHTASTIC_APP_ID);
        return 2;
    }

    runtime_args.push_back(nullptr);
    lilygo_ui_meshtastic_configure(std::move(host), port);
    return cm0_app_run(static_cast<int>(runtime_args.size() - 1), runtime_args.data(), cm0_app_get_descriptor());
}
