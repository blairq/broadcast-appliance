// ============================================================================
// src/app/ui/SalidasViewModel.hpp — el área de las salidas (F3-03, partición; PLAN-SALIDAS)
//
// Sale de StudioViewModel: las salidas (cada una un operador completo: su escena al
// aire y la armada, su negro y su placa, su transmisión y su grabación) y cuál maneja
// esta interfaz. Los modelos son el runtime (AppRuntime: crear, cambiar, sacar
// salidas; Config/salidas.json), el conmutador de escenas de cada salida
// (escenas::Conmutadores) y el FTB y la placa (emision::SeguridadAire).
//
//   - `lista`: una fila por salida, la por omisión primera; se rehace sola cuando
//     cambia algo de lo que muestra (el modelo avisa).
//   - La ACTIVA: la que manejan Dirección, Escenas y el Mezclador de esta interfaz. Se
//     recuerda en Config/interfaz.json (es de esta interfaz, no del aire); si se la
//     borra, vuelve sola a la por omisión. Lo que no es de la interfaz (el MCP, el bus,
//     las pantallas de tally de la sala) sigue con la por omisión (conmutador()).
//   - Lo que sale de cada una (aire) y desde cuándo, con un reloj que no se reinicia al
//     cambiar de activa; lo de la por omisión lo dicen las pastillas de la raíz.
//   - Lo que está al aire de cada una, para el multiview (el tally, los medios que
//     suenan) y para las superficies de control (el bus: aire/…, y los de cada salida).
//
// EL MOTOR ES DEL PLANO DE CONTROL (F3-03, docs/arquitectura/05-motor-del-plano.md): todo
// lo que se muestra sale de la foto del motor (emision/FotoMotor: cada salida con su aire,
// su armada, lo que sale, su negro y su placa, sus medios), y se rehace cuando la
// ventanilla avisa que cambió; lo que se hace (crear, cambiar, borrar, grabar, el negro, la
// placa) se le pide al dueño por la ventanilla y el error vuelve como `notify`. Los
// conmutadores de escenas y el negro y la placa son modelos del dueño: este área los crea y
// los muda allá (los usan también el MCP del control, la línea de tiempo y la asamblea).
//
// En QML: Studio.salidas (lista, activa, activaDatos, varias, aire, placa, tope, nueva
// y los comandos).
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <vector>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class JsonListModel;
class MixerModel;
class RecursosViewModel;
class VentanillaMotor;
} // namespace ba::app
namespace ba::app::config {
class InterfazConfig;
}
namespace ba::app::control {
class BusEventos;
class Dsk;
} // namespace ba::app::control
namespace ba::app::emision {
class SeguridadAire;
}
namespace ba::app::escenas {
class Conmutador;
class Conmutadores;
} // namespace ba::app::escenas

