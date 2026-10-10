// src/app/ui/FuenteQrPintor.cpp — ver FuenteQrPintor.hpp
#include "ui/FuenteQrPintor.hpp"

#include "emision/FuenteQr.hpp"
#include "emision/FuentesApp.hpp"
#include "ui/QrViewModel.hpp"

#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <functional>
#include <set>

namespace ba::app {

FuenteQrPintor::FuenteQrPintor(QrViewModel& qr, QObject* parent) : QObject(parent), qr_(qr) {
    // La lista de «Qué QR» en Propiedades: los del aparato y los guardados.
    fuentesqr::ponerOpciones([this]() {
        nlohmann::json out = nlohmann::json::array();
        for (const QVariant& v : qr_.paraElegir()) {
            const QVariantMap m = v.toMap();
            out.push_back({{"value", m.value("value").toString().toStdString()}, {"label", m.value("label").toString().toStdString()}});
        }
        return out;
    });
    connect(&timer_, &QTimer::timeout, this, &FuenteQrPintor::tic);
    timer_.start(1000);
}

FuenteQrPintor::~FuenteQrPintor() { fuentesqr::ponerOpciones({}); }

void FuenteQrPintor::tic() {
    std::set<std::uint64_t> vivas;
    for (const auto& f : fuentes::de(fuentesqr::kTipo)) {
        vivas.insert(f.id);
        const std::string elegido = f.ajustes.value("qr", std::string());
        const std::string texto = f.ajustes.value("texto", std::string());
        const bool fondo = f.ajustes.value("fondo", true);
        const int lado = std::clamp(f.ajustes.value("tamano", 720), 128, 2160);
        const QString svg = qr_.svgElegido(QString::fromStdString(elegido), QString::fromStdString(texto));
        // Sin nada que codificar todavía (la sala no dio el enlace): se reintenta.
        if (svg.isEmpty()) continue;
        const std::size_t firma = std::hash<std::string>{}(svg.toStdString()) ^ (static_cast<std::size_t>(lado) << 1) ^
                                  (fondo ? 0x9e3779b9u : 0u) ^ (static_cast<std::size_t>(f.version) << 7);
        if (firmas_[f.id] == firma) continue;
        firmas_[f.id] = firma;
        QImage img(lado, lado, QImage::Format_ARGB32);
        img.fill(fondo ? Qt::white : Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, false);
        QSvgRenderer r(svg.toUtf8());
        // Con fondo, un margen blanco (la «zona silenciosa» que piden los lectores).
        const int m = fondo ? lado / 14 : 0;
        r.render(&p, QRectF(m, m, lado - 2 * m, lado - 2 * m));
        p.end();
        fuentes::empujar(f.id, img.constBits(), img.width(), img.height(), static_cast<int>(img.bytesPerLine()));
    }
    for (auto it = firmas_.begin(); it != firmas_.end();)
        it = vivas.count(it->first) ? std::next(it) : firmas_.erase(it);
}

} // namespace ba::app
