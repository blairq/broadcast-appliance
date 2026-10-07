// ============================================================================
// src/app/tests/test_bitacora.cpp — la bitácora del evento en curso
//
//   1. Lo anotado llega al archivo, una línea JSON por hecho, en orden.
//   2. Sin archivo no se escribe nada (y no se acumula).
//   3. Cambiar de evento: lo de antes queda en el anterior, lo de después en el nuevo.
//   4. Una categoría apagada no se escribe.
//   5. El limpiador se aplica (los secretos no llegan al disco).
//   6. Leer filtra por tipo, texto, rango y límite; una línea rota (corte de
//      luz a mitad) se saltea sin perder las demás.
//   7. Muchos hilos a la vez: no se pierde ni se mezcla ninguna línea.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "evento/Bitacora.hpp"

namespace fs = std::filesystem;
using ba::evento::Bitacora;
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

std::size_t lineas(const fs::path& p) {
    std::ifstream f(p);
    std::size_t n = 0;
    std::string l;
    while (std::getline(f, l)) ++n;
    return n;
}
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ba-test-bitacora-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    const fs::path a = dir / "A" / "bitacora.jsonl", b = dir / "B" / "bitacora.jsonl";
    {
        Bitacora bit;
        bit.anotar("mcp", "mcp", "sin archivo");
        bit.vaciar();
        CHECK(!fs::exists(a) && bit.escritas() == 0, "sin archivo no escribe");

        bit.setArchivo(a);
        bit.anotar("evento", "app", "arranca", json::object(), 1000);
        bit.anotar("bus", "aire/escena", {}, json{{"escena", "Principal"}}, 2000);
        bit.anotar("bus", "salida/stream", {}, true, 3000);
        bit.vaciar();
        CHECK(lineas(a) == 3, "tres líneas en A");
        const json l = Bitacora::leer(a);
        CHECK(l.size() == 3 && l[1].value("escena", "") == "Principal", "objeto se suma a la línea");
        CHECK(l.size() == 3 && l[2].value("valor", false) == true, "lo que no es objeto va en valor");

        bit.setRegistrar("botonera", false);
        bit.anotar("botonera", "botonera", "apagada");
        bit.setLimpiador([](const json& j) {
            json o = j;
            if (o.is_object() && o.contains("clave")) o["clave"] = "«tapado»";
            return o;
        });
        bit.anotar("mcp", "mcp", "stream", json{{"clave", "abcd-1234-secreta"}}, 4000);
        bit.setArchivo(b);
        bit.anotar("evento", "historico", "en curso", json::object(), 5000);
        bit.vaciar();
        CHECK(lineas(a) == 4, "lo de antes del cambio queda en A");
        CHECK(lineas(b) == 1, "lo de después va a B");
        std::ifstream fa(a);
        const std::string todo((std::istreambuf_iterator<char>(fa)), {});
        CHECK(todo.find("secreta") == std::string::npos, "el secreto no llega al disco");
        CHECK(todo.find("apagada") == std::string::npos, "categoría apagada no se escribe");

        // Muchos hilos.
        std::vector<std::thread> hs;
        for (int h = 0; h < 8; ++h)
            hs.emplace_back([&bit, h] {
                for (int i = 0; i < 500; ++i) bit.anotar("aviso", "h" + std::to_string(h), "linea " + std::to_string(i));
            });
        for (auto& t : hs) t.join();
        bit.vaciar();
        CHECK(lineas(b) == 1 + 8 * 500, "ninguna línea perdida con 8 hilos");
        CHECK(Bitacora::leer(b, 0, 0, {}, {}, 0).size() == 1 + 8 * 500, "todas se leen (ninguna mezclada)");
    }
    // Leer: filtros y línea rota.
    {
        std::ofstream f(a, std::ios::app);
        f << "{\"t\": 6000, \"tipo\": \"bus\", \"orig";  // cortado
    }
    CHECK(Bitacora::leer(a, 0, 0, {"bus"}).size() == 2, "filtra por tipo y saltea la rota");
    CHECK(Bitacora::leer(a, 0, 0, {}, "PRINCIPAL").size() == 1, "texto sin mayúsculas");
    CHECK(Bitacora::leer(a, 2500, 3500).size() == 1, "rango");
    const json ult = Bitacora::leer(a, 0, 0, {}, {}, 2);
    CHECK(ult.size() == 2 && ult[1].value("t", 0) == 4000, "límite: las últimas");
    fs::remove_all(dir);
    std::printf(g_fallos == 0 ? "test_bitacora: OK\n" : "test_bitacora: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
