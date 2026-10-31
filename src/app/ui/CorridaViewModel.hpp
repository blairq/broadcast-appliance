// ============================================================================
// src/app/ui/CorridaViewModel.hpp — la línea de tiempo corriendo (la conducción)
//
// La corrida (`evento::linea::Corrida`, un EjecutorLinea por salida) y la traducción de
// cada pieza a lo que hay que hacerle al sistema. El modelo de abajo no sabe ejecutar
// nada a propósito; acá es donde se cierra esa costura. Lo importante es que la
// traducción sea HONESTA: una pieza de un tipo que todavía no sabemos ejecutar no se hace
// la que sí — lo dice, queda registrado, y la línea sigue. En vivo, un cue que falla en
// silencio es peor que uno que avisa.
//
// EL ENSAYO es la misma máquina con la ejecución apagada: se recorre igual, se ven los
// tiempos, pero no se toca lo que sale al aire.
//
// EL MOTOR ES DEL PLANO DE CONTROL (F3-03, docs/arquitectura/05-motor-del-plano.md). La
// corrida (su reloj, sus EjecutorLinea) vive en la interfaz: casi todo lo que hace una
// pieza es de la interfaz (los gráficos, el carrusel, el teleprompter, los oradores de la
// sala, los vínculos, el DSK, las PTZ, la bitácora). Lo que toca el aire —cortar y armar
// en el conmutador de cada salida, la música, el video, el audio— se PIDE por la
// ventanilla y lo que se lee del motor sale de su foto (los medios que suenan: la parte
// Medidas, que avisa; nada se mira con un reloj).
//
// LAS REGLAS DEL VIVO viven acá, para la conducción, la vista de la línea y el MCP
// (`linea_*`): arrancar pasa por la revisión previa, un ítem del estante que es un paso
// entra de próximo, las salidas tienen que existir. Cada acción devuelve "" o por qué no.
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
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "emision/FotoMotor.hpp"
#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::app {
class AppRuntime;
class CarruselViewModel;
class ConferenceViewModel;
class EjecutorLinea;
class EventoAbierto;
class GuionViewModel;
class VentanillaMotor;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos
namespace escenas {
class Conmutador;
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

class CorridaViewModel : public QObject {
    Q_OBJECT
    /// "parado" | "corriendo" | "pausado" | "terminado"
    Q_PROPERTY(QString estado READ estado NOTIFY correChanged)
    Q_PROPERTY(bool ensayo READ ensayo WRITE setEnsayo NOTIFY correChanged)
    /// El guion está corriendo DE VERDAD (no en ensayo): los cues ejecutan. NO es estar al
    /// aire: eso es que salga algo (Studio.aire). Correr el guion para probar sin
    /// transmitir no bloquea nada.
    Q_PROPERTY(bool guionCorriendo READ guionCorriendo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    /// {desvioSeg, enCueSeg, restanteSeg, cueNro, cuesTotal, …}: el reloj, que cambia cada
    /// segundo (aparte para no redibujar el guion entero).
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojChanged)
    /// El aviso «entra solo en 0:05» (los últimos 5 s); 0 = no hay aviso.
    Q_PROPERTY(int entraSoloEnSeg READ entraSoloEnSeg NOTIFY relojChanged)
    /// HOLD: el paso al aire no se va solo aunque se cumpla su duración.
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY correChanged)
    /// Tras un corte de luz: {activa, pideDescripcion, descripcion, resumen}.
    Q_PROPERTY(QVariantMap retoma READ retoma NOTIFY correChanged)
    /// Grabar la línea en vivo (L10): el ensayo corre con todo a GO y lo que dura cada paso
    /// queda para guardarlo como duración prevista. Se elige antes de arrancar.
    Q_PROPERTY(bool grabarTiempos READ grabarTiempos WRITE setGrabarTiempos NOTIFY correChanged)
    /// Lo que grabó el último ensayo, sin guardar: {ops, pasos, totalAntesSeg, totalGrabadoSeg}.
    Q_PROPERTY(QVariantMap tiemposGrabados READ tiemposGrabadosUi NOTIFY correChanged)
    /// Lo último que se ejecutó o se intentó, para la vista de conducción.
    Q_PROPERTY(QVariantList bitacora READ bitacora NOTIFY bitacoraChanged)

