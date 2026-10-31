// test_politica.cpp — lo que decide el supervisor, sin procesos (Politica.hpp)
//
//   1. Backoff que se olvida: 5 caídas espaciadas 10 min (el hijo anduvo bien en
//      el medio) → cada relanzamiento con espera 0 s. Seguidas, sí crece hasta 10 s.
//   2. El backoff no frena al otro: A esperando sus 10 s y B que se cae → B toca
//      ya (en la misma vuelta del bucle, ≤ 250 ms) y A sigue esperando lo suyo.
//   3. Readopción con versión: v2 + A v1 vivo → reemplazar; con aire → posponer;
//      misma versión → adoptar; nadie vivo → lanzar.
//   4. Aire: al-aire.json con alguna salida transmitiendo o grabando, solo si A
//      vive y late en BA.Health; una corrida cerrada en orden no es aire.

#include "Politica.hpp"
#include "persist/Generaciones.hpp"

#include "ipc/HealthBlock.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace ba::launcher;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr std::uint64_t kEstable = 120000;
constexpr std::uint64_t kVuelta = 250; // la vuelta del watchdog

void escribir(const fs::path& p, const std::string& t) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
}

/// Simula el watchdog con un hijo andando de `desde` a `hasta`, vuelta por vuelta.
void andar(Relanzador& r, std::uint64_t desde, std::uint64_t hasta) {
    for (std::uint64_t t = desde; t < hasta; t += kVuelta) r.vigilar(t, true, kEstable);
}
} // namespace

