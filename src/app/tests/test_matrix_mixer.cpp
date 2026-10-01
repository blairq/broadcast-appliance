// src/app/tests/test_matrix_mixer.cpp — el Matrix Mixer (nuestro módulo LSP) con audio
//
// third_party/lsp-plugins-matrix-mixer, compilado dentro de lsp-plugins.clap,
// probado por el mismo host CLAP del rack: cada entrada lleva una continua
// distinta (0,1 · 0,2 · 0,3 · 0,4) y se mira la última muestra de cada salida,
// después de dejar que las rampas de ganancia lleguen a su valor.
//
//   - de fábrica es la identidad: cada entrada sale por su número;
//   - un cruce prendido suma: salida 2 = entrada 2 + entrada 1;
//   - mute, solo y fase de las entradas; ganancia y mute de las salidas;
//   - bypass: identidad aunque la matriz diga otra cosa;
//   - estéreo: el balance de una salida.
//
// Sin el plugin compilado (el .clap viejo, o Linux con el binario oficial)
// se saltea (77).

#include "audio/ClapHost.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ba::app::audio;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

struct Direct : HostServices {
    void runOnMain(std::function<void()> fn) override { fn(); }
    int addTimer(int, std::function<void()>) override { return 1; }
    void removeTimer(int) override {}
    bool addFd(int, uint32_t, std::function<void(uint32_t)>) override { return true; }
    void removeFd(int) override {}
};

/// N entradas y salidas de `canales` canales; cada entrada una continua.
struct Banco {
    static constexpr uint32_t kN = 256;
    int puertos, canales;
    std::vector<std::vector<float>> in, out;
    std::vector<std::vector<float*>> inPtr, outPtr;
    std::vector<float* const*> ins, outs;
    Banco(int p, int c) : puertos(p), canales(c), in(p * c), out(p * c), inPtr(p), outPtr(p) {
        for (int i = 0; i < p; ++i) {
            for (int k = 0; k < c; ++k) {
                in[i * c + k].assign(kN, 0.1f * static_cast<float>(i + 1));
                out[i * c + k].assign(kN, 0.0f);
                inPtr[i].push_back(in[i * c + k].data());
                outPtr[i].push_back(out[i * c + k].data());
            }
            ins.push_back(inPtr[i].data());
            outs.push_back(outPtr[i].data());
        }
    }
    void correr(ClapInstance& inst, int bloques = 40) {
        for (int b = 0; b < bloques; ++b)
            inst.process(ins.data(), outs.data(), kN);
    }
    float sale(int puerto, int canal = 0) const { return out[puerto * canales + canal][kN - 1]; }
};

bool cerca(float a, float b) { return std::fabs(a - b) < 1e-3f; }

/// Pone un parámetro por su nombre ("Matrix gain 1 to 2"), en las unidades del host.
bool poner(ClapInstance& inst, const std::string& nombre, double valor) {
    for (const auto& p : inst.params())
        if (p.name == nombre) return inst.setValue(p.id, valor);
    std::printf("   (no existe el parametro «%s»)\n", nombre.c_str());
    return false;
}

std::unique_ptr<ClapInstance> crear(std::shared_ptr<ClapLibrary> lib, const char* id, Direct* svc) {
    std::string err;
    auto inst = ClapInstance::create(lib, id, svc, &err);
    if (inst) inst->activate(48000, Banco::kN);
    return inst;
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
    Direct svc;
    auto m = crear(lib, "in.lsp-plug.matrix_mixer_x4_mono", &svc);
    if (!m) {
        std::printf("SKIP: este lsp-plugins.clap no trae el Matrix Mixer (make deps-lsp-macos lo compila)\n");
        return 77;
    }

    CHECK(m->inputs().size() == 4 && m->outputs().size() == 4, "4 entradas y 4 salidas");
    CHECK(m->inputs()[0].main && m->outputs()[0].main, "la entrada y la salida 1 son las principales");

    Banco b(4, 1);
    b.correr(*m);
    CHECK(cerca(b.sale(0), 0.1f) && cerca(b.sale(1), 0.2f) && cerca(b.sale(2), 0.3f) &&
              cerca(b.sale(3), 0.4f),
          "de fabrica es la identidad");

    poner(*m, "Matrix gain 1 to 2", 0.0); // 0 dB
    b.correr(*m);
    CHECK(cerca(b.sale(1), 0.3f), "un cruce suma: salida 2 = entrada 2 + entrada 1");
    CHECK(cerca(b.sale(0), 0.1f), "la entrada 1 sigue en su salida");

    poner(*m, "Input mute 1", 1.0);
    b.correr(*m);
    CHECK(cerca(b.sale(0), 0.0f) && cerca(b.sale(1), 0.2f), "mute de entrada: sale de todos sus cruces");
    poner(*m, "Input mute 1", 0.0);

    poner(*m, "Input phase invert 2", 1.0);
    b.correr(*m);
    CHECK(cerca(b.sale(1), 0.1f - 0.2f), "fase invertida en la entrada 2");
    poner(*m, "Input phase invert 2", 0.0);

    poner(*m, "Output gain 3", -6.0206);
    b.correr(*m);
    CHECK(cerca(b.sale(2), 0.15f), "ganancia de salida (-6 dB = la mitad)");

    poner(*m, "Output mute 4", 1.0);
    b.correr(*m);
    CHECK(cerca(b.sale(3), 0.0f), "mute de salida");
    poner(*m, "Output mute 4", 0.0);

    poner(*m, "Input solo 4", 1.0);
    b.correr(*m);
    CHECK(cerca(b.sale(0), 0.0f) && cerca(b.sale(1), 0.0f) && cerca(b.sale(3), 0.4f),
          "solo: las demas entradas se callan");
    poner(*m, "Input solo 4", 0.0);

    poner(*m, "Bypass", 1.0);
    b.correr(*m, 200); // el bypass hace un fundido
    CHECK(cerca(b.sale(1), 0.2f) && cerca(b.sale(2), 0.3f),
          "bypass: identidad aunque la matriz diga otra cosa");

    // Estéreo: el balance de una salida.
    auto s = crear(lib, "in.lsp-plug.matrix_mixer_x4_stereo", &svc);
    CHECK(s != nullptr && s->inputs().size() == 4 && s->inputs()[0].channels == 2,
          "estereo: 4 entradas de 2 canales");
    if (s) {
        Banco e(4, 2);
        poner(*s, "Output balance 1", 100.0);
        e.correr(*s);
        CHECK(cerca(e.sale(0, 0), 0.0f) && cerca(e.sale(0, 1), 0.2f), "balance de salida todo a la derecha");
        CHECK(cerca(e.sale(1, 0), 0.2f) && cerca(e.sale(1, 1), 0.2f), "las otras salidas no cambian");
    }

    std::printf(g_failures == 0 ? "test_matrix_mixer: OK\n" : "test_matrix_mixer: %d fallos\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
