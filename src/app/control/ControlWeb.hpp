// SPDX-License-Identifier: GPL-2.0-or-later
//
// ControlWeb: el puente seguro del dashboard web (/control/ y las consolas nuevas)
// con la app (docs/planes/PLAN-WEB-CONTROL.md §2). Modelo, sin interfaz.
//
// Un aparato (tablet, notebook, PC de la red local) se EMPAREJA como la
// botonera: QR con un código de un uso → el navegador lo canjea en el gateway →
// UNA PERSONA lo aprueba en la app → el aparato guarda su token (acá queda solo
// el hash con sal). Cada aparato tiene un PERFIL: las categorías de
// herramientas MCP que puede usar (["*"] = todas). Ampliar un perfil (o aprobar
// un aparato, que es ampliar desde nada) lo hace una persona: en la app, o el
// asistente con la aprobación explícita de una persona. Nunca el aparato.
//
// La app es la fuente de verdad: el gateway (src/gateway/ControlWeb) solo
// autentica el WebSocket con los hashes que le pasa ControlWebGateway y reenvía
// los mensajes de cada conexión con su id. Todo lo que se decide, se decide acá.
//
// Config/control-web.json (en la carpeta de datos del evento):
//   {aparatos: [{id, nombre, hash, sal, perfil: ["*"] | [categorías], ip,
//                creado_ms, ultimo_uso_ms, preferencias: {…}}],
//    tunel: {habilitado, sal, hash_pin}}
//
// --- Protocolo del WebSocket /ws?control=1 (JSON de texto, campo `type`) ------
//
// aparato → app
//   {type:"auth", token, pin?}     el primero (lo verifica el gateway; `pin` solo
//                                  desde afuera, con el túnel habilitado).
//   {type:"llamar", id, herramienta, args, confirmado?}
//                                  corre la herramienta MCP con
//                                  CallContext{origen:"control-web:<aparato>",
//                                  humano:true}: una persona apretando, como la
//                                  botonera. Fuera del perfil, no corre.
//   {type:"mirar", temas:[…]}      reemplaza lo que mira esta conexión ([] = nada).
//   {type:"hola"}                  pide de nuevo el auth (perfil y herramientas).
//   {type:"preferencias", datos:{…}}  lo que el aparato quiere recordar (p. ej.
//                                  qué tarjetas muestra): se guarda en SU registro,
//                                  no en el navegador (sobrevive a reinicios, a
//                                  borrar los datos del navegador y a otra URL).
//                                  Como mucho 8 KiB.
//   {type:"salida", salida}        el aparato pasa a manejar esa salida (id; "" = la
//                                  por omisión): lo que llama va con ella en el
//                                  contexto (CallContext::salida) y los temas que mira
//                                  son los de ella. Elegir salida no amplía el perfil.
//   {type:"ping"}                  lo contesta el gateway con {type:"pong"}.
//
// app → aparato
//   {type:"auth", ok:true, aparato, nombre, perfil, lugar, preferencias, categorias:[{id,titulo}],
//    salida, salidas:[{id, nombre, color, por_omision}],
//    herramientas:[{name, title, category, description, inputSchema, readOnly,
//                   destructive, aire}], mas}
//                                  `herramientas` va en tandas (el catálogo
//                                  entero no entra en un mensaje): si `mas`,
//                                  siguen {type:"herramientas", herramientas, mas}
//                                  hasta mas:false. Se vuelve a mandar entero
//                                  si cambia el perfil.
//   {type:"auth", ok:false, error} y el gateway corta.
//   {type:"salida", salida, salidas, error?}
//                                  cambió la salida del aparato (o la lista de salidas).
//   {type:"resultado", id, ok, datos | error}
//                                  datos = structuredContent, o el texto como
//                                  JSON si lo es, o el texto.
//   {type:"confirmar", id, herramienta, texto}
//                                  la herramienta es «de aire» y hay aire
//                                  (McpCore::aire().esDeAire + hayAire): no se
//                                  corrió. Para correrla, el mismo llamar con
//                                  confirmado:true (después de que la persona
//                                  confirme en la pantalla del aparato).
//   {type:"mirando", temas:[…], rechazados:[{tema, error}]}
//   {type:"estado", tema, datos}   SOLO cuando cambió (y una vez al empezar a
//                                  mirarlo). Con un tema de varias
//                                  herramientas, datos = {herramienta: datos}.
//
// Temas (tema → herramientas de solo lectura, cada cuánto se miran):
//   escenas → scene_list (500 ms)          audio → mezclador_estado (66 ms; mientras
//   ptz → ptz_camaras (500 ms)                     alguien mira, el Mezclador mide solo)
//   salidas → stream_salud + record_disco (1 s)    latencias → tiempos_mapa (1 s)
//   estado → status (2 s)                  sala → room_status (1 s)
//   oradores → remotos_fila_ver (1 s)       graficos → dsk_capas_ver + grafico_en_escena (1 s)
// Un tema cuyas herramientas no estén todas en el perfil se rechaza. Un tema
// que nadie mira no se consulta. Se consulta una vez por salida que lo mira (las
// herramientas de salida, Tool::porSalida, con la de esos aparatos).
//
// Auditoría: cada llamar (hecho, rechazado o a confirmar) queda en el registro
// (diag, componente «control-web») con el aparato, su nombre y la herramienta.
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ba::app::mcp {
class McpCore;
struct Tool;
} // namespace ba::app::mcp

