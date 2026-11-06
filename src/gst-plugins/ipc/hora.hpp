// ============================================================================
// src/gst-plugins/ipc/hora.hpp — la hora del gateway en el reloj del pipeline
//
// El gateway sella cada cuadro y cada bloque de audio con su reloj monotónico
// (HealthBlock::nowQpc: steady_clock en ns; QPC en Windows) al publicarlo. Para
// que el audio y el video de un invitado —dos cadenas distintas— queden
// alineados por esa hora y no por cuándo los leímos (el sondeo agrega
// temblor), se pasa al reloj del pipeline: se leen los DOS relojes en el mismo
// instante y la diferencia traduce, sin suponer que tengan la misma base.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::ipc_gst {

/// El reloj del gateway (lo mismo que HealthBlock::nowQpc).
inline std::uint64_t ahoraQpc() {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0 || !QueryPerformanceCounter(&c)) return 0;
    const std::uint64_t ff = static_cast<std::uint64_t>(f.QuadPart), cc = static_cast<std::uint64_t>(c.QuadPart);
    return (cc / ff) * 1000000000ULL + (cc % ff) * 1000000000ULL / ff;
#else
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

/// La hora de algo sellado con `qpc`, en tiempo de ejecución del pipeline de `e`
/// (GST_CLOCK_TIME_NONE sin reloj). Sin sello válido (0, del futuro o de hace
/// más de 5 s): la de ahora.
inline GstClockTime horaDeQpc(GstElement* e, std::uint64_t qpc) {
    GstClock* reloj = gst_element_get_clock(e);
    if (reloj == nullptr) return GST_CLOCK_TIME_NONE;
    const GstClockTime t = gst_clock_get_time(reloj);
    const std::uint64_t q = ahoraQpc();
    gst_object_unref(reloj);
    const GstClockTime base = gst_element_get_base_time(e);
    GstClockTime captura = t;
    if (qpc != 0 && qpc <= q && q - qpc < 5 * GST_SECOND) captura = t - (q - qpc);
    return captura >= base ? captura - base : 0;
}

/// Lo mismo para varios sellos de una misma vuelta: los dos relojes se leen UNA vez.
/// `futuro`: cuánto del futuro se acepta (la hora del contenido de un invitado, la que le
/// dio libwebrtc, puede ser unos ms adelante: la hora a la que lo mostraría).
struct TraductorQpc {
    GstClockTime reloj = GST_CLOCK_TIME_NONE, base = 0;
    std::uint64_t qpc = 0;
    explicit TraductorQpc(GstElement* e) {
        GstClock* r = gst_element_get_clock(e);
        if (r == nullptr) return;
        reloj = gst_clock_get_time(r);
        qpc = ahoraQpc();
        gst_object_unref(r);
        base = gst_element_get_base_time(e);
    }
    bool vale() const { return GST_CLOCK_TIME_IS_VALID(reloj); }
    /// La hora de `sello` en el pipeline; sin sello creíble (0, de hace más de 5 s o más
    /// adelante que `futuro`), GST_CLOCK_TIME_NONE.
    GstClockTime hora(std::uint64_t sello, std::uint64_t futuro = 0) const {
        if (!vale() || sello == 0) return GST_CLOCK_TIME_NONE;
        GstClockTime h;
        if (sello <= qpc) {
            if (qpc - sello >= 5 * GST_SECOND) return GST_CLOCK_TIME_NONE;
            h = reloj - (qpc - sello);
        } else {
            if (sello - qpc > futuro) return GST_CLOCK_TIME_NONE;
            h = reloj + (sello - qpc);
        }
        return h >= base ? h - base : 0;
    }
    /// Ahora, en el pipeline.
    GstClockTime ahora() const { return vale() && reloj >= base ? reloj - base : 0; }
};

} // namespace ba::ipc_gst
