// ============================================================================
// src/app/evento/Corrida.hpp — la línea de tiempo corriendo (L2, la parte pura)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md (3.3, 3.8 y 3.9). Linea.hpp dice qué
// va a pasar; esto es lo que PASA: qué pasos salieron y cuándo (las instancias), qué
// piezas se dispararon y siguen vivas, los HOLD, lo salteado, el próximo movido a
// mano, lo insertado del estante, la pausa general y la retoma tras un corte.
//
// NO EJECUTA NADA. Cada movimiento devuelve ACCIONES («entró el paso p3», «disparar
// la pieza z7», «sacar z2», «armar la escena X en el PVW»); quien sabe hacerlas
// (EjecutorLinea y el view model, en la app) las hace. Y no tiene reloj: el `ahora`
// entra en cada llamada. Así se prueba entera sin motor y con el tiempo inyectado
// (test_linea), igual que el resolvedor.
//
// UN SOLO CÁLCULO. Qué entra solo y cuándo, qué pieza toca y hasta cuándo vive lo
// dice el resolvedor (resolver) con lo emitido hasta ahora; la corrida solo le
// pregunta, hace lo que corresponde y le dice al de arriba para cuándo volver a
// preguntar (`proximoMs`): un timer exacto al próximo evento, no un sondeo.
//
// EL CORTE DE LUZ (3.8, decidido el 2026-10-31). Lo vivo se escribe en
// Runtime/linea-aire.json (`aJsonVivo`). Al volver, `retomar` repone lo que estaba
// (la música, los gráficos fijos: T47) pero deja TODAS las salidas en HOLD y pide
// una descripción («¿qué está pasando?»). El asistente la lee con el resumen y
// propone cómo adecuar la línea; la persona confirma y recién ahí se suelta
// (`soltar`). Sin respuesta queda en HOLD; con GO se suelta tal cual estaba. La
// descripción y la decisión quedan en el recorrido.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Linea.hpp"

namespace ba::evento::linea {

/// Lo que hay que hacer en el aire.
struct Accion {
    enum class Que {
        EntraPaso, ///< entró un paso (lo que hace en el aire lo hacen sus piezas)
        Pieza,     ///< disparar una pieza
        Saca,      ///< sacar una pieza: se terminó su vida o ya no va
        Armar,     ///< el lookahead: dejar la escena del próximo armada en el PVW
        Avisar,    ///< algo para la persona (una pieza del asistente que se perdió en un corte)
        Termina,   ///< la salida no tiene más pasos
    };
    Que que = Que::Pieza;
    std::string salida, paso, pieza; ///< `paso` vacío en una Pieza: es del estante
    std::string escena;              ///< Armar
    bool reposicion = false;         ///< Pieza: repone el estado (saltar, retomar); no es la de su hora
    std::string texto;               ///< Avisar
};

enum class EstadoCorrida { Parada, Corriendo, Pausada, Terminada };
const char* nombreDe(EstadoCorrida e);

class Corrida {
   public:
    /// `base`: el arranque previsto, la medianoche, el fin planeado y las cámaras de
    /// cada escena. Lo emitido, los holds y el `ahora` los pone la corrida.
    Corrida(Linea l, Entrada base);

    /// La línea con la que corre: la del evento más lo insertado en vivo.
    const Linea& linea() const { return linea_; }
    /// La línea cambió (se editó el futuro): lo insertado se vuelve a poner.
    void setLinea(Linea l);
    EstadoCorrida estado() const { return estado_; }
    std::string salidaPorOmision() const { return linea_.salidasEfectivas().front(); }
    /// Lo que el resolvedor necesita para decir qué pasa en `ahora`.
    Entrada entrada(int64_t ahora) const;
    Resolucion resolver(int64_t ahora) const { return linea::resolver(linea_, entrada(ahora)); }