namespace ba::app::control {

class ControlWeb : public QObject {
    Q_OBJECT
    /// [{id, nombre, perfil (texto: «todo» o las categorías), categorias, ip,
    ///   creado_ms, ultimo_uso_ms, conectado}] (sin hashes).
    Q_PROPERTY(QVariantList aparatos READ aparatosUi NOTIFY aparatosCambio)
    /// [{id, nombre, ip, perfil, ms}]: los que esperan a una persona.
    Q_PROPERTY(QVariantList solicitudes READ solicitudesUi NOTIFY solicitudesCambio)
    /// Cuántas conexiones vivas hay (de todos los aparatos).
    Q_PROPERTY(int conectados READ conectados NOTIFY conexionesCambio)
    /// El acceso desde afuera (por el túnel, con PIN) está habilitado.
    Q_PROPERTY(bool tunel READ tunel NOTIFY aparatosCambio)
    /// Las salidas para el selector de cada aparato: [{id, nombre, color, por_omision}].
    Q_PROPERTY(QVariantList salidas READ salidasUi NOTIFY aparatosCambio)

   public:
    using json = nlohmann::json;
    struct Ganchos {
        /// Alguien empieza (true) o deja (false) de mirar un tema: p. ej. con
        /// «audio», el Mezclador mide solo mientras tanto (así mezclador_estado no
        /// rehace sus listas en cada consulta).
        std::function<void(const std::string& tema, bool mirando)> mirando;
        /// Las salidas: [{id, nombre, color, por_omision}], la por omisión primera.
        std::function<json()> salidas;
    };
    struct Tema {
        std::string id;
        std::vector<std::string> herramientas;
        int periodoMs = 1000;
    };

    explicit ControlWeb(const std::filesystem::path& datos, QObject* parent = nullptr);
    ~ControlWeb() override;
    /// El servidor MCP (se arma después que el modelo). Sin él no corre nada.
    void setCore(mcp::McpCore* core);
    Ganchos& ganchos() { return g_; }

    // --- Aparatos ------------------------------------------------------------------
    json aparatos() const;
    json solicitudes() const;
    /// Un código de un uso (128 bits) con el perfil que se le propone a quien
    /// aprueba. {codigo, vence_ms, perfil} o {error}.
    json nuevoCodigo(const json& perfil, const std::string& nombre);
    /// El gateway canjeó un código: queda una solicitud que una persona aprueba.
    /// "" o el error (código inválido, vencido o ya usado; demasiados aparatos).
    std::string solicitud(const std::string& solicitud, const std::string& codigo, const std::string& nombre,
                          const std::string& ip);
    /// Aprueba (genera el token: {aprobada, aparato, token}) o rechaza. `perfil`
    /// null = el que propuso el código. Aprobar SOLO si `humano`: si no, {error}.
    json resolverSolicitud(const std::string& solicitud, bool aprobar, const json& perfil, bool humano);
    /// Cambia el perfil. Sin `humano` solo se puede achicar. "" o el error.
    /// `amplia`: si no se guardó porque lo ampliaba el asistente (lo decide una persona).
    std::string setPerfil(const std::string& aparato, const json& categorias, bool humano, bool* amplia = nullptr);
    /// La salida que maneja el aparato ("" = la por omisión, también si la suya ya no está).
    std::string salidaDe(const std::string& aparato) const;
    /// El aparato pasa a manejar `salida` (id o nombre; "" = la por omisión). No amplía
    /// nada: lo puede hacer el mismo aparato. "" o el error.
    std::string setSalida(const std::string& aparato, const std::string& salida);
    /// Cambiaron las salidas (crear, borrar, nombre, color): a los aparatos, si cambió algo.
    void salidasCambiaron();
    /// Saca un aparato: su token deja de valer y se cortan sus conexiones.
    bool quitar(const std::string& aparato);
    /// Habilita (con un PIN de 4 a 12 cifras) o apaga el acceso desde afuera.
    /// Habilitar solo si `humano`. "" o el error.
    std::string setTunel(bool habilitar, const std::string& pin, bool humano);
    bool tunel() const;
    /// {aparatos:[{id, hash, sal}], tunel:{habilitado, sal, hash_pin}} para el gateway.
    json credencialesParaGateway() const;
    /// El aparato de ese token ("" si ninguno). El gateway hace lo mismo con los hashes.
    std::string autenticar(const std::string& token) const;

