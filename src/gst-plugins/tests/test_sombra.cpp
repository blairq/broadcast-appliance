// ============================================================================
// src/gst-plugins/tests/test_sombra.cpp — basombra: la sombra cae donde tiene
// que caer, el borde se funde, el margen agranda el cuadro, y la CPU y la GPU
// (basombragl) dan lo mismo
//
// La imagen de prueba: 64 x 48 transparente con un rectángulo rojo opaco en el
// medio (x 16..48, y 12..36), o toda opaca para el borde difuso.
// ============================================================================
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int fallas = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallas;
}

constexpr int kW = 64, kH = 48;

struct Imagen {
    int w = 0, h = 0;
    std::vector<unsigned char> px; // RGBA
    const unsigned char* en(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
};

std::vector<unsigned char> prueba(bool opaca) {
    std::vector<unsigned char> v(static_cast<size_t>(kW) * kH * 4, 0);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            unsigned char* p = &v[(static_cast<size_t>(y) * kW + x) * 4];
            if (opaca || (x >= 16 && x < 48 && y >= 12 && y < 36)) p[0] = 255, p[3] = 255;
        }
    return v;
}

/// Pasa la imagen por `elemento props` y devuelve lo que sale (w = 0 si no corrió).
Imagen pasar(const std::string& elemento, const std::string& props, bool opaca, std::string& error) {
    const bool gl = elemento.find("gl") != std::string::npos;
    const std::string caps = "video/x-raw,format=RGBA,width=" + std::to_string(kW) + ",height=" + std::to_string(kH) + ",framerate=1/1";
    const std::string d = "appsrc name=a format=time caps=" + caps + " ! " + (gl ? "glupload ! " : "") + elemento + " " + props +
                          (gl ? " ! gldownload" : "") + " ! video/x-raw,format=RGBA ! appsink name=k sync=false";
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &err);
    Imagen out;
    if (err != nullptr) {
        error = err->message;
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        return out;
    }
    GstElement* a = gst_bin_get_by_name(GST_BIN(p), "a");
    GstElement* k = gst_bin_get_by_name(GST_BIN(p), "k");
    gst_element_set_state(p, GST_STATE_PLAYING);
    const std::vector<unsigned char> img = prueba(opaca);
    GstBuffer* b = gst_buffer_new_memdup(img.data(), img.size());
    GST_BUFFER_PTS(b) = 0;
    GST_BUFFER_DURATION(b) = GST_SECOND;
    gst_app_src_push_buffer(GST_APP_SRC(a), b);
    gst_app_src_end_of_stream(GST_APP_SRC(a));
    if (GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(k), 10 * GST_SECOND)) {
        const GstStructure* st = gst_caps_get_structure(gst_sample_get_caps(s), 0);
        gst_structure_get_int(st, "width", &out.w);
        gst_structure_get_int(st, "height", &out.h);
        GstMapInfo m;
        GstBuffer* ob = gst_sample_get_buffer(s);
        if (gst_buffer_map(ob, &m, GST_MAP_READ)) {
            out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
            const size_t fila = m.size / static_cast<size_t>(out.h); // con su stride
            for (int y = 0; y < out.h; ++y) std::copy_n(m.data + y * fila, out.w * 4, &out.px[static_cast<size_t>(y) * out.w * 4]);
            gst_buffer_unmap(ob, &m);
        }
        gst_sample_unref(s);
    } else {
        GstBus* bus = gst_element_get_bus(p);
        if (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            error = e != nullptr ? e->message : "error";
            if (e != nullptr) g_error_free(e);
            gst_message_unref(msg);
        } else {
            error = "no salió ningún cuadro";
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(a);
    gst_object_unref(k);
    gst_object_unref(p);
    return out;
}

int diferenciaMaxima(const Imagen& a, const Imagen& b) {
    if (a.w != b.w || a.h != b.h) return 999;
    int m = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x) {
            const unsigned char *p = a.en(x, y), *q = b.en(x, y);
            // El color de un punto casi transparente no importa.
            const int alfa = std::abs(p[3] - q[3]);
            m = std::max(m, alfa);
            if (p[3] > 32 && q[3] > 32)
                for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(p[c] - q[c]));
        }
    return m;
}

} // namespace

