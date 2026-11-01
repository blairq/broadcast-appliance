// ============================================================================
// src/app/ui/ConduccionLinea.hpp — el núcleo de órdenes de la línea corriendo
//
// La corrida (`evento::linea::Corrida`, un EjecutorLinea por salida), el cursor del guion
// de cues y LAS ÓRDENES DEL VIVO, con sus reglas: arrancar pasa por la revisión previa, un
// ítem del estante que es un paso entra de próximo, las salidas tienen que existir, tras un
// corte la línea vuelve en HOLD. Cada orden devuelve "" o por qué no; la página de
// conducción (ui/CorridaViewModel, la vista), la vista de la línea (ui/LineaViewModel) y
// el MCP (`linea_*`) solo convierten. Las reglas puras (el cursor, el reloj, lo público, las
// salidas) son de evento/Corrida; lo que cada pieza le hace al sistema, de
// ui/EjecucionLinea.
//
// EL ENSAYO es la misma máquina con la ejecución apagada: se recorre igual, se ven los
// tiempos, pero no se toca lo que sale al aire.
//
// EL MOTOR ES DEL PLANO DE CONTROL (F3-03, docs/arquitectura/05-motor-del-plano.md). La
// corrida (su reloj, sus EjecutorLinea) vive en la interfaz; lo que toca el aire se PIDE
// por la ventanilla (ui/EjecucionLinea) y lo que se lee del motor sale de su foto.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::app {
class AppRuntime;
class CarruselViewModel;
class ConferenceViewModel;
class EjecucionLinea;
class EjecutorLinea;
class EventoAbierto;
class GuionViewModel;
class VentanillaMotor;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos
namespace escenas {
class Conmutadores;
} // namespace escenas
namespace control {
class BusEventos;
class Dsk;
} // namespace control
namespace ptz {
class ControlCamaras;
}
namespace asamblea {
class OrdenDelDia;
class Votaciones;
} // namespace asamblea
namespace medios {
class Playlist;
}

/// Lo que la línea maneja además del guion (L6/L7): la mesa de corte DE CADA SALIDA, el
/// bus, las capas DSK, las PTZ, el orden del día, las votaciones y la playlist. Se pide cada
/// vez (algunos se crean después) y cualquiera puede faltar.
struct SistemaLinea {
    escenas::Conmutadores* conmutadores = nullptr;
    control::BusEventos* bus = nullptr;
    control::Dsk* dsk = nullptr;
    ptz::ControlCamaras* camaras = nullptr;
    asamblea::OrdenDelDia* ordenDelDia = nullptr;
    asamblea::Votaciones* votaciones = nullptr;
    medios::Playlist* playlist = nullptr;
};

class ConduccionLinea : public QObject {
    Q_OBJECT
   public:
    using Herramienta =
        std::function<void(const std::string&, const nlohmann::json&, std::function<void(bool, const std::string&)>)>;

    ConduccionLinea(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                    ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent = nullptr);
    ~ConduccionLinea() override;

    void setSistema(std::function<SistemaLinea()> f) { sistema_ = std::move(f); }
    SistemaLinea sistema() const { return sistema_ ? sistema_() : SistemaLinea{}; }
    void setHerramienta(Herramienta fn);
    void setVinculos(graficos::Vinculos* v);
    void setCarruseles(CarruselViewModel* c);
    /// Correr de verdad pone el evento en curso: lo hace quien sabe (la lista y la web).
    void setPonerEnCurso(std::function<void()> f) { ponerEnCurso_ = std::move(f); }
    /// Abrir otro evento (para retomar el que corría antes de un corte).
    void setAbrir(std::function<bool(const QString&)> f) { abrir_ = std::move(f); }

