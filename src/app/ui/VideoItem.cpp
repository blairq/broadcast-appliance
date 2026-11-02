// ============================================================================
// src/app/ui/VideoItem.cpp — ver VideoItem.hpp
// ============================================================================

#include "ui/VideoItem.hpp"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <rhi/qrhi.h>

#include <utility>

#include "ui/FrameService.hpp"

namespace ba::app {

namespace {

// La textura de un VideoItem: una sola QRhiTexture que se reescribe con cada cuadro.
// createTextureFromImage por cuadro creaba y soltaba recursos de GPU cada vez (~9 % del
// hilo de render con muchas vistas). La subida va en commitTextureOperations, que el
// material llama en el hilo de render al preparar el cuadro.
class TexturaVideo final : public QSGTexture {
   public:
    explicit TexturaVideo(const QImage& primera) { poner(primera); }
    ~TexturaVideo() override {
        // Puede estar en uso en el cuadro en vuelo: se suelta cuando la GPU termina.
        if (tex_ != nullptr) tex_->deleteLater();
    }
    TexturaVideo(const TexturaVideo&) = delete;
    TexturaVideo& operator=(const TexturaVideo&) = delete;

    /// El cuadro nuevo (del mismo tamaño): se sube en el próximo commit.
    void poner(const QImage& img) {
        pendiente_ = img;
        tam_ = img.size();
        alfa_ = img.hasAlphaChannel();
    }

    qint64 comparisonKey() const override { return qint64(reinterpret_cast<qintptr>(this)); }
    QRhiTexture* rhiTexture() const override { return tex_; }
    QSize textureSize() const override { return tam_; }
    bool hasAlphaChannel() const override { return alfa_; }
    bool hasMipmaps() const override { return false; }

    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* cambios) override {
        if (pendiente_.isNull()) return;
        QImage img = std::exchange(pendiente_, QImage());
        // Lo que viene de los canales y del motor es RGB32 (BGRA en memoria): va tal cual
        // si la GPU lo toma; si no, o si es otro formato, se pasa a RGBA (como QSGPlainTexture).
        QRhiTexture::Format formato = QRhiTexture::RGBA8;
        const auto f = img.format();
        if ((f == QImage::Format_RGB32 || f == QImage::Format_ARGB32_Premultiplied) &&
            Q_BYTE_ORDER == Q_LITTLE_ENDIAN && rhi->isTextureFormatSupported(QRhiTexture::BGRA8)) {
            formato = QRhiTexture::BGRA8;
        } else if (f != QImage::Format_RGBX8888 && f != QImage::Format_RGBA8888_Premultiplied) {
            img = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        }
        if (tex_ != nullptr && (tex_->pixelSize() != img.size() || tex_->format() != formato)) {
            tex_->deleteLater();
            tex_ = nullptr;
        }
        if (tex_ == nullptr) {
            tex_ = rhi->newTexture(formato, img.size());
            if (!tex_->create()) {
                delete tex_;
                tex_ = nullptr;
                return;
            }
        }
        cambios->uploadTexture(tex_, img);
    }

   private:
    QRhiTexture* tex_ = nullptr;
    QImage pendiente_;
    QSize tam_;
    bool alfa_ = false;
};

} // namespace

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

void VideoItem::setEnVista(bool si) {
    if (si == enVista_) return;
    enVista_ = si;
    emit enVistaChanged();
    // De vuelta en la vista: lo último que haya, sin esperar al próximo cuadro.
    if (si) refresh();
}

void VideoItem::refresh() {
    if (service_ == nullptr) {
        return;
    }
    const bool had = !image_.isNull();
    // Oculto (una página precargada que no se ve), fuera de la vista desplazada o diminuto
    // (no se distingue) no copia cuadros ni redibuja la ventana: al mostrarse, volver a la
    // vista o agrandarse los pide. Sin leerlos (latest), FrameService deja de copiar ese
    // canal. El primero sí, por si alguien mira hasFrame para decidir si mostrarlo.
    if (had && (!isVisible() || !enVista_ || diminuto())) return;
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

void VideoItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged && value.boolValue) refresh();
}

void VideoItem::geometryChange(const QRectF& nuevo, const QRectF& viejo) {
    QQuickItem::geometryChange(nuevo, viejo);
    const bool era = diminuto(viejo.size()), es = diminuto(nuevo.size());
    if (era == es) return;
    // Diminuto: updatePaintNode suelta la textura. Visible de nuevo: lo último que haya
    // (sin esperar al próximo cuadro, por si la fuente está quieta) y los cuadros que siguen.
    dirty_ = true;
    update();
    if (!es) refresh();
}

QSGNode* VideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGImageNode*>(old);
    if (image_.isNull() || diminuto()) {
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
        // El cuadro nuevo va a la misma textura (se sube en el hilo de render); una nueva
        // solo la primera vez o si cambió el tamaño (setTexture suelta la anterior).
        // Sin RHI (el scene graph por software, sin GPU) no hay QRhi: una textura por cuadro.
        const bool rhi = QSGRendererInterface::isApiRhiBased(window()->rendererInterface()->graphicsApi());
        auto* tex = rhi ? static_cast<TexturaVideo*>(node->texture()) : nullptr;
        if (!rhi) {
            node->setTexture(window()->createTextureFromImage(image_));
        } else if (tex == nullptr || tex->textureSize() != image_.size()) {
            node->setTexture(new TexturaVideo(image_));
        } else {
            tex->poner(image_);
            node->markDirty(QSGNode::DirtyMaterial);
        }
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
        node->setRect(QRectF(QPointF((box.width() - scaled.width()) / 2, (box.height() - scaled.height()) / 2),
                             scaled));
    }
    return node;
}

} // namespace ba::app
