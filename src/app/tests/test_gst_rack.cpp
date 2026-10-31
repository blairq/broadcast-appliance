// test_gst_rack.cpp — el rack como elemento de GStreamer (audio/GstRack)
//
// Con el GStreamer vendoreado: `barack` se registra, sin anfitrión el audio
// pasa tal cual, con uno publicado le llega todo el audio en bloques planares
// de hasta 1024 muestras y lo que devuelve sale (acá: la mitad, -6 dB), y al
// retirarlo vuelve a pasar tal cual sin parar la cadena.
//
// LATENCIA (F2-07): con un anfitrión que declara 256 muestras (como el rack
// con un limitador con lookahead), `barack` la suma a la consulta LATENCY,
// avisa con un mensaje de latencia y adelanta el sello de lo que sale 256
// muestras (5,333 ms).

#include "audio/GstRack.hpp"
#include "video/CatalogoGst.hpp"

#include <gst/gst.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace ba::app::audio;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                 \
            ++fallos;                                                                        \
        }                                                                                    \
    } while (0)

struct Mitad : gstrack::Anfitrion {
    std::atomic<std::uint64_t> muestras{0};
    std::atomic<std::uint32_t> mayorBloque{0};
    void procesar(float* const* p, int canales, std::uint32_t n) override {
        for (int c = 0; c < canales; ++c)
            for (std::uint32_t i = 0; i < n; ++i) p[c][i] *= 0.5f;
        muestras += n;
        if (n > mayorBloque) mayorBloque = n;
    }
};

/// Declara 256 muestras de latencia (no toca el audio: lo que se mira es el sello).
struct Retardo256 : gstrack::Anfitrion {
    void procesar(float* const*, int, std::uint32_t) override {}
    std::uint32_t latenciaMuestras() const override { return 256; }
};

struct Sellos {
    std::atomic<std::int64_t> entra{-1}, sale{-1}; ///< PTS del primer buffer después del 10.º
    std::atomic<int> nEntra{0}, nSale{0};
};

GstPadProbeReturn anotar(GstPad*, GstPadProbeInfo* info, gpointer datos) {
    auto* par = static_cast<std::pair<std::atomic<std::int64_t>*, std::atomic<int>*>*>(datos);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (++*par->second == 10) par->first->store(static_cast<std::int64_t>(GST_BUFFER_PTS(b)));
    return GST_PAD_PROBE_OK;
}

/// En vivo: la latencia que contesta la tubería (min, ns), si se vio el mensaje
/// de latencia, y cuánto adelantó `barack` el sello del 10.º buffer.
struct Latencia {
    GstClockTime min = 0;
    bool mensaje = false;
    std::int64_t adelanto = 0;
};
Latencia medirLatencia(const char* fuente) {
    const std::string desc = std::string("audiotestsrc is-live=true samplesperbuffer=480 ! "
                                         "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! "
                                         "barack name=r fuente=\"") + fuente + "\" ! fakesink name=s sync=true";
    Latencia out;
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &err);
    if (err != nullptr) {
        g_error_free(err);
        return out;
    }
    Sellos se;
    std::pair<std::atomic<std::int64_t>*, std::atomic<int>*> pe{&se.entra, &se.nEntra}, ps{&se.sale, &se.nSale};
    GstElement* r = gst_bin_get_by_name(GST_BIN(p), "r");
    GstPad* sink = gst_element_get_static_pad(r, "sink");
    GstPad* src = gst_element_get_static_pad(r, "src");
    gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_BUFFER, anotar, &pe, nullptr);
    gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_BUFFER, anotar, &ps, nullptr);
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    const gint64 hasta = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < hasta && se.nSale < 30) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 50 * GST_MSECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_LATENCY | GST_MESSAGE_ERROR));
        if (m == nullptr) continue;
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_LATENCY && GST_MESSAGE_SRC(m) == GST_OBJECT(r)) {
            out.mensaje = true;
            gst_bin_recalculate_latency(GST_BIN(p));
        }
        gst_message_unref(m);
    }
    GstQuery* q = gst_query_new_latency();
    if (gst_element_query(p, q)) {
        gboolean vivo = FALSE;
        GstClockTime max = 0;
        gst_query_parse_latency(q, &vivo, &out.min, &max);
    }
    gst_query_unref(q);
    out.adelanto = se.entra.load() - se.sale.load();
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(sink);
    gst_object_unref(src);
    gst_object_unref(r);
    gst_object_unref(p);
    return out;
}