    // --- Vivo (3.3): cada una devuelve lo que hay que hacer -----------------------------
    /// Arranca y toma el primer paso de la salida por omisión.
    std::vector<Accion> arrancar(int64_t ahora);
    /// GO / TAKE: entra el próximo de la salida. Sin próximo, la salida termina (y
    /// la corrida, si ya no hay nada al aire en ninguna). En una retoma, además
    /// suelta el HOLD del corte: «con GO se suelta tal cual estaba».
    std::vector<Accion> go(const std::string& salida, int64_t ahora);
    /// HOLD del paso al aire: no se va solo (la cuenta sigue y empuja). false si no hay paso.
    bool setHold(const std::string& salida, bool on, int64_t ahora);
    /// En HOLD: el del paso o el del corte.
    bool hold(const std::string& salida) const;
    /// F9/F10: mueve el próximo `delta` pasos sin tocar el aire. false si no hay a dónde.
    bool moverProximo(const std::string& salida, int delta, int64_t ahora);
    /// Marca el próximo como salteado.
    bool saltar(const std::string& salida, int64_t ahora);
    /// «Ir a»: entra ese paso con el estado que tendría que tener el aire ahí (el
    /// «load to time»): repone las piezas de vida larga y saca las que sobran.
    std::vector<Accion> irA(const std::string& salida, const std::string& paso, int64_t ahora, std::string* error = nullptr);
    /// Vuelve a entrar el paso al aire.
    std::vector<Accion> repetir(const std::string& salida, int64_t ahora);
    /// Dispara a mano una pieza: una del paso al aire (sus adlibs, o una que todavía
    /// no llegó a su hora) o una del estante (por el id del ítem o de su pieza).
    std::vector<Accion> adlib(const std::string& salida, const std::string& pieza, int64_t ahora, std::string* error = nullptr);
    /// Un paso del estante: va de próximo (o al aire ya, con `alAire`).
    std::vector<Accion> insertar(const std::string& salida, const std::string& item, bool alAire, int64_t ahora,
                                 std::string* error = nullptr);
    /// La pausa general: nada avanza solo y lo pausado no cuenta como desvío.
    void pausar(int64_t ahora);
    std::vector<Accion> reanudar(int64_t ahora);
    std::vector<Accion> terminar(int64_t ahora);

    // --- El tiempo -------------------------------------------------------------------
    /// Lo que tocaba hasta `ahora`: pasos que entran solos, piezas que llegan a su
    /// hora y piezas que se terminan. Se llama cuando dice `proximoMs`.
    std::vector<Accion> tick(int64_t ahora);
    /// Lo que corresponde ahora SIN hacer entrar pasos: piezas que tocan, piezas que se
    /// terminan y el lookahead. Para después de mover el próximo, saltar o un HOLD.
    std::vector<Accion> alDia(int64_t ahora);
    /// Cuánto falta (ms) para que haya algo que hacer; -1 si nada va a pasar solo.
    /// Con `salida`, solo lo de esa salida (el timer de su ejecutor).
    int64_t proximoMs(int64_t ahora, const std::string& salida = {}) const;
    /// El aviso «entra solo en 0:05»: los segundos (para arriba) en los últimos
    /// `kAvisoSeg` antes de que el próximo de la salida entre solo; 0 si no.
    int entraSoloEnSeg(const std::string& salida, int64_t ahora) const;
    static constexpr int kAvisoSeg = 5;

    // --- Lo emitido -------------------------------------------------------------------
    const std::vector<Instancia>& instancias() const { return instancias_; }
    /// El paso al aire de la salida (nullptr si no hay).
    const Instancia* alAire(const std::string& salida) const;
    /// ¿Ya se disparó esa pieza en el paso al aire?
    bool disparada(const std::string& salida, const std::string& pieza) const;
    /// Las piezas vivas de la salida (sus ids).
    std::vector<std::string> activas(const std::string& salida) const;
    /// La pieza por su paso y su id; con `paso` vacío, del estante.
    const Pieza* pieza(const std::string& paso, const std::string& id) const;
    int64_t arranqueMs() const { return arranqueMs_; }
    /// Todo lo pausado hasta `ahora` (incluida la pausa en curso).
    int64_t pausadoMs(int64_t ahora) const;
    /// Los renglones nuevos del recorrido (recorrido.jsonl), y se olvidan.
    std::vector<nlohmann::json> sacarRecorrido();

