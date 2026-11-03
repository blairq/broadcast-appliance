// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/control/CuadrosWeb.cpp — ver el .hpp
#include "control/CuadrosWeb.hpp"

#include "ui/ProveedorCuadros.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QImageWriter>
#include <QThreadPool>

#include <algorithm>
#include <chrono>

namespace ba::app::control {

namespace {
std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
/// Los hilos que comprimen: dos (dos claves a la vez; más no gana en una máquina ocupada).
QThreadPool& pileta() {
    static QThreadPool* p = [] {
        auto* x = new QThreadPool();
        x->setMaxThreadCount(2);
        x->setObjectName(QStringLiteral("cuadros-web"));
        return x;
    }();
    return *p;
}
} // namespace

CuadrosWeb::CuadrosWeb(ProveedorCuadros* fuente, QObject* parent) : QObject(parent), fuente_(fuente) {
    // Cada 8 ms se mira si hay un cuadro nuevo de lo pedido (el servicio avisa por vuelta,
    // pero también lee más espaciado si nadie quiere más: así se lo pedimos al ritmo justo).
    reloj_.setInterval(8);
    connect(&reloj_, &QTimer::timeout, this, &CuadrosWeb::vuelta);
}

CuadrosWeb::~CuadrosWeb() {
    *vivo_ = false;
    reloj_.stop();
    pileta().waitForDone(2000);
}

void CuadrosWeb::pedir(const std::string& cid, const json& claves) {
    std::vector<Pedido> nuevos;
    if (claves.is_array())
        for (const auto& k : claves) {
            if (!k.is_object() || !k.contains("clave") || !k["clave"].is_string()) continue;
            Pedido p;
            p.clave = k["clave"].get<std::string>();
            p.ancho = std::clamp(k.value("ancho", 640), 64, 1920);
            p.fps = std::clamp(k.value("fps", 15), 1, 30);
            // Lo que ya tenía de esa clave (no le vuelve a mandar el mismo cuadro).
            if (auto it = pedidos_.find(cid); it != pedidos_.end())
                for (const Pedido& v : it->second)
                    if (v.clave == p.clave) p.enviado = v.enviado;
            nuevos.push_back(std::move(p));
        }
    if (nuevos.empty()) pedidos_.erase(cid);
    else pedidos_[cid] = std::move(nuevos);
    // Las fuentes que nadie pide se sueltan (el cuadro guardado también).
    std::set<std::string> usadas;
    for (const auto& [c, ps] : pedidos_)
        for (const Pedido& p : ps) usadas.insert(p.clave);
    for (auto it = fuentes_.begin(); it != fuentes_.end();)
        it = usadas.count(it->first) ? std::next(it) : fuentes_.erase(it);
    if (pedidos_.empty()) reloj_.stop();
    else if (!reloj_.isActive()) reloj_.start();
}

QByteArray CuadrosWeb::comprimir(const QImage& img, int ancho, int calidad, int* w, int* h) {
    QImage chica = img.width() > ancho ? img.scaledToWidth(ancho, Qt::SmoothTransformation) : img;
    if (chica.format() != QImage::Format_RGB32 && chica.format() != QImage::Format_RGB888)
        chica = chica.convertToFormat(QImage::Format_RGB32);
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    QImageWriter wr(&b, "jpeg");
    wr.setQuality(calidad);
    if (!wr.write(chica)) return {};
    if (w) *w = chica.width();
    if (h) *h = chica.height();
    return out;
}

void CuadrosWeb::vuelta() {
    if (!fuente_) return;
    const std::int64_t ahora = ahoraMs();
    // 1. El último cuadro de cada clave pedida (al ritmo del que más pide).
    std::map<std::string, int> maxFps;
    for (const auto& [c, ps] : pedidos_)
        for (const Pedido& p : ps) maxFps[p.clave] = std::max(maxFps[p.clave], p.fps);
    for (const auto& [clave, fps] : maxFps) {
        Fuente& f = fuentes_[clave];
        QImage img;
        std::uint64_t serial = f.serial;
        if (fuente_->ultimo(QString::fromStdString(clave), img, serial, 1'000'000 / fps)) {
            f.img = img;
            f.serial = serial;
            f.t = QDateTime::currentMSecsSinceEpoch();
        }
    }
    // 2. A cada pedido que le toca y tiene uno nuevo: comprimirlo (una vez por clave y ancho).
    for (auto& [cid, ps] : pedidos_)
        for (Pedido& p : ps) {
            const Fuente& f = fuentes_[p.clave];
            if (f.img.isNull() || f.serial == p.enviado || ahora - p.ultimoMs < 1000 / p.fps - 4) continue;
            const auto clave = std::make_tuple(p.clave, p.ancho, f.serial);
            if (enCurso_.count(clave)) continue; // ya se está comprimiendo: le llega al terminar
            enCurso_.insert(clave);
            const QImage img = f.img;
            const std::uint64_t serial = f.serial;
            const std::int64_t t = f.t;
            const int ancho = p.ancho;
            const std::string k = p.clave;
            std::shared_ptr<bool> vivo = vivo_;
            pileta().start([this, vivo, img, ancho, serial, t, k]() {
                QElapsedTimer reloj;
                reloj.start();
                int w = 0, h = 0;
                const QByteArray jpg = comprimir(img, ancho, kCalidad, &w, &h);
                const std::int64_t us = reloj.nsecsElapsed() / 1000;
                if (!*vivo) return;
                QMetaObject::invokeMethod(
                    this, [this, vivo, k, ancho, serial, t, jpg, w, h, us]() {
                        if (*vivo) listo(k, ancho, serial, t, jpg, w, h, us);
                    },
                    Qt::QueuedConnection);
            });
        }
}

void CuadrosWeb::listo(const std::string& clave, int ancho, std::uint64_t serial, std::int64_t t, const QByteArray& jpg,
                       int w, int h, std::int64_t us) {
    enCurso_.erase(std::make_tuple(clave, ancho, serial));
    if (jpg.isEmpty()) return;
    ++cuentas_.cuadros;
    cuentas_.bytes += static_cast<std::uint64_t>(jpg.size());
    cuentas_.usComprimir += static_cast<std::uint64_t>(us);
    const std::int64_t ahora = ahoraMs();
    json msg;
    bool armado = false;
    for (auto& [cid, ps] : pedidos_)
        for (Pedido& p : ps) {
            if (p.clave != clave || p.ancho != ancho || serial <= p.enviado) continue;
            if (!armado) {
                msg = {{"type", "replica"},
                       {"op", "cuadro"},
                       {"clave", clave},
                       {"s", serial},
                       {"t", t},
                       {"w", w},
                       {"h", h},
                       {"us", us},
                       {"jpg", jpg.toBase64().toStdString()}};
                armado = true;
            }
            p.enviado = serial;
            p.ultimoMs = ahora;
            ++cuentas_.enviados;
            if (enviar) enviar(cid, msg);
        }
}

} // namespace ba::app::control
