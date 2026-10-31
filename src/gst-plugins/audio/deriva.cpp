// ============================================================================
// src/gst-plugins/audio/deriva.cpp — baderiva: la placa al reloj del estudio
//
// Va justo después de la fuente de una placa (motor/Piezas.cpp lo pone ahí
// solo). Mira a qué hora del reloj del pipeline (el del estudio) llega cada
// bloque, sigue con un lazo el reloj de la placa y remuestrea lo justo para
// que salgan exactamente las muestras que corresponden al reloj del estudio
// (ba::dsp::Deriva, con libsamplerate). Sin esto, una placa a 100 ppm se corre
// 50 ms cada ~8 minutos y el reanclaje deja un hueco o un recorte audible.
//
//   - El sello de lo que sale es la hora de captura en la grilla del estudio;
//     lo que el remuestreador retiene se suma a la consulta LATENCY.
//   - activo=false: pasa tal cual (para comparar).
//   - ppm, reanclajes, desfase-ms, enganchado (solo lectura): el diagnóstico
//     de la placa (sistema_recursos → deriva_placas).
//   - F32LE, S16LE o S32LE intercalado, cualquier frecuencia y canales: no
//     cambia el formato (no hace falta un audioconvert delante).
//
//   gst-launch-1.0 osxaudiosrc ! baderiva ! audioconvert ! autoaudiosink
// ============================================================================

#include "dsp/Deriva.hpp"

#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <samplerate.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_DERIVA (ba_deriva_get_type())
G_DECLARE_FINAL_TYPE(BaDeriva, ba_deriva, BA, DERIVA, GstElement)
G_END_DECLS

namespace {
/// Lo que el hilo de streaming publica para quien lee las propiedades.
struct Medido {
    std::atomic<double> ppm{0}, desfaseMs{0}, latenciaS{0};
    std::atomic<guint64> reanclajes{0};
    std::atomic<bool> enganchado{false};
};
} // namespace

struct _BaDeriva {
    GstElement parent;
    GstPad* sink;
    GstPad* src;
    // Solo el hilo de streaming (y los eventos, que van por el mismo hilo):
    ba::dsp::Deriva* deriva;
    GstAudioInfo info;
    bool listo;
    std::vector<float>* entra;
    std::vector<float>* sale;
    // De cualquiera:
    std::atomic<bool>* activo;
    Medido* medido;
};

G_DEFINE_TYPE(BaDeriva, ba_deriva, GST_TYPE_ELEMENT)

enum { PROP_0, PROP_ACTIVO, PROP_PPM, PROP_REANCLAJES, PROP_DESFASE, PROP_ENGANCHADO };

#define BA_DERIVA_CAPS                                                                                                  \
    "audio/x-raw, format=(string){ F32LE, S16LE, S32LE }, layout=(string)interleaved, rate=(int)[ 1, MAX ], "          \
    "channels=(int)[ 1, MAX ]"

static GstStaticPadTemplate g_sink = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(BA_DERIVA_CAPS));
static GstStaticPadTemplate g_src = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(BA_DERIVA_CAPS));

static void reiniciar(BaDeriva* d) {
    d->deriva->reiniciar();
    d->medido->enganchado = false;
}

