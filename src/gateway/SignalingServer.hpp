// ============================================================================
// src/gateway/SignalingServer.hpp — Servidor HTTP + WebSocket del gateway (B)
//
// Proposito:
//   Servidor embebido (sin Qt, sin Boost) con DOS superficies, cada una en su
//   propio socket de escucha:
//
//   1) INVITADOS — `bindAddress:port` (127.0.0.1 por defecto; cloudflared la
//      publica por el tunel):
//        GET  /, /app.js, ...        app web del invitado (Web/guest)
//        GET  /healthz               200 "ok"
//        POST /api/join              {room, pin} -> token de un solo uso
//        GET  /api/verify            ?token=&room= -> verificacion sin efecto
//        GET  /ws?token=&room=&name= WebSocket de sala (entra al lobby)
//        GET  /ws?resume=&key=       WebSocket de sala (reconexion)
//        POST /api/v1/files?name=    subida de un archivo para mostrar
//                                    (cabecera X-BA-Key; solo admitidos)
//        POST /api/v1/avatar         foto chica (JPEG <= 64 KB) para las
//                                    tarjetas de oradores (X-BA-Key)
//        GET  /board/...             tablero de oradores (solo lectura)
//        GET  /api/v1/board          estado del tablero (cabecera X-BA-Board)
//        GET  /api/v1/board/avatar/<id>?k=  foto de una tarjeta del tablero
//        POST /api/v1/board/view     {participant, sdp} -> video en vivo de
//                                    un orador (relay, ver Webrtc.hpp)
//
//   2) CONTROL DEL OPERADOR — SIEMPRE 127.0.0.1:adminPort (nunca por el
//      tunel). Es la API de la que cuelgan la vista web (Web/admin) y la vista
//      Qt de la app. Ver AdminApi.cpp para las rutas.
//
//   El estado de la conferencia (lobby, participantes, chat, archivos) vive en
//   `Room`; este servidor solo traduce entre la red y ese modelo.
//
// Protocolo del WebSocket de sala (frames binarios con el encoding Frame del
// proyecto: 4 bytes LE + JSON, spec §4.1):
//   servidor -> invitado:
//     {type:"hello", id, key, name, room, lobby}   al entrar / reengancharse
//     {type:"lobby", position}                     sigue esperando
//     {type:"admitted", slot, backstage}           puede publicar medios
//     {type:"speaker", state, position?, etaSec?, allottedSec, startedMs,
//      usedMs, serverNowMs}                        modo oradores: turno y reloj
//     {type:"profile", name, info, contact}        el operador edito sus datos
//     {type:"rejected"} / {type:"kicked"}          y se corta la conexion
//     {type:"request", what}                       pedido del operador
//     {type:"chat", msg} / {type:"shared", ...}
//     {type:"answer", sdp} / {type:"ice", candidate}
//   invitado -> servidor:
//     {type:"offer", sdp} / {type:"ice", candidate}   (solo admitido)
//     {type:"media", mic, cam, screen, hand}
//     {type:"chat", text}
//     {type:"profile", info, contact}
//     {type:"bye"}
//
// Modelo de threading:
//   Un thread de accept por socket de escucha y un thread (detachado) por
//   conexion. Las escrituras a cada socket se serializan con un mutex por
//   conexion: la sala, los callbacks de libwebrtc y el propio lazo pueden
//   escribir en la misma conexion desde threads distintos.
//
// Estrategia de errores:
//   No lanza. Un fallo de parseo/IO cierra esa conexion (logueado) y no tumba
//   al servidor.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Botonera.hpp"
#include "Sesion.hpp"
#include "Carruseles.hpp"
#include "MediaRoute.hpp"
#include "Preguntas.hpp"
#include "Prompter.hpp"
#include "Rotulador.hpp"
#include "Superficies.hpp"
#include "TokenAuth.hpp"
#include "Votacion.hpp"
#include "Webrtc.hpp"