   public:
    /// Lo que la línea maneja además del guion (L6/L7): la mesa de corte DE CADA SALIDA,
    /// el bus, las capas DSK, las PTZ, el orden del día, las votaciones y la playlist. Se
    /// pide cada vez (algunos se crean después) y cualquiera puede faltar.
    struct Sistema {
        escenas::Conmutadores* conmutadores = nullptr;
        control::BusEventos* bus = nullptr;
        control::Dsk* dsk = nullptr;
        ptz::ControlCamaras* camaras = nullptr;
        asamblea::OrdenDelDia* ordenDelDia = nullptr;
        asamblea::Votaciones* votaciones = nullptr;
        medios::Playlist* playlist = nullptr;
    };
    using Herramienta = std::function<void(const std::string&, const nlohmann::json&, std::function<void(bool, const std::string&)>)>;

    CorridaViewModel(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                     ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent = nullptr);
    ~CorridaViewModel() override;

    void setSistema(std::function<Sistema()> f) { sistema_ = std::move(f); }
    void setHerramienta(Herramienta fn) { herramienta_ = std::move(fn); }
    void setVinculos(graficos::Vinculos* v) { vinculos_ = v; }
    void setCarruseles(CarruselViewModel* c) { carruseles_ = c; }
    /// Correr de verdad pone el evento en curso: lo hace quien sabe (la lista y la web).
    void setPonerEnCurso(std::function<void()> f) { ponerEnCurso_ = std::move(f); }
    /// Abrir otro evento (para retomar el que corría antes de un corte).
    void setAbrir(std::function<bool(const QString&)> f) { abrir_ = std::move(f); }

    // --- Lo que se ve ------------------------------------------------------------------
    QString estado() const;
    bool ensayo() const { return ensayo_; }
    void setEnsayo(bool on);
    bool guionCorriendo() const;
    QVariantMap actual() const;
    QVariantMap siguiente() const;
    QVariantMap reloj() const;
    int entraSoloEnSeg() const;
    bool hold() const;
    QVariantMap retoma() const;
    QVariantList bitacora() const { return bitacora_; }
    bool grabarTiempos() const { return grabar_; }
    void setGrabarTiempos(bool on);
    QVariantMap tiemposGrabadosUi() const;
    /// ¿La corrida está andando (corriendo o en pausa)?
    bool corriendo() const;
    /// La corrida (la que corre o la última; nullptr si no corrió).
    const evento::linea::Corrida* corrida() const { return corrida_.get(); }
    /// La salida de la línea que sale hoy (la por omisión).
    std::string salidaPorOmision() const;
    /// "HH:MM:SS" en el huso del evento.
    std::string horaDe(int64_t ms) const;
    /// La hora de hoy (en el huso del evento) en ms; -1 si "HH:MM" no es una hora.
    int64_t horaHoyMs(const std::string& hhmm) const;
    /// Arranque previsto, medianoche y fin planeado (del día del evento o, con `hoy`, de hoy).
    evento::linea::Entrada entradaBase(bool hoy) const;

    /// Lo público para la página web del evento: en qué sección va y lo que le falta.
    struct Publico {
        std::string estado = "proximo"; ///< proximo | en_vivo | pausa | terminado
        int bloque = -1;
        int restanteActualSeg = -1; ///< -1: no corre
        std::string habla;          ///< el que tiene la palabra (un cue de orador al aire)
    };
    Publico publico() const;

    /// A la bitácora de la página y, si `alRegistro`, al registro general.
    void anotar(const QString& texto, const QString& tipo, bool alRegistro = true);
    void limpiarBitacora();

    // --- Correr --------------------------------------------------------------------------
    Q_INVOKABLE void arrancar();
    /// Arrancar con la revisión previa: si encontró problemas graves y no se pide `forzar`,
    /// NO arranca y devuelve cuántos son. 0 = arrancó.
    Q_INVOKABLE int arrancarRevisando(bool forzar);
    Q_INVOKABLE void avanzar();
    Q_INVOKABLE void retroceder();
    Q_INVOKABLE void irA(int bloque, int cue);
    Q_INVOKABLE void pausar();
    Q_INVOKABLE void reanudar();
    void setHold(bool on);
    Q_INVOKABLE void alternarHold() { setHold(!hold()); }
    Q_INVOKABLE void parar();
    /// Ensayo acelerado: recorre el guion ENTERO de una, sin esperar las duraciones y sin
    /// tocar nada, y dice de cada cue si se podría ejecutar.
    Q_INVOKABLE void ensayoAcelerado();
    /// «¿Qué está pasando?» tras un corte: anota la descripción y devuelve el pedido para
    /// el asistente.
    Q_INVOKABLE QString describirRetoma(const QString& texto);
    /// «Seguir como estaba»: suelta el HOLD del corte sin cambiar nada.
    Q_INVOKABLE void seguirComoEstaba();
    /// Lo grabado pasa a la línea (con su revisión para deshacer). "" o por qué no.
    Q_INVOKABLE QString aplicarTiemposGrabados();
    Q_INVOKABLE void descartarTiemposGrabados();
    Q_INVOKABLE QString pulsar(const QString& clave) { return QString::fromStdString(lineaPulso(clave.toStdString())); }
    Q_INVOKABLE QString reenganchar(const QString& salida) { return QString::fromStdString(lineaReenganchar(salida.toStdString())); }
    Q_INVOKABLE QString decidirPtz(const QString& pieza, const QString& decision) {
        return QString::fromStdString(lineaPtz(pieza.toStdString(), decision.toStdString()));
    }

