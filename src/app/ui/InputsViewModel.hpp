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
// ============================================================================
#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <set>
#include <string>

namespace ba::app {

class AppRuntime;
class StudioViewModel;
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
    InputsViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent = nullptr);
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
    Q_INVOKABLE QString addNdi(const QString& ndiName, const QString& name, const QString& scene);

    Q_INVOKABLE void refresh();
    /// Modos de una camara: [{label, pixelformat, resolution, framerate}].
    Q_INVOKABLE QVariantList modes(const QString& device) const;
    /// "" o el error.
    /// Agrega un microfono o entrada de audio del sistema (sin video).
    Q_INVOKABLE QString addAudioInput(const QString& device, const QString& name = {});
    /// Con que nombre queda esa entrada de audio (el de la fuente creada).
    Q_INVOKABLE QString audioInputName(const QString& device) const;
    Q_INVOKABLE QString addHardware(const QString& device, const QString& name, const QString& scene,
                                    const QVariantMap& mode);
    Q_INVOKABLE QString addNetwork(const QString& url, const QString& name, const QString& scene,
                                   bool lowLatency);
    Q_INVOKABLE void remove(const QString& name);
    /// La pagina esta a la vista (miniaturas vivas).
    Q_INVOKABLE void setActive(bool on);

   signals:
    void changed();
    void videoInputsChanged();
    void audioInputsChanged();
    void stampChanged();
    void ndiChanged();

   private:
    void rebuildInputs();
    /// Las listas para elegir, del registro de dispositivos.
    void rebuildDevices();
    /// El dispositivo de la maquina que usa una entrada: {dispositivo, dispositivoId, estado, conectado}.
    QVariantMap dispositivoDe(const QString& tipo, const QString& valor) const;
    void syncHolds();

    AppRuntime& rt_;
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
};

/// image://inputs/<nombre>?<stamp>: miniatura de una entrada.
class InputThumbProvider : public QQuickImageProvider {
   public:
    // En segundo plano: capturar espera el cuadro de la fuente (no se traba la interfaz).
    explicit InputThumbProvider(AppRuntime& rt)
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading), rt_(rt) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;

   private:
    AppRuntime& rt_;
};

} // namespace ba::app
