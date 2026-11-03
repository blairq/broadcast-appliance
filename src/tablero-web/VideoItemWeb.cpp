// SPDX-License-Identifier: GPL-2.0-or-later
// src/tablero-web/VideoItemWeb.cpp — ver el .hpp
#include "VideoItemWeb.hpp"

#include "ClienteReplica.hpp"

#include <QDateTime>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>

#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
QPointer<ClienteReplica> g_cliente;

/// Lo medido, para quien mire desde la página (window.baMedidas: la prueba sin ventana y
/// el que mide la latencia). En la app no hay nada de esto.
void anotarRetardo(const QString& clave, int ms, int w, int h) {
#ifdef __EMSCRIPTEN__
    const QByteArray c = clave.toUtf8();
    EM_ASM(
        {
            const m = (window.baMedidas = window.baMedidas || {});
            const k = UTF8ToString($0);
            const x = (m[k] = m[k] || {n : 0, suma : 0, max : 0, ultimo : 0});
            x.n++;
            x.suma += $1;
            x.ultimo = $1;
            x.max = Math.max(x.max, $1);
            x.w = $2;
            x.h = $3;
            x.prom = Math.round(x.suma / x.n);
        },
        c.constData(), ms, w, h);
#else
    Q_UNUSED(clave);
    Q_UNUSED(ms);
    Q_UNUSED(w);
    Q_UNUSED(h);
#endif
}
} // namespace

void VideoItemWeb::setCliente(ClienteReplica* c) { g_cliente = c; }

VideoItemWeb::VideoItemWeb(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    if (g_cliente) connect(g_cliente, &ClienteReplica::cuadro, this, &VideoItemWeb::alCuadro);
}

VideoItemWeb::~VideoItemWeb() {
    if (g_cliente) g_cliente->soltarVideo(this);
}

void VideoItemWeb::setSource(const QString& s) {
    if (s == source_) return;
    source_ = s;
    imagen_ = {};
    emit sourceChanged();
    emit hasFrameChanged();
    emit frameSizeChanged();
    update();
    repedir();
}

void VideoItemWeb::setFillMode(FillMode m) {
    if (m == fillMode_) return;
    fillMode_ = m;
    emit fillModeChanged();
    update();
}

void VideoItemWeb::setEnVista(bool v) {
    if (v == enVista_) return;
    enVista_ = v;
    emit enVistaChanged();
    repedir();
}

void VideoItemWeb::setTopeCps(int v) {
    if (v == topeCps_) return;
    topeCps_ = v;
    emit topeCpsChanged();
    repedir();
}

void VideoItemWeb::geometryChange(const QRectF& nueva, const QRectF& vieja) {
    QQuickItem::geometryChange(nueva, vieja);
    if (nueva.size() != vieja.size()) repedir();
}

void VideoItemWeb::itemChange(ItemChange cambio, const ItemChangeData& dato) {
    QQuickItem::itemChange(cambio, dato);
    if (cambio == ItemVisibleHasChanged || cambio == ItemSceneChange) repedir();
    if (cambio == ItemSceneChange) {
        disconnect(alDibujar_);
        if (dato.window != nullptr)
            alDibujar_ = connect(dato.window, &QQuickWindow::frameSwapped, this, [this]() {
                // Recién mostrado: cuánto pasó desde que el cuadro llegó a la app.
                if (t_ <= 0) return;
                retardoMs_ = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - t_);
                t_ = 0;
                anotarRetardo(source_, retardoMs_, imagen_.width(), imagen_.height());
                emit retardoChanged();
            });
    }
}

void VideoItemWeb::repedir() {
    if (!g_cliente) return;
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal lado = std::min(width(), height());
    if (source_.isEmpty() || !isVisible() || !enVista_ || window() == nullptr || lado < 24) {
        g_cliente->soltarVideo(this);
        return;
    }
    // El ancho en píxeles de la pantalla, en escalones de 160 (no pedir de nuevo por un píxel)
    // y hasta 1280 (más no se distingue en un monitor de tablero y cuesta el doble).
    const int ancho = std::clamp(static_cast<int>(std::ceil(width() * dpr / 160.0)) * 160, 160, 1280);
    const int fps = topeCps_ > 0 ? std::min(topeCps_, 30) : (std::max(width(), height()) <= 400 ? 12 : 25);
    g_cliente->pedirVideo(this, source_, ancho, fps);
}

void VideoItemWeb::alCuadro(const QString& clave, const QImage& img, qint64 t, quint64) {
    if (clave != source_ || !isVisible()) return;
    const bool tenia = !imagen_.isNull();
    const QSize antes = imagen_.size();
    imagen_ = img;
    nueva_ = true;
    t_ = t;
    if (!tenia) emit hasFrameChanged();
    if (antes != imagen_.size()) emit frameSizeChanged();
    update();
}

QSGNode* VideoItemWeb::updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) {
    auto* nodo = static_cast<QSGImageNode*>(viejo);
    if (imagen_.isNull() || width() <= 0 || height() <= 0) {
        delete nodo;
        return nullptr;
    }
    if (nodo == nullptr) {
        nodo = window()->createImageNode();
        nodo->setOwnsTexture(true);
        nodo->setFiltering(QSGTexture::Linear);
        nueva_ = true;
    }
    if (nueva_) {
        nodo->setTexture(window()->createTextureFromImage(imagen_));
        nueva_ = false;
    }
    // Fit: entero con bandas; Cover: llena recortando (como en la app).
    const QSizeF img = imagen_.size();
    const qreal escala = fillMode_ == Fit ? std::min(width() / img.width(), height() / img.height())
                                          : std::max(width() / img.width(), height() / img.height());
    const QSizeF tam = img * escala;
    QRectF destino((width() - tam.width()) / 2, (height() - tam.height()) / 2, tam.width(), tam.height());
    QRectF origen(0, 0, img.width(), img.height());
    if (fillMode_ == Cover) {
        // Recortar el origen a lo que se ve.
        const QRectF visible = destino.intersected(QRectF(0, 0, width(), height()));
        origen = QRectF((visible.x() - destino.x()) / escala, (visible.y() - destino.y()) / escala, visible.width() / escala,
                        visible.height() / escala);
        destino = visible;
    }
    nodo->setRect(destino);
    nodo->setSourceRect(origen);
    return nodo;
}
