// test_ptz_gl: los filtros de GL que se ponen pasantes cuando no hacen nada
// (baptzgl, bacolorgl, barecortegl), con el video andando, pasan de no hacer
// nada a trabajar y vuelven, varias veces, sin errores. Es lo que hace un
// operador con el PTZ de una cámara: arranca quieta y la mueve. (En 2026-10 se
// caía con «gst_gl_filter_filter_texture: assertion 'gst_is_gl_memory (out_tex)'»:
// al dejar de ser pasante, el filtro no tenía de dónde sacar texturas.)
#include <gst/gst.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <string>

namespace {
std::atomic<int> gCuadros{0};
GstElement* gFiltro = nullptr;
std::function<void(int)> gCambiar;

GstPadProbeReturn alCuadro(GstPad*, GstPadProbeInfo*, gpointer) {
    gCambiar(++gCuadros);
    return GST_PAD_PROBE_OK;
}

/// 150 cuadros con `cambiar(n)` en cada uno: "" si anduvo, o el error.
std::string probar(const char* elemento, std::function<void(int)> cambiar) {
    gCuadros = 0;
    gCambiar = std::move(cambiar);
    GError* err = nullptr;
    const std::string d = std::string("videotestsrc num-buffers=150 ! video/x-raw,width=320,height=240,framerate=30/1 ! "
                                      "glupload ! glcolorconvert ! video/x-raw(memory:GLMemory),format=RGBA ! ") +
                          elemento + " name=f ! gldownload ! videoconvert ! fakesink name=fin";
    GstElement* p = gst_parse_launch(d.c_str(), &err);
    if (err != nullptr) {
        std::string e = std::string("al armar: ") + err->message;
        g_error_free(err);
        return e;
    }
    gFiltro = gst_bin_get_by_name(GST_BIN(p), "f");
    GstElement* fin = gst_bin_get_by_name(GST_BIN(p), "fin");
    GstPad* pad = gst_element_get_static_pad(fin, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, alCuadro, nullptr, nullptr);
    gst_object_unref(pad);
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, 20 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    std::string r;
    if (m == nullptr) {
        r = "no terminó (" + std::to_string(gCuadros.load()) + " cuadros)";
    } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
        GError* e = nullptr;
        gst_message_parse_error(m, &e, nullptr);
        r = "en el cuadro " + std::to_string(gCuadros.load()) + ": " + e->message;
        g_error_free(e);
    } else if (gCuadros.load() < 150) {
        r = "llegaron " + std::to_string(gCuadros.load()) + " de 150 cuadros";
    }
    if (m != nullptr) gst_message_unref(m);
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(fin);
    gst_object_unref(gFiltro);
    gst_object_unref(p);
    return r;
}
} // namespace

int main(int argc, char** argv) {
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, TRUE);
    gst_init(&argc, &argv);
    if (gst_element_factory_find("baptzgl") == nullptr || gst_element_factory_find("glupload") == nullptr) {
        std::printf("test_ptz_gl: SKIP (sin baptzgl u OpenGL)\n");
        return 0;
    }
    // Con los elementos pero sin dónde dibujar (un contenedor sin placa ni pantalla: EGL no
    // arranca), no hay nada que probar: se saltea como los otros tests de GL.
    {
        GError* e = nullptr;
        GstElement* prueba = gst_parse_launch("gltestsrc num-buffers=1 ! fakesink", &e);
        bool hayGl = prueba != nullptr && e == nullptr;
        if (hayGl) {
            gst_element_set_state(prueba, GST_STATE_PLAYING);
            GstBus* bus = gst_element_get_bus(prueba);
            GstMessage* m = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                       static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            hayGl = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
            if (m != nullptr) gst_message_unref(m);
            gst_object_unref(bus);
            gst_element_set_state(prueba, GST_STATE_NULL);
        }
        if (e != nullptr) g_error_free(e);
        if (prueba != nullptr) gst_object_unref(prueba);
        if (!hayGl) {
            std::printf("test_ptz_gl: SKIP (OpenGL no arranca acá: sin placa ni pantalla)\n");
            return 0;
        }
    }
    // Cada uno: no hace nada → trabaja → nada → trabaja → nada.
    struct Caso {
        const char* elemento;
        std::function<void(int)> cambiar;
    };
    const Caso casos[] = {
        {"baptzgl",
         [](int n) {
             if (n == 20) g_object_set(gFiltro, "ir", "0.5,0.5,2,0", nullptr);
             if (n == 50) g_object_set(gFiltro, "ir", "0.5,0.5,1,0", nullptr);
             if (n == 80) g_object_set(gFiltro, "ir", "0.3,0.6,1.5,0", nullptr);
             if (n == 110) g_object_set(gFiltro, "ir", "0.5,0.5,1,0", nullptr);
         }},
        {"bacolorgl",
         [](int n) {
             if (n == 20 || n == 80) g_object_set(gFiltro, "brillo", 0.2, nullptr);
             if (n == 50 || n == 110) g_object_set(gFiltro, "brillo", 0.0, nullptr);
         }},
        {"barecortegl",
         [](int n) {
             if (n == 20) g_object_set(gFiltro, "espejo-h", TRUE, nullptr);
             if (n == 50) g_object_set(gFiltro, "espejo-h", FALSE, nullptr);
             if (n == 80) g_object_set(gFiltro, "espejo-v", TRUE, nullptr);
             if (n == 110) g_object_set(gFiltro, "espejo-v", FALSE, nullptr);
         }},
    };
    int fallas = 0;
    for (const Caso& k : casos) {
        const std::string r = probar(k.elemento, k.cambiar);
        std::printf("%-12s %s\n", k.elemento, r.empty() ? "ok (quieto ↔ trabajando 4 veces)" : ("FALLA " + r).c_str());
        if (!r.empty()) ++fallas;
    }
    std::printf("test_ptz_gl: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
