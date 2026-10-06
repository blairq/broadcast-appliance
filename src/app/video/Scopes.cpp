// SPDX-License-Identifier: GPL-2.0-or-later
#include "video/Scopes.hpp"

#include <QPainter>
#include <QPainterPath>

#include <numbers>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ba::app::video {

namespace {

constexpr int kAnchoMuestra = 480; // se cuenta sobre la imagen achicada a esto

QImage muestra(const QImage& imagen) {
    QImage m = imagen.width() > kAnchoMuestra ? imagen.scaledToWidth(kAnchoMuestra, Qt::FastTransformation) : imagen;
    return m.convertToFormat(QImage::Format_RGB32);
}

inline float luma709(int r, int g, int b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

/// Cuentas → brillo, con raíz para que un píxel suelto se vea y una zona plana no queme.
inline int brillo(int n, int maximo) {
    if (n <= 0) return 0;
    return std::min(255, static_cast<int>(40 + 215 * std::sqrt(static_cast<double>(n) / std::max(1, maximo))));
}

const QColor kFondo(10, 12, 14);
const QColor kGrilla(255, 255, 255, 40);
const QColor kTexto(255, 255, 255, 120);
const QColor kIlegal(255, 70, 70, 150);

/// Las líneas de 0/25/50/75/100 % y las de rango legal de una forma de onda en `r`.
void grillaOnda(QPainter& p, const QRect& r) {
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    for (int pct = 0; pct <= 100; pct += 25) {
        const int y = r.bottom() - static_cast<int>(pct / 100.0 * r.height());
        p.setPen(kGrilla);
        p.drawLine(r.left(), y, r.right(), y);
        p.setPen(kTexto);
        p.drawText(r.left() + 2, y - 2, QString::number(pct));
    }
    // Los bordes del rango legal (16 y 235 sobre 255): lo que pase de acá en una
    // entrega a TV lo recorta el legalizador.
    p.setPen(QPen(kIlegal, 1, Qt::DashLine));
    for (int v : {16, 235}) {
        const int y = r.bottom() - static_cast<int>(v / 255.0 * r.height());
        p.drawLine(r.left(), y, r.right(), y);
    }
}

/// Un canal (o la luma) como forma de onda en el rectángulo `r`, del color `c`.
void onda(QImage& out, const QRect& r, const QImage& original, int canal, const QColor& c) {
    const int cols = r.width(), filas = r.height();
    // Una columna de la imagen por columna del scope: si no, quedan rayas vacías
    // (o columnas que suman dos) según cuánto se estire.
    const QImage m = original.width() == cols ? original
                                              : original.scaled(cols, std::max(1, original.height() * cols / std::max(1, original.width())),
                                                                Qt::IgnoreAspectRatio, Qt::FastTransformation);
    std::vector<int> cuentas(static_cast<size_t>(cols) * filas, 0);
    int maximo = 1;
    for (int y = 0; y < m.height(); ++y) {
        const auto* fila = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        for (int x = 0; x < m.width(); ++x) {
            const QRgb px = fila[x];
            const float v = canal == 0 ? qRed(px) : canal == 1 ? qGreen(px) : canal == 2 ? qBlue(px)
                                                                             : luma709(qRed(px), qGreen(px), qBlue(px));
            const int cx = x * cols / m.width();
            const int cy = filas - 1 - std::clamp(static_cast<int>(v / 255.0f * (filas - 1)), 0, filas - 1);
            int& n = cuentas[static_cast<size_t>(cy) * cols + cx];
            maximo = std::max(maximo, ++n);
        }
    }
    for (int y = 0; y < filas; ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(r.top() + y)) + r.left();
        for (int x = 0; x < cols; ++x) {
            const int b = brillo(cuentas[static_cast<size_t>(y) * cols + x], maximo);
            if (b == 0) continue;
            dst[x] = qRgb(std::min(255, qRed(dst[x]) + c.red() * b / 255), std::min(255, qGreen(dst[x]) + c.green() * b / 255),
                          std::min(255, qBlue(dst[x]) + c.blue() * b / 255));
        }
    }
}

/// Cb/Cr BT.709 de rango completo, en -0.5..0.5.
inline void cbcr(int r, int g, int b, float& cb, float& cr) {
    const float y = luma709(r, g, b) / 255.0f;
    cb = (b / 255.0f - y) / 1.8556f;
    cr = (r / 255.0f - y) / 1.5748f;
}

void vector(QImage& out, const QImage& m) {
    const int lado = std::min(out.width(), out.height()) - 8;
    const QPoint centro(out.width() / 2, out.height() / 2);
    const float radio = lado / 2.0f;
    std::vector<int> cuentas(static_cast<size_t>(lado) * lado, 0);
    int maximo = 1;
    for (int y = 0; y < m.height(); ++y) {
        const auto* fila = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        for (int x = 0; x < m.width(); ++x) {
            float cb, cr;
            cbcr(qRed(fila[x]), qGreen(fila[x]), qBlue(fila[x]), cb, cr);
            const int px = std::clamp(static_cast<int>((cb / 0.5f) * radio * 0.98f + radio), 0, lado - 1);
            const int py = std::clamp(static_cast<int>(radio - (cr / 0.5f) * radio * 0.98f), 0, lado - 1);
            int& n = cuentas[static_cast<size_t>(py) * lado + px];
            maximo = std::max(maximo, ++n);
        }
    }
    const int ox = centro.x() - lado / 2, oy = centro.y() - lado / 2;
    for (int y = 0; y < lado; ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(oy + y)) + ox;
        for (int x = 0; x < lado; ++x) {
            const int b = brillo(cuentas[static_cast<size_t>(y) * lado + x], maximo);
            if (b > 0) dst[x] = qRgb(b * 6 / 10, b, b * 7 / 10);
        }
    }
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(kGrilla);
    p.drawEllipse(QPointF(centro), radio * 0.98f, radio * 0.98f);
    p.drawLine(centro.x() - static_cast<int>(radio), centro.y(), centro.x() + static_cast<int>(radio), centro.y());
    p.drawLine(centro.x(), centro.y() - static_cast<int>(radio), centro.x(), centro.y() + static_cast<int>(radio));
    // Las cajas de los colores al 75 % (las barras), donde tiene que caer cada uno.
    struct Meta {
        const char* nombre;
        int r, g, b;
    };
    static const std::array<Meta, 6> kMetas{{{"R", 191, 0, 0}, {"Mg", 191, 0, 191}, {"B", 0, 0, 191},
                                             {"Cy", 0, 191, 191}, {"G", 0, 191, 0}, {"Yl", 191, 191, 0}}};
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    for (const auto& mt : kMetas) {
        float cb, cr;
        cbcr(mt.r, mt.g, mt.b, cb, cr);
        const QPointF q(centro.x() + (cb / 0.5f) * radio * 0.98f, centro.y() - (cr / 0.5f) * radio * 0.98f);
        p.setPen(QPen(QColor(mt.r ? 255 : 90, mt.g ? 255 : 90, mt.b ? 255 : 90, 170), 1));
        p.drawRect(QRectF(q.x() - 6, q.y() - 6, 12, 12));
        p.drawText(q + QPointF(8, -6), mt.nombre);
    }
    // La línea de piel (~123° desde el eje de Cb, hacia rojo/amarillo).
    const double ang = 123.0 * std::numbers::pi / 180.0;
    p.setPen(QPen(QColor(255, 200, 150, 110), 1, Qt::DashLine));
    p.drawLine(QPointF(centro), QPointF(centro.x() + std::cos(ang) * radio, centro.y() - std::sin(ang) * radio));
}