static GstFlowReturn cadena(GstPad*, GstObject* padre, GstBuffer* buf) {
    auto* d = BA_DERIVA(padre);
    if (!d->listo || !d->activo->load(std::memory_order_relaxed)) return gst_pad_push(d->src, buf);
    // La hora de llegada en el reloj del pipeline (el del estudio): sin reloj
    // todavía (antes de PLAYING) no hay con qué comparar y pasa tal cual.
    GstClock* reloj = gst_element_get_clock(GST_ELEMENT(d));
    if (reloj == nullptr) return gst_pad_push(d->src, buf);
    const GstClockTime ahora = gst_clock_get_time(reloj);
    gst_object_unref(reloj);
    const GstClockTime base = gst_element_get_base_time(GST_ELEMENT(d));
    if (ahora < base) return gst_pad_push(d->src, buf);

    const GstAudioFormat formato = GST_AUDIO_INFO_FORMAT(&d->info);
    const int canales = GST_AUDIO_INFO_CHANNELS(&d->info);
    const int bpf = GST_AUDIO_INFO_BPF(&d->info);
    const int rate = GST_AUDIO_INFO_RATE(&d->info);
    GstMapInfo m;
    if (!gst_buffer_map(buf, &m, GST_MAP_READ)) {
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }
    const std::size_t n = m.size / static_cast<std::size_t>(bpf);
    const std::size_t muestras = n * static_cast<std::size_t>(canales);
    if (d->entra->size() < muestras) d->entra->resize(muestras);
    if (formato == GST_AUDIO_FORMAT_F32LE)
        std::copy_n(reinterpret_cast<const float*>(m.data), muestras, d->entra->data());
    else if (formato == GST_AUDIO_FORMAT_S16LE)
        src_short_to_float_array(reinterpret_cast<const short*>(m.data), d->entra->data(), static_cast<int>(muestras));
    else
        src_int_to_float_array(reinterpret_cast<const int*>(m.data), d->entra->data(), static_cast<int>(muestras));
    gst_buffer_unmap(buf, &m);

    const std::size_t cap = d->deriva->capacidadPara(n);
    if (d->sale->size() < cap * static_cast<std::size_t>(canales)) d->sale->resize(cap * static_cast<std::size_t>(canales));
    double ptsS = 0;
    bool corte = false;
    const double llegadaS = static_cast<double>(ahora - base) / GST_SECOND;
    const std::size_t hechos = d->deriva->procesar(d->entra->data(), n, llegadaS, d->sale->data(), cap, ptsS, corte);

    Medido& med = *d->medido;
    med.ppm = d->deriva->ppm();
    med.desfaseMs = d->deriva->desfaseMs();
    med.reanclajes = d->deriva->reanclajes();
    const bool engancho = d->deriva->enganchado() && !med.enganchado.exchange(d->deriva->enganchado());
    const double latAntes = med.latenciaS.exchange(d->deriva->latenciaS());
    // La latencia que se informa cambia una vez, al enganchar: que la tubería la vuelva a pedir.
    if (engancho && std::fabs(latAntes - d->deriva->latenciaS()) > 1e-4)
        gst_element_post_message(GST_ELEMENT(d), gst_message_new_latency(GST_OBJECT(d)));

    if (hechos == 0) {
        gst_buffer_unref(buf);
        return GST_FLOW_OK;
    }
    GstBuffer* out = gst_buffer_new_allocate(nullptr, hechos * static_cast<std::size_t>(bpf), nullptr);
    gst_buffer_copy_into(out, buf, GST_BUFFER_COPY_FLAGS, 0, 0);
    gst_buffer_unref(buf);
    GstMapInfo mo;
    if (!gst_buffer_map(out, &mo, GST_MAP_WRITE)) {
        gst_buffer_unref(out);
        return GST_FLOW_ERROR;
    }
    const std::size_t total = hechos * static_cast<std::size_t>(canales);
    if (formato == GST_AUDIO_FORMAT_F32LE)
        std::copy_n(d->sale->data(), total, reinterpret_cast<float*>(mo.data));
    else if (formato == GST_AUDIO_FORMAT_S16LE)
        src_float_to_short_array(d->sale->data(), reinterpret_cast<short*>(mo.data), static_cast<int>(total));
    else
        src_float_to_int_array(d->sale->data(), reinterpret_cast<int*>(mo.data), static_cast<int>(total));
    gst_buffer_unmap(out, &mo);
    GST_BUFFER_PTS(out) = ptsS > 0 ? static_cast<GstClockTime>(ptsS * GST_SECOND) : 0;
    GST_BUFFER_DTS(out) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(out) = gst_util_uint64_scale(hechos, GST_SECOND, static_cast<guint64>(rate));
    if (corte)
        GST_BUFFER_FLAG_SET(out, GST_BUFFER_FLAG_DISCONT);
    else
        GST_BUFFER_FLAG_UNSET(out, GST_BUFFER_FLAG_DISCONT);
    return gst_pad_push(d->src, out);
}

static gboolean evento(GstPad* pad, GstObject* padre, GstEvent* e) {
    auto* d = BA_DERIVA(padre);
    switch (GST_EVENT_TYPE(e)) {
        case GST_EVENT_CAPS: {
            GstCaps* caps = nullptr;
            gst_event_parse_caps(e, &caps);
            d->listo = gst_audio_info_from_caps(&d->info, caps);
            if (d->listo) {
                ba::dsp::Deriva::Opciones o;
                o.frecuencia = GST_AUDIO_INFO_RATE(&d->info);
                o.canales = GST_AUDIO_INFO_CHANNELS(&d->info);
                d->deriva->configurar(o);
                d->medido->enganchado = false;
            }
            break;
        }
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START: reiniciar(d); break;
        default: break;
    }
    return gst_pad_event_default(pad, padre, e);
}

