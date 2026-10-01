// src/app/tests/test_habla.cpp — de texto a voz con una voz de piper de verdad
//
// BA_VOZ_TTS=<carpeta de la voz bajada> y BA_VOZ_TTS_MODELO=<el .onnx>; sin
// eso se saltea (77). Revisa que salga audio frase por frase (la primera antes
// que el resto), que callar() corte, y escribe el wav en BA_TEST_TMP.

#include "audio/Habla.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using ba::app::audio::Habla;

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

void escribirWav(const std::string& ruta, const std::vector<float>& m, std::uint32_t f) {
    std::ofstream o(ruta, std::ios::binary);
    const std::uint32_t datos = static_cast<std::uint32_t>(m.size() * 2), tam = 36 + datos, fmt = 16,
                        br = f * 2;
    const std::uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    o.write("RIFF", 4).write(reinterpret_cast<const char*>(&tam), 4).write("WAVEfmt ", 8);
    o.write(reinterpret_cast<const char*>(&fmt), 4).write(reinterpret_cast<const char*>(&pcm), 2);
    o.write(reinterpret_cast<const char*>(&ch), 2).write(reinterpret_cast<const char*>(&f), 4);
    o.write(reinterpret_cast<const char*>(&br), 4).write(reinterpret_cast<const char*>(&ba), 2);
    o.write(reinterpret_cast<const char*>(&bits), 2)
        .write("data", 4)
        .write(reinterpret_cast<const char*>(&datos), 4);
    for (float x : m) {
        const auto s = static_cast<std::int16_t>(std::max(-1.0f, std::min(1.0f, x)) * 32767.0f);
        o.write(reinterpret_cast<const char*>(&s), 2);
    }
}

} // namespace

int main() {
    const char* dir = std::getenv("BA_VOZ_TTS");
    const char* modelo = std::getenv("BA_VOZ_TTS_MODELO");
    if (dir == nullptr || modelo == nullptr) {
        std::printf("test_habla: sin BA_VOZ_TTS/BA_VOZ_TTS_MODELO, se saltea\n");
        return 77;
    }
    Habla h;
    Habla::Voz v;
    v.modelo = std::string(dir) + "/" + modelo;
    v.tokens = std::string(dir) + "/tokens.txt";
    v.datos = std::string(dir) + "/espeak-ng-data";
    std::mutex mu;
    std::condition_variable cv;
    bool cargo = false;
    std::string err;
    h.cargar(v, [&](const std::string& e) {
        std::lock_guard<std::mutex> l(mu);
        err = e;
        cargo = true;
        cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(mu);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return cargo; });
    }
    CHECK(cargo && err.empty() && h.cargada(), "carga la voz");

    std::vector<float> audio;
    std::uint32_t frecuencia = 0;
    int frases = 0;
    double msPrimera = 0;
    bool fin = false;
    const auto t0 = std::chrono::steady_clock::now();
    h.decir(
        "Listo, bajé el micrófono del invitado dos. ¿Querés que también lo saque del aire? Avisame.", 0, 1.0f,
        [&](const float* m, std::size_t n, std::uint32_t f) {
            std::lock_guard<std::mutex> l(mu);
            if (frases++ == 0)
                msPrimera =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            audio.insert(audio.end(), m, m + n);
            frecuencia = f;
        },
        [&]() {
            std::lock_guard<std::mutex> l(mu);
            fin = true;
            cv.notify_all();
        });
    {
        std::unique_lock<std::mutex> l(mu);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return fin; });
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double seg = frecuencia > 0 ? static_cast<double>(audio.size()) / frecuencia : 0;
    std::printf("   %d frases, %.1f s de audio a %u Hz; la primera a los %.0f ms, todo en %.0f ms\n", frases,
                seg, frecuencia, msPrimera, ms);
    CHECK(fin && seg > 2.0, "sale audio");
    CHECK(frases >= 2, "frase por frase");
    CHECK(msPrimera < ms, "la primera llega antes que el resto");
    escribirWav(std::string(BA_TEST_TMP) + "/test_habla.wav", audio, frecuencia);

    // callar() corta lo que esperaba.
    fin = false;
    int despues = 0;
    h.decir(
        "Esta frase es larga y no tendría que terminar de decirse nunca, porque se calla antes.", 0, 1.0f,
        [&](const float*, std::size_t, std::uint32_t) { ++despues; },
        [&]() {
            std::lock_guard<std::mutex> l(mu);
            fin = true;
            cv.notify_all();
        });
    h.callar();
    {
        std::unique_lock<std::mutex> l(mu);
        cv.wait_for(l, std::chrono::seconds(5), [&] { return fin; });
    }
    CHECK(despues == 0, "callar descarta lo pendiente");

    std::printf(g_failures == 0 ? "test_habla: OK\n" : "test_habla: %d fallos\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
