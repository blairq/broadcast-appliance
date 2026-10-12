// test_capturas.cpp — Capturas: las ventanas que se pueden capturar y los orígenes
//
// - ventanasCapturables() no se cae y no se incluye a sí mismo;
// - el origen de una pantalla se arma (y en Windows con el nombre del monitor);
// - una ventana que no existe da origen vacío (la entrada espera);
// - con BA_PRUEBA_VENTANA=<aplicación>: se captura de verdad esa ventana y tiene
//   que llegar un cuadro (para probar a mano con una ventana abierta por la prueba).

#include "Capturas.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ba::motor;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
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
    const auto vs = ventanasCapturables();
    std::printf("%zu ventanas capturables\n", vs.size());
    for (const auto& v : vs) std::printf("  [%llu] %s — %s (%dx%d)\n", static_cast<unsigned long long>(v.handle), v.app.c_str(), v.titulo.c_str(), v.ancho, v.alto);
    bool yo = false;
    for (const auto& v : vs) yo = yo || v.titulo.find("test_capturas") != std::string::npos;
    check(!yo, "no se lista a sí mismo");

    const std::string p = origenPantalla({{"monitor", "no-existe"}, {"indice", 0}, {"cursor", false}, {"x", 0}, {"y", 0}, {"ancho", 1920},
                                          {"alto", 1080}, {"px", {0, 0, 1920, 1080}}});
    std::printf("origen de pantalla: %s\n", p.c_str());
    check(!p.empty(), "el origen de una pantalla se arma");
    check(origenVentana("aplicacion-que-no-existe", "tampoco este título", true).empty(), "una ventana que no está: origen vacío");
    std::string porque;
    std::printf("captura de ventana: %s %s\n", hayCapturaDeVentana(&porque) ? "sí" : "no", porque.c_str());

    if (const char* app = std::getenv("BA_PRUEBA_VENTANA")) {
        const std::string o = origenVentana(app, std::getenv("BA_PRUEBA_TITULO") ? std::getenv("BA_PRUEBA_TITULO") : "", false);
        check(!o.empty(), std::string("está la ventana de ") + app);
        if (!o.empty()) {
            std::printf("origen: %s\n", o.c_str());
            GError* e = nullptr;
            GstElement* pl = gst_parse_launch((o + " ! videoconvert ! video/x-raw,format=BGRA ! appsink name=k").c_str(), &e);
            if (e != nullptr) {
                check(false, std::string("la cadena: ") + e->message);
                g_error_free(e);
            } else {
                GstElement* k = gst_bin_get_by_name(GST_BIN(pl), "k");
                gst_element_set_state(pl, GST_STATE_PLAYING);
                GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(k), 8 * GST_SECOND);
                int w = 0, h = 0;
                if (s != nullptr) {
                    const GstStructure* st = gst_caps_get_structure(gst_sample_get_caps(s), 0);
                    gst_structure_get_int(st, "width", &w);
                    gst_structure_get_int(st, "height", &h);
                    gst_sample_unref(s);
                }
                check(s != nullptr, "llega un cuadro de la ventana (" + std::to_string(w) + "x" + std::to_string(h) + ")");
                if (s == nullptr) {
                    GstBus* bus = gst_element_get_bus(pl);
                    if (GstMessage* m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
                        GError* ee = nullptr;
                        gst_message_parse_error(m, &ee, nullptr);
                        std::printf("error: %s\n", ee ? ee->message : "?");
                        if (ee) g_error_free(ee);
                        gst_message_unref(m);
                    }
                    gst_object_unref(bus);
                }
                gst_element_set_state(pl, GST_STATE_NULL);
                gst_object_unref(k);
                gst_object_unref(pl);
            }
        }
    }
    std::printf(fallos ? "\ntest_capturas: %d fallas\n" : "\ntest_capturas: todo ok\n", fallos);
    return fallos ? 1 : 0;
}
