// test_motor_salidas.cpp — muchas salidas en el motor (docs/planes/PLAN-SALIDAS.md, etapa 1)
//
//   - la salida por omisión («principal») existe desde el arranque, con los nombres de siempre;
//   - dos salidas con escenas distintas al aire a la vez (el color de «programa» y «programa:b»);
//   - la misma escena en las dos;
//   - el take y la T-bar de una no tocan la otra;
//   - el audio sigue a la escena de cada salida: un tono en una escena al aire solo en B
//     suena en el bus de B y no en el de la por omisión;
//   - el fader y el mute son de cada salida;
//   - la toma del programa (el loudness) de otra salida;
//   - el tally del multiview: rojo si está al aire en alguna salida;
//   - crear y borrar una salida andando sin romper la por omisión (que no se borra).

#include "Motor.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
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

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Px {
    int r = -1, g = -1, b = -1;
};
Px px(const Cuadro& c, int x, int y) {
    if (c.bgra.empty()) return {};
    const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
    return {p[2], p[1], p[0]};
}
bool igual(const Px& p, int r, int g, int b, int tol = 40) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}
std::string txt(const Px& p) { return "(" + std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b) + ")"; }

Px mirar(Motor& m, const std::string& clave, int x, int y) {
    Cuadro c;
    std::uint64_t s = 0;
    for (int i = 0; i < 30 && !m.verUltimo(clave, c, s); ++i) esperar(50);
    return px(c, x, y);
}

/// Espera hasta que lo que se ve cumpla (o el tope) y devuelve lo último que vio.
template <typename F>
Px mirarHasta(Motor& m, const std::string& clave, int x, int y, F cumple, int topeMs = 6000) {
    Px p = mirar(m, clave, x, y);
    for (int t = 0; t < topeMs && !cumple(p); t += 100) {
        esperar(100);
        p = mirar(m, clave, x, y);
    }
    return p;
}

/// El RMS del bus de una salida (dBFS) según el estado del motor.
double rms(Motor& m, const std::string& salida) {
    const json e = m.estado();
    if (!e["salidas"].contains(salida) || !e["salidas"][salida]["programa"].is_object()) return -999;
    return e["salidas"][salida]["programa"].value("rms_db", -120.0);
}

/// Espera hasta que el RMS de la salida cumpla (o el tope) y lo devuelve.
template <typename F>
double rmsHasta(Motor& m, const std::string& salida, F cumple, int topeMs = 4000) {
    double v = rms(m, salida);
    for (int t = 0; t < topeMs && !cumple(v); t += 100) {
        esperar(100);
        v = rms(m, salida);
    }
    return v;
}

/// El RMS de la pista n de una salida (dBFS), esperando hasta que cumpla (o el tope).
template <typename F>
double rmsPistaHasta(Motor& m, const std::string& salida, int n, F cumple, int topeMs = 4000) {
    double v = -999;
    for (int t = 0; t <= topeMs; t += 100) {
        const json e = m.estado();
        const std::string k = std::to_string(n);
        v = e["salidas"].contains(salida) && e["salidas"][salida]["pistas"].contains(k)
                ? e["salidas"][salida]["pistas"][k].value("rms_db", -120.0)
                : -999;
        if (cumple(v)) break;
        esperar(100);
    }
    return v;
}

bool suena(double db) { return std::fabs(db - (-15.05)) < 1.5; } // un seno de 0,25
bool calla(double db) { return db < -60; }

