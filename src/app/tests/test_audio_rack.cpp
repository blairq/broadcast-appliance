// ============================================================================
// src/app/tests/test_audio_rack.cpp — rack de audio con los plugins LSP reales
//
//   1. in -> compresor -> analizador -> out; un tono fuerte sale mas bajo y
//      el medidor del compresor muestra la reduccion.
//   2. Cables: en paralelo (in -> out directo ademas) se suman; un ciclo se
//      rechaza; quitar un modulo deja la cadena conectada.
//   3. Guardar/cargar reproduce el grafo y el estado de cada modulo.
//   4. Medicion fuera de linea: curva de transferencia del compresor (sin
//      tocar el audio en vivo) y respuesta de un modulo neutro (plana).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "audio/AudioRack.hpp"

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

// Pasa `seconds` de un seno de 1 kHz a `amp` por el rack (bloques de 480);
// devuelve el pico de salida del ultimo bloque.
float feed(AudioRack& rack, float amp, double seconds) {
    std::vector<float> l(480), r(480);
    float* planes[2] = {l.data(), r.data()};
    static double ph = 0;
    float peak = 0;
    for (int b = 0; b < static_cast<int>(seconds * 100); ++b) {
        for (int i = 0; i < 480; ++i) {
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(ph));
            ph += 2 * M_PI * 1000 / 48000;
        }
        rack.process(planes, 2, 480);
        peak = 0;
        for (float v : l)
            peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

bool setByName(ClapInstance& c, const char* name, double value) {
    for (const auto& p : c.params())
        if (p.name == name) return c.setValue(p.id, value);
    return false;
}
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
    AudioRack rack(lib, &svc, 48000);
    CHECK(rack.describe()["cables"].size() == 1, "de fabrica: in -> out");
    const float dry = feed(rack, 0.5f, 1);
    CHECK(std::fabs(dry - 0.5f) < 0.01f, "sin modulos pasa igual");

    const std::string comp = rack.addPlugin("in.lsp-plug.compressor_stereo", "Compresor", "", &err);
    CHECK(!comp.empty(), "agrega el compresor");
    const std::string an = rack.addAnalyzer("", comp);
    CHECK(!an.empty(), "agrega un analizador despues");
    auto d = rack.describe();
    std::printf("%s\n", d["cables"].dump().c_str());
    CHECK(d["cables"].size() == 3, "in -> compresor -> analizador -> out");

    // Umbral bajo, ratio alto: comprime.
    ClapInstance* c = rack.plugin(comp);
    double v = 0;
    for (const auto& p : c->params()) {
        if (p.name == "Attack threshold") c->setValue(p.id, p.min);
        if (p.name == "Ratio") c->setValue(p.id, p.max);
        (void)v;
    }
    const float wet = feed(rack, 0.5f, 2);
    const json m = rack.meters(comp);
    std::printf("pico %.3f -> %.3f, medidores del compresor %s\n", dry, wet, m.dump().c_str());
    CHECK(wet < dry * 0.5f, "comprime");
    CHECK(m.value("cambioDb", 0.0) < -6, "el medidor muestra la reduccion");
    CHECK(rack.analyzer(an)->snapshot().seconds > 1, "el analizador midio");
    CHECK(rack.analyzer("in")->snapshot().seconds > 1 && rack.analyzer("out")->snapshot().seconds > 1,
          "entrada y salida se miden siempre");

    // Paralelo: in -> out directo ademas (se suma la señal limpia).
    CHECK(rack.connect("in", "out", "out", "in", &err), "cable en paralelo");
    const float par = feed(rack, 0.5f, 1);
    CHECK(par > wet + 0.3f, "las dos ramas se suman");
    CHECK(rack.disconnect("in", "out", "out", "in"), "se saca");
    CHECK(!rack.connect(an, "out", comp, "in", &err), "un ciclo se rechaza");
    std::printf("ciclo: %s\n", err.c_str());

    // Guardar / cargar.
    const json saved = rack.save();
    AudioRack rack2(lib, &svc, 48000);
    CHECK(rack2.load(saved, &err), "carga");
    CHECK(rack2.describe()["cables"] == rack.describe()["cables"], "mismos cables");
    const float wet2 = feed(rack2, 0.5f, 2);
    CHECK(std::fabs(wet2 - wet) < 0.05f, "mismo estado (comprime igual)");

    // Medicion fuera de linea: curva del compresor.
    const json curve = rack.measure("curva", comp, 0, &err);
    CHECK(!curve.is_null(), "mide la curva");
    if (!curve.is_null()) {
        double at0 = 0, atMinus70 = 0;
        for (const auto& pt : curve["puntos"]) {
            if (pt["entradaDb"] == 0) at0 = pt["cambioDb"];
            if (pt["entradaDb"] == -70) atMinus70 = pt["cambioDb"];
        }
        std::printf("curva: cambio a 0 dBFS %.1f dB, a -70 dBFS %.1f dB\n", at0, atMinus70);
        CHECK(at0 < -20, "arriba del umbral reduce mucho");
    }
    const std::string eq = rack.addPlugin("in.lsp-plug.para_equalizer_x8_stereo", "EQ", "", &err);
    const json flat = rack.measure("respuesta", eq, -20, &err);
    bool isFlat = !flat.is_null();
    for (const auto& pt : flat.value("puntos", json::array()))
        isFlat = isFlat && std::fabs(pt.value("gananciaDb", 99.0)) < 0.5;
    CHECK(isFlat, "EQ sin filtros: respuesta plana");
    // Una campana de +12 dB en 1 kHz, Q 4.
    ClapInstance* e = rack.plugin(eq);
    for (const auto& p : e->params()) {
        if (p.name == "Filter type 0") {
            for (double k = p.min; k <= p.max; k += 1)
                if (e->valueText(p.id, k) == "Bell") e->setValue(p.id, k);
        }
        if (p.name == "Frequency 0" && e->textToValue(p.id, "1000", v)) e->setValue(p.id, v);
        if (p.name == "Gain 0") e->setValue(p.id, 12);
        // La campana de LSP viene de 4 octavas de ancho: Q 4 para que sea angosta.
        if (p.name == "Quality factor 0" && e->textToValue(p.id, "4", v)) e->setValue(p.id, v);
    }
    const json bell = rack.measure("respuesta", eq, -20, &err);
    double at1k = 0, at100 = 0;
    for (const auto& pt : bell.value("puntos", json::array())) {
        if (pt["hz"] == 1000) at1k = pt["gananciaDb"];
        if (pt["hz"] == 100) at100 = pt["gananciaDb"];
    }
    std::printf("campana: 1 kHz %+.1f dB, 100 Hz %+.1f dB\n", at1k, at100);
    CHECK(std::fabs(at1k - 12) < 1.5 && std::fabs(at100) < 1.5, "la respuesta muestra la campana");

    // Quitar el compresor: la cadena sigue conectada (in -> analizador -> eq -> out).
    CHECK(rack.remove(comp, &err), "quita el compresor");
    const float after = feed(rack, 0.5f, 1);
    CHECK(after > 0.4f, "sigue pasando audio sin el compresor");
    CHECK(!rack.remove("in", &err), "la entrada no se quita");

    // 5. Salida de audio del equipo: sumidero aparte. Sin cable no junta nada;
    //    conectado a la entrada copia lo que entra y NO cambia lo que sale al aire.
    CHECK(rack.addOutput("dev-1", "Auriculares") == "salida-audio" && rack.hasOutput(),
          "agrega el nodo de salida");
    CHECK(rack.outputDevice() == "dev-1", "la salida recuerda su dispositivo");
    const float outNoWire = feed(rack, 0.5f, 0.1);
    CHECK(rack.monitorFrames() == 0, "sin cable, la salida no junta audio");
    CHECK(rack.connect("in", "out", "salida-audio", "in", &err), "se patchea la entrada a la salida");
    const float outWired = feed(rack, 0.5f, 0.1);
    CHECK(rack.monitorFrames() == 480, "la salida junta el bloque entero");
    float monPeak = 0;
    for (uint32_t i = 0; i < rack.monitorFrames(); ++i)
        monPeak = std::max(monPeak, std::fabs(rack.monitorPlane(0)[i]));
    CHECK(monPeak > 0.45f && monPeak < 0.55f, "la salida oye la entrada tal cual");
    CHECK(std::fabs(outWired - outNoWire) < 0.02f, "la salida no altera lo que sale al aire");
    // 5b. Tarjeta ENTRADA: el audio de otra fuente llega por su anillo y se
    //     suma donde se la cablee; sin anillo, silencio. Se guarda su fuente.
    {
        AudioRack r3(lib, &svc, 48000);
        const std::string ent = r3.addEntrada("Micrófono");
        CHECK(ent == "entrada" && r3.entradas().size() == 1 && r3.entradas()[0].second == "Micrófono",
              "agrega la tarjeta de entrada");
        CHECK(r3.connect(ent, "out", "out", "in", &err), "se cablea la entrada a la mezcla");
        auto ring = std::make_shared<anillo::Ring>();
        r3.setEntradaAnillo(ent, ring);
        // La otra fuente manda 0.25 constante; la propia, silencio.
        std::vector<float> otra(480, 0.25f), l(480, 0.f), rr(480, 0.f);
        float* planes[2] = {l.data(), rr.data()};
        float pico = 0;
        for (int b = 0; b < 10; ++b) {
            ring->push(otra.data(), otra.data(), 480);
            std::fill(l.begin(), l.end(), 0.f);
            std::fill(rr.begin(), rr.end(), 0.f);
            r3.process(planes, 2, 480);
            pico = std::max(pico, l[100]);
        }
        CHECK(std::fabs(pico - 0.25f) < 0.01f, "lo que llega de la otra fuente sale en la mezcla");
        r3.setEntradaAnillo(ent, nullptr);
        std::fill(l.begin(), l.end(), 0.f);
        r3.process(planes, 2, 480);
        CHECK(std::fabs(l[100]) < 1e-6, "sin anillo, la entrada es silencio");
        // Audio agendado a futuro (la voz del sistema dice la frase entera de
        // una vez, para dentro de 1 s): suena a su hora y entero.
        {
            anillo::Ring futuro;
            std::vector<float> frase(9600, 0.5f); // 200 ms
            const std::uint64_t ahora = 1'000'000'000'000ULL;
            futuro.pushEn(frase.data(), frase.data(), 9600, ahora + 1'000'000'000ULL, ahora, 48000);
            std::vector<float> a(48000), b(48000);
            futuro.pop(a.data(), b.data(), 48000);
            const bool silencioAntes = std::all_of(a.begin(), a.end(), [](float x) { return x == 0.f; });
            std::vector<float> c(9600), d(9600);
            futuro.pop(c.data(), d.data(), 9600);
            const bool frasEntera = std::all_of(c.begin(), c.end(), [](float x) { return x == 0.5f; });
            CHECK(silencioAntes && frasEntera, "lo agendado a futuro suena a su hora y entero (no se recorta)");
        }
        AudioRack r4(lib, &svc, 48000);
        CHECK(r4.load(r3.save(), &err) && r4.entradas().size() == 1 && r4.entradas()[0].second == "Micrófono",
              "la tarjeta de entrada se guarda con su fuente");
        CHECK(r4.setEntradaFuente(ent, "Invitado 1") && r4.entradas()[0].second == "Invitado 1",
              "se le cambia la fuente");
    }
    // 6. Cables por canal: la derecha de un modulo a la izquierda de otro, y
    //    un solo canal (la otra mitad queda en silencio).
    {
        AudioRack r2(lib, &svc, 48000);
        const std::string an2 = r2.addAnalyzer("", "");
        CHECK(r2.connect("in", "out", an2, "in", &err, 1, 0), "derecha -> izquierda");
        const json d = r2.describe();
        bool found = false;
        for (const auto& c : d["cables"])
            if (c.value("desde", "") == "in" && c.value("hacia", "") == an2)
                found = c.value("canalSalida", -1) == 1 && c.value("canalEntrada", -1) == 0;
        CHECK(found, "el cable informa sus canales");
        // Solo llega a la izquierda: con una señal igual en L y R, la salida
        // del analizador tiene la izquierda con señal y la derecha en cero.
        std::vector<float> l(480), rr(480);
        float* planes[2] = {l.data(), rr.data()};
        double ph = 0;
        for (int b2 = 0; b2 < 20; ++b2) {
            for (int i = 0; i < 480; ++i) {
                l[static_cast<size_t>(i)] = rr[static_cast<size_t>(i)] =
                    0.5f * static_cast<float>(std::sin(ph));
                ph += 2 * M_PI * 1000 / 48000;
            }
            r2.process(planes, 2, 480);
        }
        const json m = r2.meters(an2);
        CHECK(m.contains("entradaDb") && m["entradaDb"].get<double>() > -20,
              "el canal conectado tiene señal");
        // Guardar y cargar conserva los canales.
        AudioRack r3(lib, &svc, 48000);
        CHECK(r3.load(r2.save(), &err), "carga cables por canal");
        bool ok2 = false;
        // OJO: `describe()` devuelve un temporario; hay que guardarlo antes de
        // recorrer un subobjeto (si no, la referencia queda colgando).
        const json d3 = r3.describe();
        for (const auto& c : d3["cables"])
            if (c.value("hacia", "") == an2)
                ok2 = c.value("canalSalida", -1) == 1 && c.value("canalEntrada", -1) == 0;
        CHECK(ok2, "los canales vuelven del archivo");
        // Un cable de puerto entero reemplaza al de canal suelto.
        CHECK(r2.connect("in", "out", an2, "in", &err), "puerto entero");
        int cables = 0;
        const json d2 = r2.describe();
        for (const auto& c : d2["cables"])
            if (c.value("hacia", "") == an2) ++cables;
        CHECK(cables == 1, "no quedan dos cables sumando lo mismo");
    }

    // Sobrevive a guardar y cargar.
    AudioRack rack3(lib, &svc, 48000);
    CHECK(rack3.load(rack.save(), &err), "carga un rack con salida de audio");
    CHECK(rack3.hasOutput() && rack3.outputDevice() == "dev-1",
          "la salida y su dispositivo vuelven del archivo");
    (void)setByName;

    if (g_failures == 0) std::printf("test_audio_rack: OK\n");
    return g_failures == 0 ? 0 : 1;
}
