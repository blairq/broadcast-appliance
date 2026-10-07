// ============================================================================
// src/app/tests/test_cierre_evento.cpp — lo que queda al cerrar un evento (H5)
//
//   1. La hora de cada formato de log (UTC con Z, local, «====», «[…]», jsonl).
//   2. Recortar los logs: solo lo de la ventana, las líneas sin hora siguen a
//      la anterior, los rotados en orden (del más viejo al más nuevo).
//   3. La asamblea se copia sin secretos.
//   4. Sellar y verificar: intacto; un archivo tocado, uno borrado, uno nuevo.
//   5. Sellar cancelado no escribe sellos.json.
// ============================================================================
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "evento/CierreEvento.hpp"

namespace fs = std::filesystem;
using ba::evento::CierreEvento;
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
std::string leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}
std::int64_t local(int y, int mo, int d, int h, int mi, int s) {
    std::tm tm{};
    tm.tm_year = y - 1900, tm.tm_mon = mo - 1, tm.tm_mday = d, tm.tm_hour = h, tm.tm_min = mi, tm.tm_sec = s;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm)) * 1000;
}
} // namespace

int main() {
    // 1. Horas. 2026-10-07T04:28:37.700Z = 1791347317700.
    CHECK(CierreEvento::horaDeLinea("2026-10-07T04:28:37.700Z info | app | 0 | x") == 1791347317700LL, "UTC con Z");
    CHECK(CierreEvento::horaDeLinea("[2026-10-07T04:28:37.700Z] info") == 1791347317700LL, "entre corchetes");
    CHECK(CierreEvento::horaDeLinea("2026-10-05T04:22:11.294  tab  accion") == local(2026, 10, 5, 4, 22, 11) + 294, "local");
    CHECK(CierreEvento::horaDeLinea("==== 2026-10-07T01:28:35 | el launcher") == local(2026, 10, 7, 1, 28, 35), "====");
    CHECK(CierreEvento::horaDeLinea(R"({"ms":1791225273871,"que":"entro"})") == 1791225273871LL, "jsonl ms");
    CHECK(CierreEvento::horaDeLinea("ba-app: arrancando motor") == 0, "sin hora");

    const fs::path raiz = fs::temp_directory_path() /
                          ("ba-test-cierre-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    // 2. Logs.
    escribir(raiz / "Logs" / "app.log.1", "2026-10-07T10:00:00.000Z info | viejo\n2026-10-07T11:00:00.000Z info | rotado\n");
    escribir(raiz / "Logs" / "app.log",
             "2026-10-07T11:30:00.000Z info | uno\nba-app: sin hora, sigue\n2026-10-07T13:00:00.000Z info | despues\n");
    escribir(raiz / "Logs" / "sesion-remota.jsonl", "{\"ms\":1791372517700,\"que\":\"entro\"}\n{\"ms\":1,\"que\":\"viejo\"}\n");
    escribir(raiz / "Logs" / "nada.txt", "2026-10-07T11:30:00.000Z no es log\n");
    const std::int64_t desde = CierreEvento::horaDeLinea("2026-10-07T10:30:00.000Z"),
                       hasta = CierreEvento::horaDeLinea("2026-10-07T12:30:00.000Z");
    const json r = CierreEvento::recortarRegistros(raiz / "Logs", desde, hasta, raiz / "ev" / "registro");
    CHECK(r.value("app.log", 0) == 3, "rotado + uno + la línea sin hora");
    CHECK(leer(raiz / "ev" / "registro" / "app.log") ==
              "2026-10-07T11:00:00.000Z info | rotado\n2026-10-07T11:30:00.000Z info | uno\nba-app: sin hora, sigue\n",
          "en orden y sin lo de afuera");
    CHECK(r.value("sesion-remota.jsonl", 0) == 1, "jsonl por su ms");
    CHECK(!r.contains("nada.txt"), "solo .log y .jsonl");

    // 3. Asamblea.
    escribir(raiz / "Config" / "orden-del-dia.json", R"({"puntos": ["Apertura", "Informe"]})");
    escribir(raiz / "Config" / "sesion-remota.json", R"({"integrantes": [{"nombre": "Ana", "hash_token": "0123456789abcdef0123"}]})");
    escribir(raiz / "Config" / "escenas.json", R"({"no": "es de la asamblea"})");
    CHECK(CierreEvento::copiarAsamblea(raiz / "Config", raiz / "ev") == 2, "dos de la asamblea");
    CHECK(leer(raiz / "ev" / "asamblea" / "sesion-remota.json").find("0123456789abcdef") == std::string::npos,
          "sin el token");
    CHECK(!fs::exists(raiz / "ev" / "asamblea" / "escenas.json"), "lo demás no");

    // 4. Sellos.
    escribir(raiz / "ev" / "bitacora.jsonl", "{\"t\":1}\n");
    escribir(raiz / "ev" / "Grabaciones" / "ba-1.mkv", std::string(3 << 20, 'x'));
    escribir(raiz / "ev" / "Respaldos" / "evento-1.json", "{}");
    escribir(raiz / "ev" / "config" / "ultima.json", "{}");
    const json s = CierreEvento::sellar(raiz / "ev", 1000);
    CHECK(!s.is_null() && fs::exists(raiz / "ev" / "sellos.json"), "sellado");
    CHECK(s["archivos"].contains("Grabaciones/ba-1.mkv"), "con las grabaciones");
    CHECK(!s["archivos"].contains("Respaldos/evento-1.json") && !s["archivos"].contains("config/ultima.json"),
          "sin respaldos ni la foto que se sigue escribiendo");
    CHECK(CierreEvento::verificar(raiz / "ev").value("ok", false), "intacto");
    escribir(raiz / "ev" / "bitacora.jsonl", "{\"t\":2}\n");
    fs::remove(raiz / "ev" / "asamblea" / "orden-del-dia.json");
    escribir(raiz / "ev" / "agregado.txt", "x");
    const json v = CierreEvento::verificar(raiz / "ev");
    CHECK(!v.value("ok", true), "tocado");
    CHECK(v["cambiados"] == json::array({"bitacora.jsonl"}), "cuál cambió");
    CHECK(v["faltan"] == json::array({"asamblea/orden-del-dia.json"}), "cuál falta");
    CHECK(v["nuevos"] == json::array({"agregado.txt"}), "cuál es nuevo");

    // 5. Cancelado.
    fs::remove(raiz / "ev" / "sellos.json");
    std::atomic<bool> cancelar{true};
    CHECK(CierreEvento::sellar(raiz / "ev", 2000, &cancelar).is_null(), "cancelado devuelve null");
    CHECK(!fs::exists(raiz / "ev" / "sellos.json"), "y no escribe nada");

    std::error_code ecFin;
    fs::remove_all(raiz, ecFin); // sin lanzar: en Windows, un archivo abierto no se borra
    std::printf(g_fallos == 0 ? "test_cierre_evento: OK\n" : "test_cierre_evento: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
