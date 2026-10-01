// ============================================================================
// src/app/audio/MonitorVoz.hpp — la voz del asistente, SOLO a los auriculares
//
// docs/AUDIO-TTS.md: «la voz del asistente va solo al monitoreo, nunca al bus
// de programa. Que no exista el camino, no que haya una casilla destildada.»
//
// Una fuente propia de la app (`ba_voz_asistente`, como el chat o los gráficos)
// creada PRIVADA —no está en ninguna escena ni canal de salida, así que no
// entra a la mezcla— y en MONITOR_ONLY: suena por el dispositivo de monitoreo
// de libobs, el mismo de los auriculares del operador (el PFL de
// audio_escuchar, la voz del lobby). Se le empuja audio con timestamps
// seguidos, frase por frase, a medida que se sintetiza.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

struct obs_source;

namespace ba::app::audio {

/// Registra el tipo de fuente (al arrancar libobs, con las demás).
void registrarMonitorVoz();

class MonitorVoz {
   public:
    MonitorVoz() = default;
    ~MonitorVoz();
    MonitorVoz(const MonitorVoz&) = delete;
    MonitorVoz& operator=(const MonitorVoz&) = delete;

    /// Mono en float. Se encola después de lo que todavía está sonando (las
    /// frases de una respuesta salen pegadas, sin huecos ni encimadas).
    /// Thread-safe. Sin libobs arriba no hace nada.
    void reproducir(const float* muestras, std::size_t n, std::uint32_t frecuencia);
    /// Corta lo que se está diciendo (el operador volvió a apretar la tecla).
    void callar();

   private:
    bool asegurar();
    std::mutex lock_;
    struct obs_source* fuente_ = nullptr;
    std::uint64_t proximo_ = 0; ///< ns: dónde termina lo encolado
};

} // namespace ba::app::audio
