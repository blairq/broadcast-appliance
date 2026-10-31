// ============================================================================
// src/app/ui/ImagenViewModel.hpp — el área de la imagen: cámaras y entradas de video (F3-03, partición)
//
// Sale de StudioViewModel: lo que llega por las entradas de video y lo que se hace con
// la imagen, cada pieza su modelo:
//
//   ptz::ControlCamaras       el PTZ de cada entrada (digital y de hardware) y Mejorar imagen
//   video::VideoGrafos        el grafo de tarjetas de video de cada entrada (y el legalizador):
//                             ES DEL DUEÑO DEL MOTOR (lo adopta la ventanilla); la interfaz lo
//                             ve y le pide por VideoViewModel (Studio.video)
//   video::SuperRes / Banco   la superresolución de las entradas de red y qué puede esta máquina
//   tiempos::TiemposModel     el grafo de demoras de cada entrada a cada salida
//   resolucion::ResolucionModel  el plan de resolución de cada entrada
//   red::Red                  los equipos de video de la red (escáner, credenciales, ficha)
//   Seguidor                  analizar las caras de una cámara y seguir a una con el PTZ
//
// Acá se arman sus fuentes (lo que ya publican el motor, las salidas, los racks y la
// sala), se los sincroniza con las entradas, se cede la superresolución cuando la
// máquina calienta y se avisa cuando no llega. Los scopes y la revisión previa del
// evento (lo que mira del aparato) también son de acá.
//
// NADA DE ACÁ ENTRA AL MOTOR (F3-03, docs/arquitectura/05-motor-del-plano.md): lo que se
// muestra sale de la foto del motor (la parte Video: la vista del grafo, las fuentes de
// video con su tamaño, lo que está al aire, la configuración de la imagen) y lo que se
// cambia se pide por la ventanilla. Del motor solo se lee lo que publica para cualquier
// hilo (la instantánea del motor y la foto de las salidas, para los tiempos).
//
// En QML los modelos siguen en Studio (Studio.camaras, Studio.video…); lo propio del
// área en Studio.imagen (scopes, legalizar).
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace ba::app {
class AppRuntime;
class BroadcastEngine;
class ConferenceViewModel;
class FrameService;
class InputsViewModel;
class PersonasViewModel;
class RecursosViewModel;
class Seguidor;
class VentanillaMotor;
class VideoViewModel;
} // namespace ba::app
namespace ba::app::audio {
class RackManager;
}
namespace ba::app::control {
class BusEventos;
}
namespace ba::app::ptz {
class ControlCamaras;
}
namespace ba::app::red {
class Red;
}
namespace ba::app::resolucion {
class ResolucionModel;
}
namespace ba::app::tiempos {
class TiemposModel;
}
namespace ba::app::video {
class BancoSuperRes;
class SuperRes;
class VideoGrafos;
} // namespace ba::app::video

namespace ba::app {

class ImagenViewModel : public QObject {
    Q_OBJECT
    /// La red eléctrica del país del sistema contra el lienzo (F2-17): {hz (50, 60; 0 = no
    /// se sabe), aviso ("" si el lienzo no parpadea con esa red), cadencias: [{valor (el de
    /// video.fps: "25", "30000/1001"), texto ("25", "29,97")}] (las que no parpadean)}. Se
    /// avisa y se ofrece, no se cambia solo. Cambia al aplicar la configuración.
    Q_PROPERTY(QVariantMap redElectrica READ redElectrica NOTIFY redElectricaChanged)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla;   ///< la foto del motor y las órdenes (F3-03)
        /// Solo lo que el motor publica para cualquier hilo (la instantánea, la foto de las
        /// salidas): lo que leen los tiempos.
        BroadcastEngine& motorEntreHilos;
        control::BusEventos& bus;      ///< la superresolución que no llega
        ConferenceViewModel& sala;     ///< los lugares de la sala sin nadie (tiempos)
        InputsViewModel& entradas;     ///< las entradas nuevas y lo que cambia en Configuración
        RecursosViewModel& recursos;   ///< «priorizar programa» por calor
        FrameService& cuadros;         ///< el cuadro del programa para los scopes
        PersonasViewModel* personas = nullptr; ///< las caras (el seguidor)
        std::function<audio::RackManager*()> racks; ///< se montan después (tiempos)
    };
    explicit ImagenViewModel(Partes p, QObject* parent = nullptr);
    ~ImagenViewModel() override;

    ptz::ControlCamaras& camaras() { return *camaras_; }
    /// El grafo de video: vive en el dueño (el MCP del control, el vigía, las órdenes).
    video::VideoGrafos& video() { return *video_; }
    /// Lo que la interfaz ve de él y cómo le pide (Studio.video).
    VideoViewModel& videoVm() { return *videoVm_; }
    tiempos::TiemposModel& tiempos() { return *tiempos_; }
    resolucion::ResolucionModel& resolucion() { return *resolucion_; }
    video::SuperRes& superres() { return *superres_; }
    video::BancoSuperRes& bancoSr() { return *bancoSr_; }
    red::Red& red() { return *red_; }
    Seguidor& seguidor() { return *seguidor_; }
    QObject* camarasQml() const;
    QObject* videoQml() const;
    QObject* tiemposQml() const;
    QObject* resolucionQml() const;
    QObject* superresQml() const;
    QObject* bancoSrQml() const;
    QObject* redQml() const;
    QObject* seguidorQml() const;

    /// Se aplicó la configuración: Mejorar imagen general y el legalizador rigen ya.
    void ajustesAplicados();
    QVariantMap redElectrica() const;
    /// Lo que la revisión previa del evento mira del aparato: [{grave, texto}] (el disco,
    /// si hay destino de transmisión, las entradas sin señal).
    QVariantList chequeosDelAparato(bool hayDestino) const;

    /// Un scope (onda|parade|vector|histograma) de `fuente` ("" = programa):
    /// {imagen: data URL PNG, legalidad: {...}, legalizador} o {error}.
    Q_INVOKABLE QVariantMap scopeUi(const QString& fuente, const QString& tipo, int ancho, int alto) const;
    /// Legalizar para TV: no | pal | ntsc | rango. "" o el error.
    Q_INVOKABLE QString legalizarUi(const QString& modo);

   signals:
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);
    void redElectricaChanged();

   private:
    void armarTiempos();
    void armarResolucion();

    Partes p_;
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<ptz::ControlCamaras> camaras_;
    std::unique_ptr<tiempos::TiemposModel> tiempos_;
    std::unique_ptr<resolucion::ResolucionModel> resolucion_;
    std::unique_ptr<video::VideoGrafos> video_;
    std::unique_ptr<VideoViewModel> videoVm_;
    std::unique_ptr<red::Red> red_;
    std::unique_ptr<Seguidor> seguidor_;
    std::unique_ptr<video::SuperRes> superres_;
    std::unique_ptr<video::BancoSuperRes> bancoSr_;
    // El scope de una fuente que no es el programa: el último cuadro que llegó
    // (scopeUi lo pide aparte; la interfaz no espera el cuadro).
    mutable bool scopeCapturando_ = false;
    mutable QString scopeFuente_, scopeError_;
    mutable QImage scopeCuadro_;
};

} // namespace ba::app
