// ============================================================================
// src/app/audio/EscuchaNemotron.hpp — Nemotron 3.5 ASR Streaming con sherpa-onnx
//
// Lo de sherpa-onnx de EscuchaStreaming: el modelo (OnlineRecognizer, con los
// streams decodificados en lote por SherpaOnnxDecodeMultipleOnlineStreams) y
// el detector de voz de cada corriente (Silero VAD). Solo con BA_HAVE_VOZ.
//
// EL IDIOMA. Nemotron 3.5 es multilingüe y se le dice el idioma con un «prompt»
// por stream (la opción "language" de sherpa-onnx: "es-US", "es-ES", "es"...;
// vacío o "auto" lo detecta solo). De fábrica, castellano de América
// (`escucha.nemotronIdioma`, Configuración → avanzado): detectar solo cuesta
// palabras al principio de cada frase.
// ============================================================================
#pragma once

#include "audio/EscuchaStreaming.hpp"

#include <string>

namespace ba::app::audio {

struct ModelosNemotron {
    std::string encoder, decoder, joiner, tokens; ///< transducer streaming
    std::string vad;                              ///< silero_vad.onnx
    std::string idioma = "es-US";
    int hilos = 2;
    /// Fin de turno del modelo: silencio (s) después de lo dicho que cierra el
    /// turno, y largo máximo (s). Se fijan al cargar (son de la configuración
    /// del reconocedor); los cortes nuevos los aplica el VAD de cada corriente.
    double pausaTurno = 0.8;
    double maximoTurno = 20.0;
};

/// Carga Nemotron (y Silero para cada corriente) en `e`. `listo` en el hilo del motor.
void cargarNemotron(EscuchaStreaming& e, const ModelosNemotron& m, std::function<void(const std::string&)> listo);

} // namespace ba::app::audio
