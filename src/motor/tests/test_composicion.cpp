// test_composicion.cpp — el bus de video del motor (compositor) con escenas y transiciones
//
// Entradas de color sólido (rojo, verde, azul) y lo que sale del PROGRAMA leído
// píxel por píxel (como lo leería un consumidor: intersrc video:programa):
//   - una escena con dos capas en su lugar y con su tamaño; el resto, negro;
//   - corte a otra escena: la nueva enseguida y la vieja se va;
//   - la misma escena con una capa movida: se mueve andando (sin rearmar);
//   - mezcla de 1 s: a mitad de camino los colores se mezclan; al final, la nueva;
//   - recorte y alfa de una capa;
//   - sombra y borde difuso de una capa (basombra): la caja crece, la imagen no se mueve.
//   - la Mezcla de GStreamer: otro módulo de varias entradas (el compositor con las
//     propiedades de cada pad; smpte con sus dos pads fijos).

#include "Composicion.hpp"
#include "Piezas.hpp"
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
#include <tuple>
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
                              "\" ! queue ! " + aSistemaEscalado("RGBA", ancho, alto) +
                              " ! appsink name=s max-buffers=1 drop=true sync=true"; // GL o CPU, lo que use el motor
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
            const int stride = w * 4;
            const std::size_t i = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
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
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE); // los nuestros (basombra), sin instalar
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

    // Empuje: a mitad de camino, la vieja (rojo) a la izquierda y la nueva (azul) entrando por la derecha.
    {
        Escena azul;
        azul.nombre = "Azul";
        azul.capas.push_back({"azul", 0, 0, 0, 0});
        prog.mostrar(azul, "empuje", 1600);
        esperar(es, prog, 750);
        const Rgb izq = v.en(60, 180), der = v.en(600, 180);
        std::printf("empuje a mitad: izq %s, der %s\n", txt(izq).c_str(), txt(der).c_str());
        CHECK(igual(izq, 255, 0, 0), "empuje: la vieja todavía a la izquierda");
        CHECK(igual(der, 0, 0, 255), "empuje: la nueva entrando por la derecha");
        esperar(es, prog, 1200);
        CHECK(igual(v.en(60, 180), 0, 0, 255) && !prog.enTransicion(), "empuje terminado: la nueva entera");
        // Cortina: a mitad, la nueva (verde) a la izquierda y lo de antes (azul) a la derecha.
        Escena verdeE;
        verdeE.nombre = "Verde";
        verdeE.capas.push_back({"verde", 0, 0, 0, 0});
        prog.mostrar(verdeE, "cortina", 1600);
        esperar(es, prog, 750);
        const Rgb ci = v.en(60, 180), cd = v.en(600, 180);
        std::printf("cortina a mitad: izq %s, der %s\n", txt(ci).c_str(), txt(cd).c_str());
        CHECK(igual(ci, 0, 255, 0), "cortina: la nueva descubierta a la izquierda");
        CHECK(igual(cd, 0, 0, 255), "cortina: lo de antes todavía a la derecha");
        esperar(es, prog, 1200);
        CHECK(igual(v.en(600, 180), 0, 255, 0) && !prog.enTransicion(), "cortina terminada: la nueva entera");
    }

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
    if (hayFabrica("input-selector")) {
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
        // La placa pasa un cuadro por segundo (es siempre la misma imagen): el compositor
        // tiene que seguir mostrándola entre uno y otro, sin dejar ver el fondo.
        int sinPlaca = 0;
        for (int i = 0; i < 8; ++i) {
            esperar(es, prog, 500);
            if (!igual(v.en(20, 20), 0x1c, 0x1c, 0x22, 25)) ++sinPlaca;
        }
        std::printf("placa sostenida: %d de 8 miradas sin ella\n", sinPlaca);
        CHECK(sinPlaca == 0, "la placa se ve todo el tiempo (con un cuadro por segundo)");
        verde.actualizar(color("0xff00ff00"));
        std::unique_ptr<Visor> vv;
        if (std::getenv("BA_TRAZA")) vv = std::make_unique<Visor>(reloj, "verde:video", reloj.latencia(), 64, 36);
        for (int i = 0; i < 8 && std::getenv("BA_TRAZA"); ++i) {
            esperar(es, prog, 500);
            const json ev = verde.estado();
            std::printf("  directo verde:video %s edad p50 %.0f p95 %.0f lat %.0f cuadros %d\n", txt(vv->en(32, 18)).c_str(),
                        ev.value("edad_p50_ms", 0.0), ev.value("edad_p95_ms", 0.0), ev.value("latencia_ms", 0.0), ev.value("cuadros", 0));
            std::printf("  vuelta %d: %s placa %d %s\n", i, txt(v.en(320, 180)).c_str(), verde.enPlaca(), verde.estado()["sale"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        }
        esperar(es, prog, 2500);
        p1 = v.en(320, 180);
        std::printf("vuelve: %s, en placa: %d\n", txt(p1).c_str(), verde.enPlaca());
        CHECK(igual(p1, 0, 255, 0), "vuelve sola");
        CHECK(!verde.enPlaca(), "y deja la placa");
    }
    // Fuentes con transparencia (lo que daba OBS): un texto y un color con alfa sobre el rojo.
    {
        Entrada texto(reloj, lz, "texto", json{{"tipo", "texto"}, {"texto", "HOLA"}, {"tamano", 60}, {"color", "#FFFFFF"},
                                                {"ancho", 640}, {"alto", 360}});
        Entrada velo(reloj, lz, "velo", json{{"tipo", "color"}, {"color", "#8000FF00"}, {"ancho", 320}, {"alto", 180}});
        es.push_back(&texto);
        es.push_back(&velo);
        Escena t;
        t.nombre = "T";
        t.capas.push_back({"rojo", 0, 0, 0, 0});
        Capa kt{"texto", 0, 0, 0, 0};
        kt.orden = 2;
        t.capas.push_back(kt);
        Capa kv{"velo", 320, 180, 320, 180};
        kv.orden = 1;
        t.capas.push_back(kv);
        prog.mostrar(t, "corte");
        esperar(es, prog, 1500);
        const Rgb fondo = v.en(20, 20), letra = v.en(320, 180), velado = v.en(600, 340);
        std::printf("texto y velo: fondo %s, centro %s, velado %s\n", txt(fondo).c_str(), txt(letra).c_str(), txt(velado).c_str());
        CHECK(igual(fondo, 255, 0, 0), "donde no hay letras se ve lo de abajo (fondo transparente)");
        CHECK(igual(velado, 128, 128, 0, 45), "un color con alfa 0x80 se mezcla");
        int blancos = 0;
        for (int x = 200; x < 440; x += 4) {
            const Rgb c = v.en(x, 180);
            if (c.r > 200 && c.g > 200 && c.b > 200) ++blancos;
            if (blancos > 0) break;
        }
        CHECK(blancos > 0, "las letras blancas se ven");
        es.pop_back();
        es.pop_back();
        prog.mostrar(Escena{"vacia", {}}, "corte");
        esperar(es, prog, 300);
    }
    // Sombra: el verde en el medio sobre el rojo; primero sin sombra, después la MISMA
    // escena con sombra a la derecha (la rama se rearma una vez), y el borde difuso.
    if (hayFabrica("basombra") || hayFabrica("basombragl")) {
        Escena s;
        s.nombre = "S";
        s.capas.push_back({"rojo", 0, 0, 0, 0});
        Capa kv{"verde", 200, 100, 240, 160};
        kv.orden = 1;
        s.capas.push_back(kv);
        prog.mostrar(s, "corte");
        esperar(es, prog, 800);
        CHECK(igual(v.en(450, 180), 255, 0, 0), "sin sombra: a la derecha del verde, rojo");
        s.capas[1].sombra.alfa = 1;
        s.capas[1].sombra.difuminado = 4;
        s.capas[1].sombra.distancia = 20;
        s.capas[1].sombra.angulo = 0;
        prog.mostrar(s);
        esperar(es, prog, 1200);
        const Rgb centro = v.en(320, 180), der = v.en(450, 180), izq = v.en(190, 180), borde = v.en(205, 180);
        std::printf("sombra: centro %s, a la derecha %s, a la izquierda %s, borde izq %s\n", txt(centro).c_str(), txt(der).c_str(),
                    txt(izq).c_str(), txt(borde).c_str());
        CHECK(igual(centro, 0, 255, 0), "la imagen sigue en su lugar");
        CHECK(igual(borde, 0, 255, 0), "y del mismo tamaño (su borde izquierdo no se movió)");
        CHECK(igual(der, 0, 0, 0, 60), "la sombra cae a la derecha (negra)");
        CHECK(igual(izq, 255, 0, 0), "a la izquierda no hay sombra");
        // Borde difuso de 40 sin sombra: el borde del verde deja ver el rojo; el centro, verde.
        s.capas[1].sombra.alfa = 0;
        s.capas[1].sombra.borde = 40;
        prog.mostrar(s);
        esperar(es, prog, 800);
        const Rgb fundido = v.en(204, 180), medio = v.en(320, 180);
        std::printf("borde difuso: en el borde %s, en el medio %s\n", txt(fundido).c_str(), txt(medio).c_str());
        CHECK(fundido.r > 150 && fundido.g < 110, "en el borde se ve casi el rojo de abajo");
        CHECK(igual(medio, 0, 255, 0), "el medio, verde");
        CHECK(igual(v.en(450, 180), 255, 0, 0), "sin sombra otra vez, a la derecha rojo");
    } else {
        std::printf("sin basombra: no se prueba la sombra\n");
    }
    // DSK y clean feed: un programa con DSK; la capa del DSK (un recuadro azul) se ve en
    // el programa pero no en el limpio, y sigue arriba aunque cambie la escena.
    {
        Composicion::Opciones op;
        op.dsk = true;
        Composicion pd(reloj, lz, "pd", op);
        Escena r1;
        r1.nombre = "R";
        r1.capas.push_back({"rojo", 0, 0, 0, 0});
        pd.mostrar(r1);
        Capa logo{"azul", 520, 20, 100, 60};
        CHECK(pd.ponerDsk({logo}, 0), "poner el DSK");
        esperar(es, pd, 1500);
        Visor vp(reloj, pd.productor(), pd.latenciaSalidaNs(), lz.ancho, lz.alto);
        Visor vl(reloj, pd.productorLimpio(), pd.latenciaSalidaNs(), lz.ancho, lz.alto);
        esperar(es, pd, 900);
        Rgb conDsk = vp.en(570, 50), limpio = vl.en(570, 50), resto = vp.en(100, 100);
        std::printf("DSK: programa %s, limpio %s, resto %s\n", txt(conDsk).c_str(), txt(limpio).c_str(), txt(resto).c_str());
        CHECK(igual(conDsk, 0, 0, 255), "el DSK se ve en el programa");
        CHECK(igual(limpio, 255, 0, 0), "y no en el limpio");
        CHECK(igual(resto, 255, 0, 0), "la escena abajo");
        Escena g;
        g.nombre = "G";
        g.capas.push_back({"verde", 0, 0, 0, 0});
        pd.mostrar(g, "corte");
        esperar(es, pd, 700);
        conDsk = vp.en(570, 50);
        resto = vp.en(100, 100);
        std::printf("DSK tras cambiar de escena: %s, resto %s\n", txt(conDsk).c_str(), txt(resto).c_str());
        CHECK(igual(conDsk, 0, 0, 255) && igual(resto, 0, 255, 0), "el DSK sigue arriba con la escena nueva");
        CHECK(pd.ponerDsk({}, 400), "sacar el DSK con fundido");
        esperar(es, pd, 900);
        CHECK(igual(vp.en(570, 50), 0, 255, 0), "sin DSK, la escena");
        CHECK(pd.estado()["dsk"].empty(), "y se soltó la capa");
    }
    // LA MEZCLA DE GSTREAMER (otro mezclador): un compositor como un módulo cualquiera, con
    // las propiedades de cada pad tal cual (el de la CPU y el de la placa de este sistema:
    // la imagen va en su memoria), y smpte con sus dos entradas fijas.
    for (const char* modulo : {"compositor", "glvideomixer", "d3d12compositor"}) {
        if (!hayFabrica(modulo) || (std::string(modulo) != "compositor" && memoriaDeTrabajo(modulo) != memoriaDelMotor())) continue;
        const std::string mod = modulo;
        Composicion::Opciones op;
        op.mezclador = {{"elemento", mod}};
        Composicion mx(reloj, lz, "mezcla_" + mod, op);
        CHECK(mx.anda(), ("la mezcla con " + mod + " arranca").c_str());
        Escena m;
        m.nombre = "__compositor:mezcla";
        Capa a{"rojo"};
        a.pad = "sink_0";
        a.propiedadesPad = {{"width", 640}, {"height", 360}};
        Capa b{"verde"};
        b.pad = "sink_1";
        b.propiedadesPad = {{"xpos", 320}, {"ypos", 180}, {"width", 320}, {"height", 180}, {"zorder", 1}};
        m.capas = {a, b};
        mx.mostrar(m);
        esperar(es, mx, 1500);
        Visor vm(reloj, mx.productor(), mx.latenciaSalidaNs(), lz.ancho, lz.alto);
        esperar(es, mx, 800);
        Rgb izq = vm.en(100, 100), der = vm.en(500, 300);
        std::printf("mezcla (%s): arriba izq %s, abajo der %s\n", modulo, txt(izq).c_str(), txt(der).c_str());
        CHECK(igual(izq, 255, 0, 0), ("mezcla " + mod + ": el rojo de fondo (sink_0)").c_str());
        CHECK(igual(der, 0, 255, 0), ("mezcla " + mod + ": el verde donde dicen sus xpos/ypos (sink_1)").c_str());
        // Las propiedades del pad cambian andando (misma rama).
        m.capas[1].propiedadesPad["xpos"] = 0;
        m.capas[1].propiedadesPad["ypos"] = 0;
        mx.mostrar(m);
        esperar(es, mx, 500);
        izq = vm.en(100, 100);
        der = vm.en(500, 300);
        std::printf("mezcla (%s) movida: arriba izq %s, abajo der %s\n", modulo, txt(izq).c_str(), txt(der).c_str());
        CHECK(igual(izq, 0, 255, 0) && igual(der, 255, 0, 0), ("mezcla " + mod + ": el verde se movió arriba a la izquierda").c_str());
        CHECK(mx.estado()["capas"].size() == 2 && mx.estado().value("mezclador", "") == mod, "mezcla: dos ramas, con su módulo");
        CHECK(mx.ponerMezclador(json{{"elemento", mod}}), "el mismo módulo: andando");
        CHECK(!mx.ponerMezclador(json{{"elemento", "smpte"}}), "otro módulo: hay que rearmarla");
    }
    if (hayFabrica("smpte")) {
        Composicion::Opciones op;
        op.mezclador = {{"elemento", "smpte"}, {"propiedades", {{"type", "bar-wipe-lr"}}}};
        Composicion sm(reloj, lz, "smpte", op);
        CHECK(sm.anda(), "la mezcla con smpte arranca");
        Escena m;
        m.nombre = "__compositor:smpte";
        Capa a{"rojo"};
        a.pad = "sink1";
        Capa b{"azul"};
        b.pad = "sink2";
        m.capas = {a, b};
        sm.mostrar(m);
        esperar(es, sm, 1500);
        Visor vs(reloj, sm.productor(), sm.latenciaSalidaNs(), lz.ancho, lz.alto);
        esperar(es, sm, 800);
        const Rgb c = vs.en(320, 180);
        std::printf("mezcla (smpte): %s; %s\n", txt(c).c_str(), sm.estado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        CHECK(igual(c, 255, 0, 0) || igual(c, 0, 0, 255), "smpte: junta sus dos entradas fijas (sink1 rojo, sink2 azul)");
        CHECK(sm.estado()["capas"].size() == 2, "smpte: una rama por pad fijo");
        // smpte cuenta su tiempo desde 0: lo que sale tiene que llevar la hora de lo que entra.
        const json edad = sm.estado().value("edad_salida", json::object());
        CHECK(edad.is_object() && edad.value("p50_ms", 1e9) < 1000, "smpte: lo que sale va en hora (no 30 s tarde)");
    } else {
        std::printf("sin smpte: no se prueba la mezcla de dos entradas fijas\n");
    }
    // Los otros de varias entradas que haya en este sistema: arrancan y dan imagen.
    for (const auto& [modulo, p1, p2] : std::initializer_list<std::tuple<const char*, const char*, const char*>>{
             {"glmosaic", "sink_0", "sink_1"}, {"glstereomix", "sink_0", "sink_1"}, {"shapewipe", "video_sink", "mask_sink"}}) {
        if (!hayFabrica(modulo)) continue;
        Composicion::Opciones op;
        op.mezclador = {{"elemento", modulo}};
        Composicion otro(reloj, lz, std::string("otro_") + modulo, op);
        Escena m;
        m.nombre = std::string("__compositor:") + modulo;
        Capa a{"rojo"};
        a.pad = p1;
        Capa b{"azul"};
        b.pad = p2;
        m.capas = {a, b};
        otro.mostrar(m);
        esperar(es, otro, 1500);
        Visor vo(reloj, otro.productor(), otro.latenciaSalidaNs(), lz.ancho, lz.alto);
        esperar(es, otro, 800);
        const Rgb c = vo.en(320, 180);
        std::printf("mezcla (%s): %s; error «%s»\n", modulo, txt(c).c_str(), otro.estado().value("error", "").c_str());
        CHECK(c.r >= 0 && otro.estado()["capas"].size() == 2, (std::string(modulo) + ": da imagen con sus dos entradas").c_str());
    }
    std::printf("composición: %s\n", prog.estado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    std::printf("== test_composicion: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
