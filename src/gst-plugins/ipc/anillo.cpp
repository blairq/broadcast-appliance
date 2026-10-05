// ============================================================================
// src/gst-plugins/ipc/anillo.cpp — baanillo: el audio de un anillo compartido
//
// Lee el anillo de audio (ba::ipc::SharedAudioRing, bloques de 10 ms a 48 kHz)
// que escribe el gateway por cada invitado (`BA.Audio.Guest.<n>`), el remoto
// con la palabra (`BA.Audio.Remoto.1`) o el que espera (`BA.Audio.Lobby.<n>`),
// y lo entrega como una fuente EN VIVO de GStreamer.
//
//   - Si el anillo todavía no existe (el gateway arranca después) o se rehace
//     (el gateway se reinició: se nota tras 1 s sin datos), se reabre solo;
//     mientras tanto no sale nada.
//   - Los bloques de silencio por falta de datos (underrun) no se entregan: el
//     invitado que no habla o no está no ocupa la cadena.
//   - La hora de cada bloque: la del reloj del pipeline al leerlo, contada por
//     muestras mientras la lectura sigue al día (sin el temblor del sondeo);
//     si se corre más de 40 ms, se vuelve a tomar del reloj.
//   - Declara 10 ms de latencia (un bloque).
// ============================================================================

#include "ipc/SharedAudioRing.hpp"

#include <gst/audio/audio.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ar = ba::ipc::audio_ring;

G_BEGIN_DECLS
#define BA_TYPE_ANILLO (ba_anillo_get_type())
G_DECLARE_FINAL_TYPE(BaAnillo, ba_anillo, BA, ANILLO, GstPushSrc)
G_END_DECLS

struct _BaAnillo {
    GstPushSrc parent;
    std::mutex* mu;           ///< `canal`
    std::string* canal;
    std::atomic<bool> parar;  ///< unlock: salir de la espera
    // Solo el hilo de streaming:
    ba::ipc::SharedAudioRing* ring;
    bool abierto;
    std::string* abiertoCanal;
    std::vector<float>* bloque;
    GstClockTime proxima;     ///< la hora del bloque siguiente si la lectura sigue al día
};

G_DEFINE_TYPE(BaAnillo, ba_anillo, GST_TYPE_PUSH_SRC)

enum { PROP_0, PROP_CANAL };

#define CAPS "audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)48000, channels=(int)2"

static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(CAPS));

