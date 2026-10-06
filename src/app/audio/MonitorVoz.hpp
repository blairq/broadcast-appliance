// ============================================================================
// src/app/audio/MonitorVoz.hpp — «Voz del sistema»: la voz sintetizada, como fuente de audio
//
// Una fuente de audio más de la app (`ba_voz_asistente`, «Voz del sistema»):
// aparece en Audio y en la Matriz como cualquier entrada, se le puede poner un
// rack con filtros y rutearla. Por ella habla el asistente (contestar con voz)
// y la puede usar cualquier otra función que lea texto en voz alta.
//
// De fábrica NO va al aire: tiene su canal de salida (63, para que se pueda
// mezclar) pero ninguna pista, y suena en MONITOR_ONLY por la salida de
// auriculares del operador. Si el operador la rutea a una pista en la Matriz,
// va a donde la mande (docs/AUDIO-TTS.md: antes era privada y sin camino al
// aire; el operador pidió poder rutearla).
//
// Se le empuja audio con timestamps seguidos, frase por frase, a medida que
// se sintetiza. Entra por GStreamer (ba_gst_fuente con `bavoz`, audio/GstVoz):
// su rack corre en barack; sin GStreamer, la fuente de libobs de antes.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

struct obs_source;

namespace ba::app::audio {

/// Registra el tipo de fuente (al arrancar libobs, con las demás).
void registrarMonitorVoz();

class MonitorVoz {
   public:
    /// `nombre`: como aparece en Audio y en la Matriz; `canal`: su canal de
    /// salida (63 la del sistema, 62 la del teleprompter).
    explicit MonitorVoz(std::string nombre = "Voz del sistema", std::uint32_t canal = 63)
        : nombre_(std::move(nombre)), canal_(canal) {}
    ~MonitorVoz();
    MonitorVoz(const MonitorVoz&) = delete;
    MonitorVoz& operator=(const MonitorVoz&) = delete;

    /// Mono en float. Se encola después de lo que todavía está sonando (las
    /// frases de una respuesta salen pegadas, sin huecos ni encimadas).
    /// Thread-safe. Sin libobs arriba no hace nada.
    void reproducir(const float* muestras, std::size_t n, std::uint32_t frecuencia);
    /// Lo mismo, pero que empiece en `inicioNs` (reloj de libobs, os_gettime_ns):
    /// para sincronizar con algo de afuera (el teleprompter). Si ya pasó, sale
    /// enseguida.
    void reproducirEn(const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t inicioNs);
    /// Corta lo que se está diciendo (el operador volvió a apretar la tecla).
    /// Silencia hasta que se acabe lo encolado (la fuente no se rearma: así no
    /// se pierden su rack ni su ruteo).
    void callar();
    /// Crea la fuente ya (al arrancar), para que esté en Audio y en la Matriz
    /// antes de que hable por primera vez.
    void preparar();

   private:
    bool asegurar();
    std::mutex lock_;
    struct obs_source* fuente_ = nullptr;
    void empujar(const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t ts);
    std::string nombre_;
    std::uint32_t canal_;
    std::uint64_t proximo_ = 0;       ///< ns: dónde termina lo encolado
    std::uint64_t silencioHasta_ = 0; ///< ns: callada hasta que se acabe lo encolado
    bool gst_ = false;                 ///< por GStreamer (ba_gst_fuente + bavoz) o la fuente de libobs de antes
};

} // namespace ba::app::audio
