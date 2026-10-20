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

    f.enEspera["tarde"] = "el lugar 3 de la sala está vacío (llega la placa)";
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
    CHECK(nodo(g, "entrada:tarde") && (*nodo(g, "entrada:tarde"))["activo"] == false &&
              (*nodo(g, "entrada:tarde"))["detalle"].contains("en_espera"),
          "un lugar de la sala vacío: en reposo aunque llegue la placa, con el motivo");
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

    // El análisis por elemento (las sondas): lo medido manda sobre lo que informa la tarjeta,
    // con la memoria y el cruce; la entrada guarda su recorrido.
    {
        Fotos f2 = f;
        f2.motor["entradas"]["cam1"]["filtros"][1]["instancia"] = "ba_f_sr";
        f2.pipelines["cam1"] = {
            {"elementos",
             json::array({{{"nombre", "src"}, {"agrega_ms", 1.0}, {"memoria_salida", "sistema"}},
                          {{"nombre", "gldownloadelement3"}, {"fabrica", "gldownload"}, {"agrega_ms", 0.5},
                           {"memoria_entrada", "gl"}, {"memoria_salida", "sistema"}, {"cruce", "baja"}},
                          {{"nombre", "capsfilter9"}, {"fabrica", "capsfilter"}, {"agrega_ms", 0.0}},
                          {{"nombre", "ba_f_sr"}, {"agrega_ms", 30.0}, {"agrega_p95_ms", 35.0}, {"memoria_entrada", "sistema"},
                           {"memoria_salida", "gl"}, {"cruce", "sube"}},
                          {{"nombre", "ba_f_sr_baja"}, {"agrega_ms", 5.0}, {"memoria_entrada", "gl"}, {"memoria_salida", "sistema"},
                           {"cruce", "baja"}}})},
            {"cruces", json::array({{{"elemento", "ba_f_sr"}, {"que", "sube"}}, {{"elemento", "ba_f_sr_baja"}, {"que", "baja"}}})}};
        const json g2 = armar(f2);
        const json* sr2 = nodo(g2, "tarjeta:cam1:sr");
        CHECK(sr2 && cerca((*sr2)["ms"], 35.5) && (*sr2)["detalle"].value("medido_sondas", false) &&
                  cerca((*sr2)["detalle"]["ms_por_cuadro"], 18.0) && (*sr2)["detalle"]["elementos"].size() == 3,
              "la tarjeta medida por las sondas: el gldownload que pide, sus dos elementos (0,5 + 30 + 5), no los 18 que informa");
        CHECK(sr2 && (*sr2)["detalle"]["memoria_entrada"] == "gl" && (*sr2)["detalle"]["memoria_salida"] == "sistema" &&
                  (*sr2)["detalle"]["cruces"].size() == 3 && (*sr2)["detalle"]["cruce"] == "baja",
              "la memoria con que entra y sale, y sus cruces");
        const json* ptz2 = nodo(g2, "tarjeta:cam1:ptz");
        CHECK(ptz2 && cerca((*ptz2)["ms"], 2.0) && !(*ptz2)["detalle"].contains("medido_sondas"),
              "la tarjeta sin instancia sigue con lo que informa");
        const json* e2 = nodo(g2, "entrada:cam1");
        CHECK(e2 && (*e2)["detalle"]["pipeline"]["cruces"].size() == 2, "la entrada guarda su recorrido");
        CHECK(e2 && (*e2)["detalle"]["diagnostico"]["hallazgos"].is_array(), "y su diagnóstico");
    }

    // Lo medido después de la entrada (MedidorEdad): la escena, la mezcla y la salida dejan de
    // mostrar lo fijado.
    {
        Fotos f3 = f;
        f3.motor["video"]["programa"]["edad_salida"] = {{"p50_ms", 200.0}, {"p95_ms", 230.0}};
        f3.salidas["principal"]["edad_salida"] = {{"p50_ms", 520.0}, {"p95_ms", 560.0}};
        const json g3 = armar(f3);
        const json* esc3 = nodo(g3, "escena:programa");
        CHECK(esc3 && (*esc3)["fuente"] == "medido" && cerca((*esc3)["acumulado_ms"], 200.0) && cerca((*esc3)["ms"], 140.0),
              "la escena medida: sale con 200 ms; espera 140 sobre los 60 que trae la cámara");
        CHECK(esc3 && cerca((*esc3)["detalle"]["contra_lo_fijado_ms"], 50.0) && (*esc3)["estado"] == "justo",
              "50 ms más tarde que los 150 fijados (más de un cuadro): justo, con el aviso");
        const json* tx3 = nodo(g3, "salida:transmision");
        CHECK(tx3 && (*tx3)["fuente"] == "medido" && cerca((*tx3)["acumulado_ms"], 520.0) && cerca((*tx3)["detalle"]["latencia_fijada_ms"], 450.0),
              "la transmisión medida (520) contra la fijada (450)");
    }

    // La llegada de cada entrada a la composición (medida en su intersrc): la espera del colchón
    // va en el tramo y la escena queda con lo que agrega el compositor.
    {
        Fotos f4 = f;
        f4.motor["video"]["programa"]["capas"] = json::array({{{"entrada", "cam1"}, {"edad_llega", {{"p50_ms", 160.0}}}}});
        f4.motor["video"]["programa"]["edad_salida"] = {{"p50_ms", 195.0}, {"p95_ms", 205.0}};
        const json g4 = armar(f4);
        bool tramoOk = false;
        for (const auto& t : g4["tramos"])
            if (t["desde"] == "tarjeta:cam1:sr" && t["hacia"] == "escena:programa")
                tramoOk = cerca(t["espera_ms"], 100.0) && cerca(t["llega_ms"], 160.0);
        CHECK(tramoOk, "el tramo de la cámara a la escena: sale con 60, llega con 160, esperó 100 (el colchón)");
        const json* esc4 = nodo(g4, "escena:programa");
        CHECK(esc4 && cerca((*esc4)["ms"], 35.0) && (*esc4)["estado"] == "bien" && !(*esc4)["detalle"].contains("aviso"),
              "la escena: el compositor agrega 35 (un cuadro): bien, sin aviso aunque lo fijado sea menos");
    }

    // El diagnóstico: lo que un asistente (o el operador) necesita leer del recorrido.
    {
        const json pl = {
            {"elementos",
             json::array({
                 {{"nombre", "src"}, {"fabrica", "v4l2src"}, {"caps_salida", "video/x-raw NV12 1920x1080 30 fps"}, {"memoria_salida", "sistema"}},
                 {{"nombre", "videoconvert0"}, {"fabrica", "videoconvert"}, {"agrega_ms", 6.0}, {"memoria_entrada", "sistema"},
                  {"memoria_salida", "sistema"}, {"caps_entrada", "video/x-raw NV12 1920x1080 30 fps"},
                  {"caps_salida", "video/x-raw RGBA 1920x1080 30 fps"}},
                 {{"nombre", "gluploadelement0"}, {"fabrica", "glupload"}, {"agrega_ms", 1.0}, {"memoria_entrada", "sistema"},
                  {"memoria_salida", "gl"}, {"cruce", "sube"}},
                 {{"nombre", "ba_f_ptz"}, {"fabrica", "baptz"}, {"agrega_ms", 0.5}, {"memoria_entrada", "gl"}, {"memoria_salida", "gl"},
                  {"puede_entrada", {"gl"}}},
                 {{"nombre", "gldownloadelement0"}, {"fabrica", "gldownload"}, {"agrega_ms", 2.0}, {"memoria_entrada", "gl"},
                  {"memoria_salida", "sistema"}, {"cruce", "baja"}},
                 {{"nombre", "ba_f_disolver"}, {"fabrica", "bamovimiento"}, {"agrega_ms", 800.0}, {"declara_ms", 0.0},
                  {"memoria_entrada", "sistema"}, {"memoria_salida", "sistema"}, {"puede_entrada", {"sistema"}}},
                 {{"nombre", "gluploadelement1"}, {"fabrica", "glupload"}, {"agrega_ms", 1.5}, {"memoria_entrada", "sistema"},
                  {"memoria_salida", "gl"}, {"cruce", "sube"}},
             })}};
        const json d = diagnosticar(pl, {{"ba_f_ptz", {"tarjeta:cam:ptz", "PTZ digital"}},
                                         {"ba_f_disolver", {"tarjeta:cam:disolver", "Disolución por movimiento"}}});
        std::printf("%s\n", d.dump(1, ' ', false, json::error_handler_t::replace).c_str());
        const auto de = [&](const std::string& tipo) -> const json* {
            for (const auto& h : d["hallazgos"])
                if (h["tipo"] == tipo) return &h;
            return nullptr;
        };
        CHECK(d["hallazgos"].size() > 0 && d["hallazgos"][0]["tipo"] == "retiene" && d["hallazgos"][0]["gravedad"] == "problema",
              "lo primero: la disolución que retiene 800 ms (≈24 cuadros)");
        const json* iv = de("ida_y_vuelta");
        CHECK(iv && cerca((*iv)["ms"], 3.5) && (*iv)["tarjetas"].size() == 1 && (*iv)["tarjetas"][0]["titulo"] == "Disolución por movimiento" &&
                  (*iv)["texto"].get<std::string>().find("solo aceptan memoria del sistema") != std::string::npos,
              "la ida y vuelta: la culpa es de la disolución (solo RAM), 2 + 1,5 ms de copias");
        const json* dm = de("declara_menos");
        CHECK(dm && cerca((*dm)["ms"], 800.0), "declara 0 y tarda 800: el estudio no lo compensa");
        const json* cv = de("conversion_cpu");
        CHECK(cv && (*cv)["gravedad"] == "aviso" && (*cv)["texto"].get<std::string>().find("NV12") != std::string::npos,
              "videoconvert NV12 → RGBA en la CPU, 6 ms");
        CHECK(d["resumen"]["idas_y_vueltas"] == 1 && d["resumen"]["cruces"] == 3 && cerca(d["resumen"]["cuadro_ms"], 33.3),
              "el resumen: 3 cruces, 1 ida y vuelta, cuadros de 33 ms");
    }
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

    // La cadena de la placa: el traspaso que espera y el fallbackswitch, un solo hallazgo con la suma.
    {
        const json pl = {{"elementos", json::array({
            {{"nombre", "videotestsrc1"}, {"fabrica", "videotestsrc"}, {"fuente", true}, {"agrega_ms", 4.0}, {"caps_salida", "video/x-raw I420 320x180 30 fps"}},
            {{"nombre", "salida/intersrc8"}, {"fabrica", "intersrc"}, {"traspaso", true}, {"agrega_ms", 66.0}},
            {{"nombre", "salida/fs"}, {"fabrica", "fallbackswitch"}, {"agrega_ms", 33.0}, {"declara_ms", 33.0}},
        })}};
        const json d = diagnosticar(pl, {});
        const json* h = nullptr;
        for (const auto& x : d["hallazgos"])
            if (x["tipo"] == "colchon_placa") h = &x;
        CHECK(h && cerca((*h)["ms"], 99.0) && (*h)["gravedad"] == "aviso", "el colchón de la placa: 66 + 33 = 99 ms, un solo hallazgo");
        bool retiene = false;
        for (const auto& x : d["hallazgos"]) retiene = retiene || x["tipo"] == "retiene";
        CHECK(!retiene, "el traspaso y el fallbackswitch no salen además como «retiene»");
        CHECK(cerca(d["resumen"]["total_ms"], 103.0), "el total: 4 de captura + 99 del colchón");
    }
    if (g_fallas == 0) std::printf("test_tiempos: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
