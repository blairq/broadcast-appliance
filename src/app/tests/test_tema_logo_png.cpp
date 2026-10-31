// test_tema_logo_png.cpp — el PNG de un logo SVG del evento (TemaLogoPng)
//
// El QR que dibuja la app es un SVG con el logo en un <image>, y QtSvg no pinta un SVG
// adentro de otro. Que al poner un logo SVG quede su PNG en Tema/ (con la proporción del
// logo y fondo transparente), que el QR lo use (logoParaQr) y QtSvg lo pinte de verdad,
// que no se dibuje dos veces, y que el PNG se vaya con su logo (limpiarRecursos).
#include "evento/TemaLogoPng.hpp"
#include "tema/TemaWeb.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
int fallos = 0;
void check(bool ok, const char* que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que);
    if (!ok) ++fallos;
}
void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
bool rojo(const QColor& c) { return c.alpha() > 200 && c.red() > 200 && c.green() < 60 && c.blue() < 60; }

/// Un QR de mentira como el que arma la app: fondo blanco y el logo al centro en un <image>.
QImage qrCon(const std::string& dataUrl) {
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                            "viewBox=\"0 0 100 100\" width=\"100\" height=\"100\"><rect width=\"100\" height=\"100\" "
                            "fill=\"#ffffff\"/><image x=\"30\" y=\"30\" width=\"40\" height=\"40\" xlink:href=\"" +
                            dataUrl + "\" href=\"" + dataUrl + "\"/></svg>";
    QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QSvgRenderer(QByteArray::fromStdString(svg)).render(&p);
    p.end();
    return img;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const fs::path ev = fs::temp_directory_path() / "ba-test-tema-logo-png";
    fs::remove_all(ev);
    const fs::path tema = ba::evento::carpetaTema(ev);
    // Un logo apaisado: un rectángulo rojo de 200×100.
    escribir(tema / "logo-1a2b3c.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 100\">"
                                       "<rect x=\"0\" y=\"0\" width=\"200\" height=\"100\" fill=\"#ff0000\"/></svg>");

    check(ba::tema::pngDeLogo("logo-1a2b3c.svg") == "logo-1a2b3c.png" && ba::tema::pngDeLogo("logo-1a2b3c.png").empty() &&
              ba::tema::pngDeLogo("").empty(),
          "el nombre del PNG: el del SVG con .png; nada para lo que no es SVG");

    ba::evento::TemaEvento t;
    t.existe = true;
    t.marca.logo = "logo-1a2b3c.svg";
    check(ba::evento::asegurarPngDeLogos(ev, t) == 1 && fs::exists(tema / "logo-1a2b3c.png"), "al ponerlo, su PNG en Tema/");
    const QImage png(QString::fromStdString((tema / "logo-1a2b3c.png").string()));
    check(png.width() == ba::evento::kLadoPngDeLogo && png.height() == ba::evento::kLadoPngDeLogo / 2,
          "con la proporción del logo y el lado mayor de 512");
    check(rojo(png.pixelColor(png.width() / 2, png.height() / 2)), "dibujado de verdad");
    check(ba::evento::asegurarPngDeLogos(ev, t) == 0, "no se vuelve a dibujar si ya está");

    // El QR: con el SVG adentro QtSvg no pinta nada; con el PNG sí.
    const std::string u = ba::tema::logoParaQr(tema, t.marca.logo);
    check(u.rfind("data:image/png;base64,", 0) == 0, "el QR usa el PNG");
    check(rojo(qrCon(u).pixelColor(50, 50)), "y QtSvg lo pinta al centro del QR");
    const std::string crudo = ba::tema::imagenComoDataUrl(tema / t.marca.logo);
    std::printf("(el SVG adentro del QR, sin el PNG: %s)\n", rojo(qrCon(crudo).pixelColor(50, 50)) ? "lo pinta" : "no lo pinta");
    check(ba::tema::logoParaQr(tema, "logo-no-esta.svg").empty() && ba::tema::logoParaQr(tema, "").empty(), "sin logo, nada");

    // Un PNG o un WebP van tal cual (no tienen PNG aparte).
    escribir(tema / "logo-9f9f.png", std::string("\x89PNG\r\n\x1a\n", 8));
    check(ba::tema::logoParaQr(tema, "logo-9f9f.png").rfind("data:image/png;base64,", 0) == 0, "un PNG, tal cual");

    // Un SVG que QtSvg no entiende: no queda un PNG roto.
    escribir(tema / "logo-roto.svg", "esto no es un svg");
    ba::evento::TemaEvento r = t;
    r.marca.logo = "logo-roto.svg";
    check(ba::evento::asegurarPngDeLogos(ev, r) == 0 && !fs::exists(tema / "logo-roto.png"), "un SVG ilegible: sin PNG");
    check(ba::tema::logoParaQr(tema, "logo-roto.svg").rfind("data:image/svg+xml", 0) == 0, "y el QR prueba con el SVG");

    // El PNG se va con su logo, no antes.
    ba::evento::limpiarRecursos(ev, t);
    check(fs::exists(tema / "logo-1a2b3c.svg") && fs::exists(tema / "logo-1a2b3c.png"), "mientras el logo está, su PNG también");
    ba::evento::limpiarRecursos(ev, ba::evento::TemaEvento{});
    check(!fs::exists(tema / "logo-1a2b3c.svg") && !fs::exists(tema / "logo-1a2b3c.png"), "se va con su logo");

    fs::remove_all(ev);
    std::printf(fallos == 0 ? "test_tema_logo_png: todo bien\n" : "test_tema_logo_png: %d fallas\n", fallos);
    return fallos == 0 ? 0 : 1;
}
