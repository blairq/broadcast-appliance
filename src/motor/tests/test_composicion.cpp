// test_composicion.cpp — el bus de video del motor (compositor) con escenas y transiciones
//
// Entradas de color sólido (rojo, verde, azul) y lo que sale del PROGRAMA leído
// píxel por píxel (como lo leería un consumidor: intersrc video:programa):
//   - una escena con dos capas en su lugar y con su tamaño; el resto, negro;
//   - corte a otra escena: la nueva enseguida y la vieja se va;
//   - la misma escena con una capa movida: se mueve andando (sin rearmar);
//   - mezcla de 1 s: a mitad de camino los colores se mezclan; al final, la nueva;
//   - recorte y alfa de una capa.

#include "Composicion.hpp"
#include "Entrada.hpp"
#include "Reloj.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

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

struct Rgb {
    int r = -1, g = -1, b = -1;
};

/// Un consumidor del programa: guarda el último cuadro en RGB.
struct Visor {
    GstElement* p = nullptr;
    GstElement* sink = nullptr;
    int w = 0, h = 0;
    Visor(Reloj& reloj, const std::string& productor, std::int64_t latNs, int ancho, int alto) : w(ancho), h(alto) {
        const std::string d = "intersrc producer-name=\"" + productor +
                              "\" ! queue ! videoconvert ! videoscale ! video/x-raw,format=RGB,width=" + std::to_string(ancho) +
                              ",height=" + std::to_string(alto) + " ! appsink name=s max-buffers=1 drop=true sync=true";
        p = gst_parse_launch(d.c_str(), nullptr);
        sink = gst_bin_get_by_name(GST_BIN(p), "s");
        reloj.adoptar(p);
        gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(latNs + 60 * GST_MSECOND));
        gst_element_set_state(p, GST_STATE_PLAYING);
    }
    ~Visor() {
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(sink);
        gst_object_unref(p);
    }
    Rgb en(int x, int y) {
        Rgb c;
        GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 2 * GST_SECOND);
        if (s == nullptr) return c;
        GstBuffer* b = gst_sample_get_buffer(s);
        GstMapInfo m;
        if (gst_buffer_map(b, &m, GST_MAP_READ)) {
            const int stride = (w * 3 + 3) & ~3;
            const std::size_t i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 3;
            if (i + 2 < m.size) c = {m.data[i], m.data[i + 1], m.data[i + 2]};
            gst_buffer_unmap(b, &m);
        }
        gst_sample_unref(s);
        return c;
    }
};

bool igual(const Rgb& c, int r, int g, int b, int tol = 40) {
    return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol && std::abs(c.b - b) <= tol;
}
std::string txt(const Rgb& c) { return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")"; }

json color(const char* argb) {
    return {{"pipeline", std::string("videotestsrc is-live=true do-timestamp=true pattern=solid-color foreground-color=") + argb +
                             " ! video/x-raw,width=320,height=180,framerate=30/1"}};
}

