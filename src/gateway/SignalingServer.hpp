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
#include <set>
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
    void handleConnection(long long clientFd, std::uint32_t clientIp);

    // --- Rate-limit de PIN POR IP -------------------------------------------
    //
    // El lockout de TokenService es global al proceso: cinco intentos fallidos
    // de cualquiera cerraban /api/join durante 60 s para TODOS los invitados,
    // asi que servia de negacion de servicio barata contra la sala. Este
    // contador es por direccion de origen, de modo que quien falla se bloquea
    // a si mismo y no a los demas.
    struct IpAttempts {
        int failures = 0;
        int lockedUntilUnix = 0;
    };
    static constexpr int kPinMaxAttemptsPerIp = 5;
    static constexpr int kPinLockoutSecPerIp = 60;
    std::map<std::uint32_t, IpAttempts> ipAttempts_;
    std::mutex ipAttemptsLock_;
    /// :returns: true si esa IP todavia puede intentar un PIN.
    bool ipAttemptAllowed(std::uint32_t ip);
    /// Registra un intento fallido de esa IP (y abre su lockout si toca).
    void ipRecordFailure(std::uint32_t ip);
    /// Limpia el estado de esa IP tras un intento exitoso.
    void ipRecordSuccess(std::uint32_t ip);

    // HTTP
    void handleHttp(long long fd, const std::string& method, const std::string& path,
                    const std::string& query, const std::string& body, std::uint32_t clientIp);
    bool serveStatic(long long fd, const std::string& relPath);
    void apiJoin(long long fd, const std::string& body, std::uint32_t clientIp);
    void apiVerify(long long fd, const std::string& query);

    // WebSocket
    bool wsHandshake(long long fd, const std::string& req, const std::string& query);
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
    /// Reserva un slot (1..maxGuests) para el cliente `ip` y lo devuelve.
    ///
    /// El cliente web tenia el slot codificado a 1, asi que el segundo invitado
    /// siempre chocaba con un 409 y la "mesa redonda" de hasta 4 invitados no
    /// podia existir. Ahora el servidor -- que es quien conoce la ocupacion --
    /// lo elige y lo devuelve junto al token en /api/join.
    ///
    /// La reserva existe porque entre el /api/join y el WebSocket pasa un rato
    /// real: el invitado tiene que dar permiso de camara y microfono. Sin
    /// reservar, dos invitados que se preparan a la vez reciben el MISMO slot y
    /// el segundo se lleva un 409.
    ///
    /// Va CLAVEADA POR IP para que recargar la pagina no queme un slot nuevo
    /// cada vez: un segundo join del mismo origen reutiliza su reserva en vez
    /// de consumir otra. Vence junto con el token (el token es justamente lo
    /// que habilita a tomarla), asi que una reserva abandonada se recicla sola.
    ///
    /// :param ip: direccion de origen del cliente.
    /// :returns: el slot reservado, o 0 si la sala esta llena.
    int reserveSlotFor(std::uint32_t ip);

    /// Recicla las reservas vencidas. La llaman reserveSlotFor y tryClaimSlot.
    void expireReservationsLocked(int nowUnix);

    // Utilidades de socket (dual plataforma).
    bool sendAll(long long fd, const void* data, size_t len) const;
    bool sendResponse(long long fd, int code, const std::string& status, const std::string& contentType,
                      const std::string& body) const;
    long long listenFd_ = -1;

    Cfg cfg_;
    TokenAuth& auth_;
    WebrtcHub& hub_;
    Log& log_;
    MediaPump* pump_ = nullptr; // publica el uplink del invitado (B→A)

    std::atomic<bool> stop_{false};
    std::thread acceptThread_;

    // Conexiones en vuelo. Los threads de conexion se DETACHAN y se cuentan
    // aqui en vez de guardarse en un vector: el vector solo crecia (nunca se
    // recolectaba hasta stop()) y un cliente que abriera conexiones en bucle
    // agotaba memoria y threads. stop() espera a que el contador llegue a 0.
    std::atomic<int> activeConns_{0};
    // Tope de conexiones simultaneas. El servidor atiende como mucho unos
    // pocos invitados (maxGuests <= 4) mas sus peticiones HTTP; por encima de
    // esto se rechaza en vez de lanzar threads sin limite.
    static constexpr int kMaxConcurrentConns = 64;
    // Timeout de la fase HTTP/handshake, en ms (ver setSocketTimeout).
    static constexpr int kHandshakeTimeoutMs = 15000;
    // Descriptores de TODAS las conexiones aceptadas que siguen vivas, esten
    // en la fase HTTP o ya promovidas a WebSocket. stop() les hace shutdown()
    // para que su recv retorne al instante: sin esto, una conexion en la fase
    // HTTP solo se desbloqueaba al vencer su timeout, y el drenaje de stop()
    // se rendia dejando threads detachados vivos sobre un objeto ya destruido.
    std::set<long long> activeFds_;
    std::mutex activeFdsLock_;
    void trackFd(long long fd);
    void untrackFd(long long fd);
    // Hace shutdown() de todos los fds vivos para desbloquear sus recv.
    void closeActiveConns();

    // unique_ptr<WsConn>: WsConn tiene un std::mutex (no movible), asi que no
    // puede insertarse por value en el map (invocaria el move-assign borrado).
    std::map<int, std::unique_ptr<WsConn>> conns_;
    std::mutex connsLock_;

    std::vector<bool> slotBusy_;
    // Reserva pendiente por slot: hasta cuando vale (seg Unix, 0 = ninguna) y
    // de que cliente es. Una reserva cuenta como ocupado para los demas, pero
    // su dueño la convierte en ocupacion activa al abrir el WebSocket.
    std::vector<int> slotReservedUntil_;
    std::vector<std::uint32_t> slotReservedIp_;
    std::mutex slotLock_;
};

} // namespace ba::gateway
