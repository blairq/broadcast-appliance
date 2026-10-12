// ============================================================================
// src/gateway/Pinggy.hpp — Supervisión de ba-pinggy (acceso remoto por pinggy.io)
//
// Proposito:
//   Lanza y supervisa `ba-pinggy` (bin/64bit/ba-pinggy[.exe], el envoltorio de
//   libpinggy en un proceso aparte: aísla su OpenSSL del BoringSSL de
//   libwebrtc, y sus caídas). Ver docs/planes/PINGGY.md. Igual que
//   CloudflaredSupervisor:
//     - AUTOSTART al arrancar B y RECUPERACION con backoff si el hijo cae.
//     - killWithParent: si B muere, el túnel muere con él.
//     - TOKEN por la variable de entorno BA_PINGGY_TOKEN del hijo, nunca por
//       línea de comandos. Sin token el túnel anda igual, anónimo (gratis, 60
//       minutos y URL al azar): se avisa.
//     - SALIDA: stdout/stderr agregados a Logs/pinggy.log. ba::platform::Process
//       solo redirige a un archivo, así que el estado se saca LEYENDO ese log
//       desde donde estaba al lanzar (logOffset_): ba-pinggy escribe una línea
//       JSON por evento ({"ev":"listo","urls":[...],"forwardings":[...]}, ...).
//
//   Forwardings (todo hacia 127.0.0.1):
//     - http  -> puertoTunel (8786): la escucha propia del túnel en
//       SignalingServer, donde TODA conexión es externa (ver SignalingServer.hpp).
//     - con el TURN de la caja prendido: tlstcp y tcp -> turnLocal (3478), y
//       udp -> turnLocal si turnUdp.
//   Nunca se reenvía la API de control (8788), el MCP (8790) ni la
//   señalización común (8787, que confía en 127.0.0.1 como red local): un
//   forwarding a un puerto prohibido hace que no se lance nada.
//
// Modelo de threading:
//   Un thread de supervisión (runLoop) que cada 500 ms lee lo nuevo del log,
//   aplica los eventos al estado (protegido por estadoLock_) y relanza el hijo
//   con backoff si murió. start()/stop() desde el thread de control de B; los
//   getters (estado(), urlPublica(), turnPublico()) desde cualquier thread.
//
// Estrategia de errores:
//   No lanza. Sin binario no se lanza (se avisa). Un {"ev":"error",
//   "recuperable":false} (token inválido, por ejemplo) no se reintenta: queda en
//   estado "error" hasta reiniciar el gateway. Lo demás se reintenta.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/Process.hpp"

namespace ba::gateway {

class Log;

class PinggySupervisor {
   public:
    struct Cfg {
        std::string exe;      ///< bin/64bit/ba-pinggy[.exe]
        std::string token;    ///< token de túnel (vacío = anónimo); va por env
        std::string logPath;  ///< Logs/pinggy.log
        std::string region = "auto"; ///< auto|us|eu|ap
        std::string host;     ///< dominio propio o subdominio persistente (Pro); vacío = el que dé pinggy
        std::string listaBlanca; ///< IPs/CIDR separadas por coma; vacío = todas
        uint16_t puertoTunel = 8786; ///< la escucha del túnel en SignalingServer
        // --- TURN de la caja (lo prende main si el servidor TURN arrancó) ---
        bool turn = false;
        uint16_t turnLocal = 3478;
        uint16_t turnTcpPuerto = 0; ///< puerto público TCP fijo (Pro); 0 = el que dé pinggy
        bool turnUdp = false;
        uint16_t turnUdpPuerto = 0; ///< puerto público UDP fijo (Pro); 0 = el que dé pinggy
        /// PROXY protocol hacia lo local (v1|v2|no). v1 deja ver la IP real en
        /// la escucha del túnel (límites y registro). OJO: es global en
        /// ba-pinggy, también le llega a los forwardings TCP del TURN.
        std::string haproxy = "v1";
        /// Puertos locales a los que nunca se reenvía (main suma 8787 y el de control).
        std::set<uint16_t> prohibidos{8788, 8790};
    };

    /// Lo que se sabe del túnel (copia; ver estado()).
    struct Estado {
        std::string estado = "apagado"; ///< apagado|conectando|listo|reconectando|error
        std::vector<std::string> urls;
        std::string urlHttp;     ///< el público del forwarding http (si vino)
        std::string error;
        std::string aviso;       ///< el último {"ev":"aviso"}
        std::string turnPublico; ///< host:puerto del TURN por TCP (tlstcp si hay)
        std::string turnTipo;    ///< tlstcp|tcp
        std::string turnUdp;     ///< host:puerto del TURN por UDP, o vacío
        bool fatal = false;      ///< error no recuperable: no se reintenta
    };

    PinggySupervisor(Cfg cfg, Log& log);
    ~PinggySupervisor();
    PinggySupervisor(const PinggySupervisor&) = delete;
    PinggySupervisor& operator=(const PinggySupervisor&) = delete;

    /// Arranca ba-pinggy y la supervisión. :returns: true si el hijo se lanzó.
    bool start();
    /// Detiene la supervisión y termina el hijo (idempotente).
    void stop();

    bool activo() const { return started_.load(); }
    bool running() const { return alive_.load(); }
    int restartCount() const { return restartCount_.load(); }
    Estado estado() const;

    /// La URL pública para enlaces y QR ("https://<host>"), sin barra final.
    /// Vacía si el túnel no está listo (o reconectando con URL ya conocida).
    std::string urlPublica() const;
    /// host:puerto público del TURN por TCP (tlstcp o tcp), vacío si no hay.
    /// El tipo sale de estado().turnTipo. Para armar los iceServers.
    std::string turnPublico() const;
    /// {"activo","estado","urls","urlPublica","error","turn":{...}} (API de control).
    nlohmann::json estadoJson() const;

    // --- Piezas puras (públicas para los tests) ---------------------------

    /// Los argumentos de ba-pinggy para esta configuración. Si algún
    /// forwarding apunta a un puerto prohibido devuelve {} y explica en `error`.
    static std::vector<std::string> argumentos(const Cfg& cfg, std::string* error = nullptr);
    /// Aplica una línea de la salida de ba-pinggy al estado. Las que no son
    /// JSON (o no traen "ev") se ignoran. :returns: true si cambió algo.
    static bool aplicarLinea(const std::string& linea, Estado& e, uint16_t turnLocal = 3478);
    /// URL pública según el estado y el host configurado (ver urlPublica()).
    static std::string urlPublicaDe(const Estado& e, const std::string& host);
    /// estadoJson() a partir de piezas (para los tests).
    static nlohmann::json estadoJsonDe(bool activo, const Estado& e, const std::string& host);

   private:
    void runLoop();
    bool spawn();
    void kill();
    bool processAlive();
    void leerLog();
    void sleepBackoff();
    void setEstado(const std::string& estado, const std::string& error = {});

    Cfg cfg_;
    Log& log_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> alive_{false};
    std::atomic<bool> started_{false};
    std::atomic<int> restartCount_{0};
    int backoffMs_ = 1000;

    std::thread thread_;
    std::uintmax_t logOffset_ = 0; // solo spawn/leerLog (thread de supervisión o start)
    std::string pendiente_;        // la línea a medio escribir

    mutable std::mutex estadoLock_;
    Estado estado_;

    std::mutex procLock_;
    ba::platform::Process proc_;
};

} // namespace ba::gateway
