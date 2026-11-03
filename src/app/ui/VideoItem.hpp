// ============================================================================
// src/app/ui/VideoItem.hpp — Componente de VISTA que dibuja un video en QML
//
// `VideoItem { source: "program"; fillMode: VideoItem.Cover }`
//
// Solo presentacion: pide al servicio de cuadros (FrameService, ver
// ProveedorCuadros) la ultima imagen de su clave y la sube como textura del
// scene graph (GPU). No sabe del motor ni de IPC.
//
// Lo que no se distingue no se paga: oculto, fuera de la parte que se ve de una
// lista o grilla desplazable (enVista: false, lo dice quien la arma), o más chico
// que kLadoMinimo en algún lado, no pide cuadros, no los sube ni redibuja la
// ventana; al volverse visible, entrar a la vista o agrandarse los pide.
// Chico (una celda de la grilla de la sala, hasta kLadoChico), como mucho
// kCpsChico cuadros por segundo (topeCps), y todos los chicos en el MISMO turno
// del reloj: con 120 celdas, la ventana se redibuja ~15 veces por segundo por
// ellas y no cada vez que llega el cuadro de alguna.
// La textura es una sola por item y se reescribe en cada cuadro (sin crear y
// soltar recursos de GPU por cuadro); se rehace solo si cambia el tamaño.
// ============================================================================

#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include <atomic>
#include <cstdint>

#include "ui/ProveedorCuadros.hpp"

namespace ba::app {

class VideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    /// El tamaño del cuadro (para ubicar el mouse sobre la imagen con Fit).
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
    /// Dentro de la parte que se ve (una celda de una grilla desplazable fuera de la vista:
    /// false). Fuera de la vista se queda con el último cuadro y no pide más.
    Q_PROPERTY(bool enVista READ enVista WRITE setEnVista NOTIFY enVistaChanged)
    /// Cuántos cuadros por segundo como mucho: -1 (de fábrica) según el tamaño (chico,
    /// kCpsChico; si no, todos), 0 todos, N como mucho N.
    Q_PROPERTY(int topeCps READ topeCps WRITE setTopeCps NOTIFY topeCpsChanged)
   public:
    enum FillMode { Fit, Cover };
    Q_ENUM(FillMode)

    /// Por debajo de esto (px lógicos, el lado más corto) el video no se distingue: no se dibuja.
    static constexpr qreal kLadoMinimo = 24;
    /// Hasta esto (px lógicos, el lado más largo) es una vista chica (celda de grilla, miniatura).
    static constexpr qreal kLadoChico = 400;
    /// Los cuadros por segundo de una vista chica (topeCps -1): una cara de 160-400 px no
    /// gana nada con 30, y son la mitad de copias, subidas a la GPU y redibujos.
    static constexpr int kCpsChico = 15;

    /// Lo que hicieron todas las vistas de video desde que arrancó el proceso (el banco de
    /// la sala, tests/bench_sala_conferencia.cpp, los lee antes y después).
    struct Contadores {
        std::atomic<std::uint64_t> creados{0};
        std::atomic<std::int64_t> vivos{0};
        std::atomic<std::uint64_t> refrescos{0}; ///< refresh() (uno por item por vuelta con cuadros)
        std::atomic<std::uint64_t> pedidos{0};   ///< los que llegaron a pedir al servicio
        std::atomic<std::uint64_t> cuadros{0};   ///< cuadros nuevos tomados
        std::atomic<std::uint64_t> subidas{0};   ///< texturas reescritas (updatePaintNode con cuadro nuevo)
    };
    static Contadores& contadores();

    explicit VideoItem(QQuickItem* parent = nullptr);
    ~VideoItem() override;

    QString source() const { return source_; }
    void setSource(const QString& s);
    FillMode fillMode() const { return fillMode_; }
    void setFillMode(FillMode m);
    bool hasFrame() const { return !image_.isNull(); }
    QSize frameSize() const { return image_.size(); }
    bool enVista() const { return enVista_; }
    void setEnVista(bool si);
    int topeCps() const { return topeCps_; }
    void setTopeCps(int n);

    /// Lo fija el arranque de la UI (unico servicio de frames; el banco, uno falso).
    static void setService(ProveedorCuadros* service);

   signals:
    void sourceChanged();
    void fillModeChanged();
    void hasFrameChanged();
    void frameSizeChanged();
    void enVistaChanged();
    void topeCpsChanged();

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
    /// Cada cuánto toma un cuadro (µs; 0: todos), según topeCps y el tamaño.
    std::int64_t intervaloUs() const;
    /// Trae lo último que haya, sin esperar el turno (al cambiar de fuente, volver a la vista…).
    void traer(bool sinTurno);

    QString source_ = "program";
    FillMode fillMode_ = Fit;
    QImage image_;
    std::uint64_t serial_ = 0;
    std::int64_t turno_ = -1; ///< el turno del reloj (ahora / intervalo) del último cuadro tomado
    int topeCps_ = -1;
    bool dirty_ = false;
    bool enVista_ = true;
    static QPointer<ProveedorCuadros> service_;
};

} // namespace ba::app
