// ============================================================================
// src/app/evento/TemaLogoPng.cpp — ver TemaLogoPng.hpp
// ============================================================================
#include "evento/TemaLogoPng.hpp"

#include "persist/Escritor.hpp"
#include "tema/TemaWeb.hpp"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace ba::evento {

namespace fs = std::filesystem;

bool dibujarPngDeLogo(const fs::path& svg, const fs::path& png, int lado) {
    QFile f(QString::fromStdWString(svg.wstring()));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QSvgRenderer r(f.readAll());
    if (!r.isValid()) return false;
    // La proporción del SVG (viewBox o su tamaño); sin ninguno, cuadrado.
    QSizeF t = r.viewBoxF().size();
    if (t.isEmpty()) t = r.defaultSize();
    if (t.isEmpty()) t = QSizeF(1, 1);
    const QSize px = t.scaled(lado, lado, Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    p.end();
    // Con el escritor único (atómico): el gateway puede estar leyéndolo para un QR.
    QByteArray bytes;
    QBuffer buf(&bytes);
    if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, "PNG")) return false;
    return ba::persist::escribir(png, std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

int asegurarPngDeLogos(const fs::path& carpetaEvento, const TemaEvento& t) {
    const fs::path dir = carpetaTema(carpetaEvento);
    int n = 0;
    for (const std::string* a : {&t.marca.logo, &t.marca.logoClaro, &t.marca.favicon}) {
        const std::string png = ba::tema::pngDeLogo(*a);
        std::error_code ec;
        if (png.empty() || fs::exists(dir / png, ec) || !fs::is_regular_file(dir / *a, ec)) continue;
        if (dibujarPngDeLogo(dir / *a, dir / png)) ++n;
    }
    return n;
}

} // namespace ba::evento
