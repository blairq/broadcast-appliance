// ============================================================================
// src/app/evento/Linea.hpp — la línea de tiempo del evento (modelo v2)
//
// El plan entero está en docs/planes/PLAN-LINEA-DE-TIEMPO.md (3.2, 3.3, 3.4,
// 3.8 y 3.9). Esto es el paso L1: el MODELO y el RESOLVEDOR, en C++ puro.
//
// QUÉ CAMBIA CONTRA EL GUION v1 (Evento.hpp). Ahí un cue hacía una sola cosa y
// todo iba en fila: «cámara 2 + zócalo + bajar la música» eran tres cues. Acá la
// unidad que entra con un GO es el PASO, y adentro lleva PIEZAS en PISTAS, con
// su tiempo relativo al paso (el zócalo a los 5 s, la música que sigue hasta el
// final del bloque). Es el modelo de Sofie, Mosart e Ignite.
//
//     Linea
//      ├─ salidas[]     "principal", "sala", "web-en"
//      ├─ pistas[]      escena, graficos, audio… (por salida o globales)
//      ├─ bloques[] → pasos[] → piezas[]
//      ├─ estante[]     adlibs: piezas o pasos sueltos, con tecla y botón
//      ├─ disparadores[]
//      └─ plantillas[]  pasos-modelo ("entrevista", "votación")
//
// EL RESOLVEDOR. Una función pura (como resolveTimeline() de SuperFly): entra el
// modelo, lo que ya se emitió (instancias), el `ahora`, las condiciones que se
// conocen y los holds; sale, por salida, el PRONÓSTICO de cada paso (front, end,
// back time, estado), el over/under, los hard starts con su gap u overlap, las
// piezas con su tiempo absoluto, lo que va a entrar solo y los conflictos entre
// salidas. La interfaz, el MCP, el ensayo, el ejecutor y la retoma tras un corte
// usan el MISMO cálculo: así no hay dos opiniones sobre qué va a pasar.
//
// Nada de reloj de verdad: el `ahora` entra como número (ms de época). Las horas
// del día ("19:30") se pasan a ms con `medianocheMs`, que calcula quien llama con
// el huso del evento (acá no hay base de husos).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {
struct Evento;
struct Cue;
enum class Tipo;
}

namespace ba::evento::linea {

/// Lo que nunca termina (una pieza «hasta que se saque»).
constexpr int64_t kNunca = std::numeric_limits<int64_t>::max();

/// Una expresión chica sobre lo que ya existe ("bus:votacion/abierta",
/// "medio:<pieza>.termino", "hora>=19:30"). Acá es solo un texto: quien la evalúa
/// (L7) le dice al resolvedor desde cuándo se cumple o deja de cumplirse.
using Condicion = std::string;

// --- Cómo entra un paso -------------------------------------------------------

enum class ModoInicio {
    Go,     ///< espera el GO de la persona que opera (por defecto)
    Tras,   ///< auto-follow: cuando termina `de`
    Con,    ///< auto-continue: cuando arranca `de`, más `masMs`
    Hora,   ///< hard start: a la hora del día, según la `politica`
    Cuando, ///< lo dispara algo de afuera (`condicion`)
    Ancla,  ///< sincroniza salidas: con el paso `paso` de la salida `salida`
};

/// Qué hace un hard start cuando llega su hora.
enum class Politica {
    Avisar,  ///< cuenta regresiva y gap/overlap; el GO lo da la persona
    Esperar, ///< si se llega antes, queda en hold hasta la hora
    Cortar,  ///< a la hora entra pase lo que pase (cadena nacional, satélite)
};

struct Inicio {
    ModoInicio modo = ModoInicio::Go;
    std::string de = "anterior"; ///< tras/con: "anterior" o el id de un paso de la misma salida
    int64_t masMs = 0;           ///< con, ancla
    std::string hora;            ///< hora: "HH:MM" en el huso del evento
    Politica politica = Politica::Avisar;
    Condicion condicion;         ///< cuando
    std::string salida, paso;    ///< ancla

