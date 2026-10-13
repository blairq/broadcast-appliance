// src/app/ui/Vumetro.cpp — ver el .hpp
#include "ui/Vumetro.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ba::app {

namespace {

struct Color {
    unsigned char r, g, b, a;
};
// Los colores de la escala (como una consola de broadcast: verde, ámbar, rojo).
constexpr Color kFondo{255, 255, 255, 14};
constexpr Color kEscala{255, 255, 255, 34};
constexpr Color kVerde{52, 199, 89, 255};
constexpr Color kAmbar{255, 184, 0, 255};
constexpr Color kRojo{255, 59, 48, 255};
constexpr Color kPico{255, 255, 255, 235};

Color apagar(Color c, bool apagado) {
    if (!apagado) return c;
    const unsigned char gris = static_cast<unsigned char>((c.r + c.g + c.b) / 3);
    return {gris, gris, gris, static_cast<unsigned char>(c.a / 3)};
}

// Un rectángulo = dos triángulos, con color por vértice (premultiplicado).
void rect(std::vector<QSGGeometry::ColoredPoint2D>& v, float x0, float y0, float x1, float y1, Color c) {
    if (x1 <= x0 || y1 <= y0) return;
    const float a = c.a / 255.0f;
    const auto pm = [a](unsigned char k) { return static_cast<unsigned char>(std::lround(k * a)); };
    const unsigned char r = pm(c.r), g = pm(c.g), b = pm(c.b);
    QSGGeometry::ColoredPoint2D p[4];
    p[0].set(x0, y0, r, g, b, c.a);
    p[1].set(x1, y0, r, g, b, c.a);
    p[2].set(x0, y1, r, g, b, c.a);
    p[3].set(x1, y1, r, g, b, c.a);
    v.push_back(p[0]);
    v.push_back(p[1]);
    v.push_back(p[2]);
    v.push_back(p[2]);
    v.push_back(p[1]);
    v.push_back(p[3]);
}

} // namespace

Vumetro::Vumetro(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void Vumetro::poner(int i, qreal x) {
    if (!std::isfinite(x)) x = -100;
    if (std::abs(v_[i] - x) < 0.05) return; // sin cambio visible: ni un repintado
    v_[i] = x;
    emit cambio();
    update();
}

void Vumetro::setCanales(int c) {
    c = c >= 2 ? 2 : 1;
    if (c == canales_) return;
    canales_ = c;
    emit cambio();
    update();
}
void Vumetro::setMinDb(qreal x) {
    if (x == min_) return;
    min_ = x;
    emit cambio();
    update();
}
void Vumetro::setMaxDb(qreal x) {
    if (x == max_) return;
    max_ = x;
    emit cambio();
    update();
}
void Vumetro::setAmarilloDb(qreal x) {
    if (x == amarillo_) return;
    amarillo_ = x;
    emit cambio();
    update();
}
void Vumetro::setRojoDb(qreal x) {
    if (x == rojo_) return;
    rojo_ = x;
    emit cambio();
    update();
}
void Vumetro::setHorizontal(bool h) {
    if (h == horizontal_) return;
    horizontal_ = h;
    emit cambio();
    update();
}
void Vumetro::setSegmentos(int n) {
    n = std::clamp(n, 0, 64);
    if (n == segmentos_) return;
    segmentos_ = n;
    emit cambio();
    update();
}
void Vumetro::setApagado(bool a) {
    if (a == apagado_) return;
    apagado_ = a;
    emit cambio();
    update();
}

qreal Vumetro::fraccion(qreal db) const {
    if (max_ <= min_) return 0;
    return std::clamp((db - min_) / (max_ - min_), 0.0, 1.0);
}

QSGNode* Vumetro::updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) {
    auto* nodo = static_cast<QSGGeometryNode*>(viejo);
    if (nodo == nullptr) {
        nodo = new QSGGeometryNode;
        auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geo->setDrawingMode(QSGGeometry::DrawTriangles);
        nodo->setGeometry(geo);
        nodo->setFlag(QSGNode::OwnsGeometry);
        nodo->setMaterial(new QSGVertexColorMaterial);
        nodo->setFlag(QSGNode::OwnsMaterial);
    }
    // Se dibuja parado: `ancho` a lo ancho y `alto` a lo largo (arriba = el máximo). Acostado,
    // cada rectángulo se gira: lo de arriba queda a la derecha.
    const float W = static_cast<float>(width()), H = static_cast<float>(height());
    const float w = horizontal_ ? H : W, h = horizontal_ ? W : H;
    std::vector<QSGGeometry::ColoredPoint2D> v;
    v.reserve(200);
    const auto r = [&](float x0, float y0, float x1, float y1, Color col) {
        if (horizontal_) rect(v, h - y1, x0, h - y0, x1, col);
        else rect(v, x0, y0, x1, y1, col);
    };
    const auto y = [&](qreal db) { return h - static_cast<float>(fraccion(db)) * h; };
    const float hueco = canales_ == 2 ? std::max(1.0f, w * 0.08f) : 0.0f;
    const float ancho = canales_ == 2 ? (w - hueco) / 2.0f : w;
    for (int c = 0; c < canales_; ++c) {
        const float x0 = c == 0 ? 0.0f : ancho + hueco, x1 = x0 + ancho;
        if (segmentos_ > 0) {
            // LED: cada segmento se enciende si el pico pasa su comienzo; color por zona.
            const float paso = h / static_cast<float>(segmentos_);
            for (int k = 0; k < segmentos_; ++k) {
                const qreal desde = min_ + (max_ - min_) * k / segmentos_;
                Color col = desde >= rojo_ ? kRojo : desde >= amarillo_ ? kAmbar : kVerde;
                const bool on = v_[c] > desde && !apagado_;
                if (!on) col.a = 30;
                const float yb = h - k * paso, yt = yb - paso + 1.0f;
                r(x0, yt, x1, yb, col);
            }
            continue;
        }
        r(x0, 0, x1, h, kFondo);
        // Las líneas de la escala (cada 6 dB), finitas, detrás de la barra.
        for (qreal db = std::ceil(min_ / 6.0) * 6.0; db < max_; db += 6.0) {
            const float yy = y(db);
            r(x0, yy, x1, yy + 1.0f, kEscala);
        }
        const float yAmarillo = y(amarillo_), yRojo = y(rojo_);
        // La barra del RMS, por zonas.
        const float yRms = y(v_[2 + c]);
        r(x0, std::max(yRms, yAmarillo), x1, h, apagar(kVerde, apagado_));
        r(x0, std::max(yRms, yRojo), x1, std::min(h, yAmarillo), apagar(kAmbar, apagado_));
        r(x0, yRms, x1, std::min(h, yRojo), apagar(kRojo, apagado_));
        // El pico (línea) y la retención (marca del color de su zona).
        const qreal pico = v_[c], ret = v_[4 + c];
        if (pico > min_) {
            const float yp = y(pico);
            r(x0, yp, x1, yp + 1.5f, apagar(kPico, apagado_));
        }
        if (ret > min_) {
            const float yr = y(ret);
            const Color col = ret >= rojo_ ? kRojo : ret >= amarillo_ ? kAmbar : kVerde;
            r(x0, yr, x1, yr + 2.0f, apagar(col, apagado_));
        }
    }
    QSGGeometry* geo = nodo->geometry();
    geo->allocate(static_cast<int>(v.size()));
    std::copy(v.begin(), v.end(), geo->vertexDataAsColoredPoint2D());
    nodo->markDirty(QSGNode::DirtyGeometry);
    return nodo;
}

} // namespace ba::app
