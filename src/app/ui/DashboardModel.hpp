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
// Persistencia: <datos>/Config/ui.json. Las ventanas sueltas ya no van acá: son
// pantallas/Ventanas.hpp (ventanas.json), que trae las que había en ui.json.
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
    int nextId_ = 1;
};

} // namespace ba::app