    /// Se abre otro evento (o se cierra): para lo que corría y olvida la corrida.
    void olvidar();
    /// Tras un corte de luz (lo llama quien abre el último evento): si la línea estaba
    /// corriendo, vuelve en HOLD.
    void retomarLinea();
    /// Sin corte: lo que haya quedado escrito no es de un corte.
    void borrarVivo();
    /// Se engancha a lo que la línea mira (una vez por cosa).
    void enchufar();

    // --- La línea, para el MCP (linea_*) y la vista de la línea ----------------------------
    nlohmann::json lineaEstado() const;
    nlohmann::json lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const;
    /// Un lote de operaciones; con `probar` no aplica. Aplicado: la línea anterior queda en
    /// Respaldos/linea-<n>.json (lineaDeshacer) y se guarda.
    nlohmann::json lineaEditar(const nlohmann::json& ops, bool probar);
    nlohmann::json lineaDeshacer();
    nlohmann::json lineaRevisar() const;
    nlohmann::json lineaAjustar(const std::string& salida, int64_t objetivoMs) const;
    /// linea_desde_plantilla: arma el lote de una plantilla (de la línea, de fábrica o de
    /// una guía) o del orden del día y lo prueba o lo aplica (como lineaEditar), con
    /// `ops`, `avisos`, `pasos` y `bloques`. `guia`: la línea de la guía de `escenario`.
    /// Sin plantilla, ni escenario, ni origen: la lista de plantillas.
    nlohmann::json lineaDesdePlantilla(const nlohmann::json& a, const std::string& salida, const nlohmann::json& guia,
                                       const nlohmann::json& ordenDelDia);
    /// linea_camaras: los próximos planos de cada cámara (`camara`: solo esa).
    nlohmann::json lineaCamaras(const std::string& camara,
                                const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena) const;
    std::string lineaGo(const std::string& salida);
    std::string lineaHold(const std::string& salida, std::optional<bool> on);
    std::string lineaMoverProximo(const std::string& salida, int delta);
    std::string lineaSaltar(const std::string& salida);
    std::string lineaIr(const std::string& salida, const std::string& paso);
    std::string lineaRepetir(const std::string& salida);
    /// Una pieza a mano; un ítem del estante que es un paso entra de próximo (lineaInsertar).
    std::string lineaAdlib(const std::string& salida, const std::string& pieza);
    std::string lineaInsertar(const std::string& salida, const std::string& item, bool alAire);
    nlohmann::json lineaRecorrido(int ultimos) const;
    std::string lineaRetomar(const std::string& descripcion, bool simular);
    std::string lineaSoltar(const std::string& decision);
    std::string lineaPulso(const std::string& clave);
    std::string lineaReenganchar(const std::string& salida);
    std::string lineaPtz(const std::string& pieza, const std::string& decision);
    nlohmann::json lineaTiemposGrabados() const;

   signals:
    void correChanged();
    void relojChanged();
    void bitacoraChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    // El cursor del guion de cues: el cue que se ve «al aire» (el último disparado del paso).
    struct Cursor {
        int bloque = -1;
        int cue = -1;
        int64_t desdeMs = 0;
        int64_t pausadoAlEntrarMs = 0;
        bool valido() const { return bloque >= 0 && cue >= 0; }
    };
    struct PtzPendiente {
        std::string salida, paso, pieza, camara, posicion;
        double segundos = -1;
        int64_t desdeMs = 0;
    };
    struct Palabra {
        std::string paso;
        int previstoSeg = 0;
        std::map<std::string, int64_t> segundos; ///< lo que llevaba cada uno al entrar el paso
    };

    Sistema sistema() const { return sistema_ ? sistema_() : Sistema{}; }
    const evento::Evento& ev() const;
    const evento::linea::Linea& linea() const;
    const evento::linea::Proyeccion& proy() const;
    const evento::Cue* cue(int bloque, int cue) const;

