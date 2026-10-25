// ============================================================================
// src/app/ui/AtencionViewModel.hpp — la atención a quien se conecta (Sala y Asamblea)
//
// «Los atiende el asistente» y, prendido, «Hablando» (docs/funciones/ATENCION.md).
// Las reglas viven en sala/Atencion (sin Qt, con test); esto las conecta con lo
// que ya existe, sin un sistema aparte:
//
//   SALA. El encargo es el de siempre (McpService, setIntakeSink): el que
//   atiende (rol «recepcion» o, si no, el asistente de automatización; nunca el
//   del evento) charla con `chat_send` y `chat_wait`. Con «Hablando», además:
//     - la sala sube la voz de esa persona (`atiende` en su ruta, gateway);
//       llega por su lugar del lobby (OperatorFeed::escucharLobby), se pasa a
//       texto con el MISMO reconocedor del dictado (agent::Voz, un oído propio)
//       y entra a su hilo del chat como un mensaje suyo (`voz: true`): el que
//       atiende lo lee con `chat_wait` como si lo hubiera escrito;
//     - lo que se le escribe a esa persona (ControlSala::chatear, venga del
//       que atiende o del operador) se dice además con «la voz del atendedor»
//       (audio/Habla) y le llega por BA.Audio.Atendedor (emision/VozAlLobby):
//       solo a ella, nunca al aire ni a los demás del lobby.
//   Termina al admitirla (o al irse): la atención es del lobby.
//
//   ASAMBLEA. El que espera en la sesión remota no tiene chat: atender es
//   mirarle la cara (la foto de su celular) contra su ficha. Si es quien dice
//   ser y el padrón del evento lo deja entrar solo, se lo habilita y se avisa;
//   si no, se le encarga al que atiende que le pregunte al operador
//   (`operator_ask`) con lo que se vio. Con «Hablando» (de a uno, la MISMA voz
//   que la sala), además:
//     - el gateway le pide a su página el micrófono (`/api/v1/sesion/atiende`):
//       su voz sube a BA.Audio.Atencion (OperatorFeed::escucharAtencionRemota),
//       pasa a texto con el mismo oído y entra al hilo del chat como un dicho
//       suyo; el que atiende la lee con `chat_wait {participant: <id>}`;
//     - lo que el que atiende le manda con `chat_send {to: <id>}` (decirAlIntegrante)
//       se le dice con la voz del atendedor, que el gateway le baja SOLO a él, y
//       su página lo muestra escrito.
//   Termina al habilitarlo (o al irse), como en la sala.
//
//   PERSONAS. Con la atención de la sala prendida, el lobby se mira siempre
//   (PersonasViewModel::setLobbyPorAtencion), y el alta automática anota a
//   quien dio su nombre (opción de Personas, visible acá también).
//
// La lista de quién entra solo si se lo reconoce es la del PADRÓN del evento
// (`dejarPasarSolo`): no hay otra. La interfaz la muestra en Sala, Asamblea y
// Personas; por MCP, padron_ver {solos: true} / padron_cambiar.
// ============================================================================
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "sala/Atencion.hpp"

