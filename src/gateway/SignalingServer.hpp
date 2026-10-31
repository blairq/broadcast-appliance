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
//   1b) TUNEL — 127.0.0.1:tunnelPort (8786; solo con Cfg::tunel): lo mismo
//      que la de invitados, pero para ba-pinggy (pinggy.io). TODA conexion que
//      entra por aca es EXTERNA, sin importar la IP ni las cabeceras: llega
//      desde 127.0.0.1 (el proceso de ba-pinggy) y sin esta marca se tomaria
//      como de la red local (la botonera, por ejemplo, la atenderia). La IP
//      real llega en una cabecera PROXY v1 (opcional) y es la que cuenta para
//      los limites por IP y el registro.
//
//   1c) LA PUERTA DE LA SALA (solo con Cfg::controlLan; docs/planes/PLAN-DIRECCIONES.md):
//      los servicios de la sala (tablets, presidencia, integrantes, televisores,
//      teleprompter, rótulos, corrección, tablero, público, Companion) son de la
//      red local por naturaleza y llegan aunque la sala de INVITADOS esté cerrada
//      a la red. Dos entradas:
//        - 127.0.0.1:controlPort, detrás del HTTPS propio de main.cpp
//          (controlLanPort, 8792), que reenvía con PROXY v1;
//        - puertaHttpBind:puertaHttpPort (8793), HTTP directo (televisores y
//          Companion no aceptan un certificado propio).
//      Por acá se atiende TODO menos entrar a la sala como invitado (/invitados/,
//      /api/join, sus subidas): eso sigue lo que diga «Acceso». Cada superficie
//      tiene su propia clave o emparejamiento; lo que pide permiso es lo de afuera.
//
//   2) CONTROL DEL OPERADOR — SIEMPRE 127.0.0.1:adminPort (nunca por el
//      tunel). Es la API que usa la app. Ver AdminApi.cpp para las rutas.
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
#include "Direcciones.hpp"
#include "Registro.hpp"
#include "Sesion.hpp"
#include "ControlWeb.hpp"
#include "Carruseles.hpp"
#include "MediaRoute.hpp"
#include "Preguntas.hpp"
#include "Prompter.hpp"
#include "Rotulador.hpp"
#include "Superficies.hpp"
#include "TokenAuth.hpp"
#include "Votacion.hpp"
#include "Webrtc.hpp"
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

class Log;
class MediaPump;
class Grillas;
namespace proxy {
class GestorProxy;
}
class Room;
class ShareStore;

