// ============================================================================
// src/gst-plugins/ipc/anillosink.cpp — baanillosink: escribe audio en un anillo del gateway
//
// Lo contrario de `baanillo`: lo que entra (F32, 48 kHz, estéreo) se escribe en
// un SharedAudioRing con nombre (`canal`) en bloques de 480 cuadros (10 ms),
// cada uno sellado con la hora del gateway (QPC) que le corresponde a su hora
// en el pipeline. Así el motor le manda al gateway, sin libobs:
//     BA.Audio.ContentForReturn   el programa (lo que oyen los invitados)
//     BA.Audio.Return.<n>         el mix-minus del invitado n (el programa sin él)
//
// - sync=true (de fábrica): escribe a tiempo real, como espera el gateway.
// - Cada arranque es una generación nueva del escritor: el gateway lo nota y
//   reabre (no queda mudo si la app se reinicia).
// - Si el lector no llega (anillo lleno), el bloque se descarta (overrun): nunca
//   traba el pipeline.
//
//   gst-launch-1.0 audiotestsrc is-live=true ! audioconvert ! baanillosink canal=BA.Audio.Return.1
// ============================================================================

#include "hora.hpp"

#include "ipc/HoraQpc.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <gst/audio/audio.h>
#include <gst/base/gstbasesink.h>
#include <gst/gst.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ba_getpid _getpid
#else
#include <unistd.h>
#define ba_getpid getpid
#endif

G_BEGIN_DECLS
#define BA_TYPE_ANILLO_SINK (ba_anillo_sink_get_type())
G_DECLARE_FINAL_TYPE(BaAnilloSink, ba_anillo_sink, BA, ANILLO_SINK, GstBaseSink)
G_END_DECLS

struct _BaAnilloSink {
    GstBaseSink parent;
    std::mutex* mu;
    std::string* canal;
    ba::ipc::SharedAudioRing* anillo;
    bool abierto;
    std::vector<float>* bloque; ///< lo acumulado hasta completar 480 cuadros
    std::size_t llenos;         ///< cuadros en `bloque`
    GstClockTime horaBloque;    ///< la hora (ejecución) del primer cuadro del bloque
    guint64 escritos, descartados;
};

G_DEFINE_TYPE(BaAnilloSink, ba_anillo_sink, GST_TYPE_BASE_SINK)

enum { PROP_0, PROP_CANAL, PROP_ESCRITOS, PROP_DESCARTADOS };

namespace {
constexpr std::size_t kCuadros = ba::ipc::audio_ring::kBlockFrames;
constexpr std::size_t kCanales = 2;

std::wstring ancho(const std::string& s) { return std::wstring(s.begin(), s.end()); }

/// La hora del gateway (QPC) de algo que tiene `rt` en el pipeline de `e`.
std::uint64_t qpcDe(GstElement* e, GstClockTime rt) {
    GstClock* reloj = gst_element_get_clock(e);
    const std::uint64_t q = ba::ipc_gst::ahoraQpc();
    if (reloj == nullptr || !GST_CLOCK_TIME_IS_VALID(rt)) {
        if (reloj != nullptr) gst_object_unref(reloj);
        return q;
    }
    const GstClockTime ahora = gst_clock_get_time(reloj);
    gst_object_unref(reloj);
    return ba::ipc::qpcDeOtroReloj(gst_element_get_base_time(e) + rt, ahora, q);
}
} // namespace

static gboolean arrancar(GstBaseSink* bs) {
    auto* s = BA_ANILLO_SINK(bs);
    std::lock_guard<std::mutex> l(*s->mu);
    if (s->canal->empty()) {
        GST_ELEMENT_ERROR(s, RESOURCE, SETTINGS, ("falta `canal` (p. ej. BA.Audio.Return.1)"), (nullptr));
        return FALSE;
    }
    const auto gen = static_cast<std::uint32_t>(std::time(nullptr));
    const ba::ipc::Status st = ba::ipc::SharedAudioRing::init(ancho(*s->canal).c_str(), kCanales,
                                                               ba::ipc::audio_ring::kDefaultCapacityBlocks,
                                                               static_cast<std::uint32_t>(ba_getpid()), gen, *s->anillo);
    if (!st.ok) {
        GST_ELEMENT_ERROR(s, RESOURCE, OPEN_WRITE, ("no se pudo crear el anillo %s", s->canal->c_str()), (nullptr));
        return FALSE;
    }
    s->abierto = true;
    s->llenos = 0;
    s->horaBloque = GST_CLOCK_TIME_NONE;
    s->escritos = s->descartados = 0;
    return TRUE;
}