    nlohmann::json aJson() const;
    static Inicio deJson(const nlohmann::json& j);
};

// --- Cómo termina un paso -----------------------------------------------------

enum class ModoFin {
    Duracion,   ///< {"duracion":N}: N segundos (0 = la duración prevista)
    AlTerminar, ///< {"alTerminar":"medio"}: lo que dure el medio de su pieza principal
    Abierto,    ///< {"abierto":true}: no se sabe; cuenta la prevista y nunca avanza solo
    Mientras,   ///< {"mientras":C}: mientras la condición se cumpla
};

struct Fin {
    ModoFin modo = ModoFin::Duracion;
    int duracionSeg = 0;
    std::string alTerminar = "medio";
    Condicion mientras;

    nlohmann::json aJson() const;
    static Fin deJson(const nlohmann::json& j);
};

/// Qué hace el paso con el que sigue (el `automatico` del v1, ordenado).
enum class Continuar {
    No,    ///< el siguiente espera su propio inicio
    Sigue, ///< auto-follow: el siguiente entra al terminar este (más la post espera)
    Con,   ///< auto-continue: el siguiente entra al arrancar este (más la post espera)
};

// --- Piezas ---------------------------------------------------------------------

/// Cuándo arranca una pieza, relativo a su paso (como el secundario de Mosart).
struct Ancla {
    enum class Modo { Ms, AntesDelFin, Manual, Cuando };
    Modo modo = Modo::Ms;
    /// Ms: desde que arranca el paso; AntesDelFin: antes de que termine; Manual:
    /// dónde la pone el plan (para la escaleta y los tiempos), aunque no entra sola.
    int64_t ms = 0;
    Condicion cuando;

    nlohmann::json aJson() const;
    static Ancla deJson(const nlohmann::json& j);
};

/// Cuánto dura una pieza. `Vida` = hasta que termine su vida.
struct Dura {
    enum class Modo { Vida, Ms, AlTerminar };
    Modo modo = Modo::Vida;
    int64_t ms = 0;
    std::string alTerminar = "medio";

    nlohmann::json aJson() const;
    static Dura deJson(const nlohmann::json& j);
};

/// Hasta dónde vive una pieza (el lifespan de Sofie, los modos de salida de Mosart).
enum class Vida {
    Paso,            ///< mientras esté su paso (por defecto)
    Bloque,          ///< hasta que termine el último paso de su bloque
    HastaQueSeSaque, ///< hasta que otra la reemplace o alguien la saque
    Evento,          ///< hasta el fin del evento
    AlAvanzar,       ///< el `seVa: al-avanzar` del v1: para el cálculo, igual que Paso
};

/// Qué hacer con una pieza que debería estar activa y no se ejecutó (al saltar
/// pasos o al volver de un corte).
enum class SiSePerdio { Ejecutar, Saltar, Preguntar };

struct Pieza {
    std::string id;
    std::string pista;
    std::string tipo; ///< escena, grafico, musica, video, ptz, asistente, asamblea…
    std::string texto; ///< lo que lee el operador (el `texto` del cue v1)
    nlohmann::json datos = nlohmann::json::object();
    Ancla desde;
    Dura dura;
    Vida vida = Vida::Paso;
    int prioridad = 0;
    /// Vacío = el de siempre (ver `siSePerdioEfectivo`).
    std::optional<SiSePerdio> siSePerdio;

    /// Por defecto: `preguntar` para las del asistente (una acción MCP perdida
    /// puede no tener sentido ya), `ejecutar` para las de vida larga (se reponen:
    /// es el estado que tiene que tener el aire) y `saltar` para las de su paso.
    SiSePerdio siSePerdioEfectivo() const;

