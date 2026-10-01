// ============================================================================
// src/app/audio/Escucha.hpp — de la voz del operador a texto (push-to-talk)
//
// QUE ES. El oído del control por voz (docs/AUDIO-TTS.md): mientras el
// operador aprieta la tecla, su micrófono —capturado en OperatorFeed antes de
// la mezcla, y cortado hacia el lobby en ese mismo momento— llega acá; al
// soltar, se recorta el silencio con el VAD y se transcribe. El texto va a la
// barra del asistente como si lo hubiera escrito.
//
// CON QUE. sherpa-onnx (extern/sherpa-onnx) sobre el ONNX Runtime del bundle,
// en CPU: la GPU la usa libobs. Los modelos —un reconocedor y Silero VAD— NO
// viajan en el bundle: se bajan a pedido (Configuración -> Modelos locales).
// Medido en una M1 Pro con Parakeet-TDT-0.6B-v3 int8: 5,3 s de habla en
// 0,18 s, con puntuación y acentos.
//
// LA FORMA, la del Reconocedor de rostros: un hilo propio es el único dueño de
// los modelos; pedido y callback, y NADA pesado en el hilo de la interfaz (el
// latido se estanca y el launcher da la app por colgada). `alimentar` corre en
// el hilo de audio de libobs: solo copia muestras bajo un mutex.
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ba::app::audio {

class Escucha {
   public:
    /// Los archivos de los modelos (carpetas bajadas por ModelosLocales).
    struct Modelos {
        std::string encoder, decoder, joiner, tokens; ///< transducer (Parakeet)
        std::string modelType = "nemo_transducer";
        std::string vad; ///< silero_vad.onnx (vacío: sin recortar silencios)
        int hilos = 2;
    };
    /// Lo que salió de una toma.
    struct Resultado {
        std::string texto;
        std::string error;
        double segundosDeAudio = 0; ///< lo que se habló (después del VAD)
        double ms = 0;              ///< lo que tardó en transcribir
    };
    using Listo = std::function<void(const Resultado&)>;

    /// Lo más que se graba en una toma: una orden, no un discurso.
    static constexpr double kMaxSegundos = 30.0;

    Escucha();
    ~Escucha();
    Escucha(const Escucha&) = delete;
    Escucha& operator=(const Escucha&) = delete;

    /// Carga (en su hilo). `listo` recibe error vacío si anduvo. Se puede
    /// volver a llamar para cambiar de modelos.
    void cargar(const Modelos& m, std::function<void(const std::string& error)> listo);
    bool cargado() const { return cargado_; }

    /// Tecla apretada: empieza una toma nueva (descarta lo anterior).
    void empezar();
    /// Muestras mono en float, desde cualquier hilo (el de audio de libobs).
    /// Fuera de una toma no hace nada.
    void alimentar(const float* muestras, std::size_t n, std::uint32_t frecuencia);
    /// Tecla suelta: transcribe lo grabado en su hilo y llama a `listo` (EN
    /// ESE HILO: quien lo use lo pasa al suyo).
    void terminar(Listo listo);
    /// Tecla suelta sin querer transcribir (se canceló).
    void cancelar();
    bool grabando() const { return grabando_; }

   private:
    struct Motor; // sherpa-onnx: solo existe en el hilo de trabajo
    void bucle();
    void encolar(std::function<void()> trabajo);
    Resultado transcribir(std::vector<float> audio, std::uint32_t frecuencia);

    std::unique_ptr<Motor> motor_;
    std::atomic<bool> cargado_{false};
    std::atomic<bool> grabando_{false};

    std::mutex tomaLock_;
    std::vector<float> toma_;
    std::uint32_t frecuencia_ = 48000;

    std::mutex colaLock_;
    std::condition_variable colaCv_;
    std::deque<std::function<void()>> cola_;
    bool salir_ = false;
    std::thread hilo_;
};

} // namespace ba::app::audio