    // --- Lo que se lee -----------------------------------------------------------------
    const evento::Evento& ev() const;
    const evento::linea::Linea& linea() const;
    const evento::linea::Proyeccion& proy() const;
    const evento::Cue* cue(int bloque, int cue) const;
    /// La corrida (la que corre o la última; nullptr si no corrió).
    const evento::linea::Corrida* corrida() const { return corrida_.get(); }
    const evento::linea::CursorGuion& cursor() const { return cursor_; }
    /// ¿Corriendo o en pausa?
    bool corriendo() const;
    bool ensayo() const { return ensayo_; }
    void setEnsayo(bool on);
    bool grabarTiempos() const { return grabar_; }
    void setGrabarTiempos(bool on);
    /// Lo que grabó el último ensayo, sin guardar (null si nada).
    const nlohmann::json& grabado() const { return grabado_; }
    /// Lo último que se ejecutó o se intentó: [{hora, texto, tipo}], lo más nuevo primero.
    const QVariantList& bitacora() const { return bitacora_; }
    /// La salida de la línea que sale hoy (la por omisión).
    std::string salidaPorOmision() const;
    /// "HH:MM:SS" en el huso del evento.
    std::string horaDe(int64_t ms) const;
    /// La hora de hoy (en el huso del evento) en ms; -1 si "HH:MM" no es una hora.
    int64_t horaHoyMs(const std::string& hhmm) const;
    /// Arranque previsto, medianoche y fin planeado (del día del evento o, con `hoy`, de hoy).
    evento::linea::Entrada entradaBase(bool hoy) const;
    /// El fin planeado del guion de hoy (inicio + lo previsto), en ms; -1 sin hora de inicio.
    int64_t finPlaneadoMs(int totalPrevistoSeg) const;
    /// Una pieza como cue (con la duración del guion de cues o de su paso).
    evento::Cue cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const;
    /// «"salida" · texto» si hay varias salidas.
    QString enSalida(const std::string& salida, const QString& texto) const;

    /// A la bitácora de la página y, si `alRegistro`, al registro general.
    void anotar(const QString& texto, const QString& tipo, bool alRegistro = true);
    void limpiarBitacora();
    /// Un aviso a quien mira (la barra de la app).
    void avisar(const QString& texto, const QString& tipo) { emit notify(texto, tipo); }

    // --- Las órdenes: "" o por qué no ------------------------------------------------------
    /// Arrancar con la revisión previa: si encontró problemas graves y no se pide `forzar`,
    /// NO arranca y devuelve cuántos son. 0 = arrancó.
    int arrancarRevisando(bool forzar);
    std::string lineaGo(const std::string& salida);
    std::string lineaHold(const std::string& salida, std::optional<bool> on);
    std::string lineaMoverProximo(const std::string& salida, int delta);
    std::string lineaSaltar(const std::string& salida);
    std::string lineaIr(const std::string& salida, const std::string& paso);
    std::string lineaRepetir(const std::string& salida);
    /// Una pieza a mano; un ítem del estante que es un paso entra de próximo (lineaInsertar).
    std::string lineaAdlib(const std::string& salida, const std::string& pieza);
    std::string lineaInsertar(const std::string& salida, const std::string& item, bool alAire);
    std::string lineaRetomar(const std::string& descripcion, bool simular);
    std::string lineaSoltar(const std::string& decision);
    std::string lineaPulso(const std::string& clave);
    std::string lineaReenganchar(const std::string& salida);
    std::string lineaPtz(const std::string& pieza, const std::string& decision);
    /// Vuelve un cue atrás en el guion de cues (dentro del paso: lo vuelve a disparar).
    void retroceder();
    void irA(int bloque, int cue);
    void pausar();
    void reanudar();
    void parar();
    /// Ensayo acelerado: recorre el guion ENTERO de una, sin esperar las duraciones y sin
    /// tocar nada, y dice de cada cue si se podría ejecutar.
    void ensayoAcelerado();
    /// «¿Qué está pasando?» tras un corte: anota la descripción y devuelve el pedido para el
    /// asistente ("" si no hay retoma).
    QString describirRetoma(const QString& texto);
    /// «Seguir como estaba»: suelta el HOLD del corte sin cambiar nada.
    void seguirComoEstaba();
    /// Lo grabado pasa a la línea (con su revisión para deshacer). "" o por qué no.
    std::string aplicarTiemposGrabados();
    void descartarTiemposGrabados();
    /// Se abre otro evento (o se cierra): para lo que corría y olvida la corrida.
    void olvidar();
    /// Tras un corte de luz: si la línea estaba corriendo, vuelve en HOLD.
    void retomarLinea();
    /// Sin corte: lo que haya quedado escrito no es de un corte.
    void borrarVivo();
    /// Se engancha a lo que la línea mira (una vez por cosa).
    void enchufar();
    /// Un medio (o la playlist) de una pieza terminó: «medio:<pieza>.termino».
    void medioTermino(const std::string& pieza);
    /// La escena al aire de una salida cambió (lo avisa su conmutador).
    void alAireCambio(const std::string& salida, const std::string& escena);