class SignalingServer {
   public:
    struct Cfg {
        int pinIntentos = 5;  // PIN mal tantas veces desde una IP: bloqueo (Configuración → Sala)
        int pinBloqueoS = 60; // por cuánto
        uint16_t port = 8787;
        std::string bindAddress = "127.0.0.1"; // "0.0.0.0" = red local (eleccion explicita)
        std::string webroot;                   // Web/guest
        std::string room;                      // sala por defecto
        int maxGuests = 4;
        // --- API de control (loopback siempre) ---
        uint16_t adminPort = 8788; // 0 = puerto efimero (tests)
        std::string adminToken;    // Bearer / cookie de la consola; vacio = sin API de control
        int maxSharesPerGuest = 20;
        std::string boardWebroot;    // Web/board (tablero de oradores)
        std::string prompterWebroot; // Web/prompter (teleprompter del orador)
        /// Clave del control del teleprompter: sin ella cualquiera en la LAN
        /// podria frenarle el texto al orador en medio del discurso. Viaja en
        /// el enlace que el operador le pasa (o en el QR). Vacia = sin control
        /// remoto (la tablet igual muestra el texto).
        std::string prompterKey;
        std::string correccionWebroot; // Web/correccion (puesto del corrector de subtítulos, T44)
        std::string correccionKey;     // clave del puesto: nueva en cada arranque
        std::string rotulosWebroot;  // Web/rotulos (los rotulos desde el celular)
        std::string pantallaWebroot; // Web/pantalla (lo que se manda a un televisor)
        std::string castWebroot;     // Web/cast (el programa en vivo, para un televisor)
        std::string comunWebroot;    // Web/comun (el canal de control, lo usan TODAS)
        /// Clave de los rotulos. A diferencia del teleprompter, hace falta
        /// TAMBIEN PARA MIRAR: un rotulo suele llevar el nombre del invitado
        /// que todavia no se anuncio. Vacia = la pagina no atiende a nadie.
        std::string rotulosKey;
        std::string publicoWebroot; // Web/publico (la pagina del publico)
        std::string eventoWebroot;  // Web/evento (el marco de la pagina del evento en vivo y vivo.js)
        /// Clave de la pagina del publico. Viaja en el QR que se reparte (o se
        /// proyecta): no es un secreto fuerte y no pretende serlo —lo tiene
        /// todo el que esta en la sala—, es lo que evita que la cola de
        /// preguntas la llene cualquiera que pase por el tunel.
        std::string publicoKey;
        /// La clave de ba-web (src/web, adelante del túnel): con ella manda los ids de
        /// mirada que juntó su caché (X-BA-Vistos en /api/v1/evento). Vacía: no se aceptan.
        std::string webClave;
        std::string botoneraWebroot; // Web/botonera (la superficie de control de la tablet)
        std::string carasBotonera;   // <datos>/Media/botonera (las caras propias, /botonera/caras/)
        std::string sesionWebroot;   // Web/sesion (la sesión remota de los integrantes)
        std::string registroWebroot; // Web/registro (el registro de personas por tótem)
        std::string controlWebroot;  // Web/control (el dashboard web del operador, /control/)
        // --- Escucha del tunel de pinggy (siempre 127.0.0.1, todo externo) ---
        bool tunel = false;         // abrirla (main: pinggy activo)
        uint16_t tunnelPort = 8786; // 0 = puerto efimero (tests)
        // --- Aparatos de control de la red local (ver 1c; siempre 127.0.0.1) ---
        bool controlLan = false;     // abrir la puerta de la sala (ver 1c)
        uint16_t controlPort = 0;    // 0 = puerto efimero (lo normal: solo lo usa el HTTPS de adelante)
        std::string puertaHttpBind = "0.0.0.0"; // la IP de «Acceso» si es una sola
        uint16_t puertaHttpPort = 0;            // 0 = sin la entrada HTTP
    };

    SignalingServer(const Cfg& cfg, TokenAuth& auth, WebrtcHub& hub, Room& room, ShareStore& shares,
                    Log& log);
    ~SignalingServer();

    SignalingServer(const SignalingServer&) = delete;
    SignalingServer& operator=(const SignalingServer&) = delete;

    /// Motor de medios para publicar el uplink del invitado (B->A). Antes de start().
    void setPump(MediaPump* pump) { pump_ = pump; }
    /// La «Grilla de personas» (Grillas.hpp): el mosaico que arma la sala. Antes de start().
    /// Le llegan los cuadros, el audio y las fotos de cada persona; su API es /api/v1/grillas.
    void setGrillas(Grillas* g);
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
    /// Puerto real de la escucha del tunel (0 si no esta abierta).
    uint16_t tunnelPort() const { return tunnelBoundPort_; }
    /// Puerto real de la escucha de los aparatos de control (0 si no esta abierta).
    uint16_t controlPort() const { return controlBoundPort_; }
    /// Puerto real de la entrada HTTP de la puerta de la sala (0 si no esta abierta).
    uint16_t puertaHttpPort() const { return puertaHttpBoundPort_; }
    /// ¿El control web está habilitado desde afuera (túnel, con PIN)?
    bool controlWebDesdeAfuera() const;
    /// ¿Hay una página del evento publicada en la raíz?
    bool eventoWebPublicado() const { return eventoWebActivo() && !cfg_.eventoWebroot.empty(); }

