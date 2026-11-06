// ============================================================================
// src/motor/TrazaPad.hpp — SONDA: cada cuadro que pasa por un pad, a un tramo (diag/TrazaCuadros)
//
// Solo con BA_SONDAS_CUADROS (sin la macro, trazarPad no hace nada y no pone ninguna sonda).
// Anota la hora en que pasa (steady_clock, µs), la hora de lo que muestra (su PTS pasado al
// reloj de los sellos, como Vistas::contenidoQpc: la misma hora de origen en todos los tramos
// de un cuadro) y, de dato, cuántos µs pasaron desde el anterior en este pad.
// ============================================================================
#pragma once

#include "diag/SondasCuadros.hpp"
#include "diag/TrazaCuadros.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/HoraQpc.hpp"

#include <gst/gst.h>

#include <memory>
#include <string>

namespace ba::motor {

/// La hora (µs del reloj de los sellos) del PTS `pts` en el segmento `seg` del pipeline de
/// `el` (su reloj y su base). 0 si no se sabe.
inline std::int64_t origenUsDePts(GstElement* el, const GstSegment* seg, GstClockTime pts) {
    if (el == nullptr || seg == nullptr || !GST_CLOCK_TIME_IS_VALID(pts)) return 0;
    const GstClockTime rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, pts);
    if (!GST_CLOCK_TIME_IS_VALID(rt)) return 0;
    GstClock* reloj = gst_element_get_clock(el);
    if (reloj == nullptr) return 0;
    const std::uint64_t q = ba::ipc::HealthBlock::nowQpc();
    const GstClockTime ahora = gst_clock_get_time(reloj);
    gst_object_unref(reloj);
    return static_cast<std::int64_t>(ba::ipc::qpcDeOtroReloj(gst_element_get_base_time(el) + rt, ahora, q) / 1000);
}

/// Pone en `pad` una sonda de buffers que anota cada uno en el tramo `nombre`.
inline void trazarPad(GstPad* pad, const std::string& nombre) {
    if constexpr (!ba::diag::kSondasCuadros) return;
    if (pad == nullptr) return;
    struct Datos {
        std::shared_ptr<ba::diag::TrazaCuadros> tr;
        std::int64_t antes = 0;
    };
    gst_pad_add_probe(
        pad, GST_PAD_PROBE_TYPE_BUFFER,
        [](GstPad* p, GstPadProbeInfo* info, gpointer d) -> GstPadProbeReturn {
            auto* x = static_cast<Datos*>(d);
            const std::int64_t t = ba::diag::TrazaCuadros::ahoraUs();
            GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
            std::int64_t origen = 0;
            if (b != nullptr && GST_BUFFER_PTS_IS_VALID(b))
                if (GstEvent* ev = gst_pad_get_sticky_event(p, GST_EVENT_SEGMENT, 0)) {
                    const GstSegment* seg = nullptr;
                    gst_event_parse_segment(ev, &seg);
                    if (GstElement* el = gst_pad_get_parent_element(p)) {
                        origen = origenUsDePts(el, seg, GST_BUFFER_PTS(b));
                        gst_object_unref(el);
                    }
                    gst_event_unref(ev);
                }
            x->tr->anotar(t, origen, x->antes != 0 ? t - x->antes : 0);
            x->antes = t;
            return GST_PAD_PROBE_OK;
        },
        new Datos{ba::diag::traza(nombre)}, [](gpointer d) { delete static_cast<Datos*>(d); });
}

} // namespace ba::motor
