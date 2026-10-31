// test_salidas_fallas.cpp — las salidas que se rearman solas (04-plan-unificado: F1-05..F1-09)
//
// Un motor chico (640x360) con una cámara de prueba al aire y, con fuentes y
// sumideros sintéticos (nada por los parlantes, sin red externa: SRT a esta
// misma máquina):
//   - el tee: la grabación, su copia y dos transmisiones con el mismo perfil
//     abren UN codificador de video (y la transmisión con respaldo sale a los dos
//     puntos; apagado el principal, el de respaldo sigue recibiendo);
//   - el codificador que da error en el aire (uno falso, registrado acá) va a la
//     lista negra de la salida y la transmisión vuelve con otro en < 5 s; la
//     grabación que lo compartía sigue en otra parte;
//   - el disco que falla a los N s (la carpeta desaparece): la grabación sigue en
//     `-parte2` en < 10 s; la carpeta que ya no se puede escribir: la parte que
//     sigue va a la de respaldo; todas las partes se abren;
//   - el archivo en curso se baja al disco cada segundo (sincronizaciones).

#include "Motor.hpp"
#include "Salidas.hpp"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string texto(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

/// ¿Se abre el archivo? (y cuánto dura)
bool seAbre(const std::string& ruta, double* seg = nullptr) {
    gchar* uri = gst_filename_to_uri(ruta.c_str(), nullptr);
    GstDiscoverer* d = gst_discoverer_new(10 * GST_SECOND, nullptr);
    GstDiscovererInfo* i = gst_discoverer_discover_uri(d, uri, nullptr);
    const bool ok = i != nullptr && gst_discoverer_info_get_result(i) == GST_DISCOVERER_OK;
    if (ok && seg != nullptr) *seg = static_cast<double>(gst_discoverer_info_get_duration(i)) / 1e9;
    if (i != nullptr) gst_discoverer_info_unref(i);
    g_object_unref(d);
    g_free(uri);
    return ok;
}

// --- Un codificador que falla a propósito -------------------------------------------------
// Un bin con x264enc adentro que, a los `g_fallarEn` cuadros, da un error de
// codificación (como una sesión de NVENC o VideoToolbox que se cae en el aire).
std::atomic<int> g_fallarEn{60};
struct FalsoEnc {
    GstBin parent;
    int cuadros;
};
struct FalsoEncClass {
    GstBinClass parent_class;
};
G_DEFINE_TYPE(FalsoEnc, falso_enc, GST_TYPE_BIN)
GstStaticPadTemplate kEntra = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));
GstStaticPadTemplate kSale = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-h264"));
void falso_enc_class_init(FalsoEncClass* k) {
    auto* ek = GST_ELEMENT_CLASS(k);
    gst_element_class_add_static_pad_template(ek, &kEntra);
    gst_element_class_add_static_pad_template(ek, &kSale);
    gst_element_class_set_static_metadata(ek, "Codificador que falla", "Codec/Encoder/Video", "Da error a los N cuadros (pruebas)", "ba");
}
void falso_enc_init(FalsoEnc* self) {
    self->cuadros = 0;
    GstElement* x = gst_element_factory_make("x264enc", nullptr);
    gst_util_set_object_arg(G_OBJECT(x), "tune", "zerolatency");
    gst_util_set_object_arg(G_OBJECT(x), "speed-preset", "ultrafast");
    gst_bin_add(GST_BIN(self), x);
    GstPad* in = gst_element_get_static_pad(x, "sink");
    GstPad* out = gst_element_get_static_pad(x, "src");
    gst_element_add_pad(GST_ELEMENT(self), gst_ghost_pad_new_from_template("sink", in, gst_static_pad_template_get(&kEntra)));
    gst_element_add_pad(GST_ELEMENT(self), gst_ghost_pad_new_from_template("src", out, gst_static_pad_template_get(&kSale)));
    gst_pad_add_probe(out, GST_PAD_PROBE_TYPE_BUFFER,
                      +[](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                          auto* f = static_cast<FalsoEnc*>(d);
                          if (++f->cuadros == g_fallarEn.load())
                              GST_ELEMENT_ERROR(GST_ELEMENT(f), LIBRARY, ENCODE, ("falla a propósito"), (nullptr));
                          return f->cuadros >= g_fallarEn.load() ? GST_PAD_PROBE_DROP : GST_PAD_PROBE_OK;
                      },
                      self, nullptr);
    gst_object_unref(in);
    gst_object_unref(out);
}

