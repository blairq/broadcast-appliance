// ============================================================================
// src/app/ui/VideoItem.cpp — ver VideoItem.hpp
// ============================================================================

#include "ui/VideoItem.hpp"

#include <QQuickWindow>
#include <QSGImageNode>

#include "ui/FrameService.hpp"

namespace ba::app {

QPointer<FrameService> VideoItem::service_;

void VideoItem::setService(FrameService* service) { service_ = service; }

VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    if (service_ != nullptr) {
        connect(service_, &FrameService::framesUpdated, this, &VideoItem::refresh);
    }
}

void VideoItem::setSource(const QString& s) {
    if (s == source_) {
        return;
    }
    source_ = s;
    serial_ = 0;
    image_ = QImage();
    emit sourceChanged();
    emit hasFrameChanged();
    emit frameSizeChanged();
    refresh();
}

void VideoItem::setFillMode(FillMode m) {
    if (m == fillMode_) {
        return;
    }
    fillMode_ = m;
    emit fillModeChanged();
    update();
}

void VideoItem::refresh() {
    if (service_ == nullptr) {
        return;
    }
    const bool had = !image_.isNull();
    const QSize antes = image_.size();
    if (service_->latest(source_, image_, serial_)) {
        dirty_ = true;
        update();
        if (!had) {
            emit hasFrameChanged();
        }
        if (image_.size() != antes) {
            emit frameSizeChanged();
        }
    }
}

QSGNode* VideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGImageNode*>(old);
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }
    if (node == nullptr) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        dirty_ = true;
    }
    if (dirty_) {
        // Subida a la GPU del frame nuevo (una textura por item).
        node->setTexture(window()->createTextureFromImage(image_));
        dirty_ = false;
    }
    // Ajuste: Fit = entero con barras; Cover = llena y recorta (fondos).
    const QSizeF img = image_.size();
    const QSizeF box(width(), height());
    const QSizeF scaled =
        img.scaled(box, fillMode_ == Cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
    if (fillMode_ == Cover) {
        const qreal sx = img.width() * (box.width() / scaled.width());
        const qreal sy = img.height() * (box.height() / scaled.height());
        node->setSourceRect(QRectF((img.width() - sx) / 2, (img.height() - sy) / 2, sx, sy));
        node->setRect(QRectF(QPointF(0, 0), box));
    } else {
        node->setSourceRect(QRectF(QPointF(0, 0), img));
        node->setRect(QRectF(
            QPointF((box.width() - scaled.width()) / 2, (box.height() - scaled.height()) / 2), scaled));
    }
    return node;
}

} // namespace ba::app
