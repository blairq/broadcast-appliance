// ============================================================================
// src/app/ui/AsambleaViewModel.hpp — el área de la asamblea (F3-03, partición)
//
// Sale de StudioViewModel: el cuerpo deliberativo y lo que se arma alrededor de él,
// cada pieza su modelo (src/app/asamblea/):
//
//   CuerpoViewModel  los integrantes, sus bancas y sus fotos
//   Bancas           la fila de oradores presenciales (y los remotos, en la misma fila)
//   Debate           el debate entre dos (de la sala o remotos)
//   ConferenciaDcn   la central de conferencia (plugin bosch-dcn, por el bus)
//   SesionRemota     la banca virtual (integrantes desde el navegador)
//   Quorum           presentes, mayorías y resultados oficiales
//   OrdenDelDia      los puntos de la sesión y el uso de la palabra
//   SeguirMicrofono  la banca que pidió la palabra por la central
//   InformesResultado el resultado de la sesión en PDF
//   Votaciones       las votaciones (central, celulares, Quórum)
//   Presidencia      la consola de la presidencia (y el operador hablándole a un remoto)
//
// Acá se cablean entre ellos y con el resto (cámaras, escenas, motor, sala, bus), y se
// arma lo que de la asamblea ven otros: la página web del evento, los vínculos de los
// rótulos, el anexo del acta y el informe. El reloj del orador (los avisos de tiempo) y
// las ranuras de la fuente «Banca con la palabra» también son de acá.
//
// El motor y los conmutadores son del plano de control (F3-03): lo que la asamblea les hace
// (armar y cortar, abrir un micrófono, las ranuras de la banca, los canales de la sesión
// remota, capturar una foto) se pide por la ventanilla, y lo que lee (qué escena muestra una
// cámara, la fuente de la banca) sale de la foto del motor.
//
// En QML los modelos siguen en Studio (Studio.cuerpo, Studio.bancas…); lo propio del
// área en Studio.asamblea (presidenciaHabla y los mandos del operador).
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class ConferenceViewModel;
class EventoViewModel;
class VentanillaMotor;
}
namespace ba::app::control {
class BusEventos;
}
namespace ba::app::escenas {
class Conmutador;
}
namespace ba::app::ptz {
class ControlCamaras;
}
namespace ba::app::plugins {
class Plugins;
}
namespace ba::app::sala {
class Direcciones;
}
namespace ba::app::asamblea {
class CuerpoViewModel;
class Bancas;
class Debate;
class ConferenciaDcn;
class SesionRemota;
class Votaciones;
class Presidencia;
class SeguirMicrofono;
class OrdenDelDia;
class InformesResultado;
class Quorum;
} // namespace ba::app::asamblea

namespace ba::app {

class AsambleaViewModel : public QObject {
    Q_OBJECT
    /// La presidencia tiene el micrófono abierto en la central («Hablo yo»).
    Q_PROPERTY(bool presidenciaHabla READ presidenciaHabla NOTIFY presidenciaHablaChanged)
   public:
    /// Lo que el área usa del resto de la app.
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla;        ///< el motor: se le pide y se lee su foto
        control::BusEventos& bus;
        ConferenceViewModel& sala;          ///< el cliente del gateway, el micrófono del operador
        sala::Direcciones& direcciones;     ///< de dónde salen los links de la banca virtual
        escenas::Conmutador& conmutador;    ///< el de la salida por omisión (la fila arma y corta ahí)
        ptz::ControlCamaras& camaras;       ///< posiciones, interlock y miniaturas
        std::function<plugins::Plugins*()> plugins; ///< la central es un plugin
        EventoViewModel* evento = nullptr;  ///< el padrón, el acta y la carpeta del evento
        /// Los archivos de la transcripción de la sesión (para el anexo del acta).
        std::function<QStringList()> archivosDeSesion;
    };
    explicit AsambleaViewModel(Partes p, QObject* parent = nullptr);
    ~AsambleaViewModel() override;

