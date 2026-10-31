// ============================================================================
// src/app/ui/AsistenteViewModel.hpp — el área del asistente y la voz (F3-03, partición)
//
// Sale de StudioViewModel: todo lo que habla, escucha o escribe por la app, cada pieza
// su modelo (src/app/agent/):
//
//   AgentViewModel (×2)   el asistente del evento y el de automatización (atiende las alertas)
//   Roles / Despachador   los roles automáticos y sus corridas (un disparo reclamado va a un rol)
//   ModelosLocales        el modelo local (llama-server, Modelos/), motor de los anteriores
//   Voz / VozPrompter     hablarle al asistente; el guion dicho en voz alta
//   FlujosTexto           las salidas de texto (voz a texto con nombre), con su corrector web
//                         (PuestoCorreccion), sus cuadros «Subtítulos» y los subtítulos ocultos
//   VocesRack / Traductor las tarjetas «Texto a voz» y la traducción en vivo
//   EstadoIA              qué parte de la IA no anda (la pastilla «IA», ia_estado)
//   Vocabulario           lo que el reconocedor tiene que escribir bien (los nombres del cuerpo)
//   OperatorMessages      la barra de mensajes del asistente al operador
//   ManualesViewModel     los manuales de usuario que arma el asistente
//
// Acá se cablean entre ellos (el modelo local como motor, la voz con los flujos, las
// respuestas tardías del operador a quien las pidió) y con los racks de audio (de dónde
// sale la voz y las tarjetas de cada salida de texto). El rótulo «Traducción automática»
// mientras sale una traducción también es de acá.
//
// En QML los modelos siguen en Studio (Studio.agent, Studio.voz…); lo propio del área
// en Studio.asistente (el vocabulario, el rótulo de traducción).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

namespace ba::app {
class AppRuntime;
class AlarmasViewModel;
class ConferenceViewModel;
class EventoViewModel;
class ManualesViewModel;
class OperatorMessages;
class PuestoCorreccion;
class VentanillaMotor;
} // namespace ba::app
namespace ba::app::agent {
class AgentViewModel;
class Despachador;
class EstadoIA;
class FlujosTexto;
class ModelosLocales;
class Roles;
class RotuloTraduccionConfig;
class Subtitulos;
class SubtitulosCC;
class Traductor;
class Vocabulario;
class VocesRack;
class Voz;
class VozPrompter;
} // namespace ba::app::agent
namespace ba::app::asamblea {
class Cuerpo;
}
namespace ba::app::audio {
class RackManager;
}
namespace ba::app::control {
class Dsk;
}

namespace ba::app {

class AsistenteViewModel : public QObject {
    Q_OBJECT
   public:
    struct Partes {
        AppRuntime& rt;
        /// El motor (F3-03): se le pide (los CC, el rótulo de la traducción) y se lee su foto.
        VentanillaMotor& ventanilla;
        AlarmasViewModel& alarmas;  ///< el vigía que atienden los asistentes
        ConferenceViewModel& sala;  ///< el chat a los invitados, el micrófono del operador, el puesto de corrección
        EventoViewModel* evento = nullptr; ///< la carpeta del evento (las transcripciones)
        std::function<bool()> enVivo; ///< los manuales no se arman en vivo
        /// El formulario seguro de credenciales (lo que el asistente pide que se complete).
        std::function<QString(const QString& plataforma, const QVariantMap& valores)> guardarCredencial;
        /// Lo que se arma después del área se pide cada vez.
        std::function<control::Dsk*()> dsk;                  ///< la capa del rótulo de traducción
        std::function<const asamblea::Cuerpo*()> cuerpo;     ///< los nombres para el vocabulario
    };
    explicit AsistenteViewModel(Partes p, QObject* parent = nullptr);
    ~AsistenteViewModel() override;

    /// Los racks de audio (se montan después): la voz sale de sus nodos «Voz a texto»,
    /// las salidas de texto son sus tarjetas y las traducidas, cada traducción es una
    /// tarjeta «Traducir».
    void conRacks(audio::RackManager& racks);

