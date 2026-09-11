// ============================================================================
// src/gateway/SignalingServer.hpp — Servidor de señalización HTTP + WebSocket
//
// Proposito:
//   Servidor embebido (sin Qt, sin Boost) que expone la señalización de B y la
//   app web del invitado (spec §8.3). Escucha SOLO en 127.0.0.1 (spec §5.6:
//   "Ningun proceso escucha en interfaces que no sean loopback"; cloudflared se
//   conecta ahi y expone el tunel). Dos superficies:
//
//   HTTP (estático + API):
//     - GET  /            → Web/guest/index.html (app del invitado).
//     - GET  /app.js, /style.css, /<file> → archivos estáticos de webroot.
//     - GET  /healthz     → 200 "ok" (probe de salud).
//     - POST /api/join    → {room, pin} → emite token (TokenAuth::issue); 401 si
//                               el PIN es invalido o hay lockout.
//     - GET  /api/verify  → ?token=..&room=.. → verifica (TokenAuth::verify).
//
//   WebSocket (señalización WebRTC, spec §8.1/§8.3):
//     - GET /ws?token=..&slot=.. (Upgrade: websocket) → handshake 101 y luego
//       el flujo de señalización por invitado. El cliente (navegador) es el
//       OFFERER: envía {type:"offer", sdp}; B responde {type:"answer", sdp}.
//       Los candidatos ICE fluyen en ambos sentidos ({type:"ice", candidate}).
//       {type:"bye"} cierra.
//
//   FRAMING: los mensajes de señalización viajan como frames binarios WS que
//   cargan el encoding canonico del proyecto (ba_core::ipc::Frame:
//   encodeFrame/decodeFrame, prefijo LE de 4 bytes + JSON, spec §4.1). Asi la
//   señalización reutiliza el mismo codec de framing que el canal de control.
//
// Modelo de threading:
//   - Un thread de ACCEPT (acceptLoop) que, por cada conexion, lanza un thread
//     de conexion (handleConnection). Loopback + ≤4 invitados: un thread por
//     conexion es suficiente y simple.
//   - La tabla de slots (slot → ocupado) se protege con un mutex: varias
//     conexiones pueden abrir WS a la vez y competir por un slot.
//   - Los callbacks de sesion (onLocalAnswer/onLocalIce) pueden llamarse desde
//     el thread de libwebrtc (backend real); el send por conexion toma un
//     mutex por conexion para serializar las escrituras al socket.
//   - stop() cierra el socket de escucha y senala parada; los threads de
//     conexion se detienen al ver EOF/cierre.
//
// Estrategia de errores:
//   No lanza. Un fallo de parseo/IO cierra esa conexion (logueado) y no tumba
//   al servidor. Un token invalido o un slot ocupado cierra el WS con un
//   mensaje de error antes del handshake (o un close frame).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "TokenAuth.hpp"
#include "Webrtc.hpp"

namespace ba::gateway {

class Log;
// Forward decl: `pump_` es solo un puntero en el header; el tipo completo se
// incluye en SignalingServer.cpp (donde se invoca forwardAudio/forwardVideo).
class MediaPump;

/// Servidor de señalización HTTP + WebSocket en loopback. Ver el header.
class SignalingServer {
public:
    /// Configuracion del servidor.
    struct Cfg {
        uint16_t port = 8787;
        std::string webroot; // Web/guest
        std::string room;    // sala por defecto
        int maxGuests = 4;
    };

    /// :param cfg: puerto, webroot, sala, max invitados.
    /// :param auth: auth de invitado (TokenService).
    /// :param hub: hub de sesiones WebRTC.
    /// :param log: logger compartido.
    SignalingServer(const Cfg& cfg, TokenAuth& auth, WebrtcHub& hub, Log& log);
    ~SignalingServer();

    /// Registra el motor de medios para publicar el uplink del invitado (B→A):
    /// los callbacks onRemoteAudio/onRemoteVideo de cada sesion reenvian al
    /// pump (forwardAudio/forwardVideo). Llamar antes de start().
    ///
    /// :param pump: motor de medios (no se toma la propiedad).
    void setPump(MediaPump* pump) { pump_ = pump; }

    SignalingServer(const SignalingServer&) = delete;
    SignalingServer& operator=(const SignalingServer&) = delete;

    /// Bind + listen en 127.0.0.1:port y arranca el thread de accept.
    ///
    /// :returns: true si quedo escuchando; false si el bind fallo (puerto en uso).
    bool start();

    /// Cierra el socket de escucha y detiene los threads.
    void stop();

    /// Indica si esta escuchando.
    bool running() const;

private:
    void acceptLoop();
    void handleConnection(long long clientFd);

    // HTTP
    void handleHttp(long long fd, const std::string& method,
                    const std::string& path, const std::string& query,
                    const std::string& body);
    bool serveStatic(long long fd, const std::string& relPath);
    void apiJoin(long long fd, const std::string& body);
    void apiVerify(long long fd, const std::string& query);

    // WebSocket
    bool wsHandshake(long long fd, const std::string& req,
                     const std::string& query);
    void wsLoop(long long fd, int slot);

    // Conexiones WS activas (slot → conexion) para el send por callback.
    // WsConn contiene un std::mutex (no movible / no asignable), por lo que se
    // guarda detras de un unique_ptr y se construye in-place: evitar el
    // move-assign que la std::map::operator[] invoca al insertar.
    struct WsConn {
        long long fd = -1;
        std::mutex sendLock;
    };
    void sendSignaling(int slot, const std::string& json);
    WsConn* connForSlot(int slot);
    void registerConn(int slot, long long fd);
    void unregisterConn(int slot);

    // Tabla de slots ocupados.
    bool tryClaimSlot(int slot);
    void releaseSlot(int slot);

    // Utilidades de socket (dual plataforma).
    bool sendAll(long long fd, const void* data, size_t len) const;
    bool sendResponse(long long fd, int code, const std::string& status,
                      const std::string& contentType,
                      const std::string& body) const;
    long long listenFd_ = -1;

    Cfg cfg_;
    TokenAuth& auth_;
    WebrtcHub& hub_;
    Log& log_;
    MediaPump* pump_ = nullptr; // publica el uplink del invitado (B→A)

    std::atomic<bool> stop_{false};
    std::thread acceptThread_;
    std::vector<std::thread> connThreads_;
    mutable std::mutex connThreadsLock_;

    // unique_ptr<WsConn>: WsConn tiene un std::mutex (no movible), asi que no
    // puede insertarse por value en el map (invocaria el move-assign borrado).
    std::map<int, std::unique_ptr<WsConn>> conns_;
    std::mutex connsLock_;

    std::vector<bool> slotBusy_;
    std::mutex slotLock_;
};

} // namespace ba::gateway
