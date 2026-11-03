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
#include "ui/CapturaError.hpp"

namespace ba::app {
class AppRuntime;
class EstadoAireViewModel;
struct FotoMotor;
class JsonListModel;
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
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// Para las acciones que dicen si se hicieron: el error a quien mira y true si no hubo.
    bool sinError(const QString& e) {
        avisarError(e);
        return e.isEmpty();
    }
    bool sinError(const std::string& e) { return sinError(QString::fromStdString(e)); }
    /// Las salidas: filas con id, nombre, color, porOmision, alAire, armada,
    /// transmitiendo, grabando, archivo (lo que graba), destinos ([{id, nombre, activo,
    /// reconectando, respaldo}], los suyos), programa (la clave de FrameService de su
    /// programa) y codificador (el que eligió; "" = el de la configuración).
    Q_PROPERTY(QObject* lista READ lista CONSTANT)
    /// Las mismas filas como lista (para buscar en un binding sin pedirle fila por fila al
    /// modelo: el «Mute de» de la matriz). Avisa con `cambio`.
    Q_PROPERTY(QVariantList filasLista READ filasLista NOTIFY cambio)
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
    /// transmiteDesdeMs, grabaDesdeMs} (el tiempo lo cuenta la vista). Con la por omisión, lo de
    /// sus pastillas.
    Q_PROPERTY(QVariantMap aire READ aire NOTIFY aireCambio)
    /// El FTB y la placa de la activa: {ftb, ftb_audio, placa, placa_texto, placa_imagen}.
    Q_PROPERTY(QVariantMap placa READ placa NOTIFY placaCambio)
    /// El tally para el multiview, el de la activa: {aire: [fuentes en el programa (con las
    /// capas DSK)], armada: [las del preview], borde: {fuente: color}, salidas: {fuente:
    /// [{nombre, color}]} (con varias salidas, en cuáles está al aire cada una)}.
    Q_PROPERTY(QVariantMap tally READ tally NOTIFY tallyCambio)
    /// Los medios que suenan/se ven al aire en la por omisión: [{fuente, restante_s, duracion_s}] (T46).
    Q_PROPERTY(QVariantList mediosAlAire READ mediosAlAire NOTIFY mediosCambio)
    /// Los textos de placa de siempre, en el idioma del evento (salen al aire), y el último
    /// que se usó primero.
    Q_PROPERTY(QStringList textosPlaca READ textosPlaca NOTIFY textosPlacaCambio)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla;      ///< pedir y leer la foto del motor
        RecursosViewModel& recursos;      ///< si la máquina da para otra salida
        config::InterfazConfig& interfaz; ///< dónde se recuerda la activa
        /// Lo que sale de la por omisión (las pastillas de la raíz) y su reloj de 1 Hz.
        EstadoAireViewModel& aire;
    };
    explicit SalidasViewModel(Partes p, QObject* parent = nullptr);
    ~SalidasViewModel() override;

    /// Rehace la lista de la foto y avisa siempre listaCambio (al gateway, al monitor): la
    /// primera vez la llama quien la armó, ya conectado (y cuando hay con quién hablar).
    void publicar();
    /// Vuelve a la activa que tenía esta interfaz al cerrarse (si sigue estando).
    void retomarActiva();
    /// Cuando cambia la foto, el aire o el DSK: lo que está al aire de cada salida, al bus (la
    /// escena, la armada, el tally, los medios, la T-bar y las capas DSK de cada una) y al
    /// multiview (tally, mediosAlAire). Lo que sale de la por omisión lo dicen las pastillas;
    /// los niveles de cada mezcla van a pedido (StudioViewModel::publicarNivelesEnBus).
    void publicarEnBus(control::BusEventos& bus, control::Dsk& dsk, bool transmitiendo, bool grabando);
    QVariantMap tally() const { return tally_; }
    QVariantList mediosAlAire() const { return mediosAlAire_; }

    QObject* lista() const;
    QVariantList filasLista() const { return filas().toVariantList(); }
    QString activa() const;
    void setActiva(const QString& id);
    /// La activa como la anota el motor: "" = la por omisión.
    const std::string& activaId() const { return hayContexto_ ? contexto_ : activa_; }
    /// Mientras dure: la salida «activa» es `salida` para todo lo que la lee (los getters, lo
    /// que hacen los botones), SIN avisar ni cambiar la de esta interfaz. Es cómo la app atiende
    /// a un aparato del tablero web que maneja otra salida (control/ReplicaPublicador). ""
    /// = la por omisión. Devuelve con qué volver a lo de antes.
    std::function<void()> contexto(const std::string& salida) {
        const bool habia = hayContexto_;
        std::string antes = contexto_;
        hayContexto_ = true;
        contexto_ = salida == "principal" ? std::string() : salida; // la por omisión se anota ""
        return [this, habia, antes]() {
            hayContexto_ = habia;
            contexto_ = antes;
        };
    }
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
    Q_INVOKABLE void editar(const QString& id, const QString& nombre, const QString& color);
    /// El codificador de video de la salida ("" = el de la configuración): rige desde lo
    /// próximo que arranque en ella.
    Q_INVOKABLE void setCodificador(const QString& id, const QString& codificador);
    /// `susDestinos`: "borrar" o el id de otra salida (con destinos guardados hay que decirlo).
    Q_INVOKABLE bool borrar(const QString& id, const QString& susDestinos = {});
    /// Lo mismo, para quien necesita el resultado (el MCP): `listo(error)` en este hilo, con
    /// la foto ya al día. `tope` >= 0 cambia también cuántas salidas puede haber (antes de
    /// los cambios); `cambios` vacío no cambia la salida.
    using Listo = std::function<void(const std::string& error)>;
    void crearSalida(const std::string& id, const std::string& nombre, const std::string& color, Listo listo);
    void editar(const std::string& id, const nlohmann::json& cambios, int tope, Listo listo);
    void borrar(const std::string& id, const std::string& susDestinos, Listo listo);
    /// Una salida nueva con su nombre (el id sale del nombre, único) y su color ("" = el
    /// que le toque). Queda como la activa. "" o por qué no.
    Q_INVOKABLE bool crear(const QString& nombre, const QString& color);
    Q_INVOKABLE bool setTope(int tope);
    /// Graba (o deja de grabar) el programa de una salida. "" o por qué no.
    Q_INVOKABLE void grabar(const QString& id, bool grabar);
    // --- FTB y placa de seguridad (encima de todo lo que sale) ---------------
    // Los dos son de la activa (cada salida tiene su negro y su placa).
    /// Fundido a negro; `audio`: "bajar" (de fábrica) o "mantener". El error va por la señal del modelo.
    Q_INVOKABLE void ftbUi(bool activar, int ms = 800, const QString& audio = "bajar");
    /// La placa con `texto` (y una imagen opcional). El error va por la señal del modelo.
    Q_INVOKABLE void placaUi(bool activar, const QString& texto = {}, const QString& imagen = {}, int ms = 600);
    QStringList textosPlaca() const { return textosPlaca_; }
    /// Rearma los textos de placa (cambió el idioma del evento o la placa usada) y avisa si cambiaron.
    void revisarTextosPlaca();
    /// Los de siempre, con `usado` (la placa que está o estuvo puesta) primero si no es uno de
    /// ellos. Sin la vista: lo usa el MCP desde el dueño del motor (aire_placas_listar).
    static QStringList textosDePlaca(const QString& usado);

   signals:
    void cambio();
    void nuevaCambio();
    void aireCambio();
    void placaCambio();
    void tallyCambio();
    void mediosCambio();
    void textosPlacaCambio();
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
    /// Con el error para quien llama (en vez de `notify`).
    void pedir(const char* que, std::function<std::string(AppRuntime&)> orden, Listo listo);
    void revisarNueva();
    /// Rehace `aire` (de la foto y la hora) y avisa si cambió.
    void revisarAire();
    static QStringList textosPlacaDe(const FotoMotor& f);

    Partes p_;
    std::unique_ptr<escenas::Conmutadores> conmutadores_; ///< el aire y la armada de cada salida (modelo)
    escenas::Conmutador* conmutador_ = nullptr;           ///< el de la por omisión (de conmutadores_)
    std::unique_ptr<JsonListModel> lista_;
    std::unique_ptr<emision::SeguridadAire> seguridadAire_; ///< FTB y placa (modelo)
    /// La activa: "" = la por omisión.
    std::string activa_;
    std::string contexto_;   ///< la de contexto() (un aparato del tablero web)
    bool hayContexto_ = false;
    int tope_ = 0;
    QVariantMap nuevaAntes_; ///< lo último avisado de nueva
    QVariantMap aire_;            ///< lo último avisado de aire
    QStringList textosPlaca_;     ///< lo último avisado de textosPlaca
    QVariantMap tally_;
    QVariantList mediosAlAire_;
    QJsonArray alGatewayAntes_;      ///< lo último que se le dijo al gateway
    /// De la activa y de la por omisión, lo último avisado (para avisar solo lo que cambió).
    std::string alAireActivaAntes_, armadaActivaAntes_, alAirePorOmisionAntes_, armadaPorOmisionAntes_;
    std::vector<std::string> afvActivaAntes_;
    nlohmann::json placaAntes_;
};

} // namespace ba::app
