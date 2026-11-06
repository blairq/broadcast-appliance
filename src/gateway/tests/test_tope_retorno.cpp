// ============================================================================
// src/gateway/tests/test_tope_retorno.cpp — la resolución máxima del retorno (TopeRetorno.hpp)
//
//   1. El ajuste (`retorno.tope`) y los escalones del automático.
//   2. El tope aplicado a las calidades del retorno compartido (capasConTope): «480p» codifica
//      480p, «1080p» agrega su capa, el tope de la sala rige para todos (capasRetornoVigentes).
//   3. El regulador: baja de a un escalón cuando no llega (sostenido), no por un pozo de un
//      instante; con la máquina holgada un retorno lento es la red de alguien (no se toca); el
//      codificador que descarta también baja; sube con margen sostenido; la histéresis; con la
//      misma gente espera para volver a probar y la espera se duplica si recae; fijo manda.
//   4. Lo que va a la sala: el regulador cuenta sus cambios (cambios()) y solo ESOS se esperan;
//      la medida de cada segundo va a la sala sin esperar su candado (Room::setRetornoTope con
//      esperar = false): con la sala libre entra y no publica nada; un cambio publica «room».
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "RetornoCompartido.hpp"
#include "Room.hpp"
#include "TopeRetorno.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

MedidaRetorno medida(double cps, double debido, double cpu, double descartes, int oyentes) {
    MedidaRetorno m;
    m.cpsLogrado = cps;
    m.cpsDebido = debido;
    m.cpuMaquina = cpu;
    m.cpuGateway = cpu / 2;
    m.descartesPct = descartes;
    m.oyentes = oyentes;
    return m;
}

const MedidaRetorno kNoLlega = medida(6, 30, 92, 0, 200);   // la corrida por cable del 2026-11-05
const MedidaRetorno kSobra = medida(29.5, 30, 50, 0, 200);
const MedidaRetorno kRed = medida(9, 30, 50, 0, 200);       // la red de cada uno, no la máquina
const MedidaRetorno kMedio = medida(29.5, 30, 78, 0, 200);  // entre los dos umbrales

/// Pasa `ms` de a un segundo con la misma medida; devuelve en qué segundo cambió (o -1).
std::int64_t correr(ReguladorRetorno& r, const MedidaRetorno& m, std::int64_t& t, std::int64_t ms) {
    std::int64_t cambio = -1;
    for (std::int64_t fin = t + ms; t < fin;) {
        t += 1000;
        if (r.medir(m, t) && cambio < 0) cambio = t;
    }
    return cambio;
}

void ajuste() {
    CHECK(altoDeAjusteTope("auto") == 0, "auto");
    CHECK(altoDeAjusteTope("360p") == 360 && altoDeAjusteTope("480p") == 480, "360p y 480p");
    CHECK(altoDeAjusteTope("720p") == 720 && altoDeAjusteTope("1080p") == 1080, "720p y 1080p");
    CHECK(altoDeAjusteTope("cualquiera") == 0, "lo que no se entiende: automático");
    CHECK((escalonesDesde(720) == std::vector<int>{720, 480, 360}), "de fábrica: 720 → 480 → 360");
    CHECK((escalonesDesde(1080) == std::vector<int>{1080, 720, 480, 360}), "con capa de 1080");
    CHECK((escalonesDesde(540) == std::vector<int>{540, 480, 360}), "una capa alta rara va primero");
}

void capas() {
    const auto todas = leerCapasRetorno("720:1500,360:600,180:200");
    CHECK(capasConTope(todas, 0) == todas, "sin tope: las mismas");
    CHECK(capasConTope(todas, 720) == todas, "720: las mismas");
    CHECK((capasConTope(todas, 480) == std::vector<CapaRetorno>{{480, 900}, {360, 600}, {180, 200}}),
          "480: una capa de 480 (bitrate escalado de la de 360) en lugar de la de 720");
    CHECK((capasConTope(todas, 360) == std::vector<CapaRetorno>{{360, 600}, {180, 200}}),
          "360: dos calidades, dos codificaciones");
    CHECK((capasConTope(todas, 1080) == std::vector<CapaRetorno>{{1080, 2750}, {720, 1500}, {360, 600}, {180, 200}}),
          "1080: se agrega la capa de 1080");
    CHECK(capasPara(720, capasConTope(todas, 1080)).size() == 3, "1080 con una fuente de 720: no se agranda");
    CHECK(indiceTope(capasConTope(todas, 480), 360) == 1, "el tope del operador sigue mandando dentro del de la sala");

    // El tope de la sala rige para todos los que piden las calidades vigentes.
    CHECK(topeSala() == 0, "de entrada, sin tope");
    ponerTopeSala(480);
    CHECK(topeSala() == 480 && capasRetornoVigentes().front().alto == 480, "con el tope en 480");
    ponerTopeSala(0);
    CHECK(capasRetornoVigentes().front().alto == 720, "sin tope: la de fábrica");
}

