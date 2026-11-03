#include "services/meshtasticd_service.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace meshtastic {
namespace {

#if defined(__linux__) && !defined(MESHTASTIC_SIMULATOR)
const char *find_executable(const char *primary, const char *fallback) noexcept
{
    if (access(primary, X_OK) == 0) return primary;
    if (access(fallback, X_OK) == 0) return fallback;
    return nullptr;
}

int run_command(const char *executable, std::initializer_list<const char *> arguments) noexcept
{
    if (!executable) return 127;

    std::array<char *, 10> argv{};
    std::size_t argument_count = 0;
    argv[argument_count++] = const_cast<char *>(executable);
    for (const auto *argument : arguments) {
        if (argument_count + 1 >= argv.size()) return 126;
        argv[argument_count++] = const_cast<char *>(argument);
    }
    argv[argument_count] = nullptr;

    const auto child = fork();
    if (child < 0) return 126;
    if (child == 0) {
        execv(executable, argv.data());
        _exit(127);
    }

    int status = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
            return 126;
        }
        if (result < 0 && errno != EINTR) return 126;
        usleep(100'000);
    }

    kill(child, SIGTERM);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return 124;
}
#endif

}  // namespace

bool is_local_meshtasticd_endpoint(const Endpoint &endpoint) noexcept
{
    return endpoint.port == kDefaultTcpPort &&
           (endpoint.host == "127.0.0.1" || endpoint.host == "localhost" || endpoint.host == "::1");
}

MeshtasticdStartResult ensure_local_meshtasticd(const Endpoint &endpoint) noexcept
{
    if (!is_local_meshtasticd_endpoint(endpoint)) return MeshtasticdStartResult::not_needed;

#if !defined(__linux__) || defined(MESHTASTIC_SIMULATOR)
    (void)endpoint;
    return MeshtasticdStartResult::unsupported;
#else
    const auto *systemctl = find_executable("/usr/bin/systemctl", "/bin/systemctl");
    if (!systemctl) {
        std::fprintf(stderr, "meshtasticd: systemctl is unavailable\n");
        return MeshtasticdStartResult::unavailable;
    }

    constexpr const char *service = "meshtasticd.service";
    if (run_command(systemctl, {"is-active", "--quiet", service}) == 0)
        return MeshtasticdStartResult::already_running;

    const auto *sudo = find_executable("/usr/bin/sudo", "/bin/sudo");
    if (!sudo) {
        std::fprintf(stderr, "meshtasticd: sudo is unavailable; cannot start %s\n", service);
        return MeshtasticdStartResult::unavailable;
    }

    // A previous hardware-detection failure can leave systemd at
    // start-limit-hit. Reset only that transient failure before retrying.
    const auto reset = run_command(
        sudo, {"-n", "--", systemctl, "--no-ask-password", "reset-failed", service});
    if (reset != 0) {
        std::fprintf(stderr, "meshtasticd: unable to reset %s failure state (exit %d)\n", service, reset);
    }

    const auto start = run_command(
        sudo, {"-n", "--", systemctl, "--no-ask-password", "start", "--no-block", service});
    if (start == 0) {
        std::fprintf(stderr, "meshtasticd: requested startup of %s\n", service);
        return MeshtasticdStartResult::start_requested;
    }

    std::fprintf(stderr, "meshtasticd: failed to start %s (exit %d)\n", service, start);
    return MeshtasticdStartResult::failed;
#endif
}

}  // namespace meshtastic
