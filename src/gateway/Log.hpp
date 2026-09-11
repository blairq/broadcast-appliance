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
#include <mutex>
#include <string>

namespace ba::gateway {

/// Componente del logger en el log estructurado (spec §3.3).
inline constexpr const char* kLogComponent = "gateway";

/// Logger estructurado thread-safe (stderr). Ver el header para el formato.
class Log {
   public:
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

    void line(const char* level, int code, const std::string& ctx) const {
        std::lock_guard<std::mutex> lock(m_);
        std::fprintf(stderr, "%s | %s | %d | %s\n", level, kLogComponent, code, ctx.c_str());
        std::fflush(stderr);
    }
};

} // namespace ba::gateway
