// src/app/tests/test_sincronia.cpp — medir el desfase entre dos entradas con datos sintéticos

#include "video/Sincronia.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using ba::app::video::medirDesfase;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

constexpr std::uint64_t kPeriodo = 33'333'333; // 30 fps
constexpr size_t kLado = 144;

/// Una escena que se mueve: manchas que caminan por la grilla.
std::vector<std::uint8_t> escena(int cuadros, std::mt19937& rng) {
    std::vector<std::uint8_t> g(static_cast<size_t>(cuadros) * kLado);
    std::uniform_real_distribution<float> u(0, 1);
    float x = 3, y = 4, vx = 0.7f, vy = 0.4f;
    for (int c = 0; c < cuadros; ++c) {
        if (u(rng) < 0.05f) vx = (u(rng) - 0.5f) * 2, vy = (u(rng) - 0.5f) * 1.5f;
        x += vx, y += vy;
        if (x < 0 || x > 15) vx = -vx;
        if (y < 0 || y > 8) vy = -vy;
        for (int j = 0; j < 144; ++j) {
            const float dx = static_cast<float>(j % 16) - x, dy = static_cast<float>(j / 16) - y;
            g[static_cast<size_t>(c) * kLado + static_cast<size_t>(j)] =
                static_cast<std::uint8_t>(40 + 180 * std::exp(-(dx * dx + dy * dy) / 6));
        }
    }
    return g;
}

/// B = A atrasada `atraso` cuadros, con ruido.
void armar(const std::vector<std::uint8_t>& base, int cuadros, int atraso, std::mt19937& rng,
           std::vector<std::uint64_t>& ta, std::vector<std::uint8_t>& ga, std::vector<std::uint64_t>& tb,
           std::vector<std::uint8_t>& gb) {
    std::normal_distribution<float> ruido(0, 3);
    const int n = cuadros - 60;
    for (int c = 0; c < n; ++c) {
        const std::uint64_t t = 1'000'000'000ULL + static_cast<std::uint64_t>(c) * kPeriodo;
        ta.push_back(t);
        tb.push_back(t);
        const int ca = c + 30, cb = c + 30 - atraso;
        for (size_t j = 0; j < kLado; ++j) {
            ga.push_back(base[static_cast<size_t>(ca) * kLado + j]);
            const float v = static_cast<float>(base[static_cast<size_t>(cb) * kLado + j]) + ruido(rng);
            gb.push_back(static_cast<std::uint8_t>(std::fmin(255.0f, std::fmax(0.0f, v))));
        }
    }
}
} // namespace

int main() {
    std::mt19937 rng(7);
    const int cuadros = 400;
    const auto base = escena(cuadros, rng);

    // La misma imagen por dos caminos, B 6 cuadros (200 ms) atrás.
    {
        std::vector<std::uint64_t> ta, tb;
        std::vector<std::uint8_t> ga, gb;
        armar(base, cuadros, 6, rng, ta, ga, tb, gb);
        const auto m = medirDesfase(ta, ga, tb, gb);
        std::printf("   misma imagen, B +200 ms: %s %.1f ms (confianza %.2f, %s)\n",
                    m.ok ? "ok" : m.motivo.c_str(), m.desfaseMs, m.confianza, m.metodo.c_str());
        CHECK(m.ok && std::fabs(m.desfaseMs - 200) < 20 && m.metodo == "espacial",
              "misma imagen: mide 200 ms (espacial)");
        // Al revés: A atrasa, el desfase da negativo.
        const auto r = medirDesfase(tb, gb, ta, ga);
        CHECK(r.ok && std::fabs(r.desfaseMs + 200) < 20, "al revés da -200 ms");
    }
    // Dos cámaras distintas que solo comparten CUÁNDO cambia (un flash): la energía.
    {
        std::vector<std::uint64_t> ta, tb;
        std::vector<std::uint8_t> ga, gb;
        std::normal_distribution<float> ruido(0, 2);
        std::uniform_real_distribution<float> u(0, 1);
        std::vector<float> flash(cuadros, 0);
        for (int c = 0; c < cuadros; ++c)
            if (u(rng) < 0.04f) flash[static_cast<size_t>(c)] = 60 + 60 * u(rng);
        for (int c = 0; c < cuadros - 60; ++c) {
            const std::uint64_t t = 1'000'000'000ULL + static_cast<std::uint64_t>(c) * kPeriodo;
            ta.push_back(t);
            tb.push_back(t);
            for (size_t j = 0; j < kLado; ++j) {
                // A: el flash en la mitad de arriba; B (otro encuadre, 4 cuadros tarde): abajo.
                const float fa = j < 72 ? flash[static_cast<size_t>(c + 30)] : 0;
                const float fb = j >= 72 ? flash[static_cast<size_t>(c + 30 - 4)] : 0;
                ga.push_back(static_cast<std::uint8_t>(std::fmin(255.f, 80 + fa + ruido(rng))));
                gb.push_back(static_cast<std::uint8_t>(std::fmin(255.f, 120 + fb + ruido(rng))));
            }
        }
        const auto m = medirDesfase(ta, ga, tb, gb);
        std::printf("   dos cámaras, un flash, B +133 ms: %s %.1f ms (confianza %.2f, %s)\n",
                    m.ok ? "ok" : m.motivo.c_str(), m.desfaseMs, m.confianza, m.metodo.c_str());
        CHECK(m.ok && std::fabs(m.desfaseMs - 133.3) < 25 && m.metodo == "energia",
              "dos cámaras: mide 133 ms (energía)");
    }
    // Imagen quieta: no hay nada que medir, y lo dice.
    {
        std::vector<std::uint64_t> ta, tb;
        std::vector<std::uint8_t> ga, gb;
        for (int c = 0; c < 100; ++c) {
            ta.push_back(static_cast<std::uint64_t>(c) * kPeriodo);
            tb.push_back(static_cast<std::uint64_t>(c) * kPeriodo);
            for (size_t j = 0; j < kLado; ++j)
                ga.push_back(100), gb.push_back(100);
        }
        const auto m = medirDesfase(ta, ga, tb, gb);
        CHECK(!m.ok && m.motivo.find("quieta") != std::string::npos,
              "imagen quieta: no mide y explica por qué");
    }
    std::printf(fallos == 0 ? "test_sincronia: OK\n" : "test_sincronia: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
