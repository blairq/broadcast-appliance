// test_grabador_wav.cpp — grabar lo que le llega a la tarjeta de salida en .wav
//
// El encabezado (IEEE float, estéreo, 48 kHz) con los tamaños bien
// completados al cerrar, los datos exactos y en orden (L R intercalados), y
// el corte en partes antes del tope (un WAV no puede pasar de 4 GB).

#include "audio/GrabadorWav.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using ba::app::audio::GrabadorWav;
namespace fs = std::filesystem;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

std::uint32_t leer32(const std::vector<char>& b, std::size_t i) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + i, 4);
    return v;
}
std::uint16_t leer16(const std::vector<char>& b, std::size_t i) {
    std::uint16_t v = 0;
    std::memcpy(&v, b.data() + i, 2);
    return v;
}
std::vector<char> leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ba-test-grabador";
    fs::remove_all(dir);

    // 1. Un archivo: 1000 frames con L = i, R = -i.
    {
        GrabadorWav g(dir / "Mic-uno", 48000);
        CHECK(g.empezar().empty() && g.grabando(), "empieza");
        std::vector<float> l(250), r(250);
        for (int b = 0; b < 4; ++b) {
            for (int i = 0; i < 250; ++i) {
                l[i] = static_cast<float>(b * 250 + i);
                r[i] = -l[i];
            }
            g.agregar(l.data(), r.data(), 250);
        }
        g.terminar();
        CHECK(!g.grabando(), "termina");
        g.agregar(l.data(), r.data(), 250); // después de terminar no hace nada
        const auto b = leer(dir / "Mic-uno.wav");
        CHECK(b.size() == 4 + 4 + 4 + 24 + 12 + 8 + 1000 * 8,
              "tamaño: encabezado + 1000 frames estéreo float");
        if (b.size() >= 60) {
            CHECK(std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "WAVE", 4) == 0,
                  "RIFF/WAVE");
            CHECK(leer32(b, 4) == b.size() - 8, "tamaño RIFF completado al cerrar");
            CHECK(leer16(b, 20) == 3 && leer16(b, 22) == 2 && leer32(b, 24) == 48000 && leer16(b, 34) == 32,
                  "formato: float, estéreo, 48 kHz, 32 bits");
            CHECK(std::memcmp(b.data() + 48, "data", 4) == 0 && leer32(b, 52) == 8000, "tamaño de datos");
            float muestra[2];
            std::memcpy(muestra, b.data() + 56 + 777 * 8, 8);
            CHECK(muestra[0] == 777.0f && muestra[1] == -777.0f, "los datos, en orden y sin tocar");
        }
    }

    // 2. Con tope chico: sigue en «-parte2.wav», «-parte3.wav».
    {
        GrabadorWav g(dir / "Sala", 48000);
        g.setTope(8 * 1000); // 1000 frames por archivo
        CHECK(g.empezar().empty(), "empieza");
        std::vector<float> l(2500, 0.5f), r(2500, -0.5f);
        g.agregar(l.data(), r.data(), 2500);
        g.terminar();
        CHECK(fs::exists(dir / "Sala.wav") && fs::exists(dir / "Sala-parte2.wav") &&
                  fs::exists(dir / "Sala-parte3.wav"),
              "corta en partes");
        const auto p1 = leer(dir / "Sala.wav"), p3 = leer(dir / "Sala-parte3.wav");
        CHECK(p1.size() >= 56 && leer32(p1, 52) == 8000, "la parte 1 llena hasta el tope");
        CHECK(p3.size() >= 56 && leer32(p3, 52) == 500 * 8, "la parte 3 con lo que sobró");
        CHECK(g.segundos() > 0.052 && g.segundos() < 0.0522, "cuenta el total");
    }
    fs::remove_all(dir);
    std::printf(fallos == 0 ? "test_grabador_wav: OK\n" : "test_grabador_wav: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
