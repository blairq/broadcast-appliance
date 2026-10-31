// ============================================================================
// src/app/mcp/McpCore.hpp — Nucleo del servidor MCP (Model Context Protocol)
//
// Proposito:
//   Que un asistente de IA (un LLM con vision) pueda operar TODO el appliance:
//   escenas, fuentes, audio, salidas, configuracion, sala, oradores, ventanas,
//   y VER lo que pasa (programa, cada fuente, cada persona, la propia app).
//
//   Este archivo es el protocolo, sin Qt ni transporte: JSON-RPC 2.0 segun la
//   especificacion MCP (2025-06-18, compatible con 2025-03-26 y 2024-11-05):
//     initialize / ping / tools/list / tools/call / resources/list /
//     resources/templates/list / resources/read / prompts/list / prompts/get /
//     logging/setLevel / completion/complete (vacio).
//   El transporte (HTTP en 127.0.0.1 con token) esta en McpHttp; el puente
//   stdio para clientes que lanzan procesos, en el binario ba-mcp.
//
// Sistema de ayuda propio:
//   - Herramienta `help` (sin argumentos: guia general y catalogo por
//     categoria; `topic`: un tema; `tool`: la ficha completa de una
//     herramienta con su esquema y ejemplos) y `help_search` (busqueda por
//     palabras en herramientas y temas).
//   - Cada tema es tambien un recurso `ba://ayuda/<tema>` y cada herramienta
//     `ba://ayuda/herramienta/<nombre>`.
//   - Las `instructions` del initialize le dicen al cliente por donde empezar.
//
// Errores: un argumento invalido o una falla de la app vuelven como resultado
// con isError=true y un texto que explica que paso y como seguir (el modelo
// puede corregirse solo). Los errores de protocolo, como error JSON-RPC.
//
// Threading (F3-03): seguro entre hilos. El transporte HTTP vive en el hilo del
// plano de control (arranque/PlanoControl): con la interfaz trabada, el servidor
// sigue contestando. Cada herramienta corre en el hilo que DECLARA (Tool::hilo,
// ver Ejecutores): la de la interfaz, en la interfaz y con tope (si no la toma a
// tiempo, se contesta «ocupada» y NO se hace después); la de control, en la cola
// del control; la de la foto, en el pool. La validación, las guardas y la
// aprobación corren en el hilo de la herramienta (así leen lo que ella lee); la
// tarjeta de aprobación y el guardado posterior, en la interfaz. `done` vuelve en el
// hilo del que llamó. Sin ejecutores (los tests, la consola), todo en el acto, como
// antes. Las herramientas pueden terminar DESPUES (asincronas): llaman `done`
// cuando tienen el resultado.
// ============================================================================
#pragma once

#include <memory>

#include "mcp/McpIndice.hpp"

#include <atomic>
#include <deque>
#include <mutex>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mcp/McpAire.hpp"

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

using json = nlohmann::json;

/// Resultado de una herramienta (contenido MCP: texto, imagenes, JSON).
struct ToolResult {
    json content = json::array();
    json structured; ///< structuredContent (opcional)
    bool isError = false;
    /// La respuesta trae contenido escrito por invitados o publico (chat,
    /// nombres, archivos, video de invitados): contamina la sesion (ver McpCore).
    bool untrusted = false;

    ToolResult& text(const std::string& t);
    /// Imagen en base64 (`mime`: image/png | image/jpeg).
    ToolResult& image(const std::string& base64, const std::string& mime);
    /// Agrega `data` como JSON estructurado y como texto legible.
    ToolResult& data(const json& data);
    static ToolResult error(const std::string& message);
    static ToolResult ok(const std::string& message);
};

using Done = std::function<void(ToolResult)>;

