// src/app/tests/test_tiempos.cpp — el grafo de demoras (tiempos::armar) con fotos de mentira
#include "tiempos/Tiempos.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using json = nlohmann::json;
using namespace ba::app::tiempos;

static int g_fallas = 0;
#define CHECK(c, m)                                   \
    do {                                              \
        if (!(c)) {                                   \
            std::printf("FALLA: %s\n", m);            \
            ++g_fallas;                               \
        }                                             \
    } while (0)

static const json* nodo(const json& g, const std::string& id) {
    for (const auto& n : g["nodos"])
        if (n["id"] == id) return &n;
    return nullptr;
}
static bool cerca(const json& v, double x) { return v.is_number() && std::fabs(v.get<double>() - x) < 0.05; }

int main() {
    Fotos f;
    f.motor = {
        {"estudio", {{"latencia_ms", 120.0}, {"tope_ms", 250.0}}},
        {"entradas",
         {{"cam1",
           {{"nombre", "Cámara 1"},
            {"video", true},
            {"audio", false},
            {"edad_video", {{"p50_ms", 60.0}, {"p95_ms", 80.0}}},
            {"latencia_ms", 90.0},
            {"sin_datos_s", 0.02},
            {"filtros",
             json::array({{{"id", "ptz"}, {"elemento", "baptz"}, {"audio", false}, {"ms", 2.0}},
                          {{"id", "sr"}, {"elemento", "basuperres"}, {"audio", false}, {"ms", 18.0}}})}}},
          {"mic",
           {{"nombre", "Mic 1"},
            {"video", false},
            {"audio", true},
            {"edad_audio", {{"p50_ms", 25.0}, {"p95_ms", 30.0}}},
            {"filtros", json::array()}}},
          {"tarde",
           {{"nombre", "Cámara lenta"}, {"video", true}, {"edad_video", {{"p50_ms", 200.0}, {"p95_ms", 300.0}}}, {"llega_tarde", true}}},
          {"muda", {{"nombre", "sin nada"}, {"video", false}, {"audio", false}}},
          {"corrida", {{"nombre", "Corrida"}, {"video", true}, {"edad_video", {{"p50_ms", 4.0e12}, {"p95_ms", 4.0e12}}}}},
          {"atrasada", {{"nombre", "Atrasada"}, {"video", true}, {"desfase_ms", 200}, {"edad_video", {{"p50_ms", -196.0}, {"p95_ms", -190.0}}}}}}},
        {"video", {{"al_aire", "Orador"}, {"programa", {{"latencia_salida_ms", 150.0}, {"capas", json::array({{{"entrada", "cam1"}}})}}}}},
        {"programa", {{"latencia_salida_ms", 150.0}}}};
    f.salidas = {{"principal", {{"andando", true}, {"latencia_ms", 450.0}, {"kbps", 6000}, {"perdidos", 0}}},
                 {"grabacion", {{"activa", false}}}};
    f.titulos = {{"sr", "Superres x2"}};
    f.racks["mic"] = {{"frecuenciaMuestreo", 48000},
                      {"nodos", json::array({{{"id", "n1"}, {"tipo", "plugin"}, {"etiqueta", "Compresor"}, {"modulo", "lsp.comp"},
                                              {"ms_medio", 0.5}, {"latencia_muestras", 480}},
                                             {{"id", "in"}, {"tipo", "in"}},
                                             {{"id", "t1"}, {"tipo", "texto"}, {"etiqueta", ""}}})}};

    const json g = armar(f);
    std::printf("%s\n", g.dump(1, ' ', false, json::error_handler_t::replace).c_str());

    const json* e = nodo(g, "entrada:cam1");
    CHECK(e && (*e)["fuente"] == "medido" && cerca((*e)["ms"], 40.0), "la entrada se queda con lo que no es de sus tarjetas (60 − 2 − 18)");
    CHECK(e && (*e)["estado"] == "bien", "80 ms contra 120 del estudio: bien");
    const json* sr = nodo(g, "tarjeta:cam1:sr");
    CHECK(sr && (*sr)["titulo"] == "Superres x2" && cerca((*sr)["ms"], 18.0) && cerca((*sr)["acumulado_ms"], 60.0),
          "la tarjeta: su título, lo que agrega y lo acumulado");
    const json* esc = nodo(g, "escena:programa");
    CHECK(esc && (*esc)["titulo"] == "Orador" && (*esc)["fuente"] == "declarado" && cerca((*esc)["acumulado_ms"], 150.0) &&
              cerca((*esc)["ms"], 90.0),
          "la escena: espera hasta su latencia (150) desde lo que traen sus entradas (60)");
    const json* mod = nodo(g, "modulo:mic:n1");
    CHECK(mod && (*mod)["titulo"] == "Compresor" && cerca((*mod)["ms"], 10.5), "el módulo: proceso + su latencia (480 muestras = 10 ms)");
    CHECK(nodo(g, "modulo:mic:in") == nullptr, "lo que no es un plugin del rack no es un paso");
    const json* vt = nodo(g, "modulo:mic:t1");
    CHECK(vt && (*vt)["tipo"] == "derivacion" && (*vt)["titulo"] == "Voz a texto" && (*vt)["fuente"] == "derivacion",
          "la voz a texto: una rama que escucha una copia");
    const json* ea = nodo(g, "entrada:mic:audio");
    CHECK(ea && cerca((*ea)["ms"], 14.5), "la entrada de audio: 25 − 10,5 del módulo");
    CHECK(e && (*e)["activo"] == true && sr && (*sr)["activo"] == true, "le llegan datos: activa (y sus tarjetas)");
    CHECK(nodo(g, "entrada:mic:audio") && (*nodo(g, "entrada:mic:audio"))["activo"] == false, "sin datos recientes: en reposo");
    const json* tarde = nodo(g, "entrada:tarde");
    CHECK(tarde && (*tarde)["estado"] == "tarde", "la que llega tarde, marcada");
    CHECK(nodo(g, "entrada:muda") == nullptr, "sin video ni audio, no se dibuja");
    const json* at = nodo(g, "entrada:atrasada");
    CHECK(at && cerca((*at)["ms"], 4.0) && cerca((*at)["acumulado_ms"], 4.0) && (*at)["detalle"]["desfase_ms"] == 200.0,
          "con desfase +200 (edad −196): tarda 4 ms de verdad");
    const json* co = nodo(g, "entrada:corrida");
    CHECK(co && (*co)["fuente"] == "hora_corrida" && (*co)["ms"].is_null() && (*co)["estado"] == "tarde",
          "una hora corrida 126 años: marcada, sin número");
    const json* tx = nodo(g, "salida:transmision");
    CHECK(tx && cerca((*tx)["acumulado_ms"], 450.0) && cerca((*tx)["ms"], 300.0), "la transmisión: su latencia es el total");
    CHECK(nodo(g, "salida:grabacion") == nullptr, "la grabación apagada no aparece");
    bool total = false;
    for (const auto& s : g["salidas"])
        if (s["id"] == "salida:transmision")
            total = cerca(s["total_ms"], 450.0) && s["camino"].size() >= 4 && s["camino"][0] == "entrada:cam1";
    CHECK(total, "la salida: total y el camino más lento, desde la cámara");
    bool tramo = false;
    for (const auto& t : g["tramos"]) tramo = tramo || (t["desde"] == "tarjeta:cam1:sr" && t["hacia"] == "escena:programa");
    CHECK(tramo, "de la última tarjeta a la escena");
    // Campos con otro tipo (gpu como texto, un número como texto, un bool como número): no se cae.
    Fotos raro = f;
    raro.motor["video"]["programa"]["gpu"] = "gl";
    raro.motor["video"]["programa"]["latencia_salida_ms"] = "150";
    raro.motor["entradas"]["cam1"]["video"] = 1;
    const json gr = armar(raro);
    const json* er = nodo(gr, "escena:programa");
    CHECK(er && cerca((*er)["acumulado_ms"], 150.0) && (*er)["detalle"]["gpu"] == "gl", "tipos cambiados: se lee igual");
    // Sin motor: un grafo vacío, sin caerse.
    const json vacio = armar(Fotos{});
    CHECK(vacio["nodos"].empty() && vacio["salidas"].empty(), "sin fotos: vacío");

    if (g_fallas == 0) std::printf("test_tiempos: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