    /// La IP de origen de una cabecera PROXY v1 ("PROXY TCP4 <src> <dst> <sp>
    /// <dp>"), en orden de red. TCP6: la IPv4 mapeada (::ffff:a.b.c.d) o, si
    /// no, una seudo-IP estable 240.x.y.z por /64 (clase E: nunca es la red
    /// local y separa a los clientes para los limites). :returns: false si la
    /// linea no se entiende (UNKNOWN incluido).
    static bool ipDeProxyV1(const std::string& linea, std::uint32_t& ipRed);
    /// La IP real del X-Forwarded-For (el ULTIMO de la lista: el que agrego el
    /// proxy de adelante). Solo se usa en la escucha del tunel sin PROXY v1.
    static bool ipDeXff(const std::string& valor, std::uint32_t& ipRed);
    /// Una IP en texto (IPv4, o IPv6 como en ipDeProxyV1) en orden de red.
    static bool ipDeTexto(const std::string& texto, std::uint32_t& ipRed);

    /// IP publica del tunel WireGuard: los candidatos ICE de loopback (los que
    /// atiende el puente del tunel) se anuncian con esta IP. Vacio = no.
    void setPublicAddress(const std::string& ip) { publicAddress_ = ip; }
    /// El tema de las interfaces web de control (lo elige la app: POST /api/v1/tema).
    /// Las páginas lo leen de /comun/tema.json. :returns: false si el id no vale.
    bool setTemaWeb(const std::string& id);
    std::string temaWeb() const;
    /// El tema del EVENTO abierto (docs/planes/PLAN-TEMA-EVENTO.md; lo manda la app con
    /// POST /api/v1/tema-evento al abrir o cambiar de evento y cada vez que la sala vuelve):
    /// {tema: lo de Eventos/<x>/tema.json, carpeta: Eventos/<x>/Tema}. Sin `tema` (o vacío),
    /// las páginas quedan como son. Lo resuelve ba::tema (colores que faltan, contraste) y
    /// arma /comun/tema.css una vez. Devuelve {v, avisos}.
    nlohmann::json setTemaEvento(const nlohmann::json& cuerpo);
    /// El teleprompter, para que la API de control (A) le cargue discursos y
    /// lo comande sin pasar por la clave del control remoto.
    Prompters& prompters() { return prompters_; }
    /// El buzon de rotulos, que A publica y viene a vaciar.
    Rotulador& rotulador() { return rotulador_; }
    Preguntas& preguntas() { return preguntas_; }
    Votacion& votacion() { return votacion_; }

    /// Lo que la pagina del publico muestra arriba y de fondo. Lo publica A,
    /// que es el unico que sabe que evento esta abierto. B solo lo repite.
    void setPublicoInfo(const nlohmann::json& info);
    nlohmann::json publicoInfo() const;
    /// La página del evento en vivo (src/app/evento/EventoWeb.hpp). La manda A con el
    /// evento abierto: {activo, dir: la carpeta Web/ del evento en el disco, estado:
    /// lo público}. Con `activo`, la raíz de la sala sirve el marco del evento (la
    /// página de la sección que se trata) y los invitados entran por /invitados/.
    void setEventoWeb(const nlohmann::json& e);
    nlohmann::json eventoWeb() const;
    /// Los bloqueos por PIN mal: {global: {fallos, maximo, hasta, bloqueado}, ips: [{ip,
    /// fallos, hasta, bloqueado}], intentosPorIp, bloqueoS}. Para el operador (API de control).
    nlohmann::json bloqueosPin() const;
    /// Levanta los bloqueos: los de esa IP, o (ip vacía) todos, el global incluido.
    /// Devuelve cuántas IPs se desbloquearon.
    int desbloquearPin(const std::string& ip);
    /// Se llama (fuera de los locks) cada vez que cambia la carpeta Web/ del evento en vivo
    /// ("" si no hay): ba-web la sirve desde ahí.
    void setAlCambiarEventoDir(std::function<void(const std::string&)> fn) { alCambiarEventoDir_ = std::move(fn); }
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
    /// Las direcciones de los servicios (Direcciones.hpp): quien junta las entradas
    /// (main, frescas en cada llamada). Se recalculan cada 2 s; al cambiar, evento
    /// «direcciones» a la app y `alCambiar` (p. ej. el enlace de invitados de la sala).
    void setDirecciones(std::function<direcciones::Entradas()> entradas,
                        std::function<void(const nlohmann::json&)> alCambiar = {}) {
        direccionesEntradas_ = std::move(entradas);
        alCambiarDirecciones_ = std::move(alCambiar);
        recalcularDirecciones();
    }
    /// El catálogo vigente, con su `seq` (GET /api/v1/direcciones).
    nlohmann::json direcciones() const;
    /// El origen de un servicio, "local" o "remote": {origen, url} o {error, message}.
    nlohmann::json origenDe(const std::string& servicio, const std::string& donde) const;
    /// Genera la configuracion del servidor WireGuard (/api/v1/network/server-config).
    void setServerConfigGenerator(std::function<nlohmann::json(const nlohmann::json&)> fn) {
        serverConfig_ = std::move(fn);
    }
    /// Estado del acceso remoto para GET /api/remoto: {"pinggy":{...}, ...}.
    /// Sin proveedor: pinggy apagado.
    void setRemotoStatus(std::function<nlohmann::json()> fn) { remotoStatus_ = std::move(fn); }
    /// GET /api/ice: los iceServers para los navegadores (turn/Ice.hpp). Recibe si
    /// la conexión vino por el túnel (externa) y devuelve el cuerpo JSON.
    void setIce(std::function<std::string(bool externo)> fn) { ice_ = std::move(fn); }

