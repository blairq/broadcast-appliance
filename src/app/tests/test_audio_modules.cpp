// ============================================================================
// src/app/tests/test_audio_modules.cpp — catalogo de modulos contra LSP real
//
//   1. Cada parametro curado de cada modulo existe en el plugin (por banda
//      tambien): si LSP cambia un nombre, esto falla.
//   2. Los presets se aplican sin errores y en unidades reales.
//   3. El preset "voz" del EQ mide lo que promete (pasa-altos en 80 Hz, -2 dB
//      en 250 Hz, +2 dB en 4 kHz) y el gate "voz" queda en -45 dBFS.
// ============================================================================

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio/AudioRack.hpp"
#include "audio/Modules.hpp"

using namespace ba::app::audio;
using json = nlohmann::json;

namespace {
int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)
struct Direct : HostServices {
    void runOnMain(std::function<void()> fn) override { fn(); }
    int addTimer(int, std::function<void()>) override { return 1; }
    void removeTimer(int) override {}
    bool addFd(int, uint32_t, std::function<void(uint32_t)>) override { return true; }
    void removeFd(int) override {}
};
} // namespace

int main() {
    const char* path = std::getenv("BA_LSP_CLAP");
    if (path == nullptr) path = BA_TEST_LSP_CLAP;
    std::string err;
    auto lib = ClapLibrary::open(path, &err);
    if (!lib) {
        std::printf("SKIP: %s\n", err.c_str());
        return 77;
    }
    Direct svc;
    for (const auto& t : moduleCatalog()) {
        auto inst = ClapInstance::create(lib, t.pluginId, &svc, &err);
        CHECK(inst != nullptr, t.key.c_str());
        if (!inst) continue;
        inst->activate(48000, 1024);
        const auto ps = inst->params();
        for (const auto& mp : t.params)
            if (findParam(ps, mp.lspName) == nullptr) {
                std::printf("FALLO: %s: no existe '%s'\n", t.key.c_str(), mp.lspName.c_str());
                ++g_failures;
            }
        for (int b = t.bandFirst; b < t.bandFirst + t.bandCount; ++b)
            for (const auto& mp : t.bandParams) {
                std::string n = mp.lspName;
                n.replace(n.find("{n}"), 3, std::to_string(b));
                // La banda 1 del multibanda no tiene "activa"/"corte" (va desde abajo).
                if (t.key == "multibanda" && b == 1 && (mp.key == "activa" || mp.key == "corte_hz")) continue;
                if (findParam(ps, n) == nullptr) {
                    std::printf("FALLO: %s: no existe '%s'\n", t.key.c_str(), n.c_str());
                    ++g_failures;
                }
            }
        for (auto it = t.presets.begin(); it != t.presets.end(); ++it) {
            const json r = applyModule(*inst, &t, it.value());
            std::printf("%s/%s: %s\n", t.key.c_str(), it.key().c_str(), r.dump().c_str());
            CHECK(r["errores"].empty(), "el preset se aplica sin errores");
        }
    }

    // El preset "voz" del EQ, medido.
    AudioRack rack(lib, &svc, 48000);
    const std::string eq = rack.addPlugin(findModule("eq")->pluginId, "EQ", "", &err);
    applyModule(*rack.plugin(eq), findModule("eq"), findModule("eq")->presets["voz"]);
    const json resp = rack.measure("respuesta", eq, -20, &err);
    double g25 = 0, g250 = 0, g4k = 0, g1k = 0;
    for (const auto& pt : resp["puntos"]) {
        const double hz = pt["hz"];
        if (hz == 25) g25 = pt["gananciaDb"];
        if (hz == 250) g250 = pt["gananciaDb"];
        if (hz == 4000) g4k = pt["gananciaDb"];
        if (hz == 1000) g1k = pt["gananciaDb"];
    }
    std::printf("EQ voz: 25 Hz %+.1f, 250 Hz %+.1f, 1 kHz %+.1f, 4 kHz %+.1f\n", g25, g250, g1k, g4k);
    CHECK(g25 < -6, "pasa-altos de 80 Hz corta 25 Hz");
    CHECK(std::fabs(g250 + 2) < 1, "-2 dB en 250 Hz");
    CHECK(std::fabs(g4k - 2) < 1, "+2 dB en 4 kHz");
    CHECK(std::fabs(g1k) < 1.5, "1 kHz casi sin cambios");

    const std::string gate = rack.addPlugin(findModule("gate")->pluginId, "Gate", "", &err);
    applyModule(*rack.plugin(gate), findModule("gate"), findModule("gate")->presets["voz"]);
    const json g = readModule(*rack.plugin(gate), *findModule("gate"));
    std::printf("gate: umbral %s, reduccion %s\n", g["umbral_db"]["texto"].get<std::string>().c_str(),
                g["reduccion_db"]["texto"].get<std::string>().c_str());
    CHECK(g["umbral_db"]["texto"].get<std::string>().find("-45") != std::string::npos, "umbral en -45 dB");
    // Un error claro ante una opcion que no existe.
    const json bad =
        applyModule(*rack.plugin(eq), findModule("eq"), {{"bandas", {{{"banda", 3}, {"tipo", "Campana"}}}}});
    std::printf("error esperado: %s\n", bad["errores"].dump().c_str());
    CHECK(!bad["errores"].empty(), "opcion invalida -> error con las validas");

    if (g_failures == 0) std::printf("test_audio_modules: OK\n");
    return g_failures == 0 ? 0 : 1;
}