json color(const char* c) { return {{"tipo", "color"}, {"color", c}, {"ancho", 320}, {"alto", 180}}; }
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
    for (const char* e : {"intersink", "compositor", "imagefreeze", "audiomixer", "level"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.lienzo.ancho = 320;
    cfg.lienzo.alto = 180;
    cfg.prefijoAnillos = "";
    cfg.sinkEscucha = "fakesink";
    Motor m(cfg);
    std::mutex fallasM;
    std::vector<std::string> fallas; // las órdenes encoladas que fallaron
    m.alFallar([&](const std::string& que, const std::string& e) {
        std::lock_guard<std::mutex> l(fallasM);
        fallas.push_back(que + ": " + e);
    });
    const auto fallo = [&](const std::string& que) {
        m.esperarCola();
        std::lock_guard<std::mutex> l(fallasM);
        for (const auto& f : fallas)
            if (f.rfind(que + ":", 0) == 0) return true;
        return false;
    };
    CHECK(m.iniciar().empty(), "el motor arranca");
    CHECK(m.salidaPorOmision() == "principal", "la por omisión es «principal»");
    CHECK(m.agregarEntrada("rojo", color("#FF0000")).empty(), "rojo");
    CHECK(m.agregarEntrada("verde", color("#00FF00")).empty(), "verde");
    // Un tono de ESCENA (no global): suena solo donde su escena está al aire.
    CHECK(m.agregarEntrada("tono", json{{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"}}).empty(),
          "un tono de escena");
    m.definirEscena({{"nombre", "Roja"}, {"capas", {{{"entrada", "rojo"}}}}});
    m.definirEscena({{"nombre", "Verde"}, {"capas", {{{"entrada", "verde"}}, {{"entrada", "tono"}}}}});

    CHECK(m.crearSalida("b", "Salida B").empty(), "crear la salida B");
    m.esperarCola();
    CHECK(!fallo("crearSalida"), "la salida B se creó");
    {
        const json e = m.estado();
        CHECK(e["salidas"].contains("principal") && e["salidas"].contains("b"), "las dos en el estado");
        CHECK(e["salidas"]["principal"].value("por_omision", false), "la principal es la por omisión");
        CHECK(e["salidas"]["b"].value("nombre", "") == "Salida B", "con su nombre");
    }
    std::int64_t lat = 0;
    CHECK(m.productorDeVideo("programa") == "video:programa", "la por omisión, con el nombre de siempre");
    CHECK(m.productorDeVideo("programa", &lat, "b") == "video:programa:b", "la B, con su id");
    CHECK(m.productorDeVideo("programa:b") == "video:programa:b", "la B por la clave");
    CHECK(m.productorDeVideo("limpio:b") == "video:programa:b:limpio", "la B limpia");
    CHECK(m.productorDeVideo("preview:b") == "video:preview:b", "el preview de la B");
    CHECK(m.productorDeBus("programa") == "bus:programa", "el bus de la por omisión");
    CHECK(m.productorDeBus("programa", &lat, "b") == "bus:programa:b", "el bus de la B");
    CHECK(m.productorDeBus("programa:b") == "bus:programa:b", "el bus de la B por la clave");
    CHECK(m.productorDeVideo("programa:nadie").empty(), "una salida que no existe: nada");

    // Dos escenas distintas al aire a la vez.
    m.alAire("Roja");
    m.alAire("Verde", "corte", 0, "b");
    CHECK(m.verPedir("programa", 320, 180).empty(), "ver el programa");
    CHECK(m.verPedir("programa:b", 320, 180).empty(), "ver el programa de la B");
    {
        const Px a = mirarHasta(m, "programa", 160, 90, [](const Px& p) { return igual(p, 255, 0, 0); });
        const Px b = mirarHasta(m, "programa:b", 160, 90, [](const Px& p) { return igual(p, 0, 255, 0); });
        std::printf("principal %s, B %s\n", txt(a).c_str(), txt(b).c_str());
        CHECK(igual(a, 255, 0, 0), "la principal, roja");
        CHECK(igual(b, 0, 255, 0), "la B, verde");
        // La vista en NV12 (el canal del emisor aparte, F3-01): los dos planos, el UV
        // después del Y; el rojo tiene el V alto y el U bajo.
        CHECK(m.verPedir("programa@emisor", 320, 180, "NV12").empty(), "ver el programa en NV12");
        Cuadro c;
        std::uint64_t serie = 0;
        for (int i = 0; i < 60 && !m.verUltimo("programa@emisor", c, serie); ++i) esperar(50);
        CHECK(c.nv12 && c.ancho == 320 && c.alto == 180, "un cuadro NV12 del tamaño pedido");
        CHECK(c.desdeUv >= static_cast<std::size_t>(c.stride) * 180 && c.strideUv >= 320 &&
                  c.bgra.size() >= c.desdeUv + static_cast<std::size_t>(c.strideUv) * 90,
              "con el plano UV después del Y");
        if (c.nv12 && !c.bgra.empty()) {
            const std::uint8_t* uv = c.bgra.data() + c.desdeUv + static_cast<std::size_t>(c.strideUv) * 45 + 160;
            std::printf("NV12 en el centro: Y %d, U %d, V %d\n", c.bgra.data()[c.stride * 90 + 160], uv[0], uv[1]);
            CHECK(uv[1] > 200 && uv[0] < 128, "el rojo en NV12");
        }
        m.verSoltar("programa@emisor");
        m.pistasAlEmisor("b", {2});
        CHECK(fallo("pistasAlEmisor"), "las pistas al emisor, solo con emisor");
        const json e = m.estado();
        CHECK(e["video"].value("al_aire", "") == "Roja", "los campos de siempre: la por omisión");
        CHECK(e["salidas"]["b"].value("al_aire", "") == "Verde", "la B dice lo suyo");
    }
    // El audio sigue a la escena de cada salida.
    {
        const double b = rmsHasta(m, "b", suena);
        const double a = rmsHasta(m, "principal", calla);
        std::printf("tono en la escena de la B: principal %.1f dB, B %.1f dB\n", a, b);
        CHECK(suena(b), "el tono suena en el bus de la B");
        CHECK(calla(a), "y no en el de la principal");
        const json e = m.estado();
        CHECK(!e["entradas"]["tono"].value("en_escena", true), "en la por omisión, fuera de escena");
        CHECK(e["salidas"]["b"]["mandos"]["tono"].value("en_escena", false), "en la B, en escena");
    }
    // La toma del programa de la B (lo que mide el loudness de esa salida).
    {
        std::atomic<double> pico{0};
        std::atomic<std::size_t> cuadros{0};
        const int oyente = m.escucharPrograma(
            [&](const float* d, std::size_t n) {
                double p = 0;
                for (std::size_t i = 0; i < n * 2; ++i) p = std::max(p, static_cast<double>(std::fabs(d[i])));
                pico = p;
                cuadros += n;
            },
            "b");
        esperar(1500);
        std::printf("toma de la B: %zu cuadros, pico %.3f\n", cuadros.load(), pico.load());
        CHECK(cuadros > 24000, "llega el audio del programa de la B");
        CHECK(std::fabs(pico - 0.25) < 0.03, "con el tono");
        m.dejarDeEscuchar(oyente);
    }

    // La misma escena en las dos.
    m.alAire("Verde");
    {
        const Px a = mirarHasta(m, "programa", 160, 90, [](const Px& p) { return igual(p, 0, 255, 0); });
        CHECK(igual(a, 0, 255, 0), "la principal también verde");
        CHECK(igual(mirar(m, "programa:b", 160, 90), 0, 255, 0), "y la B sigue verde");
        CHECK(suena(rmsHasta(m, "principal", suena)), "el tono suena en la principal");
        CHECK(suena(rms(m, "b")), "y en la B");
    }

    // El take de la B no toca la principal.
    m.armar("Roja", "b");
    m.take("corte", 0, "b");
    {
        const Px b = mirarHasta(m, "programa:b", 160, 90, [](const Px& p) { return igual(p, 255, 0, 0); });
        CHECK(igual(b, 255, 0, 0), "take en la B: roja");
        CHECK(igual(mirar(m, "programa", 160, 90), 0, 255, 0), "la principal sigue verde");
        const json e = m.estado();
        CHECK(e["salidas"]["b"].value("armada", "") == "Verde", "el take intercambia en la B");
        CHECK(e["video"].value("armada", "x").empty(), "y no arma nada en la principal");
        CHECK(calla(rmsHasta(m, "b", calla)), "la B se calla (el tono salió de su escena)");
        CHECK(suena(rms(m, "principal")), "la principal sigue sonando");
    }
    // La T-bar de la principal no toca la B.
    m.tbar("Roja", 0.5, false);
    {
        const auto mezclado = [](const Px& p) { return p.r > 70 && p.r < 190 && p.g > 70 && p.g < 190; };
        const Px a = mirarHasta(m, "programa", 160, 90, mezclado);
        std::printf("T-bar de la principal a mitad: %s (hacia %s); B %s\n", txt(a).c_str(), m.tbarHacia().c_str(),
                    m.tbarHacia("b").c_str());
        CHECK(mezclado(a), "la principal, a mitad");
        CHECK(m.tbarHacia() == "Roja", "va hacia la roja");
        CHECK(m.tbarHacia("b").empty(), "la B no tiene T-bar");
        CHECK(igual(mirar(m, "programa:b", 160, 90), 255, 0, 0), "la B sigue roja entera");
    }
    CHECK(m.tbar("Roja", 1.0, true), "la T-bar termina");
    esperar(500);
    CHECK(igual(mirarHasta(m, "programa", 160, 90, [](const Px& p) { return igual(p, 255, 0, 0); }), 255, 0, 0),
          "la principal queda roja");
    CHECK(m.estado()["salidas"]["b"].value("al_aire", "") == "Roja", "la B sigue con lo suyo");

    // El fader y el mute de cada salida: la Verde en las dos.
    m.alAire("Verde");
    m.alAire("Verde", "corte", 0, "b");
    CHECK(suena(rmsHasta(m, "principal", suena)) && suena(rmsHasta(m, "b", suena)), "el tono en las dos");
    m.ponerNivel("tono", 0.0, "b");
    {
        const double b = rmsHasta(m, "b", calla);
        std::printf("fader de la B a 0: principal %.1f, B %.1f\n", rms(m, "principal"), b);
        CHECK(calla(b), "el fader de la B la calla en la B");
        CHECK(suena(rms(m, "principal")), "y no en la principal");
    }
    m.ponerNivel("tono", 1.0, "b");
    m.ponerMute("tono", true);
    {
        const double a = rmsHasta(m, "principal", calla);
        CHECK(calla(a), "el mute de la principal la calla en la principal");
        CHECK(suena(rmsHasta(m, "b", suena)), "y la B sigue sonando");
        const json e = m.estado();
        CHECK(e["entradas"]["tono"].value("mute", false), "el estado de siempre: el mute de la por omisión");
        CHECK(!e["salidas"]["b"]["mandos"]["tono"].value("mute", true), "la B, sin mute");
    }
    m.ponerMute("tono", false);
    m.ponerNivel("tono", 1.0, "nadie");
    CHECK(fallo("ponerNivel"), "el fader de una salida que no existe falla");

    // Las pistas 2-6 son de cada salida, con su mando: la grabación de la B lleva las suyas.
    CHECK(m.ponerPistas("tono", {1, 2}).empty(), "el tono también a la pista 2");
    CHECK(m.productorDeBus("pista2") == "bus:pista2", "la pista 2 de la por omisión, con el nombre de siempre");
    CHECK(m.productorDeBus("pista2", &lat, "b") == "bus:pista2:b", "la de la B, con su id");
    CHECK(m.productorDeBus("pista2:b") == "bus:pista2:b", "la de la B por la clave");
    CHECK(m.productorDeBus("pista2:nadie").empty(), "la de una salida que no existe: nada");
    CHECK(suena(rmsPistaHasta(m, "principal", 2, suena)) && suena(rmsPistaHasta(m, "b", 2, suena)), "el tono en la pista 2 de las dos");
    m.ponerNivel("tono", 0.0, "b");
    {
        const double b = rmsPistaHasta(m, "b", 2, calla);
        std::printf("fader de la B a 0: pista 2 de la principal %.1f, de la B %.1f\n", rmsPistaHasta(m, "principal", 2, suena), b);
        CHECK(calla(b), "el fader de la B la calla en la pista 2 de la B");
        CHECK(suena(rmsPistaHasta(m, "principal", 2, suena)), "y no en la de la principal");
    }
    m.ponerNivel("tono", 1.0, "b");
    CHECK(m.ponerPistas("tono", {1}).empty(), "el tono solo al programa otra vez");

    // El tally del multiview: rojo si está al aire en alguna salida (la roja, solo en la B).
    m.alAire("Roja", "corte", 0, "b");
    CHECK(m.multiview(true, 640, 360).empty(), "el multiview");
    CHECK(m.verPedir("multiview", 640, 360).empty(), "verlo");
    {
        // La primera celda de abajo es «rojo» (en orden): su borde es el tally.
        const Px t = mirarHasta(m, "multiview", 1, 182, [](const Px& p) { return igual(p, 0xE0, 0x24, 0x2F); });
        std::printf("tally de «rojo» (al aire solo en la B): %s\n", txt(t).c_str());
        CHECK(igual(t, 0xE0, 0x24, 0x2F), "rojo: al aire en otra salida");
    }
    m.verSoltar("multiview");
    m.multiview(false);

    // Borrar la B andando: la principal sigue como estaba.
    CHECK(m.borrarSalida("b").empty(), "borrar la B");
    m.esperarCola();
    CHECK(!fallo("borrarSalida"), "se borró");
    {
        const json e = m.estado();
        CHECK(!e["salidas"].contains("b"), "ya no está");
        CHECK(e["salidas"].contains("principal"), "la principal sí");
    }
    CHECK(igual(mirarHasta(m, "programa", 160, 90, [](const Px& p) { return igual(p, 0, 255, 0); }), 0, 255, 0),
          "la principal sigue verde");
    CHECK(suena(rmsHasta(m, "principal", suena)), "y sonando");
    m.borrarSalida("principal");
    CHECK(fallo("borrarSalida"), "la por omisión no se borra");
    m.alAire("Roja", "corte", 0, "b");
    CHECK(fallo("alAire"), "al aire en una salida que no existe: falla");
    // Crearla de nuevo: arranca sin nada al aire y con lo mismo que la principal en la mezcla.
    CHECK(m.crearSalida("b", "").empty(), "crearla de nuevo");
    m.alAire("Roja", "corte", 0, "b");
    {
        const Px b = mirarHasta(m, "programa:b", 160, 90, [](const Px& p) { return igual(p, 255, 0, 0); });
        CHECK(igual(b, 255, 0, 0), "la vista «programa:b» sigue andando con la nueva");
        CHECK(calla(rmsHasta(m, "b", calla)), "la B nueva, con la roja: sin el tono");
        // estado() es la foto que publica la vuelta del motor: la salida nueva aparece a
        // la vuelta siguiente (pedirla antes daba null y value() tiraba).
        nlohmann::json fb;
        for (int i = 0; i < 50 && !(fb = m.estado()["salidas"].value("b", nlohmann::json())).is_object(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(fb.is_object() && fb.value("nombre", "") == "b", "sin nombre: su id");
        CHECK(igual(mirar(m, "programa", 160, 90), 0, 255, 0), "la principal, verde todavía");
    }
    // Las vistas que nadie mira (la política térmica, F4-05): sin leerla, se duerme (sigue
    // pedida, sin pipeline); al leerla se despierta sola y vuelve a dar imagen. Las que
    // tienen aviso no se tocan.
    {
        CHECK(m.verPedir("programa:b@sinmirar", 160, 90).empty(), "una vista para dejar de mirar");
        CHECK(igual(mirarHasta(m, "programa:b@sinmirar", 80, 45, [](const Px& p) { return igual(p, 255, 0, 0); }), 255, 0, 0),
              "anda");
        m.verAvisar("programa:b", [](const Cuadro&) {}); // con aviso: no se duerme
        m.vistasSinMirar(true, 300);
        const auto vista = [&m](const char* k) { return m.estado()["vistas"].value(k, json::object()); };
        json v;
        for (int i = 0; i < 40 && !(v = vista("programa:b@sinmirar")).value("dormida", false); ++i) esperar(50);
        CHECK(v.value("dormida", false) && !v.value("anda", true), "sin mirarla, se duerme");
        CHECK(!vista("programa:b").value("dormida", true), "la que tiene aviso sigue");
        Cuadro c;
        std::uint64_t s = 0;
        m.verUltimo("programa:b@sinmirar", c, s); // alguien la mira
        CHECK(igual(mirarHasta(m, "programa:b@sinmirar", 80, 45, [](const Px& p) { return igual(p, 255, 0, 0); }), 255, 0, 0),
              "mirarla la despierta");
        CHECK(!vista("programa:b@sinmirar").value("dormida", true), "despierta");
        m.vistasSinMirar(false);
        m.verAvisar("programa:b", nullptr);
        m.verSoltar("programa:b@sinmirar");
    }
    m.verSoltar("programa:b");
    m.verSoltar("programa");
    m.detener();
    std::printf("%s (%d fallos)\n", fallos == 0 ? "OK" : "FALLÓ", fallos);
    return fallos == 0 ? 0 : 1;
}