    /// Cambia el PIN de la sala (API de control). Vacio = generar uno.
    /// :returns: el PIN vigente, o vacio si no se pudo generar.
    std::string changePin(const std::string& pin);
    /// El PIN del día: el último PIN de la sala queda guardado (cifrado, en
    /// Credenciales) con la fecha local. Los arranques del mismo día lo reusan;
    /// con otro día se genera uno nuevo. Vacío si no hay o es de otro día.
    static std::string pinDeHoy();
    static void guardarPinDeHoy(const std::string& pin);
    /// Clave del tablero de oradores (aleatoria por arranque).
    const std::string& boardToken() const { return boardToken_; }

   private:
    // --- Conexiones ---------------------------------------------------------
    /// `externo`: la escucha del tunel (ver el header): toda conexion es de afuera.
    void acceptLoop(long long listenFd, bool admin, bool externo = false, bool soloControl = false);
    void handleConnection(long long fd, std::uint32_t clientIp, bool admin, bool externo, bool soloControl);
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
    /// `canal`: la escena que muestra (-1: ninguna); si no, `salida` ("" = la por omisión).
    std::string altaPantalla(std::string& claveOut, int canal = -1, const std::string& salida = {});
    /// La clave de reenganche de esa pantalla (vacia si no existe).
    std::string claveDePantalla(const std::string& pid) const;
    /// Valida un reenganche. true si ese id existe y la clave coincide.
    bool reengancharPantalla(const std::string& pid, const std::string& clave, int canal = -1,
                             const std::string& salida = {});
    /// La ruta de una pantalla: su escena, o su salida (la fila sin preview: recibe y no
    /// sube nada a ningún lado).
    MediaRoute rutaDePantalla(const std::string& pid) const;
    void bajaPantalla(const std::string& pid);

