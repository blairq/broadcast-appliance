// test_salidas.cpp — muchas salidas del lado del gateway (docs/planes/PLAN-SALIDAS.md, etapa 6)
//
// Lo que baja cada ruta según su salida (MediaRoute::miraSalida / oyeSalida), qué
// salida baja de verdad según la lista de la app (SalidasConocidas, Salidas.hpp), y
// que MediaPump levanta los hilos de bajada de una salida solo mientras alguien la
// mira: con una sola salida no hay ninguno y todo baja de BA.Program como siempre.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Log.hpp"
#include "MediaPump.hpp"
#include "MediaRoute.hpp"
#include "Salidas.hpp"
#include "Webrtc.hpp"
#include "ipc/CanalesSalida.hpp"

using namespace ba::gateway;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

void testRutas() {
    CHECK(MediaRoute::program(1).miraSalida("") && MediaRoute::backstage(0).miraSalida(""),
          "sin salida, el invitado y la fila miran la por omisión (como siempre)");
    MediaRoute p = MediaRoute::program(2, true);
    p.salida = "b";
    CHECK(p.miraSalida("b") && !p.miraSalida("") && p.oyeSalida("b") && !p.oyeSalida(""),
          "un invitado con la salida «b» ve y oye la «b», no el programa");
    CHECK(MediaRoute::deSalida("b").miraSalida("b") && MediaRoute::deSalida("b").kind == MediaRoute::Kind::Backstage &&
              MediaRoute::deSalida("b").index == 0,
          "deSalida: la fila sin preview (no sube nada) mirando esa salida");
    CHECK(MediaRoute::deSalida("") == MediaRoute::backstage(0), "deSalida(\"\") es la pantalla de siempre");
    CHECK(MediaRoute::escena(1).oyeSalida("") && !MediaRoute::escena(1).miraSalida("") && !MediaRoute::escena(1).oyeSalida("b"),
          "una pantalla de escena oye el retorno de la por omisión y no mira ninguna salida");
    CHECK(!MediaRoute::lobby(1, true).oyeSalida("") && !MediaRoute::remoto(1).miraSalida(""),
          "el lobby y el remoto no reciben ninguna salida");
    CHECK(MediaRoute::deSalida("a") != MediaRoute::deSalida("b"), "la salida cuenta para comparar rutas");
}

void testConocidas() {
    const json lista = salidas::sanear(json::parse(R"([
        {"id": "principal", "nombre": "Principal", "color": "#e53935"},
        {"id": "b", "nombre": "Segunda", "porOmision": true},
        {"id": "b", "nombre": "repetida"},
        {"id": "no vale/..", "nombre": "x"},
        {"id": "c", "porOmision": true}
    ])"));
    CHECK(lista.size() == 3, "se descartan los ids repetidos y los que no sirven para un canal");
    CHECK(lista[1]["porOmision"] == true && lista[0]["porOmision"] == false && lista[2]["porOmision"] == false,
          "una sola por omisión: la marcada primero");
    CHECK(lista[2]["nombre"] == "c", "sin nombre, el id");
    CHECK(salidas::sanear(json::parse(R"([{"id":"x"},{"id":"y"}])"))[0]["porOmision"] == true,
          "si ninguna es la por omisión, la primera");
    CHECK(salidas::sanear(json::object()).empty(), "algo que no es una lista: ninguna");

    const SalidasConocidas c = salidas::conocidas(lista);
    CHECK(c.porOmision == "b" && c.ids.size() == 3, "las conocidas: ids y la por omisión");
    CHECK(c.vigente("principal") == "principal", "una que existe y no es la por omisión baja por su canal");
    CHECK(c.vigente("b").empty(), "la por omisión baja de BA.Program");
    CHECK(c.vigente("borrada").empty() && c.vigente("").empty(), "una que no existe, o ninguna: BA.Program");
    CHECK(SalidasConocidas{}.vigente("principal").empty(), "sin lista de la app, todo es BA.Program");

    CHECK(ba::ipc::nombreCanalSalida("b") == L"BA.Salida.b" && ba::ipc::nombreAudioSalida("b") == L"BA.Audio.Salida.b",
          "los nombres del contrato con la app");
    CHECK(!ba::ipc::idDeSalidaValido("") && !ba::ipc::idDeSalidaValido(std::string(33, 'a')) &&
              ba::ipc::idDeSalidaValido("Salida_2-b"),
          "ids: ASCII, número, - y _, hasta 32");
}

