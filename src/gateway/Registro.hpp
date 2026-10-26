// ============================================================================
// src/gateway/Registro.hpp — el borde de red del registro de personas por tótem
//                            (docs/funciones/REGISTRO-TOTEM.md)
//
// Un tótem (o una TV con cámara y micrófono, o el celular de quien escanea su QR)
// donde la persona pone la cara y se registra HABLANDO: el asistente le pregunta
// los datos que se configuraron y le contesta con voz. La app (A) es la fuente de
// verdad: qué tótems hay, qué se pregunta, la conversación (un guion determinista)
// y dónde se guarda. Este lado (B) es SOLO el borde, a imagen de la sesión remota
// (Sesion.hpp): sirve la página, autentica cada WebSocket con el id y el token del
// tótem, reparte los canales (el micrófono y la cámara de cada conversación van por
// WebRTC a BA.Audio.Registro.<k> y BA.Registro.<k>) y pasa mensajes. No decide nada.
//
// Cada tótem admite varias conexiones a la vez (la TV que muestra el QR y cada
// celular que lo escanea): cada una tiene su `conn` ("c17") y su conversación.
// Conversaciones con voz a la vez: hasta kMaxTotems (los canales de audio 1..8,
// MediaRoute::registro). Sin canal libre, la página espera y vuelve a pedir.
//
// Página (puerto de invitados, el HTTPS de la sala o el túnel):
//   GET /registro/...          la página (CSP estricta; Web/registro)
//   GET /ws?registro=1         WebSocket de texto (JSON). Primer mensaje
//        {type:"auth", r, t} (el link /registro/#r=<id>&t=<token> los trae en el
//        FRAGMENTO: nunca llegan al servidor por la URL). Respuesta
//        {type:"hola", nombre, conn} o {type:"error", error} y se cierra.
//        Después:
//        {type:"canal"}              -> {type:"canal", canal:k} o {type:"canal", canal:0, error:"lleno"}
//        {type:"soltar"}             suelta el canal (la persona terminó y la página queda esperando)
//        {type:"oferta", sdp}        WebRTC de micrófono y cámara (ofertaValida) por su canal
//                                    -> {type:"respuesta", sdp}; {type:"ice", candidate} en los dos sentidos
//        {type:"empezar"} | {type:"cancelar"} | {type:"escrito", clave, valor}  -> a la app («toque»)
//        {type:"ping"}               -> {type:"pong"}
//        y le llega tal cual lo que la app mande por /api/v1/registro/enviar.
//
// App (API de control, loopback + Bearer):
//   POST /api/v1/registro/totems  {totems:[{id, nombre, sal, hash_token}]} reemplaza la lista;
//        hash_token = sha256 hex de (sal + token). Un tótem que desaparece (o cambia de token)
//        corta sus conexiones.
//   GET  /api/v1/registro/estado  {conexiones:[{conn, id, canal, ip, lugar, desdeMs, audio}]}
//   POST /api/v1/registro/enviar  {conn, msg} o {id, msg} (a todas las de ese tótem)
//   POST /api/v1/registro/cortar  {conn}
//   y por el SSE de siempre, evento "registro":
//     {tipo:"conexion", conn, id, conectado}   {tipo:"canal", conn, id, canal}  (0 = lo soltó)
//     {tipo:"audio", conn, id, listo}          (el WebRTC de micrófono y cámara)
//     {tipo:"toque", conn, id, accion:"empezar"|"cancelar"|"escrito", clave?, valor?}
//     {tipo:"intento", id, ip}                 (un link que no vale: para el registro de la app)
//
// Atención remota (RegistroAtiende.cpp; docs/funciones/REGISTRO-TOTEM.md, «Atención remota»):
// una persona del equipo, desde cualquier lado, ve TODAS las conversaciones del evento, mira y
// escucha una en vivo, la atiende ella (el asistente se calla y su voz le llega solo a ese
// tótem) y completa o corrige los datos. Un link por evento (cualquiera del equipo lo usa; cada
// una pone su nombre, que es lo que ve la persona del tótem: «Te atiende Laura»).
//   GET /registro-atiende/...          la página (Web/registro-atiende, al lado de Web/registro)
//   GET /ws?registro_atiende=1         primer mensaje {type:"auth", t, nombre} (el token viene en
//        el FRAGMENTO del link /registro-atiende/#t=<token>) -> {type:"hola", aconn, nombre}.
//        Después:
//        {type:"mirar", conn}        mira y escucha esa conversación ("" = ninguna) -> {type:"mirando", conn, canal}
//        {type:"oferta", sdp}        su WebRTC: un audio (su micrófono; por ahí oye al tótem) y a lo
//                                    sumo un video (por ahí ve la cámara del tótem) -> respuesta / ice
//        {type:"atender"|"devolver"|"guardar"|"cancelar", conn} | {type:"dato", conn, clave, valor}
//                                    -> a la app (evento «atiende»): ella decide
//   App: POST /api/v1/registro/atencion {sal, hash_token} (vacío = sin atención remota; cambiarlo
//        corta a quienes estaban), POST /api/v1/registro/atendida {conn, aconn} (quién atiende esa
//        conversación; "" = el asistente: solo entonces la voz de esa persona baja al tótem),
//        POST /api/v1/registro/enviar {aconn, msg} o {atiende:"*", msg}; en el estado, `atienden`.
//   SSE «registro»: {tipo:"atiende-conexion", aconn, nombre, conectado, ip, lugar},
//        {tipo:"atiende", aconn, nombre, accion, conn, clave?, valor?}, {tipo:"intento", atiende:true, ip}.
//
// Seguridad, como la sesión remota: el token (256 bits) se guarda solo como hash con
// sal y se compara en tiempo constante; Origin igual a Host; 20 mensajes por segundo;
// latido de 30 s; mensajes de hasta 64 KB. Los intentos por IP cuentan solo los FALLIDOS
// (10 cada 10 min): un tótem se escanea desde muchos celulares detrás del mismo wifi,
// y entrar bien no puede dejar afuera al siguiente.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Log;