namespace ba::gateway {

class Log;
class MediaPump;
namespace proxy {
class GestorProxy;
}
class Room;
class ShareStore;

class SignalingServer {
   public:
    struct Cfg {
        uint16_t port = 8787;
        std::string bindAddress = "127.0.0.1"; // "0.0.0.0" = red local (eleccion explicita)
        std::string webroot;                   // Web/guest
        std::string room;                      // sala por defecto
        int maxGuests = 4;
        // --- API de control (loopback siempre) ---
        uint16_t adminPort = 8788; // 0 = puerto efimero (tests)
        std::string adminWebroot;  // Web/admin (consola web)
        std::string adminToken;    // Bearer / cookie de la consola; vacio = sin API de control
        int maxSharesPerGuest = 20;
        std::string boardWebroot;    // Web/board (tablero de oradores)
        std::string prompterWebroot; // Web/prompter (teleprompter del orador)
        /// Clave del control del teleprompter: sin ella cualquiera en la LAN
        /// podria frenarle el texto al orador en medio del discurso. Viaja en
        /// el enlace que el operador le pasa (o en el QR). Vacia = sin control
        /// remoto (la tablet igual muestra el texto).
        std::string prompterKey;
        std::string rotulosWebroot;  // Web/rotulos (los rotulos desde el celular)
        std::string pantallaWebroot; // Web/pantalla (lo que se manda a un televisor)
        std::string castWebroot;     // Web/cast (el programa en vivo, para un televisor)
        std::string comunWebroot;    // Web/comun (el canal de control, lo usan TODAS)
        /// Clave de los rotulos. A diferencia del teleprompter, hace falta
        /// TAMBIEN PARA MIRAR: un rotulo suele llevar el nombre del invitado
        /// que todavia no se anuncio. Vacia = la pagina no atiende a nadie.
        std::string rotulosKey;
        std::string publicoWebroot; // Web/publico (la pagina del publico)
        /// Clave de la pagina del publico. Viaja en el QR que se reparte (o se
        /// proyecta): no es un secreto fuerte y no pretende serlo —lo tiene
        /// todo el que esta en la sala—, es lo que evita que la cola de
        /// preguntas la llene cualquiera que pase por el tunel.
        std::string publicoKey;
        std::string botoneraWebroot; // Web/botonera (la superficie de control de la tablet)
        std::string sesionWebroot;   // Web/sesion (la sesión remota de los legisladores)
    };

    SignalingServer(const Cfg& cfg, TokenAuth& auth, WebrtcHub& hub, Room& room, ShareStore& shares,
                    Log& log);
    ~SignalingServer();

    SignalingServer(const SignalingServer&) = delete;
    SignalingServer& operator=(const SignalingServer&) = delete;

    /// Motor de medios para publicar el uplink del invitado (B->A). Antes de start().
    void setPump(MediaPump* pump) { pump_ = pump; }
    /// El proxy por nombre (T61), si está prendido: GET /api/v1/proxy, POST /api/v1/proxy/renovar.
    void setProxy(proxy::GestorProxy* p) { proxy_ = p; }

    /// Abre los sockets de escucha y arranca los threads de accept.
    /// :returns: true si la superficie de invitados quedo escuchando (la de
    ///           control es opcional: si falla se loguea y se sigue).
    bool start();
    void stop();
    bool running() const;
    /// Puerto real de la superficie de invitados (util si se pidio el 0).
    uint16_t guestPort() const { return guestBoundPort_; }
    /// Puerto real de la API de control (0 si no esta abierta).
    uint16_t adminPort() const { return adminBoundPort_; }

    /// IP publica del tunel WireGuard: los candidatos ICE de loopback (los que
    /// atiende el puente del tunel) se anuncian con esta IP. Vacio = no.
    void setPublicAddress(const std::string& ip) { publicAddress_ = ip; }
    /// El teleprompter, para que la API de control (A) le cargue discursos y
    /// lo comande sin pasar por la clave del control remoto.
    Prompter& prompter() { return prompter_; }
    /// El buzon de rotulos, que A publica y viene a vaciar.
    Rotulador& rotulador() { return rotulador_; }
    Preguntas& preguntas() { return preguntas_; }
    Votacion& votacion() { return votacion_; }

    /// Lo que la pagina del publico muestra arriba y de fondo. Lo publica A,
    /// que es el unico que sabe que evento esta abierto. B solo lo repite.
    void setPublicoInfo(const nlohmann::json& info);
    nlohmann::json publicoInfo() const;
    /// Los codigos que escanearon su invitacion y todavia no fueron a parar al
    /// padron. Es un BUZON, como el de los rotulos: B no tiene el padron —lo
    /// tiene A— asi que anota quien llego y A lo viene a buscar. Encolar es lo
    /// que hace que acreditarse funcione aunque A este ocupado o reiniciando;
    /// una llamada directa a un proceso que no esta se pierde, y el que se
    /// acredito ya guardo el telefono.
    std::vector<std::string> tomarAcreditaciones();