    nlohmann::json aJson() const;
    static Pieza deJson(const nlohmann::json& j);
};

// --- Pasos, bloques y el resto ----------------------------------------------------

/// Lo que entra con un TAKE (la Part de Sofie).
struct Paso {
    std::string id;
    std::string nombre;
    std::string salida; ///< vacío = la primera salida; "*" = un momento compartido por todas
    std::string guion;  ///< texto para el prompter o la conducción
    Inicio inicio;
    Fin fin;
    Continuar continuar = Continuar::No;
    int64_t preEsperaMs = 0;
    int64_t postEsperaMs = 0;
    nlohmann::json transicion; ///< null = la de la escena
    int duracionPrevistaSeg = 0;
    std::string notaOperador;
    std::vector<std::string> etiquetas;
    std::vector<Pieza> piezas;

    /// La duración que se usa para el cálculo: la de `fin` si dice, si no la prevista.
    int64_t duracionPlanMs() const;

    nlohmann::json aJson() const;
    static Paso deJson(const nlohmann::json& j);
};

struct Bloque {
    std::string id;
    std::string nombre;
    std::string nota;
    std::optional<int> presupuestoSeg;
    std::optional<Inicio> inicio; ///< el de su primer paso, si ese no dice otra cosa
    std::string plantilla;        ///< "punto": se expande desde `origen`
    std::string origen;           ///< "asamblea/orden-del-dia"
    std::vector<Paso> pasos;

    nlohmann::json aJson() const;
    static Bloque deJson(const nlohmann::json& j);
};

/// Un carril. Las de `escena`, `graficos`, `dsk`, `audio` y `medios` son de una
/// salida; las demás (prompter, sala, asamblea, pantallas, ptz, asistente, notas)
/// son globales y van con `salida` vacía.
struct Pista {
    std::string id;
    std::string tipo;
    std::string salida;
    std::string nombre;
    std::string color;
    bool oculta = false;

    nlohmann::json aJson() const;
    static Pista deJson(const nlohmann::json& j);
};

/// Un adlib: una pieza o un paso suelto que se dispara a mano.
struct ItemEstante {
    std::string id;
    std::string tecla, boton, color;
    std::optional<Pieza> pieza;
    std::optional<Paso> paso;

    nlohmann::json aJson() const;
    static ItemEstante deJson(const nlohmann::json& j);
};

struct Disparador {
    std::string id;
    Condicion cuando;
    nlohmann::json hacer = nlohmann::json::object(); ///< la acción (L7)
    Vida activo = Vida::Evento;

    nlohmann::json aJson() const;
    static Disparador deJson(const nlohmann::json& j);
};

struct Plantilla {
    std::string id;
    std::string nombre;
    std::vector<Paso> pasos;

    nlohmann::json aJson() const;
    static Plantilla deJson(const nlohmann::json& j);
};

/// La línea entera: lo que va en `evento.json` → `"linea"`.
struct Linea {
    static constexpr int kVersion = 2;

    std::vector<std::string> salidas;
    std::vector<Pista> pistas;
    std::vector<Bloque> bloques;
    std::vector<ItemEstante> estante;
    std::vector<Disparador> disparadores;
    std::vector<Plantilla> plantillas;

    /// Las salidas de verdad: si no dice ninguna, "principal".
    std::vector<std::string> salidasEfectivas() const;
    /// La salida de un paso: la suya, o la primera si no dice.
    std::string salidaDe(const Paso& p) const;
    /// ¿El paso sale por esa salida? ("*" sale por todas.)
    bool vaPor(const Paso& p, const std::string& salida) const;

    const Pista* pista(const std::string& id) const;
    const Paso* paso(const std::string& id) const;
    /// El bloque de un paso (nullptr si no está).
    const Bloque* bloqueDe(const std::string& pasoId) const;