    agent::AgentViewModel& agente() { return *agent_; }
    agent::AgentViewModel& agenteAutomatizacion() { return *agenteAuto_; }
    agent::Roles& roles() { return *roles_; }
    agent::Despachador& despachador() { return *despachador_; }
    agent::ModelosLocales& modelos() { return *modelos_; }
    agent::Voz& voz() { return *voz_; }
    agent::FlujosTexto& flujos() { return *flujos_; }
    agent::SubtitulosCC& cc() { return *cc_; }
    agent::Traductor* traductor() { return traductor_.get(); }
    agent::EstadoIA& estadoIA() { return *estadoIA_; }
    agent::Vocabulario& vocabulario() { return *vocabulario_; }
    OperatorMessages& mensajes() { return *messages_; }
    QObject* agenteQml() const;
    QObject* agenteAutomatizacionQml() const;
    QObject* despachadorQml() const;
    QObject* modelosQml() const;
    QObject* vozQml() const;
    QObject* vozPrompterQml() const;
    QObject* flujosQml() const;
    QObject* puestoCorreccionQml() const;
    QObject* ccQml() const;
    QObject* vocesQml() const;
    QObject* traductorQml() const;
    QObject* estadoIAQml() const;
    QObject* mensajesQml() const;
    QObject* manualesQml() const;

    /// Vocabulario del reconocedor de voz (agent/Vocabulario): grupos, términos,
    /// probar una frase y traer los nombres del Cuerpo.
    Q_INVOKABLE QVariantMap vocabularioUi() const;
    Q_INVOKABLE QString vocabularioAgregarUi(const QString& termino, const QString& variantes, const QString& grupo);
    Q_INVOKABLE bool vocabularioQuitarUi(const QString& termino);
    Q_INVOKABLE void vocabularioGrupoUi(const QString& grupo, bool activo);
    /// {texto, reemplazos: [{original, reemplazo}]}.
    Q_INVOKABLE QVariantMap vocabularioProbarUi(const QString& texto);
    /// Pone los nombres y apellidos del Cuerpo en el grupo «cuerpo» (agent::Vocabulario::cargarCuerpo)
    /// y avisa cuántos. Devuelve cuántos.
    Q_INVOKABLE int vocabularioDelCuerpoUi();

    /// «Traducción automática»: mientras salga una traducción en pantalla o en CC, una capa DSK lo rotula.
    Q_INVOKABLE bool rotuloTraduccionActivo() const;
    Q_INVOKABLE void setRotuloTraduccion(bool activo);
    nlohmann::json rotuloTraduccionEstado() const;

   signals:
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    void armarAsistentes();
    void armarMensajes();
    void armarTextos();
    void rotularTraduccion(const QString& flujo, const QString& origen);
    /// El rótulo `nombre` ya está en el motor: arma su capa (una vez) y la muestra.
    void mostrarRotulo(control::Dsk& dsk, const std::string& nombre);

    Partes p_;
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<agent::Vocabulario> vocabulario_;
    std::unique_ptr<agent::RotuloTraduccionConfig> rotulo_; ///< Config/rotulo-traduccion.json (modelo)
    std::unique_ptr<agent::FlujosTexto> flujos_;
    std::unique_ptr<PuestoCorreccion> puestoCorreccion_;
    std::unique_ptr<agent::Subtitulos> subtitulos_;
    std::unique_ptr<agent::SubtitulosCC> cc_;
    std::unique_ptr<agent::ModelosLocales> modelos_;
    std::unique_ptr<agent::Roles> roles_;
    std::unique_ptr<agent::AgentViewModel> agent_;
    std::unique_ptr<agent::AgentViewModel> agenteAuto_;
    std::unique_ptr<agent::Despachador> despachador_;
    std::unique_ptr<ManualesViewModel> manuales_;
    std::unique_ptr<agent::Voz> voz_;
    std::unique_ptr<agent::VozPrompter> vozPrompter_;
    std::unique_ptr<agent::VocesRack> voces_;
    std::unique_ptr<agent::Traductor> traductor_;
    std::unique_ptr<agent::EstadoIA> estadoIA_;
    std::unique_ptr<OperatorMessages> messages_;
    QTimer rotuloApagar_; ///< el rótulo se va después de la última frase traducida
};

} // namespace ba::app