    /// La imagen de fondo, en memoria. Se guarda ACA y no se manda a cada
    /// telefono dentro del JSON: son doscientos telefonos pidiendo cada tres
    /// segundos, y una imagen repetida en cada respuesta es el mismo fondo
    /// mandado mil veces. Asi va una vez y el navegador la cachea.
    void setPublicoFondo(std::string bytes, std::string tipo);
    const std::string& rotulosKey() const { return cfg_.rotulosKey; }
    const std::string& prompterKey() const { return cfg_.prompterKey; }

    /// true si el offer es de SOLO RECEPCION (ninguna pista sube). Lo que
    /// decide es el SDP, no la buena fe del cliente: el token de ingreso de
    /// una pantalla lo puede tener cualquiera. Publica porque es LA garantia
    /// de que un televisor no sea un microfono abierto, y como tal tiene su
    /// propio test (test_pantalla).
    static bool soloRecibe(const std::string& sdp);

    /// Estado de red para la API de control (/api/v1/network).
    void setNetworkStatus(std::function<nlohmann::json()> fn) { networkStatus_ = std::move(fn); }
    /// URL de ingreso para el QR: target "local" (red local) o "remote"
    /// (dominio de Cloudflare). Devuelve {url} o {error}.
    void setJoinUrlProvider(std::function<nlohmann::json(const std::string& target)> fn) {
        joinUrl_ = std::move(fn);
    }
    /// Genera la configuracion del servidor WireGuard (/api/v1/network/server-config).
    void setServerConfigGenerator(std::function<nlohmann::json(const nlohmann::json&)> fn) {
        serverConfig_ = std::move(fn);
    }

    /// Cambia el PIN de la sala (API de control). Vacio = generar uno.
    /// :returns: el PIN vigente, o vacio si no se pudo generar.
    std::string changePin(const std::string& pin);
    /// Clave del tablero de oradores (aleatoria por arranque).
    const std::string& boardToken() const { return boardToken_; }

   private:
    // --- Conexiones ---------------------------------------------------------
    void acceptLoop(long long listenFd, bool admin);
    void handleConnection(long long fd, std::uint32_t clientIp, bool admin);
    void trackFd(long long fd);
    void untrackFd(long long fd);
    void closeActiveConns();

    // --- Superficie de invitados --------------------------------------------
    // --- Pantallas (solo miran) ----------------------------------------------
    // Una PANTALLA es un televisor que muestra el programa y nada mas. No es
    // un participante de la sala: no tiene nombre, no aparece en la lista, no
    // pasa por el lobby, no puede escribir en el chat y no puede publicar
    // audio ni video. Eso ULTIMO no depende de que su pagina se porte bien:
    // el servidor le rechaza el offer si no es de solo recepcion, y su ruta de
    // medios (backstage sin preview) manda lo que subiera a ningun lado.
    //
    // Entra por /ws?pantalla=1&token=..., con el mismo token de ingreso que un
    // invitado: lo que cambia es TODO lo que puede hacer despues.
    /// true si ese id es una pantalla (y no un participante de la sala).
    bool esPantalla(const std::string& pid) const;
    /// Registra una pantalla nueva y devuelve su id. `claveOut` recibe la
    /// clave de reenganche, que se le manda en el "hello".
    std::string altaPantalla(std::string& claveOut);
    /// La clave de reenganche de esa pantalla (vacia si no existe).
    std::string claveDePantalla(const std::string& pid) const;
    /// Valida un reenganche. true si ese id existe y la clave coincide.
    bool reengancharPantalla(const std::string& pid, const std::string& clave);
    void bajaPantalla(const std::string& pid);

