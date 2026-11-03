// SPDX-License-Identifier: GPL-2.0-or-later
//
// VideoItem del tablero web: el mismo tipo QML que el de la app (src/app/ui/VideoItem.hpp:
// source, fillMode, hasFrame, frameSize, enVista, topeCps), así la QML compartida no cambia.
// En la app los cuadros salen del servicio de cuadros; acá llegan de la app en JPEG
// (control/CuadrosWeb) y se dibujan como textura de la escena: el video queda adentro de la
// vista, con las tarjetas de vidrio y los menús encima, igual que en la app.
//
// Pide su clave al ancho al que se dibuja (en píxeles de la pantalla) y a los cuadros por
// segundo que vale la pena (chico: 12; grande: 25; o topeCps). Oculto, fuera de la vista o
// diminuto, no pide nada.
// ============================================================================
#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QSize>

class ClienteReplica;

class VideoItemWeb : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
    Q_PROPERTY(bool enVista READ enVista WRITE setEnVista NOTIFY enVistaChanged)
    Q_PROPERTY(int topeCps READ topeCps WRITE setTopeCps NOTIFY topeCpsChanged)
    /// Cuánto tardó el último cuadro en verse desde que llegó a la app (ms; -1 sin medir).
    Q_PROPERTY(int retardoMs READ retardoMs NOTIFY retardoChanged)
   public:
    enum FillMode { Fit, Cover };
    Q_ENUM(FillMode)
    static void setCliente(ClienteReplica* c);

    explicit VideoItemWeb(QQuickItem* parent = nullptr);
    ~VideoItemWeb() override;

    QString source() const { return source_; }
    void setSource(const QString& s);
    FillMode fillMode() const { return fillMode_; }
    void setFillMode(FillMode m);
    bool hasFrame() const { return !imagen_.isNull(); }
    QSize frameSize() const { return imagen_.size(); }
    bool enVista() const { return enVista_; }
    void setEnVista(bool v);
    int topeCps() const { return topeCps_; }
    void setTopeCps(int v);
    int retardoMs() const { return retardoMs_; }

   signals:
    void sourceChanged();
    void fillModeChanged();
    void hasFrameChanged();
    void frameSizeChanged();
    void enVistaChanged();
    void topeCpsChanged();
    void retardoChanged();

   protected:
    QSGNode* updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& nueva, const QRectF& vieja) override;
    void itemChange(ItemChange cambio, const ItemChangeData& dato) override;

   private:
    void repedir();
    void alCuadro(const QString& clave, const QImage& img, qint64 t, quint64 serial);

    QString source_;
    FillMode fillMode_ = Fit;
    bool enVista_ = true;
    int topeCps_ = -1;
    QImage imagen_;
    bool nueva_ = false;
    qint64 t_ = 0;
    int retardoMs_ = -1;
    QMetaObject::Connection alDibujar_;
};