void histograma(QImage& out, const QImage& m) {
    std::array<std::array<int, 256>, 4> h{}; // R, G, B, luma
    for (int y = 0; y < m.height(); ++y) {
        const auto* fila = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        for (int x = 0; x < m.width(); ++x) {
            const QRgb px = fila[x];
            ++h[0][qRed(px)];
            ++h[1][qGreen(px)];
            ++h[2][qBlue(px)];
            ++h[3][std::clamp(static_cast<int>(luma709(qRed(px), qGreen(px), qBlue(px)) + 0.5f), 0, 255)];
        }
    }
    int maximo = 1;
    for (const auto& c : h)
        for (int i = 1; i < 255; ++i) // los extremos (recortados) no fijan la escala
            maximo = std::max(maximo, c[i]);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = out.rect().adjusted(6, 6, -6, -14);
    static const std::array<QColor, 4> kColores{QColor(255, 60, 60, 110), QColor(60, 255, 60, 110),
                                               QColor(80, 120, 255, 110), QColor(255, 255, 255, 150)};
    for (int c = 0; c < 4; ++c) {
        QPainterPath camino;
        camino.moveTo(r.left(), r.bottom());
        for (int i = 0; i < 256; ++i) {
            const double v = std::min(1.0, std::sqrt(static_cast<double>(h[static_cast<size_t>(c)][static_cast<size_t>(i)]) / maximo));
            camino.lineTo(r.left() + i * r.width() / 255.0, r.bottom() - v * r.height());
        }
        camino.lineTo(r.right(), r.bottom());
        if (c < 3) p.fillPath(camino, kColores[static_cast<size_t>(c)]);
        else p.strokePath(camino, QPen(kColores[3], 1.2));
    }
    p.setPen(QPen(kIlegal, 1, Qt::DashLine));
    for (int v : {16, 235}) {
        const int x = r.left() + v * r.width() / 255;
        p.drawLine(x, r.top(), x, r.bottom());
    }
    p.setPen(kTexto);
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.drawText(r.left(), out.height() - 3, "0");
    p.drawText(r.right() - 16, out.height() - 3, "255");
}

} // namespace