class RegistroBorde {
   public:
    using Evento = std::function<void(const nlohmann::json& datos)>;
    RegistroBorde(std::string webroot, Log& log, Evento alApp);

    /// Atiende si es del registro (página o WebSocket). true si la tomó. Si es el
    /// WebSocket, NO vuelve hasta que se cierra. `externo`: llegó por la escucha del
    /// túnel de pinggy (la IP es la que dio, no una cabecera).
    bool atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                 const std::string& headers, std::uint32_t clientIp, bool externo = false);
    /// Rutas /api/v1/registro/* de la API de control.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
               const std::string& body);
    void cerrar();

    /// El audio de una conversación va a la app por el WebRTC del gateway (una sesión
    /// del pool, ruta Registro(canal)). Lo arma SignalingServer (que tiene el hub y el pump).
    struct Medios {
        /// Una oferta de la conexión `conn` por el canal `canal` (1..kMaxTotems): `enviar` le
        /// contesta (respuesta e ICE). false si no se pudo.
        std::function<bool(const std::string& conn, int canal, const std::string& sdp,
                           std::function<void(const nlohmann::json&)> enviar)>
            oferta;
        std::function<void(const std::string& conn, const std::string& candidato)> ice;
        /// Soltó el canal `canal`: se cierra su WebRTC y su cámara queda en negro.
        std::function<void(const std::string& conn, int canal)> cerrar;
    };
    void setMedios(Medios m) { medios_ = std::move(m); }
    /// El WebRTC de audio de `conn` quedó conectado o se cayó (lo avisa SignalingServer).
    /// A la app, solo si cambió y si la conexión sigue teniendo canal.
    void audioListo(const std::string& conn, bool listo);

    // --- Para las pruebas (y lo que usa el lazo del WebSocket) ---
    /// `[a-z0-9-]{1,32}`.
    static bool idValido(const std::string& id);
    /// Reemplaza la lista de tótems. Devuelve los `conn` que hay que cortar (su tótem
    /// ya no está o cambió de token).
    std::vector<std::string> ponerTotems(const nlohmann::json& totems);
    /// El nombre del tótem si el token es el suyo; si no, false.
    bool autenticar(const std::string& id, const std::string& token, std::string& nombre) const;
    /// Anota una conexión autenticada (sin socket en las pruebas: fd -1). Devuelve su `conn`,
    /// o "" si no hay lugar.
    std::string alta(const std::string& id, const std::string& ip, const std::string& lugar, long long fd = -1);
    /// La conexión se fue: suelta su canal y su WebRTC, y avisa a la app.
    void baja(const std::string& conn);
    /// Le da un canal a `conn` (el que ya tenía, o el primero libre). 0 = no hay.
    int tomarCanal(const std::string& conn);
    /// Suelta el canal de `conn` (y cierra su WebRTC). Nada si no tenía.
    void soltarCanal(const std::string& conn);
    int canalDe(const std::string& conn) const;
    /// La oferta WebRTC de la página: un audio (el micrófono; por ahí baja la voz del
    /// asistente) y a lo sumo un video (la cámara). Nada más.
    static bool ofertaValida(const std::string& sdp);
    nlohmann::json estado() const;
    /// Los `conn` de un tótem (para enviar a todas).
    std::vector<std::string> conexionesDe(const std::string& id) const;
    /// Un mensaje de la página (ya autenticada) que va a la app como «toque»; null si no es uno.
    static nlohmann::json toque(const std::string& conn, const std::string& id, const nlohmann::json& m);

    // --- La atención remota (RegistroAtiende.cpp) -----------------------------------------
    /// Los medios de quien atiende: una sesión del pool por página (ruta RegistroAtiende). Lo
    /// arma SignalingServer. `canal`: el de la conversación que mira (0 = ninguna); `habla`: la
    /// atiende (su micrófono baja a ese tótem).
    struct MediosAtiende {
        std::function<bool(const std::string& aconn, int canal, bool habla, const std::string& sdp,
                           std::function<void(const nlohmann::json&)> enviar)>
            oferta;
        std::function<void(const std::string& aconn, const std::string& candidato)> ice;
        std::function<void(const std::string& aconn, int canal, bool habla)> ruta;
        std::function<void(const std::string& aconn)> cerrar;
    };
    void setMediosAtiende(MediosAtiende m) { mediosAtiende_ = std::move(m); }
    /// La credencial del link de atención {sal, hash_token} (vacía = no hay). Devuelve los
    /// `aconn` a cortar (cambió o se sacó).
    std::vector<std::string> ponerCredencialAtiende(const nlohmann::json& c);
    bool autenticarAtiende(const std::string& token) const;
    /// Anota una página de atención autenticada. Su `aconn` ("a3"), o "" si no hay lugar.
    std::string altaAtiende(const std::string& nombre, const std::string& ip, const std::string& lugar, long long fd = -1);
    void bajaAtiende(const std::string& aconn);
    /// Mira la conversación `conn` ("" = ninguna). false si no hay esa página o esa conversación.
    bool mirar(const std::string& aconn, const std::string& conn);
    /// La app dice quién atiende la conversación `conn` ("" = nadie: el asistente). Quien la
    /// atiende pasa a mirarla. false si no hay esa conversación (o esa página).
    bool ponerAtendida(const std::string& conn, const std::string& aconn);
    std::string atendidaPor(const std::string& conn) const;
    /// La ruta de medios que le toca a `aconn` ahora: {canal que mira, si le habla}.
    std::pair<int, bool> rutaAtiende(const std::string& aconn) const;
    /// El nombre que puso quien atiende, prolijo: sin controles, a lo sumo 40 bytes; "" si no queda nada.
    static std::string nombreAtiende(const std::string& s);
    /// Un mensaje de la página de atención que va a la app (evento «atiende»); null si no es uno.
    static nlohmann::json accionAtiende(const std::string& aconn, const std::string& nombre, const nlohmann::json& m);
    std::vector<std::string> atiendenTodas() const;

   private:
    struct Conn {
        long long fd = -1;
        std::string conn, id, ip;
        std::string lugar; // "sala" (red local) o "afuera" (túnel o IP pública)
        std::int64_t desdeMs = 0;
        int canal = 0;
        bool audio = false;
        std::mutex sendLock;
    };
    struct Totem {
        std::string nombre, sal, hashToken;
    };
    void wsLoop(long long fd, const std::string& ip, const std::string& lugar);

    // La atención remota (RegistroAtiende.cpp).
    struct Atiende {
        long long fd = -1;
        std::string aconn, nombre, ip, lugar;
        std::string mira; // la conversación (`conn`) que mira; "" = ninguna
        std::int64_t desdeMs = 0;
        bool medios = false; // ya ofreció su WebRTC
        std::mutex sendLock;
    };
    bool atenderAtiende(long long fd, const std::string& method, const std::string& path, const std::string& query,
                        const std::string& headers, std::uint32_t clientIp, bool externo);
    bool adminAtiende(long long fd, const std::string& method, const std::string& que, const std::string& body);
    void wsLoopAtiende(long long fd, const std::string& ip, const std::string& lugar);
    void enviarAtiende(const std::shared_ptr<Atiende>& a, const nlohmann::json& msg);
    std::shared_ptr<Atiende> buscarAtiende(const std::string& aconn) const;
    /// La conversación `conn` cambió de canal, se fue o cambió quién la atiende: las rutas de
    /// quienes la miran se rehacen.
    void reubicarAtienden(const std::string& conn);
    void aplicarRuta(const std::shared_ptr<Atiende>& a);
    std::pair<int, bool> rutaAtiendeSinLock(const Atiende& a) const;
    nlohmann::json estadoAtiendenSinLock() const;
    void enviar(const std::shared_ptr<Conn>& c, const nlohmann::json& msg);
    void enviarA(const std::string& conn, const nlohmann::json& msg);
    void cortarConn(const std::string& conn, const std::string& motivo);
    bool intentoPermitido(const std::string& ip);
    void anotarIntentoFallido(const std::string& ip);
    std::shared_ptr<Conn> buscar(const std::string& conn) const;

    std::string webroot_;
    Log& log_;
    Evento alApp_;
    Medios medios_;
    mutable std::mutex mu_;
    std::map<std::string, Totem> totems_;
    std::map<std::string, std::shared_ptr<Conn>> conns_; // conn -> conexión
    std::map<std::string, std::vector<std::int64_t>> fallidos_; // ip -> intentos fallidos
    std::map<int, std::string> canales_;                 // canal (1..kMaxTotems) -> conn
    unsigned long long siguiente_ = 0;                   // para el próximo `conn`
    // La atención remota (bajo mu_).
    MediosAtiende mediosAtiende_;
    std::string salAtiende_, hashAtiende_;
    std::map<std::string, std::shared_ptr<Atiende>> atienden_; // aconn -> página de atención
    std::map<std::string, std::string> atendida_;               // conn -> aconn que la atiende
    unsigned long long siguienteAtiende_ = 0;
};

} // namespace ba::gateway
