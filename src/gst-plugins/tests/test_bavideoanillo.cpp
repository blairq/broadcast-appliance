// test_bavideoanillo.cpp — bavideoanillo: el video de un canal compartido como fuente de GStreamer
//
// Hace de gateway: publica un canal de 320x180 (como BA.Guest.<n>) y le manda
// cuadros. La cadena `bavideoanillo ! appsink` tiene que salir en 320x180 (el
// tamaño del canal), también después de que abajo pidan renegociar: la
// negociación de fábrica de basesrc fijaba el template en 1x1 y los elementos
// D3D12 de Windows piden renegociar, así que el invitado quedaba negro (1x1).
// Y un invitado chico viaja a su tamaño (F2-13, V3): un cuadro de 160x90 con su tamaño
// real sale de 160x90 (las caps cambian), sin agrandarse al del canal.

#include "ipc/SpoutTransport.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        }                                                                            \
    } while (0)

std::atomic<bool> g_fin{false}, g_chico{false};

void gateway(const wchar_t* nombre) {
    ba::ipc::SpoutSender snd;
    if (!snd.publish(nombre, ba::ipc::SlotConfig{320, 180})) {
        std::printf("no se pudo publicar el canal\n");
        return;
    }
    std::vector<std::uint8_t> px(320u * 180u * 4u, 0x80), chico(160u * 90u * 4u, 0x40);
    for (std::uint64_t i = 1; !g_fin; ++i) {
        if (g_chico)
            snd.send(chico.data(), chico.size(), ba::ipc::FrameMeta{i, 0, 0, 160, 90});
        else
            snd.send(px.data(), px.size(), ba::ipc::FrameMeta{i, 0});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

/// El tamaño de las caps del appsink, o {0,0}.
void tamano(GstElement* sink, int& w, int& h) {
    w = h = 0;
    GstPad* pad = gst_element_get_static_pad(sink, "sink");
    if (GstCaps* c = gst_pad_get_current_caps(pad)) {
        const GstStructure* s = gst_caps_get_structure(c, 0);
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);
        gst_caps_unref(c);
    }
    gst_object_unref(pad);
}

/// Saca `n` cuadros del appsink; devuelve cuántos tenían `bytes` (de fábrica, 320x180 BGRA).
int sacar(GstElement* sink, int n, std::size_t bytes = 320u * 180u * 4u) {
    int buenos = 0;
    for (int i = 0; i < n; ++i) {
        GstSample* s = nullptr;
        g_signal_emit_by_name(sink, "try-pull-sample", static_cast<GstClockTime>(2 * GST_SECOND), &s);
        if (s == nullptr) break;
        if (gst_buffer_get_size(gst_sample_get_buffer(s)) == bytes) ++buenos;
        gst_sample_unref(s);
    }
    return buenos;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, TRUE);
#endif
    gst_init(nullptr, nullptr);
    if (GstElementFactory* f = gst_element_factory_find("bavideoanillo")) {
        gst_object_unref(f);
    } else {
        std::printf("SKIP: no está bavideoanillo\n");
        return 77;
    }
    std::thread gw(gateway, L"BA.Video.Prueba.1");

    GError* err = nullptr;
    GstElement* p = gst_parse_launch(
        "bavideoanillo canal=BA.Video.Prueba.1 ! queue ! appsink name=s sync=false max-buffers=4 drop=true", &err);
    CHECK(p != nullptr, "la cadena se arma");
    if (p == nullptr) {
        g_fin = true;
        gw.join();
        return 1;
    }
    GstElement* sink = gst_bin_get_by_name(GST_BIN(p), "s");
    gst_element_set_state(p, GST_STATE_PLAYING);

    CHECK(sacar(sink, 10) == 10, "salen cuadros enteros del canal");
    int w = 0, h = 0;
    tamano(sink, w, h);
    CHECK(w == 320 && h == 180, "las caps tienen el tamaño del canal");

    // Abajo piden renegociar (como d3d12upload): el tamaño no cambia.
    for (int i = 0; i < 3; ++i) {
        GstPad* pad = gst_element_get_static_pad(sink, "sink");
        gst_pad_push_event(pad, gst_event_new_reconfigure());
        gst_object_unref(pad);
        CHECK(sacar(sink, 5) == 5, "después de renegociar siguen los cuadros enteros");
    }
    tamano(sink, w, h);
    CHECK(w == 320 && h == 180, "después de renegociar, el tamaño del canal (no 1x1)");

    // Un invitado chico: a su tamaño.
    g_chico = true;
    sacar(sink, 4, 0); // los que ya estaban en camino
    CHECK(sacar(sink, 5, 160u * 90u * 4u) == 5, "el chico sale con sus bytes (160x90)");
    tamano(sink, w, h);
    CHECK(w == 160 && h == 90, "y las caps con su tamaño, no el del canal");
    g_chico = false;
    sacar(sink, 4, 0);
    CHECK(sacar(sink, 5) == 5, "vuelve al tamaño del canal");

    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(p);
    g_fin = true;
    gw.join();
    std::printf(fallos == 0 ? "== test_bavideoanillo: OK ==\n" : "== test_bavideoanillo: %d fallos ==\n", fallos);
    return fallos == 0 ? 0 : 1;
}
