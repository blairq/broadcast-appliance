// ============================================================================
// src/app/audio/Escucha.hpp — de la voz del operador a texto (push-to-talk)
//
// QUE ES. El oído del control por voz (docs/funciones/AUDIO-TTS.md): mientras el
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
#include <map>
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
    /// Transcribe una toma ya juntada afuera (el dictado desde los nodos del
    /// rack). Mismo resultado y mismo hilo que `terminar`.
    void transcribir(std::vector<float> audio, std::uint32_t frecuencia, Listo listo);

    // --- Transcripción continua (subtítulos, acta) ------------------------------
    // Una «corriente» por fuente: el audio llega sin parar, su VAD corta las
    // frases y cada una se transcribe apenas termina (1-3 s de retraso). Usa
    // el MISMO reconocedor que el dictado (un solo modelo en memoria).
    using Frase = std::function<void(const std::string& clave, const Resultado& r)>;
    /// Cuándo se corta una frase. `pausa`: silencio que la termina (s);
    /// `maximo`: lo más que dura (s; al pasarlo se corta en la próxima pausa
    /// corta, no en seco); `umbral`: cuán seguro tiene que estar el detector de
    /// que es voz (0..1; más alto ignora más el ruido de la sala).
    struct Cortes {
        double pausa = 0.35;
        double maximo = 6.0;
        double umbral = 0.5;
    };
    /// Cambia los cortes de todas las corrientes (las rearma: se pierde la frase en curso).
    void setCortes(const Cortes& c);
    Cortes cortes() const;
    /// Lo que oye cada corriente ahora: clave -> {hablando, segundos de la frase en curso}.
    struct Oido {
        bool hablando = false;
        double segundos = 0;
    };
    std::map<std::string, Oido> oido() const;
    /// A dónde van las frases (en el hilo de Escucha).
    void setFrase(Frase f);
    void abrirContinuo(const std::string& clave);
    void cerrarContinuo(const std::string& clave);
    /// Desde el hilo de audio: copia y vuelve. Sin corriente abierta no hace nada.
    void alimentarContinuo(const std::string& clave, const float* muestras, std::size_t n,
                           std::uint32_t frecuencia);

   private:
    struct Motor;     // sherpa-onnx: solo existe en el hilo de trabajo
    struct Corriente; // VAD y remuestreo de una fuente (hilo de trabajo)
    void bucle();
    void atenderCorrientes();
    void encolar(std::function<void()> trabajo);
    Resultado transcribir(std::vector<float> audio, std::uint32_t frecuencia);

    std::unique_ptr<Motor> motor_;
    std::atomic<bool> cargado_{false};
    std::atomic<bool> grabando_{false};

    std::mutex tomaLock_;
    std::vector<float> toma_;
    std::uint32_t frecuencia_ = 48000;

    // Lo que llegó de cada corriente y todavía no se procesó.
    struct Pendiente {
        std::vector<float> audio;
        std::uint32_t frecuencia = 48000;
        bool abierta = true;
    };
    mutable std::mutex contLock_;
    std::map<std::string, Pendiente> pendientes_;
    std::map<std::string, std::unique_ptr<Corriente>> corrientes_; // solo el hilo de trabajo
    Frase frase_;
    std::string vadModelo_;
    Cortes cortes_;                    ///< bajo contLock_
    int versionCortes_ = 0;            ///< bajo contLock_: al cambiar, las corrientes se rearman
    std::map<std::string, Oido> oido_; ///< bajo contLock_

    std::mutex colaLock_;
    std::condition_variable colaCv_;
    std::deque<std::function<void()>> cola_;
    bool salir_ = false;
    std::thread hilo_;
};

} // namespace ba::app::audio
