// ============================================================================
// src/app/audio/EscuchaStreaming.hpp — voz a texto en streaming: muchas voces a la vez
//
// QUE ES. El segundo motor de voz a texto (audio/Reconocedor.hpp), pensado para
// Nemotron 3.5 ASR Streaming (NVIDIA, sherpa-onnx): UN modelo en memoria y un
// stream por corriente (cada tótem, cada persona atendida, cada «Voz a texto»
// continua). En cada vuelta se decodifican JUNTOS todos los streams que tienen
// audio listo (un paso en lote: una sola corrida del encoder para N voces), así
// que diez voces no hacen cola detrás de la primera como en el motor offline.
//
// EL VAD MANDA QUÉ SE LE PASA AL MODELO. El modelo streaming procesa todo lo que
// recibe, silencio incluido: con diez tótems mudos se gastaría CPU en nada. Cada
// corriente tiene su detector de voz (Silero); al modelo le llega solo lo que
// tiene voz, con un MARGEN ANTES (el comienzo de la palabra, que el detector
// marca tarde) y un MARGEN DESPUÉS (la cola de la frase y el silencio que el
// modelo necesita para cerrar el turno).
//
// LA FRASE SE CIERRA por lo que pase primero:
//   * el fin de turno del modelo (las reglas de endpoint de sherpa-onnx sobre
//     sus propios blancos): el texto sale y el stream sigue, reiniciado;
//   * el margen después de la voz se acabó sin que vuelva: se cierra la entrada,
//     se decodifica lo que falta, sale el texto y el stream se suelta;
//   * pasó el máximo de la frase (los cortes): se cierra en la primera pausa
//     (y, si no hay ninguna, al doble se reinicia el turno sin cortar el audio).
// Mientras tanto, lo entendido va en `oido()` como texto parcial.
//
// PROBABLE SIN MODELOS. Lo que hace falta de sherpa-onnx está detrás de dos
// interfaces chicas (Modelo y Detector): EscuchaNemotron.cpp las implementa con
// sherpa-onnx y los tests con unas falsas (test_reconocedores), para probar el
// lote, los márgenes y los cortes sin bajar 700 MB.
// ============================================================================
#pragma once

#include "audio/Reconocedor.hpp"

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

class EscuchaStreaming : public Reconocedor {
   public:
    static constexpr std::uint32_t kFrecuencia = 16000; ///< la que esperan el modelo y el VAD

    /// Lo que hace falta del reconocedor streaming. Los streams son números
    /// que da el propio modelo. Todo se llama desde el hilo del motor.
    class Modelo {
       public:
        virtual ~Modelo() = default;
        virtual int nuevoStream() = 0;
        virtual void soltarStream(int s) = 0;
        /// Muestras a 16 kHz.
        virtual void aceptar(int s, const float* m, std::size_t n) = 0;
        /// No llega más audio a ese stream: lo que queda se decodifica con relleno.
        virtual void terminarEntrada(int s) = 0;
        virtual bool listo(int s) = 0;
        /// Un paso de decodificación de TODOS esos streams juntos (en lote).
        virtual void decodificar(const std::vector<int>& streams) = 0;
        virtual std::string texto(int s) = 0;
        /// El modelo dice que terminó el turno (endpoint).
        virtual bool finDeTurno(int s) = 0;
        /// Empieza un turno nuevo en el mismo stream (después de un fin de turno).
        virtual void reiniciar(int s) = 0;
        /// Un remuestreador a 16 kHz para una corriente (con estado: va de a
        /// bloques). El de fábrica es uno simple; sherpa-onnx trae uno mejor.
        virtual std::function<std::vector<float>(const float*, std::size_t)> remuestreador(std::uint32_t frecuencia);
    };
    /// El detector de voz de una corriente (Silero), armado con los cortes.
    class Detector {
       public:
        virtual ~Detector() = default;
        /// Muestras por ventana a 16 kHz (512 en Silero: 32 ms).
        virtual std::size_t ventana() const = 0;
        /// Una ventana: si tiene voz.
        virtual bool voz(const float* m) = 0;
    };
    using FabricaModelo = std::function<std::unique_ptr<Modelo>(std::string& error)>;
    using FabricaDetector = std::function<std::unique_ptr<Detector>(const Cortes&)>;