/// El RMS (dBFS, canal izquierdo) de lo que sale de la cadena con `n` bloques de 4096.
double medir(const char* fuente) {
    const std::string desc = std::string("audiotestsrc wave=sine volume=0.5 samplesperbuffer=4096 num-buffers=20 ! "
                                         "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! "
                                         "barack fuente=\"") + fuente + "\" ! level name=nivel interval=100000000 post-messages=true ! fakesink";
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &err);
    if (err != nullptr) {
        std::printf("pipeline: %s\n", err->message);
        g_error_free(err);
        return 0;
    }
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    double ultimo = 0;
    for (;;) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_ELEMENT | GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (m == nullptr) break;
        const bool fin = GST_MESSAGE_TYPE(m) != GST_MESSAGE_ELEMENT;
        if (!fin) {
            const GstStructure* s = gst_message_get_structure(m);
            if (const GValue* rms = gst_structure_get_value(s, "rms")) {
                GValueArray* a = static_cast<GValueArray*>(g_value_get_boxed(rms));
                ultimo = g_value_get_double(g_value_array_get_nth(a, 0));
            }
        }
        gst_message_unref(m);
        if (fin) break;
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ultimo;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    std::string e;
    if (!ba::app::video::iniciarGStreamer(&e)) {
        std::printf("SKIP: sin GStreamer (%s)\n", e.c_str());
        return 77;
    }
    CHECK(gstrack::registrarElemento(), "se registra barack");
    CHECK(gstrack::registrarElemento(), "registrar dos veces no falla");

    const double seco = medir("Micrófono 1");
    std::printf("sin rack: %.2f dBFS\n", seco);
    CHECK(seco < -8 && seco > -10, "sin anfitrión pasa tal cual (seno de 0,5: -9 dBFS)");

    auto m = std::make_shared<Mitad>();
    gstrack::publicar("Micrófono 1", m);
    const double conRack = medir("Micrófono 1");
    std::printf("con rack: %.2f dBFS, %llu muestras, bloque mayor %u\n", conRack,
                static_cast<unsigned long long>(m->muestras.load()), m->mayorBloque.load());
    CHECK(std::fabs((seco - conRack) - 6.02) < 0.1, "el anfitrión procesa (-6 dB)");
    CHECK(m->muestras == 20u * 4096u, "le llega todo el audio");
    CHECK(m->mayorBloque == 1024, "en bloques de hasta 1024");
    CHECK(gstrack::procesando("Micrófono 1"), "queda anotado que procesa");
    CHECK(!gstrack::procesando("Otro"), "otra fuente no");

    const double otra = medir("Otra fuente");
    CHECK(std::fabs(otra - seco) < 0.1, "el rack de una fuente no toca a otra");

    gstrack::retirar("Micrófono 1");
    const double sinRack = medir("Micrófono 1");
    CHECK(std::fabs(sinRack - seco) < 0.1, "al retirarlo vuelve a pasar tal cual");

    // Latencia: sin anfitrión, nada; con uno de 256 muestras, 5,333 ms más.
    const Latencia sin = medirLatencia("Con lookahead");
    gstrack::publicar("Con lookahead", std::make_shared<Retardo256>());
    const Latencia con = medirLatencia("Con lookahead");
    gstrack::retirar("Con lookahead");
    const GstClockTime k256 = gst_util_uint64_scale_int(256, GST_SECOND, 48000);
    std::printf("latencia: sin rack %.3f ms, con rack %.3f ms; sello adelantado %.3f ms; mensaje %s\n",
                sin.min / 1e6, con.min / 1e6, con.adelanto / 1e6, con.mensaje ? "si" : "no");
    CHECK(sin.adelanto == 0, "sin latencia, el sello no se toca");
    CHECK(con.min == sin.min + k256, "LATENCY suma las 256 muestras del rack");
    CHECK(con.adelanto == static_cast<std::int64_t>(k256), "el sello sale adelantado esas 256 muestras");
    CHECK(con.mensaje, "avisa con un mensaje de latencia al cambiar");

    if (fallos == 0) std::printf("test_gst_rack: OK\n");
    return fallos == 0 ? 0 : 1;
}
