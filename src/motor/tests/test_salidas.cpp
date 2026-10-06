// test_salidas.cpp — la grabación del motor (codificador de la placa, MKV en tramos)
//
// Un motor chico (640x360) con una cámara de prueba y su audio al aire, y una
// grabación del PROGRAMA (video + pista 1) y de la pista 2:
//   - un solo archivo de ~3 s: tiene video H.264 y dos pistas de audio AAC, y dura lo que se grabó;
//   - en tramos de 1 s: varios archivos, cada uno se abre;
//   - parar cierra el archivo (EOS: el índice queda escrito).

#include "Motor.hpp"
#include "Salidas.hpp"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        }                                                                            \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Info {
    bool ok = false;
    double seg = 0;
    int videos = 0, audios = 0;
    std::string codecV;
};

Info mirar(const std::string& ruta) {
    Info r;
    gchar* uri = gst_filename_to_uri(ruta.c_str(), nullptr);
    GstDiscoverer* d = gst_discoverer_new(10 * GST_SECOND, nullptr);
    GstDiscovererInfo* i = gst_discoverer_discover_uri(d, uri, nullptr);
    if (i != nullptr && gst_discoverer_info_get_result(i) == GST_DISCOVERER_OK) {
        r.ok = true;
        r.seg = static_cast<double>(gst_discoverer_info_get_duration(i)) / 1e9;
        GList* v = gst_discoverer_info_get_video_streams(i);
        GList* a = gst_discoverer_info_get_audio_streams(i);
        r.videos = static_cast<int>(g_list_length(v));
        r.audios = static_cast<int>(g_list_length(a));
        if (v != nullptr) {
            GstCaps* c = gst_discoverer_stream_info_get_caps(GST_DISCOVERER_STREAM_INFO(v->data));
            if (c != nullptr) {
                r.codecV = gst_structure_get_name(gst_caps_get_structure(c, 0));
                gst_caps_unref(c);
            }
        }
        gst_discoverer_stream_info_list_free(v);
        gst_discoverer_stream_info_list_free(a);
    }
    if (i != nullptr) gst_discoverer_info_unref(i);
    g_object_unref(d);
    g_free(uri);
    return r;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, 1);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, 1);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "compositor", "splitmuxsink", "matroskamux", "avenc_aac", "h264parse"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    if (elegirCodificador().empty()) {
        std::printf("SKIP: sin codificador H.264\n");
        return 77;
    }
    Motor::Config cfg;
    cfg.lienzo.ancho = 640;
    cfg.lienzo.alto = 360;
    cfg.prefijoAnillos = ""; // sin gateway
    cfg.sinkEscucha = "fakesink";
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    const json cam = {{"pipeline", "videotestsrc is-live=true do-timestamp=true pattern=ball ! video/x-raw,width=640,height=360,framerate=30/1"},
                      {"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"}};
    CHECK(motor.agregarEntrada("cam", cam, json{{"pistas", {1, 2}}}).empty(), "la cámara");
    CHECK(motor.definirEscena({{"nombre", "S"}, {"capas", {{{"entrada", "cam"}}}}}).empty(), "la escena");
    CHECK(motor.alAire("S").empty(), "al aire");
    esperar(1500);

    const std::string dir = (std::filesystem::temp_directory_path() / ("ba_test_salidas_" + std::to_string(getpid()))).string();
    Fuentes f;
    f.video = motor.productorDeVideo("programa", &f.latVideoNs);
    std::int64_t l1 = 0, l2 = 0;
    f.audios.push_back(motor.productorDeBus("programa", &l1));
    f.audios.push_back(motor.productorDeBus("pista2", &l2));
    f.latAudioNs = std::max(l1, l2);
    std::printf("fuentes: %s %.0f ms, %s / %s %.0f ms; codificador %s\n", f.video.c_str(), f.latVideoNs / 1e6,
                f.audios[0].c_str(), f.audios[1].c_str(), f.latAudioNs / 1e6, elegirCodificador().c_str());

    {
        Grabacion::Config gc;
        gc.carpeta = dir;
        gc.nombre = "uno";
        gc.tramoSeg = 0;
        gc.kbps = 2000;
        Grabacion g(motor.reloj(), gc);
        CHECK(g.iniciar(f).empty(), "la grabación arranca");
        for (int i = 0; i < 15; ++i) {
            esperar(200);
            g.tic();
        }
        CHECK(g.parar(), "parar cierra el archivo (EOS)");
        const auto ar = g.archivos();
        CHECK(ar.size() == 1, "un solo archivo");
        if (!ar.empty()) {
            const Info i = mirar(ar[0]);
            std::printf("archivo: %s: %.2f s, %d video (%s), %d audio\n", ar[0].c_str(), i.seg, i.videos, i.codecV.c_str(), i.audios);
            CHECK(i.ok, "se abre");
            CHECK(i.videos == 1 && i.codecV == "video/x-h264", "video H.264");
            CHECK(i.audios == 2, "dos pistas de audio (programa y pista 2)");
            CHECK(i.seg > 2.0 && i.seg < 4.0, "dura lo grabado (~3 s)");
        }
    }
    {
        Grabacion::Config gc;
        gc.carpeta = dir;
        gc.nombre = "tramos";
        gc.tramoSeg = 1;
        gc.kbps = 2000;
        gc.gopSeg = 1;
        Grabacion g(motor.reloj(), gc);
        CHECK(g.iniciar(f).empty(), "la grabación en tramos arranca");
        for (int i = 0; i < 18; ++i) {
            esperar(200);
            g.tic();
        }
        g.parar();
        const auto ar = g.archivos();
        std::printf("tramos: %zu archivos\n", ar.size());
        CHECK(ar.size() >= 3, "varios tramos de 1 s");
        for (const auto& a : ar) CHECK(mirar(a).ok, "cada tramo se abre");
    }
    // Transmisión por SRT a esta misma máquina: el motor escucha, un receptor
    // demultiplexa y cuenta lo que llega (video H.264 y audio AAC).
    if (hayFabrica("srtsink") && hayFabrica("srtsrc") && hayFabrica("tsdemux")) {
        const int puerto = 20000 + getpid() % 20000;
        Transmision::Config tc;
        tc.url = "srt://:" + std::to_string(puerto) + "?mode=listener";
        tc.kbps = 1500;
        Transmision t(motor.reloj(), tc);
        CHECK(t.iniciar(f).empty(), "la transmisión SRT arranca");
        const std::string rx = "srtsrc uri=srt://127.0.0.1:" + std::to_string(puerto) +
                               "?mode=caller latency=120 ! tsdemux name=d d. ! queue ! h264parse ! fakesink name=v sync=false "
                               "d. ! queue ! aacparse ! fakesink name=a sync=false";
        GstElement* r = gst_parse_launch(rx.c_str(), nullptr);
        std::atomic<int> nv{0}, na{0};
        for (auto [nom, cuenta] : {std::pair<const char*, std::atomic<int>*>{"v", &nv}, {"a", &na}}) {
            GstElement* s = gst_bin_get_by_name(GST_BIN(r), nom);
            GstPad* pad = gst_element_get_static_pad(s, "sink");
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                              [](GstPad*, GstPadProbeInfo*, gpointer d) {
                                  ++*static_cast<std::atomic<int>*>(d);
                                  return GST_PAD_PROBE_OK;
                              },
                              cuenta, nullptr);
            gst_object_unref(pad);
            gst_object_unref(s);
        }
        gst_element_set_state(r, GST_STATE_PLAYING);
        for (int i = 0; i < 20; ++i) {
            esperar(200);
            t.tic();
        }
        const json st = t.estado();
        std::printf("SRT: recibidos %d cuadros y %d bloques de audio; %s\n", nv.load(), na.load(), st.dump().c_str());
        CHECK(nv > 30, "llega el video");
        CHECK(na > 30, "llega el audio");
        CHECK(st.value("conectada", false), "conectada");
        CHECK(st.value("kbps", 0.0) > 100, "mide lo que sale");
        gst_element_set_state(r, GST_STATE_NULL);
        gst_object_unref(r);
        t.parar();
        CHECK(!t.andando(), "parada");
    }
    // RTMP a un puerto cerrado: queda reconectando (con espera creciente) y no muestra la clave.
    {
        Transmision::Config tc;
        tc.url = "rtmp://127.0.0.1:1/live/clave-secreta";
        Transmision t(motor.reloj(), tc);
        CHECK(t.iniciar(f).empty(), "RTMP arranca (y reintenta)");
        for (int i = 0; i < 25; ++i) {
            esperar(200);
            t.tic();
        }
        const json st = t.estado();
        std::printf("RTMP sin servidor: %s\n", st.dump().c_str());
        CHECK(st.value("reconectando", false), "queda reconectando");
        CHECK(st.value("intentos", 0) >= 2, "reintenta");
        CHECK(st.value("url", "").find("clave-secreta") == std::string::npos, "no muestra la clave");
        t.parar();
    }
    {
        Transmision t(motor.reloj(), Transmision::Config{"http://x", 1000, 128, 2, 30, {}});
        CHECK(!t.iniciar(f).empty(), "una dirección rara avisa");
    }

    motor.detener();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::printf("== test_salidas: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
