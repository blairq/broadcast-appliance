// test_fallas.cpp — las fallas del banco de caos (F1-14) rompen lo que dicen.
//
// Pipelines chicos con los nombres de los de verdad (Salidas.cpp): «codificador» y
// «mux» / «red». Programada la falla, el tic de la salida (aplicarFallasProgramadas)
// la aplica: el error llega por el bus con el dominio de la falla real, desde el
// elemento que corresponde, y la pieza deja de pasar datos. Sin nada programado, no
// toca nada.
#include "Fallas.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include "fault/FallasProgramadas.hpp"

#include <gst/gst.h>

#include <atomic>
#include <cstdio>
#include <string>

using namespace ba::motor;
using ba::fault::FallasProgramadas;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)

std::atomic<int> g_llegan{0};

GstElement* armar(const std::string& d) {
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::printf("no arma: %s\n", e->message);
        g_error_free(e);
        return nullptr;
    }
    // Lo que llega al final (fakesink «fin»): para ver que la pieza rota no deja pasar.
    GstElement* fin = gst_bin_get_by_name(GST_BIN(p), "fin");
    g_object_set(fin, "signal-handoffs", TRUE, nullptr);
    g_signal_connect(fin, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer*, GstPad*, gpointer) { ++g_llegan; }), nullptr);
    gst_object_unref(fin);
    gst_element_set_state(p, GST_STATE_PLAYING);
    gst_element_get_state(p, nullptr, nullptr, 5 * GST_SECOND);
    return p;
}

/// El primer error del bus en `ms`: {quién, dominio, código}.
struct Error {
    std::string de;
    GQuark dominio = 0;
    int codigo = 0;
};
Error esperarError(GstElement* p, int ms) {
    Error r;
    GstBus* bus = gst_element_get_bus(p);
    if (GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(ms) * GST_MSECOND, GST_MESSAGE_ERROR)) {
        GError* e = nullptr;
        gst_message_parse_error(m, &e, nullptr);
        r.de = GST_OBJECT_NAME(GST_MESSAGE_SRC(m));
        if (e != nullptr) {
            r.dominio = e->domain;
            r.codigo = e->code;
            g_error_free(e);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    return r;
}

void apagar(GstElement* p) {
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    auto& f = FallasProgramadas::instancia();
    f.limpiar();

    // --- El codificador ---------------------------------------------------------------
    // identity hace de codificador: lo que importa es el nombre y el error que avisa.
    GstElement* p = armar("audiotestsrc is-live=true ! identity name=codificador ! fakesink name=fin sync=false");
    CHECK(p != nullptr, "pipeline con «codificador»");
    if (p != nullptr) {
        g_usleep(200000);
        aplicarFallasProgramadas(p, "transmision");
        CHECK(esperarError(p, 100).de.empty(), "sin nada programado no toca nada");
        CHECK(f.programar("transmision", "codificador", 0).empty(), "programar el codificador de la transmisión");
        aplicarFallasProgramadas(p, "grabacion");
        CHECK(esperarError(p, 100).de.empty(), "la de la transmisión no la toma la grabación");
        aplicarFallasProgramadas(p, "transmision");
        const Error e = esperarError(p, 1000);
        CHECK(e.de == "codificador" && e.dominio == GST_STREAM_ERROR && e.codigo == GST_STREAM_ERROR_ENCODE,
              "error de codificación, desde el codificador");
        g_usleep(100000);
        const int antes = g_llegan.load();
        g_usleep(300000);
        CHECK(g_llegan.load() == antes, "y no saca más nada");
        apagar(p);
    }

    // --- El sumidero de la grabación («mux») y de la transmisión («red») -------------
    for (const char* salida : {"grabacion", "transmision"}) {
        const std::string nombre = std::string(salida) == "grabacion" ? "mux" : "red";
        g_llegan = 0;
        p = armar("audiotestsrc is-live=true ! identity name=" + nombre + " ! fakesink name=fin sync=false");
        if (p == nullptr) continue;
        CHECK(f.programar(salida, "sumidero", 300).empty(), "programar el sumidero a los 300 ms");
        aplicarFallasProgramadas(p, salida);
        CHECK(esperarError(p, 100).de.empty(), "antes de tiempo, nada");
        g_usleep(300000);
        aplicarFallasProgramadas(p, salida);
        const Error e = esperarError(p, 1000);
        CHECK(e.de == nombre && e.dominio == GST_RESOURCE_ERROR && e.codigo == GST_RESOURCE_ERROR_WRITE,
              std::string(salida) == "grabacion" ? "grabación: error de escritura desde «mux»" : "transmisión: error de escritura desde «red»");
        g_usleep(100000);
        const int antes = g_llegan.load();
        g_usleep(300000);
        CHECK(g_llegan.load() == antes, "y no le llega más nada");
        apagar(p);
    }
    CHECK(!f.hay() && f.estado()["disparadas"].size() == 3, "las tres disparadas, ninguna pendiente");
    f.limpiar();
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