    asamblea::CuerpoViewModel& cuerpo() { return *cuerpo_; }
    asamblea::Bancas& bancas() { return *bancas_; }
    asamblea::Debate* debate() { return debate_.get(); }
    asamblea::ConferenciaDcn& dcn() { return *dcn_; }
    asamblea::SesionRemota* sesionRemota() { return sesionRemota_.get(); }
    asamblea::Votaciones* votaciones() { return votaciones_.get(); }
    asamblea::Presidencia* presidencia() { return presidencia_.get(); }
    asamblea::SeguirMicrofono& seguirMicrofono() { return *seguirMic_; }
    asamblea::OrdenDelDia& ordenDelDia() { return *ordenDia_; }
    asamblea::InformesResultado* informes() { return informes_.get(); }
    asamblea::Quorum& quorum() { return *quorum_; }
    QObject* cuerpoQml() const;
    QObject* bancasQml() const;
    QObject* debateQml() const;
    QObject* dcnQml() const;
    QObject* sesionRemotaQml() const;
    QObject* votacionesQml() const;
    QObject* seguirMicrofonoQml() const;
    QObject* ordenDelDiaQml() const;
    QObject* informesQml() const;
    QObject* quorumQml() const;

    bool presidenciaHabla() const;
    /// «Apretar para hablar» desde la app (asamblea::Presidencia): mientras `activo`, el
    /// remoto oye la voz del operador en su celular. "" o el error.
    Q_INVOKABLE QString hablarARemoto(const QString& integrante, bool activo);
    /// La presidencia calla a todos (asamblea::Presidencia); lo que falle se avisa.
    Q_INVOKABLE void silenciarTodos();
    /// «Hablo yo» (asamblea::Presidencia): calla a todos y abre el micrófono de la
    /// presidencia en la central; con false lo cierra. Lo que falle se avisa.
    Q_INVOKABLE void hablaPresidencia(bool activo);

    /// Los oradores remotos con el tiempo que hablaron (de la sala): [{nombre, segundos}].
    nlohmann::json remotosPalabra() const;
    /// Lo de la asamblea para la página web del evento: {oradores, ordenDelDia, quorum,
    /// asamblea}; cada cosa solo si hay algo (la página no muestra lo que no viene).
    nlohmann::json paraLaWeb() const;
    /// Suma a los vínculos de los rótulos lo de la asamblea: el punto en tratamiento, el
    /// orador presencial (manda sobre el de la sala) con su cronómetro, el quórum y la
    /// última votación oficial. `ctx` ya trae lo de la sala.
    void completarVinculos(QJsonObject& ctx) const;
    /// En el reloj de la interfaz (10 Hz): el tiempo del que tiene la palabra.
    void tic();

   signals:
    void presidenciaHablaChanged();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    void armarBancas();
    void armarSesion();
    void armarInformes();
    /// Lo que va en cada ranura de la fuente «Banca con la palabra» (1 = quien tiene la
    /// palabra; en un debate, A y B). Se recalcula con cada cambio de la fila, del
    /// debate, del cuerpo o de las posiciones de las cámaras.
    void actualizarRanurasBanca(bool forzar = false);

    Partes p_;
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<asamblea::CuerpoViewModel> cuerpo_;
    std::unique_ptr<asamblea::SeguirMicrofono> seguirMic_;
    std::unique_ptr<asamblea::OrdenDelDia> ordenDia_;
    std::unique_ptr<asamblea::InformesResultado> informes_;
    std::unique_ptr<asamblea::Quorum> quorum_;
    std::unique_ptr<asamblea::Bancas> bancas_;
    std::unique_ptr<asamblea::Debate> debate_;
    nlohmann::json centralAntesDelDebate_; ///< modo y máximo de abiertos para devolverle a la central
    std::unique_ptr<asamblea::ConferenciaDcn> dcn_;
    std::unique_ptr<asamblea::SesionRemota> sesionRemota_;
    std::unique_ptr<asamblea::Votaciones> votaciones_; ///< usa sesionRemota_, dcn_, quorum_ y el bus
    std::unique_ptr<asamblea::Presidencia> presidencia_; ///< usa casi todo lo de arriba
    /// El orador presencial cuyo tiempo se lleva en la central (banca@desde) y los avisos ya dados.
    std::string dcnOrador_;
    int dcnAvisos_ = 0;
    int dcnTic_ = 0;
    QTimer ranurasTarde_; ///< una vuelta más, cuando ya se sabe el tamaño de la cámara
};

} // namespace ba::app