void esperar(std::vector<Entrada*>& es, Composicion& c, int ms) {
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < fin) {
        for (Entrada* e : es) e->tic();
        c.tic();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, 1);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "intersrc", "compositor", "videocrop"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Reloj reloj;
    Lienzo lz;
    lz.ancho = 640;
    lz.alto = 360;
    Entrada rojo(reloj, lz, "rojo", color("0xffff0000"));
    Entrada verde(reloj, lz, "verde", color("0xff00ff00"));
    Entrada azul(reloj, lz, "azul", color("0xff0000ff"));
    std::vector<Entrada*> es{&rojo, &verde, &azul};
    Composicion prog(reloj, lz, "programa");
    CHECK(prog.anda(), "el compositor arranca");

    Escena a;
    a.nombre = "A";
    a.capas.push_back({"rojo", 0, 0, 320, 180});
    a.capas.push_back({"verde", 320, 180, 320, 180});
    prog.mostrar(a);
    esperar(es, prog, 1500);
    Visor v(reloj, prog.productor(), prog.latenciaSalidaNs(), lz.ancho, lz.alto);
    esperar(es, prog, 800);
    Rgb p1 = v.en(100, 100), p2 = v.en(500, 300), p3 = v.en(500, 100);
    std::printf("escena A: arriba izq %s, abajo der %s, arriba der %s\n", txt(p1).c_str(), txt(p2).c_str(), txt(p3).c_str());
    CHECK(igual(p1, 255, 0, 0), "rojo arriba a la izquierda");
    CHECK(igual(p2, 0, 255, 0), "verde abajo a la derecha");
    CHECK(igual(p3, 0, 0, 0), "el resto, negro");

    Escena b;
    b.nombre = "B";
    b.capas.push_back({"azul", 0, 0, 0, 0}); // todo el lienzo
    prog.mostrar(b, "corte");
    esperar(es, prog, 600);
    p1 = v.en(100, 100);
    std::printf("corte a B: %s (en transición: %d)\n", txt(p1).c_str(), prog.enTransicion());
    CHECK(igual(p1, 0, 0, 255), "corte: la nueva entera");
    CHECK(!prog.enTransicion(), "el corte terminó (la vieja se fue)");
    CHECK(prog.estado()["capas"].size() == 1, "queda una capa");

    // La misma escena, con la capa achicada: se actualiza en el lugar.
    b.capas[0] = {"azul", 320, 0, 320, 180};
    prog.mostrar(b);
    esperar(es, prog, 400);
    p1 = v.en(100, 100);
    p2 = v.en(500, 100);
    std::printf("B achicada: izq %s, der %s\n", txt(p1).c_str(), txt(p2).c_str());
    CHECK(igual(p1, 0, 0, 0), "donde estaba, negro");
    CHECK(igual(p2, 0, 0, 255), "se movió andando");
    CHECK(prog.estado()["capas"][0].value("generacion", 0) == 2, "sin rearmar (misma generación)");

    // Mezcla de 1 s del rojo entero (sobre el negro de la izquierda).
    Escena c;
    c.nombre = "C";
    c.capas.push_back({"rojo", 0, 0, 0, 0});
    prog.mostrar(c, "mezcla", 1000);
    esperar(es, prog, 450);
    p1 = v.en(100, 100);
    std::printf("mezcla a mitad: %s\n", txt(p1).c_str());
    CHECK(p1.r > 50 && p1.r < 220, "a mitad de la mezcla, el rojo va subiendo");
    esperar(es, prog, 1000);
    p1 = v.en(100, 100);
    p2 = v.en(500, 100);
    std::printf("mezcla terminada: %s, %s\n", txt(p1).c_str(), txt(p2).c_str());
    CHECK(igual(p1, 255, 0, 0) && igual(p2, 255, 0, 0), "terminada: la nueva entera");
    CHECK(!prog.enTransicion(), "la mezcla terminó");

    // Recorte y alfa.
    Escena d;
    d.nombre = "D";
    Capa k{"verde", 0, 0, 0, 0};
    k.alfa = 0.5;
    d.capas.push_back({"rojo", 0, 0, 0, 0});
    k.orden = 1;
    d.capas.push_back(k);
    prog.mostrar(d, "corte");
    esperar(es, prog, 600);
    p1 = v.en(320, 180);
    std::printf("verde al 50 %% sobre rojo: %s\n", txt(p1).c_str());
    CHECK(igual(p1, 128, 128, 0, 45), "alfa 0,5: mitad y mitad");
    // La placa: una entrada en vivo cuya cadena se cae muestra «sin señal» y vuelve sola.
    if (hayFabrica("fallbackswitch")) {
        Escena f;
        f.nombre = "F";
        f.capas.push_back({"verde", 0, 0, 0, 0});
        prog.mostrar(f, "corte");
        esperar(es, prog, 800);
        CHECK(igual(v.en(320, 180), 0, 255, 0), "la verde al aire");
        CHECK(!verde.enPlaca(), "sin placa mientras manda");
        // Se le cambia el origen por uno que no existe: la cadena se cae y se reintenta.
        verde.actualizar(json{{"pipeline", "elementoquenoexiste"}, {"nombre", "Cámara verde"}});
        esperar(es, prog, 4500);
        p1 = v.en(20, 20);
        std::printf("caída: %s, en placa: %d\n", txt(p1).c_str(), verde.enPlaca());
        CHECK(verde.enPlaca(), "a los 3 s, la placa");
        CHECK(igual(p1, 0x1c, 0x1c, 0x22, 25), "se ve la placa (gris oscuro), ni verde congelado ni negro");
        verde.actualizar(color("0xff00ff00"));
        std::unique_ptr<Visor> vv;
        if (std::getenv("BA_TRAZA")) vv = std::make_unique<Visor>(reloj, "verde:video", reloj.latencia(), 64, 36);
        for (int i = 0; i < 8 && std::getenv("BA_TRAZA"); ++i) {
            esperar(es, prog, 500);
            const json ev = verde.estado();
            std::printf("  directo verde:video %s edad p50 %.0f p95 %.0f lat %.0f cuadros %d\n", txt(vv->en(32, 18)).c_str(),
                        ev.value("edad_p50_ms", 0.0), ev.value("edad_p95_ms", 0.0), ev.value("latencia_ms", 0.0), ev.value("cuadros", 0));
            std::printf("  vuelta %d: %s placa %d %s\n", i, txt(v.en(320, 180)).c_str(), verde.enPlaca(), verde.estado()["sale"].dump().c_str());
        }
        esperar(es, prog, 2500);
        p1 = v.en(320, 180);
        std::printf("vuelve: %s, en placa: %d\n", txt(p1).c_str(), verde.enPlaca());
        CHECK(igual(p1, 0, 255, 0), "vuelve sola");
        CHECK(!verde.enPlaca(), "y deja la placa");
    }
    std::printf("composición: %s\n", prog.estado().dump().c_str());
    std::printf("== test_composicion: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
