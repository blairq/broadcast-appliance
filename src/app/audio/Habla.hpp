// ============================================================================
// src/app/audio/Habla.hpp — de texto a voz, frase por frase
//
// La boca del control por voz (docs/AUDIO-TTS.md): sherpa-onnx con voces de
// piper, en CPU (la GPU la usa libobs). Sintetiza FRASE POR FRASE y entrega
// cada una apenas está: la primera suena mientras se arma la segunda, que es
// lo que hace que una respuesta se sienta inmediata. Medido en una M1 Pro con
// es_AR-daniela-high: 3,8 s de habla en 1,4 s (2 hilos).
//
// Las voces no viajan en el bundle: se bajan a pedido (tipo «tts»).
//
// La forma, la de Escucha: un hilo dueño del modelo, pedido y callback. El
// callback de audio corre en ESE hilo.
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

namespace ba::app::audio {

class Habla {
   public:
    struct Voz {
        std::string modelo; ///< el .onnx de piper
        std::string tokens; ///< tokens.txt
        std::string datos;  ///< espeak-ng-data
        int hilos = 2;
    };
    /// Audio de una frase: mono en float.
    using Salida = std::function<void(const float* muestras, std::size_t n, std::uint32_t frecuencia)>;

    Habla();
    ~Habla();
    Habla(const Habla&) = delete;
    Habla& operator=(const Habla&) = delete;

    /// Carga una voz (en su hilo); `listo` con error vacío si anduvo.
    void cargar(const Voz& v, std::function<void(const std::string& error)> listo);
    bool cargada() const { return cargada_; }
    /// Cuántas voces trae el modelo (sharvard trae dos).
    int hablantes() const { return hablantes_; }

    /// Dice `texto` con el hablante `sid`: cada frase va a `salida` apenas
    /// está; `fin` al terminar (o al callarse). Lo que estaba en cola sigue.
    void decir(const std::string& texto, int sid, float velocidad, Salida salida,
               std::function<void()> fin = {});
    /// Corta lo que está diciendo y descarta lo que esperaba.
    void callar();

   private:
    struct Motor;
    void bucle();
    void encolar(std::function<void()> trabajo);

    std::unique_ptr<Motor> motor_;
    std::atomic<bool> cargada_{false};
    std::atomic<int> hablantes_{0};
    std::atomic<std::uint64_t> generacion_{0}; ///< callar() la sube: lo viejo se descarta

    std::mutex colaLock_;
    std::condition_variable colaCv_;
    std::deque<std::function<void()>> cola_;
    bool salir_ = false;
    std::thread hilo_;
};

} // namespace ba::app::audio
