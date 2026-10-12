// ============================================================================
// src/gst-plugins/audio/limitador.cpp — balimitador: limitador del máster con true peak
//
// El de la rama del PROGRAMA del motor (T12): lo que sale nunca pasa el techo
// (de fábrica −1 dBTP), contando los picos entre muestras. El DSP es
// ba::dsp::LimitadorTP (lookahead, sobremuestreo ×4); acá va como elemento:
//
//   - techo-db (−1), lookahead-ms (5), liberacion-ms (100): cambian andando.
//   - activo=false: no limita, pero sigue demorando lo mismo (la latencia del
//     estudio no se mueve al prenderlo o apagarlo).
//   - Declara su demora en la consulta LATENCY (los sinks y los mezcladores la
//     cuentan: el audio sigue con su video).
//   - reduccion-db (solo lectura): cuánto está bajando ahora (≤ 0), para la GUI.
//
//   gst-launch-1.0 audiotestsrc volume=2 ! audioconvert ! balimitador techo-db=-1 ! autoaudiosink
// ============================================================================

#include "dsp/LimitadorTP.hpp"

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/gst.h>

#include <atomic>
#include <mutex>

G_BEGIN_DECLS
#define BA_TYPE_LIMITADOR (ba_limitador_get_type())
G_DECLARE_FINAL_TYPE(BaLimitador, ba_limitador, BA, LIMITADOR, GstAudioFilter)
G_END_DECLS

struct _BaLimitador {
    GstAudioFilter parent;
    std::mutex* mu; ///< el limitador (propiedades contra el hilo de audio)
    ba::dsp::LimitadorTP* lim;
    double techo, lookahead, liberacion;
    bool activo;
    std::atomic<double>* reduccion;
};

G_DEFINE_TYPE(BaLimitador, ba_limitador, GST_TYPE_AUDIO_FILTER)

enum { PROP_0, PROP_TECHO, PROP_LOOKAHEAD, PROP_LIBERACION, PROP_ACTIVO, PROP_REDUCCION };

static gboolean preparar(GstAudioFilter* f, const GstAudioInfo* info) {
    auto* l = BA_LIMITADOR(f);
    std::lock_guard<std::mutex> g(*l->mu);
    l->lim->ponerTecho(l->techo);
    l->lim->ponerLiberacion(l->liberacion);
    l->lim->ponerActivo(l->activo);
    l->lim->ponerLookahead(l->lookahead);
    l->lim->preparar(GST_AUDIO_INFO_CHANNELS(info), GST_AUDIO_INFO_RATE(info));
    return TRUE;
}

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* buf) {
    auto* l = BA_LIMITADOR(bt);
    const int canales = GST_AUDIO_INFO_CHANNELS(&GST_AUDIO_FILTER(l)->info);
    if (canales <= 0) return GST_FLOW_OK;
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) return GST_FLOW_ERROR;
    {
        std::lock_guard<std::mutex> g(*l->mu);
        l->lim->procesar(reinterpret_cast<float*>(map.data), map.size / (sizeof(float) * static_cast<size_t>(canales)));
        l->reduccion->store(l->lim->reduccionDb());
    }
    gst_buffer_unmap(buf, &map);
    // Un bloque de silencio de verdad deja de serlo (lo que sale es lo demorado).
    GST_BUFFER_FLAG_UNSET(buf, GST_BUFFER_FLAG_GAP);
    return GST_FLOW_OK;
}