    nlohmann::json aJson() const;
    static Linea deJson(const nlohmann::json& j);
};

/// Pasa un guion v1 (bloques → cues) a la línea v2, sin perder nada:
///  - cada bloque queda igual;
///  - cada cue `escena`, `orador`, `video` o `espera` abre un paso (y un bloque que
///    empieza con otra cosa abre un paso sin escena, `<bloque>-inicio`);
///  - los cues que le siguen y no abren paso se vuelven piezas de ese paso, con
///    `desde` = la suma de duraciones en el medio si el cue de antes encadenaba
///    solo; si no, `{"manual":true}`, porque en el v1 ese cue esperaba el clic;
///  - `automatico` del último cue del paso pasa a `continuar: "sigue"`;
///  - `pausa` es un paso sin escena con su duración y `continuar: "sigue"`;
///  - `espera` es un paso con `inicio: go`, abierto (espera a la persona y nunca
///    se va sola, aunque estuviera marcada automática) y su texto en la nota del operador.
/// La duración del paso es la suma de las de sus cues: el total no cambia.
/// Pura: guardar la copia v1 en Respaldos/ es de quien la llama (L2).
Linea migrar(const Evento& v1);

/// ¿Este cue v1 abre un paso? Los que cambian el plano principal (escena, orador,
/// video) o frenan el guion (pausa, espera). Los demás van como piezas del paso.
bool abrePaso(Tipo t);

/// La vida (y, para un gráfico que «se va sola», la duración) que la migración le
/// da a la pieza de un cue v1: la música sigue hasta la siguiente, el gráfico según
/// su `seVa`. La usa también reconciliar() para saber si una vida fue tocada a mano.
void vidaDeCue(const Cue& c, Pieza& p);

// --- Lo emitido y lo que se sabe en vivo ---------------------------------------------

enum class ComoEntro { Go, Auto, Hora, Cuando, Ancla };

/// Un paso que salió al aire de verdad (lo planeado no se toca al correr).
struct Instancia {
    std::string salida, paso;
    int64_t desdeMs = 0;
    int64_t hastaMs = 0; ///< 0 = sigue al aire
    ComoEntro comoEntro = ComoEntro::Go;

    nlohmann::json aJson() const;
    static Instancia deJson(const nlohmann::json& j);
};

/// Un paso de una salida. Con `paso` vacío, la salida entera (para los holds).
struct RefPaso {
    std::string salida, paso;
    bool operator==(const RefPaso& o) const { return salida == o.salida && paso == o.paso; }
};

/// Lo que se sabe de una condición: si se cumple ahora y desde cuándo.
struct EstadoCondicion {
    bool cumple = false;
    int64_t desdeMs = 0;
};

/// Todo lo que el resolvedor necesita además del modelo.
struct Entrada {
    int64_t ahoraMs = 0;
    /// Hora de arranque prevista (ms de época). 0 = no hay: se cuenta desde `ahora`.
    int64_t inicioPlaneadoMs = 0;
    /// Las 00:00 del día del evento en su huso, para pasar "HH:MM" a ms. Una hora
    /// anterior al arranque previsto se toma del día siguiente (un evento que cruza
    /// la medianoche).
    int64_t medianocheMs = 0;
    /// Fin planeado del evento. 0 = arranque previsto + lo previsto de cada salida.
    int64_t finPlaneadoMs = 0;