/// Qué hace una herramienta: lo DECLARA cada una al registrarse (F3-02, el
/// registro declarativo). De acá salen `readOnly` (lo pone addTool), la guarda de
/// aire (McpAire), el hilo por omisión y la columna `clase` de docs/mcp.json.
///
/// POR QUE DECLARADA Y NO DEDUCIDA. Se deducía: «de aire» era estar en una lista
/// de McpAire.cpp, «lectura» era readOnly, y nada decía cuáles solo mueven la
/// interfaz. Una herramienta nueva que cortaba el aire y nadie sumaba a la lista
/// quedaba como escritura común, sin confirmación en vivo, y no lo notaba nadie.
/// Con la clase al lado del nombre, el que la escribe tiene que decidirlo, y la
/// prueba del inventario (tools/docs/mcp-inventario.py --verificar) no deja
/// registrar una sin declarar.
enum class Clase {
    SinDeclarar, ///< solo las armadas en ejecución (pruebas): addTool la deduce de readOnly
    Lectura,     ///< solo mira: no cambia el estado (ni la foto que leen los demás)
    Escritura,   ///< cambia el estado de la app, no lo que sale
    Aire,        ///< puede cambiar lo que sale: ver porQueAire, aireSi y McpAire
    Ui,          ///< mueve la interfaz del operador (páginas, ventanas, avisos)
};
/// En qué hilo corre (F3-03: el despacho lo respeta, ver McpCore::Ejecutores).
///
/// POR QUE LA INTERFAZ ES LO DE FÁBRICA. La clase dice qué hace una herramienta, no
/// qué toca para hacerlo: casi todas leen o mueven objetos de la interfaz o el motor
/// (que todavía es del hilo de la interfaz), y correrlas en otro hilo sería una
/// carrera. Así que corren en la interfaz (con tope) hasta que cada una declare otro
/// hilo, y lo declara la que lee SOLO cosas seguras entre hilos (el registro, las
/// alarmas, el documento de aire, las métricas, los archivos).
enum class Hilo {
    SegunClase, ///< addTool lo resuelve: la interfaz (ver arriba)
    Interfaz,   ///< toca objetos de la interfaz o del motor; con tope si llama otro hilo
    Control,    ///< la cola del plano de control (en orden; no espera a la interfaz)
    Foto,       ///< solo lee cosas seguras entre hilos: un hilo del pool
};
/// "lectura" | "escritura" | "aire" | "ui" (los nombres de mcp.json; no se traducen).
const char* nombreClase(Clase c);
/// "interfaz" | "control" | "foto".
const char* nombreHilo(Hilo h);

struct Tool {
    std::string name;
    std::string title;
    std::string category; ///< id de categoria (ver addCategory)
    std::string description;
    std::string details; ///< explicacion larga para `help tool=...`
    json inputSchema = json{{"type", "object"}, {"properties", json::object()}};
    std::vector<std::string> examples; ///< argumentos de ejemplo (JSON) con comentario
    /// La clase declarada (ver Clase). Toda herramienta registrada en src/app/mcp la
    /// declara con un literal: `t.clase = Clase::Lectura;`.
    Clase clase = Clase::SinDeclarar;
    Hilo hilo = Hilo::SegunClase;
    /// Clase::Aire: por qué cambia lo que sale, marcado con QT_TRANSLATE_NOOP("mcp", …)
    /// (se traduce al mostrarlo en mcp_aire_lista).
    const char* porQueAire = nullptr;
    /// Clase::Aire: con qué argumentos cuenta (de fábrica, siempre).
    AireSi aireSi = AireSi::Siempre;
    /// En vivo, la voz y un disparador la piden sin confirmar: es reversible al
    /// toque (mute, volumen, prompter, parar una cámara).
    bool reversible = false;
    /// Lo pone addTool desde `clase` (clase == Lectura). Se asigna a mano solo en una
    /// herramienta armada en ejecución, sin clase (Clase::SinDeclarar).
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
    bool openWorld = false;
    /// Siempre devuelve contenido de invitados/publico (contamina la sesion).
    bool untrustedOutput = false;
    /// Su efecto lo decide una persona con un click (preguntar al operador,
    /// sugerir respuestas): no necesita aprobacion aunque la sesion este contaminada.
    bool mediated = false;
    /// Nombres que tuvo antes y siguen andando (modelos entrenados, botoneras, atajos y
    /// guiones guardados con el viejo): no se listan, pero una llamada con uno de ellos
    /// es la misma llamada con `name` (callTool lo cambia al empezar).
    std::vector<std::string> antes;
    /// Apunta a una salida (PLAN-SALIDAS): acepta `salida` (id o nombre; sin ella, la
    /// del que llama o la por omisión). addTool le suma la propiedad al esquema;
    /// callTool la valida (error claro si no existe) y la usa para la guarda de
    /// aire: cuenta si sale ESA salida. La herramienta la lee con McpCore::salidaDe.
    bool porSalida = false;
    std::function<void(const json& args, Done done)> run;
};

