// ============================================================================
// src/app/audio/LazoMedicion.hpp — sacar un barrido por un parlante y grabar un micrófono
//
// La calibración de sala (audio::Calibracion) necesita emitir una señal por UNA
// salida (un canal de un dispositivo) y grabar UNA entrada al mismo tiempo. Va
// aparte de la Matriz y del motor: abre los dispositivos en modo compartido
// (CoreAudio, WASAPI y PulseAudio mezclan con lo que ya esté sonando) solo
// mientras dura la medición. El retardo entre emitir y grabar no hace falta
// saberlo: la deconvolución lo deja a la vista (el directo llega más tarde).
//
// LazoSimulado: la misma interfaz con una sala de mentira (directo, primeras
// reflexiones por fuentes imagen, modos de la sala rectangular y una cola
// difusa), para probar todo sin que suene nada (de noche, en las pruebas).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <vector>

class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;
class QTimer;

namespace ba::app::audio {

/// El id de la sala simulada en las listas de dispositivos (solo con --test o BA_SALA_SIMULADA).
inline constexpr const char* kSalaSimulada = "sala-simulada";

class LazoMedicion {
   public:
    using Listo = std::function<void(std::vector<float> captura, QString error)>;
    virtual ~LazoMedicion() = default;
    /// Emite `estimulo` (mono) y llama a `listo` con lo grabado (desde un poco antes
    /// de emitir hasta un poco después), en el hilo principal. "" si arrancó.
    virtual QString arrancar(const std::vector<float>& estimulo, int frecuencia, Listo listo) = 0;
    virtual void cancelar() = 0;
};

/// Los dispositivos del equipo: [{id, nombre, canales}] (salidas o entradas).
QVariantList dispositivosAudio(bool salidas, bool conSimulada);

/// Con los dispositivos de verdad (QtMultimedia).
std::unique_ptr<LazoMedicion> lazoReal(const QString& salida, int canalSalida, const QString& entrada,
                                       int canalEntrada);

/// La respuesta de una sala simulada entre un parlante y un micrófono, aplicada a
/// `x`: sala {largo, ancho, alto, rt60}, posiciones {x, y, z} en metros.
std::vector<float> simularSala(const nlohmann::json& sala, const nlohmann::json& parlante,
                               const nlohmann::json& microfono, const std::vector<float>& x, int frecuencia);
std::unique_ptr<LazoMedicion> lazoSimulado(const nlohmann::json& sala, const nlohmann::json& parlante,
                                           const nlohmann::json& microfono);

} // namespace ba::app::audio
