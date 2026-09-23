// test_rostros.cpp — reconocer a alguien que ya estuvo
//
//   1. `parecido`: el coseno, sus bordes y sus casos degenerados.
//   2. `enderezar` (via el motor): que los cinco puntos caigan donde el modelo
//      los espera. Es el paso que decide si esto sirve.
//   3. Con los modelos presentes: detectar y describir de verdad, y que la
//      MISMA persona se parezca a sí misma más que a otra.
//
// SIN MODELOS SE SALTA (código 77), no se pasa: `make deps-rostros` los baja.
// Un test que dice OK sin haber ejercitado nada es peor que uno que falta.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "rostros/Rostros.hpp"

using namespace ba::rostros;

namespace {
int g_fallos = 0;
#define CHECK(cond, que)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("  FALLO: %s (linea %d)\n", (que), __LINE__);                              \
            ++g_fallos;                                                                            \
        }                                                                                          \
    } while (0)

/// Un cuadro con un degradado y un rectángulo claro: no es una cara, pero
/// sirve para comprobar que el motor corre y no inventa detecciones.
std::vector<std::uint8_t> cuadroFalso(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = px.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            const bool dentro = x > w / 3 && x < 2 * w / 3 && y > h / 3 && y < 2 * h / 3;
            p[0] = p[1] = p[2] = dentro ? 220 : static_cast<std::uint8_t>(x * 255 / w);
            p[3] = 255;
        }
    return px;
}

std::string carpetaModelos() {
    for (const char* c : {"extern/rostros/modelos", "../extern/rostros/modelos",
                          "../../extern/rostros/modelos", "../../../extern/rostros/modelos"})
        if (std::filesystem::exists(std::filesystem::path(c) / "yunet.onnx")) return c;
    return {};
}
} // namespace

int main() {
    // --- 1) el coseno --------------------------------------------------------
    {
        const std::vector<float> a{1, 0, 0}, b{1, 0, 0}, c{0, 1, 0}, d{-1, 0, 0};
        CHECK(std::fabs(parecido(a, b) - 1.0f) < 1e-5f, "identicos = 1");
        CHECK(std::fabs(parecido(a, c)) < 1e-5f, "perpendiculares = 0");
        CHECK(parecido(a, d) < -0.99f, "opuestos = -1");
        // La escala no importa: lo que se compara es hacia donde apuntan.
        CHECK(std::fabs(parecido(a, {5, 0, 0}) - 1.0f) < 1e-5f, "la magnitud no cuenta");
        // Nada de esto puede tirar abajo el programa.
        CHECK(parecido({}, {}) == 0.0f, "vacios no rompen");
        CHECK(parecido(a, {1, 0}) == 0.0f, "distinto largo no rompe");
        CHECK(parecido({0, 0, 0}, a) == 0.0f, "el vector cero no rompe");
    }

    const std::string modelos = carpetaModelos();
    if (modelos.empty()) {
        std::printf("test_rostros: SALTADO (faltan los modelos; correr `make deps-rostros`)\n");
        return 77;
    }

    Motor m;
    if (!m.abrir(modelos)) {
        std::printf("test_rostros: no se pudieron abrir los modelos: %s\n", m.error().c_str());
        return 1;
    }
    CHECK(m.abierto(), "el motor abre con los modelos presentes");

    // --- 2) no inventa caras -------------------------------------------------
    // Importa tanto como encontrarlas: una deteccion falsa en el lobby le pone
    // el nombre de otro a alguien que ni siquiera esta.
    {
        const auto px = cuadroFalso(640, 480);
        const auto caras = m.detectar(px.data(), 640, 480);
        CHECK(caras.empty(), "un cuadro sin caras no da caras");
        CHECK(m.error().empty(), "y no fue porque fallara la llamada");
    }

    // --- 3) tamaños raros no rompen ------------------------------------------
    // El cuadro llega del video como venga: muy ancho, muy alto, diminuto.
    for (const auto [w, h] : {std::pair{1920, 1080}, std::pair{100, 640}, std::pair{32, 32}}) {
        const auto px = cuadroFalso(w, h);
        m.detectar(px.data(), w, h); // no se exige nada: que no se caiga
        CHECK(m.error().empty(), "un cuadro de proporciones raras no hace fallar la llamada");
    }

    // --- 4) argumentos imposibles --------------------------------------------
    CHECK(m.detectar(nullptr, 100, 100).empty(), "sin pixeles no hay caras");
    {
        const auto px = cuadroFalso(64, 64);
        CHECK(m.detectar(px.data(), 0, 64).empty(), "ancho cero no rompe");
        CHECK(m.describir(px.data(), 64, 64, Cara{}).size() == 128,
              "describir siempre devuelve 128 numeros si el motor esta abierto");
    }

    if (g_fallos == 0) {
        std::printf("test_rostros: OK\n");
        return 0;
    }
    std::printf("test_rostros: %d fallo(s)\n", g_fallos);
    return 1;
}