/// Quien llama (para la contaminacion por sesion). La sesion la pone el
/// cliente MCP (cabecera Mcp-Session-Id), no el modelo.
///
/// El ROL lo declara el cliente al conectarse (cabecera X-BA-Rol; el puente
/// la manda con `ba-mcp --rol <rol>`). Con rol, el cliente ve SOLO el juego de
/// herramientas de ese rol (ver setRoles). Sin rol, ve todo: el asistente
/// interactivo y los clientes externos siguen como estaban.
struct CallContext {
    std::string session;
    std::string rol;
    /// Quién pide: asistente, plugin, receta, disparador, voz, botonera… (para
    /// las guardas de aire y el registro).
    std::string origen;
    /// Una persona apretó algo (p. ej. un botón de la botonera): no pasa por la
    /// guarda de aire (ya decidió un humano).
    bool humano = false;
    /// Herramientas que este pedido puede usar aunque su rol no las tenga (un
    /// disparador con `permitir_aire`).
    std::set<std::string> permitidas;
    /// Qué asistente de la app llama (evento | automatizacion; cabecera X-BA-Asistente).
    /// Al final: hay inicializaciones por posición de los campos de arriba.
    std::string asistente;
    /// La salida por omisión de quien llama (PLAN-SALIDAS): la de su perfil, su
    /// aparato o su asistente. Vacía = la por omisión del aparato. Una herramienta
    /// de salida (Tool::porSalida) usa `salida` de los argumentos si vino; si no,
    /// esta; si no, la por omisión (ver McpCore::salidaDe). Nadie la mete en los args.
    std::string salida;
    /// El id de la orden (diag/Correlacion.hpp, F4-02). Vacío: lo pone el núcleo al
    /// atenderla (m-, b- de la botonera, w- del control web).
    std::string correlacion;
};

/// Politica de aprobacion de acciones que modifican algo.
enum class ApprovalPolicy {
    Untrusted, ///< (defecto) aprobacion humana solo si la sesion recibio contenido de invitados/publico
    Always,    ///< toda accion que modifica necesita aprobacion
    Never      ///< nunca (la persona decidio confiar)
};

struct HelpTopic {
    std::string id;
    std::string title;
    std::string summary;
    std::string body; ///< markdown
};

struct Resource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mimeType = "text/markdown";
    std::function<std::string()> read;
};

struct Prompt {
    std::string name;
    std::string title;
    std::string description;
    json arguments = json::array(); ///< [{name, description, required}]
    /// Devuelve el texto del mensaje de usuario con los argumentos aplicados.
    std::function<std::string(const json& args)> render;
};

class McpCore {
   public:
    McpCore();

    void setServerInfo(std::string name, std::string version, std::string instructions);
    /// Le pega texto a las instrucciones que van en el `initialize`.
    void addInstructions(const std::string& mas);

    /// Un renglon por categoria: id, titulo, cuantas herramientas y para que
    /// sirve.
    ///
    /// POR QUE VA SIEMPRE. El listado de herramientas cuesta ~32.000 tokens EN
    /// CADA PEDIDO, asi que tarde o temprano hay que mandar menos. Pero mandar
    /// menos SIN un indice de lo que existe es peor que mandar todo: el modelo
    /// concluye «esto no se puede hacer» cuando la herramienta esta a una
    /// categoria de distancia. El mapa cuesta unos 400 tokens y hace que eso
    /// no pueda pasar.
    ///
    /// Con `rol`, el mapa es el de ESE rol: sus categorias y las comunes. Lo
    /// que no ve, no aparece, porque un agente automatico no tiene a quien
    /// pedirle «la otra categoria».
    std::string mapaDeCategorias(const std::string& rol = {}) const;
    void addCategory(const std::string& id, const std::string& title, const std::string& summary);
    void addTool(Tool tool);
    void addTopic(HelpTopic topic);
    void addResource(Resource resource);
    void addPrompt(Prompt prompt);

    /// Un mensaje JSON-RPC (objeto o arreglo). `reply` recibe la respuesta
    /// (objeto, arreglo, o null si eran solo notificaciones). Puede llamarse
    /// despues (herramientas asincronas), una sola vez.
    void handle(const json& message, std::function<void(json)> reply, const CallContext& ctx = {});

