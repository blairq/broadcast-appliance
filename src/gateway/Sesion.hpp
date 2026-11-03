// ============================================================================
// src/gateway/Sesion.hpp — el borde de red de la sesión remota (docs/funciones/SESION-REMOTA.md)
//
// Integrantes que sesionan desde afuera. La app (A) es la fuente de verdad:
// quién tiene credencial, quién está habilitado, la palabra y la votación viven
// allá (src/app/asamblea/SesionRemota). Este lado (B) es SOLO el borde:
// sirve la página, autentica cada WebSocket con el link personal y el PIN, guarda
// la última foto de la cámara de cada uno (para que el operador lo vea antes de
// habilitarlo) y reparte mensajes. No decide nada.
//
// A diferencia de la botonera, se entra también DESDE INTERNET (por el túnel): no
// hay restricción de red local, pero sí límites de intentos por IP y por
// integrante. Es transparente estar en la sala o afuera (también es el respaldo
// de la central): cada conexión dice su `lugar` ("sala" | "afuera").
//
// Integrante (puerto de invitados, 8787, o el HTTPS 8789, o el túnel):
//   GET /sesion/...            la página (CSP estricta)
//   GET /ws?sesion=1           WebSocket de texto (JSON). Primer mensaje
//        {type:"auth", i, t, pin} (el link trae i y t en el fragmento, nunca
//        llega al servidor por la URL) o {type:"auth", u, clave} (usuario y
//        contraseña, para entrar tipeando); después solo hola, ping, foto {jpeg}
//        y lo que la app habilite (palabra, voto: fases R2 y R4).
//
// App (API de control, loopback + Bearer):
//   POST /api/v1/sesion/credenciales {participantes:[{id, nombre, banca, sal, hash_token, hash_pin}]}
//   POST /api/v1/sesion/enviar       {id | "*" (integrantes) | "consolas", msg}
//   POST /api/v1/sesion/cortar       {id, motivo}
//   GET  /api/v1/sesion/estado       conexiones vivas
//   GET  /api/v1/sesion/foto?id=     la última foto de su cámara (image/jpeg)
//   POST /api/v1/sesion/medios       {ids: [a, b], siguiente?} (o {id | ""}): quiénes mandan audio y
//        video a la sala (R3), hasta dos a la vez (un debate): el primero va al canal 1
//        (BA.Remoto.1…), el segundo al 2. Al que entra se le dice {type:"medios", activo:true} y su
//        celular ofrece ({type:"medios-oferta", sdp, capas}, {type:"medios-ice", candidate}); al
//        que sale, false. `siguiente`: el próximo de la fila (Capas.hpp): abre su WebRTC desde
//        ya ({type:"medios", activo:true, previo:true}) con la capa alta llegando, pero no va a
//        ningún canal (ni su audio a la mezcla) hasta que le dan la palabra: ahí pasa a su
//        canal sin reconectar ({type:"medios", activo:true, previo:false}).
//   POST /api/v1/sesion/vistas       {vistas:[{id, nombre, canal, salida?}]}: los canales que el
//        integrante puede mirar en su celular (canal -1 = el programa, k = BA.Escena.<k>; con
//        `salida`, esa salida en vez del programa); se les manda {type:"vistas", lista:[{id,
//        nombre, color?}]}. Con más de una salida (SignalingServer::ponerSalidas), al lado del
//        programa van las demás («salida:<id>», con su nombre y su color): quien puede mirar el
//        programa puede mirar cualquiera. Su celular abre un WebRTC que solo recibe
//        ({type:"vista-oferta", sdp, vista}, {type:"vista-ice", candidate}) y cambia de canal
//        con {type:"vista", vista} ("yo" = ninguno: solo el sonido) sin reconectar.
//   POST /api/v1/sesion/hablar       {id, activo}: el operador le habla («apretar para hablar»
//        desde la app): su voz (BA.Audio.Operator) le llega mezclada en el sonido de su vista.
//   POST /api/v1/sesion/atiende      {id, on}: mientras espera, lo atiende el asistente HABLANDO
//        (docs/funciones/ATENCION.md). Se le dice {type:"atiende", on}; su página pide el
//        micrófono y ofrece un WebRTC de solo audio ({type:"atiende-oferta", sdp},
//        {type:"atiende-ice", candidate}; si no puede, {type:"atiende-error", error}, que va a
//        la app como evento «atiende_error»). Su voz sube a BA.Audio.Atencion (la app la pasa a
//        texto) y le baja la del que atiende (BA.Audio.Atendedor), SOLO a él. De a uno: prenderlo
//        para otro se lo apaga al anterior (hay una sola voz del que atiende). Se apaga solo si
//        se va; al habilitarlo lo apaga la app (el borde no sabe quién está habilitado).
//   y por el SSE de siempre, evento "sesion": {tipo: conexion|entrada|intento|bloqueo, …}
//
// Presidencia (la consola web, docs/funciones/SESION-REMOTA.md §8):
//   GET /presidencia/...       la página (Web/presidencia)
//   GET /ws?presidencia=1      primer mensaje {type:"auth", c, t, pin}: credencial propia
//        (id "consola:<c>"). Manda lo que la app decide (habilitar, dar_palabra,
//        votacion, ptt…), pide fotos {type:"foto", id} y hace de punta de la
//        señalización WebRTC directa con un integrante ({type:"rtc", a, …}: el
//        gateway solo la pasa, y solo entre una consola y un integrante).
//
// APARATOS REGISTRADOS (una consola en la tablet de la presidencia, el celular de un
// integrante): el PIN se pide UNA vez, para registrar el aparato. Con el PIN bien, B le da
// a la página un token de aparato (256 bits, al azar) que ella guarda en el navegador
// (comun/aparato.js) y que vale, junto con el link, para las siguientes: recargar, cerrar
// el navegador o reiniciar la app o la sala no vuelve a pedir el PIN.
//   - El primer mensaje lleva {aparato} en vez de {pin} (con el link igual: la URL tiene
//     que ser la correcta). Si no vale (revocado, de otro integrante), {ok:false,
//     error_codigo:"aparato"}: la página lo olvida y recién ahí pide el PIN.
//   - Con el PIN, {registrar:true, nombre}: el {ok:true} trae {aparato} (el token).
//   - El dueño es A (asamblea/SesionRemota): los guarda como hash con sal junto con la
//     credencial y los manda con ella (credenciales: [{…, aparatos:[{id, sal, hash}]}]).
//     Uno recién registrado queda en B como PENDIENTE hasta que A lo va a buscar
//     (GET /api/v1/sesion/aparatos; avisa el evento sesion {tipo:"aparatos"}) y lo
//     confirma (POST /api/v1/sesion/aparatos {recibidos:[id]}): una app que no estaba no
//     lo pierde. Revocar = A manda la lista sin él: la conexión de ese aparato se corta.
//   - Una B recién arrancada que todavía no recibió las credenciales de A contesta
//     {ok:false, error_codigo:"esperar"} (nunca «mal»): la página reintenta sola.
//
// Seguridad: el token del link (256 bits) y el PIN se guardan solo como hash con
// sal; comparación en tiempo constante; 10 intentos por IP cada 10 min; 5 PIN
// errados seguidos bloquean al integrante 15 min (la app se entera); Origin
// igual a Host; una conexión por integrante (la nueva corta la vieja); 20
// mensajes por segundo; latido de 30 s; fotos de hasta 96 KB.
//
// INTEGRANTES DE PRUEBA (la «Prueba de asamblea» de la app, docs/funciones/PRUEBA-ASAMBLEA.md):
// la app marca su credencial con `prueba: true`. Entran por el mismo camino que los reales
// (link + PIN o usuario + contraseña, el bloqueo por PIN errado incluido), con dos
// diferencias, solo para ellos: (1) no cuentan para el tope de intentos por IP (todos los
// simulados salen de una o pocas máquinas: con el tope, el undécimo no entraría) y (2) el
// primer mensaje puede traer `lugar_prueba` ("sala" | "afuera") para simular dónde está (desde
// la máquina del probador todos serían «sala»). Una credencial real nunca tiene la marca: la
// pone la app solo a los integrantes que creó la prueba, que borra al terminar.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Canal.hpp"

