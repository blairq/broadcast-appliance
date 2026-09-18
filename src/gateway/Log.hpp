// ============================================================================
// src/gateway/Log.hpp — Logger estructurado del Proceso B (componente "gateway")
//
// Proposito:
//   Logger minimo y thread-safe para el gateway, en el mismo formato que el
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

#include "diag/LogRotator.hpp"

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
    void openFile(const std::string& dir, std::size_t maxBytes = 2u * 1024u * 1024u,
                  std::size_t keepFiles = 5) {
        std::lock_guard<std::mutex> lock(m_);
        rotator_ = std::make_unique<ba::diag::LogRotator>(dir, maxBytes, keepFiles);
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
    mutable std::mutex m_;
    // Rotador opcional: nulo hasta que alguien llame a openFile(). LogRotator
    // tiene su propio lock, pero se escribe bajo `m_` igual para que la linea
    // salga en el mismo orden en stderr y en el archivo.
    std::unique_ptr<ba::diag::LogRotator> rotator_;

    void line(const char* level, int code, const std::string& ctx) const {
        char buf[1200];
        std::snprintf(buf, sizeof(buf), "%s | %s | %d | %s", level, kLogComponent, code, ctx.c_str());
        std::lock_guard<std::mutex> lock(m_);
        std::fprintf(stderr, "%s\n", buf);
        std::fflush(stderr);
        if (rotator_) {
            rotator_->write(buf);
        }
    }
};

} // namespace ba::gateway
