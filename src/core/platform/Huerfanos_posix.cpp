// ============================================================================
// src/core/platform/Huerfanos_posix.cpp — los procesos, en Linux y macOS (ver el .hpp)
// ============================================================================
#include "platform/Huerfanos.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <thread>

#include <unistd.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#endif

namespace ba::platform::huerfanos {

namespace fs = std::filesystem;

namespace {
#if defined(__APPLE__)
std::vector<std::int64_t> hijosDe(std::int64_t pid) {
    std::vector<pid_t> buf(256);
    for (;;) {
        const int bytes = proc_listpids(PROC_PPID_ONLY, static_cast<uint32_t>(pid), buf.data(),
                                        static_cast<int>(buf.size() * sizeof(pid_t)));
        if (bytes <= 0) return {};
        const auto n = static_cast<std::size_t>(bytes) / sizeof(pid_t);
        if (n < buf.size()) return {buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n)};
        buf.resize(buf.size() * 2); // pudo no entrar: de nuevo con más lugar
    }
}
#else
/// Lo que dice /proc/<pid>/stat después del nombre (que puede tener espacios y paréntesis).
std::string statDe(std::int64_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string s;
    std::getline(f, s);
    const auto p = s.rfind(')');
    return p == std::string::npos ? std::string() : s.substr(p + 2); // «S ppid …»
}

std::vector<std::int64_t> hijosDe(std::int64_t pid) {
    std::vector<std::int64_t> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/proc", ec)) {
        const std::string n = e.path().filename().string();
        if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos) continue;
        const std::string st = statDe(std::stoll(n));
        const auto sp = st.find(' ');
        if (sp != std::string::npos && std::atoll(st.c_str() + sp + 1) == pid) out.push_back(std::stoll(n));
    }
    return out;
}
#endif
} // namespace

bool vivo(std::int64_t pid) {
    if (pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0) return false;
#if defined(__APPLE__)
    proc_bsdinfo info{};
    if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof info) == sizeof info)
        return info.pbi_status != SZOMB;
    return true;
#else
    return statDe(pid).rfind("Z", 0) != 0;
#endif
}

fs::path ejecutableDe(std::int64_t pid) {
    if (pid <= 0) return {};
#if defined(__APPLE__)
    char ruta[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(static_cast<int>(pid), ruta, sizeof ruta) <= 0) return {};
    const fs::path r(ruta);
#else
    std::error_code ec;
    const fs::path r = fs::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);
    if (ec) return {};
#endif
    std::error_code ec2;
    const fs::path c = fs::weakly_canonical(r, ec2);
    return ec2 ? r : c;
}

std::vector<std::int64_t> descendientes(std::int64_t pid) {
    std::vector<std::int64_t> out;
    std::set<std::int64_t> vistos{pid};
    std::vector<std::int64_t> pendientes{pid};
    while (!pendientes.empty()) {
        const std::int64_t p = pendientes.back();
        pendientes.pop_back();
        for (const std::int64_t h : hijosDe(p))
            if (h > 1 && vistos.insert(h).second) {
                out.push_back(h);
                pendientes.push_back(h);
            }
    }
    return out;
}

void terminar(const std::vector<std::int64_t>& pids, int graciaMs) {
    // Cada uno y su grupo: lo que haya lanzado sin desprenderse va con él.
    const auto senal = [&pids](int s) {
        for (const std::int64_t p : pids)
            if (p > 1) {
                ::kill(-static_cast<pid_t>(p), s);
                if (vivo(p)) ::kill(static_cast<pid_t>(p), s);
            }
    };
    if (graciaMs > 0) {
        senal(SIGTERM);
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(graciaMs);
        const auto quedan = [&pids]() {
            for (const std::int64_t p : pids)
                if (vivo(p)) return true;
            return false;
        };
        while (quedan() && std::chrono::steady_clock::now() < hasta)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    senal(SIGKILL); // los que quedan (y los grupos de los que ya se fueron)
}

} // namespace ba::platform::huerfanos
