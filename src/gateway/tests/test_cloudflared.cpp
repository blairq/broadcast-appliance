// ============================================================================
// src/gateway/tests/test_cloudflared.cpp — supervisor de cloudflared
//
// Con un cloudflared falso (script de shell, solo POSIX):
//   1. Token invalido: muere diciendolo -> el supervisor NO lo relanza (antes
//      lo reintentaba para siempre y llenaba el log).
//   2. Caida comun: muere sin motivo conocido -> se relanza con backoff.
// ============================================================================
#include "Cloudflared.hpp"
#include "Log.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

#if !defined(_WIN32)
int g_failures = 0;
#endif

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

#if !defined(_WIN32)
std::filesystem::path fakeCloudflared(const std::filesystem::path& dir, const std::string& name,
                                      const std::string& message) {
    const auto p = dir / name;
    std::ofstream out(p);
    out << "#!/bin/sh\n";
    if (!message.empty()) out << "echo \"" << message << "\"\n";
    out << "exit 1\n";
    out.close();
    ::chmod(p.c_str(), 0755);
    return p;
}
#endif

} // namespace

int main() {
#if defined(_WIN32)
    std::printf("SKIP: el cloudflared falso es un script de shell (POSIX)\n");
    return 77;
#else
    using namespace std::chrono_literals;
    const auto dir =
        std::filesystem::temp_directory_path() / ("ba_cloudflared_test_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    ba::gateway::Log log;

    // 1) Token invalido: un solo intento.
    {
        const auto exe = fakeCloudflared(dir, "bad-token.sh", "Provided Tunnel token is not valid.");
        const auto logPath = (dir / "bad-token.log").string();
        ba::gateway::CloudflaredSupervisor sup(exe.string(), "no-es-un-token", 49991, logPath, log);
        CHECK(sup.start());
        std::this_thread::sleep_for(9s); // con el bug: relanzado a los ~3 s, ~5 s, ~9 s
        std::printf("  token invalido: %d reinicios\n", sup.restartCount());
        CHECK(sup.restartCount() == 0);
        CHECK(!sup.running());
        sup.stop();
    }

    // 2) Caida sin motivo conocido: se sigue reintentando.
    {
        const auto exe = fakeCloudflared(dir, "crash.sh", "");
        const auto logPath = (dir / "crash.log").string();
        ba::gateway::CloudflaredSupervisor sup(exe.string(), "token", 49992, logPath, log);
        CHECK(sup.start());
        std::this_thread::sleep_for(6s);
        std::printf("  caida comun: %d reinicios\n", sup.restartCount());
        CHECK(sup.restartCount() >= 1);
        sup.stop();
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (g_failures == 0) std::printf("test_cloudflared: OK\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
