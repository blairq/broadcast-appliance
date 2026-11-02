// ============================================================================
// src/gst-plugins/ipc/sinhilo.hpp — lo común de una fuente en vivo SIN hilo propio
//
// baanillocomun y bavideoanillocomun no son GstBaseSrc (que tiene su hilo): son un
// GstElement con un pad de salida por el que empuja el lector común (comun.hpp). Lo que
// hace basesrc y acá se hace a mano:
//   - en vivo: READY→PAUSED y PLAYING→PAUSED devuelven NO_PREROLL (el pipeline no espera
//     un primer buffer para pasar a PAUSED); se lee solo en PLAYING;
//   - antes del primer buffer, stream-start, caps y un segmento TIME desde 0 (las horas
//     son las del reloj del pipeline menos su base, como las de una fuente en vivo);
//     caps nuevas si cambia el formato;
//   - un error del flujo (no negociado, error) se avisa al bus UNA vez, como basesrc: el
//     dueño del pipeline lo rearma.
// ============================================================================
#pragma once

#include <gst/gst.h>

namespace ba::ipc_gst {

struct SalidaSinHilo {
    GstPad* pad = nullptr;
    GstCaps* caps = nullptr; ///< las últimas que se mandaron (con referencia)
    bool eventos = true;     ///< falta stream-start/caps/segmento (al activar)
    bool fallo = false;      ///< ya se avisó un error del flujo

    /// Al pasar a PAUSED (o al desactivar): de nuevo desde el principio.
    void reiniciar() {
        eventos = true;
        fallo = false;
        if (caps != nullptr) gst_caps_unref(caps);
        caps = nullptr;
    }

    /// Empuja `b` (se lo queda) con `c` (las caps del buffer: si cambian, se mandan antes).
    GstFlowReturn empujar(GstElement* e, GstBuffer* b, GstCaps* c) {
        if (eventos) {
            gchar* id = gst_pad_create_stream_id(pad, e, nullptr);
            GstEvent* ss = gst_event_new_stream_start(id);
            g_free(id);
            gst_event_set_group_id(ss, gst_util_group_id_next());
            gst_pad_push_event(pad, ss);
        }
        if (eventos || caps == nullptr || !gst_caps_is_equal(caps, c)) {
            if (caps != nullptr) gst_caps_unref(caps);
            caps = gst_caps_ref(c);
            gst_pad_push_event(pad, gst_event_new_caps(c));
        }
        if (eventos) {
            GstSegment s;
            gst_segment_init(&s, GST_FORMAT_TIME);
            gst_pad_push_event(pad, gst_event_new_segment(&s));
            eventos = false;
        }
        const GstFlowReturn r = gst_pad_push(pad, b);
        if (r < GST_FLOW_EOS && r != GST_FLOW_FLUSHING && !fallo) {
            fallo = true;
            GST_ELEMENT_FLOW_ERROR(e, r);
        }
        return r;
    }
};

/// Las consultas del pad: en vivo con `latencia` (mín.) y `maxima`; las caps, las del molde.
inline gboolean consultaSinHilo(GstPad* pad, GstObject* padre, GstQuery* q, GstClockTime latencia, GstClockTime maxima) {
    switch (GST_QUERY_TYPE(q)) {
        case GST_QUERY_LATENCY:
            gst_query_set_latency(q, TRUE, latencia, maxima);
            return TRUE;
        case GST_QUERY_SCHEDULING:
            gst_query_set_scheduling(q, GST_SCHEDULING_FLAG_SEQUENTIAL, 1, -1, 0);
            gst_query_add_scheduling_mode(q, GST_PAD_MODE_PUSH);
            return TRUE;
        default: return gst_pad_query_default(pad, padre, q);
    }
}

/// Los eventos que suben (reconfigurar, QoS, latencia): no hay nada que hacer con ellos.
inline gboolean eventoSinHilo(GstPad*, GstObject*, GstEvent* ev) {
    const bool ok = GST_EVENT_TYPE(ev) != GST_EVENT_SEEK;
    gst_event_unref(ev);
    return ok ? TRUE : FALSE;
}

} // namespace ba::ipc_gst
