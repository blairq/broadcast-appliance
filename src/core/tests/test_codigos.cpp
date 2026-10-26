// ============================================================================
// src/core/tests/test_codigos.cpp — el lector de códigos, de punta a punta
//
// Genera con zxing-cpp un PDF417 con un DNI argentino SINTÉTICO, lo pega en un
// «cuadro de cámara» BGRA más grande (fondo gris, agrandado, corrido), lo lee de
// vuelta y lo pasa por el catálogo de tipos: tiene que salir el mismo DNI. Y
// además: el mismo cuadro espejado (la cámara de adelante de una tablet), un QR,
// una imagen sin nada, y que los formatos que no se piden no se lean.
// ============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "documentos/Documento.hpp"
#include "documentos/lector/LectorCodigos.hpp"

using namespace ba::documentos;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

const char* kDni = "00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016@274";

/// Un cuadro BGRA de ancho x alto, gris medio, con el código (gris) agrandado
/// `escala` veces en (x0, y0). Si `espejado`, todo dado vuelta de izquierda a derecha.
std::vector<std::uint8_t> cuadro(const std::vector<std::uint8_t>& g, int gw, int gh, int ancho, int alto, int x0, int y0,
                                 int escala, bool espejado = false) {
    std::vector<std::uint8_t> f(static_cast<std::size_t>(ancho) * alto * 4, 128);
    for (int y = 0; y < alto; ++y)
        for (int x = 0; x < ancho; ++x) {
            const int gx = (x - x0) / escala, gy = (y - y0) / escala;
            if (x < x0 || y < y0 || gx >= gw || gy >= gh) continue;
            const std::uint8_t v = g[static_cast<std::size_t>(gy) * gw + gx];
            const int xx = espejado ? ancho - 1 - x : x;
            std::uint8_t* p = &f[(static_cast<std::size_t>(y) * ancho + xx) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    return f;
}

void dniDePuntaAPunta() {
    std::vector<std::uint8_t> g;
    int gw = 0, gh = 0;
    std::string err;
    CHECK(generarCodigo("PDF417", kDni, 400, 120, g, gw, gh, &err), "genera el PDF417");
    if (g.empty()) {
        std::printf("  (%s)\n", err.c_str());
        return;
    }
    const int W = 1280, H = 720;
    const auto f = cuadro(g, gw, gh, W, H, 150, 200, 2);
    const auto codigos = leerCodigos(f.data(), W, H);
    CHECK(codigos.size() == 1, "un código en el cuadro");
    if (codigos.empty()) return;
    CHECK(codigos[0].formato == "PDF417", "es un PDF417");
    CHECK(codigos[0].texto == kDni, "el texto vuelve igual");

    Catalogo cat;
    Datos d;
    CHECK(cat.leer(codigos[0], {"dni-ar"}, d), "el catálogo lo reconoce como DNI");
    CHECK(d.numero == "11222333" && d.apellido == "Prueba" && d.nombres == "Juana Andrea", "los datos del DNI");
    CHECK(d.cuil == "27-11222333-4" && d.nacimiento == "1990-03-15", "CUIL y nacimiento");

    // Espejado (la cámara de adelante de una tablet): zxing lo prueba solo.
    const auto e = cuadro(g, gw, gh, W, H, 150, 200, 2, true);
    const auto ce = leerCodigos(e.data(), W, H);
    CHECK(ce.size() == 1 && ce[0].texto == kDni, "lo lee espejado");

    // Si no se pide PDF417, no se lee.
    OpcionesLector soloQr;
    soloQr.formatos = {"QRCode"};
    CHECK(leerCodigos(f.data(), W, H, 0, soloQr).empty(), "solo QR no lee el PDF417");

    // Una fila con bytes de relleno (como un QImage con stride).
    const int fila = W * 4 + 64;
    std::vector<std::uint8_t> conRelleno(static_cast<std::size_t>(fila) * H, 0);
    for (int y = 0; y < H; ++y)
        std::copy(f.begin() + static_cast<std::ptrdiff_t>(y) * W * 4, f.begin() + static_cast<std::ptrdiff_t>(y + 1) * W * 4,
                  conRelleno.begin() + static_cast<std::ptrdiff_t>(y) * fila);
    const auto cr = leerCodigos(conRelleno.data(), W, H, fila);
    CHECK(cr.size() == 1 && cr[0].texto == kDni, "respeta los bytes por fila");
}

void qrYNada() {
    std::vector<std::uint8_t> g;
    int gw = 0, gh = 0;
    CHECK(generarCodigo("QRCode", "https://ejemplo.com/registro", 200, 200, g, gw, gh), "genera un QR");
    const auto f = cuadro(g, gw, gh, 640, 480, 100, 100, 1);
    const auto c = leerCodigos(f.data(), 640, 480);
    CHECK(c.size() == 1 && c[0].formato == "QRCode" && c[0].texto == "https://ejemplo.com/registro", "lee el QR");
    Catalogo cat;
    Datos d;
    CHECK(c.empty() || !cat.leer(c[0], {}, d), "un QR con una url no es un documento");

    std::vector<std::uint8_t> vacio(static_cast<std::size_t>(640) * 480 * 4, 200);
    CHECK(leerCodigos(vacio.data(), 640, 480).empty(), "una imagen sin nada no da códigos");
    CHECK(leerCodigos(nullptr, 0, 0).empty(), "sin imagen no se cae");

    CHECK(formatoValido("PDF417") && formatoValido("DataMatrix") && !formatoValido("Inventado"), "formatos válidos");
    std::string err;
    CHECK(!generarCodigo("Inventado", "x", 10, 10, g, gw, gh, &err) && !err.empty(), "formato que no existe");
}

} // namespace

int main() {
    dniDePuntaAPunta();
    qrYNada();
    if (g_fallos == 0) std::printf("test_codigos: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
