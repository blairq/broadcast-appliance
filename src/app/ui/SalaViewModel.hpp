// ============================================================================
// src/app/ui/SalaViewModel.hpp — el área de la sala: la conferencia y sus invitados (F3-03, partición)
//
// Sale de StudioViewModel: lo que se maneja de la sala (la conferencia del gateway, que
// sigue en ConferenceViewModel) en caliente, cada pieza su modelo (src/app/sala/ y net/):
//
//   CupoSala        cuántos invitados entran (el resultado llega como aviso)
//   ConfigSala      la configuración de la sala en caliente
//   Direcciones     de dónde se entra a cada servicio (la única fuente de los enlaces)
//   VotacionSala    la votación de la sala (con el padrón del evento abierto)
//   PreguntasSala   la cola de preguntas (un mensaje del chat se suma desde acá)
//   SeguridadSala   el PIN por día y el token del MCP (Config/seguridad.json)
//   AccesoRemoto    el túnel (pinggy, Cloudflare, WireGuard) y su dirección pública
//
// También lo que se hace con quien se conecta: aprobar lo que el invitado dijo de sí
// mismo (el cargo de su zócalo), el chat que junta las plataformas (destacar, sumarlo a
// las preguntas), encargarle al asistente que atienda a alguien del lobby, y lo que de la
// sala ven otros: los rótulos (quién habla, quién sigue), la página Enlaces (el lobby, el
// túnel) y el bus (los participantes).
//
// En QML los modelos siguen en Studio (Studio.direcciones, Studio.accesoRemoto); lo
// propio del área en Studio.sala.
// ============================================================================
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>

#include "ui/EnlacesViewModel.hpp"

namespace ba::app {
class AccesoRemoto;
class AppRuntime;
class ChatOverlayRenderer;
class ConferenceViewModel;
class EventoViewModel;
class RecursosViewModel;
} // namespace ba::app
namespace ba::app::control {
class BusEventos;
}
namespace ba::app::sala {
class ConfigSala;
class CupoSala;
class Direcciones;
class PreguntasSala;
class SeguridadSala;
class VotacionSala;
} // namespace ba::app::sala

namespace ba::app {

class SalaViewModel : public QObject {
    Q_OBJECT
   public:
    struct Partes {
        AppRuntime& rt;
        ConferenceViewModel& conferencia; ///< la sala del gateway (su cliente, su foto)
        RecursosViewModel& recursos;      ///< el transporte A→B viaja en la foto de la sala
        std::function<bool()> enVivo;     ///< el PIN y el túnel no se tocan en vivo
        /// Se transmite o se graba (cambiar la dirección pública reinicia la sala).
        std::function<bool()> conAire;
        /// Lo que se arma después del área se pide cada vez.
        std::function<EventoViewModel*()> evento;         ///< el padrón (quién vota)
        std::function<ChatOverlayRenderer*()> chat;       ///< el chat que junta las plataformas
        /// El integrante del Cuerpo con ese nombre (vacío si no está): su nombre y su
        /// cargo validados para el zócalo.
        std::function<nlohmann::json(const std::string& nombre)> enElCuerpo;
    };
    explicit SalaViewModel(Partes p, QObject* parent = nullptr);
    ~SalaViewModel() override;

    sala::CupoSala& cupo() { return *cupoSala_; }
    sala::ConfigSala& config() { return *configSala_; }
    sala::Direcciones& direcciones() { return *direcciones_; }
    sala::VotacionSala& votacion() { return *votacion_; }
    sala::PreguntasSala& preguntas() { return *preguntas_; }
    sala::SeguridadSala& seguridad() { return *seguridad_; }
    AccesoRemoto& accesoRemoto() { return *accesoRemoto_; }
    QObject* direccionesQml() const;
    QObject* accesoRemotoQml() const;

    /// Desde la interfaz: "" o el error inmediato (el resultado llega como aviso).
    Q_INVOKABLE QString cupoUi(int n);
    /// Aprueba lo que el invitado escribió de sí mismo (o un `texto` corregido)
    /// como el cargo de su zócalo. "" o el error.
    Q_INVOKABLE QString aprobarInfoInvitado(const QString& participante, const QString& texto = {});
    /// Chat: los recientes con {key, source, author, text, destacado}; destacar y sacar.
    Q_INVOKABLE QVariantList chatRecientesUi() const;
    Q_INVOKABLE bool destacarChatUi(const QString& key);
    Q_INVOKABLE void quitarDestacadoChatUi(const QString& key);
    /// Suma un mensaje del chat (por su key) a la cola de preguntas. "" o el error.
    Q_INVOKABLE QString preguntaDesdeChatUi(const QString& key);
    /// El operador le encarga al asistente atender a alguien del lobby: que le
    /// pida sus datos por chat y despues le presente todo junto para admitirlo.
    /// Devuelve el error o "".
    Q_INVOKABLE QString assistantIntake(const QString& id, const QString& name, bool yaAdentro);
    /// Lo engancha McpService (necesita el nucleo MCP, que vive afuera).
    void setIntakeSink(std::function<QString(const QString&, const QString&, bool)> fn) { intakeSink_ = std::move(fn); }

    /// Un evento con página web y dirección propia: al ponerlo en curso, la dirección
    /// pública pasa a ser su subdominio (con la API key de pinggy). Reinicia la sala: al
    /// aire no se toca (se avisa).
    void pedirDireccion(const QString& subdominio);
    /// Arrancó una sesión en vivo (el guion corre): la sala cambia de PIN si cambió el día.
    void alEmpezarElVivo();
    /// Los vínculos de los rótulos: la sala en el orden en que la ve el operador
    /// (invitado1, invitado2…), quién habla (orador) y quién sigue (siguiente).
    void completarVinculos(QJsonObject& ctx) const;
    /// Lo de la sala en la página Enlaces: la sala, el túnel, las direcciones y el lobby.
    void alimentarEnlaces(EnlacesViewModel::Fuentes& f);
    /// Que los enlaces se enteren cuando cambia algo del área.
    void avisarA(EnlacesViewModel& e);
    /// Una vez por segundo: los participantes, al bus.
    void publicarParticipantes(control::BusEventos& bus) const;

   signals:
    /// Cambió el cupo de invitados (los monitores muestran ese número de lugares).
    void cupoCambio(int n);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    Partes p_;
    // En el orden en que se usan: lo que usa a otro va después (se destruye antes).
    std::unique_ptr<sala::CupoSala> cupoSala_;
    std::unique_ptr<sala::ConfigSala> configSala_;
    std::unique_ptr<sala::Direcciones> direcciones_; ///< el catálogo de direcciones del gateway
    std::unique_ptr<sala::VotacionSala> votacion_;
    std::unique_ptr<sala::PreguntasSala> preguntas_;
    std::unique_ptr<sala::SeguridadSala> seguridad_;
    std::unique_ptr<AccesoRemoto> accesoRemoto_;
    QHash<QString, QString> infoAprobada_; ///< participante -> cargo aprobado por el operador
    std::function<QString(const QString&, const QString&, bool)> intakeSink_;
};

} // namespace ba::app
