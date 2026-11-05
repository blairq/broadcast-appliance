// test_motor_falta_pieza.cpp — el motor con una pieza de GStreamer que no está: sigue y avisa
//
// En el paquete de Linux faltaba intersink (gst-plugins-rs): el bus PROGRAMA no se armaba,
// iniciar() volvía con el error y quien leía estado() hacía .value() sobre el "programa" null
// (nlohmann: type_error.306, terminate). Acá se saca intersink/intersrc del registro (como si
// no estuvieran: anda en cualquier máquina) y:
//   - iniciar() no frena: el motor arranca sin ese bus;
//   - el error llega a alFallar (la app lo muestra) y dice qué falta;
//   - estado() e instantanea() traen la parte que no está como objeto con «anda»: false y su
//     «error» (no null): leerlos como los lee la app no tira;
//   - una entrada que no se puede armar tampoco tira, y una orden que usa el bus devuelve su
//     error (por alFallar: va por la cola).

#include "Motor.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Saca la fábrica del registro: desde acá, gst_parse_launch dice «no element» y hayFabrica, false.
void sacar(const char* nombre) {
    if (GstElementFactory* f = gst_element_factory_find(nombre)) {
        gst_registry_remove_feature(gst_registry_get(), GST_PLUGIN_FEATURE(f));
        gst_object_unref(f);
    }
}

bool tieneError(const json& parte) {
    return parte.is_object() && !parte.value("anda", true) && !parte.value("error", std::string()).empty();
}

/// Lo que lee la app de la foto del motor (VistasYMedicion, Tiempos, los vúmetros): con .value()
/// encadenados, como ellos.
void leerComoLaApp(const json& e) {
    (void)e.value("programa", json::object()).value("rms_db", -120.0);
    (void)e.value("video", json::object()).value("programa", json::object()).value("capas", json::array());
    (void)e.value("video", json::object()).value("preview", json::object()).value("capas", json::array());
    // Con nombre antes de recorrerlos: un for sobre .items() de un temporal lo recorre ya destruido.
    const json salidas = e.value("salidas", json::object());
    for (const auto& [sid, s] : salidas.items()) {
        (void)s.value("programa", json::object()).value("pico_db", -120.0);
        (void)s.value("video", json::object()).value("programa", json::object()).value("anda", false);
    }
    const json entradas = e.value("entradas", json::object());
    for (const auto& [id, en] : entradas.items())
        (void)en.value("error", std::string());
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
    for (const char* e : {"audiotestsrc", "audiomixer", "videotestsrc"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    sacar("intersink");
    sacar("intersrc");
    CHECK(!hayFabrica("intersink") && !hayFabrica("intersrc"), "intersink e intersrc, fuera del registro");

    try {
        Motor::Config cfg;
        cfg.sinkEscucha = "fakesink sync=true"; // nada por los parlantes
        cfg.video = true;                      // también las composiciones (usan intersink)
        Motor motor(cfg);
        std::mutex m;
        std::vector<std::pair<std::string, std::string>> avisos;
        motor.alFallar([&](const std::string& que, const std::string& error) {
            std::lock_guard<std::mutex> l(m);
            avisos.emplace_back(que, error);
        });

        // Sin arrancar: la foto ya tiene la forma de siempre (nada en null).
        {
            const json e = motor.estado();
            CHECK(e["programa"].is_object() && !e["programa"].value("anda", true), "sin arrancar: el programa, «anda»: false (no null)");
            leerComoLaApp(e);
        }

        const std::string ini = motor.iniciar();
        std::printf("iniciar: «%s»\n", ini.c_str());
        CHECK(ini.empty(), "iniciar no frena por una pieza que falta");
        CHECK(motor.andando(), "el motor arrancó igual");

        const std::string ent = motor.agregarEntrada("tono", {{"pipeline_audio", "audiotestsrc is-live=true volume=0"}}, {{"global", true}});
        std::printf("agregarEntrada: «%s»\n", ent.c_str());
        const std::string esc = motor.escuchar("programa");
        std::printf("escuchar programa: «%s» (va por la cola: el error, a alFallar)\n", esc.c_str());
        motor.esperarCola();
        esperar(500);

        const json e = motor.estado();
        std::printf("programa: %s\nvideo.programa: %s\n", e["programa"].dump().c_str(), e["video"]["programa"].dump().c_str());
        CHECK(tieneError(e["programa"]), "estado: el programa sin bus, con «anda»: false y el motivo");
        CHECK(e["programa"].value("error", std::string()).find("intersink") != std::string::npos, "el motivo dice qué falta (intersink)");
        CHECK(e["salidas"].is_object() && !e["salidas"].empty(), "estado: las salidas");
        for (const auto& [sid, s] : e["salidas"].items()) CHECK(tieneError(s["programa"]), "la salida, con su bus que no está y el motivo");
        CHECK(e["video"]["programa"].is_object() && e["video"]["preview"].is_object(), "el video del programa y del preview: objetos");
        // Sin el bus de audio el video se arma igual (acá también falla, por intersink: con su motivo).
        CHECK(e["video"]["programa"].contains("capas"), "el video del programa se armó aunque el bus no");
        CHECK(e["video"]["programa"].value("error", std::string()).find("intersink") != std::string::npos,
              "y su motivo también dice qué falta");
        leerComoLaApp(e);
        CHECK(e["entradas"].contains("tono"), "la entrada quedó (aunque no pueda llegar al programa)");

        // La foto que lee la interfaz (la arma el hilo del motor).
        std::shared_ptr<const json> foto;
        for (int i = 0; i < 50 && !foto; ++i, esperar(20)) foto = motor.instantanea();
        CHECK(foto != nullptr, "la foto del motor");
        if (foto) leerComoLaApp(*foto);

        {
            std::lock_guard<std::mutex> l(m);
            bool bus = false, escucha = !esc.empty();
            for (const auto& [que, error] : avisos) {
                std::printf("aviso: %s: %s\n", que.c_str(), error.c_str());
                bus = bus || (que == "iniciar" && error.find("PROGRAMA") != std::string::npos);
                escucha = escucha || que == "escuchar";
            }
            CHECK(escucha, "escuchar el programa sin su bus devuelve el error (por alFallar)");
            CHECK(bus, "el bus que no arrancó llega a alFallar (la app lo muestra)");
        }
        motor.detener();
    } catch (const std::exception& ex) {
        std::printf("FALLO: excepción: %s\n", ex.what());
        ++fallos;
    }
    if (fallos == 0) std::printf("test_motor_falta_pieza: OK\n");
    return fallos == 0 ? 0 : 1;
}