// --- Un receptor SRT (esta misma máquina) ----------------------------------------------------
// Llama al puerto de la transmisión (que escucha), demultiplexa y cuenta video y
// audio por separado (cada pista nueva del demux, a su fakesink). Se reconecta solo.
struct Receptor {
    GstElement* p = nullptr;
    std::atomic<int> video{0}, audio{0};
    std::atomic<std::uint64_t> bytes{0};
    explicit Receptor(int puerto) {
        const std::string d = "srtsrc name=src uri=srt://127.0.0.1:" + std::to_string(puerto) + "?mode=caller latency=120 ! tsdemux name=d";
        p = gst_parse_launch(d.c_str(), nullptr);
        GstElement* src = gst_bin_get_by_name(GST_BIN(p), "src");
        GstPad* sp = gst_element_get_static_pad(src, "src");
        gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                          +[](GstPad*, GstPadProbeInfo* i, gpointer d) -> GstPadProbeReturn {
                              static_cast<Receptor*>(d)->bytes += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(i));
                              return GST_PAD_PROBE_OK;
                          },
                          this, nullptr);
        gst_object_unref(sp);
        gst_object_unref(src);
        GstElement* dmx = gst_bin_get_by_name(GST_BIN(p), "d");
        g_signal_connect(dmx, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer d) {
                             auto* r = static_cast<Receptor*>(d);
                             const std::string nombre = GST_PAD_NAME(pad);
                             std::atomic<int>* cuenta = nombre.rfind("video", 0) == 0 ? &r->video : &r->audio;
                             GstElement* s = gst_element_factory_make("fakesink", nullptr);
                             g_object_set(s, "sync", FALSE, "async", FALSE, nullptr);
                             gst_bin_add(GST_BIN(r->p), s);
                             GstPad* entra = gst_element_get_static_pad(s, "sink");
                             gst_pad_link(pad, entra);
                             gst_object_unref(entra);
                             gst_element_sync_state_with_parent(s);
                             gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                                               +[](GstPad*, GstPadProbeInfo*, gpointer c) -> GstPadProbeReturn {
                                                   ++*static_cast<std::atomic<int>*>(c);
                                                   return GST_PAD_PROBE_OK;
                                               },
                                               cuenta, nullptr);
                         }),
                         this);
        gst_object_unref(dmx);
        gst_element_set_state(p, GST_STATE_PLAYING);
    }
    ~Receptor() { apagar(); }
    void apagar() {
        if (p == nullptr) return;
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        p = nullptr;
    }
};

