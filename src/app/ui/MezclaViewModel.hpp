// ============================================================================
// src/app/ui/MezclaViewModel.hpp — el área del audio: racks, matriz y Mezclador (F3-03, partición)
//
// Sale de StudioViewModel: el audio de cada fuente y cómo se mezcla, cada pieza su
// modelo (src/app/audio/):
//
//   RackManager     los racks de módulos LSP de cada fuente (se montan los guardados)
//   AudioViewModel  la pestaña Audio (el patchbay de cada fuente)
//   Matriz          fuentes × pistas y buses virtuales; la escucha (PFL)
//   Mezclador       la ventana propia: niveles de entradas y salidas, calibración
//
// El audio de cada fuente es del motor: cuando cambia, por el camino que sea, la matriz
// lo anota y el mezclador refresca esa tira. Lo que se escucha (PFL), la balística de
// los medidores, el retardo de cada fuente y medirlo con un aplauso también son de acá.
//
// En QML los modelos siguen en Studio (Studio.audio, Studio.matriz, Studio.mezclador);
// lo propio del área en Studio.mezcla (pfl, balistica, retardo, medir la sincronía).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class MixerModel;
} // namespace ba::app
namespace ba::app::audio {
class AudioViewModel;
class Matriz;
class Mezclador;
class RackManager;
} // namespace ba::app::audio

namespace ba::app {

class MezclaViewModel : public QObject {
    Q_OBJECT
    /// Balística de los medidores (T31): "pico", "ppm" (EBU tipo IIb) o "vu".
    Q_PROPERTY(QString balistica READ balistica WRITE setBalistica NOTIFY balisticaChanged)
    /// La fuente en solo por el monitor (PFL o AFL), "" si ninguna.
    Q_PROPERTY(QString pfl READ pfl NOTIFY pflChanged)
    /// Cómo es el solo: "pfl" (antes del fader) o "afl" (con el fader de la salida activa); "" sin solo.
    Q_PROPERTY(QString pflModo READ pflModo NOTIFY pflChanged)
    /// La salida de la escucha fija: de cuál son su fader y su mute ("" = la por omisión).
    Q_PROPERTY(QString salidaEscucha READ salidaEscucha WRITE setSalidaEscucha NOTIFY pflChanged)
   public:
    struct Partes {
        AppRuntime& rt;
        MixerModel& tiras;            ///< las tiras del mezclador de las tarjetas
        std::string salida;           ///< la salida activa al armarse
        std::function<bool()> alAire; ///< un barrido de calibración nunca con aire
    };
    explicit MezclaViewModel(Partes p, QObject* parent = nullptr);
    ~MezclaViewModel() override;

    audio::RackManager& racks() { return *racks_; }
    audio::AudioViewModel& audio() { return *audio_; }
    audio::Matriz& matriz() { return *matriz_; }
    audio::Mezclador& mezclador() { return *mezclador_; }
    QObject* audioQml() const;
    QObject* matrizQml() const;
    QObject* mezcladorQml() const;

    /// La salida activa de la interfaz cambió: el Mezclador muestra la suya.
    void setSalida(const std::string& salida);
    /// Cambió lo que sale (las pastillas): las pistas que graba el programa quedan rotuladas
    /// en la Matriz.
    void revisarPistas();
    /// Una entrada de audio nueva (o una que se va): la lista de la pestaña Audio se rehace.
    void revisarFuentes();
    /// Una vez por segundo: los módulos que se saltaron por lentos o colgados (se avisa).
    void vigilarModulos();

    QString balistica() const;
    void setBalistica(const QString& b);
    QString pfl() const;
    QString pflModo() const;
    QString salidaEscucha() const;
    void setSalidaEscucha(const QString& salida);
    /// PFL exclusivo: escucha `fuente` por el monitor (o la apaga si ya era esa). "" o el error.
    Q_INVOKABLE QString pflUi(const QString& fuente);
    /// AFL: lo mismo con el fader y el mute de la salida activa (la del Mezclador).
    Q_INVOKABLE QString aflUi(const QString& fuente);
    /// Retardo del audio de una fuente respecto de su video (ms, como audio_sincro).
    Q_INVOKABLE int retardoUi(const QString& fuente) const;
    Q_INVOKABLE QString setRetardoUi(const QString& fuente, int ms);
    /// Medir con aplauso: mide `segundos` sobre el programa de `salida` ("" = la por
    /// omisión) y llama a `listo` con el resultado (ver BroadcastEngine::terminarMedicionSincro).
    /// "" o el error al empezar.
    QString medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo, const std::string& salida = "");
    /// Lo mismo para la GUI (la salida activa): el resultado llega por medicionSincroLista(resultado).
    Q_INVOKABLE QString medirSincroUi(int segundos, const QString& salida = {});

   signals:
    void balisticaChanged();
    void pflChanged();
    void medicionSincroLista(const QVariantMap& resultado);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    Partes p_;
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<audio::RackManager> racks_;
    std::unique_ptr<audio::Matriz> matriz_;
    std::unique_ptr<audio::Mezclador> mezclador_;
    std::unique_ptr<audio::AudioViewModel> audio_;
};

} // namespace ba::app
