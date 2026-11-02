// ============================================================================
// src/app/ui/VideoItem.hpp — Componente de VISTA que dibuja un video en QML
//
// `VideoItem { source: "program"; fillMode: VideoItem.Cover }`
//
// Solo presentacion: pide al FrameService la ultima imagen de su clave y la
// sube como textura del scene graph (GPU). No sabe del motor ni de IPC.
//
// Lo que no se distingue no se paga: oculto, o más chico que kLadoMinimo en
// algún lado (una grilla de 40 invitados en una tarjeta), no pide cuadros, no
// los sube ni redibuja la ventana; al volverse visible o agrandarse los pide.
// La textura es una sola por item y se reescribe en cada cuadro (sin crear y
// soltar recursos de GPU por cuadro); se rehace solo si cambia el tamaño.
// ============================================================================

#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include <cstdint>

namespace ba::app {

class FrameService;

class VideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    /// El tamaño del cuadro (para ubicar el mouse sobre la imagen con Fit).
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
   public:
    enum FillMode { Fit, Cover };
    Q_ENUM(FillMode)

    /// Por debajo de esto (px lógicos, el lado más corto) el video no se distingue: no se dibuja.
    static constexpr qreal kLadoMinimo = 24;

    explicit VideoItem(QQuickItem* parent = nullptr);

    QString source() const { return source_; }
    void setSource(const QString& s);
    FillMode fillMode() const { return fillMode_; }
    void setFillMode(FillMode m);
    bool hasFrame() const { return !image_.isNull(); }
    QSize frameSize() const { return image_.size(); }

    /// Lo fija el arranque de la UI (unico servicio de frames).
    static void setService(FrameService* service);

   signals:
    void sourceChanged();
    void fillModeChanged();
    void hasFrameChanged();
    void frameSizeChanged();

   protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    /// Al volverse visible trae el último cuadro (oculto no los copia ni los sube).
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    /// Al pasar de diminuto a visible (o al revés) pide los cuadros (o suelta la textura).
    void geometryChange(const QRectF& nuevo, const QRectF& viejo) override;

   private slots:
    void refresh();

   private:
    /// Demasiado chico para que el video se distinga (ver kLadoMinimo).
    static bool diminuto(const QSizeF& s) { return s.width() < kLadoMinimo || s.height() < kLadoMinimo; }
    bool diminuto() const { return diminuto(size()); }

    QString source_ = "program";
    FillMode fillMode_ = Fit;
    QImage image_;
    std::uint64_t serial_ = 0;
    bool dirty_ = false;
    static QPointer<FrameService> service_;
};

} // namespace ba::app