namespace ba::app {

class AppRuntime;
class StudioViewModel;
class VozAlLobby;

namespace audio {
class Habla;
}
namespace agent {
class Voz;
class ModelosLocales;
} // namespace agent

class AtencionViewModel : public QObject {
    Q_OBJECT
    /// Sala: «Los atiende el asistente» (se guarda con la sala; ver ControlSala).
    Q_PROPERTY(bool salaAtiende READ salaAtiende WRITE setSalaAtiende NOTIFY cambio)
    /// Sala: «Hablando» (solo cuenta con «Los atiende» prendido).
    Q_PROPERTY(bool salaHablando READ salaHablando WRITE setSalaHablando NOTIFY cambio)
    /// Asamblea: «Los atiende el asistente».
    Q_PROPERTY(bool asambleaAtiende READ asambleaAtiende WRITE setAsambleaAtiende NOTIFY cambio)
    /// Asamblea: «Hablando» (solo cuenta con «Los atiende» prendido): mientras
    /// espera, su página abre el micrófono hacia el gateway.
    Q_PROPERTY(bool asambleaHablando READ asambleaHablando WRITE setAsambleaHablando NOTIFY cambio)
    /// "recepcion" | "automatizacion" | "" (nadie): quién atendería ahora.
    Q_PROPERTY(QString quienAtiende READ quienAtiende NOTIFY cambio)
    /// Lo mismo, para leer: «Atiende el rol de recepción», o por qué nadie.
    Q_PROPERTY(QString quienAtiendeTexto READ quienAtiendeTexto NOTIFY cambio)
    /// Están las dos puntas de la voz: el reconocedor (voz a texto) y la voz del atendedor.
    Q_PROPERTY(bool puedeHablar READ puedeHablar NOTIFY cambio)
    /// Qué falta para poder atender hablando ("" si nada).
    Q_PROPERTY(QString hablarFalta READ hablarFalta NOTIFY cambio)
    /// Los que se están atendiendo: [{id, nombre, ambito, quien, modo}].
    Q_PROPERTY(QVariantList turnos READ turnos NOTIFY turnosCambio)
    // --- La voz del atendedor -------------------------------------------------
    /// Las voces del catálogo (tipo «tts»): {repo, nombre, calidad, instalado, elegida, …}.
    Q_PROPERTY(QVariantList voces READ voces NOTIFY cambio)
    Q_PROPERTY(QString vozElegida READ vozElegida NOTIFY cambio)
    Q_PROPERTY(bool vozLista READ vozLista NOTIFY cambio)
    Q_PROPERTY(int hablante READ hablante WRITE setHablante NOTIFY cambio)
    Q_PROPERTY(int hablantes READ hablantes NOTIFY cambio)
    /// 0,7 (más lento) … 1,5 (más rápido).
    Q_PROPERTY(double velocidad READ velocidad WRITE setVelocidad NOTIFY cambio)
    /// La última prueba (sin sonar): {probando, texto, segundos, forma: [0..1], archivo, error}.
    Q_PROPERTY(QVariantMap prueba READ prueba NOTIFY pruebaCambio)

   public:
    AtencionViewModel(AppRuntime& rt, StudioViewModel& vm, agent::Voz& voz, agent::ModelosLocales& modelos,
                      QObject* parent = nullptr);
    ~AtencionViewModel() override;

    bool salaAtiende() const;
    void setSalaAtiende(bool on);
    bool salaHablando() const { return aj_.salaHablando; }
    void setSalaHablando(bool on);
    bool asambleaAtiende() const { return aj_.asambleaAtiende; }
    void setAsambleaAtiende(bool on);
    bool asambleaHablando() const { return aj_.asambleaHablando; }
    void setAsambleaHablando(bool on);
    QString quienAtiende() const { return QString::fromLatin1(sala::claveDe(atendedor())); }
    QString quienAtiendeTexto() const;
    bool puedeHablar() const { return hablarFalta().isEmpty(); }
    QString hablarFalta() const;
    QVariantList turnos() const;

    QVariantList voces() const;
    QString vozElegida() const;
    bool vozLista() const { return vozLista_; }
    int hablante() const { return aj_.vozHablante; }
    void setHablante(int h);
    int hablantes() const { return hablantes_; }
    double velocidad() const { return aj_.vozVelocidad; }
    void setVelocidad(double v);
    QVariantMap prueba() const { return prueba_; }

    /// Elegir la voz del atendedor (tiene que estar bajada).
    Q_INVOKABLE void elegirVoz(const QString& repo);
    /// Genera `texto` con la voz del atendedor SIN reproducirlo: deja la
    /// duración, la forma de onda y un .wav (Logs/voz-atendedor-prueba.wav) en
    /// `prueba`. Vacío = una frase de ejemplo.
    Q_INVOKABLE void probarVoz(const QString& texto);
    /// Lo mismo con aviso al terminar (el MCP): `fin(prueba)`.
    void probar(const QString& texto, std::function<void(const QVariantMap&)> fin);

