// ============================================================================
// src/gst-plugins/ipc/anillocomun.cpp — baanillocomun: baanillo SIN hilo propio
//
// Lo mismo que baanillo (anillo.cpp: el audio de un anillo compartido, la hora que le
// puso el gateway contada por muestras, sin entregar los bloques de silencio por falta
// de datos, 10 ms de latencia), pero lo lee el lector común del audio (comun.hpp): un
// hilo para todos los anillos, una vuelta cada 5 ms, en vez de un hilo por anillo
// despertado por su timbre. Lo usa el motor para los anillos de las entradas
// (Entrada.cpp): con 40 lugares eran 40 hilos (y 40 colas) solo para leer.
//
//   gst-launch-1.0 baanillocomun canal=BA.Audio.Guest.1 ! audioconvert ! autoaudiosink
// ============================================================================

#include "comun.hpp"
#include "hora.hpp"
#include "sinhilo.hpp"
#include "tiempos.hpp"

#include "ipc/SharedAudioRing.hpp"

#include <gst/gst.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace ar = ba::ipc::audio_ring;

namespace {

#define CAPS_ANILLO "audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)48000, channels=(int)2"

/// Lo que lee el hilo común (con su candado): el anillo y la salida.
class LecturaAnillo : public ba::ipc_gst::Lectura {
   public:
    GstElement* e = nullptr;
    ba::ipc_gst::SalidaSinHilo salida;
    GstCaps* caps = gst_caps_from_string(CAPS_ANILLO);
    std::string canal; ///< bajo mu (la propiedad)
    ba::ipc::SharedAudioRing ring;
    bool abierto = false;
    std::string abiertoCanal;
    std::vector<float> bloque;
    GstClockTime proxima = GST_CLOCK_TIME_NONE;
    std::chrono::steady_clock::time_point reintento{};

    ~LecturaAnillo() override {
        salida.reiniciar();
        gst_caps_unref(caps);
    }

    void cerrar() {
        ring = ba::ipc::SharedAudioRing{};
        abierto = false;
        proxima = GST_CLOCK_TIME_NONE;
    }

    void leer() override {
        if (e == nullptr || !ba::ipc_gst::corriendo(e)) return; // lo están bajando: ver comun.hpp
        // Otro canal (cambiaron la propiedad) o el gateway rehízo el anillo: reabrir.
        if (abierto && (canal != abiertoCanal || ring.stale())) cerrar();
        if (!abierto) {
            const auto ahora = std::chrono::steady_clock::now();
            if (canal.empty() || ahora < reintento) return;
            const std::wstring w(canal.begin(), canal.end());
            if (!ba::ipc::SharedAudioRing::openReader(w.c_str(), ring).ok || ring.header()->channels != ar::kDefaultChannels) {
                ring = ba::ipc::SharedAudioRing{};
                reintento = ahora + std::chrono::milliseconds(100); // el gateway todavía no lo hizo
                return;
            }
            abierto = true;
            abiertoCanal = canal;
            bloque.assign(ring.blockFloats(), 0.0f);
            // Desde ahora: lo que el gateway escribió antes de abrirlo (hasta el anillo entero,
            // viejo) llegaba tarde a los mezcladores al entrar cada invitado ("llega tarde (K
            // bloques); no entra").
            ring.descartarPendientes();
        }
        const GstClockTime dur = gst_util_uint64_scale(ar::kBlockFrames, GST_SECOND, ar::kSampleRate);
        ba::ipc::PoppedBlock meta;
        // Lo que haya (de a un bloque), COMO MUCHO lo que cabe en el anillo: si la cadena de
        // atrás se demora en cada bloque al ritmo en que el gateway escribe, vaciarlo no
        // terminaba nunca y el hilo común se quedaba con esta entrada (las demás, sin datos:
        // 2026-11-01). Lo que quede, en la próxima vuelta. El silencio por falta de datos no
        // se entrega.
        const std::uint64_t tope = ring.capacityBlocks();
        for (std::uint64_t n = 0; n < tope && ring.pop(bloque.data(), meta) && !meta.isSilence; ++n) {
            // La hora: la del gateway (al escribir el bloque, que empezó un bloque antes),
            // contada por muestras mientras siga al día (como baanillo).
            const GstClockTime escrito = ba::ipc_gst::horaDeQpc(e, meta.qpc);
            GstClockTime pts = proxima;
            if (GST_CLOCK_TIME_IS_VALID(escrito)) {
                const GstClockTime captura = escrito > dur ? escrito - dur : 0;
                if (!GST_CLOCK_TIME_IS_VALID(pts) || (pts > captura ? pts - captura : captura - pts) > 40 * GST_MSECOND)
                    pts = captura;
            }
            const gsize bytes = bloque.size() * sizeof(float);
            GstBuffer* b = gst_buffer_new_allocate(nullptr, bytes, nullptr);
            gst_buffer_fill(b, 0, bloque.data(), bytes);
            GST_BUFFER_PTS(b) = pts;
            GST_BUFFER_DURATION(b) = dur;
            if (GST_CLOCK_TIME_IS_VALID(pts)) proxima = pts + dur;
            namespace ti = ba::ipc_gst::tiempos;
            const std::uint64_t t0 = ti::activos() ? ti::ahora() : 0;
            if (t0 != 0 && meta.qpc != 0 && meta.qpc <= t0) ti::anillo.anotar(t0 - meta.qpc);
            const GstFlowReturn r = salida.empujar(e, b, caps);
            if (t0 != 0) ti::cadena.anotar(ti::ahora() - t0);
            if (r != GST_FLOW_OK) break;
        }
    }
};

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_ANILLO_COMUN (ba_anillo_comun_get_type())
G_DECLARE_FINAL_TYPE(BaAnilloComun, ba_anillo_comun, BA, ANILLO_COMUN, GstElement)
G_END_DECLS

struct _BaAnilloComun {
    GstElement parent;
    std::shared_ptr<LecturaAnillo>* l;
};

G_DEFINE_TYPE(BaAnilloComun, ba_anillo_comun, GST_TYPE_ELEMENT)

enum { PROP_0, PROP_CANAL };

static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(CAPS_ANILLO));