void bajar() {
    ReguladorRetorno r;
    r.ajustes(AjustesTope{});
    std::int64_t t = 0;
    CHECK(r.medir(kSobra, t += 1000), "la primera vuelta se anota");
    CHECK(r.tope() == 720 && r.automatico() && r.causa() == ReguladorRetorno::Causa::Inicio, "arranca arriba");

    // Un pozo de 2 s no alcanza.
    CHECK(correr(r, kNoLlega, t, 2000) < 0, "2 s sin llegar: nada");
    correr(r, kSobra, t, 3000);
    CHECK(r.tope() == 720, "sigue en 720");

    // Sostenido: baja de a un escalón, con la espera entre cambios.
    const std::int64_t desde = t;
    const std::int64_t c1 = correr(r, kNoLlega, t, 7000);
    CHECK(c1 > 0 && r.tope() == 480 && r.causa() == ReguladorRetorno::Causa::Cps, "baja a 480 por los cps");
    CHECK(c1 - desde >= 3000, "después de sostenerse");
    CHECK(r.motivo().find("480") != std::string::npos, "el motivo dice a dónde");
    CHECK(r.medidaDelCambio().cpsLogrado < 10 && r.medidaDelCambio().oyentes == 200, "la medida que lo decidió");
    const std::int64_t c2 = correr(r, kNoLlega, t, 8000);
    CHECK(c2 > 0 && r.tope() == 360, "y a 360");
    CHECK(c2 - c1 >= 5000, "con la espera entre cambios");
    CHECK(correr(r, kNoLlega, t, 20000) < 0 && r.tope() == 360, "360 es el piso del automático");

    // La red de cada uno (máquina holgada): ni baja ni sube.
    ReguladorRetorno red;
    red.ajustes(AjustesTope{});
    std::int64_t u = 0;
    red.medir(kSobra, u += 1000);
    CHECK(correr(red, kRed, u, 60000) < 0 && red.tope() == 720, "retorno lento con la máquina holgada: no se toca");

    // El codificador que descarta baja aunque la máquina no esté al tope.
    ReguladorRetorno cod;
    cod.ajustes(AjustesTope{});
    std::int64_t v = 0;
    cod.medir(kSobra, v += 1000);
    CHECK(correr(cod, medida(28, 30, 60, 25, 100), v, 5000) > 0 && cod.tope() == 480 &&
              cod.causa() == ReguladorRetorno::Causa::Codificador,
          "el codificador descarta: baja");
}

void subir() {
    ReguladorRetorno r;
    r.ajustes(AjustesTope{});
    std::int64_t t = 0;
    r.medir(kSobra, t += 1000);
    correr(r, kNoLlega, t, 12000);
    CHECK(r.tope() == 360, "abajo con 200");

    // Histéresis: entre los umbrales se queda.
    CHECK(correr(r, kMedio, t, 60000) < 0 && r.tope() == 360, "entre umbrales: quieto");

    // Con menos gente (menos del 80 %), sube apenas sobra margen sostenido (20 s), de a uno.
    const MedidaRetorno pocos = medida(29.5, 30, 50, 0, 100);
    const std::int64_t desde = t;
    const std::int64_t c = correr(r, pocos, t, 25000);
    CHECK(c > 0 && r.tope() == 480 && r.causa() == ReguladorRetorno::Causa::Margen, "sube a 480 con margen");
    CHECK(c - desde >= 20000, "subir es prudente");

    // Con la misma gente: espera reintentarMs desde que bajó.
    ReguladorRetorno m;
    m.ajustes(AjustesTope{});
    std::int64_t u = 0;
    m.medir(kSobra, u += 1000);
    const std::int64_t bajo = correr(m, kNoLlega, u, 7000);
    CHECK(m.tope() == 480, "bajó con 200");
    const std::int64_t c2 = correr(m, kSobra, u, 61000);
    CHECK(c2 > 0 && m.tope() == 720, "volvió a probar");
    CHECK(c2 - bajo >= 60000, "con la misma gente, recién pasado el minuto");

    // Recae enseguida: la próxima vez espera el doble.
    const std::int64_t c3 = correr(m, kNoLlega, u, 8000);
    CHECK(c3 > 0 && m.tope() == 480, "recayó");
    const std::int64_t c4 = correr(m, kSobra, u, 200000);
    CHECK(c4 > 0 && c4 - c3 >= 120000, "la espera se duplicó");
}