    void handleGuestHttp(long long fd, const std::string& method, const std::string& path,
                         const std::string& query, const std::string& headers, const std::string& body,
                         std::uint32_t clientIp);
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
    /// R3: la oferta del celular de un integrante remoto con la palabra -> ruta Remoto 1.
    bool ofertaRemoto(const std::string& id, int canal, const std::string& sdp,
                      std::function<void(const nlohmann::json&)> enviar);
    /// Lo que mira un integrante en su celular (sesión del pool que solo recibe, pid «vista:<id>»).
    bool ofertaVista(const std::string& id, const std::string& sdp, const SesionBorde::Vista& vista, bool hablar,
                     std::function<void(const nlohmann::json&)> enviar);
    static MediaRoute rutaDeVista(const SesionBorde::Vista& vista, bool hablar);
    /// La atención hablada del que espera en la sesión remota (pid «atiende:<id>», ruta
    /// Atencion): sube su micrófono a BA.Audio.Atencion y le baja la voz del que atiende.
    bool ofertaAtencion(const std::string& id, const std::string& sdp, std::function<void(const nlohmann::json&)> enviar);
    /// Quien atiende el registro por web (pid «registro-atiende:<aconn>», ruta
    /// RegistroAtiende(canal, habla)): recibe la cámara y el sonido del tótem que mira y, si
    /// lo atiende, su micrófono baja solo a ese tótem.
    bool ofertaRegistroAtiende(const std::string& aconn, int canal, bool habla, const std::string& sdp,
                               std::function<void(const nlohmann::json&)> enviar);
    /// Una conversación del registro por tótem (pid «registro:<conn>», ruta Registro(canal)):
    /// sube su micrófono a BA.Audio.Registro.<canal> y le baja BA.Audio.RegistroVoz.<canal>.
    bool ofertaRegistro(const std::string& conn, int canal, const std::string& sdp,
                        std::function<void(const nlohmann::json&)> enviar);

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
    mutable std::mutex ipAttemptsLock_;
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

    struct SseConn {
        long long fd = -1;
        std::mutex sendLock;
    };
    std::vector<std::shared_ptr<SseConn>> sse_;
    std::mutex sseLock_;