bool tipoScopeDeTexto(const QString& texto, TipoScope& tipo) {
    const QString t = texto.trimmed().toLower();
    if (t == "onda" || t == "waveform" || t == "forma_de_onda") tipo = TipoScope::Onda;
    else if (t == "parade" || t == "rgb") tipo = TipoScope::Parade;
    else if (t == "vector" || t == "vectorscopio" || t == "vectorscope") tipo = TipoScope::Vector;
    else if (t == "histograma" || t == "histogram") tipo = TipoScope::Histograma;
    else return false;
    return true;
}

QImage dibujarScope(const QImage& imagen, TipoScope tipo, int ancho, int alto) {
    QImage out(std::max(64, ancho), std::max(48, alto), QImage::Format_RGB32);
    out.fill(kFondo);
    if (imagen.isNull()) return out;
    const QImage m = muestra(imagen);
    const QRect area = out.rect().adjusted(18, 6, -4, -6);
    switch (tipo) {
        case TipoScope::Onda: {
            onda(out, area, m, 3, QColor(150, 255, 170));
            QPainter p(&out);
            grillaOnda(p, area);
            break;
        }
        case TipoScope::Parade: {
            const int w3 = area.width() / 3;
            static const std::array<QColor, 3> kC{QColor(255, 80, 80), QColor(80, 255, 80), QColor(90, 140, 255)};
            for (int c = 0; c < 3; ++c) {
                const QRect r(area.left() + c * w3, area.top(), w3 - 4, area.height());
                onda(out, r, m, c, kC[static_cast<size_t>(c)]);
            }
            QPainter p(&out);
            for (int c = 0; c < 3; ++c) grillaOnda(p, QRect(area.left() + c * w3, area.top(), w3 - 4, area.height()));
            break;
        }
        case TipoScope::Vector: vector(out, m); break;
        case TipoScope::Histograma: histograma(out, m); break;
    }
    return out;
}

Legalidad medirLegalidad(const QImage& imagen) {
    Legalidad l;
    if (imagen.isNull()) return l;
    const QImage m = muestra(imagen);
    long baja = 0, alta = 0, croma = 0, total = 0;
    for (int y = 0; y < m.height(); ++y) {
        const auto* fila = reinterpret_cast<const QRgb*>(m.constScanLine(y));
        for (int x = 0; x < m.width(); ++x, ++total) {
            const int r = qRed(fila[x]), g = qGreen(fila[x]), b = qBlue(fila[x]);
            const float yy = luma709(r, g, b);
            // En RGB de rango completo, «fuera de rango» quiere decir negros
            // aplastados o blancos quemados (lo que al pasar a rango limitado
            // queda pegado a 16 o a 235).
            if (yy < 2.0f) ++baja;
            if (yy > 253.0f) ++alta;
            float cb, cr;
            cbcr(r, g, b, cb, cr);
            const float u = 0.913f * cb, v = 1.381f * cr, c = std::sqrt(u * u + v * v), yn = yy / 255.0f;
            if (yn + c > 1.33f || yn - c < -0.33f) ++croma; // gama compuesta PAL (como balegal)
        }
    }
    if (total > 0) {
        l.lumaBajaPct = 100.0 * baja / total;
        l.lumaAltaPct = 100.0 * alta / total;
        l.cromaPct = 100.0 * croma / total;
    }
    return l;
}

} // namespace ba::app::video
