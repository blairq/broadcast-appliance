// ============================================================================
// src/app/ui/InputsViewModel.hpp — Pagina Entradas: hardware y red
//
// Camaras USB y capturadoras conectadas (con sus modos) y camaras/señales por
// la red (RTSP, RTMP, SRT, HLS, UDP): agregarlas a una escena, ver si llega
// imagen (miniatura viva) y quitarlas. Arma las fuentes igual que el MCP
// (InputSpecs). Mientras la pagina esta visible, las entradas se mantienen
// "mostrandose" para que sus miniaturas se actualicen aunque no esten al aire.
//
// Los dispositivos para elegir (camaras, microfonos, NDI) NO se buscan aca:
// salen del registro de la maquina (dispositivos::Dispositivos, la pagina
// «Dispositivos y equipos»), con el nombre que les puso el operador y si estan
// conectados. Una entrada muestra el nombre de su dispositivo, o que esta
// ausente (y se la espera), en vez del id tecnico. Las paginas «Entradas de
// video» y «Entradas de audio» usan este mismo modelo.
//
// NO ENTRA AL MOTOR (F3-03, docs/arquitectura/05-motor-del-plano.md): las entradas (con
// su tamaño, sus escenas, el modo de la cámara) salen de la foto del motor (parte Video:
// FotoMotor::fuentesVideo) y se rearman cuando llega; agregar, quitar y mantener las
// miniaturas vivas se le pide al dueño por la ventanilla. Lo que valida acá vuelve en el
// acto; lo que dice el motor, por `agregado`.
// ============================================================================
#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <set>
#include <string>

namespace ba::app {

class AppRuntime;
class StudioViewModel;
class VentanillaMotor;
namespace dispositivos {
class Dispositivos;
}

class InputsViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    /// Entradas de audio del sistema (microfonos, lineas, interfaces).
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    /// Las de video (camaras, capturadoras, red, NDI) y las de audio, por separado:
    /// cada una tiene su pagina.
    /// Con su propio aviso: la lista de audio no se rearma porque cambió el tamaño de una cámara.
    Q_PROPERTY(QVariantList videoInputs READ videoInputs NOTIFY videoInputsChanged)
    Q_PROPERTY(QVariantList audioInputs READ audioInputs NOTIFY audioInputsChanged)
    Q_PROPERTY(QStringList scenes READ scenes NOTIFY changed)
    Q_PROPERTY(int stamp READ stamp NOTIFY stampChanged)
    Q_PROPERTY(bool hardwareAvailable READ hardwareAvailable NOTIFY changed)
    Q_PROPERTY(bool ndiAvailable READ ndiAvailable CONSTANT)
    Q_PROPERTY(QVariantList ndiFound READ ndiFound NOTIFY ndiChanged)
    Q_PROPERTY(bool ndiSearching READ ndiSearching NOTIFY ndiChanged)
   public:
    InputsViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, StudioViewModel& vm, QObject* parent = nullptr);
    ~InputsViewModel() override;

    QVariantList devices() const { return devices_; }
    QVariantList audioDevices() const { return audioDevices_; }
    QVariantList inputs() const { return inputs_; }
    QVariantList videoInputs() const { return videoInputs_; }
    QVariantList audioInputs() const { return audioInputs_; }
    /// El registro de los dispositivos de la maquina (lo engancha dispositivos/Registro).
    void setDispositivos(dispositivos::Dispositivos* d);
    QStringList scenes() const;
    int stamp() const { return stamp_; }
    bool hardwareAvailable() const;
    bool ndiAvailable() const;
    QVariantList ndiFound() const { return ndiFound_; }
    bool ndiSearching() const { return ndiSearching_; }
    /// Busca emisores NDI en la red (en segundo plano, unos segundos).
    Q_INVOKABLE void searchNdi();
    Q_INVOKABLE QString addNdi(const QString& ndiName, const QString& name, const QString& scene) {
        return addNdi(ndiName, name, scene, {});
    }

    Q_INVOKABLE void refresh();
    /// Modos de una camara: [{label, pixelformat, resolution, framerate}].
    Q_INVOKABLE QVariantList modes(const QString& device) const;
    /// Los add*: "" o el error de lo que se valida acá; lo que diga el motor llega por
    /// `agregado` (que: hardware | red | ndi | audio), o a `listo` si se lo da C++ (los
    /// dispositivos, el MCP).
    using Listo = std::function<void(const QString& error)>;
    QString addNdi(const QString& ndiName, const QString& name, const QString& scene, Listo listo);
    QString addAudioInput(const QString& device, const QString& name, Listo listo);
    QString addHardware(const QString& device, const QString& name, const QString& scene, const QVariantMap& mode,
                        Listo listo);
    QString addNetwork(const QString& url, const QString& name, const QString& scene, bool lowLatency, Listo listo);
    /// Agrega un microfono o entrada de audio del sistema (sin video).
    Q_INVOKABLE QString addAudioInput(const QString& device, const QString& name = {}) {
        return addAudioInput(device, name, Listo{});
    }
    /// Con que nombre queda esa entrada de audio (el de la fuente creada).
    Q_INVOKABLE QString audioInputName(const QString& device) const;
    Q_INVOKABLE QString addHardware(const QString& device, const QString& name, const QString& scene,
                                    const QVariantMap& mode) {
        return addHardware(device, name, scene, mode, {});
    }
    Q_INVOKABLE QString addNetwork(const QString& url, const QString& name, const QString& scene,
                                   bool lowLatency) {
        return addNetwork(url, name, scene, lowLatency, {});
    }
    Q_INVOKABLE void remove(const QString& name);
    /// La pagina esta a la vista (miniaturas vivas).
    Q_INVOKABLE void setActive(bool on);

   signals:
    void changed();
    void videoInputsChanged();
    void audioInputsChanged();
    void stampChanged();
    void ndiChanged();
    /// Terminó de agregarse una entrada (lo hizo el dueño del motor): `error` vacío si salió.
    void agregado(const QString& que, const QString& error);

   private:
    void rebuildInputs();
    /// Lo que avisó el runtime (ver el constructor): se rehace la lista solo si
    /// cambió de verdad (un volumen también avisa «Audio»).
    void revisarEntradas(unsigned avisos);
    /// Las listas para elegir, del registro de dispositivos.
    void rebuildDevices();
    /// El dispositivo de la maquina que usa una entrada: {dispositivo, dispositivoId, estado, conectado}.
    QVariantMap dispositivoDe(const QString& tipo, const QString& valor) const;
    void syncHolds();
    /// Una orden al dueño del motor que agrega una fuente: la respuesta va a `agregado`.
    void pedirAgregar(const char* que, std::function<std::string(AppRuntime&)> orden, Listo listo);

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    StudioViewModel& vm_;
    QVariantList devices_;
    QVariantList audioDevices_;
    QVariantList inputs_;
    QVariantList videoInputs_;
    QVariantList audioInputs_;
    dispositivos::Dispositivos* dd_ = nullptr;
    int stamp_ = 0;
    bool active_ = false;
    std::set<std::string> held_;
    QTimer tick_;
    QVariantList ndiFound_;
    bool ndiSearching_ = false;
    int observador_ = -1;
};

/// image://inputs/<nombre>?<stamp>: miniatura de una entrada.
class InputThumbProvider : public QQuickImageProvider {
   public:
    // En segundo plano: capturar espera el cuadro de la fuente (no se traba la interfaz).
    InputThumbProvider(AppRuntime& rt, VentanillaMotor& ventanilla)
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading), rt_(rt),
          ventanilla_(ventanilla) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;

   private:
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
};

} // namespace ba::app
