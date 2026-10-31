// ============================================================================
// src/gst-plugins/ipc/video.cpp — bavideoanillo: el video de un canal compartido
//
// Lee el canal de video (ba::ipc::SpoutReceiver: triple buffer en memoria
// compartida; Spout en Windows el día que se use) que escribe el gateway por
// cada invitado (`BA.Guest.<n>`), su pantalla compartida (`BA.GuestShare.<n>`)
// o el remoto con la palabra (`BA.Remoto.1`), y lo entrega como una fuente EN
// VIVO de GStreamer en BGRA.
//
//   - El tamaño: el real de cada cuadro si lo trae (el gateway ya no agranda al
//     del canal a un invitado chico: F2-13, V3; lo escala la composición, en la
//     GPU), si no el del canal. Si cambia, se renegocian las caps.
//   - Si el canal todavía no existe o el gateway se reinició, se resuscribe
//     solo; mientras tanto no sale nada.
//   - Sin cuadro nuevo se duerme hasta que el gateway toca el timbre del canal
//     (ipc/Timbre.hpp, F3-13): se despierta con el cuadro, sin sondear. Con un
//     gateway que no toca (viejo, o un macOS anterior a 14.4), hasta justo después
//     del próximo que debería publicar (su último envío más su período, de la
//     cabecera del canal: ipc/Mirada.hpp, F2-14); quieto hace más de 1 s, de a 15 ms.
//   - La hora de cada cuadro: la que le puso el gateway al publicarlo, pasada
//     al reloj del pipeline (hora.hpp); así el audio del invitado (baanillo,
//     otra cadena) queda alineado por la misma hora. Cuadros por segundo
//     variables (framerate=0/1), como llegan.
//   - Declara 1 cuadro de latencia (a 30 cps).
// ============================================================================

#include "espera.hpp"
#include "hora.hpp"
#include "ipc/SpoutTransport.hpp"

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

G_BEGIN_DECLS
#define BA_TYPE_VIDEO_ANILLO (ba_video_anillo_get_type())
G_DECLARE_FINAL_TYPE(BaVideoAnillo, ba_video_anillo, BA, VIDEO_ANILLO, GstPushSrc)
G_END_DECLS

struct _BaVideoAnillo {
    GstPushSrc parent;
    std::mutex* mu; ///< canal, y reemplazar rx (despertarlo es de otro hilo: desbloquear)
    std::string* canal;
    std::atomic<bool> parar;
    // Solo el hilo de streaming (reemplazarlo, con mu):
    ba::ipc::SpoutReceiver* rx;
    bool suscripto;
    std::string* suscriptoCanal;
    int ancho, alto; ///< el de las caps negociadas (0 = sin negociar)
};

G_DEFINE_TYPE(BaVideoAnillo, ba_video_anillo, GST_TYPE_PUSH_SRC)

enum { PROP_0, PROP_CANAL };

static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string)BGRA, width=(int)[1, MAX], height=(int)[1, MAX], framerate=(fraction)0/1"));

static GstCaps* capsDe(int w, int h) {
    return gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", "width", G_TYPE_INT, w, "height",
                               G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION, 0, 1,
                               // sin el píxel cuadrado, d3d12upload fija 1/2147483647 y no negocia
                               "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, nullptr);
}

// La negociación de basesrc de fábrica fija el template ([1, MAX]) en 1x1 antes
// del primer cuadro, y vuelve a hacerlo cada vez que abajo piden renegociar (los
// elementos D3D12 de Windows lo piden al cambiar de caps): el invitado quedaba
// en 1x1, negro. Acá: sin cuadro todavía, nada (las fija crear con el tamaño del
// canal); después, siempre el tamaño del canal.
static gboolean negociar(GstBaseSrc* bs) {
    auto* v = BA_VIDEO_ANILLO(bs);
    if (v->ancho <= 0 || v->alto <= 0) return TRUE;
    GstCaps* caps = capsDe(v->ancho, v->alto);
    const gboolean ok = gst_base_src_set_caps(bs, caps);
    gst_caps_unref(caps);
    return ok;
}

/// Quieto (un lugar vacío, una pantalla que no se comparte): de a medio cuadro.
constexpr auto kPasoQuieto = std::chrono::milliseconds(15);