/// La demora del lookahead, sumada a lo que ya demora lo de arriba.
static gboolean consulta(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    auto* l = BA_LIMITADOR(bt);
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY && dir == GST_PAD_SRC) {
        if (!GST_BASE_TRANSFORM_CLASS(ba_limitador_parent_class)->query(bt, dir, q)) return FALSE;
        gboolean vivo = FALSE;
        GstClockTime min = 0, max = 0;
        gst_query_parse_latency(q, &vivo, &min, &max);
        GstClockTime propia = 0;
        {
            std::lock_guard<std::mutex> g(*l->mu);
            const int rate = GST_AUDIO_INFO_RATE(&GST_AUDIO_FILTER(l)->info);
            const double ms = l->lookahead;
            propia = rate > 0 ? gst_util_uint64_scale(l->lim->demora(), GST_SECOND, static_cast<guint64>(rate))
                              : static_cast<GstClockTime>(ms * GST_MSECOND);
        }
        min += propia;
        if (GST_CLOCK_TIME_IS_VALID(max)) max += propia;
        gst_query_set_latency(q, vivo, min, max);
        return TRUE;
    }
    return GST_BASE_TRANSFORM_CLASS(ba_limitador_parent_class)->query(bt, dir, q);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* l = BA_LIMITADOR(o);
    bool cambiaLatencia = false;
    {
        std::lock_guard<std::mutex> g(*l->mu);
        switch (id) {
            case PROP_TECHO:
                l->techo = g_value_get_double(v);
                l->lim->ponerTecho(l->techo);
                break;
            case PROP_LOOKAHEAD:
                l->lookahead = g_value_get_double(v);
                l->lim->ponerLookahead(l->lookahead);
                cambiaLatencia = true;
                break;
            case PROP_LIBERACION:
                l->liberacion = g_value_get_double(v);
                l->lim->ponerLiberacion(l->liberacion);
                break;
            case PROP_ACTIVO:
                l->activo = g_value_get_boolean(v);
                l->lim->ponerActivo(l->activo);
                break;
            default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
        }
    }
    if (cambiaLatencia)
        gst_element_post_message(GST_ELEMENT(o), gst_message_new_latency(GST_OBJECT(o)));
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* l = BA_LIMITADOR(o);
    std::lock_guard<std::mutex> g(*l->mu);
    switch (id) {
        case PROP_TECHO: g_value_set_double(v, l->techo); break;
        case PROP_LOOKAHEAD: g_value_set_double(v, l->lookahead); break;
        case PROP_LIBERACION: g_value_set_double(v, l->liberacion); break;
        case PROP_ACTIVO: g_value_set_boolean(v, l->activo); break;
        case PROP_REDUCCION: g_value_set_double(v, l->reduccion->load()); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* l = BA_LIMITADOR(o);
    delete l->lim;
    delete l->mu;
    delete l->reduccion;
    G_OBJECT_CLASS(ba_limitador_parent_class)->finalize(o);
}

static void ba_limitador_class_init(BaLimitadorClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    const auto vivo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    g_object_class_install_property(go, PROP_TECHO,
                                    g_param_spec_double("techo-db", "Techo", "Techo en dBTP (true peak)", -20.0, 0.0, -1.0, vivo));
    g_object_class_install_property(go, PROP_LOOKAHEAD,
                                    g_param_spec_double("lookahead-ms", "Anticipación", "Cuánto se anticipa (y demora el audio), en ms",
                                                        1.0, 20.0, 5.0, vivo));
    g_object_class_install_property(go, PROP_LIBERACION,
                                    g_param_spec_double("liberacion-ms", "Liberación", "Cuánto tarda en volver, en ms", 10.0, 2000.0,
                                                        100.0, vivo));
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo",
                                                         "Apagado no limita (pero demora lo mismo: la latencia no cambia)", TRUE, vivo));
    g_object_class_install_property(go, PROP_REDUCCION,
                                    g_param_spec_double("reduccion-db", "Reducción", "Cuánto baja ahora (dB, ≤ 0)", -120.0, 0.0, 0.0,
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Limitador true peak", "Filter/Effect/Audio",
                                          "Limitador del máster: nunca pasa el techo, picos entre muestras incluidos",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("audio/x-raw, format=(string)F32LE, layout=(string)interleaved, "
                                         "rate=(int)[ 8000, 192000 ], channels=(int)[ 1, 16 ]");
    gst_audio_filter_class_add_pad_templates(GST_AUDIO_FILTER_CLASS(k), caps);
    gst_caps_unref(caps);
    GST_AUDIO_FILTER_CLASS(k)->setup = preparar;
    bt->transform_ip = transformar;
    bt->transform_ip_on_passthrough = FALSE;
    bt->query = consulta;
}

static void ba_limitador_init(BaLimitador* l) {
    l->mu = new std::mutex();
    l->lim = new ba::dsp::LimitadorTP();
    l->reduccion = new std::atomic<double>(0.0);
    l->techo = -1.0;
    l->lookahead = 5.0;
    l->liberacion = 100.0;
    l->activo = true;
    l->lim->preparar(2, 48000);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(l), TRUE);
    gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(l), FALSE);
}

extern "C" gboolean ba_registrar_limitador(GstPlugin* p) {
    return gst_element_register(p, "balimitador", GST_RANK_NONE, BA_TYPE_LIMITADOR);
}
