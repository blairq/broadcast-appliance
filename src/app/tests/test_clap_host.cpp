// ============================================================================
// src/app/tests/test_clap_host.cpp — host CLAP contra los plugins LSP reales
//
//   1. Abre lsp-plugins.clap y lista sus plugins.
//   2. Crea un compresor estereo, lo activa y procesa audio: con un umbral
//      bajo y ratio alto, un tono fuerte sale mas bajo.
//   3. Los parametros cambian por la cola (mientras procesa) y por flush
//      (quieto); el estado se guarda y se restaura.
// LSP no publica sus medidores por CLAP (solo a su interfaz): los mide el rack.
// Con BA_CLAP_DUMP=<filtro> lista los plugins cuyo id contiene el filtro y
// todos sus parametros (para armar las interfaces de alto nivel).
// ============================================================================

#include <numbers>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audio/ClapHost.hpp"

using namespace ba::app::audio;

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

// Procesa `blocks` bloques de un tono de 1 kHz a `amp`; devuelve el pico de salida del ultimo.
float run(ClapInstance& c, float amp, int blocks) {
    const uint32_t N = 512;
    std::vector<float> l(N), r(N), ol(N), orr(N), sl(N, 0.f), sr(N, 0.f), osl(N), osr(N);
    float* inMain[2] = {l.data(), r.data()};
    float* inSc[2] = {sl.data(), sr.data()};
    float* outMain[2] = {ol.data(), orr.data()};
    float* outSc[2] = {osl.data(), osr.data()};
    float* const* ins[2] = {inMain, inSc};
    float* const* outs[2] = {outMain, outSc};
    static double phase = 0;
    float peak = 0;
    for (int b = 0; b < blocks; ++b) {
        for (uint32_t i = 0; i < N; ++i) {
            l[i] = r[i] = amp * static_cast<float>(std::sin(phase));
            phase += 2 * std::numbers::pi * 1000.0 / 48000.0;
        }
        c.process(ins, outs, N);
        peak = 0;
        for (uint32_t i = 0; i < N; ++i)
            peak = std::max(peak, std::fabs(ol[i]));
    }
    return peak;
}

} // namespace