namespace ba::app {

class SalidasViewModel : public QObject {
    Q_OBJECT
    /// Las salidas: filas con id, nombre, color, porOmision, alAire, armada,
    /// transmitiendo, grabando, archivo (lo que graba), destinos ([{id, nombre, activo,
    /// reconectando, respaldo}], los suyos), programa (la clave de FrameService de su
    /// programa) y codificador (el que eligió; "" = el de la configuración).
    Q_PROPERTY(QObject* lista READ lista CONSTANT)
    /// El id de la activa.
    Q_PROPERTY(QString activa READ activa WRITE setActiva NOTIFY cambio)
    /// Su fila de `lista`.
    Q_PROPERTY(QVariantMap activaDatos READ activaDatos NOTIFY cambio)
    /// Hay más de una salida: el selector se muestra solo entonces.
    Q_PROPERTY(bool varias READ varias NOTIFY cambio)
    /// Las claves de FrameService del programa y del preview de la activa.
    Q_PROPERTY(QString claveProgramaActiva READ claveProgramaActiva NOTIFY cambio)
    Q_PROPERTY(QString claveArmadaActiva READ claveArmadaActiva NOTIFY cambio)
    /// Cuántas salidas se pueden tener (Config/salidas.json).
    Q_PROPERTY(int tope READ tope NOTIFY cambio)
    /// Si se puede crear otra: {puede, aviso}. `aviso` dice por qué no (el tope) o que
    /// la máquina está justa (el estado de los recursos, lo mismo que sistema_recursos).
    Q_PROPERTY(QVariantMap nueva READ nueva NOTIFY nuevaCambio)
    /// Lo que sale de la activa: {transmitiendo (su principal o algún destino), grabando,
    /// reconectando, alAire (transmisión, grabación o señal limpia: lo que pide confirmar),
    /// tiempoTransmision, tiempoGrabacion}. Con la por omisión, lo de sus pastillas.
    Q_PROPERTY(QVariantMap aire READ aire NOTIFY aireCambio)
    /// El FTB y la placa de la activa: {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    Q_PROPERTY(QVariantMap placa READ placa NOTIFY placaCambio)
    /// El tally para el multiview, el de la activa: {aire: [fuentes en el programa (con las
    /// capas DSK)], armada: [las del preview], borde: {fuente: color}, salidas: {fuente:
    /// [{nombre, color}]} (con varias salidas, en cuáles está al aire cada una)}.
    Q_PROPERTY(QVariantMap tally READ tally NOTIFY tallyCambio)
    /// Los medios que suenan/se ven al aire en la por omisión: [{fuente, restante_s, duracion_s}] (T46).
    Q_PROPERTY(QVariantList mediosAlAire READ mediosAlAire NOTIFY mediosCambio)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla;      ///< pedir y leer la foto del motor
        RecursosViewModel& recursos;      ///< si la máquina da para otra salida
        config::InterfazConfig& interfaz; ///< dónde se recuerda la activa
        /// Lo que sale de la por omisión (las pastillas de la raíz): {transmitiendo,
        /// grabando, reconectando, tiempoTransmision, tiempoGrabacion}.
        std::function<QVariantMap()> airePorOmision;
    };
    explicit SalidasViewModel(Partes p, QObject* parent = nullptr);
    ~SalidasViewModel() override;

    /// Rehace la lista de la foto y avisa siempre listaCambio (al gateway, al monitor): la
    /// primera vez la llama quien la armó, ya conectado (y cuando hay con quién hablar).
    void publicar();
    /// Vuelve a la activa que tenía esta interfaz al cerrarse (si sigue estando).
    void retomarActiva();
    /// En cada vuelta del reloj: lo que está al aire de cada salida, al bus (la escena, la
    /// armada, el tally, los medios, la T-bar; las capas DSK y la mezcla de cada una) y al
    /// multiview (tally, mediosAlAire). Lo que sale de la por omisión lo dicen las pastillas.
    void publicarEnBus(control::BusEventos& bus, MixerModel& tiras, control::Dsk& dsk, bool transmitiendo, bool grabando);
    QVariantMap tally() const { return tally_; }
    QVariantList mediosAlAire() const { return mediosAlAire_; }

    QObject* lista() const;
    QString activa() const;
    void setActiva(const QString& id);
    /// La activa como la anota el motor: "" = la por omisión.
    const std::string& activaId() const { return activa_; }
    QVariantMap activaDatos() const;
    bool varias() const;
    QString claveProgramaActiva() const;
    QString claveArmadaActiva() const;
    int tope() const { return tope_; }
    QVariantMap nueva() const;
    QVariantMap aire() const;
    QVariantMap placa() const;
    /// Las filas de `lista` (para el bus y el tally).
    const QJsonArray& filas() const;
    /// Para las superficies de control (botonera, atajos, control web):
    /// [{id, nombre, color, por_omision}], las que andan, la por omisión primera.
    nlohmann::json paraSuperficies() const;

    /// El conmutador de escenas de la por omisión (modelo): la GUI y el MCP cambian de
    /// escena por ahí.
    escenas::Conmutador& conmutador() { return *conmutador_; }
    /// Los de todas las salidas (uno por salida; el de la por omisión es conmutador()).
    escenas::Conmutadores& conmutadores() { return *conmutadores_; }
    /// El de la activa (el de la por omisión si todavía no hay o se fue).
    escenas::Conmutador& conmutadorActivo() const;
    /// El FTB y la placa de seguridad (modelo): la GUI y el MCP los ponen por ahí.
    emision::SeguridadAire& seguridadAire() { return *seguridadAire_; }

