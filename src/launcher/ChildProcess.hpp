// ChildProcess.hpp
//
// Proposito:
//   Lanzamiento y seguimiento de un proceso hijo del launcher (spec 3.1.6):
//   subsistema GUI, sin consola, con entorno saneado (sin QT_*/PYTHON*) y
//   PATH minimo. Intenta CREATE_BREAKAWAY_FROM_JOB y reintenta sin ese flag si
//   falla (spec 3.1.6). Publica el PID para el readopt (3.1.9).
//
// Modelo de threading:
//   start() se invoca desde el thread principal (o el del supervisor) ANTES de
//   entrar al loop de vigilancia. alive()/pid() son lecturas sobre un HANDLE
//   de proceso (thread-safe por naturaleza: WaitForSingleObject/GetExitCode).
//   close() libera el handle; no llamar desde dos threads a la vez.
//
// Estrategia de errores:
//   start() devuelve bool y loguea el fallo (codigo GetLastError). No lanza.
//   Un hijo que no pudo lanzarse deja pid()==0.

#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::launcher {

/// Un proceso hijo lanzado por el launcher.
class ChildProcess {
   public:
    /// Parametros de lanzamiento.
    struct Config {
        std::wstring exePath; // ruta al .exe (bin/64bit/ba-app.exe, etc.).
        std::wstring args;    // argumentos de linea (p. ej. L"--restore").
        std::wstring workDir; // directorio de trabajo (raiz del disco).
        std::wstring name;    // nombre logico para logs ("A", "B").
    };

    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = default;
    ChildProcess& operator=(ChildProcess&&) = default;

    /// Lanza el hijo con entorno saneado.
    ///
    /// :param cfg: configuracion de lanzamiento.
    /// :returns: true si el proceso arranco; false en fallo (logueado).
    bool start(const Config& cfg);

    /// Indica si el proceso sigue vivo.
    bool alive() const;

    /// PID del hijo (0 si no arranco).
    std::uint32_t pid() const;

    /// Handle del proceso (para WaitForMultipleObjects / MiniDumpWriteDump).
    /// En no-Windows devuelve 0.
    void* handle() const;

    /// Adopta un proceso ya vivo (readopt, spec 3.1.9): abre un handle sobre
    /// un PID publicado en memoria compartida por una ejecucion anterior.
    ///
    /// :param pid: PID del proceso a adoptar.
    /// :returns: true si se pudo abrir el handle; false si el proceso no existe.
    bool attach(std::uint32_t pid);

    /// Codigo de salida del proceso (STILL_ACTIVE si sigue vivo).
    ///
    /// :returns: codigo de salida; 0 si no hay handle. Un 0 real (cierre
    ///           ordenado) se distingue de "sin handle" por alive().
    std::uint32_t exitCode() const;

    /// Libera el handle. Idempotente.
    void close();

   private:
    void* hProcess_ = nullptr; // HANDLE en Win32; nullptr en no-Win.
    std::uint32_t pid_ = 0;
    std::wstring name_;
};

} // namespace ba::launcher
