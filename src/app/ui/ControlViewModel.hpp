// ============================================================================
// src/app/ui/ControlViewModel.hpp — el área de las superficies de control (F3-03, partición)
//
// Sale de StudioViewModel: lo que maneja la app desde afuera de la interfaz, cada
// pieza su modelo (src/app/control/):
//
//   Botonera / BotoneraGateway   las tablets y el hardware (Stream Deck, MIDI de los
//                                plugins), con sus perfiles y su emparejamiento
//   CarasBotonera                las caras de los botones (foto o video corto)
//   SatelliteServidor            Companion Satellite manejando la botonera
//   ControlWeb / ControlWebGateway  el dashboard /control/ y sus aparatos
//   Dsk                          las capas encima del programa (logo, intérprete, zócalo)
//   Acciones                     el catálogo de acciones y los atajos de teclado
//
// Acá se arman sus ganchos (escenas, fuentes, salidas, bancas, caras), se las conecta
// con el gateway y los plugins, y se arma lo que de ellas muestra la página Enlaces
// (los pedidos de aprobación por aparato). El tema de las páginas web de control
// también es de acá (se lo pasa al gateway).
//
// En QML los modelos siguen en Studio (Studio.botonera, Studio.satellite…); lo propio
// del área en Studio.control (el tema web, emparejar, la API remota).
//
// El motor es del plano de control (F3-03, docs/arquitectura/05-motor-del-plano.md): el
// área no lo toca. Lo que muestra sale de la foto (las escenas, las capas DSK y el tema web:
// la parte Superficies) y de la mezcla de la interfaz (MixerModel: las fuentes de audio y su
// mudo); lo que cambia se pide por la ventanilla. Las capas DSK son del dueño (CapasDsk) y la
// interfaz las ve por control::Dsk. Lo que hacen la botonera, el Satellite, el control web y
// los atajos va por el mismo camino que el asistente: las herramientas (McpCore::callTool,
// GuiMode), que corren donde declara cada una.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/EnlacesViewModel.hpp"

namespace ba::app {
class AppRuntime;
class AsambleaViewModel;
class ConferenceViewModel;
class MixerModel;
class PdfEscena;
class VentanillaMotor;
} // namespace ba::app
namespace ba::app::control {
class BusEventos;
class Botonera;
class BotoneraGateway;
class CarasBotonera;
class ControlWeb;
class ControlWebGateway;
class SatelliteServidor;
class CapasDsk;
class Dsk;
class Acciones;
} // namespace ba::app::control
namespace ba::app::ptz {
class ControlCamaras;
}
namespace ba::app::plugins {
class Plugins;
}
namespace ba::app::sala {
class Direcciones;
}

namespace ba::app {

class ControlViewModel : public QObject {
    Q_OBJECT
    /// El tema de las interfaces web de control (Servicios → Templates, Configuración).
    Q_PROPERTY(QString temaWeb READ temaWeb WRITE setTemaWeb NOTIFY temaWebChanged)
    /// [{id, nombre, descripcion, fondo, superficie, texto, acento}] (control/TemasWeb.hpp).
    Q_PROPERTY(QVariantList temasWeb READ temasWeb CONSTANT)
    /// Lo que se puede poner en una capa DSK: escenas y fuentes, por nombre (de la foto).
    Q_PROPERTY(QStringList nombresParaDsk READ nombresParaDsk NOTIFY nombresParaDskChanged)
   public:
    /// Lo que el área usa del resto de la app.
    struct Partes {
        AppRuntime& rt; ///< solo lo usan las órdenes que van al dueño (y dataRoot)
        VentanillaMotor& ventanilla;
        control::BusEventos& bus;
        ConferenceViewModel& sala;       ///< el cliente del gateway (tablets, control web, tema)
        sala::Direcciones& direcciones;  ///< la puerta HTTP de la API remota
        AsambleaViewModel& asamblea;     ///< las bancas, el recinto y las fotos de los integrantes
        ptz::ControlCamaras* camaras = nullptr; ///< las miniaturas de las posiciones
        plugins::Plugins* plugins = nullptr;    ///< sus aparatos de hardware son botoneras más
        PdfEscena* pdfEscena = nullptr;         ///< los PDF que se pueden pasar desde un botón
        MixerModel* tiras = nullptr;            ///< las fuentes de audio y su mudo en cada salida
        /// Las salidas: [{id, nombre, color, por_omision}], la por omisión primera.
        std::function<nlohmann::json()> salidas;
    };
    explicit ControlViewModel(Partes p, QObject* parent = nullptr);
    ~ControlViewModel() override;