    // --- Salvaguarda determinista contra instrucciones inyectadas ----------------
    // Una sesion que recibio contenido de invitados o del publico queda
    // "contaminada" hasta que termina (cliente nuevo = sesion nueva). En una
    // sesion contaminada, NINGUNA herramienta que modifica algo se ejecuta sin
    // que una persona la apruebe con un click (la tarjeta la arma el servidor:
    // herramienta y argumentos exactos). No depende de lo que diga el modelo.
    using Approver = std::function<void(const Tool& tool, const json& args, const std::string& reason,
                                        std::function<void(bool approved)> decide)>;
    void setApprover(Approver fn) { approver_ = std::move(fn); }
    /// Que una persona apruebe algo que una herramienta solo hace con permiso
    /// explícito (p. ej. ampliar el perfil de un aparato del control web): la
    /// misma tarjeta que la salvaguarda. Sin a quién preguntar, no; en modo
    /// prueba (--test), sí (y queda en el registro).
    void pedirAprobacion(const std::string& herramienta, const json& args, const std::string& motivo,
                         std::function<void(bool approved)> decide);

    /// --- Guardas de aire (McpAire) --------------------------------------------
    /// Mientras `hayAire()` (transmitiendo, grabando o la sesión en vivo), una
    /// herramienta de aire pedida por algo automático espera SIEMPRE a una
    /// persona; y la voz y los disparadores solo leen o hacen lo reversible.
    void setHayAire(std::function<bool()> fn) { hayAire_ = std::move(fn); }
    bool hayAire() const { return hayAire_ && hayAire_(); }
    /// ¿Esta llamada cambia el aire AHORA? Una herramienta de aire (McpAire) y, si es de
    /// salida, que salga la que pide (`salida` de los args, o `salidaDeQuien`: la del
    /// aparato o el perfil); si no, que salga cualquiera. Lo mismo que mira callTool:
    /// la botonera lo usa para pedir confirmación en el aparato.
    bool cambiaElAire(const std::string& herramienta, const json& args, const std::string& salidaDeQuien) const;
    McpAire& aire() { return aire_; }

    /// --- Las salidas (PLAN-SALIDAS, etapa 3) -----------------------------------
    /// `lista`: [{id, nombre, por_omision}] (la por omisión primera); `sale(id)`: esa
    /// salida transmite o graba (para la guarda de aire de las herramientas de salida).
    void setSalidas(std::function<json()> lista, std::function<bool(const std::string& id)> sale) {
        salidasLista_ = std::move(lista);
        salidaSale_ = std::move(sale);
    }
    /// El id de la salida pedida (id o nombre, sin mayúsculas); "" = la por omisión.
    /// Si no existe, "" y `err` con las que hay. Sin setSalidas, devuelve lo pedido.
    std::string resolverSalida(const std::string& pedida, std::string& err) const;
    /// Lo que pidió una llamada: `salida` de los argumentos si vino (texto no vacío);
    /// si no, la del contexto (CallContext::salida); si no, "" (la por omisión).
    static std::string salidaPedida(const json& args, const CallContext& ctx);
    /// La salida de la llamada EN CURSO, resuelta a su id ("" = la por omisión).
    /// Para el `run` de una herramienta de salida (callTool ya la validó): leerla al
    /// empezar, que el contexto en curso se limpia cuando `run` vuelve.
    std::string salidaDe(const json& args) const;

    /// Lo que hay que hacer DESPUES de cada herramienta que modifica algo:
    /// persistir y avisarle a la interfaz.
    ///
    /// POR QUE ESTA ACA Y NO EN CADA HERRAMIENTA. Estaba en cada una, como
    /// convencion: el que escribia una herramienta que modificaba algo tenia
    /// que acordarse de llamar a `persist()`. De veintisiete mutadores, doce se
    /// acordaban. Las otras cambiaban el motor por abajo y la interfaz seguia
    /// mostrando lo anterior hasta que algo la despertara, que es lo peor que
    /// puede pasar: dos verdades a la vez y ninguna senal de que difieren.
    ///
    /// Puesto en el despachador, la invariante deja de depender de la memoria
    /// de nadie: TODA herramienta que no sea de solo lectura refresca las
    /// vistas, y una superficie nueva la hereda sin hacer nada.
    void setSincronizador(std::function<void()> fn) { sincronizar_ = std::move(fn); }
    void setApprovalPolicy(ApprovalPolicy p) { policy_.store(p); }
    /// Modo prueba (`ba-app --test` / `--debug`): NADA espera aprobación de una persona
    /// (ni la de contenido no confiable ni la guarda de aire). Cada acción que la
    /// habría pedido queda en el registro. Solo por línea de comandos; no se guarda.
    void setSinAprobaciones(bool on) { sinAprobaciones_ = on; }
    bool sinAprobaciones() const { return sinAprobaciones_; }
    ApprovalPolicy approvalPolicy() const { return policy_.load(); }
    /// Marca la sesion como contaminada (p. ej. al leer un recurso de la sala).
    void taint(const std::string& session, const std::string& source);