    Cfg cfg_;
    TokenAuth& auth_;
    WebrtcHub& hub_;
    Room& room_;
    Prompters prompters_; ///< los teleprompters (Prompter.hpp: «principal» y los que se agreguen)
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
    /// La entrada SOBREVIVE a la desconexion (hasta `kPantallaTtlMs()`) porque
    /// el token de ingreso es de un solo uso: sin esto, un televisor que
    /// pierde el WebSocket un segundo no puede volver nunca mas y se queda en
    /// "reconectando" hasta que alguien lo vuelva a castear a mano.
    struct Pantalla {
        std::string clave;
        int canal = -1; ///< canal de escena que muestra (BA.Escena.<k>); -1: el programa
        std::string salida; ///< con canal -1: de qué salida es el programa ("" = la por omisión)
        std::int64_t sueltaMs = 0; ///< cuando se desconecto (0: conectada)
    };
    static std::int64_t kPantallaTtlMs() { return ba::ajustes::entero64("pantallas.ttlHoras", 12) * 60 * 60 * 1000; }
    static std::size_t kMaxPantallas() { return static_cast<std::size_t>(ba::ajustes::entero("pantallas.max", 32)); }
    mutable std::mutex pantallasLock_;
    std::map<std::string, Pantalla> pantallas_;
    Rotulador rotulador_;
    /// La botonera (superficie de control): borde de red, ver Botonera.hpp.
    std::unique_ptr<BotoneraBorde> botonera_;
    /// La sesión remota (integrantes desde afuera): borde de red, ver Sesion.hpp.
    std::unique_ptr<SesionBorde> sesion_;
    /// El registro de personas por tótem: borde de red, ver Registro.hpp.
    std::unique_ptr<RegistroBorde> registro_;
    /// El control web (/control/ y /ws?control): borde de red, ver ControlWeb.hpp.
    std::unique_ptr<ControlWebBorde> controlWeb_;
    Preguntas preguntas_;
    Votacion votacion_;
    mutable std::mutex publicoM_;
    nlohmann::json publicoInfo_ = nlohmann::json::object();
    /// Los que miran la página del evento: id del navegador (lo manda el marco) → última vez.
    std::map<std::string, std::int64_t> mirando_;
    mutable std::mutex eventoWebM_;
    std::function<void(const std::string&)> alCambiarEventoDir_;
    /// El caché de /api/v1/evento (ver eventoPublicoCacheado): la respuesta armada, la de
    /// /api/v1/evento/vivos y cuándo se armó (0 = hay que rearmarla).
    std::mutex eventoCacheM_;
    std::string eventoCache_, eventoCacheVivos_;
    std::int64_t eventoCacheMs_ = 0;
    std::pair<std::string, std::string> eventoPublicoCacheado();
    nlohmann::json eventoWeb_ = nlohmann::json::object();
    /// Hay sitio del evento para mostrar (activo y su carpeta existe).
    bool eventoWebActivo(std::string* dir = nullptr) const;
    std::vector<std::string> acreditaciones_;
    std::string fondoBytes_;
    std::string fondoTipo_;
    ShareStore& shares_;
    Log& log_;
    MediaPump* pump_ = nullptr;
    // Las salidas del aparato (docs/planes/PLAN-SALIDAS.md), como las manda la app por
    // POST /api/v1/salidas, ya saneadas (Salidas.hpp). Las usan el motor de medios (de
    // qué salida baja cada ruta) y la sesión remota (qué se puede mirar).
    mutable std::mutex salidasMu_;
    nlohmann::json salidas_ = nlohmann::json::array();
    void ponerSalidas(const nlohmann::json& lista);
    // La grilla de personas: el alto del lugar más grande de cada participante en las
    // grillas y la calidad que ya se le pidió a su navegador (ver enviarCalidadGrilla).
    Grillas* grillas_ = nullptr;
    std::mutex calidadMu_;
    std::map<std::string, int> altoGrilla_;           // pid -> alto en píxeles (0 = en ninguna)
    std::map<std::string, std::string> calidadPedida_; // pid -> "180p" | "360p" | "libre"
    void enviarCalidadGrilla(const std::string& pid, bool forzar = false);
    // T44: las frases que esperan al corrector (las manda la app) y desde cuándo.
    std::mutex correccionMu_;
    nlohmann::json correccionPend_ = nlohmann::json::array();
    std::int64_t correccionMs_ = 0;
    proxy::GestorProxy* proxy_ = nullptr;
    std::string publicAddress_;
    mutable std::mutex temaLock_;
    std::string temaWeb_ = "neuro-oscuro";
    /// El del evento, ya armado (bajo temaLock_): lo que se publica en /comun/tema.json,
    /// la hoja, su versión y de dónde salen el logo y las fuentes.
    nlohmann::json temaEventoPublico_; // null = sin tema
    std::string temaEventoCss_, temaEventoVersion_, temaEventoCarpeta_;
    /// GET /comun/tema.json, /comun/tema.css y /comun/tema/<archivo> (todas las superficies,
    /// por todas las puertas). true si lo atendió.
    bool servirTema(long long fd, const std::string& method, const std::string& path, const std::string& query);
    std::function<nlohmann::json()> networkStatus_;
    std::function<nlohmann::json(const nlohmann::json&)> serverConfig_;
    std::function<direcciones::Entradas()> direccionesEntradas_;
    std::function<void(const nlohmann::json&)> alCambiarDirecciones_;
    mutable std::mutex direccionesLock_;
    nlohmann::json direcciones_ = nlohmann::json::object();
    std::int64_t direccionesSeq_ = 0;
    void recalcularDirecciones();
    std::function<nlohmann::json()> remotoStatus_;
    std::function<std::string(bool externo)> ice_;
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
    long long tunnelListenFd_ = -1;
    uint16_t tunnelBoundPort_ = 0;
    long long controlListenFd_ = -1;
    uint16_t controlBoundPort_ = 0;
    std::thread controlAcceptThread_;
    long long puertaHttpFd_ = -1;
    uint16_t puertaHttpBoundPort_ = 0;
    std::thread puertaHttpThread_;
    std::thread acceptThread_;
    std::thread adminAcceptThread_;
    std::thread tunnelAcceptThread_;

    std::atomic<int> activeConns_{0};
    static int kMaxConcurrentConns() { return ba::ajustes::entero("room.maxConexiones", 64); }
    static int kHandshakeTimeoutMs() { return ba::ajustes::entero("room.handshakeMs", 15000); }
    std::set<long long> activeFds_;
    std::mutex activeFdsLock_;

    std::map<std::string, std::shared_ptr<WsConn>> conns_;
    std::mutex connsLock_;
    uint64_t nextGen_ = 0;
};

} // namespace ba::gateway
