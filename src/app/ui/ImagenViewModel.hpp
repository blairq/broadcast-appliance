// ============================================================================
// src/app/ui/ImagenViewModel.hpp — el área de la imagen: cámaras y entradas de video (F3-03, partición)
//
// Sale de StudioViewModel: lo que llega por las entradas de video y lo que se hace con
// la imagen, cada pieza su modelo:
//
//   ptz::ControlCamaras       el PTZ de cada entrada (digital y de hardware) y Mejorar imagen
//   video::VideoGrafos        el grafo de tarjetas de video de cada entrada (y el legalizador)
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
class ConferenceViewModel;
class FrameService;
class InputsViewModel;
class PersonasViewModel;
class RecursosViewModel;
class Seguidor;
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
   public:
    struct Partes {
        AppRuntime& rt;
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
    video::VideoGrafos& video() { return *video_; }
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

   private:
    void armarTiempos();
    void armarResolucion();

    Partes p_;
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<ptz::ControlCamaras> camaras_;
    std::unique_ptr<tiempos::TiemposModel> tiempos_;
    std::unique_ptr<resolucion::ResolucionModel> resolucion_;
    std::unique_ptr<video::VideoGrafos> video_;
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
