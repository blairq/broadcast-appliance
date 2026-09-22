// ============================================================================
// src/gateway/Log.hpp — Logger estructurado del Proceso B (componente "gateway")
//
// Proposito:
//   Es una CARA del registro comun (core/diag/Registro.hpp) con el componente
//   "gateway" ya puesto: existe para que el gateway no tenga que nombrar su
//   propio componente en cada linea, y para no tocar los cientos de llamadas
//   que ya hay. El formato, los niveles, la rotacion y el anillo en memoria
//   son los del registro comun, en el mismo formato que el
//   resto del arbol (spec §3.3: "sin dialogos de sistema; todo el log va a
//   stderr con el componente"). Formato: `nivel | gateway | codigo | contexto`.
//   El gateway es headless (sin Qt), asi que stderr es el canal de log y el
//   launcher lo redirige a Logs/gateway.log (spec §5.1 layout).
//
// Modelo de threading:
//   Un solo Logger se comparte entre threads (control, señalización, pump de
//   medios, supervisor de cloudflared). La escritura se protege con un mutex
//   para que las lineas no se intercalen. No hay excepciones: `line` no lanza.
//
// Estrategia de errores:
//   No lanza. `fflush` falla silenciosamente (no hay nada que hacer con el
//   fallo de un log en un proceso headless).
// ============================================================================
#pragma once

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

#include "diag/Registro.hpp"

namespace ba::gateway {

/// Componente del logger en el log estructurado (spec §3.3).
inline constexpr const char* kLogComponent = "gateway";

/// Logger estructurado thread-safe. Ver el header para el formato.
///
/// Escribe siempre a stderr (util en desarrollo y cuando el launcher captura la
/// salida del hijo) y, si se llama a `openFile()`, TAMBIEN a un log rotativo en
/// disco via `ba::diag::LogRotator`. Sin ese cableado el gateway no dejaba
/// ningun rastro en `Logs/`: ante un incidente en campo no habia nada que leer.
class Log {
   public:
    /// Abre el log rotativo en `dir` (normalmente `<root>/Logs`).
    ///
    /// Idempotente y no-fallible: si el directorio no se puede crear, el
    /// rotador queda inactivo y el logger sigue funcionando solo contra stderr.
    ///
    /// :param dir: directorio de logs.
    /// :param maxBytes: tamaño del archivo activo antes de rotar.
    /// :param keepFiles: cuantos archivos rotados se conservan.
    void openFile(const std::string& dir, std::size_t maxBytes = 4u * 1024u * 1024u,
                  std::size_t keepFiles = 5) {
        auto& reg = ba::diag::registro();
        reg.setComponentePorDefecto(kLogComponent);
        // Si el launcher ya esta capturando stderr a Logs/gateway.log, ESA es
        // la captura y no se abre otro archivo: si no, cada linea quedaria dos
        // veces y habria dos escritores rotando el mismo archivo.
        if (!reg.abrirSegunEntorno(dir, "gateway.log")) return;
        (void)maxBytes;
        (void)keepFiles;
    }

    /// Registra una linea de info (codigo 0).
    void info(const std::string& ctx) const { line("info", 0, ctx); }

    /// Registra una advertencia con un codigo de error estable.
    void warn(const std::string& ctx, int code) const { line("warn", code, ctx); }

    /// Registra un error con un codigo de error estable.
    void error(const std::string& ctx, int code) const { line("error", code, ctx); }

    /// Registra una linea con formato printf. `level` es "info"/"warn"/"error".
    ///
    /// :param level: nivel de log.
    /// :param code: codigo de error (0 en info).
    /// :param fmt: cadena de formato printf.
    template <typename... Args> void fmt(const char* level, int code, const char* fmt, Args&&... args) const {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
        line(level, code, buf);
    }

   private:

    void line(const char* level, int code, const std::string& ctx) const {
        ba::diag::Nivel n = ba::diag::Nivel::Info;
        ba::diag::nivelDeNombre(level, n);
        ba::diag::registro().linea(n, kLogComponent, code, ctx);
    }
};

} // namespace ba::gateway
