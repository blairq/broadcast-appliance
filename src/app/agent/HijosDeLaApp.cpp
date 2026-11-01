// ============================================================================
// src/app/agent/HijosDeLaApp.cpp — ver HijosDeLaApp.hpp
// ============================================================================
#include "agent/HijosDeLaApp.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "platform/Huerfanos.hpp"

#include <QFileInfo>
#include <QProcess>

#include <map>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

namespace ba::app::agent::hijos {

namespace huerfanos = ba::platform::huerfanos;

namespace {
#if defined(_WIN32)
std::map<QString, HANDLE>& jobs() {
    static std::map<QString, HANDLE> j; // solo el hilo de la interfaz (los servidores son de ahí)
    return j;
}
/// Cierra su Job Object; con `matar`, antes se lleva todo lo de adentro.
void soltarJob(const QString& nombre, bool matar) {
    if (auto it = jobs().find(nombre); it != jobs().end()) {
        if (matar) TerminateJobObject(it->second, 1); // él y lo que haya lanzado
        CloseHandle(it->second);
        jobs().erase(it);
    }
}
#endif
} // namespace

void preparar(QProcess& p) {
#if defined(_WIN32)
    (void)p;
#else
    p.setChildProcessModifier([]() {
        ::setpgid(0, 0); // su grupo: parar() se lleva también lo que lance sin desprenderse
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
        soltarJob(nombre, false);
        jobs()[nombre] = job;
    }
#endif
    huerfanos::anotar(nombre.toStdString(), pid, QFileInfo(p.program()).canonicalFilePath().toStdString());
}

void olvidar(const QString& nombre) {
    huerfanos::quitar(nombre.toStdString());
#if defined(_WIN32)
    soltarJob(nombre, false);
#endif
}

void parar(QProcess& p, const QString& nombre) {
    huerfanos::quitar(nombre.toStdString());
#if defined(_WIN32)
    soltarJob(nombre, true);
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(2000);
    }
#else
    const auto pid = static_cast<std::int64_t>(p.processId());
    if (p.state() != QProcess::NotRunning && pid > 0) {
        huerfanos::terminarArbol(pid, 3000); // él, su grupo y los que lanzó desprendidos
        p.waitForFinished(2000);
    }
#endif
}

void matar(QProcess& p, const QString& nombre) {
    huerfanos::quitar(nombre.toStdString());
#if defined(_WIN32)
    soltarJob(nombre, true);
    p.kill();
#else
    const auto pid = static_cast<std::int64_t>(p.processId());
    if (p.state() != QProcess::NotRunning && pid > 0) huerfanos::terminarArbol(pid, 0);
#endif
}

void barrerHuerfanos() {
    for (const huerfanos::Barrido& b : huerfanos::barrer())
        ba::diag::registro().info(ba::i18n::llenar(
            ba::i18n::traducir("agente", "quedaba andando %1 (pid %2) de una corrida que no lo cerró: parado"),
            {b.nombre, std::to_string(b.pid)}));
}

} // namespace ba::app::agent::hijos