    // --- Estado vivo y retoma (3.8) -----------------------------------------------------
    /// Runtime/linea-aire.json: el EstadoAire más lo que la corrida necesita para volver.
    nlohmann::json aJsonVivo(int64_t ahora) const;
    /// Vuelve de un corte con lo que dejó `aJsonVivo`: el aire como estaba, todo en
    /// HOLD y pidiendo descripción. Devuelve lo que hay que reponer.
    std::vector<Accion> retomar(const nlohmann::json& vivo, int64_t ahora);
    bool retomando() const { return retomando_; }
    bool pideDescripcion() const { return pideDescripcion_; }
    const std::string& descripcion() const { return descripcion_; }
    /// Lo que se le pasa al asistente con la descripción (ver retomarTrasCorte).
    const nlohmann::json& resumenRetoma() const { return resumen_; }
    /// «¿Qué está pasando?»: queda en el recorrido; la línea sigue en HOLD.
    void describir(const std::string& texto, int64_t ahora);
    /// Suelta el HOLD del corte con lo que se decidió (queda en el recorrido).
    std::vector<Accion> soltar(const std::string& decision, int64_t ahora);

    /// El estado para la conducción y el MCP (linea_estado): por salida, al aire,
    /// próximo, holds, over/under, próximo hard start; la retoma; la pausa.
    nlohmann::json estadoJson(int64_t ahora, const std::function<std::string(int64_t)>& hora = {}) const;

   private:
    struct Viva {
        std::string salida, paso, pieza;
        int64_t desdeMs = 0;
        int64_t hastaMs = kNunca; ///< para las que el resolvedor no conoce (estante, repuestas)
    };
    struct Insercion {
        Paso paso;
        std::string despuesDe; ///< el paso detrás del cual va (vacío: al final)
    };

    int indiceAlAire(const std::string& salida) const;
    void entrar(const std::string& salida, const std::string& paso, ComoEntro como, int64_t ahora, std::vector<Accion>& acc);
    /// Lo que corresponde en `ahora`: pasos que entran solos (si `avanzar`), piezas que
    /// tocan y piezas que se terminan; y el lookahead.
    void procesar(int64_t ahora, bool avanzar, std::vector<Accion>& acc);
    void disparar(const std::string& salida, const std::string& paso, const std::string& pieza, int64_t ahora, int64_t hasta,
                  bool reposicion, std::vector<Accion>& acc);
    void aplicar(const Insercion& i);
    /// Suelta el HOLD del corte y anota la decisión, sin mover nada (lo mueve quien llama).
    void soltarCorte(const std::string& decision, int64_t ahora);
    void anotar(int64_t ahora, const char* que, nlohmann::json datos = nlohmann::json::object());

    Linea linea_;
    Entrada base_;
    EstadoCorrida estado_ = EstadoCorrida::Parada;
    int64_t arranqueMs_ = 0;
    int64_t pausadoMs_ = 0;
    int64_t pausadoDesdeMs_ = 0;
    std::vector<Instancia> instancias_;
    std::vector<RefPaso> salteados_;
    std::vector<RefPaso> holdsPaso_;     ///< los HOLD de la persona (del paso al aire)
    std::set<std::string> holdCorte_;    ///< las salidas en HOLD por un corte
    std::map<std::string, std::string> proximos_; ///< salida → el próximo movido a mano
    std::set<std::pair<std::size_t, std::string>> disparadas_; ///< (instancia, pieza)
    std::vector<Viva> vivas_;
    std::map<std::string, std::string> armado_; ///< salida → la escena que se mandó a armar
    std::vector<Insercion> inserciones_;
    int insertados_ = 0;
    bool retomando_ = false;
    bool pideDescripcion_ = false;
    std::string descripcion_;
    nlohmann::json resumen_ = nlohmann::json::object();
    std::vector<nlohmann::json> recorrido_;
};

} // namespace ba::evento::linea
