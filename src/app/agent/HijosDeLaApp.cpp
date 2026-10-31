// ============================================================================
// src/app/agent/HijosDeLaApp.cpp — ver HijosDeLaApp.hpp
// ============================================================================
#include "agent/HijosDeLaApp.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

#include <QFileInfo>
#include <QProcess>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <libproc.h>
#endif
#endif

namespace ba::app::agent::hijos {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path carpeta() { return ba::platform::dataRoot() / "Runtime" / "hijos"; }

fs::path anotacion(const QString& nombre) { return carpeta() / (nombre.toStdString() + ".json"); }

#if defined(_WIN32)
std::map<QString, HANDLE>& jobs() {
    static std::map<QString, HANDLE> j; // solo el hilo de la interfaz (los servidores son de ahí)
    return j;
}
#else
/// El ejecutable del proceso `pid` ("" si no existe o no se sabe).
fs::path ejecutableDe(pid_t pid) {
#if defined(__APPLE__)
    char ruta[PROC_PIDPATHINFO_MAXSIZE];
    return proc_pidpath(pid, ruta, sizeof ruta) > 0 ? fs::path(ruta) : fs::path();
#else
    std::error_code ec;
    const fs::path r = fs::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);
    return ec ? fs::path() : r;
#endif
}

/// El ejecutable, para comparar: con los enlaces resueltos.
fs::path real(const fs::path& p) {
    std::error_code ec;
    const fs::path r = fs::weakly_canonical(p, ec);
    return ec ? p : r;
}

bool vivo(pid_t pid) { return ::kill(pid, 0) == 0; }
#endif

} // namespace

void preparar(QProcess& p) {
#if defined(_WIN32)
    (void)p;
#else
    p.setChildProcessModifier([]() {
        ::setpgid(0, 0); // su grupo: parar() y barrerHuerfanos() se llevan también lo que lance
#if defined(__linux__)
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    });
#endif
}

void anotar(const QProcess& p, const QString& nombre) {
    const auto pid = static_cast<std::int64_t>(p.processId());
    if (pid <= 0) return;
#if defined(_WIN32)
    // Lo que lance nace adentro, y que se caiga la app (se cierra el handle) se lleva a todos.
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
        if (HANDLE hp = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
            AssignProcessToJobObject(job, hp);
            CloseHandle(hp);
        }
        if (auto it = jobs().find(nombre); it != jobs().end()) CloseHandle(it->second);
        jobs()[nombre] = job;
    }
#endif
    std::error_code ec;
    fs::create_directories(carpeta(), ec);
    // Ya, sin la cola: si la app se cae enseguida, el barrido del próximo arranque tiene que encontrarlo.
    ba::persist::escribir(anotacion(nombre),
                          json{{"pid", pid}, {"exe", QFileInfo(p.program()).canonicalFilePath().toStdString()}}.dump(
                              -1, ' ', false, json::error_handler_t::replace));
}

void parar(QProcess& p, const QString& nombre) {
    std::error_code ec;
    fs::remove(anotacion(nombre), ec);
#if defined(_WIN32)
    if (auto it = jobs().find(nombre); it != jobs().end()) {
        TerminateJobObject(it->second, 1); // él y lo que haya lanzado
        CloseHandle(it->second);
        jobs().erase(it);
    }
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(2000);
    }
#else
    const auto pid = static_cast<pid_t>(p.processId());
    if (p.state() != QProcess::NotRunning && pid > 0) {
        ::kill(-pid, SIGTERM);
        if (!p.waitForFinished(3000)) ::kill(-pid, SIGKILL);
        p.waitForFinished(2000);
    }
    if (pid > 0) ::kill(-pid, SIGKILL); // lo que quedó del grupo (él ya se fue)
#endif
}

void barrerHuerfanos() {
    std::error_code ec;
    for (const auto& a : fs::directory_iterator(carpeta(), ec)) {
        if (a.path().extension() != ".json") continue;
        json j;
        {
            std::ifstream f(a.path(), std::ios::binary);
            j = json::parse(f, nullptr, false);
        }
        fs::remove(a.path(), ec);
#if !defined(_WIN32) // en Windows ya se los llevó el Job Object
        if (!j.is_object() || !j.value("pid", json()).is_number_integer()) continue;
        const auto pid = static_cast<pid_t>(j["pid"].get<std::int64_t>());
        const std::string exe = j.value("exe", std::string());
        if (pid <= 1 || exe.empty() || !vivo(pid) || real(ejecutableDe(pid)).string() != exe) continue;
        ::kill(-pid, SIGTERM);
        ::kill(pid, SIGTERM);
        for (int i = 0; i < 20 && vivo(pid); ++i) ::usleep(50'000);
        ::kill(-pid, SIGKILL);
        ::kill(pid, SIGKILL);
        ba::diag::registro().info(ba::i18n::llenar(
            ba::i18n::traducir("agente", "quedaba andando %1 (pid %2) de una corrida que no lo cerró: parado"),
            {a.path().stem().string(), std::to_string(pid)}));
#endif
    }
}

} // namespace ba::app::agent::hijos