int main(int argc, char** argv) {
#ifdef BA_TEST_GST_PLUGINS
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, FALSE);
#endif
    gst_init(&argc, &argv);
    std::string e;

    // Sombra a la derecha (ángulo 0), corrida 5, difuminada 4, en un margen de 10.
    const std::string sombra = "margen=10 alfa=1 difuminado=4 distancia=5 angulo=0";
    const Imagen c = pasar("basombra", sombra, false, e);
    check(c.w == kW + 20 && c.h == kH + 20, "el margen agranda el cuadro 10 de cada lado (" + std::to_string(c.w) + "x" + std::to_string(c.h) + ")");
    if (c.w == kW + 20) {
        const unsigned char* centro = c.en(10 + 32, 10 + 24);
        check(centro[0] == 255 && centro[3] == 255, "la imagen queda igual y en su lugar");
        const unsigned char* der = c.en(10 + 48 + 2, 10 + 24);
        check(der[3] > 128 && der[0] < 10, "a la derecha del rectángulo hay sombra negra (alfa " + std::to_string(der[3]) + ")");
        const unsigned char* izq = c.en(10 + 16 - 3, 10 + 24);
        check(izq[3] < 8, "a la izquierda no (alfa " + std::to_string(izq[3]) + ")");
        const unsigned char* lejos = c.en(10 + 48 + 6, 10 + 24); // el borde de la sombra (48 + 5) más 1, dentro del difuminado (4)
        check(lejos[3] < der[3] && lejos[3] > 0, "se difumina hacia afuera (" + std::to_string(lejos[3]) + ")");
    }

    // Abajo (90°): la sombra cae debajo, no al costado.
    const Imagen ab = pasar("basombra", "margen=10 alfa=1 difuminado=2 distancia=6 angulo=90", false, e);
    if (ab.w == kW + 20) {
        check(ab.en(10 + 32, 10 + 36 + 3)[3] > 128, "con 90° cae abajo");
        check(ab.en(10 + 48 + 3, 10 + 24)[3] < 8, "con 90° no cae a la derecha");
    }

    // La caja en el lienzo es la mitad de la imagen: el margen (en el lienzo) son 20 píxeles de la imagen.
    const Imagen esc = pasar("basombra", "margen=10 alfa=0.5 caja-ancho=32 caja-alto=24", false, e);
    check(esc.w == kW + 40 && esc.h == kH + 40, "el margen va en píxeles del lienzo (" + std::to_string(esc.w) + "x" + std::to_string(esc.h) + ")");

    // Borde difuso de 8 en una imagen opaca, sin sombra: mismo tamaño.
    const Imagen b = pasar("basombra", "borde=8", true, e);
    check(b.w == kW && b.h == kH, "sin margen, el mismo tamaño");
    if (b.w == kW) {
        check(b.en(0, 0)[3] < 8, "el borde se funde a transparente (esquina: " + std::to_string(b.en(0, 0)[3]) + ")");
        const int medio = b.en(4, kH / 2)[3];
        check(medio > 60 && medio < 200, "a la mitad del borde, a medias (" + std::to_string(medio) + ")");
        check(b.en(kW / 2, kH / 2)[3] == 255, "el centro, opaco");
    }

    // Esquinas redondeadas de radio 12 en una imagen opaca: la esquina transparente, el
    // centro y el medio de un lado opacos (y del mismo tamaño).
    const Imagen r = pasar("basombra", "radio=12", true, e);
    check(r.w == kW && r.h == kH, "con esquinas, el mismo tamaño");
    if (r.w == kW) {
        check(r.en(0, 0)[3] == 0 && r.en(kW - 1, kH - 1)[3] == 0, "las esquinas, transparentes");
        check(r.en(2, 2)[3] < 40, "cerca de la esquina, casi transparente (" + std::to_string(r.en(2, 2)[3]) + ")");
        check(r.en(0, kH / 2)[3] == 255 && r.en(kW / 2, 0)[3] == 255, "el medio de cada lado, opaco");
        check(r.en(kW / 2, kH / 2)[3] == 255, "el centro, opaco");
    }

    // Sin nada que hacer, pasa tal cual.
    const Imagen nada = pasar("basombra", "", false, e);
    check(nada.w == kW && nada.px == prueba(false), "sin sombra ni borde, la imagen pasa tal cual");

    // La GPU da lo mismo que la CPU.
    for (const std::string& props : {sombra, std::string("margen=12 alfa=0.7 difuminado=6 distancia=4 angulo=135 tamano=3 color=0xFF2040FF"),
                                     std::string("borde=6 margen=8 alfa=0.6 difuminado=3 distancia=0"),
                                     std::string("radio=10 margen=8 alfa=0.8 difuminado=4 distancia=3")}) {
        std::string eg;
        const Imagen cpu = pasar("basombra", props, false, e);
        const Imagen gpu = pasar("basombragl", props, false, eg);
        if (gpu.w == 0) {
            std::printf("salteado: basombragl (%s)\n", eg.c_str());
            continue;
        }
        const int d = diferenciaMaxima(cpu, gpu);
        check(d <= 6, "CPU y GPU iguales con «" + props + "» (diferencia máxima " + std::to_string(d) + "/255)");
    }

    std::printf(fallas ? "\ntest_sombra: %d fallas\n" : "\ntest_sombra: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