    /// Atencion delegada de alguien del lobby. El operador la habilita con un
    /// click en su tarjeta: mientras dura, el asistente puede hablarle,
    /// completar su ficha y hacerla pasar SIN pedir aprobacion paso a paso
    /// (seguiria pidiendola por cada mensaje, que es lo que la hace inusable).
    /// La salvaguarda general no se toca: la exencion vale SOLO para esa
    /// persona y para esas herramientas.
    void startIntake(const std::string& id, const std::string& name);
    /// Como saber si una llamada apunta a una persona atendida: resuelve el
    /// nombre o id de los argumentos contra la sala (los nombres cambian
    /// cuando el asistente completa la ficha).
    void setIntakeMatcher(std::function<bool(const std::string& tool, const nlohmann::json& args)> fn) {
        intakeMatch_ = std::move(fn);
    }
    void endIntake(const std::string& id);
    std::vector<std::string> intakes() const;
    bool tainted(const std::string& session) const;
    /// {session, tainted, source, atMs}
    json taintInfo(const std::string& session) const;
    /// true si la herramienta modifica algo (ni solo lectura ni mediada por una persona).
    static bool mutating(const Tool& t) { return !t.readOnly && !t.mediated; }

    // --- Ayuda (tambien las usa la herramienta `help`) ------------------------
    std::string helpOverview() const;
    std::string helpTopic(const std::string& id) const;
    std::string helpTool(const std::string& name) const;
    /// Coincidencias por palabras: [{kind, id, title, score}].
    /// Las mejores coincidencias para esa consulta, por el indice invertido
    /// (ver McpIndice.hpp). Cada una trae `porque`: los terminos que
    /// engancharon, para poder corregir la busqueda en vez de adivinar.
    /// Busca en el indice. Con `rol`, SOLO lo que ese rol puede llamar: ver el
    /// comentario en el .cpp —contestarle a un rol herramientas que le van a
    /// rebotar le gasta el lugar a las que si puede usar—.
    json search(const std::string& query, const std::string& rol = {}) const;
    /// Solo herramientas, con el mismo índice y hasta `tope`: [{nombre, puntaje, porque}].
    /// Lo usa el selector de la Botonera (que busca igual que el asistente).
    json buscarHerramientas(const std::string& query, std::size_t tope = 80) const;