    // Lo que sigue se PIDE al dueño: devuelve "" si se pudo pedir (o por qué no, si se sabe
    // de antemano); lo que falle después lo dice `notify`.
    /// Crear, cambiar y sacar salidas en caliente (AppRuntime).
    Q_INVOKABLE QString crearSalida(const QString& id, const QString& nombre, const QString& color = {});
    Q_INVOKABLE QString editar(const QString& id, const QString& nombre, const QString& color);
    /// El codificador de video de la salida ("" = el de la configuración): rige desde lo
    /// próximo que arranque en ella.
    Q_INVOKABLE QString setCodificador(const QString& id, const QString& codificador);
    /// `susDestinos`: "borrar" o el id de otra salida (con destinos guardados hay que decirlo).
    Q_INVOKABLE QString borrar(const QString& id, const QString& susDestinos = {});
    /// Una salida nueva con su nombre (el id sale del nombre, único) y su color ("" = el
    /// que le toque). Queda como la activa. "" o por qué no.
    Q_INVOKABLE QString crear(const QString& nombre, const QString& color);
    Q_INVOKABLE QString setTope(int tope);
    /// Graba (o deja de grabar) el programa de una salida. "" o por qué no.
    Q_INVOKABLE QString grabar(const QString& id, bool grabar);
    // --- FTB y placa de seguridad (encima de todo lo que sale) ---------------
    // Los dos son de la activa (cada salida tiene su negro y su placa).
    /// Fundido a negro; `audio`: "bajar" (de fábrica) o "mantener". "" o el error.
    Q_INVOKABLE QString ftbUi(bool activar, int ms = 800, const QString& audio = "bajar");
    /// La placa con `texto` (y una imagen opcional). "" o el error.
    Q_INVOKABLE QString placaUi(bool activar, const QString& texto = {}, const QString& imagen = {}, int ms = 600);
    /// Los textos de placa de siempre (y los que se usaron).
    Q_INVOKABLE QStringList textosPlaca() const;

   signals:
    void cambio();
    void nuevaCambio();
    void aireCambio();
    void placaCambio();
    void tallyCambio();
    void mediosCambio();
    /// La interfaz pasa a manejar otra salida: escenas, fuentes, mezcla, tally.
    void activaElegida();
    /// Se rehízo la lista: [{id, nombre, color, porOmision}] para el gateway y el monitor.
    void listaCambio(const QJsonArray& alGateway);
    /// El conmutador de la activa: cambió su escena al aire, su armada o su AFV.
    void alAireActivaCambio(const QString& escena);
    void armadaActivaCambio(const QString& escena);
    void afvActivaCambio();
    /// El de la por omisión (las pantallas de tally de la sala, el monitor «armada»).
    void alAirePorOmisionCambio();
    void armadaPorOmisionCambio(const QString& escena);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// Lo que cambió en la foto del motor: la lista, lo que sale de cada una (y desde
    /// cuándo), la escena al aire y la armada, el AFV, el negro y la placa.
    void alCambiar(unsigned partes);
    /// Las filas de la foto; true si cambiaron (o el tope).
    bool rehacerFilas(QJsonArray* alGateway);
    /// Pide algo que devuelve "" o el error (que vuelve como `notify`).
    void pedir(const char* que, std::function<std::string(AppRuntime&)> orden, std::function<void()> alVolver = {});
    void revisarNueva();

    Partes p_;
    std::unique_ptr<escenas::Conmutadores> conmutadores_; ///< el aire y la armada de cada salida (modelo)
    escenas::Conmutador* conmutador_ = nullptr;           ///< el de la por omisión (de conmutadores_)
    std::unique_ptr<JsonListModel> lista_;
    std::unique_ptr<emision::SeguridadAire> seguridadAire_; ///< FTB y placa (modelo)
    /// La activa: "" = la por omisión.
    std::string activa_;
    int tope_ = 0;
    QVariantMap nuevaAntes_; ///< lo último avisado de nueva
    /// Lo que sale de cada una de las demás salidas y desde cuándo (los relojes de aire).
    struct AireDeSalida {
        bool transmite = false, graba = false, reconecta = false;
        std::chrono::steady_clock::time_point transmiteDesde{}, grabaDesde{};
    };
    std::map<std::string, AireDeSalida> aireSalidas_;
    QVariantMap tally_;
    QVariantList mediosAlAire_;
    QJsonArray alGatewayAntes_;      ///< lo último que se le dijo al gateway
    /// De la activa y de la por omisión, lo último avisado (para avisar solo lo que cambió).
    std::string alAireActivaAntes_, armadaActivaAntes_, alAirePorOmisionAntes_, armadaPorOmisionAntes_;
    std::vector<std::string> afvActivaAntes_;
    nlohmann::json placaAntes_;
};

} // namespace ba::app
