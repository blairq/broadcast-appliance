// test_anillocomun.cpp — el lector común de los anillos y baentrega/batoma
//
// Hace de gateway: N anillos (como BA.Audio.Guest.<n>) con un seno de 0,5 en bloques de
// 10 ms al ritmo real. Cada uno lo lee una cadena como la de una entrada del motor
// (`baanillocomun ! audioconvert ! baentrega`), y un mezclador (`batoma ! audiomixer !
// level ! fakesink`, otro pipeline) los suma:
//   - el nivel de un anillo solo es el del seno (-9 dBFS) y las horas van seguidas;
//   - con 24 cadenas andando, los hilos del proceso casi no crecen (antes: dos por cadena,
//     la fuente y su cola, y uno por rama del mezclador);
//   - una cadena que pasa a READY (el lugar vacío) y vuelve a PLAYING sigue dando audio;
//   - bajar el mezclador a NULL con todo andando no traba (ni a las cadenas ni al hilo común).

#include "ipc/SharedAudioRing.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

namespace ar = ba::ipc::audio_ring;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

std::atomic<bool> g_fin{false};

/// Los hilos del proceso (-1 si este sistema no lo dice fácil).
int hilos() {
#if defined(__APPLE__)
    thread_act_array_t lista;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &lista, &n) != KERN_SUCCESS) return -1;
    for (mach_msg_type_number_t i = 0; i < n; ++i) mach_port_deallocate(mach_task_self(), lista[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(lista), n * sizeof(thread_act_t));
    return static_cast<int>(n);
#elif defined(__linux__)
    int n = 0;
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') ++n;
        closedir(d);
    }
    return n;
#else
    return -1;
#endif
}

std::wstring ancho(const std::string& s) { return std::wstring(s.begin(), s.end()); }

/// Los anillos: un seno de 0,5 cada 10 ms, sellado como el gateway.
void gateway(std::vector<std::string> nombres) {
    std::vector<ba::ipc::SharedAudioRing> anillos(nombres.size());
    for (size_t i = 0; i < nombres.size(); ++i)
        if (!ba::ipc::SharedAudioRing::init(ancho(nombres[i]).c_str(), 2, ar::kDefaultCapacityBlocks, 1234, 1, anillos[i]).ok)
            std::printf("no se pudo crear %s\n", nombres[i].c_str());
    std::vector<float> b(ar::kBlockFrames * 2);
    double fase = 0;
    auto t = std::chrono::steady_clock::now();
    while (!g_fin) {
        for (std::uint32_t k = 0; k < ar::kBlockFrames; ++k) {
            const float v = 0.5f * static_cast<float>(std::sin(fase));
            fase += 2 * std::numbers::pi * 440.0 / ar::kSampleRate;
            b[2 * k] = b[2 * k + 1] = v;
        }
        const auto qpc = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        for (auto& a : anillos) a.push(b.data(), qpc);
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
    const GstClockTime pts = GST_BUFFER_PTS(GST_PAD_PROBE_INFO_BUFFER(info));
    if (GST_CLOCK_TIME_IS_VALID(m->ultimo) && GST_CLOCK_TIME_IS_VALID(pts) &&
        (pts > m->ultimo + 12 * GST_MSECOND || pts < m->ultimo + 8 * GST_MSECOND))
        ++m->saltos;
    m->ultimo = pts;
    ++m->buffers;
    return GST_PAD_PROBE_OK;
}

GstElement* lanzar(const std::string& d) {
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &err);
    if (err != nullptr) {
        std::printf("pipeline «%s»: %s\n", d.c_str(), err->message);
        g_error_free(err);
        ++fallos;
        if (p != nullptr) gst_object_unref(p);
        return nullptr;
    }
    return p;
}

/// Como el motor: el mismo reloj y el mismo base_time en todos los pipelines.
void enHora(GstElement* p, GstClock* reloj, GstClockTime base) {
    gst_pipeline_use_clock(GST_PIPELINE(p), reloj);
    gst_element_set_start_time(p, GST_CLOCK_TIME_NONE);
    gst_element_set_base_time(p, base);
}

/// El nivel que mide el mezclador durante `segundos` (dBFS, el último mensaje de level).
double nivel(GstElement* mezcla, double segundos, bool* error = nullptr) {
    double rms = -200;
    GstBus* bus = gst_element_get_bus(mezcla);
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(segundos * 1000));
    while (std::chrono::steady_clock::now() < fin) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_ELEMENT | GST_MESSAGE_ERROR));
        if (m == nullptr) continue;
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            if (error != nullptr) *error = true;
        } else if (const GValue* v = gst_structure_get_value(gst_message_get_structure(m), "rms")) {
            auto* a = static_cast<GValueArray*>(g_value_get_boxed(v));
            rms = g_value_get_double(g_value_array_get_nth(a, 0));
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    return rms;
}

} // namespace

