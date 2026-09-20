// ============================================================================
// src/core/platform/Process.hpp — Procesos hijos, igual en las tres plataformas
//
// Proposito:
//   Lanzar, vigilar y terminar procesos hijos (el launcher lanza A y B; B
//   lanza cloudflared). Reemplaza al `ChildProcess` del launcher, que era
//   Win32 real y un PID inventado en el resto.
//
// Implementaciones:
//   - Windows (Process_win.cpp): CreateProcessW + Job Object con
//     KILL_ON_JOB_CLOSE: si el padre muere, el SO mata a los hijos.
//   - Linux (Process_posix.cpp): fork + execve, con PR_SET_PDEATHSIG en el
//     hijo (mismo efecto que el Job Object) y grupo de procesos propio.
//   - macOS (Process_posix.cpp): posix_spawn con grupo propio. macOS no tiene
//     PDEATHSIG: un hijo propio que deba morir con el padre llama a
//     `exitWhenParentDies()` al arrancar (vigila al padre con kqueue).
//
// Quien usa killWithParent: los procesos auxiliares (cloudflared, helpers).
// A y B NO: la emision tiene que sobrevivir a un crash del launcher, que al
// volver los adopta con `attach()` (spec §3.1.9).
//
// Codigo de salida:
//   Normal -> el codigo del proceso. Muerto por señal (POSIX) -> 128 + señal,
//   la convencion de los shells, para que el supervisor distinga un crash sin
//   mirar el SO.
//
// Modelo de threading:
//   Un `Process` es de un solo dueño (move-only) y no es thread-safe.
//
// Estrategia de errores:
//   No lanza. `spawn` devuelve false y deja el motivo en `err`.
// ============================================================================

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ba::platform {

/// Opciones de lanzamiento de un hijo.
struct ProcessOptions {
    std::filesystem::path exe;                 ///< ruta del ejecutable (absoluta recomendada)
    std::vector<std::string> args;             ///< argumentos SIN el argv[0]
    std::filesystem::path workDir;             ///< cwd del hijo (vacio = el del padre)
    bool inheritEnv = true;                    ///< parte del entorno del padre
    std::vector<std::string> envRemove;        ///< variables a quitar (spec §3.1.3)
    std::map<std::string, std::string> envSet; ///< variables a fijar/reemplazar
    /// El hijo muere si el padre muere (y si se destruye el Process). Para A y
    /// B es FALSE: la emision tiene que sobrevivir a un crash del launcher.
    bool killWithParent = true;
    /// Windows: sin ventana de consola (CREATE_NO_WINDOW). NO oculta las
    /// ventanas graficas del hijo: un hijo con interfaz (A) se ve normal.
    bool hideWindow = true;
    /// Si no esta vacio, stdout y stderr del hijo van a este archivo (se
    /// crea si no existe y se AGREGA al final: no pisa corridas anteriores).
    std::filesystem::path outputFile;
};

/// Proceso hijo lanzado por `Process::spawn`.
class Process {
   public:
    Process() = default;
    ~Process();
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    /// Lanza un hijo.
    ///
    /// :param opts: opciones de lanzamiento.
    /// :param out: recibe el proceso lanzado.
    /// :param err: motivo del fallo (opcional).
    /// :returns: true si el hijo arranco.
    static bool spawn(const ProcessOptions& opts, Process& out, std::string* err = nullptr);

    /// Adopta un proceso YA vivo que no lanzo este proceso (readopt del
    /// launcher, spec §3.1.9: tras un crash del launcher, A y B siguen al aire
    /// y el launcher nuevo los adopta en vez de relanzarlos).
    ///
    /// Limitacion POSIX: un proceso adoptado no es hijo nuestro, asi que no se
    /// puede recoger su codigo de salida; `wait` informa -1 cuando desaparece.
    /// Un Process adoptado NO mata al proceso al destruirse (no es su dueño).
    ///
    /// :returns: true si el PID existe y se pudo abrir.
    static bool attach(std::uint32_t pid, Process& out);

    /// PID del hijo (0 si no hay).
    std::uint32_t pid() const { return pid_; }

    /// true si hay un hijo lanzado y todavia no se recogio su salida.
    bool valid() const { return pid_ != 0 && !exitCode_.has_value(); }

    /// Espera la salida hasta `timeoutMs` (0 = no bloquea, <0 = sin limite).
    ///
    /// :returns: el codigo de salida si termino; nullopt si sigue vivo.
    std::optional<int> wait(int timeoutMs);

    /// true si sigue vivo (equivale a `!wait(0)`).
    bool running() { return pid_ != 0 && !wait(0).has_value(); }

    /// Pide cierre "amable": SIGTERM en POSIX. En Windows no hay señal
    /// equivalente para un proceso GUI: el cierre ordenado va por el canal de
    /// control (spec §4.1), asi que aca equivale a `kill()`.
    bool terminate();

    /// Mata al hijo y a su grupo/job (SIGKILL / TerminateJobObject).
    bool kill();

   private:
    void reset();

    std::uint32_t pid_ = 0;
    std::optional<int> exitCode_;
    bool adopted_ = false;       // viene de attach(): no es hijo nuestro
    bool killOnDestroy_ = false; // killWithParent del spawn
#if defined(_WIN32)
    void* process_ = nullptr; // HANDLE del proceso
    void* job_ = nullptr;     // HANDLE del Job Object
#endif
};

/// Codigos de salida con significado para el supervisor (ademas de 0 =
/// cierre ordenado). Viven aca porque los comparten el launcher y los hijos.
namespace exit_code {
/// El proceso no puede funcionar en este entorno y reintentar enseguida no
/// va a cambiar nada (p. ej. A sin contexto grafico: sesion por SSH sin
/// DISPLAY/WAYLAND_DISPLAY). El supervisor espera mucho mas antes de
/// reintentar, en vez de relanzarlo en bucle.
inline constexpr int kEnvironment = 78; // EX_CONFIG de sysexits.h
} // namespace exit_code

/// Llamar al arrancar un proceso auxiliar propio que deba terminar si su
/// padre muere sin poder avisar. (A y B no la usan: ver arriba.)
///
/// Linux: PR_SET_PDEATHSIG(SIGTERM). macOS: hilo que espera NOTE_EXIT del
/// padre en un kqueue y se envia SIGTERM. Windows: no hace nada (el Job
/// Object del padre con killWithParent ya lo garantiza).
void exitWhenParentDies();

/// Evita que un crash de este proceso (y de los hijos, que lo heredan) abra
/// un dialogo del sistema que bloquee el reinicio automatico (spec §3.1.4).
/// Windows: SetErrorMode. POSIX: no hay dialogos; no hace nada.
void suppressCrashDialogs();

} // namespace ba::platform
