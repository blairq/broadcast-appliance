// ============================================================================
// src/app/ui/ImagenViva.cpp — ver ImagenViva.hpp
// ============================================================================
#include "ui/ImagenViva.hpp"

#include <QQuickWindow>
#include <QSGImageNode>

namespace ba::app {

ImagenViva::ImagenViva(QQuickItem* padre) : QQuickItem(padre) { setFlag(ItemHasContents, true); }

void ImagenViva::setImagen(const QImage& i) {
    // QImage comparte los datos: comparar la clave alcanza para no subir dos
    // veces la misma textura.
    if (i.cacheKey() == imagen_.cacheKey()) return;
    imagen_ = i;
    cambio_ = true;
    emit imagenChanged();
    update();
}

void ImagenViva::setAjustar(bool a) {
    if (a == ajustar_) return;
    ajustar_ = a;
    emit ajustarChanged();
    update();
}

QSGNode* ImagenViva::updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) {
    auto* n = static_cast<QSGImageNode*>(viejo);
    // Qué mostrar lo decide el modelo (con un error, el editor deja el último
    // cuadro bueno); acá solo se muestra lo que hay.
    if (imagen_.isNull() || width() <= 0 || height() <= 0) {
        delete n;
        return nullptr;
    }
    if (n == nullptr) {
        n = window()->createImageNode();
        n->setOwnsTexture(true);
        n->setFiltering(QSGTexture::Linear);
        cambio_ = true;
    }
    if (cambio_) {
        n->setTexture(window()->createTextureFromImage(imagen_));
        cambio_ = false;
    }
    const QSizeF img = imagen_.size();
    const QSizeF caja(width(), height());
    n->setSourceRect(QRectF(QPointF(0, 0), img));
    if (ajustar_) {
        const QSizeF esc = img.scaled(caja, Qt::KeepAspectRatio);
        n->setRect(QRectF(QPointF((caja.width() - esc.width()) / 2, (caja.height() - esc.height()) / 2), esc));
    } else {
        n->setRect(QRectF(QPointF(0, 0), caja));
    }
    return n;
}

} // namespace ba::app