void fijo() {
    ReguladorRetorno r;
    AjustesTope a;
    a.fijo = 360;
    r.ajustes(a);
    std::int64_t t = 0;
    CHECK(r.medir(kSobra, t += 1000) && r.tope() == 360 && !r.automatico(), "fijo en 360");
    CHECK(r.causa() == ReguladorRetorno::Causa::Fijo, "causa fijo");
    CHECK(correr(r, kSobra, t, 120000) < 0 && r.tope() == 360, "con margen no sube");
    a.fijo = 1080;
    r.ajustes(a);
    CHECK(r.medir(kNoLlega, t += 1000) && r.tope() == 1080, "otro fijo, en el acto");
    CHECK(correr(r, kNoLlega, t, 60000) < 0 && r.tope() == 1080, "sin llegar no baja: es fijo");
    a.fijo = 0;
    r.ajustes(a);
    CHECK(r.medir(kSobra, t += 1000) && r.tope() == 720 && r.automatico(), "de vuelta a automático, desde arriba");

    const nlohmann::json e = r.estado(true);
    CHECK(e.value("modo", "") == "auto" && e.value("alto", 0) == 720, "estado: modo y alto");
    CHECK(e.contains("medida") && e.contains("decision") && e.value("causa", "") == "inicio", "estado: medida y causa");
    CHECK(e["decisiones"].size() == 3, "estado: las decisiones (360, 1080, auto)");
    CHECK(!r.estado(false).contains("decisiones"), "sin historial en la sala");
}

void sala() {
    // El contador de cambios: uno por decisión, nada por la medida de cada segundo.
    ReguladorRetorno r;
    r.ajustes(AjustesTope{});
    std::int64_t t = 0;
    CHECK(r.cambios() == 0, "sin medir, sin cambios");
    CHECK(r.medir(kMedio, t += 1000) && r.cambios() == 1, "la primera vuelta es un cambio (inicio)");
    CHECK(correr(r, kMedio, t, 60000) < 0 && r.cambios() == 1, "la medida de cada segundo no es un cambio");
    CHECK(correr(r, kNoLlega, t, 6000) > 0 && r.cambios() == 2 && r.tope() == 480, "bajar es un cambio");

    // La sala: con esperar = false y la sala libre, entra; sin cambio de tope no publica.
    Room room(Room::Cfg{});
    int fotos = 0;
    Room::Sinks s;
    s.toHosts = [&](const std::string& ev, const nlohmann::json&) {
        if (ev == "room") ++fotos;
    };
    room.setSinks(s);
    nlohmann::json e = r.estado(false);
    CHECK(room.setRetornoTope(e, true) && fotos == 1, "un cambio: espera y publica");
    e["medida"]["cpu_maquina"] = 42.0;
    CHECK(room.setRetornoTope(e, false), "la medida, con la sala libre, entra sin esperar");
    CHECK(fotos == 1, "la medida sola no publica");
    CHECK(room.snapshot()["retornoTope"]["medida"].value("cpu_maquina", 0.0) == 42.0, "la foto lleva la medida nueva");
    CHECK(correr(r, kNoLlega, t, 6000) > 0 && r.tope() == 360 && r.cambios() == 3, "baja otra vez");
    CHECK(room.setRetornoTope(r.estado(false), true) && fotos == 2, "el cambio publica");
}

} // namespace

int main() {
    ajuste();
    capas();
    bajar();
    subir();
    fijo();
    sala();
    if (g_fallos == 0) std::printf("test_tope_retorno: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
