// src/app/tests/test_nemotron.cpp — Nemotron 3.5 ASR Streaming de verdad (sherpa-onnx)
//
// Necesita el modelo bajado (no viaja en el repo: ~700 MB): BA_NEMOTRON_MODELO=<la
// carpeta de sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-560ms-int8-2026-06-11>,
// BA_VOZ_VAD=<silero_vad.onnx> y, opcional, BA_VOZ_WAV=<un wav en castellano, 16
// bits mono> (si no, el test_wavs/es.wav del modelo). Sin eso se saltea (77).
//
// Lo que prueba: que esta sherpa-onnx carga el modelo y le entiende el idioma
// por stream, que DOS corrientes a la vez (el mismo wav, 48 kHz, de a 10 ms y
// con silencio antes y después, como llega del lobby) salen con texto y se
// decodificaron en lote, y la toma suelta (el dictado). Imprime el texto y los
// tiempos para mirar la calidad a ojo.

#include "audio/EscuchaNemotron.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using ba::app::audio::EscuchaStreaming;
using ba::app::audio::ModelosNemotron;
using ba::app::audio::Reconocedor;

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

bool leerWav(const std::string& ruta, std::vector<float>& out, int& frecuencia) {
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

bool existe(const std::string& p) { return std::ifstream(p).good(); }

} // namespace

int main() {
    const char* dir = std::getenv("BA_NEMOTRON_MODELO");
    const char* vad = std::getenv("BA_VOZ_VAD");
    if (dir == nullptr || vad == nullptr || !existe(std::string(dir) + "/encoder.int8.onnx")) {
        std::printf("test_nemotron: sin BA_NEMOTRON_MODELO/BA_VOZ_VAD, se saltea\n");
        return 77;
    }
    const std::string d = dir;
    const char* wavEnv = std::getenv("BA_VOZ_WAV");
    const std::string wav = wavEnv != nullptr ? std::string(wavEnv) : d + "/test_wavs/es.wav";
    std::vector<float> voz;
    int fr = 0;
    if (!leerWav(wav, voz, fr) || fr < 8000) {
        std::printf("test_nemotron: no hay wav de prueba (%s), se saltea\n", wav.c_str());
        return 77;
    }
    // A 48 kHz, como llega del lobby (lineal: alcanza para la prueba).
    std::vector<float> v48;
    const double paso = static_cast<double>(fr) / 48000.0;
    for (double p = 0; p + 1 < static_cast<double>(voz.size()); p += paso) {
        const auto i = static_cast<std::size_t>(p);
        const float t = static_cast<float>(p - static_cast<double>(i));
        v48.push_back(voz[i] + (voz[i + 1] - voz[i]) * t);
    }
    std::vector<float> senal(48000, 0.0f); // 1 s de silencio antes
    senal.insert(senal.end(), v48.begin(), v48.end());
    senal.insert(senal.end(), 48000 * 2, 0.0f); // y 2 después

    EscuchaStreaming e;
    std::mutex m;
    std::condition_variable cv;
    bool cargo = false;
    std::string error;
    std::map<std::string, std::string> textos;
    e.setFrase([&](const std::string& clave, const Reconocedor::Resultado& r) {
        std::lock_guard<std::mutex> l(m);
        std::printf("   frase de %s (%.1f s de audio): «%s»\n", clave.c_str(), r.segundosDeAudio, r.texto.c_str());
        textos[clave] += (textos[clave].empty() ? "" : " ") + r.texto;
        cv.notify_all();
    });
    ModelosNemotron mn;
    mn.encoder = d + "/encoder.int8.onnx";
    mn.decoder = d + "/decoder.int8.onnx";
    mn.joiner = d + "/joiner.int8.onnx";
    mn.tokens = d + "/tokens.txt";
    mn.vad = vad;
    const auto t0 = std::chrono::steady_clock::now();
    ba::app::audio::cargarNemotron(e, mn, [&](const std::string& err) {
        std::lock_guard<std::mutex> l(m);
        error = err;
        cargo = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(m);
        cv.wait_for(l, std::chrono::seconds(120), [&] { return cargo; });
    }
    std::printf("   carga: %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    CHECK(cargo && error.empty() && e.cargado(), "carga Nemotron");
    if (!e.cargado()) return 1;

    // Varias corrientes a la vez (2, o BA_NEMOTRON_VOCES), en tiempo «real»
    // acelerado: de a 10 ms, esperando al motor cada 200 ms de audio.
    const char* vocesEnv = std::getenv("BA_NEMOTRON_VOCES");
    const int voces = std::max(2, vocesEnv != nullptr ? std::atoi(vocesEnv) : 2);
    std::vector<std::string> claves;
    for (int k = 0; k < voces; ++k) claves.push_back(k == 0 ? "registro:a" : k == 1 ? "registro:b" : "registro:" + std::to_string(k));
    for (const auto& k : claves) e.abrirContinuo(k);
    const auto t1 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < senal.size(); i += 480) {
        const std::size_t n = std::min<std::size_t>(480, senal.size() - i);
        for (const auto& k : claves) e.alimentarContinuo(k, senal.data() + i, n, 48000);
        if (i % (480 * 20) == 0) e.esperarVuelta(); // para no pasar el tope de 10 s de cada corriente
    }
    e.esperarVuelta();
    {
        std::unique_lock<std::mutex> l(m);
        cv.wait_for(l, std::chrono::seconds(120), [&] { return static_cast<int>(textos.size()) >= voces; });
    }
    const double s = static_cast<double>(senal.size()) / 48000;
    std::printf("   %.1f s de audio x%d en %.1f s\n", s, voces,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count());
    CHECK(static_cast<int>(textos.size()) == voces && !textos["registro:a"].empty(), "todas las corrientes dan texto");
    CHECK(textos["registro:a"] == textos["registro:b"], "el mismo audio da el mismo texto en lote");
    const auto lote = e.lote();
    std::printf("   lote máximo %d, %llu pasos, %.1f s al modelo de %.1f s oídos\n", lote.maximo,
                static_cast<unsigned long long>(lote.pasos), lote.segundosAlModelo, lote.segundosOidos);
    CHECK(lote.maximo == voces, "se decodificaron juntas");
    CHECK(lote.segundosAlModelo < lote.segundosOidos, "el silencio no va al modelo");

    // La toma suelta (el dictado).
    bool hecho = false;
    Reconocedor::Resultado res;
    e.transcribir(senal, 48000, [&](const Reconocedor::Resultado& r) {
        std::lock_guard<std::mutex> l(m);
        res = r;
        hecho = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(m);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return hecho; });
    }
    std::printf("   toma: «%s» (%.1f s de habla en %.0f ms)\n", res.texto.c_str(), res.segundosDeAudio, res.ms);
    CHECK(hecho && res.error.empty() && !res.texto.empty(), "la toma suelta da texto");
    if (g_failures > 0) {
        std::printf("test_nemotron: %d fallas\n", g_failures);
        return 1;
    }
    std::printf("test_nemotron: todo bien\n");
    return 0;
}