    control::Botonera& botonera() { return *botonera_; }
    control::BotoneraGateway& botoneraGateway() { return *botoneraGw_; }
    control::CarasBotonera& carasBotonera() { return *carasBotonera_; }
    control::ControlWeb& controlWeb() { return *controlWeb_; }
    control::ControlWebGateway& controlWebGateway() { return *controlWebGw_; }
    control::SatelliteServidor& satellite() { return *satellite_; }
    control::Dsk& dsk() { return *dsk_; }
    /// El modelo de las capas, del dueño del motor: solo desde él (las herramientas dsk_*).
    control::CapasDsk& capasDsk() { return *capasDsk_; }
    control::Acciones& acciones() { return *acciones_; }
    QObject* botoneraQml() const;
    QObject* carasBotoneraQml() const;
    QObject* controlWebQml() const;
    QObject* satelliteQml() const;
    QObject* dskQml() const;
    QObject* accionesQml() const;

    QString temaWeb() const;
    /// Lo guarda (como Configuración) y se lo pasa al gateway: cambia en todas las páginas.
    void setTemaWeb(const QString& id);
    QVariantList temasWeb() const;
    /// En el DUEÑO: guarda el tema web en la configuración ("" o el error). Lo usan
    /// setTemaWeb (por la ventanilla) y la herramienta web_tema (en el control).
    static std::string guardarTemaWeb(AppRuntime& rt, const std::string& id);

    /// Genera el código de un uso del control web y muestra su QR (mostrarQr).
    Q_INVOKABLE void emparejarControlWebUi();
    /// Empareja una tablet: código de un uso (2 min) y el QR. Contesta con
    /// emparejamientoListo({url, qr (data URL de un SVG), codigo, vence_ms, huella, aviso, error}).
    Q_INVOKABLE void emparejarTabletUi(const QString& perfil, const QString& nombre, int venceDias);
    /// Una persona aprueba (o no) la tablet que pidió emparejarse.
    Q_INVOKABLE void aprobarTabletUi(const QString& solicitud, bool aprobar, const QString& perfil = QString());
    /// La API remota HTTP de la botonera (T59): {activo, accesos, url, aviso?}.
    Q_INVOKABLE QVariantMap apiRemotaUi() const;
    Q_INVOKABLE void setApiRemotaUi(bool activo);
    /// Un acceso nuevo para `perfil`: {dispositivo, token, url_ejemplo} (el token se ve UNA vez) o {error}.
    Q_INVOKABLE QVariantMap crearAccesoHttpUi(const QString& perfil, const QString& nombre);
    /// http://<ip de la red local>:<puerto de la sala> (vacío si no hay red).
    QString urlApiRemota() const;
    QStringList nombresParaDsk() const { return nombresParaDsk_; }

    /// Lo del área en la página Enlaces: la API remota, el Satellite, el control web y
    /// los pedidos de aprobación de cada aparato.
    void alimentarEnlaces(EnlacesViewModel::Fuentes& f);
    /// Que los enlaces se enteren cuando cambia algo del área.
    void avisarA(EnlacesViewModel& e);

   signals:
    void temaWebChanged();
    void nombresParaDskChanged();
    void emparejamientoListo(const QVariantMap& r);
    /// Mostrar un QR (el del control web): ver StudioViewModel::mostrarQr.
    void mostrarQr(const QString& texto, const QString& descripcion, const QString& mensaje, const QString& color);
    /// Cambió lo que muestra una capa DSK (también está al aire: el tally).
    void dskCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    void armarBotonera();
    /// Le pasa el tema web al gateway (al cambiar y cada vez que la sala vuelve).
    void mandarTemaWeb();
    /// Llegó una foto: las escenas (nombresParaDsk) y el tema web.
    void alCambiarFoto(unsigned partes);
    // Lo que leen las acciones y la botonera (de la foto y de la mezcla de la interfaz).
    std::vector<std::string> escenas() const;
    std::vector<std::string> fuentesAudio() const;
    bool mudo(const std::string& fuente, const std::string& salida) const;

    Partes p_;
    // En el orden en que se usan: lo que usa a otro va después (se destruye antes).
    std::unique_ptr<control::CarasBotonera> carasBotonera_;
    std::unique_ptr<control::Botonera> botonera_;
    std::unique_ptr<control::BotoneraGateway> botoneraGw_;
    std::unique_ptr<control::ControlWeb> controlWeb_;
    std::unique_ptr<control::ControlWebGateway> controlWebGw_;
    std::unique_ptr<control::SatelliteServidor> satellite_;
    std::unique_ptr<control::CapasDsk> capasDsk_;
    int aporteDsk_ = 0; ///< el de las capas a la foto (AppRuntime::aportarALaFoto)
    std::unique_ptr<control::Dsk> dsk_;
    std::unique_ptr<control::Acciones> acciones_;
    std::string temaWebMandado_;
    std::string temaWeb_; ///< el de la foto
    QStringList nombresParaDsk_;
    /// Las caras de los botones ya achicadas (ruta|fecha -> data: URL).
    std::map<QString, std::string> imagenesBotonera_;
};

} // namespace ba::app