    void handleGuestHttp(long long fd, const std::string& method, const std::string& path,
                         const std::string& query, const std::string& body, std::uint32_t clientIp);
    bool serveStatic(long long fd, const std::string& root, const std::string& relPath);
    void apiJoin(long long fd, const std::string& body, std::uint32_t clientIp);
    void apiVerify(long long fd, const std::string& query);
    void apiUpload(long long fd, const std::string& headers, const std::string& query);
    void apiAvatarUpload(long long fd, const std::string& headers, const std::string& body);
    void apiBoard(long long fd, const std::string& method, const std::string& path, const std::string& query,
                  const std::string& headers, const std::string& body);
    /// Espectador del video de un participante (relay de B, sin re-codificar).
    /// `board`: pedido del tablero (solo oradores de la fila, no el lobby).
    void apiView(long long fd, const std::string& body, bool board);
    /// POST .../view/<id>/keyframe y DELETE .../view/<id>.
    void apiViewAction(long long fd, const std::string& method, const std::string& rest, bool board);
    /// Candidatos del answer de un espectador: loopback -> IP publica del tunel.
    std::string publicSdp(const std::string& sdp) const;
    std::map<int, bool> viewerOwners_; // id -> es del tablero
    std::mutex viewersLock_;
    /// Foto de un participante (vacio si no hay). Borra las de quien ya no esta.
    std::string avatarOf(const std::string& id);
    void sendAvatar(long long fd, const std::string& id);

    /// Handshake + alta del participante. :returns: su id, o vacio si fallo.
    std::string wsOpen(long long fd, const std::string& headers, const std::string& query,
                       std::uint32_t clientIp);
    void wsLoop(long long fd, const std::string& pid, uint64_t gen);
    void handleWsMessage(const std::string& pid, uint64_t gen, const nlohmann::json& msg);
    void handleOffer(const std::string& pid, uint64_t gen, const std::string& sdp);

    struct WsConn {
        long long fd = -1;
        uint64_t gen = 0; // distingue una reconexion del mismo participante
        std::mutex sendLock;
    };
    uint64_t registerConn(const std::string& pid, long long fd);
    /// Quita la conexion si sigue siendo la vigente. :returns: true si lo era.
    bool unregisterConn(const std::string& pid, uint64_t gen);
    std::shared_ptr<WsConn> connFor(const std::string& pid);
    void sendToGuest(const std::string& pid, const nlohmann::json& msg);
    void dropGuest(const std::string& pid);

    // Pool de sesiones WebRTC: cada participante que publica tiene UNA
    // sesion (idx 1..hub.sessions()) toda su estadia; la sala decide a donde
    // van sus medios (MediaRoute) y el MediaPump los rutea. El dueño guarda
    // tambien la conexion: una conexion vieja que termina tarde no debe
    // cerrar la sesion de la nueva.
    struct SessionOwner {
        std::string pid;
        uint64_t gen = 0;
    };
    std::map<int, SessionOwner> sessionOwner_; // idx -> dueño
    std::map<std::string, int> sessionOf_;     // pid -> idx
    std::mutex sessLock_;
    std::thread statsThread_; ///< cada 2 s: estadisticas de cada conexion -> Room
    /// Cierra la sesion de `pid` (si `anyGen` o si sigue siendo de `gen`).
    void releaseSession(const std::string& pid, uint64_t gen, bool anyGen);
    /// La sala cambio la ruta de `pid`: se aplica a su sesion o, si todavia no
    /// publica, se le avisa que puede hacerlo.
    void applyRoute(const std::string& pid, const MediaRoute& route);

    // Rate-limit de PIN por IP (el lockout de TokenService es global).
    struct IpAttempts {
        int failures = 0;
        int lockedUntilUnix = 0;
    };
    static constexpr int kPinMaxAttemptsPerIp = 5;
    static constexpr int kPinLockoutSecPerIp = 60;
    std::map<std::uint32_t, IpAttempts> ipAttempts_;
    std::mutex ipAttemptsLock_;
    bool ipAttemptAllowed(std::uint32_t ip);
    void ipRecordFailure(std::uint32_t ip);
    void ipRecordSuccess(std::uint32_t ip);

    // --- API de control (AdminApi.cpp) --------------------------------------
    void handleAdmin(long long fd, const std::string& method, const std::string& path,
                     const std::string& query, const std::string& headers, const std::string& body);
    bool adminAuthorized(const std::string& headers, const std::string& method) const;
    void adminEvents(long long fd);
    void adminShareContent(long long fd, const std::string& id);
    void broadcastToHosts(const std::string& event, const nlohmann::json& data);
    std::string issueLoginCode();
    bool redeemLoginCode(const std::string& code);