int main() {
    // --- 1) caídas espaciadas: espera 0 ---------------------------------------------------
    {
        Relanzador a;
        std::uint64_t t = 1000;
        a.lanzado(t);
        for (int i = 0; i < 5; ++i) {
            andar(a, t, t + 10 * 60 * 1000);
            t += 10 * 60 * 1000;
            CHECK(a.caida(t) == 0, "caída aislada tras 10 min estable: sin espera");
            CHECK(a.toca(t), "se relanza en la misma vuelta");
            a.lanzado(t);
        }
        // Seguidas (sin tiempo estable en el medio): 0, 1, 2, 5 y el tope.
        Relanzador b;
        b.lanzado(t);
        const std::uint64_t esperado[] = {0, 1000, 2000, 5000, 10000, 10000};
        for (std::uint64_t e : esperado) {
            andar(b, t, t + 30000); // 30 s: menos que lo estable
            t += 30000;
            CHECK(b.caida(t) == e, "seguidas: el backoff crece");
            t += e;
            CHECK(b.toca(t), "toca al cumplirse la espera");
            b.lanzado(t);
        }
        // Un hijo que no late (colgado, o recién lanzado sin latir) no se olvida.
        Relanzador c;
        c.lanzado(t);
        c.caida(t);
        c.lanzado(t);
        CHECK(!c.vigilar(t + 10 * kEstable, false, kEstable) && c.consecutivas() == 1, "sin latido no es estable");
        CHECK(c.vigilar(t + kEstable, true, kEstable) && c.consecutivas() == 0, "con latido y el tiempo: se olvida");
    }

    // --- 2) A en backoff no frena a B ------------------------------------------------------
    {
        Relanzador a, b;
        std::uint64_t t = 5000;
        a.lanzado(t);
        b.lanzado(t);
        for (int i = 0; i < 4; ++i) {
            a.caida(t);
            t += 6000;
            a.lanzado(t);
        }
        const std::uint64_t esperaA = a.caida(t); // la quinta seguida: 10 s
        CHECK(esperaA == 10000, "A agenda su espera de 10 s");
        t += 3 * kVuelta;
        CHECK(!a.toca(t), "A todavía espera");
        const std::uint64_t caeB = t;
        CHECK(b.caida(caeB) == 0, "B: primera caída, sin espera");
        CHECK(b.toca(caeB) && b.toca(caeB + kVuelta), "B se relanza en la misma vuelta");
        b.lanzado(caeB);
        CHECK(!a.toca(caeB) && a.toca(caeB - 3 * kVuelta + esperaA), "A sigue con lo suyo");
        a.lanzado(caeB + esperaA);
        a.olvidar();
        CHECK(!a.agendado() && a.consecutivas() == 0, "forzar reinicio: desde cero");
        Relanzador e;
        e.agendar(t, 60000); // falla del entorno: espera sin contar caída
        CHECK(e.consecutivas() == 0 && !e.toca(t + 59000) && e.toca(t + 60000), "entorno: agendado sin backoff");
    }

    // --- 3) readopción con versión ---------------------------------------------------------
    CHECK(decidirReadopcion(true, 1, 2, false) == Readopcion::Reemplazar, "launcher v2 + A v1 vivo: se relanza");
    CHECK(decidirReadopcion(true, 1, 2, true) == Readopcion::Posponer, "con aire: se pospone");
    CHECK(decidirReadopcion(true, 2, 2, true) == Readopcion::Adoptar, "misma versión: se adopta");
    CHECK(decidirReadopcion(false, 1, 2, true) == Readopcion::Lanzar, "nadie vivo: se lanza");

    // --- 4) aire ---------------------------------------------------------------------------
    {
        const fs::path base = fs::temp_directory_path() / "ba-test-politica";
        fs::remove_all(base);
        const fs::path runtime = base / "Runtime";
        const fs::path alAire = runtime / "al-aire.json";
        CHECK(!salidasActivas(alAire), "sin archivo: sin aire");
        escribir(alAire, R"({"streaming":false,"recording":false,"cerrado":false,"salidas":{}})");
        CHECK(!salidasActivas(alAire), "todo apagado");
        escribir(alAire, R"({"streaming":true,"recording":false,"cerrado":false})");
        CHECK(salidasActivas(alAire), "la principal transmitiendo");
        escribir(alAire, R"({"streaming":false,"recording":false,"cerrado":false,"salidas":{"b":{"recording":true}}})");
        CHECK(salidasActivas(alAire), "otra salida grabando");
        escribir(alAire, R"({"streaming":true,"recording":true,"cerrado":true})");
        CHECK(!salidasActivas(alAire), "corrida cerrada en orden");
        escribir(alAire, R"({"streaming":null,"salidas":{"b":{"recording":"si"}}})");
        CHECK(!salidasActivas(alAire), "campos de otro tipo: apagado, sin tirar");
        escribir(alAire, "{\"streaming\":tr");
        CHECK(salidasActivas(alAire), "ilegible: ante la duda, aire");

        // Como lo escribe la app desde F1-03: en dos generaciones (y el de una sola, borrado).
        fs::remove(alAire);
        {
            ba::persist::DosGeneraciones gen(alAire);
            CHECK(gen.escribir(R"({"streaming":false,"cerrado":false,"salidas":{"b":{"recording":true}}})"),
                  "escribir en generaciones");
            CHECK(!fs::exists(alAire) && salidasActivas(alAire), "en generaciones: otra salida grabando");
            CHECK(gen.escribir(R"({"streaming":true,"cerrado":true})") && !salidasActivas(alAire),
                  "en generaciones: cerrada en orden");
            // Las dos rotas: ante la duda, aire.
            for (const fs::path& r : {gen.rutaA(), gen.rutaB()}) std::ofstream(r, std::ios::binary | std::ios::app) << "x";
            CHECK(salidasActivas(alAire), "en generaciones, las dos rotas: ante la duda, aire");
            CHECK(gen.borrar(), "borrar las generaciones");
        }

        // Con A vivo y latiendo en BA.Health, sí; sin A, no.
        escribir(alAire, R"({"streaming":true,"cerrado":false})");
        ba::ipc::HealthBlock salud;
        CHECK(!hayAire(salud, runtime), "sin bloque de salud: nadie vivo");
        const std::wstring nombre = L"BA.Health.test-politica";
        CHECK(ba::ipc::HealthBlock::create(nombre.c_str(), ba::ipc::kSlotsSalud, salud).ok, "bloque de prueba");
        CHECK(!hayAire(salud, runtime), "A sin pid: no hay aire");
        salud.setIdentity(ba::ipc::kSlotSaludApp, 4242, ba::ipc::kVersionProceso);
        salud.beat(ba::ipc::kSlotSaludApp);
        CHECK(hayAire(salud, runtime), "A late y transmite: aire");
        escribir(alAire, R"({"streaming":false,"cerrado":false})");
        CHECK(!hayAire(salud, runtime), "A late sin nada prendido");
        fs::remove_all(base);
    }

    if (g_fallos == 0) std::printf("test_politica: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