static gboolean consultar(GstPad* pad, GstObject* padre, GstQuery* q) {
    const GstClockTime bloque = gst_util_uint64_scale(ar::kBlockFrames, GST_SECOND, ar::kSampleRate);
    return ba::ipc_gst::consultaSinHilo(pad, padre, q, bloque, bloque * ar::kDefaultCapacityBlocks);
}

static GstStateChangeReturn cambiar(GstElement* e, GstStateChange t) {
    auto& l = *BA_ANILLO_COMUN(e)->l;
    if (t == GST_STATE_CHANGE_PLAYING_TO_PAUSED) ba::ipc_gst::quitar(ba::ipc_gst::Medio::Audio, l);
    GstStateChangeReturn r = GST_ELEMENT_CLASS(ba_anillo_comun_parent_class)->change_state(e, t);
    if (r == GST_STATE_CHANGE_FAILURE) return r;
    switch (t) {
        case GST_STATE_CHANGE_READY_TO_PAUSED: {
            std::lock_guard<std::mutex> g(l->mu);
            l->salida.reiniciar();
            r = GST_STATE_CHANGE_NO_PREROLL; // en vivo
            break;
        }
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: ba::ipc_gst::registrar(ba::ipc_gst::Medio::Audio, l); break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED: r = GST_STATE_CHANGE_NO_PREROLL; break;
        case GST_STATE_CHANGE_PAUSED_TO_READY: {
            std::lock_guard<std::mutex> g(l->mu);
            l->cerrar();
            l->salida.reiniciar();
            break;
        }
        default: break;
    }
    return r;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto& l = *BA_ANILLO_COMUN(o)->l;
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> g(l->mu);
    l->canal = x != nullptr ? x : "";
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto& l = *BA_ANILLO_COMUN(o)->l;
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> g(l->mu);
    g_value_set_string(v, l->canal.c_str());
}

static void finalizar(GObject* o) {
    auto* a = BA_ANILLO_COMUN(o);
    ba::ipc_gst::quitar(ba::ipc_gst::Medio::Audio, *a->l); // por si se soltó sin pararlo
    {
        std::lock_guard<std::mutex> g((*a->l)->mu);
        (*a->l)->e = nullptr;
        (*a->l)->salida.pad = nullptr;
    }
    delete a->l;
    G_OBJECT_CLASS(ba_anillo_comun_parent_class)->finalize(o);
}

static void ba_anillo_comun_class_init(BaAnilloComunClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(
        go, PROP_CANAL,
        g_param_spec_string("canal", "Canal", "El anillo de audio (BA.Audio.Guest.<n>, BA.Audio.Remoto.1…)", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    gst_element_class_set_static_metadata(ge, "Anillo de audio (lector común)", "Source/Audio",
                                          "El audio de un anillo de memoria compartida, leído por el hilo común de los anillos",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    ge->change_state = cambiar;
}

static void ba_anillo_comun_init(BaAnilloComun* a) {
    a->l = new std::shared_ptr<LecturaAnillo>(std::make_shared<LecturaAnillo>());
    GstPad* pad = gst_pad_new_from_static_template(&srcTpl, "src");
    gst_pad_set_query_function(pad, consultar);
    gst_pad_set_event_function(pad, ba::ipc_gst::eventoSinHilo);
    gst_pad_use_fixed_caps(pad);
    gst_element_add_pad(GST_ELEMENT(a), pad);
    (*a->l)->e = GST_ELEMENT(a);
    (*a->l)->salida.pad = pad;
    GST_OBJECT_FLAG_SET(a, GST_ELEMENT_FLAG_SOURCE);
}

extern "C" gboolean ba_registrar_anillo_comun(GstPlugin* p) {
    return gst_element_register(p, "baanillocomun", GST_RANK_NONE, BA_TYPE_ANILLO_COMUN);
}