    // --- La línea, para el MCP (linea_*) y la vista de la línea ----------------------------
    nlohmann::json lineaEstado() const;
    nlohmann::json lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const;
    /// Un lote de operaciones; con `probar` no aplica. Aplicado: la línea anterior queda en
    /// Respaldos/linea-<n>.json (lineaDeshacer) y se guarda.
    nlohmann::json lineaEditar(const nlohmann::json& ops, bool probar);
    nlohmann::json lineaDeshacer();
    nlohmann::json lineaRevisar() const;
    nlohmann::json lineaAjustar(const std::string& salida, int64_t objetivoMs) const;
    nlohmann::json lineaDesdePlantilla(const nlohmann::json& a, const std::string& salida, const nlohmann::json& guia,
                                       const nlohmann::json& ordenDelDia);
    nlohmann::json lineaCamaras(const std::string& camara,
                                const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena) const;
    nlohmann::json lineaRecorrido(int ultimos) const;
    nlohmann::json lineaTiemposGrabados() const;

   signals:
    /// Cambió el estado de la corrida (corre, el cursor, el HOLD, la retoma, lo grabado).
    void correChanged();
    void bitacoraChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    void arrancar();
    void nuevaCorrida();
    void despachar(const std::vector<evento::linea::Accion>& acc);
    void hacerAccion(const evento::linea::Accion& a);
    void moverCursor(int bloque, int cue);
    void vigilarTemas();
    void avisarPresentador();
    evento::linea::Linea lineaDeCorrida() const;
    void seguirPalabra(const std::vector<evento::linea::Accion>& acc);
    std::map<std::string, int64_t> segundosDePalabra() const;
    void expandirOrdenDelDia();
    /// La línea es otra (`desdeLinea`: cambió por su cuenta): la corrida se entera.
    void lineaCambio(bool desdeLinea);
    void escribirVivo();
    void retomarDesde(const nlohmann::json& vivo);
    void volcarRecorrido();
    std::filesystem::path archivoRecorrido() const;
    QString pedidoDeRetoma(const QString& descripcion) const;
    /// La revisión de la línea más nueva en Respaldos/ (las encoladas también cuentan).
    int ultimaRevision();

    struct Palabra {
        std::string paso;
        int previstoSeg = 0;
        std::map<std::string, int64_t> segundos; ///< lo que llevaba cada uno al entrar el paso
    };

    EventoAbierto& ab_;
    GuionViewModel& guion_;
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    ConferenceViewModel* sala_ = nullptr;
    std::function<SistemaLinea()> sistema_;
    std::function<void()> ponerEnCurso_;
    std::function<bool(const QString&)> abrir_;
    /// Lo que cada pieza le hace al sistema (las escenas, la música, los gráficos, las PTZ…).
    std::unique_ptr<EjecucionLinea> ejec_;

    std::unique_ptr<evento::linea::Corrida> corrida_;
    std::map<std::string, std::unique_ptr<EjecutorLinea>> ejecutores_;
    evento::linea::CursorGuion cursor_;
    bool ensayo_ = false;
    bool grabar_ = false;
    nlohmann::json grabado_;
    QVariantList bitacora_;
    QTimer reloj_; ///< cada segundo, corriendo: el estado vivo al documento de aire (escribirVivo)
    int revision_ = -1; ///< la última de Respaldos/linea-<n>.json (-1: sin leer todavía)
    std::set<const QObject*> enchufados_;
    std::map<std::string, int> suscripciones_; ///< tema del bus → id
    std::map<std::string, Palabra> palabras_;  ///< por salida
    nlohmann::json presentadorEnviado_;
};

} // namespace ba::app
