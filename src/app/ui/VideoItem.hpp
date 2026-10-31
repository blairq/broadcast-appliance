// ============================================================================
// src/app/ui/VideoItem.hpp — Componente de VISTA que dibuja un video en QML
//
// `VideoItem { source: "program"; fillMode: VideoItem.Cover }`
//
// Solo presentacion: pide al FrameService la ultima imagen de su clave y la
// sube como textura del scene graph (GPU). No sabe del motor ni de IPC.
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

   private slots:
    void refresh();

   private:
    QString source_ = "program";
    FillMode fillMode_ = Fit;
    QImage image_;
    std::uint64_t serial_ = 0;
    bool dirty_ = false;
    static QPointer<FrameService> service_;
};

} // namespace ba::app