    /// "*" o ["*"] o [categorías] → arreglo ordenado sin repetidos. null + error si no sirve.
    static json normalizarPerfil(const json& perfil, std::string* error = nullptr);
    /// ¿`despues` permite algo que `antes` no?
    static bool amplia(const json& antes, const json& despues);
    /// ¿El perfil deja usar esta herramienta? (nunca las de administrar el control web)
    static bool permite(const json& perfil, const std::string& categoria);
    static const std::vector<Tema>& temas();

    // --- Lo que llega del gateway -------------------------------------------------------
    /// Una conexión se autenticó (o se fue). `lugar`: "sala" | "afuera".
    void conexion(const std::string& conexion, const std::string& aparato, bool conectado, const std::string& ip,
                  const std::string& lugar);
    /// Un mensaje de esa conexión. `aparato` (lo dice el gateway) sirve para
    /// reconocer una conexión de antes de que la app arrancara de nuevo.
    void entrada(const std::string& conexion, const std::string& aparato, const json& msg);
    int conectados() const { return static_cast<int>(conns_.size()); }

    // --- Para la interfaz (QML): lo hace una persona ------------------------------------
    QVariantList aparatosUi() const;
    QVariantList solicitudesUi() const;
    QVariantList salidasUi() const;
    /// Aprobar con las categorías (texto: «todo», «*» o «escenas, audio»). "" o el error.
    Q_INVOKABLE QString aprobarUi(const QString& solicitud, bool aprobar, const QString& categorias);
    Q_INVOKABLE QString perfilUi(const QString& aparato, const QString& categorias);
    Q_INVOKABLE bool quitarUi(const QString& aparato);
    Q_INVOKABLE QString salidaUi(const QString& aparato, const QString& salida);

   signals:
    void aparatosCambio();
    void solicitudesCambio();
    void conexionesCambio();
    /// Cambió la lista de credenciales (sumar, quitar, el túnel): al gateway.
    void credencialesCambio();
    /// Una solicitud se resolvió: {solicitud, estado, aparato?, token?, error?} al gateway.
    void resuelta(const nlohmann::json& cuerpo);
    /// Un emparejamiento espera a una persona.
    void solicitudNueva(const QString& solicitud, const QString& nombre, const QString& ip);
    /// Hay que mandarle `msg` a esas conexiones (por el gateway).
    void enviar(const QStringList& conexiones, const nlohmann::json& msg);
    /// Hay que cortar esa conexión (aparato quitado o desconocido).
    void cortar(const QString& conexion, const QString& motivo);

   private:
    struct Conn {
        std::string aparato, ip, lugar;
        std::set<std::string> temas;
        std::deque<std::int64_t> llamadas; ///< para el límite por segundo
    };
    struct Codigo {
        json perfil;
        std::string nombre;
        std::int64_t venceMs = 0;
    };
    struct Solicitud {
        std::string nombre, ip;
        json perfil;
        std::int64_t ms = 0;
    };
    struct Sondeo { ///< un tema que alguien mira, en una salida (clave: claveSondeo)
        std::string tema, salida;
        std::unique_ptr<QTimer> timer;
        bool enCurso = false;
        std::int64_t desdeMs = 0;
        std::string ultimo; ///< lo último que se mandó (para mandar solo lo que cambió)
        json datos;
    };
    void cargar();
    void guardar() const;
    json* buscarAparato(const std::string& id);
    const json* buscarAparato(const std::string& id) const;
    void mandar(const std::string& conexion, const json& msg);
    void enviarAuth(const std::string& conexion);
    void llamar(const std::string& conexion, const json& msg);
    void preferencias(const std::string& cid, const json& msg);
    void mirar(const std::string& conexion, const json& msg);
    /// "" si el aparato puede mirar el tema; si no, por qué.
    std::string motivoTema(const json& perfil, const Tema& t) const;
    void reacomodarSondeos();
    void sondear(const std::string& clave);
    /// {type:"salida", salida, salidas} para el aparato.
    json mensajeSalida(const std::string& aparato) const;
    /// La salida pedida (id o nombre) si existe; "" si no.
    std::string salidaExistente(const std::string& salida) const;
    void auditar(const std::string& aparato, const std::string& texto) const;

    std::filesystem::path archivo_;
    Ganchos g_;
    mcp::McpCore* core_ = nullptr;
    json datos_;
    std::map<std::string, Conn> conns_;
    std::map<std::string, Codigo> codigos_;
    std::map<std::string, Solicitud> solicitudes_;
    std::map<std::string, Sondeo> sondeos_;
    std::string ultimasSalidas_; ///< la lista que vieron los aparatos (salidasCambiaron)
};

} // namespace ba::app::control