    std::vector<Instancia> instancias;
    std::vector<RefPaso> salteados; ///< marcados con «Saltar»
    std::vector<RefPaso> holds;     ///< pasos (o salidas enteras) en HOLD
    std::vector<RefPaso> proximos;  ///< el «próximo» movido a mano (F9/F10), por salida
    std::map<Condicion, EstadoCondicion> condiciones;
    /// Back times pisados (como en Cuez): paso → lo último que puede arrancar.
    std::map<std::string, int64_t> backTimesPisados;
    /// Las cámaras que usa una escena (para los conflictos de PTZ). Vacía = no se chequea.
    std::function<std::vector<std::string>(const std::string& escena)> camarasDeEscena;
};

// --- Lo que sale del resolvedor ------------------------------------------------------

enum class EstadoPaso { Pasado, AlAire, Proximo, Futuro, Salteado };

struct PasoResuelto {
    std::string paso, bloque;
    EstadoPaso estado = EstadoPaso::Futuro;
    int64_t frenteMs = 0; ///< front time: cuándo arranca (o arrancó)
    int64_t finMs = 0;    ///< end time: cuándo lo reemplaza el siguiente (o termina)
    std::optional<int64_t> backMs; ///< lo último que puede arrancar para llegar a horario
    bool backPisado = false;
    bool tarde = false;      ///< frente > back
    bool estimado = false;   ///< depende de algo sin tiempo conocido (regla 5)
    bool automatico = false; ///< entra solo (no espera un GO)
    ComoEntro como = ComoEntro::Go;
    bool enHold = false;
    bool cortadoPorHora = false; ///< un hard start «cortar» no lo deja entrar
};

/// Un hard start con su gap (positivo: sobra) u overlap (negativo: falta).
struct HardStart {
    std::string salida, paso;
    int64_t horaMs = 0;
    Politica politica = Politica::Avisar;
    int64_t gapMs = 0;
};

struct BloqueResuelto {
    std::string bloque;
    int64_t desdeMs = 0, hastaMs = 0;
    int64_t presupuestoMs = 0; ///< el del bloque, o la suma de lo previsto
    int64_t overUnderMs = 0;   ///< positivo: se pasa
};

struct SalidaResuelta {
    std::string salida;
    std::vector<PasoResuelto> pasos; ///< en el orden del modelo
    std::vector<BloqueResuelto> bloques;
    std::vector<HardStart> hardStarts;
    std::string alAire, proximo;
    int64_t finMs = 0;
    int64_t finPlaneadoMs = 0;
    int64_t overUnderMs = 0; ///< contra el fin planeado (0 si no hay)

    const PasoResuelto* paso(const std::string& id) const;
};

/// Una pieza con su tiempo absoluto.
struct PiezaResuelta {
    std::string pieza, paso, bloque, salida, pista;
    bool compartida = false; ///< pista global en un paso "*": es de todas las salidas
    int64_t desdeMs = 0;
    int64_t hastaMs = kNunca;
    int prioridad = 0;
    bool estimado = false;
    int orden = 0; ///< posición en el modelo, para desempatar
};

struct ProximoEvento {
    enum class Que { EntraPaso, EntraPieza, SalePieza };
    Que que = Que::EntraPaso;
    int64_t ms = 0;
    std::string salida, paso, pieza;
    ComoEntro como = ComoEntro::Auto;
    bool estimado = false;
};

struct Conflicto {
    enum class Tipo { Ptz, Grafico };
    Tipo tipo = Tipo::Ptz;
    int64_t ms = 0;
    std::string salida, pieza;            ///< la que choca
    std::string otraSalida, otroPaso, otraPieza;
    bool armada = false; ///< PTZ: la cámara está en el PVW (próximo) y no al aire
    std::string recurso; ///< la cámara o la fuente de gráfico
};

/// Lo que hay en una pista en un instante.
struct Activa {
    std::string pista;
    PiezaResuelta pieza;
};

struct Resolucion {
    std::vector<SalidaResuelta> salidas;
    std::vector<PiezaResuelta> piezas;
    std::vector<ProximoEvento> proximosEventos; ///< por hora
    std::vector<Conflicto> conflictos;