namespace ba::gateway {

class Log;

class SesionBorde {
   public:
    using Evento = std::function<void(const nlohmann::json& datos)>;
    SesionBorde(std::string webroot, Log& log, Evento alApp);

    /// Atiende si es de la sesión (página o WebSocket). true si la tomó. Si es el
    /// WebSocket, NO vuelve hasta que se cierra. `externo`: llegó por la escucha del
    /// túnel de pinggy: «afuera» siempre, y la IP es la que dio (PROXY v1), no una cabecera.
    bool atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                 const std::string& headers, std::uint32_t clientIp, bool externo = false);
    /// Rutas /api/v1/sesion/* de la API de control.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
               const std::string& body);
    void cerrar();

    /// Lo que mira un integrante en su celular: canal -1 = programa, k >= 0 =
    /// BA.Escena.<k>, -2 = nada (solo el sonido). Con `salida` (y canal -1), esa salida
    /// en vez de la por omisión.
    struct Vista {
        int canal = -2;
        std::string salida;
    };
    /// R3: lo que manda el celular del que tiene la palabra va a la app por el
    /// WebRTC del gateway. Lo arma SignalingServer (que tiene el hub y el pump).
    struct Medios {
        /// Una oferta del integrante `id`: `enviar` le contesta (respuesta e ICE). false si no se pudo.
        /// `canal`: 1 o 2 (BA.Remoto.<canal>), 0 = el siguiente (no va a ningún canal). `capas`:
        /// {alta, baja}, los mids de las dos capas de su cámara (Capas.hpp).
        std::function<bool(const std::string& id, int canal, const std::string& sdp,
                           std::function<void(const nlohmann::json&)> enviar, const nlohmann::json& capas)>
            oferta;
        /// El que ya manda (con su WebRTC armado) pasa a otro canal (0 = el siguiente).
        std::function<void(const std::string& id, int canal)> ruta;
        std::function<void(const std::string& id, const std::string& candidato)> ice;
        std::function<void(const std::string& id, int canal)> cerrar;
        // Lo que mira en su celular (un WebRTC que solo recibe), ver Vista. `hablar`: le
        // llega la voz del operador.
        std::function<bool(const std::string& id, const std::string& sdp, const Vista& vista, bool hablar,
                           std::function<void(const nlohmann::json&)> enviar)>
            vistaOferta;
        std::function<void(const std::string& id, const std::string& candidato)> vistaIce;
        std::function<void(const std::string& id, const Vista& vista, bool hablar)> vistaRuta;
        std::function<void(const std::string& id)> vistaCerrar;
        // La atención hablada mientras espera (ruta Atencion): solo su micrófono sube.
        std::function<bool(const std::string& id, const std::string& sdp,
                           std::function<void(const nlohmann::json&)> enviar)>
            atiendeOferta;
        std::function<void(const std::string& id, const std::string& candidato)> atiendeIce;
        std::function<void(const std::string& id)> atiendeCerrar;
    };
    void setMedios(Medios m) { medios_ = std::move(m); }
    /// Llegó una foto de la cámara de un integrante (la grilla de personas la muestra).
    /// Desde el hilo de su WebSocket, sin candados tomados. Antes de recibir conexiones.
    void setAlFoto(std::function<void(const std::string& id, const std::string& jpeg)> fn) { alFoto_ = std::move(fn); }
    /// Hay una foto nueva de `id` (como mucho una cada kAvisoFotoMs por integrante): la sala
    /// se lo dice a la app (evento «foto») y la sesión, a las consolas ({type:"foto_nueva", id});
    /// cada una pide la imagen solo si la muestra. Antes las dos preguntaban cada 3 s.
    void setAlFotoNueva(std::function<void(const std::string& id)> fn) { alFotoNueva_ = std::move(fn); }

    /// Prende o apaga la atención hablada de `id` (ver /api/v1/sesion/atiende). "" o el
    /// error (no está conectado). De a uno: prenderla para otro se la apaga al anterior.
    std::string ponerAtiende(const std::string& id, bool on);
    /// A quién se atiende hablando ahora ("" = a nadie).
    std::string conAtiende() const;
    /// Las salidas que hay (la lista ya saneada, Salidas.hpp): con más de una se ofrecen
    /// al lado del programa. Re-rutea a los que miran y les manda la lista nueva.
    void ponerSalidas(const nlohmann::json& saneada);

    /// Sirve `rel` de `webroot` con la CSP estricta de estas páginas (cámara y micrófono
    /// solo para ellas). La usa también el registro por tótem (Registro.hpp).
    static bool servir(long long fd, const std::string& rel, const std::string& webroot);

    // --- Para las pruebas ---
    static bool origenValido(const std::string& headers);
    /// La oferta de la atención hablada: sube SOLO audio (un video tiene que ser recvonly
    /// o inactive) y algo de audio. El token del link lo puede tener cualquiera: lo que
    /// garantiza que el que espera no le mande su cámara a nadie por acá es esto.
    static bool subeSoloAudio(const std::string& sdp);
    /// "" si no coincide; el id del integrante si el token y el PIN son los suyos.
    /// `pinMal` queda en true si el token es de alguien pero el PIN no.
    std::string autenticar(const std::string& id, const std::string& token, const std::string& pin, bool& pinMal);
    /// Con usuario y contraseña: el id, o "" (`claveMal` si el usuario existe; `id` queda con el suyo).
    std::string autenticarUsuario(const std::string& usuario, const std::string& clave, std::string& id, bool& claveMal);
    void ponerCredenciales(const nlohmann::json& participantes);
    /// ¿El token de aparato vale para la credencial `id` (con el token del link, o sin él si
    /// entró con usuario: `token` vacío)? El id del aparato, o "" si no.
    std::string autenticarAparato(const std::string& id, const std::string& token, const std::string& aparato);
    /// Registra un aparato nuevo de `id` (entró con el PIN): queda pendiente para A. El token
    /// para la página, o "" sin entropía. `aparatoId` queda con su id.
    std::string registrarAparato(const std::string& id, const std::string& nombre, std::string& aparatoId);
    /// Los registrados que A todavía no confirmó: [{de, id, sal, hash, nombre, creado_ms}].
    nlohmann::json aparatosPendientes() const;
    /// A ya los guardó: dejan de estar pendientes.
    void aparatosRecibidos(const nlohmann::json& ids);
    /// Ya llegaron las credenciales de A (antes, la página espera en vez de oír «mal»).
    bool credencialesListas() const;
    /// Cuántos integrantes están conectados ahora desde el navegador (sin las consolas).
    int conectados() const;
    bool bloqueado(const std::string& id) const;
    void anotarPinMal(const std::string& id);
    /// ¿La credencial de `id` (o la del usuario `usuario`, si `id` está vacío) es de un
    /// integrante de prueba? Ver «INTEGRANTES DE PRUEBA» arriba.
    bool credencialDePrueba(const std::string& id, const std::string& usuario = {}) const;
    /// El lugar que dice un integrante de prueba («lugar_prueba»: "sala" | "afuera"), o
    /// `real` si no es de prueba o no dijo nada que valga.
    std::string lugarDePrueba(const std::string& id, const nlohmann::json& pedido, const std::string& real) const;

   private:
    struct Conn : canal::Salida {
        std::string id, ip;
        std::string lugar; // "sala" (red local) o "afuera" (túnel o IP pública)
        std::string aparato; ///< el id del aparato registrado con el que entró ("" = ninguno)
    };
    struct Aparato {
        std::string id, sal, hash; ///< hash = hashConSal(sal + ":aparato:" + token)
    };
    struct Credencial {
        std::string nombre, banca, sal, hashToken, hashPin, usuario, hashClave;
        /// Un integrante de PRUEBA (la «Prueba de asamblea» de la app, ver arriba).
        bool prueba = false;
        std::vector<Aparato> aparatos;
        int fallas = 0;
        std::int64_t bloqueadoHastaMs = 0;
    };
    struct Pendiente {
        std::string de; ///< la credencial (integrante o «consola:<c>»)
        Aparato aparato;
        std::string nombre;
        std::int64_t creadoMs = 0;
    };
    std::vector<Pendiente> pendientes_; ///< registrados que A todavía no confirmó (con mu_)
    bool credencialesListas_ = false;   ///< A ya mandó la lista alguna vez (con mu_)
    /// ¿Ese aparato de `id` sigue valiendo (en su credencial o pendiente)? Con mu_ tomado.
    bool aparatoVale(const std::string& id, const std::string& aparato) const;
    void wsLoop(long long fd, const std::string& ip, const std::string& lugar, bool consola);
    bool intentoPermitido(const std::string& ip);

    std::string webroot_;
    Log& log_;
    Evento alApp_;
    mutable std::mutex mu_;
    std::map<std::string, Credencial> credenciales_;
    std::map<std::string, std::shared_ptr<Conn>> conns_; // una por integrante
    std::map<std::string, std::string> fotos_;           // id -> jpeg
    std::map<std::string, std::vector<std::int64_t>> intentos_;
    Medios medios_;
    std::function<void(const std::string&, const std::string&)> alFoto_;
    std::function<void(const std::string&)> alFotoNueva_;
    std::map<std::string, std::int64_t> avisoFotoMs_; ///< la última vez que se avisó la foto de cada uno (con mu_)
    static constexpr int kCanalesRemotos = 2;
    /// El canal del siguiente de la fila: manda su cámara (la alta, precargada) pero no va a
    /// ningún canal ni a la mezcla hasta que tenga la palabra.
    static constexpr int kCanalSiguiente = 0;
    /// Quiénes mandan audio y video a la sala, y por qué canal (1..2, o kCanalSiguiente).
    std::map<std::string, int> mediosDe_;
    void ponerMedios(const std::vector<std::string>& ids, const std::string& siguiente = {});
    long long ultimoSeqMedios_ = 0; ///< el número de la última lista de medios (la app los numera)
    nlohmann::json vistas_ = nlohmann::json::array(); ///< [{id, nombre, canal, salida}]
    nlohmann::json salidas_ = nlohmann::json::array(); ///< [{id, nombre, color, porOmision}]
    std::map<std::string, std::string> vistaDe_;       ///< integrante -> la vista que eligió
    std::map<std::string, bool> hablando_;             ///< a quién le habla el operador
    std::string atiende_;                               ///< a quién atiende el asistente hablando ("" = nadie)
    void avisarAtiende(const std::string& id, bool on); ///< sin mu_ tomado
    Vista vistaElegida(const std::string& vista) const; ///< con mu_ tomado
    nlohmann::json listaVistas() const;                 ///< con mu_ tomado
    /// Cambió lo que se puede mirar: re-rutea a los que miran y les manda la lista. Sin mu_.
    void difundirVistas();
};

} // namespace ba::gateway