/// Hace andar los tic de las salidas (como el hilo de salidas de la app) hasta que
/// `listo` dé true o pase `ms`. true si llegó.
bool andarHasta(const std::vector<std::function<void()>>& tics, int ms, const std::function<bool()>& listo) {
    const std::int64_t fin = ahoraMs() + ms;
    while (ahoraMs() < fin) {
        for (const auto& t : tics) t();
        if (listo()) return true;
        esperar(100);
    }
    return listo();
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "compositor", "splitmuxsink", "matroskamux", "avenc_aac", "h264parse", "x264enc", "srtsink", "srtsrc", "tsdemux"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    gst_element_register(nullptr, "bafalsoenc", GST_RANK_NONE, falso_enc_get_type());

    Motor::Config cfg;
    cfg.lienzo.ancho = 640;
    cfg.lienzo.alto = 360;
    cfg.prefijoAnillos = ""; // sin gateway
    cfg.sinkEscucha = "fakesink";
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    const json cam = {{"pipeline", "videotestsrc is-live=true do-timestamp=true pattern=ball ! video/x-raw,width=640,height=360,framerate=30/1"},
                      {"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"}};
    CHECK(motor.agregarEntrada("cam", cam, json{{"pistas", {1}}}).empty(), "la cámara");
    CHECK(motor.definirEscena({{"nombre", "S"}, {"capas", {{{"entrada", "cam"}}}}}).empty(), "la escena");
    CHECK(motor.alAire("S").empty(), "al aire");
    esperar(1500);

    Fuentes f;
    f.video = motor.productorDeVideo("programa", &f.latVideoNs);
    f.audios.push_back(motor.productorDeBus("programa", &f.latAudioNs));
    const std::string dir = (std::filesystem::temp_directory_path() / ("ba_test_salidas_fallas_" + std::to_string(getpid()))).string();
    const int base = 21000 + getpid() % 15000;

    // --- El tee: un codificador para la grabación, la copia y dos transmisiones; el respaldo de ingest
    {
        Grabacion::Config gc;
        gc.carpeta = dir + "/tee";
        gc.nombre = "tee";
        gc.tramoSeg = 0;
        gc.kbps = 1500;
        gc.kbpsAudio = 160;
        gc.codificadores = {"x264enc"};
        Grabacion g(motor.reloj(), gc);
        Grabacion::Config cc = gc;
        cc.carpeta = dir + "/tee-copia";
        Grabacion copia(motor.reloj(), cc);
        Transmision::Config tc;
        tc.url = "srt://:" + std::to_string(base) + "?mode=listener";
        tc.urlRespaldo = "srt://:" + std::to_string(base + 1) + "?mode=listener";
        tc.kbps = 1500;
        tc.codificadores = {"x264enc"};
        Transmision t(motor.reloj(), tc);
        Transmision::Config tc2 = tc;
        tc2.url = "srt://:" + std::to_string(base + 2) + "?mode=listener";
        tc2.urlRespaldo.clear();
        Transmision t2(motor.reloj(), tc2);
        CHECK(g.iniciar(f).empty(), "la grabación arranca");
        CHECK(copia.iniciar(f).empty(), "la copia arranca");
        CHECK(t.iniciar(f).empty(), "la transmisión con respaldo arranca");
        CHECK(t2.iniciar(f).empty(), "el destino extra arranca");
        const json abiertas = codificacionesAbiertas();
        int videos = 0, consumidores = 0;
        for (const auto& c : abiertas)
            if (c.value("tipo", "") == "video") {
                ++videos;
                consumidores = c.value("consumidores", 0);
            }
        std::printf("tee: %s\n", texto(abiertas).c_str());
        CHECK(videos == 1, "un solo codificador de video para las cuatro salidas");
        CHECK(consumidores == 4, "lo leen las cuatro");
        Receptor principal(base), respaldo(base + 1), extra(base + 2);
        const std::vector<std::function<void()>> tics{[&] { g.tic(); }, [&] { copia.tic(); }, [&] { t.tic(); }, [&] { t2.tic(); }};
        CHECK(andarHasta(tics, 8000, [&] { return principal.video > 30 && respaldo.video > 30 && extra.video > 30; }), "llega el video a los tres puntos");
        std::printf("tee: principal %d/%d, respaldo %d/%d, extra %d/%d (video/audio)\n", principal.video.load(), principal.audio.load(),
                    respaldo.video.load(), respaldo.audio.load(), extra.video.load(), extra.audio.load());
        CHECK(principal.audio > 30 && respaldo.audio > 30, "y el audio");
        const json st = t.estado();
        std::printf("con respaldo: %s\n", texto(st).c_str());
        CHECK(st.contains("respaldo") && st["respaldo"].value("conectada", false), "el respaldo, conectado");
        CHECK(st.value("kbps", 0.0) > 100, "mide lo que sale");
        // Se apaga el punto principal: el de respaldo sigue recibiendo.
        principal.apagar();
        const int antes = respaldo.video.load();
        andarHasta(tics, 2000, [] { return false; });
        std::printf("principal apagado: el respaldo pasó de %d a %d cuadros\n", antes, respaldo.video.load());
        CHECK(respaldo.video > antes + 30, "el respaldo sigue recibiendo");
        CHECK(t.estado().value("conectada", false), "la transmisión sigue conectada (por el respaldo)");
        CHECK(g.estado().value("sincronizaciones", 0) >= 2, "el archivo en curso se baja al disco cada segundo");
        CHECK(g.parar(), "la grabación cierra");
        CHECK(copia.parar(), "la copia cierra");
        t.parar();
        t2.parar();
        for (const auto& a : g.archivos()) CHECK(seAbre(a), "la grabación se abre");
        for (const auto& a : copia.archivos()) CHECK(seAbre(a), "la copia se abre");
        CHECK(codificacionesAbiertas().empty(), "paradas todas, no queda ningún codificador");
    }

    // --- El codificador que da error en el aire: vuelve con otro en < 5 s
    {
        olvidarFabricasCaidas();
        g_fallarEn = 90; // 3 s
        Grabacion::Config gc;
        gc.carpeta = dir + "/cambio";
        gc.nombre = "cambio";
        gc.tramoSeg = 0;
        gc.kbps = 1500;
        gc.kbpsAudio = 160;
        gc.codificadores = {"bafalsoenc", "x264enc"};
        Grabacion g(motor.reloj(), gc);
        Transmision::Config tc;
        tc.url = "srt://:" + std::to_string(base + 3) + "?mode=listener";
        tc.kbps = 1500;
        tc.codificadores = {"bafalsoenc", "x264enc"};
        Transmision t(motor.reloj(), tc);
        CHECK(g.iniciar(f).empty(), "la grabación arranca con el codificador falso");
        CHECK(t.iniciar(f).empty(), "la transmisión también");
        CHECK(t.estado().value("codificador", "") == "bafalsoenc", "empieza con el falso (el primero de la lista)");
        Receptor rx(base + 3);
        const std::vector<std::function<void()>> tics{[&] { g.tic(); }, [&] { t.tic(); }};
        // Hasta que falla.
        CHECK(andarHasta(tics, 10000, [&] { return fabricasCaidas(f.video).count("bafalsoenc") > 0; }), "el falso falla y va a la lista negra");
        const std::int64_t t0 = ahoraMs();
        const int antes = rx.video.load();
        const bool volvio = andarHasta(tics, 5000, [&] { return t.estado().value("codificador", "") == "x264enc" && rx.video > antes + 15; });
        std::printf("cambio de codificador: %s en %lld ms (cuadros %d → %d); %s\n", volvio ? "volvió" : "NO volvió",
                    static_cast<long long>(ahoraMs() - t0), antes, rx.video.load(), texto(t.estado()).c_str());
        CHECK(volvio, "la transmisión vuelve con x264enc en < 5 s");
        andarHasta(tics, 1500, [] { return false; });
        const json eg = g.estado();
        std::printf("la grabación tras el cambio: %s\n", texto(eg).c_str());
        CHECK(eg.value("grabando", false) && eg.value("codificador", "") == "x264enc", "la grabación sigue, con el codificador nuevo");
        CHECK(eg.value("parte", 0) == 2, "en otra parte (las caps nuevas no entran en el mismo archivo)");
        // Otra grabación de la misma salida ya no elige el que falló.
        CHECK(elegirCodificador({"bafalsoenc", "x264enc"}, fabricasCaidas(f.video)) == "x264enc", "la lista negra vale para lo que sigue");
        g.parar();
        t.parar();
        for (const auto& a : g.archivos()) {
            double s = 0;
            CHECK(seAbre(a, &s), "cada parte se abre");
            std::printf("  %s: %.2f s\n", a.c_str(), s);
        }
        olvidarFabricasCaidas();
    }

    // --- El disco que falla: parte nueva en < 10 s; si la carpeta no se puede escribir, la de respaldo
    {
        Grabacion::Config gc;
        gc.carpeta = dir + "/disco";
        gc.carpetaRespaldo = dir + "/disco-respaldo";
        gc.nombre = "disco";
        gc.tramoSeg = 1; // un archivo nuevo por segundo: el que no se puede abrir es la falla
        gc.gopSeg = 1;
        gc.kbps = 1500;
        gc.codificadores = {"x264enc"};
        Grabacion g(motor.reloj(), gc);
        CHECK(g.iniciar(f).empty(), "la grabación arranca");
        const std::vector<std::function<void()>> tics{[&] { g.tic(); }};
        andarHasta(tics, 2500, [] { return false; });
        // 1) La carpeta desaparece (un disco que se desmonta y vuelve): sigue en la misma.
        std::error_code ec;
        std::filesystem::remove_all(gc.carpeta, ec);
        CHECK(andarHasta(tics, 5000, [&] { return g.caida(); }), "se corta");
        std::int64_t t0 = ahoraMs();
        CHECK(andarHasta(tics, 10000, [&] { return g.grabando(); }), "vuelve sola");
        std::printf("disco 1: volvió en %lld ms: %s\n", static_cast<long long>(ahoraMs() - t0), texto(g.estado()).c_str());
        CHECK(ahoraMs() - t0 < 10000, "en menos de 10 s");
        CHECK(g.estado().value("parte", 0) == 2, "en la parte 2");
        andarHasta(tics, 2500, [] { return false; });
        bool hayParte2 = false;
        for (const auto& a : g.archivos()) hayParte2 = hayParte2 || (a.find("disco-parte2-") != std::string::npos && a.find(gc.carpeta + "/") == 0);
        CHECK(hayParte2, "la parte 2, en la misma carpeta");
        // 2) Ahora la carpeta no se puede escribir (en su lugar hay un archivo): la de respaldo.
        std::filesystem::remove_all(gc.carpeta, ec);
        { std::ofstream(gc.carpeta) << "no es una carpeta"; }
        CHECK(andarHasta(tics, 5000, [&] { return g.caida(); }), "se corta otra vez");
        t0 = ahoraMs();
        CHECK(andarHasta(tics, 10000, [&] { return g.grabando(); }), "vuelve sola otra vez");
        andarHasta(tics, 2500, [] { return false; });
        bool enRespaldo = false;
        for (const auto& a : g.archivos()) enRespaldo = enRespaldo || a.find(gc.carpetaRespaldo + "/disco-parte3-") == 0;
        std::printf("disco 2: %s\n", texto(g.estado()).c_str());
        CHECK(enRespaldo, "la parte 3, en la carpeta de respaldo");
        CHECK(g.parar(), "cierra");
        int abren = 0, hay = 0;
        for (const auto& a : g.archivos())
            if (std::filesystem::exists(a, ec)) {
                ++hay;
                abren += seAbre(a) ? 1 : 0;
            }
        std::printf("disco: %d de %d tramos se abren\n", abren, hay);
        CHECK(hay >= 2 && abren == hay, "todas las partes que quedaron se abren");
        CHECK(hay == static_cast<int>(g.archivos().size()), "el tramo que no se pudo abrir no figura como archivo");
        CHECK(!g.caida(), "parada no queda «caída»");
    }

    motor.detener();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("== test_salidas_fallas: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