/// Espera `ms` en pasos cortos; false si mientras tanto pidieron parar.
static bool esperar(BaAnillo* a, int ms) {
    for (int i = 0; i < ms; i += 2) {
        if (a->parar.load()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return !a->parar.load();
}

static GstFlowReturn crear(GstPushSrc* ps, GstBuffer** out) {
    auto* a = BA_ANILLO(ps);
    for (;;) {
        if (a->parar.load()) return GST_FLOW_FLUSHING;
        std::string canal;
        {
            std::lock_guard<std::mutex> l(*a->mu);
            canal = *a->canal;
        }
        // Otro canal (cambiaron la propiedad) o el gateway rehízo el anillo: reabrir.
        if (a->abierto && (canal != *a->abiertoCanal || a->ring->stale())) {
            *a->ring = ba::ipc::SharedAudioRing{};
            a->abierto = false;
        }
        if (!a->abierto) {
            const std::wstring w(canal.begin(), canal.end());
            if (canal.empty() || !ba::ipc::SharedAudioRing::openReader(w.c_str(), *a->ring).ok ||
                a->ring->header()->channels != ar::kDefaultChannels) {
                *a->ring = ba::ipc::SharedAudioRing{};
                if (!esperar(a, 100)) return GST_FLOW_FLUSHING;
                continue;
            }
            a->abierto = true;
            *a->abiertoCanal = canal;
            a->bloque->assign(a->ring->blockFloats(), 0.0f);
            a->proxima = GST_CLOCK_TIME_NONE;
        }
        ba::ipc::PoppedBlock meta;
        if (!a->ring->pop(a->bloque->data(), meta)) {
            if (!esperar(a, 2)) return GST_FLOW_FLUSHING;
            continue;
        }
        if (meta.isSilence) { // underrun (pop da silencio y true): no habla o no está
            if (!esperar(a, 2)) return GST_FLOW_FLUSHING;
            continue;
        }
        // La hora: la del reloj al leer, contada por muestras mientras siga al día.
        GstClockTime ahora = GST_CLOCK_TIME_NONE;
        if (GstClock* reloj = gst_element_get_clock(GST_ELEMENT(a))) {
            const GstClockTime t = gst_clock_get_time(reloj), base = gst_element_get_base_time(GST_ELEMENT(a));
            if (t >= base) ahora = t - base;
            gst_object_unref(reloj);
        }
        const GstClockTime dur = gst_util_uint64_scale(ar::kBlockFrames, GST_SECOND, ar::kSampleRate);
        GstClockTime pts = a->proxima;
        if (GST_CLOCK_TIME_IS_VALID(ahora)) {
            // El bloque se capturó hace ~un bloque.
            const GstClockTime captura = ahora > dur ? ahora - dur : 0;
            if (!GST_CLOCK_TIME_IS_VALID(pts) || (pts > captura ? pts - captura : captura - pts) > 40 * GST_MSECOND)
                pts = captura;
        }
        const gsize bytes = a->bloque->size() * sizeof(float);
        GstBuffer* b = gst_buffer_new_allocate(nullptr, bytes, nullptr);
        gst_buffer_fill(b, 0, a->bloque->data(), bytes);
        GST_BUFFER_PTS(b) = pts;
        GST_BUFFER_DURATION(b) = dur;
        if (GST_CLOCK_TIME_IS_VALID(pts)) a->proxima = pts + dur;
        *out = b;
        return GST_FLOW_OK;
    }
}

static gboolean desbloquear(GstBaseSrc* bs) {
    BA_ANILLO(bs)->parar = true;
    return TRUE;
}

static gboolean desbloquearFin(GstBaseSrc* bs) {
    BA_ANILLO(bs)->parar = false;
    return TRUE;
}

static gboolean alParar(GstBaseSrc* bs) {
    auto* a = BA_ANILLO(bs);
    *a->ring = ba::ipc::SharedAudioRing{};
    a->abierto = false;
    return TRUE;
}

static gboolean consultar(GstBaseSrc* bs, GstQuery* q) {
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        const GstClockTime bloque = gst_util_uint64_scale(ar::kBlockFrames, GST_SECOND, ar::kSampleRate);
        gst_query_set_latency(q, TRUE, bloque, bloque * ar::kDefaultCapacityBlocks);
        return TRUE;
    }
    return GST_BASE_SRC_CLASS(ba_anillo_parent_class)->query(bs, q);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* a = BA_ANILLO(o);
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> l(*a->mu);
    *a->canal = x != nullptr ? x : "";
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* a = BA_ANILLO(o);
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*a->mu);
    g_value_set_string(v, a->canal->c_str());
}

static void finalizar(GObject* o) {
    auto* a = BA_ANILLO(o);
    delete a->ring;
    delete a->bloque;
    delete a->abiertoCanal;
    delete a->canal;
    delete a->mu;
    G_OBJECT_CLASS(ba_anillo_parent_class)->finalize(o);
}

static void ba_anillo_class_init(BaAnilloClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    auto* bs = GST_BASE_SRC_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(
        go, PROP_CANAL,
        g_param_spec_string("canal", "Canal", "El anillo de audio (BA.Audio.Guest.<n>, BA.Audio.Remoto.1…)", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    gst_element_class_set_static_metadata(ge, "Anillo de audio", "Source/Audio",
                                          "El audio que el gateway deja en un anillo de memoria compartida (invitados, remoto)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bs->unlock = desbloquear;
    bs->unlock_stop = desbloquearFin;
    bs->stop = alParar;
    bs->query = consultar;
    GST_PUSH_SRC_CLASS(k)->create = crear;
}

static void ba_anillo_init(BaAnillo* a) {
    a->mu = new std::mutex();
    a->canal = new std::string();
    a->parar.store(false);
    a->ring = new ba::ipc::SharedAudioRing();
    a->abierto = false;
    a->abiertoCanal = new std::string();
    a->bloque = new std::vector<float>();
    a->proxima = GST_CLOCK_TIME_NONE;
    gst_base_src_set_live(GST_BASE_SRC(a), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(a), GST_FORMAT_TIME);
}

extern "C" gboolean ba_registrar_anillo(GstPlugin* p) {
    return gst_element_register(p, "baanillo", GST_RANK_NONE, BA_TYPE_ANILLO);
}
