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
// El audio de cada fuente es del motor, que es del plano de control (F3-03,
// docs/arquitectura/05-motor-del-plano.md): se lee de la foto (la parte Mezcla) y lo que
// se cambia se pide por la ventanilla. Cuando cambia, por el camino que sea, la foto lo
// trae: la matriz lo anota y las tiras se refrescan. Los niveles, del canal de niveles.
// Lo que se escucha (PFL), la balística de los medidores, el retardo de cada fuente y
// medirlo con un aplauso también son de acá.
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
class VentanillaMotor;
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
        VentanillaMotor& ventanilla;  ///< el motor: se le pide y se lee su foto
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
    /// Retardo del audio de una fuente respecto de su video (ms, como audio_sincro): el de
    /// la foto, al abrir el diálogo.
    Q_INVOKABLE int retardoUi(const QString& fuente) const;
    /// Se pide al dueño; "" (si no se pudo, llega por notify).
    Q_INVOKABLE QString setRetardoUi(const QString& fuente, int ms);
    /// Medir con aplauso: mide `segundos` sobre el programa de `salida` ("" = la por
    /// omisión) y llama a `listo` con el resultado (ver BroadcastEngine::terminarMedicionSincro);
    /// con `aplicarA` (una fuente) y una medición confiable, le suma el retardo medido al que
    /// tenía (en el dueño, en la misma orden: {aplicado: {fuente, ms}} o aplicado_error).
    /// Empezar y terminar se piden al dueño: si no pudo empezar, `fallo` con el motivo.
    /// "" o el error que se sabe antes de pedir (ya se está midiendo).
    QString medirSincro(int segundos, std::function<void(const nlohmann::json&)> listo,
                        std::function<void(const QString&)> fallo, const std::string& salida = "",
                        const std::string& aplicarA = "");
    /// Lo mismo para la GUI (la salida activa): el resultado llega por medicionSincroLista(resultado).
    Q_INVOKABLE QString medirSincroUi(int segundos, const QString& salida = {});

   signals:
    void balisticaChanged();
    void pflChanged();
    void medicionSincroLista(const QVariantMap& resultado);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// Llegó una foto: lo que muestra el área (PFL, balística, las pistas que graba el programa).
    void alCambiarFoto(unsigned partes);
    /// El final de medirSincro (en el dueño, y lo que se aplica).
    void terminarSincro(std::function<void(const nlohmann::json&)> listo, const std::string& aplicarA);
    Partes p_;
    std::string balisticaPedida_; ///< la pedida, hasta que vuelve (pendiente)
    // El orden es el de destrucción al revés: lo que usa a otro va después.
    std::unique_ptr<audio::RackManager> racks_;
    std::unique_ptr<audio::Matriz> matriz_;
    std::unique_ptr<audio::Mezclador> mezclador_;
    std::unique_ptr<audio::AudioViewModel> audio_;
};

} // namespace ba::app
