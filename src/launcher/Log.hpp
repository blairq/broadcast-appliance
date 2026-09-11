// Log.hpp
//
// Proposito:
//   Logger estructurado minimo del launcher (spec regla 8: "todo camino de
//   error deja un log estructurado: componente, codigo, contexto"). El
//   launcher no lleva Qt ni DLLs propias, asi que es un writer de archivo con
//   mutex, sin dependencias. Tambien espeja a stderr para el desarrollo.
//
// Modelo de threading:
//   Thread-safe: una unica std::mutex serializa las escrituras. Lo invocan el
//   thread principal y el thread del supervisor (Supervisor::loop). La mutex
//   es de bajo costo: las escrituras son raras (eventos, no camino de datos).
//
// Estrategia de errores:
//   Nunca lanza. Si el archivo no se pudo abrir, init() deja file_ nulo y
//   write() solo va a stderr. write() sobre un estado no inicializado es
//   inofensivo. El destructor del proceso cierra el archivo.

#pragma once

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>

namespace ba::launcher {

/// Logger de archivo con formato estructurado `nivel | componente | codigo |
/// contexto`. Un solo archivo por ejecucion (Logs/launcher.log).
class Log {
   public:
    /// Nivel de severidad del mensaje.
    enum class Level { Info = 0, Warn = 1, Error = 2 };

    /// Abre (o crea) el archivo de log.
    ///
    /// :param path: ruta UTF-8 del archivo de log. Si no se puede abrir, el
    ///              logger queda en modo "solo stderr" (no lanza).
    static void init(const std::string& path);

    /// Cierra el archivo de log. Idempotente.
    static void close();

    /// Escribe un mensaje estructurado.
    ///
    /// :param level: severidad.
    /// :param component: componente emisor (p. ej. "supervisor", "integrity").
    /// :param code: codigo de error estable (0 en info).
    /// :param context: contexto libre (rutas, PIDs, etc.).
    static void write(Level level, const char* component, int code, const std::string& context);
};

/// Macros de conveniencia para no repetir el namespace en cada llamada.
#define BA_LOG_INFO(comp, code, ctx)                                                                         \
    ::ba::launcher::Log::write(::ba::launcher::Log::Level::Info, comp, code, ctx)
#define BA_LOG_WARN(comp, code, ctx)                                                                         \
    ::ba::launcher::Log::write(::ba::launcher::Log::Level::Warn, comp, code, ctx)
#define BA_LOG_ERROR(comp, code, ctx)                                                                        \
    ::ba::launcher::Log::write(::ba::launcher::Log::Level::Error, comp, code, ctx)

} // namespace ba::launcher
