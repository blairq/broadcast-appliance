// ============================================================================
// src/app/ui/EstadoAireViewModel.hpp — lo que sale de la por omisión: las pastillas (F3-03)
//
// Sale de StudioViewModel (que lo sondeaba cada 100 ms y avisaba todo junto con un solo
// statusChanged): grabando, transmitiendo, reconectando, el problema, desde cuándo, los
// tiempos y lo que mide el motor de lo que sale (cuadros, codificador, kbps). Es una VISTA
// de la parte Estado de la foto del motor (emision/FotoMotor): escucha
// VentanillaMotor::cambio y avisa por GRUPOS, cada uno cuando cambia lo suyo:
//
//   aireChanged          qué sale y desde cuándo (recording, streaming, alAire,
//                        transmitiendo, reconnecting, outputProblem, recordStartMs)
//   tiemposChanged       recordTime, streamTime: una vez por segundo y solo mientras algo sale
//   estadisticasChanged  fps, encoder, droppedFrames, totalFrames, streamKbps: a lo sumo una
//                        vez por segundo (el motor los mueve en cada vuelta)
//
// El inicio de la grabación y de la transmisión lo anota el DUEÑO en la foto
// (EstadoAire::grabaDesdeMs): el tiempo es el de verdad aunque la interfaz se haya trabado o
// la vista se arme con la grabación andando. Los avisos de corte («Grabación detenida»,
// «reconectando…») salen del mismo manejador, comparando con la foto anterior.
//
// En QML: Studio.aire (los nombres de siempre de las pastillas).
// ============================================================================
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>

#include "emision/FotoMotor.hpp"

namespace ba::app {

class VentanillaMotor;

class EstadoAireViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY aireChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY aireChanged)
    /// Sale algo (transmisión, grabación o señal limpia) y si eso incluye una
    /// transmisión: lo mismo que mira la PTZ, las alertas y los candados.
    Q_PROPERTY(bool alAire READ alAire NOTIFY aireChanged)
    Q_PROPERTY(bool transmitiendo READ transmitiendo NOTIFY aireChanged)
    /// La transmisión se cortó y se está reintentando.
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY aireChanged)
    /// Por qué se cortó, en castellano. Vacío si no hubo problema.
    Q_PROPERTY(QString outputProblem READ outputProblem NOTIFY aireChanged)
    Q_PROPERTY(QString recordTime READ recordTime NOTIFY tiemposChanged)
    Q_PROPERTY(QString streamTime READ streamTime NOTIFY tiemposChanged)
    Q_PROPERTY(double fps READ fps NOTIFY estadisticasChanged)
    Q_PROPERTY(QString encoder READ encoder NOTIFY estadisticasChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY estadisticasChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY estadisticasChanged)
    Q_PROPERTY(int streamKbps READ streamKbps NOTIFY estadisticasChanged)
   public:
    struct Partes {
        VentanillaMotor& ventanilla;
        /// Un corte que nadie pidió (la grabación que se cayó): un aviso fijo para el
        /// operador (la barra de mensajes). Puede faltar.
        std::function<void(const QString& titulo, const QString& texto)> critico;
        /// La hora de pared en ms (el test la reemplaza).
        std::function<std::int64_t()> ahoraMs;
    };
    explicit EstadoAireViewModel(Partes p, QObject* parent = nullptr);
    ~EstadoAireViewModel() override;

    bool recording() const { return e().graba; }
    bool streaming() const { return e().transmite; }
    bool alAire() const { return e().haySalida; }
    bool transmitiendo() const { return e().hayTransmision; }
    bool reconnecting() const { return e().reconectando; }
    QString outputProblem() const { return QString::fromStdString(e().problema); }
    QString recordTime() const;
    QString streamTime() const;
    double fps() const { return e().fps; }
    QString encoder() const { return QString::fromStdString(e().codificador); }
    int droppedFrames() const { return e().descartados; }
    int totalFrames() const { return e().cuadros; }
    int streamKbps() const { return kbps_; }
    /// Cuándo arrancó la grabación (ms de pared; 0 = no graba). La anota el dueño.
    qint64 recordStartMs() const { return e().graba || e().grabacionCaida ? e().grabaDesdeMs : 0; }

    /// «hh:mm:ss» desde `desdeMs` hasta `ahoraMs` (los relojes del aire; el de cada salida también).
    static QString hms(std::int64_t desdeMs, std::int64_t ahoraMs);
    /// La hora de pared (la de Partes::ahoraMs).
    std::int64_t ahora() const;

   signals:
    void aireChanged();
    void tiemposChanged();
    void estadisticasChanged();
    /// Una vez por segundo mientras alguna salida graba o transmite (los relojes de las demás
    /// salidas siguen a este, ver SalidasViewModel).
    void segundo();
    /// Para el operador (ya traducido): los cortes, la reconexión.
    void notify(const QString& texto, const QString& nivel);

   private:
    const FotoMotor::EstadoAire& e() const;
    void alCambiar(unsigned partes);
    void alSegundo();
    /// Si algo sale en alguna salida (el reloj de 1 Hz anda solo entonces).
    bool algoSale() const;
    void avisarEstadisticas();

    Partes p_;
    FotoMotor::EstadoAire antes_; ///< la parte Estado de la foto anterior (para avisar solo lo que cambió)
    QTimer segundo_;
    QTimer estadisticas_;          ///< el próximo aviso de las estadísticas (a lo sumo 1 por segundo)
    QElapsedTimer ultimaEstadistica_;
    int kbps_ = 0;
    std::uint64_t bytesAntes_ = 0;
    std::int64_t bytesAntesMs_ = 0;
};

} // namespace ba::app
