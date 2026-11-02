// ============================================================================
// src/gateway/tests/test_fundido.cpp — de la baja a la HD con un fundido (Fundido.hpp)
//
//   1. La curva: 0 al empezar, 1 al terminar, suave en el medio; sin duración, de golpe.
//   2. mezclarBgra: a 0 queda lo que se va, a 1 lo nuevo, a 0,5 el promedio.
//   3. Fundido: sin empezar no toca nada; empezado, mezcla con el anterior (escalado al
//      tamaño del cuadro nuevo: la baja es más chica que la HD); un cuadro «anterior»
//      lo refresca; al terminar se suelta y no toca más.
// Lo usan MediaPump (el canal de la cámara) y Grillas (el retrato: test_grillas, punto 10).
// ============================================================================

#include <cstdio>
#include <cstring>
#include <vector>

#include "Fundido.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

std::vector<std::uint8_t> liso(int w, int h, std::uint32_t c) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) std::memcpy(px.data() + i, &c, 4);
    return px;
}

std::uint32_t pixel(const std::vector<std::uint8_t>& l, int w, int x, int y) {
    std::uint32_t v = 0;
    std::memcpy(&v, l.data() + (static_cast<std::size_t>(y) * w + x) * 4, 4);
    return v;
}

} // namespace

int main() {
    // 1. La curva.
    CHECK(alfaFundido(100, 300, 100) == 0.0f, "al empezar, nada de lo nuevo");
    CHECK(alfaFundido(100, 300, 400) == 1.0f && alfaFundido(100, 300, 9000) == 1.0f, "al terminar, todo lo nuevo");
    CHECK(alfaFundido(100, 300, 250) == 0.5f, "a mitad, la mitad");
    CHECK(alfaFundido(100, 0, 100) == 1.0f, "sin duración: de golpe");

    // 2. La mezcla.
    {
        auto nuevo = liso(4, 2, 0xFF0000FFu); // azul
        const auto viejo = liso(4, 2, 0xFFFF0000u); // rojo
        auto a = nuevo;
        mezclarBgra(a.data(), 16, viejo.data(), 16, 4, 2, 0.0f);
        CHECK(pixel(a, 4, 1, 1) == 0xFFFF0000u, "a 0: lo que se va");
        a = nuevo;
        mezclarBgra(a.data(), 16, viejo.data(), 16, 4, 2, 1.0f);
        CHECK(pixel(a, 4, 1, 1) == 0xFF0000FFu, "a 1: lo nuevo");
        a = nuevo;
        mezclarBgra(a.data(), 16, viejo.data(), 16, 4, 2, 0.5f);
        const std::uint32_t m = pixel(a, 4, 3, 0);
        CHECK(((m >> 16) & 0xFF) >= 126 && ((m >> 16) & 0xFF) <= 129 && (m & 0xFF) >= 126 && (m & 0xFF) <= 129 &&
                  (m >> 24) == 0xFF,
              "a 0,5: el promedio, opaco");
    }

    // 3. Fundido.
    {
        Fundido f;
        auto hd = liso(64, 36, 0xFF0000FFu);
        CHECK(!f.aplicar(hd.data(), 64 * 4, 64, 36, 0) && pixel(hd, 64, 10, 10) == 0xFF0000FFu, "sin empezar: no toca");
        const auto baja = liso(16, 9, 0xFFFF0000u);
        f.empezar(baja.data(), 16, 9, 200, 1000);
        CHECK(f.activo(1000) && f.alfa(1000) == 0.0f, "empezado");
        CHECK(f.aplicar(hd.data(), 64 * 4, 64, 36, 1000) && pixel(hd, 64, 63, 35) == 0xFFFF0000u,
              "al empezar se ve la baja (escalada al tamaño de la HD), en todo el cuadro");
        hd = liso(64, 36, 0xFF0000FFu);
        const auto baja2 = liso(16, 9, 0xFF00FF00u); // la baja siguió llegando: verde
        f.anterior(baja2.data(), 16, 9);
        f.aplicar(hd.data(), 64 * 4, 64, 36, 1100);
        const std::uint32_t m = pixel(hd, 64, 32, 18);
        CHECK(((m >> 8) & 0xFF) > 60 && (m & 0xFF) > 60 && ((m >> 16) & 0xFF) == 0, "a mitad: la baja de ahora (verde) con la HD");
        hd = liso(64, 36, 0xFF0000FFu);
        CHECK(!f.aplicar(hd.data(), 64 * 4, 64, 36, 1200) && !f.activo(1200), "terminado: se suelta");
        CHECK(pixel(hd, 64, 32, 18) == 0xFF0000FFu && f.imagen().empty(), "y no toca más (ni guarda nada)");
        f.empezar(nullptr, 0, 0, 200, 0);
        CHECK(!f.activo(0), "sin imagen de antes no hay fundido (no hay de dónde fundir)");
    }

    if (g_fallos == 0) std::printf("test_fundido: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
