// test_d3d12_de_a_uno.cpp — los convertidores de D3D12 se crean de a uno
//
// La caída del heap (0xC0000374) de las placas: GStreamer 1.26 comparte entre todos
// los d3d12convert un blob de D3D12 y le cuenta referencias sin protección; con
// muchas ramas negociando a la vez se libera dos veces. Ver
// serializarConvertidoresD3D12 (Reloj.hpp).
//
// Arranca muchas veces un pipeline con muchas ramas d3d12convert que negocian a la
// vez. Sin D3D12 (macOS, Linux): SKIP.
//   BA_VUELTAS=<n>   vueltas (de fábrica 30; para buscar la carrera, 1000)
//   BA_RAMAS=<n>     ramas por vuelta (de fábrica 32)
//   BA_SIN_CANDADO=1 control: gst_init directo, sin el candado (se tiene que caer
//                    alguna vez con muchas vueltas)

#include "Reloj.hpp"

#include "platform/GstEntorno.hpp"

#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ba::motor;

namespace {
int entero(const char* var, int fabrica) {
    const char* v = std::getenv(var);
    return v != nullptr && *v != '\0' ? std::atoi(v) : fabrica;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    const bool sinCandado = std::getenv("BA_SIN_CANDADO") != nullptr;
    if (sinCandado) {
        if (!ba::platform::prepararGst() || !gst_init_check(nullptr, nullptr, nullptr)) {
            std::printf("SKIP: sin GStreamer\n");
            return 77;
        }
    } else {
        std::string err;
        if (!iniciarGStreamer(&err)) {
            std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
            return 77;
        }
    }
    if (GstElementFactory* f = gst_element_factory_find("d3d12convert")) {
        gst_object_unref(f);
    } else {
        std::printf("SKIP: sin d3d12convert (no es Windows)\n");
        return 77;
    }
    const int vueltas = entero("BA_VUELTAS", 30), ramas = entero("BA_RAMAS", 32);
    std::string desc;
    for (int i = 0; i < ramas; ++i)
        desc += "videotestsrc num-buffers=2 ! video/x-raw,format=BGRA,width=1920,height=1080 ! d3d12upload ! d3d12convert ! "
                "video/x-raw(memory:D3D12Memory),format=RGBA,width=" +
                std::to_string(320 + 16 * i) + ",height=180 ! fakesink ";
    std::printf("%s: %d vueltas de %d ramas\n", sinCandado ? "SIN candado" : "con candado", vueltas, ramas);
    int malas = 0;
    for (int v = 1; v <= vueltas; ++v) {
        GError* e = nullptr;
        GstElement* p = gst_parse_launch(desc.c_str(), &e);
        if (e != nullptr) {
            std::printf("FALLA: %s\n", e->message);
            g_error_free(e);
            if (p != nullptr) gst_object_unref(p);
            return 1;
        }
        gst_element_set_state(p, GST_STATE_PLAYING);
        GstBus* bus = gst_element_get_bus(p);
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 20 * GST_SECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (m == nullptr || GST_MESSAGE_TYPE(m) != GST_MESSAGE_EOS) {
            ++malas;
            std::printf("vuelta %d: %s\n", v, m == nullptr ? "no terminó en 20 s" : "error");
        }
        if (m != nullptr) gst_message_unref(m);
        gst_object_unref(bus);
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
    }
    // Si el heap se rompe, el proceso no llega acá (0xC0000374).
    std::printf("%s: %d vueltas, %d malas\n", malas == 0 ? "ok" : "FALLA", vueltas, malas);
    return malas == 0 ? 0 : 1;
}