int main() {
    const char* path = std::getenv("BA_LSP_CLAP");
    if (path == nullptr) path = BA_TEST_LSP_CLAP;
    std::string err;
    auto lib = ClapLibrary::open(path, &err);
    if (!lib) {
        std::printf("SKIP: no se pudo abrir %s (%s)\n", path, err.c_str());
        return 77;
    }
    std::printf("%zu plugins en %s\n", lib->plugins().size(), path);
    CHECK(lib->plugins().size() > 50, "LSP trae muchos plugins");
    Direct svc;

    // Para armar el catálogo: id, nombre, descripción y características de todos.
    if (std::getenv("BA_CLAP_LISTA") != nullptr) {
        for (const auto& d : lib->plugins()) {
            std::string f;
            for (const auto& x : d.features)
                f += (f.empty() ? "" : ",") + x;
            std::string desc = d.description;
            for (char& c : desc)
                if (c == '\n' || c == '\t' || c == '\r') c = ' ';
            std::printf("%s\t%s\t%s\t%s\n", d.id.c_str(), d.name.c_str(), desc.c_str(), f.c_str());
        }
        return 0;
    }
    if (const char* dump = std::getenv("BA_CLAP_DUMP")) {
        for (const auto& d : lib->plugins()) {
            if (d.id.find(dump) == std::string::npos) continue;
            std::printf("\n== %s  (%s)\n", d.id.c_str(), d.name.c_str());
            auto inst = ClapInstance::create(lib, d.id, &svc, &err);
            if (!inst) continue;
            for (const auto& p : inst->inputs())
                std::printf("   in  %s x%u%s\n", p.name.c_str(), p.channels, p.main ? " (main)" : "");
            for (const auto& p : inst->outputs())
                std::printf("   out %s x%u%s\n", p.name.c_str(), p.channels, p.main ? " (main)" : "");
            for (const auto& p : inst->params()) {
                double v = 0;
                inst->getValue(p.id, v);
                std::printf("   %5u %-28s %-10s [%g..%g] def %g = %s%s%s\n", p.id, p.name.c_str(),
                            p.module.c_str(), p.min, p.max, p.def, inst->valueText(p.id, v).c_str(),
                            p.readOnly() ? "  RO" : "", p.isEnum() ? "  ENUM" : "");
            }
        }
        return 0;
    }

    const char* kComp = "in.lsp-plug.compressor_stereo";
    auto comp = ClapInstance::create(lib, kComp, &svc, &err);
    CHECK(comp != nullptr, "se crea el compresor estereo");
    if (!comp) {
        std::printf("error: %s\n", err.c_str());
        return 1;
    }
    CHECK(!comp->inputs().empty() && comp->inputs()[0].channels == 2, "entrada principal estereo");
    CHECK(comp->activate(48000, 1024), "se activa");
    // La interfaz del modulo: en cada plataforma con SU tipo de ventana (en la
    // Mac, Cocoa; antes se preguntaba por X11 y el boton quedaba apagado).
    CHECK(comp->hasEditor(), "el modulo tiene interfaz para esta plataforma");
    const auto ps = comp->params();
    CHECK(ps.size() > 10, "tiene parametros");
    // Unidades reales: LSP convierte texto a valor (muchos parametros son 0..1
    // con curva). Es lo que usa la interfaz de alto nivel.
    for (const auto& p : ps) {
        if (p.name != "Attack time") continue;
        double v = 0;
        CHECK(comp->textToValue(p.id, "35", v), "texto -> valor (ataque)");
        std::printf("ataque \"35\" -> %g -> \"%s\"\n", v, comp->valueText(p.id, v).c_str());
        CHECK(comp->valueText(p.id, v).find("35") != std::string::npos, "35 ms ida y vuelta");
    }

    const float dry = run(*comp, 0.9f, 40);
    std::printf("pico de salida con los valores por defecto: %.3f\n", dry);
    CHECK(dry > 0.05f, "pasa audio");

    // Umbral muy bajo + ratio alto: tiene que comprimir.
    const ParamInfo* thr = nullptr;
    const ParamInfo* ratio = nullptr;
    for (const auto& p : ps) {
        std::string n = p.name;
        for (auto& ch : n)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (thr == nullptr && !p.readOnly() && n.find("threshold") != std::string::npos) thr = &p;
        if (ratio == nullptr && !p.readOnly() && n.find("ratio") != std::string::npos) ratio = &p;
    }
    CHECK(thr != nullptr && ratio != nullptr, "tiene umbral y ratio");
    if (thr != nullptr && ratio != nullptr) {
        CHECK(comp->setValue(thr->id, thr->min), "umbral al minimo");
        CHECK(comp->setValue(ratio->id, ratio->max), "ratio al maximo");
        const float wet = run(*comp, 0.9f, 200);
        std::printf("pico de salida comprimido: %.3f\n", wet);
        CHECK(wet < dry * 0.7f, "comprime (la cola de parametros llega al audio)");
        double t = 0;
        CHECK(comp->getValue(thr->id, t) && std::fabs(t - thr->min) < 1e-6, "el valor quedo aplicado");
    }

    // Estado: guardar, cambiar, restaurar.
    const std::vector<uint8_t> st = comp->saveState();
    CHECK(!st.empty(), "guarda estado");
    if (thr != nullptr) {
        comp->deactivate();
        CHECK(comp->setValue(thr->id, thr->def), "cambio quieto (flush)");
        double t = 0;
        comp->getValue(thr->id, t);
        CHECK(std::fabs(t - thr->def) < 1e-6, "flush aplica sin procesar");
        CHECK(comp->loadState(st), "restaura estado");
        comp->getValue(thr->id, t);
        CHECK(std::fabs(t - thr->min) < 1e-6, "el estado trae el umbral guardado");
    }
    comp.reset();

    if (g_failures == 0) std::printf("test_clap_host: OK\n");
    return g_failures == 0 ? 0 : 1;
}
