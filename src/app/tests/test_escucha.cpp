// src/app/tests/test_escucha.cpp — la voz del operador a texto, con modelos de verdad
//
// Necesita los modelos bajados (no viajan en el repo): BA_VOZ_MODELO=<carpeta
// del reconocedor Parakeet>, BA_VOZ_VAD=<silero_vad.onnx> y BA_VOZ_WAV=<un wav
// en castellano, 16 bits mono>. Sin eso se saltea (77).
//
// Le pasa el wav en bloques de 10 ms, a su frecuencia (Escucha remuestrea a
// 16 kHz, como hace con los 48 kHz de libobs) y con un segundo de silencio
// antes y después (lo que pasa al apretar la tecla antes de hablar). Revisa que el VAD recorte el silencio y
// que el texto tenga las palabras del wav.

#include "audio/Escucha.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using ba::app::audio::Escucha;

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

/// PCM de 16 bits mono de un .wav simple (busca el bloque "data").
bool leerWav(const char* ruta, std::vector<float>& out, int& frecuencia) {
    std::ifstream f(ruta, std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0) return false;
    std::memcpy(&frecuencia, b.data() + 24, 4);
    for (std::size_t i = 12; i + 8 <= b.size();) {
        std::uint32_t tam = 0;
        std::memcpy(&tam, b.data() + i + 4, 4);
        if (std::memcmp(b.data() + i, "data", 4) == 0) {
            const auto* p = reinterpret_cast<const std::int16_t*>(b.data() + i + 8);
            for (std::size_t k = 0; k < tam / 2 && i + 8 + k * 2 + 1 < b.size(); ++k)
                out.push_back(p[k] / 32768.0f);
            return true;
        }
        i += 8 + tam;
    }
    return false;
}

} // namespace

int main() {
    const char* dir = std::getenv("BA_VOZ_MODELO");
    const char* vad = std::getenv("BA_VOZ_VAD");
    const char* wav = std::getenv("BA_VOZ_WAV");
    if (dir == nullptr || vad == nullptr || wav == nullptr) {
        std::printf("test_escucha: sin BA_VOZ_MODELO/BA_VOZ_VAD/BA_VOZ_WAV, se saltea\n");
        return 77;
    }
    std::vector<float> voz;
    int fr = 0;
    CHECK(leerWav(wav, voz, fr) && fr >= 8000, "el wav de prueba");

    Escucha e;
    Escucha::Modelos m;
    const std::string d = dir;
    m.encoder = d + "/encoder.int8.onnx";
    m.decoder = d + "/decoder.int8.onnx";
    m.joiner = d + "/joiner.int8.onnx";
    m.tokens = d + "/tokens.txt";
    m.vad = vad;
    std::mutex mu;
    std::condition_variable cv;
    bool cargo = false;
    std::string errorCarga;
    e.cargar(m, [&](const std::string& err) {
        std::lock_guard<std::mutex> l(mu);
        errorCarga = err;
        cargo = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(mu);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return cargo; });
    }
    CHECK(cargo && errorCarga.empty() && e.cargado(), "carga el reconocedor y el VAD");

    // En bloques de 10 ms, con un segundo de silencio antes y después.
    const auto f = static_cast<std::uint32_t>(fr);
    const std::size_t bloque = f / 100;
    std::vector<float> toma(f, 0.0f);
    toma.insert(toma.end(), voz.begin(), voz.end());
    toma.insert(toma.end(), f, 0.0f);
    e.alimentar(toma.data(), bloque, f);
    CHECK(!e.grabando(), "fuera de una toma no graba");
    e.empezar();
    for (std::size_t i = 0; i < toma.size(); i += bloque)
        e.alimentar(toma.data() + i, std::min(bloque, toma.size() - i), f);

    Escucha::Resultado r;
    bool listo = false;
    e.terminar([&](const Escucha::Resultado& x) {
        std::lock_guard<std::mutex> l(mu);
        r = x;
        listo = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(mu);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return listo; });
    }
    std::printf("   «%s» (%.1f s de habla, %.0f ms)%s%s\n", r.texto.c_str(), r.segundosDeAudio, r.ms,
                r.error.empty() ? "" : " error: ", r.error.c_str());
    CHECK(listo && r.error.empty(), "transcribe");
    CHECK(r.texto.find("pa") != std::string::npos && r.texto.find("hacer") != std::string::npos,
          "el texto tiene las palabras del wav");
    const double original = static_cast<double>(voz.size()) / fr;
    CHECK(r.segundosDeAudio < original + 1.0, "el VAD recorta los dos segundos de silencio");

    e.empezar();
    e.cancelar();
    CHECK(!e.grabando(), "cancelar");

    std::printf(g_failures == 0 ? "test_escucha: OK\n" : "test_escucha: %d fallos\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