static GstFlowReturn crear(GstPushSrc* ps, GstBuffer** out) {
    auto* v = BA_VIDEO_ANILLO(ps);
    for (;;) {
        if (v->parar.load()) return GST_FLOW_FLUSHING;
        std::string canal;
        {
            std::lock_guard<std::mutex> l(*v->mu);
            canal = *v->canal;
        }
        // Otro canal o el gateway lo rehízo: resuscribirse (si no, queda congelado).
        if (v->suscripto && (canal != *v->suscriptoCanal || v->rx->stale())) {
            std::lock_guard<std::mutex> l(*v->mu);
            *v->rx = ba::ipc::SpoutReceiver{};
            v->suscripto = false;
        }
        if (!v->suscripto) {
            const std::wstring w(canal.begin(), canal.end());
            bool ok = false;
            {
                std::lock_guard<std::mutex> l(*v->mu);
                ok = !canal.empty() && v->rx->subscribe(w.c_str());
                if (!ok) *v->rx = ba::ipc::SpoutReceiver{};
            }
            if (!ok) {
                if (!ba::ipc_gst::esperar(v->parar, 250)) return GST_FLOW_FLUSHING;
                continue;
            }
            v->suscripto = true;
            *v->suscriptoCanal = canal;
        }
        // Sin cuadro nuevo: hasta que el gateway toque el timbre (o su próximo cuadro, si no
        // toca). A 4 ms fijos eran 250 despertadas por segundo por invitado; a 2 ms, 500.
        const std::uint32_t visto = v->rx->timbre();
        if (!v->rx->hasData()) {
            if (!ba::ipc_gst::esperarNuevo(v->parar, *v->rx, visto, kPasoQuieto)) return GST_FLOW_FLUSHING;
            continue;
        }
        const int w = v->rx->width(), h = v->rx->height();
        if (w <= 0 || h <= 0) {
            if (!ba::ipc_gst::esperar(v->parar, 4)) return GST_FLOW_FLUSHING;
            continue;
        }
        const gsize bytes = static_cast<gsize>(w) * static_cast<gsize>(h) * 4;
        GstBuffer* b = gst_buffer_new_allocate(nullptr, bytes, nullptr);
        GstMapInfo map;
        gst_buffer_map(b, &map, GST_MAP_WRITE);
        ba::ipc::FrameMeta meta{};
        const bool hay = v->rx->receive(map.data, map.size, meta);
        gst_buffer_unmap(b, &map);
        if (!hay) {
            // Sin esto, un cuadro que no se pudo leer era una vuelta sin pausa (y un buffer
            // del tamaño del cuadro por vuelta).
            gst_buffer_unref(b);
            if (!ba::ipc_gst::esperar(v->parar, 4)) return GST_FLOW_FLUSHING;
            continue;
        }
        // Su tamaño real (más chico que el canal): solo esos bytes, con las filas juntas.
        const int cw = meta.ancho != 0 ? static_cast<int>(meta.ancho) : w;
        const int ch = meta.alto != 0 ? static_cast<int>(meta.alto) : h;
        if (cw != w || ch != h) gst_buffer_resize(b, 0, static_cast<gssize>(cw) * ch * 4);
        if (cw != v->ancho || ch != v->alto) {
            GstCaps* caps = capsDe(cw, ch);
            const bool ok = gst_base_src_set_caps(GST_BASE_SRC(v), caps);
            gst_caps_unref(caps);
            if (!ok) {
                gst_buffer_unref(b);
                return GST_FLOW_NOT_NEGOTIATED;
            }
            v->ancho = cw;
            v->alto = ch;
        }
        // La hora: la que le puso el gateway, en el reloj del pipeline.
        GST_BUFFER_PTS(b) = ba::ipc_gst::horaDeQpc(GST_ELEMENT(v), meta.qpc);
        GST_BUFFER_DURATION(b) = GST_CLOCK_TIME_NONE;
        *out = b;
        return GST_FLOW_OK;
    }
}

static gboolean desbloquear(GstBaseSrc* bs) {
    auto* v = BA_VIDEO_ANILLO(bs);
    v->parar = true;
    std::lock_guard<std::mutex> l(*v->mu);
    v->rx->despertar(); // si duerme en el timbre
    return TRUE;
}

static gboolean desbloquearFin(GstBaseSrc* bs) {
    BA_VIDEO_ANILLO(bs)->parar = false;
    return TRUE;
}

static gboolean alParar(GstBaseSrc* bs) {
    auto* v = BA_VIDEO_ANILLO(bs);
    std::lock_guard<std::mutex> l(*v->mu);
    *v->rx = ba::ipc::SpoutReceiver{};
    v->suscripto = false;
    v->ancho = v->alto = 0;
    return TRUE;
}

static gboolean consultar(GstBaseSrc* bs, GstQuery* q) {
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        gst_query_set_latency(q, TRUE, GST_SECOND / 30, GST_SECOND / 5);
        return TRUE;
    }
    return GST_BASE_SRC_CLASS(ba_video_anillo_parent_class)->query(bs, q);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* val, GParamSpec* ps) {
    auto* v = BA_VIDEO_ANILLO(o);
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(val);
    std::lock_guard<std::mutex> l(*v->mu);
    *v->canal = x != nullptr ? x : "";
    v->rx->despertar(); // que se cambie ya, aunque el canal de antes esté quieto
}

static void leerPropiedad(GObject* o, guint id, GValue* val, GParamSpec* ps) {
    auto* v = BA_VIDEO_ANILLO(o);
    if (id != PROP_CANAL) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*v->mu);
    g_value_set_string(val, v->canal->c_str());
}

static void finalizar(GObject* o) {
    auto* v = BA_VIDEO_ANILLO(o);
    delete v->rx;
    delete v->suscriptoCanal;
    delete v->canal;
    delete v->mu;
    G_OBJECT_CLASS(ba_video_anillo_parent_class)->finalize(o);
}

static void ba_video_anillo_class_init(BaVideoAnilloClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    auto* bs = GST_BASE_SRC_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(
        go, PROP_CANAL,
        g_param_spec_string("canal", "Canal", "El canal de video (BA.Guest.<n>, BA.GuestShare.<n>, BA.Remoto.1…)", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    gst_element_class_set_static_metadata(ge, "Canal de video", "Source/Video",
                                          "El video que el gateway deja en memoria compartida (invitados, pantallas, remoto)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bs->unlock = desbloquear;
    bs->unlock_stop = desbloquearFin;
    bs->stop = alParar;
    bs->query = consultar;
    bs->negotiate = negociar;
    GST_PUSH_SRC_CLASS(k)->create = crear;
}

static void ba_video_anillo_init(BaVideoAnillo* v) {
    v->mu = new std::mutex();
    v->canal = new std::string();
    v->parar.store(false);
    v->rx = new ba::ipc::SpoutReceiver();
    v->suscripto = false;
    v->suscriptoCanal = new std::string();
    v->ancho = v->alto = 0;
    gst_base_src_set_live(GST_BASE_SRC(v), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(v), GST_FORMAT_TIME);
}

extern "C" gboolean ba_registrar_video_anillo(GstPlugin* p) {
    return gst_element_register(p, "bavideoanillo", GST_RANK_NONE, BA_TYPE_VIDEO_ANILLO);
}