/// LATENCY: lo de arriba más lo que retiene el remuestreador.
static gboolean consulta(GstPad* pad, GstObject* padre, GstQuery* q) {
    if (!gst_pad_query_default(pad, padre, q)) return FALSE;
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        auto* d = BA_DERIVA(padre);
        gboolean vivo = FALSE;
        GstClockTime min = 0, max = GST_CLOCK_TIME_NONE;
        gst_query_parse_latency(q, &vivo, &min, &max);
        const auto propia = static_cast<GstClockTime>(d->medido->latenciaS.load() * GST_SECOND);
        if (vivo && propia > 0) {
            min += propia;
            if (GST_CLOCK_TIME_IS_VALID(max)) max += propia;
            gst_query_set_latency(q, vivo, min, max);
        }
    }
    return TRUE;
}

static GstStateChangeReturn cambiarEstado(GstElement* e, GstStateChange t) {
    if (t == GST_STATE_CHANGE_READY_TO_PAUSED) reiniciar(BA_DERIVA(e));
    return GST_ELEMENT_CLASS(ba_deriva_parent_class)->change_state(e, t);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* d = BA_DERIVA(o);
    if (id == PROP_ACTIVO)
        d->activo->store(g_value_get_boolean(v));
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* d = BA_DERIVA(o);
    switch (id) {
        case PROP_ACTIVO: g_value_set_boolean(v, d->activo->load()); break;
        case PROP_PPM: g_value_set_double(v, d->medido->ppm.load()); break;
        case PROP_REANCLAJES: g_value_set_uint64(v, d->medido->reanclajes.load()); break;
        case PROP_DESFASE: g_value_set_double(v, d->medido->desfaseMs.load()); break;
        case PROP_ENGANCHADO: g_value_set_boolean(v, d->medido->enganchado.load()); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* d = BA_DERIVA(o);
    delete d->deriva;
    delete d->entra;
    delete d->sale;
    delete d->activo;
    delete d->medido;
    G_OBJECT_CLASS(ba_deriva_parent_class)->finalize(o);
}

static void ba_deriva_class_init(BaDerivaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    ge->change_state = cambiarEstado;
    const auto lectura = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(
        go, PROP_ACTIVO,
        g_param_spec_boolean("activo", "Activo", "Corregir la deriva (si no, pasa tal cual)", TRUE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(go, PROP_PPM,
                                    g_param_spec_double("ppm", "ppm", "Cuánto se aparta la placa del reloj del estudio (+: rápida)",
                                                        -1e6, 1e6, 0, lectura));
    g_object_class_install_property(go, PROP_REANCLAJES,
                                    g_param_spec_uint64("reanclajes", "Reanclajes", "Veces que hubo que volver a empezar (cortes)",
                                                        0, G_MAXUINT64, 0, lectura));
    g_object_class_install_property(go, PROP_DESFASE,
                                    g_param_spec_double("desfase-ms", "Desfase", "Cuánto se apartó la fase desde que enganchó (ms)",
                                                        -1e6, 1e6, 0, lectura));
    g_object_class_install_property(go, PROP_ENGANCHADO,
                                    g_param_spec_boolean("enganchado", "Enganchado", "El lazo ya siguió a la placa", FALSE, lectura));
    gst_element_class_set_static_metadata(ge, "Deriva de la placa", "Filter/Effect/Audio",
                                          "Sigue el reloj de una placa y remuestrea al del estudio (DLL + libsamplerate)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &g_sink);
    gst_element_class_add_static_pad_template(ge, &g_src);
}

static void ba_deriva_init(BaDeriva* d) {
    d->deriva = new ba::dsp::Deriva();
    d->entra = new std::vector<float>();
    d->sale = new std::vector<float>();
    d->activo = new std::atomic<bool>(true);
    d->medido = new Medido();
    d->listo = false;
    gst_audio_info_init(&d->info);
    d->sink = gst_pad_new_from_static_template(&g_sink, "sink");
    gst_pad_set_chain_function(d->sink, cadena);
    gst_pad_set_event_function(d->sink, evento);
    GST_PAD_SET_PROXY_CAPS(d->sink);
    GST_PAD_SET_PROXY_ALLOCATION(d->sink);
    gst_element_add_pad(GST_ELEMENT(d), d->sink);
    d->src = gst_pad_new_from_static_template(&g_src, "src");
    gst_pad_set_query_function(d->src, consulta);
    GST_PAD_SET_PROXY_CAPS(d->src);
    gst_element_add_pad(GST_ELEMENT(d), d->src);
}

extern "C" gboolean ba_registrar_deriva(GstPlugin* p) {
    return gst_element_register(p, "baderiva", GST_RANK_NONE, BA_TYPE_DERIVA);
}