    void nuevaCorrida();
    void despachar(const std::vector<evento::linea::Accion>& acc);
    void hacerAccion(const evento::linea::Accion& a);
    void ejecutarCue(const evento::Cue& c, const std::string& paso, const std::string& clave, const std::string& salida, bool global);
    void sacarPieza(const evento::linea::Accion& a);
    void armarEscena(const std::string& escena, const std::string& salida);
    void tomarEscena(const std::string& escena, const nlohmann::json& datos, const std::string& salida,
                     std::function<void(const std::string& error)> listo);
    std::optional<std::string> salidaDelMotor(const std::string& salida) const;
    const FotoMotor::Salida* aireDe(const std::string& salida) const;
    void pedirAlConmutador(const char* que, const std::string& salida, std::function<std::string(escenas::Conmutador&)> orden,
                           std::function<void(const std::string& error)> listo);
    void olvidarEscenaDeLaLinea(const std::string& salida, const std::string& escena);
    void sacarAlAvanzar(const std::string& salida = {});
    static std::string prepararEscenaDeVideo(AppRuntime& rt, const std::string& escenaV, const std::string& fuenteV, bool* creada);
    void vigilarTemas();
    void avisarPresentador();
    evento::linea::Linea lineaDeCorrida() const;
    void seguirPalabra(const std::vector<evento::linea::Accion>& acc);
    std::map<std::string, int64_t> segundosDePalabra() const;
    void alAireCambio(const std::string& salida, const std::string& escena);
    void expandirOrdenDelDia();
    bool ejecutarPiezaLinea(const evento::linea::Pieza& z, const evento::linea::Accion& a, const QString& titulo);
    void moverPtz(const std::string& clave, bool forzar);
    /// Los medios de las piezas que terminan con su medio: se miran en la foto del motor
    /// cuando cambia la parte Medidas (solo mientras haya alguno).
    void mirarMedios();
    QString enSalida(const std::string& salida, const QString& texto) const;
    std::string musicaDe(const std::string& salida) const;
    std::string escenaVideoDe(const std::string& salida) const;
    std::string videoDe(const std::string& salida) const;
    /// La línea es otra (`desdeLinea`: cambió por su cuenta): la corrida se entera.
    void lineaCambio(bool desdeLinea);
    void escribirVivo();
    void retomarDesde(const nlohmann::json& vivo);
    void volcarRecorrido();
    std::filesystem::path archivoRecorrido() const;
    QString pedidoDeRetoma(const QString& descripcion) const;
    evento::Cue cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const;
    void moverCursor(int bloque, int cue);
    std::pair<int, int> cueSiguienteDe(int bloque, int cue) const;
    std::pair<int, int> cueAnteriorDe(int bloque, int cue) const;
    int enCueSeg(int64_t ahora) const;
    /// La revisión de la línea más nueva en Respaldos/ (las encoladas también cuentan).
    int ultimaRevision();

    EventoAbierto& ab_;
    GuionViewModel& guion_;
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    graficos::Vinculos* vinculos_ = nullptr;
    CarruselViewModel* carruseles_ = nullptr;
    std::function<Sistema()> sistema_;
    Herramienta herramienta_;
    std::function<void()> ponerEnCurso_;
    std::function<bool(const QString&)> abrir_;

    std::unique_ptr<evento::linea::Corrida> corrida_;
    std::map<std::string, std::unique_ptr<EjecutorLinea>> ejecutores_;
    Cursor cursor_;
    bool ensayo_ = false;
    bool grabar_ = false;
    nlohmann::json grabado_;
    QVariantList bitacora_;
    QTimer reloj_;
    int revision_ = -1; ///< la última de Respaldos/linea-<n>.json (-1: sin leer todavía)

    std::map<std::string, std::vector<std::pair<std::uint64_t, int>>> alAvanzar_; ///< por salida
    std::map<std::string, std::pair<std::uint64_t, int>> graficosDePieza_;
    std::map<std::string, std::string> musicaDePieza_; ///< salida → clave de la pieza
    std::map<std::string, std::pair<int, std::string>> dskDePieza_;
    std::map<std::string, PtzPendiente> ptzPendientes_;
    std::map<std::string, std::string> mediosVigilados_; ///< pieza → el medio («medio:<pieza>.termino»)
    std::set<std::string> mediosAndando_;
    std::string playlistDePieza_;
    bool playlistAndando_ = false;
    std::set<const QObject*> enchufados_;
    std::map<std::string, int> suscripciones_; ///< tema del bus → id
    std::map<std::string, std::multiset<std::string>> escenasDeLaLinea_;
    std::map<std::string, Palabra> palabras_; ///< por salida
    nlohmann::json presentadorEnviado_;
    QString escenaPrevia_;
};

} // namespace ba::app
