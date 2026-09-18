// ChildProcess.hpp
//
// Proposito:
//   Lanzamiento y seguimiento de un proceso hijo del launcher (spec 3.1.6),
//   sobre `ba::platform::Process` (igual en Windows, Linux y macOS). Agrega lo
//   propio del launcher: la politica de entorno saneado (spec 3.1.3), el
//   readopt de un hijo que sobrevivio a un launcher anterior (3.1.9) y el log.
//
//   Los hijos se lanzan SIN killWithParent: si el launcher se cae, A y B
//   siguen al aire y el launcher que vuelve los adopta.
//
// Politica de entorno:
//   - Windows: entorno minimo armado a mano (SystemRoot, PATH = System32 +
//     dir propio, TEMP/TMP) mas las variables de GPU de alto rendimiento para
//     laptops hibridas (spec 4.2).
//   - Linux/macOS: se hereda el entorno (hace falta DISPLAY, WAYLAND_DISPLAY,
//     XDG_RUNTIME_DIR, PulseAudio/PipeWire...) quitando lo que puede romper el
//     bundle: LD_PRELOAD/LD_LIBRARY_PATH, DYLD_*, QT_* y PYTHON*.
//
// Modelo de threading:
//   Se usa desde el thread del supervisor. No es thread-safe.
//
// Estrategia de errores:
//   start()/attach() devuelven bool y loguean el motivo. No lanza.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "platform/Process.hpp"

namespace ba::launcher {

/// Un proceso hijo lanzado (o adoptado) por el launcher.
class ChildProcess {
   public:
    /// Parametros de lanzamiento.
    struct Config {
        std::filesystem::path exePath; // bin/64bit/ba-app(.exe), etc.
        std::vector<std::string> args; // p. ej. {"--restore"}
        std::filesystem::path workDir; // raiz del bundle
        std::string name;              // nombre logico para logs ("A", "B")
    };

    /// Lanza el hijo con el entorno saneado.
    ///
    /// :returns: true si arranco; false en fallo (logueado).
    bool start(const Config& cfg);

    /// Adopta un proceso vivo publicado por un launcher anterior.
    ///
    /// :returns: true si el PID existe y se pudo abrir.
    bool attach(std::uint32_t pid, const std::string& name);

    /// true si el proceso sigue vivo.
    bool alive();

    /// PID del hijo (0 si no hay).
    std::uint32_t pid() const { return proc_.pid(); }

    /// Codigo de salida si ya termino (nullopt si sigue vivo o no hay hijo).
    /// Un hijo adoptado que termino informa -1 en POSIX (codigo desconocido).
    std::optional<int> exitCode();

    /// Pide cierre amable (SIGTERM en POSIX; en Windows equivale a kill).
    bool terminate() { return proc_.terminate(); }

    /// Mata al hijo y a sus descendientes.
    bool kill() { return proc_.kill(); }

    /// Espera la salida hasta `ms`. :returns: true si termino.
    bool waitExit(int ms) { return proc_.wait(ms).has_value(); }

    /// Suelta el proceso (no lo mata). Idempotente.
    void close() { proc_ = ba::platform::Process(); }

   private:
    ba::platform::Process proc_;
    std::string name_;
};

} // namespace ba::launcher
