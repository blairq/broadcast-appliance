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
// kCpsChico cuadros por segundo (topeCps): toma uno de cada tantos según lo que
// AVANZA el video (la hora de origen de cada cuadro, ProveedorCuadros::origenUs),
// no según un turno del reloj. Antes era un turno del reloj común a todos los
// chicos (ahora / 66 ms): un video de 30 cuyos cuadros caían cerca del cambio de
// turno tomaba a veces el de 33 ms y a veces el de 100 ms después (2026-11-05,
// «los invitados se mueven a saltitos aunque sean dos»).
// La textura es una sola por item y se reescribe en cada cuadro (sin crear y
// soltar recursos de GPU por cuadro); se rehace solo si cambia el tamaño.
// ============================================================================

#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "diag/RitmoCuadros.hpp"

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

    /// La FLUIDEZ de cada vista, siempre (diag/RitmoCuadros.hpp: anotar es barato): cada
    /// cuánto toma un cuadro nuevo (`tomado`, hilo de la UI), cada cuánto lo sube a la GPU
    /// para dibujarlo (`subido`, hilo de render), cuánto AVANZA lo que muestra entre uno y el
    /// siguiente (`contenido`: la hora de origen de cada cuadro tomado; 15 cuadros por segundo
    /// tomados parejos que muestran 33, 67, 33, 100 ms de video se ven a saltos) y los de la
    /// fuente que nunca tomó (`salteados`). Lo lee video_fluidez / la Prueba de sala.
    struct Ritmo {
        QString fuente;
        std::atomic<int> topeCps{0};     ///< el tope que usa ahora (0: todos)
        std::atomic<int> lado{0};        ///< el lado más largo, px lógicos
        std::atomic<bool> activa{false}; ///< visible, en la vista y no diminuta
        ba::diag::RitmoCuadros tomado, subido, contenido;
        std::atomic<std::uint64_t> salteados{0};
    };
    /// Las vistas vivas (una copia de los punteros: cada Ritmo vive lo que la que más lo use).
    static std::vector<std::shared_ptr<Ritmo>> ritmos();
    /// El ritmo de los cuadros de cada ventana (frameSwapped, hilo de render): lo que de
    /// verdad llega a la pantalla. {titulo, ritmo}.
    struct RitmoVentana {
        QString titulo;
        ba::diag::RitmoCuadros swaps;
    };
    static std::vector<std::shared_ptr<RitmoVentana>> ritmosVentanas();

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
    /// Trae lo último que haya, sin esperar a que le toque (al cambiar de fuente, volver a la vista…).
    void traer(bool sinTurno);

    QString source_ = "program";
    FillMode fillMode_ = Fit;
    QImage image_;
    std::uint64_t serial_ = 0;
    std::int64_t origenTomado_ = 0; ///< la hora de origen del último cuadro tomado (0 = no se sabe)
    std::int64_t tomadoUs_ = 0;     ///< cuándo se tomó (si la fuente no dice su hora de origen)
    int topeCps_ = -1;
    std::shared_ptr<Ritmo> ritmo_;
    bool dirty_ = false;
    bool enVista_ = true;
    static QPointer<ProveedorCuadros> service_;
};

} // namespace ba::app