    // --- Para el encargo (McpService) y el MCP --------------------------------------------
    /// Quién atiende ahora (la regla de sala::quienAtiende con lo que está prendido).
    sala::Atendedor atendedor() const;
    /// El encargo empezó a atender a alguien del lobby de la sala: arma la voz
    /// si corresponde. Devuelve el modo ("voz" | "chat").
    QString empezarSala(const QString& id, const QString& nombre, sala::Atendedor quien);
    /// Lo que se le agrega al encargo según el modo (vacío para "chat").
    static QString instruccion(const QString& modo);
    /// ¿Se atiende hablando a `id`, que espera en la sesión remota? (chat_send y
    /// chat_wait lo aceptan aunque no esté en la sala.)
    bool atiendeHablandoEnAsamblea(const QString& id) const;
    /// Decirle `texto` al que espera en la sesión remota (chat_send {to: <id>}): con la
    /// voz del atendedor, solo a él, y escrito en su página. "" o el error.
    QString decirAlIntegrante(const QString& id, const QString& texto);
    /// Todo junto, para `atencion_estado`.
    nlohmann::json estado() const;
    /// Cambia lo que venga ({salaAtiende, salaHablando, asambleaAtiende,
    /// asambleaHablando, altaAutomatica, voz: {repo, hablante, velocidad}}). "" o el error.
    QString configurar(const nlohmann::json& cambios);

   signals:
    void cambio();
    void turnosCambio();
    void pruebaCambio();
    void notify(const QString& texto, const QString& nivel);

   private:
    void guardar() const;
    void revisarVoz();
    /// La sala cambió: termina los turnos de quien ya no espera, pasa la voz y
    /// sigue a la persona si le cambió el lugar del lobby.
    void alSala();
    /// Arma (o desarma) la voz para `id` (de la sala o de la asamblea: lo dice su turno).
    void abrirVoz(const QString& id);
    void cerrarVoz();
    /// La voz está libre: la toma el siguiente que se atiende sin ella y puede.
    void pasarLaVoz();
    /// No se le pudo armar la voz a `id`: sigue sin ella (y no se le reintenta).
    void sinVoz(const QString& id, const QString& error);
    /// Se le escribió a alguien: si se lo atiende hablando, se le dice.
    void alChatear(const std::string& para, const std::string& texto);
    void decir(const QString& texto);
    /// La sesión remota cambió: atiende a los que esperan habilitación.
    void alAsamblea();
    void alFotoIntegrante(const QString& id);
    void encargarAsamblea(const QString& id, const QString& queSeVio);
    int lugarDe(const QString& id) const;
    /// Para pasar la voz: su ámbito tiene «Hablando» prendido y tiene por dónde
    /// (en la sala, lugar en el lobby; en la asamblea, que siga esperando conectado).
    std::function<bool(const std::string&)> puedeTomarLaVoz() const;
    bool esperaEnAsamblea(const std::string& id) const;

    AppRuntime& rt_;
    StudioViewModel& vm_;
    agent::Voz& voz_;
    agent::ModelosLocales& modelos_;
    sala::AjustesAtencion aj_;
    sala::Turnos turnos_;

    std::unique_ptr<audio::Habla> habla_;
    std::unique_ptr<VozAlLobby> alLobby_;
    QString vozCargada_;
    bool vozLista_ = false;
    int hablantes_ = 0;
    QVariantMap prueba_;

    // La voz que está armada (una por vez: ver sala::Turnos).
    QString vozId_;      ///< a quién
    sala::Ambito vozAmbito_ = sala::Ambito::Sala; ///< dónde está
    int vozLugar_ = 0;   ///< en qué lugar del lobby se lo escucha (sala)
    int vozToma_ = 0;    ///< la toma de OperatorFeed::escucharLobby / escucharAtencionRemota

    // Asamblea: a quién se le pidió la foto y todavía no llegó.
    QSet<QString> esperandoFoto_;
};

} // namespace ba::app