    /// Cuánto audio de antes y de después de la voz se le pasa al modelo (s).
    struct Margenes {
        double antes = 0.5;  ///< Silero marca la voz recién a los 0,25 s: esto recupera el comienzo
        double despues = 0.8; ///< la cola de la frase y el silencio con que el modelo cierra el turno
    };
    /// Cuántos streams se decodificaron juntos (para el estado y los tests).
    struct Lote {
        int ultimo = 0;
        int maximo = 0;
        std::uint64_t pasos = 0;     ///< llamadas a decodificar
        double segundosAlModelo = 0; ///< audio que se le pasó al modelo
        double segundosOidos = 0;    ///< audio que llegó (la diferencia es lo que ahorró el VAD)
    };

    EscuchaStreaming();
    ~EscuchaStreaming() override;
    EscuchaStreaming(const EscuchaStreaming&) = delete;
    EscuchaStreaming& operator=(const EscuchaStreaming&) = delete;

    /// Carga en su hilo. `listo` recibe "" si anduvo (en el hilo del motor).
    void cargar(FabricaModelo modelo, FabricaDetector detector, std::function<void(const std::string&)> listo);
    void setMargenes(const Margenes& m);
    Lote lote() const;

    bool cargado() const override { return cargado_; }
    void descargar() override;
    void transcribir(std::vector<float> audio, std::uint32_t frecuencia, Listo listo) override;
    void setCortes(const Cortes& c) override;
    Cortes cortes() const override;
    std::map<std::string, Oido> oido() const override;
    void setFrase(Frase f) override;
    void setEmpiezaVoz(EmpiezaVoz f) override;
    void abrirContinuo(const std::string& clave) override;
    void cerrarContinuo(const std::string& clave) override;
    void alimentarContinuo(const std::string& clave, const float* muestras, std::size_t n,
                           std::uint32_t frecuencia) override;

    /// Espera a que el hilo procese lo que llegó hasta ahora (para los tests).
    void esperarVuelta();

   private:
    struct Corriente;
    void bucle();
    void encolar(std::function<void()> trabajo);
    void atenderCorrientes();
    void decodificarListos(const std::vector<int>& streams);
    /// Termina la frase de una corriente con el texto que tenga su stream.
    void emitir(const std::string& clave, Corriente& c, const Frase& frase);
    /// Lo del texto del stream que todavía no salió como frase.
    static std::string nuevoDe(const Corriente& c, const std::string& texto);

    std::unique_ptr<Modelo> modelo_; ///< solo el hilo del motor
    FabricaDetector fabricaDetector_; ///< solo el hilo del motor
    std::atomic<bool> cargado_{false};

    struct Pendiente {
        std::vector<float> audio;
        std::uint32_t frecuencia = 48000;
        bool abierta = true;
    };
    mutable std::mutex contLock_;
    std::map<std::string, Pendiente> pendientes_;
    std::map<std::string, std::unique_ptr<Corriente>> corrientes_; ///< solo el hilo del motor
    Frase frase_;
    EmpiezaVoz empiezaVoz_;
    Cortes cortes_;          ///< bajo contLock_
    Margenes margenes_;      ///< bajo contLock_
    int versionCortes_ = 0;  ///< bajo contLock_
    std::map<std::string, Oido> oido_; ///< bajo contLock_
    Lote lote_;                        ///< bajo contLock_
    std::uint64_t vueltas_ = 0;        ///< bajo contLock_

    std::mutex colaLock_;
    std::condition_variable colaCv_;
    std::condition_variable vueltaCv_;
    std::deque<std::function<void()>> cola_;
    bool salir_ = false;
    bool despertar_ = false; ///< esperarVuelta: que no espere los 100 ms
    std::thread hilo_;
};

} // namespace ba::app::audio