int main() {
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, TRUE);
#endif
    gst_init(nullptr, nullptr);
    for (const char* f : {"baanillocomun", "baentrega", "batoma", "audiomixer", "level"}) {
        GstElementFactory* fab = gst_element_factory_find(f);
        if (fab == nullptr) {
            std::printf("SKIP: no está %s\n", f);
            return 77;
        }
        gst_object_unref(fab);
    }
    constexpr int kCadenas = 24;
    std::vector<std::string> nombres;
    for (int i = 1; i <= kCadenas; ++i) nombres.push_back("BA.Audio.PruebaComun." + std::to_string(i));
    std::thread g([&]() { gateway(nombres); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    GstClock* reloj = gst_system_clock_obtain();
    const GstClockTime base = gst_clock_get_time(reloj);
    const int hilosAntes = hilos();

    // Las cadenas de las entradas.
    std::vector<GstElement*> entradas;
    for (int i = 0; i < kCadenas; ++i) {
        GstElement* p = lanzar("baanillocomun name=src canal=" + nombres[i] +
                               " ! audioconvert ! audio/x-raw,format=F32LE,channels=2,rate=48000 ! baentrega producer-name=e" +
                               std::to_string(i + 1));
        if (p == nullptr) return 1;
        enHora(p, reloj, base);
        entradas.push_back(p);
    }
    // El mezclador: la primera sola (para medir el nivel del seno).
    GstElement* mezcla = lanzar("batoma producer-name=e1 ! audiomixer name=mx force-live=true latency=50000000 "
                                "start-time-selection=first ! level interval=200000000 ! fakesink sync=false async=false");
    if (mezcla == nullptr) return 1;
    enHora(mezcla, reloj, base);
    Medida m;
    {
        GstElement* src = gst_bin_get_by_name(GST_BIN(entradas[0]), "src");
        GstPad* pad = gst_element_get_static_pad(src, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, contar, &m, nullptr);
        gst_object_unref(pad);
        gst_object_unref(src);
    }
    gst_element_set_state(mezcla, GST_STATE_PLAYING);
    for (GstElement* p : entradas) gst_element_set_state(p, GST_STATE_PLAYING);
    bool error = false;
    const double uno = nivel(mezcla, 2.0, &error);
    const int hilosDespues = hilos();
    std::printf("un anillo por el mezclador: %.2f dBFS; %d bloques en la fuente, %d saltos; hilos %d → %d con %d cadenas\n", uno,
                m.buffers, m.saltos, hilosAntes, hilosDespues, kCadenas);
    CHECK(!error, "sin errores en el mezclador");
    CHECK(uno > -9.6 && uno < -8.4, "el nivel del seno (-9 dBFS)");
    CHECK(m.buffers >= 170 && m.buffers <= 240, "un bloque cada 10 ms");
    CHECK(m.saltos <= 2, "las horas seguidas");
    // Antes: dos hilos por cadena (la fuente y su cola). Ahora: el hilo común y los del
    // mezclador (su salida), más lo que GStreamer tenga de fondo.
    if (hilosAntes > 0) CHECK(hilosDespues - hilosAntes <= 8, "los hilos no crecen con las cadenas");

    // El lugar vacío: READY (sin hilos ni lectura) y de vuelta a PLAYING.
    gst_element_set_state(entradas[0], GST_STATE_READY);
    const double callado = nivel(mezcla, 0.8);
    gst_element_set_state(entradas[0], GST_STATE_PLAYING);
    const double vuelve = nivel(mezcla, 1.0);
    std::printf("en reposo: %.2f dBFS; al volver: %.2f dBFS\n", callado, vuelve);
    CHECK(callado < -60, "en reposo no sale nada");
    CHECK(vuelve > -9.6 && vuelve < -8.4, "al volver, el seno");

    // Bajar el mezclador con todo andando no traba nada.
    const auto t0 = std::chrono::steady_clock::now();
    gst_element_set_state(mezcla, GST_STATE_NULL);
    gst_object_unref(mezcla);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("bajar el mezclador andando: %lld ms\n", static_cast<long long>(ms));
    CHECK(ms < 1000, "bajar el mezclador no se traba");
    // Y uno nuevo con todas las ramas: la suma de 24 senos iguales (+27,6 dB sobre uno; satura,
    // pero tiene que llegar algo de todas).
    std::string d;
    for (int i = 1; i <= kCadenas; ++i) d += "batoma producer-name=e" + std::to_string(i) + " ! mx. ";
    GstElement* todas = lanzar(d + "audiomixer name=mx force-live=true latency=50000000 start-time-selection=first ! "
                                   "level interval=200000000 ! fakesink sync=false async=false");
    if (todas == nullptr) return 1;
    enHora(todas, reloj, base);
    gst_element_set_state(todas, GST_STATE_PLAYING);
    const double suma = nivel(todas, 1.5);
    std::printf("las %d ramas: %.2f dBFS; hilos %d\n", kCadenas, suma, hilos());
    CHECK(suma > uno + 20, "llegan todas las ramas");
    gst_element_set_state(todas, GST_STATE_NULL);
    gst_object_unref(todas);

    for (GstElement* p : entradas) {
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
    }
    g_fin = true;
    g.join();
    gst_object_unref(reloj);
    if (fallos == 0) std::printf("test_anillocomun: OK\n");
    return fallos == 0 ? 0 : 1;
}
