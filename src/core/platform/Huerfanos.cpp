// ============================================================================
// src/core/platform/Huerfanos.cpp — las anotaciones y el barrido (ver el .hpp)
// Lo de cada sistema (vivo, ejecutableDe, descendientes, terminar): Huerfanos_posix.cpp
// y Huerfanos_win.cpp.
// ============================================================================
#include "platform/Huerfanos.hpp"

#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ba::platform::huerfanos {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
std::int64_t yo() {
#if defined(_WIN32)
    return static_cast<std::int64_t>(::_getpid());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

/// Para comparar ejecutables: con los enlaces resueltos.
std::string real(const fs::path& p) {
    if (p.empty()) return {};
    std::error_code ec;
    const fs::path r = fs::weakly_canonical(p, ec);
    return (ec ? p : r).string();
}

/// `pid` sigue siendo ese programa (un pid reusado por otro no cuenta).
bool esEse(std::int64_t pid, const std::string& exe) {
    return pid > 0 && !exe.empty() && vivo(pid) && real(ejecutableDe(pid)) == exe;
}
} // namespace

fs::path carpeta() { return ba::platform::dataRoot() / "Runtime" / "hijos"; }

void anotar(const std::string& nombre, std::int64_t pid, const fs::path& exe) {
    if (pid <= 0) return;
    std::error_code ec;
    fs::create_directories(carpeta(), ec);
    // Ya, sin la cola: si la app se cae enseguida, el próximo barrido tiene que encontrarlo.
    const json j{{"pid", pid}, {"exe", real(exe)}, {"dueno", yo()}, {"dueno_exe", real(ejecutableDe(yo()))}};
    ba::persist::escribir(carpeta() / (nombre + ".json"), j.dump(-1, ' ', false, json::error_handler_t::replace));
}

void quitar(const std::string& nombre) {
    std::error_code ec;
    fs::remove(carpeta() / (nombre + ".json"), ec);
}

void terminarArbol(std::int64_t pid, int graciaMs) {
    if (pid <= 0) return;
    // Los descendientes ANTES: muerto el padre, los desprendidos pasan a ser de launchd/init.
    std::vector<std::int64_t> todos{pid};
    for (const std::int64_t d : descendientes(pid)) todos.push_back(d);
    terminar(todos, graciaMs);
}

std::vector<Barrido> barrer(const fs::path& dir) {
    std::vector<Barrido> out;
    std::error_code ec;
    for (const auto& a : fs::directory_iterator(dir, ec)) {
        if (a.path().extension() != ".json") continue;
        json j;
        {
            std::ifstream f(a.path(), std::ios::binary);
            j = json::parse(f, nullptr, false);
        }
        // El que lo lanzó sigue andando (el launcher volvió y readoptó a la app): es suyo.
        // Sin dueño anotado (de una versión anterior), como antes: huérfano.
        if (j.is_object() && j.value("dueno", json()).is_number_integer() &&
            esEse(j["dueno"].get<std::int64_t>(), j.value("dueno_exe", std::string())))
            continue;
        fs::remove(a.path(), ec);
        if (!j.is_object() || !j.value("pid", json()).is_number_integer()) continue;
        const std::int64_t pid = j["pid"].get<std::int64_t>();
        if (pid <= 1 || !esEse(pid, j.value("exe", std::string()))) continue;
#if !defined(_WIN32) // en Windows ya se los llevó el Job Object
        terminarArbol(pid, 1000);
        out.push_back({a.path().stem().string(), pid});
#endif
    }
    return out;
}

} // namespace ba::platform::huerfanos