    const SalidaResuelta* salida(const std::string& id) const;
    /// El getResolvedState(t) de SuperFly: una sola pieza activa por pista; gana
    /// la de mayor prioridad y, si empatan, la que arrancó más tarde.
    std::vector<Activa> estadoEn(int64_t t) const;
};

Resolucion resolver(const Linea& l, const Entrada& e);

// --- Revisión previa -----------------------------------------------------------------

struct Reparo {
    std::string bloque, paso, pieza; ///< dónde (vacío = de la línea entera)
    bool grave = false;
    std::string texto;
};

struct OpcionesRevision {
    /// Ahora, arranque, medianoche y las cámaras de cada escena (para los conflictos).
    Entrada contexto;
    /// ¿Existe la escena? Vacía = no se chequea (eso necesita el motor).
    std::function<bool(const std::string& escena)> existeEscena;
    /// Cuántos pasos seguidos entrando solos ya es una cadena larga.
    int cadenaLarga = 6;
};

/// Lo que se ve sin correr: ids repetidos, pistas que no existen, piezas
/// incompletas, escenas que faltan, referencias rotas, cadenas automáticas
/// largas, hard starts imposibles y conflictos entre salidas.
std::vector<Reparo> revisar(const Linea& l, const OpcionesRevision& o = {});

// --- Load to time y retoma ----------------------------------------------------------

struct Reposicion {
    PiezaResuelta pieza;
    SiSePerdio accion = SiSePerdio::Ejecutar;
    bool sigueActiva = true; ///< false: era una acción puntual que ya pasó
};

/// Lo que hay que reponer al llegar a un paso salteando el camino (el «load to
/// time» de QLab).
struct Carga {
    std::vector<Reposicion> deberianEstar; ///< de pasos anteriores, con su siSePerdio
    std::vector<std::string> sobran;       ///< de `activasAhora`, las que no van
};

/// Si se entra al paso `destino` de `salida`: qué piezas de pasos anteriores
/// deberían estar activas en ese momento (corriendo el plan de punta a punta) y,
/// de las que hay activas ahora, cuáles sobran. Las de vida `paso` de lo
/// salteado no aparecen: no se ejecutan. `base` aporta el arranque y la
/// medianoche (para los hard starts); lo demás de `base` se ignora.
Carga cargarEn(const Linea& l, const Entrada& base, const std::string& salida, const std::string& destino,
               const std::vector<std::string>& activasAhora = {});

/// El estado vivo (Runtime/linea-aire.json): lo que hace falta para volver.
struct PiezaAire {
    std::string pieza, salida;
    int64_t desdeMs = 0;
};

struct SalidaAire {
    std::string salida;
    std::string alAire; ///< vacío = nada al aire
    int64_t alAireDesdeMs = 0;
    std::string proximo;
    bool hold = false;
};

struct EstadoAire {
    int64_t escritoMs = 0;
    std::vector<SalidaAire> salidas;
    std::vector<PiezaAire> piezas;
    std::vector<Instancia> instancias;
    std::vector<RefPaso> salteados;
    /// Tras un corte: la conducción pregunta «¿qué está pasando?» (3.8).
    bool pideDescripcion = false;
    std::string descripcion;

    nlohmann::json aJson() const;
    static EstadoAire deJson(const nlohmann::json& j);
};

struct Retoma {
    EstadoAire estado;                 ///< con todo en HOLD y `pideDescripcion`
    std::vector<Reposicion> reponer;   ///< estaban activas y siguen: vuelven como estaban (T47)
    std::vector<Reposicion> perdidas;  ///< tocaban durante el corte: van con su siSePerdio
    std::vector<std::string> sacar;    ///< estaban activas y ya no van
    /// Compacto, para que el asistente lo lea junto con lo que cuente la persona.
    nlohmann::json resumen;
};

/// Al levantar después de un corte (3.8): el aire vuelve como estaba, pero la
/// línea queda en HOLD en todas las salidas y pide una descripción. Lo que tocaba
/// durante el corte NO avanzó solo: el resumen dice qué indicaba el plan
/// (`segunElPlan`) para que el asistente proponga cómo reenganchar.
Retoma retomarTrasCorte(const Linea& l, const EstadoAire& guardado, const Entrada& base);

// --- Nombres (para el JSON, el MCP y los logs; nunca se traducen) ----------------------

const char* nombreDe(EstadoPaso e);
const char* nombreDe(ComoEntro c);
const char* nombreDe(SiSePerdio s);
const char* nombreDe(Vida v);
const char* nombreDe(Politica p);
const char* nombreDe(Continuar c);

} // namespace ba::evento::linea