    /// El registro. Una deque: lo que se registra después del arranque (las extensiones)
    /// no mueve a las demás, y un puntero de findTool sigue valiendo. Recorrerla desde
    /// afuera, solo en el hilo que registra (la interfaz).
    const std::deque<Tool>& tools() const { return tools_; }
    /// (id, titulo) de cada categoria, en orden.
    std::vector<std::pair<std::string, std::string>> categoryTitles() const {
        const std::lock_guard<std::recursive_mutex> registro(registroMu_);
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& c : categories_)
            out.emplace_back(c.id, c.title);
        return out;
    }
    const Tool* findTool(const std::string& name) const;
    /// El nombre actual de una herramienta (el mismo si no es uno viejo, ver Tool::antes).
    std::string nombreActual(const std::string& name) const;
    /// Suma `viejo` a Tool::antes de la herramienta `actual` (la tabla de renombres
    /// de McpRenombres.cpp, despues de registrarlas todas). false si no existe.
    bool agregarNombreAnterior(const std::string& actual, const std::string& viejo);
    /// El catálogo para elegir una herramienta a mano (el selector de la
    /// Botonera): [{id, titulo, resumen, herramientas: [{nombre, titulo,
    /// descripcion, etiquetas, ejemplo, solo_lectura, destructiva}]}], en el orden
    /// de las categorías; las sin categoría van al final, en «Otras».
    json catalogo() const;
    /// Para ajustar herramientas ya registradas (marcas de seguridad).
    void forEachTool(const std::function<void(Tool&)>& fn) {
        const std::lock_guard<std::recursive_mutex> registro(registroMu_);
        for (Tool& t : tools_)
            fn(t);
    }
    // --- Roles: un juego de herramientas por identidad ---------------------------
    // Un cliente que se presenta con un rol recibe en tools/list SOLO las
    // herramientas de sus categorias y de las comunes, y llamar a otra se le
    // niega con un mensaje que lo explica. Es para que un agente automatico
    // (recepcion, tecnico...) no pueda tocar por accidente lo que no es suyo,
    // y para que su contexto sea chico: cada herramienta que no ve son tokens
    // que no paga en cada pedido.
    //
    // NO es una frontera de seguridad: el token del servidor ya da acceso a
    // todo, y un proceso que lo tiene puede presentarse con el rol que quiera.
    // Es una barandilla contra el accidente, no contra el ataque.
    /// `categoriasPorRol`: rol -> categorias que ve. `comunes`: las que ven
    /// todos los roles (ayuda, estado...). Un rol que no esta en el mapa ve
    /// solo las comunes: un rol desconocido es un rol sin permisos, no uno
    /// con todos.
    void setRoles(std::map<std::string, std::set<std::string>> categoriasPorRol,
                  std::set<std::string> comunes);    /// Avisa si algun juego nombra una categoria que no usa ninguna
    /// herramienta. Llamar CUANDO YA ESTAN TODAS REGISTRADAS: antes da falsos
    /// positivos de las que se registran mas tarde.
    void verificarJuegos() const;
    /// Si el rol puede ver y llamar a la herramienta. Sin rol, siempre.
    bool permitida(const Tool& t, const std::string& rol) const;
    bool hayRoles() const { return !roles_.empty(); }

    /// El contexto de la llamada en curso: para herramientas que necesitan
    /// saber QUIEN las llama (agente_encargar cuenta la profundidad por rol).
    /// Vale solo mientras `run` no devolvio, en el hilo en que corre; una herramienta
    /// asincrona lo lee a la entrada y se lo guarda.
    const CallContext& contextoEnCurso() const;

    /// --- Dónde corre cada cosa (F3-03) -----------------------------------------
    using Tarea = std::function<void()>;
    struct Ejecutores {
        std::function<void(Tarea)> interfaz;          ///< encola en el hilo de la interfaz
        std::function<void(Tarea)> control;           ///< encola en el hilo de control
        std::function<void(Tarea)> foto;              ///< a un hilo del pool
        std::function<void(int ms, Tarea)> despues;   ///< en el hilo de control, dentro de `ms`
        std::function<bool()> enInterfaz;             ///< ¿el que llama es el hilo de la interfaz?
        std::function<bool()> enControl;              ///< ¿y el de control?
        /// Cuánto se espera a que la interfaz tome algo pedido desde otro hilo. Pasado,
        /// se contesta que está ocupada y lo pedido no se hace (nunca tarde).
        int topeInterfazMs = 5000;
    };
    void setEjecutores(Ejecutores e);
    /// Lo que se manda junto con «la interfaz está ocupada» (structuredContent.foto): lo
    /// que se sabe sin ella (el documento de aire, el latido). Desde cualquier hilo.
    void setFotoDeEmergencia(std::function<json()> f) { fotoEmergencia_ = std::move(f); }
    /// Cuántas veces la interfaz no tomó algo a tiempo (también ba_mcp_interfaz_ocupada_total).
    std::uint64_t vencidas() const { return vencidas_.load(); }

    /// Valida `args` contra el esquema (requeridos, tipos, enums, rangos).
    /// :returns: vacio si esta bien, o el problema explicado.
    static std::string validate(const json& schema, const json& args);

    static constexpr const char* kLatestProtocol = "2025-06-18";

    /// Ejecuta una herramienta como si la hubiera pedido el cliente: valida los
    /// argumentos, pide aprobacion si la politica lo exige, refresca la
    /// interfaz al terminar. Es publica porque las RECETAS (una serie de pasos
    /// guardada) tienen que pasar por el MISMO camino: si tuvieran uno propio,
    /// serian una puerta de atras que se saltea la validacion y la aprobacion.
    void callTool(const json& params, std::function<void(json result)> done, const CallContext& ctx);

   private:
    void handleOne(const json& msg, std::function<void(json)> reply, const CallContext& ctx);
    /// callTool desde que se sabe qué herramienta es: en el hilo de ella.
    /// La guarda de aire de `tool` con la salida ya resuelta: si es de salida, que salga
    /// ESA (otra transmitiendo no la vuelve de aire); si no, que salga cualquiera.
    bool hayAireEn(const Tool& tool, const std::string& salida) const;
    void procesar(const Tool* tool, const std::string& name, const json& args, std::function<void(json)> done,
                  const CallContext& ctx);
    /// Dónde está el que llama (para devolverle `done` ahí).
    enum class Lugar { Otro, Interfaz, Control };
    Lugar lugarActual() const;
    /// `done` en el hilo `donde` (en el acto si ya está ahí o no hay ejecutores).
    void volverA(Lugar donde, Tarea t) const;
    /// Corre `fn` en el hilo `h`; `vencio` si la interfaz no la tomó a tiempo.
    void correrEn(Hilo h, Tarea fn, Tarea vencio);
    /// En la interfaz con tope: `fn` solo si la toma antes del tope; si no, `vencio`.
    void enInterfazConTope(Tarea fn, Tarea vencio);
    /// El resultado de «la interfaz está ocupada» para `que`.
    json ocupada(const std::string& que) const;
    json initializeResult(const json& params, const CallContext& ctx) const;
    json toolListEntry(const Tool& t) const;
    json resourceList() const;
    json readResource(const std::string& uri, std::string& error) const;

    struct Category {
        std::string id, title, summary;
    };
    std::string name_ = "broadcast-appliance";
    std::string version_ = "1.0";
    std::string instructions_;
    std::vector<Category> categories_;
    std::deque<Tool> tools_;
    /// El registro (herramientas, categorías, temas, recursos, prompts, roles): lo
    /// escribe la interfaz al registrar y lo leen todos los hilos. Recursivo: las
    /// lecturas se llaman entre sí (search → permitida).
    mutable std::recursive_mutex registroMu_;
    /// La salvaguarda y los clientes (taint_, clientes_, intake_).
    mutable std::mutex estadoMu_;
    Ejecutores ej_;
    bool conEjecutores_ = false;
    std::function<json()> fotoEmergencia_;
    std::atomic<std::uint64_t> vencidas_{0};
    std::vector<HelpTopic> topics_;
    /// El indice de busqueda: se arma solo la primera vez que se busca, y de
    /// nuevo si aparecieron herramientas (las extensiones se registran
    /// despues del arranque). `mutable` porque `search` es const: buscar no
    /// cambia nada de lo que se ve desde afuera.
    mutable std::unique_ptr<Indice> indice_;
    mutable std::size_t indiceDe_ = 0;
    void armarIndice() const;
    std::vector<Resource> resources_;
    std::vector<Prompt> prompts_;
    Approver approver_;
    std::function<bool()> hayAire_;
    std::function<json()> salidasLista_;
    std::function<bool(const std::string&)> salidaSale_;
    McpAire aire_{*this};
    std::function<void()> sincronizar_;
    std::atomic<ApprovalPolicy> policy_{ApprovalPolicy::Untrusted};
    std::atomic<bool> sinAprobaciones_{false};
    struct Taint {
        std::string source;
        std::int64_t atMs = 0;
    };
    std::map<std::string, Taint> taint_;
    /// Sesión → quién es el cliente (el `clientInfo` que manda al conectarse: «claude-code
    /// 2.1», «chatgpt»…). Para el registro: sin esto, una llamada de afuera no decía de quién
    /// era (2026-10-20: dos cambios de escena en la PC de pruebas que nadie supo explicar).
    std::map<std::string, std::string> clientes_;
    /// id -> nombre de las personas cuya atencion delego el operador.
    std::map<std::string, std::string> intake_;
    bool intakeAllows(const Tool& t, const nlohmann::json& args) const;
    std::function<bool(const std::string&, const nlohmann::json&)> intakeMatch_;
    std::map<std::string, std::set<std::string>> roles_;
    std::set<std::string> comunes_;
};

} // namespace ba::app::mcp
