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
//   - La hora de cada bloque: la que le puso el gateway al escribirlo, pasada
//     al reloj del pipeline (hora.hpp; así queda alineado con el video del
//     invitado, que va por otra cadena), contada por muestras mientras siga al
//     día (sin el temblor de la escritura); si se corre más de 40 ms, se vuelve
//     a tomar de la hora del gateway.
//   - Sin bloque nuevo, duerme hasta que el gateway toca el timbre del anillo
//     (ipc/Timbre.hpp, F3-13); con uno que no toca, hasta su próximo bloque (Mirada).
//   - Declara 10 ms de latencia (un bloque).
// ============================================================================

#include "espera.hpp"
#include "hora.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <gst/audio/audio.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <algorithm>
#include <vector>

namespace ar = ba::ipc::audio_ring;

G_BEGIN_DECLS
#define BA_TYPE_ANILLO (ba_anillo_get_type())
G_DECLARE_FINAL_TYPE(BaAnillo, ba_anillo, BA, ANILLO, GstPushSrc)
G_END_DECLS

struct _BaAnillo {
    GstPushSrc parent;
    std::mutex* mu;           ///< `canal`, y reemplazar `ring` (despertarlo es de otro hilo: desbloquear)
    std::string* canal;
    std::atomic<bool> parar;  ///< unlock: salir de la espera
    // Solo el hilo de streaming (reemplazar `ring`, con mu):
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

/// Sin bloque: hasta que el gateway toque el timbre (F3-13) o, si no toca, hasta justo
/// después del próximo que debería escribir (su último más 10 ms, de la cabecera del
/// anillo: ipc/Mirada.hpp, F2-14). A 4 ms fijos eran 250
/// despertadas por segundo por canal; quieto hace más de 1 s (un lugar vacío, un invitado
/// callado), de a un bloque: al volver, el primero llega hasta 10 ms tarde y los que siguen
/// ya están en el anillo.
constexpr auto kPasoQuieto = std::chrono::milliseconds(10);

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
            std::lock_guard<std::mutex> l(*a->mu);
            *a->ring = ba::ipc::SharedAudioRing{};
            a->abierto = false;
        }
        if (!a->abierto) {
            const std::wstring w(canal.begin(), canal.end());
            bool ok = false;
            {
                std::lock_guard<std::mutex> l(*a->mu);
                ok = !canal.empty() && ba::ipc::SharedAudioRing::openReader(w.c_str(), *a->ring).ok &&
                     a->ring->header()->channels == ar::kDefaultChannels;
                if (!ok) *a->ring = ba::ipc::SharedAudioRing{};
            }
            if (!ok) {
                if (!ba::ipc_gst::esperar(a->parar, 100)) return GST_FLOW_FLUSHING;
                continue;
            }
            a->abierto = true;
            *a->abiertoCanal = canal;
            a->bloque->assign(a->ring->blockFloats(), 0.0f);
            a->proxima = GST_CLOCK_TIME_NONE;
        }
        ba::ipc::PoppedBlock meta;
        // Sin bloque: hasta que el gateway toque (o el próximo que se espera, kPasoQuieto).
        const std::uint32_t visto = a->ring->timbre();
        if (!a->ring->pop(a->bloque->data(), meta) || meta.isSilence) { // underrun (pop da silencio y true): no habla o no está
            if (!ba::ipc_gst::esperarNuevo(a->parar, *a->ring, visto, kPasoQuieto)) return GST_FLOW_FLUSHING;
            continue;
        }
        // La hora: la del gateway (al escribir el bloque, que empezó un bloque
        // antes), contada por muestras mientras siga al día.
        const GstClockTime dur = gst_util_uint64_scale(ar::kBlockFrames, GST_SECOND, ar::kSampleRate);
        const GstClockTime escrito = ba::ipc_gst::horaDeQpc(GST_ELEMENT(a), meta.qpc);
        GstClockTime pts = a->proxima;
        if (GST_CLOCK_TIME_IS_VALID(escrito)) {
            const GstClockTime captura = escrito > dur ? escrito - dur : 0;
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
    auto* a = BA_ANILLO(bs);
    a->parar = true;
    std::lock_guard<std::mutex> l(*a->mu);
    a->ring->despertar(); // si duerme en el timbre
    return TRUE;
}

static gboolean desbloquearFin(GstBaseSrc* bs) {
    BA_ANILLO(bs)->parar = false;
    return TRUE;
}

static gboolean alParar(GstBaseSrc* bs) {
    auto* a = BA_ANILLO(bs);
    std::lock_guard<std::mutex> l(*a->mu);
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
    a->ring->despertar(); // que se cambie ya, aunque el anillo de antes esté callado
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
