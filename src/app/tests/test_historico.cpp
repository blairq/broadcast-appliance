// ============================================================================
// src/app/tests/test_historico.cpp — leer y mantener el histórico (evento/Historico)
//
//   1. Traer lo del día: las grabaciones (y las de entradas/) y transcripciones
//      del día van al evento; las de otro día no; dos veces no repite; sin
//      mover quedan donde están, con mover pasan a la carpeta del evento.
//   2. El inicio de una grabación sale de su nombre (ba-<segundos>-00000.mkv).
//   3. Retención: simular no borra; borrar saca Grabaciones/ y anota
//      `borrada_ms`, y deja todo lo demás.
//   4. Buscar encuentra en la bitácora y en las transcripciones.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "evento/Historico.hpp"

namespace fs = std::filesystem;
using ba::evento::Historico;
using json = nlohmann::json;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << s;
}
std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
/// Le pone a un archivo la fecha de hace `horas`.
void envejecer(const fs::path& p, int horas) {
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(horas));
}
json leer(const fs::path& p) {
    std::ifstream f(p);
    return json::parse(f, nullptr, false);
}
} // namespace

int main() {
    const fs::path raiz = fs::temp_directory_path() /
                          ("ba-test-historico-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path rec = raiz / "Recordings", ev = raiz / "Eventos" / "2026" / "Sesion";
    escribir(ev / "evento.json", "{}");
    const std::int64_t inicioSeg = ahoraMs() / 1000 - 3600;
    escribir(rec / ("ba-" + std::to_string(inicioSeg) + "-00000.mkv"), std::string(1000, 'v'));
    escribir(rec / "entradas" / "camara-1.mkv", std::string(500, 'c'));
    escribir(rec / "viejo.mkv", "x");
    envejecer(rec / "viejo.mkv", 72);
    escribir(rec / "nota.txt", "no es video");
    escribir(rec / "Transcripciones" / "sala-hoy.txt", "se aprueba el punto 3");
    escribir(rec / "Transcripciones" / "sala-viejo.txt", "otro día");
    envejecer(rec / "Transcripciones" / "sala-viejo.txt", 72);

    const std::int64_t desde = ahoraMs() - 6 * 3600 * 1000LL, hasta = ahoraMs() + 60000;
    json r = Historico::traerDelDia(ev, {rec}, rec / "Transcripciones", desde, hasta, false);
    CHECK(r["grabaciones"].size() == 2, "las dos de hoy (con la de entradas/), no la vieja ni el txt");
    CHECK(r["transcripciones"] == json::array({"sala-hoy.txt"}), "la transcripción de hoy");
    CHECK(fs::exists(rec / ("ba-" + std::to_string(inicioSeg) + "-00000.mkv")), "sin mover, queda donde está");
    CHECK(fs::exists(ev / "Transcripciones" / "sala-hoy.txt"), "la transcripción se copia");
    json idx = leer(ev / "grabaciones.json");
    bool inicio = false, tipo = false;
    for (const auto& g : idx) {
        inicio = inicio || g.value("desde_ms", 0LL) == inicioSeg * 1000;
        tipo = tipo || g.value("tipo", "") == "entradas";
        CHECK(g.value("importada", false), "marcada como importada");
    }
    CHECK(inicio, "el inicio sale del nombre");
    CHECK(tipo, "la de entradas/ dice de dónde viene");
    r = Historico::traerDelDia(ev, {rec}, rec / "Transcripciones", desde, hasta, false);
    CHECK(r["grabaciones"].empty() && r["transcripciones"].empty(), "dos veces no repite");

    // Con mover: otro evento, la grabación pasa a su carpeta.
    const fs::path ev2 = raiz / "Eventos" / "Otra";
    escribir(ev2 / "evento.json", "{}");
    r = Historico::traerDelDia(ev2, {rec}, {}, desde, hasta, true);
    CHECK(r["grabaciones"].size() == 2, "mover: las dos");
    CHECK(fs::exists(ev2 / "Grabaciones" / ("ba-" + std::to_string(inicioSeg) + "-00000.mkv")), "mover: en la carpeta del evento");
    CHECK(fs::exists(ev2 / "Grabaciones" / "entradas" / "camara-1.mkv"), "mover: entradas/ a su subcarpeta");
    CHECK(!fs::exists(rec / "entradas" / "camara-1.mkv"), "mover: ya no está en Recordings");

    // Retención.
    escribir(ev2 / "bitacora.jsonl", "{\"t\":1,\"tipo\":\"bus\",\"origen\":\"aire/escena\",\"escena\":\"Votación\"}\n");
    json s = Historico::limpiarGrabaciones(ev2, 5000, true);
    CHECK(s["archivos"].size() == 2 && s.value("bytes", 0) == 1500, "simular cuenta lo que borraría");
    CHECK(fs::exists(ev2 / "Grabaciones" / "entradas" / "camara-1.mkv"), "simular no borra");
    s = Historico::limpiarGrabaciones(ev2, 5000, false);
    CHECK(!fs::exists(ev2 / "Grabaciones" / "entradas" / "camara-1.mkv"), "borra las grabaciones");
    CHECK(fs::exists(ev2 / "bitacora.jsonl") && fs::exists(ev2 / "grabaciones.json"), "lo demás queda");
    bool marcada = true;
    for (const auto& g : leer(ev2 / "grabaciones.json")) marcada = marcada && g.value("borrada_ms", 0) == 5000;
    CHECK(marcada, "cada una anotada con borrada_ms");

    // Buscar.
    const json b = Historico::buscar(raiz / "Eventos", "VOTACIÓN");
    const json b2 = Historico::buscar(raiz / "Eventos", "punto 3");
    CHECK(b.size() == 1 && b[0]["evento"] == "Otra", "en la bitácora");
    CHECK(b2.size() == 1 && b2[0]["archivo"] == "Transcripciones/sala-hoy.txt", "en las transcripciones");

    fs::remove_all(raiz);
    std::printf(g_fallos == 0 ? "test_historico: OK\n" : "test_historico: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