    struct SseConn {
        long long fd = -1;
        std::mutex sendLock;
    };
    std::vector<std::shared_ptr<SseConn>> sse_;
    std::mutex sseLock_;
    std::map<std::string, long long> loginCodes_; // code -> vence (ms)
    std::mutex loginLock_;

    Cfg cfg_;
    TokenAuth& auth_;
    WebrtcHub& hub_;
    Room& room_;
    Prompter prompter_;
    /// Lo que rota en cada pantalla (lo publica A, lo lee /pantalla/carrusel/).
    Carruseles carruseles_;
    /// EL TALLY: que escena esta al aire, cual armada y que fuentes tiene cada
    /// una. Lo publica A (es el unico que sabe de escenas) y lo leen las
    /// pantallas de tally de la sala.
    ///
    /// Es un JSON tal cual, sin estructura propia, a proposito: lo que hay
    /// adentro lo decide quien lo publica, y el gateway solo lo guarda y lo
    /// sirve. Un tally que se atrasa porque hubo que tocar dos lados para
    /// agregarle un campo no sirve.
    mutable std::mutex tallyLock_;
    nlohmann::json tally_ = nlohmann::json::object();
    /// Quien esta VIVO del otro lado: cada superficie web late (ver Superficies).
    Superficies superficies_;
    /// Las conexiones que son PANTALLAS (ver esPantalla), con su clave de
    /// reenganche. Aparte de la sala a proposito: nada de lo que sabe Room
    /// aplica a un televisor.
    ///
    /// La entrada SOBREVIVE a la desconexion (hasta `kPantallaTtlMs`) porque
    /// el token de ingreso es de un solo uso: sin esto, un televisor que
    /// pierde el WebSocket un segundo no puede volver nunca mas y se queda en
    /// "reconectando" hasta que alguien lo vuelva a castear a mano.
    struct Pantalla {
        std::string clave;
        std::int64_t sueltaMs = 0; ///< cuando se desconecto (0: conectada)
    };
    static constexpr std::int64_t kPantallaTtlMs = 12LL * 60 * 60 * 1000;
    static constexpr std::size_t kMaxPantallas = 32;
    mutable std::mutex pantallasLock_;
    std::map<std::string, Pantalla> pantallas_;
    Rotulador rotulador_;
    /// La botonera (superficie de control): borde de red, ver Botonera.hpp.
    std::unique_ptr<BotoneraBorde> botonera_;
    /// La sesión remota (legisladores desde afuera): borde de red, ver Sesion.hpp.
    std::unique_ptr<SesionBorde> sesion_;
    Preguntas preguntas_;
    Votacion votacion_;
    mutable std::mutex publicoM_;
    nlohmann::json publicoInfo_ = nlohmann::json::object();
    std::vector<std::string> acreditaciones_;
    std::string fondoBytes_;
    std::string fondoTipo_;
    ShareStore& shares_;
    Log& log_;
    MediaPump* pump_ = nullptr;
    proxy::GestorProxy* proxy_ = nullptr;
    std::string publicAddress_;
    std::function<nlohmann::json()> networkStatus_;
    std::function<nlohmann::json(const nlohmann::json&)> serverConfig_;
    std::function<nlohmann::json(const std::string&)> joinUrl_;
    std::map<std::string, std::string> avatars_; // pid -> JPEG (solo en memoria)
    std::mutex avatarsLock_;
    std::string boardToken_;
    /// Reescribe 127.0.0.1 -> IP publica en un candidato ("candidate:...").
    std::string publicCandidate(const std::string& line) const;

    std::atomic<bool> stop_{false};
    long long listenFd_ = -1;
    long long adminListenFd_ = -1;
    uint16_t adminBoundPort_ = 0;
    uint16_t guestBoundPort_ = 0;
    std::thread acceptThread_;
    std::thread adminAcceptThread_;

    std::atomic<int> activeConns_{0};
    static constexpr int kMaxConcurrentConns = 64;
    static constexpr int kHandshakeTimeoutMs = 15000;
    std::set<long long> activeFds_;
    std::mutex activeFdsLock_;

    std::map<std::string, std::shared_ptr<WsConn>> conns_;
    std::mutex connsLock_;
    uint64_t nextGen_ = 0;
};

} // namespace ba::gateway
