// test_baanillo.cpp — baanillo: el audio de un anillo compartido como fuente de GStreamer
//
// Hace de gateway: crea un anillo (como BA.Audio.Guest.<n>) y le escribe un
// seno de 0,5 en bloques de 10 ms al ritmo real. La cadena
// `baanillo ! level ! fakesink` tiene que dar ese nivel (-9 dBFS), con las
// horas de los bloques seguidas (sin saltos) y sin perder bloques. Después el
// gateway «se reinicia» (anillo nuevo con el mismo nombre) y el audio vuelve.

#include "ipc/SharedAudioRing.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

namespace ar = ba::ipc::audio_ring;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        }                                                                            \
    } while (0)

std::atomic<bool> g_fin{false};

void gateway(const wchar_t* nombre, std::uint32_t generacion, int bloques) {
    ba::ipc::SharedAudioRing ring;
    if (!ba::ipc::SharedAudioRing::init(nombre, 2, ar::kDefaultCapacityBlocks, 1234, generacion, ring).ok) {
        std::printf("no se pudo crear el anillo\n");
        return;
    }
    std::vector<float> b(ar::kBlockFrames * 2);
    double fase = 0;
    auto t = std::chrono::steady_clock::now();
    for (int i = 0; i < bloques && !g_fin; ++i) {
        for (std::uint32_t k = 0; k < ar::kBlockFrames; ++k) {
            const float v = 0.5f * static_cast<float>(std::sin(fase));
            fase += 2 * M_PI * 440.0 / ar::kSampleRate;
            b[2 * k] = b[2 * k + 1] = v;
        }
        ring.push(b.data(), 0);
        t += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(t);
    }
}

struct Medida {
    double rms = -200;
    int buffers = 0, saltos = 0;
    GstClockTime ultimo = GST_CLOCK_TIME_NONE;
};

GstPadProbeReturn contar(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* m = static_cast<Medida*>(d);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    const GstClockTime pts = GST_BUFFER_PTS(b);
    if (GST_CLOCK_TIME_IS_VALID(m->ultimo) && GST_CLOCK_TIME_IS_VALID(pts) &&
        (pts > m->ultimo + 10 * GST_MSECOND + 2 * GST_MSECOND || pts < m->ultimo + 8 * GST_MSECOND))
        ++m->saltos;
    m->ultimo = pts;
    ++m->buffers;
    return GST_PAD_PROBE_OK;
}

/// Corre la cadena `segundos` y mide.
Medida correr(const char* canal, double segundos) {
    Medida m;
    const std::string d = std::string("baanillo name=src canal=") + canal +
                          " ! level name=nivel interval=200000000 ! fakesink sync=false";
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &err);
    if (err != nullptr) {
        std::printf("pipeline: %s\n", err->message);
        g_error_free(err);
        ++fallos;
        return m;
    }
    GstElement* src = gst_bin_get_by_name(GST_BIN(p), "src");
    GstPad* pad = gst_element_get_static_pad(src, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, contar, &m, nullptr);
    gst_object_unref(pad);
    gst_object_unref(src);
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(segundos * 1000));
    while (std::chrono::steady_clock::now() < fin) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
                                                     static_cast<GstMessageType>(GST_MESSAGE_ELEMENT | GST_MESSAGE_ERROR));
        if (msg == nullptr) continue;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            std::printf("error en la cadena\n");
            ++fallos;
        } else if (const GValue* rms = gst_structure_get_value(gst_message_get_structure(msg), "rms")) {
            auto* a = static_cast<GValueArray*>(g_value_get_boxed(rms));
            m.rms = g_value_get_double(g_value_array_get_nth(a, 0));
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return m;
}
} // namespace

/// El hilo de la cadena no gira en vacío cuando no hay datos (underrun).
double cpuDelProceso() { return static_cast<double>(std::clock()) / CLOCKS_PER_SEC; }

int main() {
#if defined(BA_TEST_GST_PLUGINS)
    setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, 1);
#endif
    gst_init(nullptr, nullptr);
    if (GstElementFactory* f = gst_element_factory_find("baanillo")) {
        gst_object_unref(f);
    } else {
        std::printf("SKIP: no está baanillo\n");
        return 77;
    }
    const wchar_t* nombre = L"BA.Audio.Prueba.1";

    // Sin anillo: la cadena arranca y espera (no falla).
    const Medida vacio = correr("BA.Audio.Prueba.1", 0.6);
    CHECK(vacio.buffers == 0, "sin anillo no sale nada");
    // Con el anillo abierto pero sin datos (nadie habla): no gira en vacío.
    {
        ba::ipc::SharedAudioRing quieto;
        ba::ipc::SharedAudioRing::init(L"BA.Audio.Prueba.2", 2, ar::kDefaultCapacityBlocks, 1234, 1, quieto);
        const double c0 = cpuDelProceso();
        const Medida q = correr("BA.Audio.Prueba.2", 1.5);
        const double cpu = cpuDelProceso() - c0;
        std::printf("anillo sin datos: %d bloques, %.2f s de CPU en 1,5 s\n", q.buffers, cpu);
        CHECK(q.buffers == 0, "sin datos no sale nada");
        CHECK(cpu < 0.5, "sin datos no ocupa la CPU");
    }

    std::thread g([&]() { gateway(nombre, 1, 400); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const Medida m = correr("BA.Audio.Prueba.1", 2.0);
    std::printf("con anillo: %.2f dBFS, %d bloques, %d saltos\n", m.rms, m.buffers, m.saltos);
    CHECK(m.rms > -9.5 && m.rms < -8.5, "el nivel del seno (-9 dBFS)");
    CHECK(m.buffers >= 180 && m.buffers <= 230, "un bloque cada 10 ms (más los que ya esperaban en el anillo)");
    CHECK(m.saltos <= 2, "las horas seguidas");
    g_fin = true;
    g.join();

    // El gateway se reinicia (anillo nuevo, otra generación) con la cadena andando.
    g_fin = false;
    std::thread g1([&]() { gateway(nombre, 2, 100); });
    std::thread g2;
    Medida r;
    std::thread cadena([&]() { r = correr("BA.Audio.Prueba.1", 3.0); });
    g1.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    g2 = std::thread([&]() { gateway(nombre, 3, 150); });
    g2.join();
    cadena.join();
    std::printf("con reinicio: %.2f dBFS, %d bloques\n", r.rms, r.buffers);
    // El reinicio se nota tras 1 s sin datos (SharedAudioRing::stale): se pierde
    // ese tramo del anillo nuevo, el resto llega.
    CHECK(r.buffers >= 100 + 60, "después del reinicio vuelve el audio");

    if (fallos == 0) std::printf("test_baanillo: OK\n");
    return fallos == 0 ? 0 : 1;
}