bool vivas(const MediaPump& pump, std::vector<std::string> esperadas) {
    auto v = pump.bajadasVivas();
    std::sort(v.begin(), v.end());
    std::sort(esperadas.begin(), esperadas.end());
    return v == esperadas;
}

void testBajadasBajoDemanda() {
    WebrtcHub hub(4, 320, 180, 48000, 2);
    MediaPump::Cfg cfg; // sin canales de subida: acá solo importa la bajada
    Log log;
    MediaPump pump(hub, cfg, log);
    pump.start();

    // Una sola salida (o sin lista): ni un hilo de más, aunque una ruta pida otra.
    pump.setRoute(1, MediaRoute::program(1));
    pump.setRoute(2, MediaRoute::deSalida("b"));
    CHECK(vivas(pump, {}), "sin lista de salidas no se levanta ninguna bajada");
    CHECK(pump.miranPrograma() == 2, "y los dos miran el programa");
    CHECK(pump.route(2).salida.empty(), "la ruta vigente baja de BA.Program");

    // La app avisa que hay dos: la pantalla que pidió la «b» pasa sola a la «b».
    pump.ponerSalidas(salidas::conocidas(salidas::sanear(json::parse(R"([{"id":"a","porOmision":true},{"id":"b"}])"))));
    CHECK(vivas(pump, {"b"}), "al aparecer la «b» se levanta su bajada");
    CHECK(pump.route(2).salida == "b" && pump.miranPrograma() == 1, "la pantalla mira la «b»; al programa, uno");
    CHECK(pump.miranSalidas() == (std::map<std::string, int>{{"b", 1}}), "miranSalidas: {b: 1}");

    MediaRoute r = MediaRoute::program(1);
    r.salida = "a"; // la por omisión por su nombre
    pump.setRoute(1, r);
    CHECK(vivas(pump, {"b"}) && pump.miranPrograma() == 1, "pedir la por omisión por su id es BA.Program");

    pump.setRoute(3, MediaRoute::deSalida("b", true));
    CHECK(vivas(pump, {"b"}) && pump.miranSalidas().at("b") == 2, "dos miran la «b»: una sola bajada");
    pump.setRoute(2, MediaRoute{});
    CHECK(vivas(pump, {"b"}), "mientras quede uno, sigue");
    pump.setRoute(3, MediaRoute::escena(0));
    CHECK(vivas(pump, {}), "sin nadie que la mire, se suelta");
    CHECK(pump.miranSalidas().empty(), "y no figura en miranSalidas");

    pump.setRoute(2, MediaRoute::deSalida("b"));
    CHECK(vivas(pump, {"b"}), "vuelve a levantarse si alguien la pide");
    pump.ponerSalidas(salidas::conocidas(salidas::sanear(json::parse(R"([{"id":"a"}])"))));
    CHECK(vivas(pump, {}) && pump.route(2).salida.empty() && pump.miranPrograma() == 2,
          "si la app borra la «b», quien la miraba ve el programa");

    pump.ponerSalidas(salidas::conocidas(salidas::sanear(json::parse(R"([{"id":"a"},{"id":"b"}])"))));
    CHECK(vivas(pump, {"b"}), "y vuelve a la suya cuando reaparece (la ruta pedida se guarda)");
    pump.stop();
    CHECK(vivas(pump, {}), "stop suelta todas");
}
} // namespace

int main() {
    testRutas();
    testConocidas();
    testBajadasBajoDemanda();
    std::printf(g_fallas == 0 ? "== test_salidas: OK ==\n" : "== test_salidas: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
