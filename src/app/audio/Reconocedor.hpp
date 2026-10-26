// ============================================================================
// src/app/audio/Reconocedor.hpp — lo que tiene que saber hacer un motor de voz a texto
//
// HAY DOS MOTORES, y se elige por quién escucha (agent/Reconocedores):
//   * Parakeet (audio/Escucha): offline, frase por frase. El VAD corta la
//     frase al primer silencio y recién ahí se transcribe entera. Muy bueno
//     en castellano, pero cada corriente se transcribe de a una y el texto
//     llega 1-3 s después de que la persona termina.
//   * Nemotron (audio/EscuchaStreaming): streaming. UN reconocedor con un
//     stream por corriente, que se decodifican JUNTOS (en lote) cada vez que
//     hay audio: muchas voces a la vez sin hacer cola, con texto parcial
//     mientras se habla y el fin de turno que marca el propio modelo.
//
// Los dos se usan igual: una toma suelta (`transcribir`, el dictado) y
// corrientes continuas con clave (`abrirContinuo`/`alimentarContinuo`/
// `cerrarContinuo`) cuyas frases salen por `setFrase`. Quien los usa (agent/Voz)
// no sabe cuál tiene adelante.
//
// HILOS. Cada motor tiene su hilo de trabajo, dueño de los modelos. `alimentar*`
// corre en el hilo de audio (GStreamer): solo copia bajo un mutex. Los
// callbacks (`Listo`, `Frase`, `EmpiezaVoz`) llegan EN EL HILO DEL MOTOR: quien
// los recibe los pasa al suyo.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ba::app::audio {

class Reconocedor {
   public:
    /// Lo que salió de una toma o de una frase.
    struct Resultado {
        std::string texto;
        std::string error;
        double segundosDeAudio = 0; ///< lo que se habló (después del VAD)
        double ms = 0;              ///< lo que tardó en transcribir
    };
    using Listo = std::function<void(const Resultado&)>;
    using Frase = std::function<void(const std::string& clave, const Resultado& r)>;
    /// Una corriente empezó a oír voz (el VAD pasó de silencio a voz). Sirve
    /// para cortar al asistente cuando lo interrumpen (el tótem).
    using EmpiezaVoz = std::function<void(const std::string& clave)>;
    /// Cuándo se corta una frase. `pausa`: silencio que la termina (s);
    /// `maximo`: lo más que dura (s; al pasarlo se corta en la próxima pausa
    /// corta, no en seco); `umbral`: cuán seguro tiene que estar el detector de
    /// que es voz (0..1; más alto ignora más el ruido de la sala).
    struct Cortes {
        double pausa = 0.35;
        double maximo = 6.0;
        double umbral = 0.5;
    };
    /// Lo que oye cada corriente ahora (para el indicador de la interfaz).
    struct Oido {
        bool hablando = false;
        double segundos = 0;  ///< de la frase en curso
        std::string parcial;  ///< lo entendido hasta ahora (solo los motores streaming)
    };

    virtual ~Reconocedor() = default;

    /// Los modelos están cargados y se puede transcribir.
    virtual bool cargado() const = 0;
    /// Suelta los modelos (en su hilo). Las corrientes abiertas siguen abiertas
    /// pero no transcriben hasta que se vuelva a cargar.
    virtual void descargar() = 0;

    /// Una toma ya juntada afuera (el dictado): `listo` en el hilo del motor.
    virtual void transcribir(std::vector<float> audio, std::uint32_t frecuencia, Listo listo) = 0;

    virtual void setCortes(const Cortes& c) = 0;
    virtual Cortes cortes() const = 0;
    virtual std::map<std::string, Oido> oido() const = 0;
    virtual void setFrase(Frase f) = 0;
    virtual void setEmpiezaVoz(EmpiezaVoz f) = 0;
    virtual void abrirContinuo(const std::string& clave) = 0;
    virtual void cerrarContinuo(const std::string& clave) = 0;
    /// Desde el hilo de audio: copia y vuelve. Sin corriente abierta no hace nada.
    virtual void alimentarContinuo(const std::string& clave, const float* muestras, std::size_t n,
                                   std::uint32_t frecuencia) = 0;
};

} // namespace ba::app::audio
