// ============================================================================
// src/gateway/Cloudflared.hpp — Supervisión de cloudflared.exe (spec §8.8)
//
// Proposito:
//   Lanza y supervisa el tunel de Cloudflare que expone la senalizacion de B
//   (spec §8.3, §8.8). El tunel lleva SOLO HTTP/WebSocket (la senalizacion), no
//   el trafico UDP de WebRTC (spec §8.4). Responsabilidades:
//     - AUTOSTART: al arrancar B, lanza cloudflared (el tunel nace con B).
//     - RECUPERACION: si cloudflared cae, B lo reinicia con backoff. Las
//       llamadas ya establecidas SIGUEN (el medio no pasa por el tunel); solo
//       fallan las conexiones nuevas hasta que vuelva (spec §3.4).
//     - AISLAMIENTO: Job Object con KILL_ON_JOB_CLOSE: si B muere, el tunel
//       muere con el (spec §8.8).
//     - TOKEN: por la variable de entorno TUNNEL_TOKEN del hijo, NO por linea
//       de comandos (ahi seria visible en el listado de procesos, spec §8.8).
//     - SALIDA: stdout/stderr redirigidos a Logs/cloudflared.log, con
//       CREATE_NO_WINDOW (spec §8.8).
//     - METRICAS: en loopback (127.0.0.1) para no exponer nada (spec §8.8).
//
// Modelo de threading:
//   - Un thread de supervision (runLoop) que, cada 2 s, verifica la salud del
//     proceso (alive + probe del puerto de metricas) y lo reinicia con backoff
//     si cae. Se detiene por un std::atomic<bool>.
//   - start()/stop() se llaman desde el thread de control de B.
//
// Estrategia de errores:
//   No lanza. Si cloudflared.exe no existe o CreateProcessW falla, se loguea y
//   se reintenta con backoff (el gateway sigue corriendo: sin tunel solo no hay
//   conexiones nuevas, pero el medio local sigue). En no-Windows es un no-op
//   (build offline) para que el arbol compile.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace ba::gateway {

class Log;

/// Supervisor de cloudflared (autostart + recovery + Job Object). Ver header.
class CloudflaredSupervisor {
   public:
    /// :param cloudflaredPath: ruta a cloudflared.exe (bin/64bit).
    /// :param tunnelToken: token del tunel (env TUNNEL_TOKEN del hijo).
    /// :param metricsPort: puerto de metricas en loopback.
    /// :param logPath: donde redirigir stdout/stderr.
    /// :param log: logger compartido.
    CloudflaredSupervisor(std::string cloudflaredPath, std::string tunnelToken, uint16_t metricsPort,
                          std::string logPath, Log& log);
    ~CloudflaredSupervisor();

    CloudflaredSupervisor(const CloudflaredSupervisor&) = delete;
    CloudflaredSupervisor& operator=(const CloudflaredSupervisor&) = delete;

    /// Arranca cloudflared (autostart) y el thread de supervision.
    ///
    /// :returns: true si el proceso se lanzo (o ya estaba corriendo); false si
    ///           no se pudo lanzar (el thread seguira reintentando con backoff).
    bool start();

    /// Detiene la supervision y mata cloudflared (cierra el Job Object).
    void stop();

    /// Indica si cloudflared esta corriendo ahora.
    bool running() const;

    /// Cantidad de reinicios acumulados (diagnostico).
    int restartCount() const;

   private:
    void runLoop();
    bool spawn();
    void kill();
    bool processAlive() const;
    bool probeMetrics() const;
    void sleepBackoff();

    std::string cloudflaredPath_;
    std::string tunnelToken_;
    uint16_t metricsPort_;
    std::string logPath_;
    Log& log_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> alive_{false};
    std::atomic<int> restartCount_{0};
    int backoffMs_ = 1000;

    std::thread thread_;

#ifdef _WIN32
    void* job_ = nullptr;  // HANDLE del Job Object (KILL_ON_JOB_CLOSE)
    void* proc_ = nullptr; // HANDLE del proceso hijo
#endif
};

} // namespace ba::gateway