static gboolean parar(GstBaseSink* bs) {
    auto* s = BA_ANILLO_SINK(bs);
    std::lock_guard<std::mutex> l(*s->mu);
    *s->anillo = ba::ipc::SharedAudioRing();
    s->abierto = false;
    return TRUE;
}

static GstFlowReturn dibujar(GstBaseSink* bs, GstBuffer* buf) {
    auto* s = BA_ANILLO_SINK(bs);
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return GST_FLOW_ERROR;
    const auto* d = reinterpret_cast<const float*>(map.data);
    const std::size_t cuadros = map.size / (sizeof(float) * kCanales);
    GstClockTime rt = GST_CLOCK_TIME_NONE;
    if (GST_BUFFER_PTS_IS_VALID(buf))
        rt = gst_segment_to_running_time(&bs->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buf));
    std::lock_guard<std::mutex> l(*s->mu);
    for (std::size_t i = 0; s->abierto && i < cuadros;) {
        if (s->llenos == 0)
            s->horaBloque = GST_CLOCK_TIME_IS_VALID(rt) ? rt + gst_util_uint64_scale(i, GST_SECOND, 48000) : GST_CLOCK_TIME_NONE;
        const std::size_t n = std::min(kCuadros - s->llenos, cuadros - i);
        if (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_GAP))
            std::memset(s->bloque->data() + s->llenos * kCanales, 0, n * kCanales * sizeof(float));
        else
            std::memcpy(s->bloque->data() + s->llenos * kCanales, d + i * kCanales, n * kCanales * sizeof(float));
        s->llenos += n;
        i += n;
        if (s->llenos == kCuadros) {
            if (s->anillo->push(s->bloque->data(), qpcDe(GST_ELEMENT(s), s->horaBloque)))
                ++s->escritos;
            else
                ++s->descartados;
            s->llenos = 0;
        }
    }
    gst_buffer_unmap(buf, &map);
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_ANILLO_SINK(o);
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*s->mu);
    const gchar* x = g_value_get_string(v);
    *s->canal = x != nullptr ? x : "";
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_ANILLO_SINK(o);
    std::lock_guard<std::mutex> l(*s->mu);
    switch (id) {
        case PROP_CANAL: g_value_set_string(v, s->canal->c_str()); break;
        case PROP_ESCRITOS: g_value_set_uint64(v, s->escritos); break;
        case PROP_DESCARTADOS: g_value_set_uint64(v, s->descartados); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* s = BA_ANILLO_SINK(o);
    delete s->anillo;
    delete s->bloque;
    delete s->canal;
    delete s->mu;
    G_OBJECT_CLASS(ba_anillo_sink_parent_class)->finalize(o);
}

static void ba_anillo_sink_class_init(BaAnilloSinkClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* bs = GST_BASE_SINK_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(go, PROP_CANAL,
                                    g_param_spec_string("canal", "Canal", "El anillo donde se escribe (BA.Audio.Return.1…)", "",
                                                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                                                 GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(go, PROP_ESCRITOS,
                                    g_param_spec_uint64("escritos", "Escritos", "Bloques de 10 ms escritos", 0, G_MAXUINT64, 0,
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(go, PROP_DESCARTADOS,
                                    g_param_spec_uint64("descartados", "Descartados", "Bloques descartados (el lector no llegó)", 0,
                                                        G_MAXUINT64, 0,
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Anillo de audio (escritor)", "Sink/Audio",
                                          "Escribe audio en un anillo de memoria compartida para el gateway (retornos, programa)",
                                          "Broadcast Appliance");
    GstCaps* caps =
        gst_caps_from_string("audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)48000, channels=(int)2");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    bs->start = arrancar;
    bs->stop = parar;
    bs->render = dibujar;
}

static void ba_anillo_sink_init(BaAnilloSink* s) {
    s->mu = new std::mutex();
    s->canal = new std::string();
    s->anillo = new ba::ipc::SharedAudioRing();
    s->bloque = new std::vector<float>(kCuadros * kCanales, 0.0f);
    s->abierto = false;
    s->llenos = 0;
    s->horaBloque = GST_CLOCK_TIME_NONE;
    s->escritos = s->descartados = 0;
    gst_base_sink_set_sync(GST_BASE_SINK(s), TRUE);
}

extern "C" gboolean ba_registrar_anillo_sink(GstPlugin* p) {
    return gst_element_register(p, "baanillosink", GST_RANK_NONE, BA_TYPE_ANILLO_SINK);
}
