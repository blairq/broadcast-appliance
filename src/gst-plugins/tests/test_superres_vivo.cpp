// test_superres_vivo.cpp — cambiar la superresolución ANDANDO, desde otro hilo
//
// Las propiedades de baonnxsr y baglsl se pueden cambiar con el elemento en
// PLAYING (la tarjeta del grafo lo hace al tocar los filtros). Antes, el
// setter soltaba la red / rearmaba las pasadas en el hilo que llamaba,
// mientras el de streaming (o el de GL) las usaba: dos caídas en la PC de
// pruebas el 2026-10-10 (corrupción del heap y violación de acceso en
// gstbasuperres). Acá: un hilo cambia todo, sin parar, mientras pasan cuadros.
// Pasa si no se cae y los cuadros siguen saliendo.

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

static int g_fallas = 0;
static void check(bool ok, const std::string& que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que.c_str());
    if (!ok) ++g_fallas;
}

/// Corre `desc` (con un elemento «sr» y un fakesink «fin») mientras `cambiar`
/// toca el elemento desde otro hilo. Devuelve los cuadros que salieron.
template <class Cambiar>
static long andarCambiando(const std::string& desc, int segundos, Cambiar cambiar) {
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &e);
    if (p == nullptr || e != nullptr) {
        std::printf("no arma: %s\n", e != nullptr ? e->message : "?");
        if (e != nullptr) g_error_free(e);
        return -1;
    }
    GstElement* sr = gst_bin_get_by_name(GST_BIN(p), "sr");
    GstElement* fin = gst_bin_get_by_name(GST_BIN(p), "fin");
    std::atomic<long> cuadros{0};
    GstPad* pad = gst_element_get_static_pad(fin, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                      [](GstPad*, GstPadProbeInfo*, gpointer d) {
                          ++*static_cast<std::atomic<long>*>(d);
                          return GST_PAD_PROBE_OK;
                      },
                      &cuadros, nullptr);
    gst_object_unref(pad);
    gst_element_set_state(p, GST_STATE_PLAYING);
    std::atomic<bool> seguir{true};
    std::thread t([&] {
        for (int i = 0; seguir; ++i) {
            cambiar(sr, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(3 + i % 17));
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(segundos));
    seguir = false;
    t.join();
    const long antes = cuadros.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(800)); // ¿sigue saliendo?
    const long despues = cuadros.load();
    GstBus* bus = gst_element_get_bus(p);
    GstMessage* m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (m != nullptr) {
        GError* er = nullptr;
        gst_message_parse_error(m, &er, nullptr);
        std::printf("error del pipeline: %s\n", er != nullptr ? er->message : "?");
        if (er != nullptr) g_error_free(er);
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(sr);
    gst_object_unref(fin);
    gst_object_unref(p);
    return despues > antes ? despues : 0;
}

int main(int argc, char** argv) {
#ifdef BA_TEST_GST_PLUGINS
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, FALSE);
#endif
    gst_init(&argc, &argv);
    const int seg = std::getenv("BA_TEST_SEGUNDOS") != nullptr ? std::atoi(std::getenv("BA_TEST_SEGUNDOS")) : 6;

    // La red ONNX: el modelo de la carpeta de datos del desarrollo (si no está, se saltea).
    std::string modelo;
    for (const char* c : {"dist/BroadcastAppliance Datos/Modelos/superres/span-x2.onnx", "Modelos/superres/span-x2.onnx"}) {
#ifdef BA_TEST_REPO
        const std::filesystem::path r = std::filesystem::path(BA_TEST_REPO) / c;
#else
        const std::filesystem::path r = c;
#endif
        std::error_code ec;
        if (std::filesystem::exists(r, ec)) {
            modelo = r.string();
            break;
        }
    }
    if (gst_element_factory_find("baonnxsr") == nullptr || modelo.empty()) {
        std::printf("saltea baonnxsr (%s)\n", modelo.empty() ? "sin span-x2.onnx" : "sin el elemento");
    } else {
        const std::string desc = "videotestsrc is-live=true ! video/x-raw,format=NV12,width=160,height=90,framerate=30/1 ! "
                                 "baonnxsr name=sr proveedor=cpu modelo=\"" +
                                 modelo + "\" ! fakesink name=fin sync=false";
        const long n = andarCambiando(desc, seg, [&](GstElement* sr, int i) {
            switch (i % 6) {
                case 0: g_object_set(sr, "modelo", (i / 6) % 3 == 2 ? "" : modelo.c_str(), nullptr); break;
                case 1: g_object_set(sr, "proveedor", (i / 6) % 2 ? "cpu" : "auto", nullptr); break;
                case 2: g_object_set(sr, "variantes", (i / 6) % 2 ? "" : ("160x90=" + modelo).c_str(), nullptr); break;
                case 3: gst_util_set_object_arg(G_OBJECT(sr), "modo", (i / 6) % 2 ? "barato" : "activo"); break;
                case 4: {
                    gchar* x = nullptr;
                    g_object_get(sr, "proveedor-usado", &x, "modelo", &x, nullptr);
                    g_free(x);
                    break;
                }
                default: g_object_set(sr, "activo", (i / 6) % 4 != 3, nullptr); break;
            }
        });
        check(n > 0, "baonnxsr: cambiando modelo/proveedor/variantes/modo andando, no se cae y sigue saliendo (" +
                         std::to_string(n) + " cuadros)");
    }

    // El shader de mpv (GL): cambiar el código andando. Sin GL (un contenedor sin
    // pantalla: EGL_NOT_INITIALIZED) se saltea, como los otros de la GPU.
    const auto hayGl = []() {
        GstElement* p = gst_parse_launch("gltestsrc num-buffers=2 ! gldownload ! fakesink", nullptr);
        if (p == nullptr) return false;
        gst_element_set_state(p, GST_STATE_PLAYING);
        GstBus* bus = gst_element_get_bus(p);
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
        if (m != nullptr) gst_message_unref(m);
        gst_object_unref(bus);
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        return ok;
    };
    if (gst_element_factory_find("baglsl") == nullptr || gst_element_factory_find("glupload") == nullptr || !hayGl()) {
        std::printf("saltea baglsl (sin GL)\n");
    } else {
        const char* a = "//!HOOK MAIN\n//!BIND HOOKED\n//!DESC a\nvec4 hook() { return HOOKED_tex(HOOKED_pos); }\n";
        const char* b = "//!HOOK MAIN\n//!BIND HOOKED\n//!DESC b\nvec4 hook() { return HOOKED_tex(HOOKED_pos) * 0.9; }\n";
        const std::string desc = "videotestsrc is-live=true ! video/x-raw,width=160,height=90,framerate=30/1 ! glupload ! "
                                 "baglsl name=sr ! gldownload ! fakesink name=fin sync=false";
        const long n = andarCambiando(desc, seg, [&](GstElement* sr, int i) {
            g_object_set(sr, "codigo", i % 2 ? a : b, nullptr);
        });
        check(n > 0, "baglsl: cambiando el shader andando, no se cae y sigue saliendo (" + std::to_string(n) + " cuadros)");
    }
    std::printf(g_fallas == 0 ? "== test_superres_vivo: OK ==\n" : "== test_superres_vivo: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
