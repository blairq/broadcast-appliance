// ============================================================================
// src/app/ui/DashboardModel.hpp — Disposicion del dashboard (capa ViewModel)
//
// Que tarjetas hay, donde estan, que muestra cada una y que va de fondo. Las
// coordenadas son FRACCIONES del area del dashboard (0..1): la disposicion se
// adapta a cualquier tamaño de ventana.
//
// El encaje magnetico ("stick") vive aca y no en la vista: `snap()` ajusta un
// rectangulo propuesto a la grilla, a los bordes del area y a los bordes de
// las demas tarjetas (con separacion). La vista solo anima hacia el resultado.
//
// Persistencia: <datos>/Config/ui.json.
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QRect>
#include <QRectF>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <vector>

namespace ba::app {

/// Ventanas de salida: video sacado del dashboard a una ventana propia (por
/// ejemplo para mandarlo a otro monitor). Cada ventana muestra una o varias
/// fuentes con una distribucion; recuerda monitor, pantalla completa y
/// geometria. Las guarda el DashboardModel en ui.json.
class OutputWindowsModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Roles { IdRole = Qt::UserRole + 1, KeysRole, LayoutRole, ScreenRole, FullscreenRole, GeometryRole };
    explicit OutputWindowsModel(std::function<void()> onChange, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Abre una ventana con `keys` y `layout`. :returns: su id.
    Q_INVOKABLE int open(const QStringList& keys, const QString& layout);
    /// Como open, pero ya ubicada: en ese monitor, con esa geometría y, si se pide, en pantalla completa.
    int abrirEn(const QStringList& keys, const QString& layout, const QString& screen, const QRect& geometria,
                bool pantallaCompleta);
    Q_INVOKABLE void close(int id);
    Q_INVOKABLE void setScreen(int id, const QString& screenName);
    Q_INVOKABLE void setFullscreen(int id, bool on);
    Q_INVOKABLE void setLayout(int id, const QString& layout);
    Q_INVOKABLE void setGeometry(int id, int x, int y, int w, int h);

    struct Win {
        int id = 0;
        QStringList keys;
        QString layout = "grid";
        QString screen;
        bool fullscreen = false;
        QRect geometry{120, 120, 960, 540};
    };
    const std::vector<Win>& windows() const { return wins_; }
    void restore(std::vector<Win> wins);

   private:
    int rowOf(int id) const;
    void changed(int row, const QList<int>& roles);
    std::vector<Win> wins_;
    int nextId_ = 1;
    std::function<void()> onChange_;
};

class DashboardModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList background READ background WRITE setBackground NOTIFY backgroundChanged)
    Q_PROPERTY(double backgroundDim READ backgroundDim WRITE setBackgroundDim NOTIFY backgroundChanged)
    Q_PROPERTY(QStringList cardKinds READ cardKinds CONSTANT)
    /// Divisiones de la grilla de encaje (para dibujarla mientras se acomoda).
    Q_PROPERTY(int gridCols READ gridCols CONSTANT)
    Q_PROPERTY(int gridRows READ gridRows CONSTANT)
    /// Distribucion de las fuentes del fondo: "grid" | "focus" | "pip" | "row".
    Q_PROPERTY(
        QString backgroundLayout READ backgroundLayout WRITE setBackgroundLayout NOTIFY backgroundChanged)
    Q_PROPERTY(QObject* windows READ windows CONSTANT)
    /// Para la barra de tarjetas: [{kind, titulo, icono, abierta (hay una en el tablero)}].
    Q_PROPERTY(QVariantList tiposDeTarjeta READ tiposDeTarjeta NOTIFY tarjetasCambio)
   public:
    enum Roles { IdRole = Qt::UserRole + 1, KindRole, XRole, YRole, WRole, HRole, SourceRole, TitleRole };

    explicit DashboardModel(std::filesystem::path file, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList background() const { return background_; }
    void setBackground(const QStringList& keys);
    double backgroundDim() const { return dim_; }
    void setBackgroundDim(double d);
    QString backgroundLayout() const { return layout_; }
    void setBackgroundLayout(const QString& l);
    QObject* windows() const { return windows_; }

    /// Tipos de tarjeta que se pueden agregar.
    QStringList cardKinds() const;
    Q_INVOKABLE QString kindTitle(const QString& kind) const;
    QVariantList tiposDeTarjeta() const;
    /// La barra de tarjetas: si hay una de ese tipo en el tablero, la oculta (y se
    /// acuerda dónde estaba); si no, la muestra donde estaba la última vez (o
    /// centrada). true si quedó a la vista.
    Q_INVOKABLE bool alternarTarjeta(const QString& kind);

    /// Agrega una tarjeta (`source`: clave de video para "monitor").
    Q_INVOKABLE void addCard(const QString& kind, const QString& source = {});
    Q_INVOKABLE void removeCard(int id);
    Q_INVOKABLE void setCardSource(int id, const QString& source);

    /// Ajusta un rectangulo propuesto (fracciones) para la tarjeta `id`:
    /// grilla, bordes del area y bordes de las otras tarjetas.
    /// `aspect` = ancho/alto del area en pixeles (para separaciones parejas).
    int gridCols() const;
    int gridRows() const;

    Q_INVOKABLE QRectF snap(int id, double x, double y, double w, double h, double aspect) const;

    /// Fija la ubicacion final de la tarjeta (ya ajustada) y guarda.
    Q_INVOKABLE void setCardRect(int id, double x, double y, double w, double h);

    /// Trae la tarjeta al frente (la ultima en dibujarse).
    Q_INVOKABLE void raise(int id);

    /// Vuelve a la disposicion por defecto.
    Q_INVOKABLE void resetLayout();

   signals:
    void backgroundChanged();
    void tarjetasCambio();

   private:
    struct Card {
        int id = 0;
        QString kind;
        QRectF rect;
        QString source;
    };
    void load();
    void save() const;
    void defaults();
    int rowOf(int id) const;

    std::filesystem::path file_;
    std::vector<Card> cards_;
    /// Las ocultas con la barra: dónde estaban y qué mostraban, para volver igual.
    std::vector<Card> ocultas_;
    QStringList background_;
    double dim_ = 0.35;
    QString layout_ = "grid";
    OutputWindowsModel* windows_ = nullptr;
    int nextId_ = 1;
};

} // namespace ba::app
