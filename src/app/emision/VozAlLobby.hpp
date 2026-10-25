// ============================================================================
// src/app/emision/VozAlLobby.hpp — la voz del que atiende, hacia la sala
//
// La atención hablada (ui/AtencionViewModel, docs/funciones/ATENCION.md): lo
// que contesta el asistente se sintetiza (audio/Habla) y llega acá; esto lo
// publica en BA.Audio.Atendedor, y el gateway se lo baja SOLO a quien se está
// atendiendo hablando (`atiende` en su ruta). No es la voz del operador
// (BA.Audio.Operator): las dos van por canales distintos, así el micrófono del
// operador nunca le llega a quien atiende el asistente, ni la voz sintetizada a
// quien el operador le está hablando.
//
// EN TIEMPO REAL, con su propio reloj. La síntesis entrega una frase entera de
// golpe (segundos de audio en milisegundos) y el anillo compartido guarda 160
// ms: empujarla toda junta la perdería casi entera. Un hilo saca bloques de 10
// ms al ritmo del reloj —con silencio cuando no hay nada que decir, para que
// el gateway no dé el canal por muerto entre frase y frase—.
//
// Threading: abrir/cerrar desde la interfaz; encolar desde cualquier hilo (el
// de la síntesis).
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "ipc/SharedAudioRing.hpp"

namespace ba::app {

class VozAlLobby {
   public:
    VozAlLobby() = default;
    ~VozAlLobby();
    VozAlLobby(const VozAlLobby&) = delete;
    VozAlLobby& operator=(const VozAlLobby&) = delete;

    /// Arranca el reloj (crea el canal la primera vez). Si ya andaba, nada.
    void abrir();
    /// Lo para y tira lo que faltaba decir: no se atiende a nadie hablando.
    void cerrar();
    bool abierto() const { return hilo_.joinable(); }
    /// Mono en float a cualquier frecuencia: se pasa a 48 kHz y se encola
    /// detrás de lo que falta decir. Thread-safe.
    void encolar(const float* muestras, std::size_t n, std::uint32_t frecuencia);
    /// Tira lo que faltaba decir (lo que ya salió, salió).
    void callar();
    /// Segundos que faltan decir.
    double pendiente() const;

   private:
    void bucle();

    ba::ipc::SharedAudioRing ring_;
    std::thread hilo_;
    std::atomic<bool> salir_{false};
    mutable std::mutex lock_;
    std::deque<float> cola_; ///< mono a 48 kHz
};

} // namespace ba::app
