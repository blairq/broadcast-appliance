// ============================================================================
// src/app/pantallas/Ventanas.hpp — las ventanas sueltas, que se recuerdan
//
// «Mostrar X en una ventana» es UNA cosa: X es una cosa del catálogo (o varias
// en grilla) —el programa, una escena, una multivista, un invitado, el padrón,
// una página, una tarjeta del tablero— y la ventana se recuerda: a qué
// monitor fue, su tamaño y si estaba en pantalla completa. Al reiniciar vuelve
// igual.
//
// Antes había tres maneras: las ventanas de video (OutputWindowsModel, en
// ui.json, con claves de video sueltas y el monitor por su nombre del
// sistema), las páginas y paneles sacados (una lista en Main.qml que no se
// recordaba) y los monitores de Salidas → Pantallas. Ahora las ventanas son
// este modelo, con los mismos ids del catálogo y los mismos ids de monitor que
// Salidas → Pantallas (MonitoresModel); la diferencia con un monitor asignado
// es solo que una ventana se mueve a mano y, si su monitor no está, aparece en
// otro (un monitor asignado lo espera). Una ventana se puede dejar fija en su
// monitor («Dejar fijo en este monitor»: pasa a Salidas → Pantallas).
//
// LO QUE YA ESTABA GUARDADO NO SE PIERDE: si no hay ventanas.json todavía, se
// leen las ventanas de video de ui.json (`desdeUiViejo`) —las claves pasan a
// cosas del catálogo, «multiview» a la multivista que estaba elegida, y el
// monitor por nombre del sistema se traduce a su id estable apenas se lo ve
// conectado—, y se guarda ventanas.json en el acto.
//
// Persistencia: <datos>/Config/ventanas.json.
// ============================================================================
#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QRect>
#include <QStringList>
#include <QVariantMap>

#include <filesystem>
#include <vector>

namespace ba::app {

class CatalogoVisible;
class MonitoresModel;

/// Una ventana suelta.
struct FilaVentana {
    int id = 0; ///< de esta corrida (ventana_listar la llama «salida:<id>»)
    QStringList cosas;
    /// Con varias cosas de video: grid | focus | pip | row.
    QString distribucion = QStringLiteral("grid");
    /// El id estable del monitor (MonitoresModel::idDe), o vacío.
    QString monitor;
    /// Solo lo migrado de ui.json: el nombre del sistema del monitor, hasta que
    /// se lo ve conectado y se lo traduce a `monitor`.
    QString sistema;
    bool pantallaCompleta = false;
    /// Vacía = que la ventana elija (centrada en su monitor, o en uno libre).
    QRect geometria;
    bool operator==(const FilaVentana&) const = default;
};

class VentanasModel : public QAbstractListModel {
    Q_OBJECT
    /// Las cosas que están SOLAS en una ventana (para «ya está en su ventana» y
    /// el «traer» de las páginas, enlazado: cambia cuando se abre o se cierra).
    Q_PROPERTY(QStringList afuera READ afuera NOTIFY cambio)
    Q_PROPERTY(int cantidad READ cantidad NOTIFY cambio)

   public:
    enum Roles {
        VentanaIdRole = Qt::UserRole + 1,
        CosasRole,
        DistribucionRole,
        MonitorRole,
        SistemaRole,
        PantallaCompletaRole,
        GeometriaRole,
        TituloRole,
    };

    /// `archivo` vacío = no se guarda (modo seguro, tests). `catalogo` y
    /// `monitores` pueden faltar (tests): entonces no se valida ni se traduce.
    VentanasModel(std::filesystem::path archivo, CatalogoVisible* catalogo, MonitoresModel* monitores,
                  QObject* parent = nullptr);

    int rowCount(const QModelIndex& padre = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Si todavía no hay ventanas.json, trae las ventanas de video de ui.json.
    /// true si migró algo.
    bool migrarDesde(const std::filesystem::path& uiJson, const QString& multiviewElegida);
    /// Las ventanas de video de ui.json (OutputWindowsModel) como filas.
    static std::vector<FilaVentana> desdeUiViejo(const QJsonArray& windows, const QString& multiviewElegida);
    /// La cosa del catálogo que muestra una clave de video vieja.
    static QString cosaDeClave(const QString& clave);

    /// Abre una ventana con estas cosas. Si ya hay una que muestra exactamente
    /// eso, no abre otra: la trae al frente. Devuelve su id, o 0 (con `error`).
    int abrirCon(const QStringList& cosas, const QString& distribucion, QString* error = nullptr);
    /// Como abrirCon, pero ya ubicada en ese monitor (id estable), con esa geometría.
    int abrirEn(const QStringList& cosas, const QString& distribucion, const QString& monitor,
                const QRect& geometria, bool pantallaCompleta, QString* error = nullptr);
    Q_INVOKABLE int abrir(const QStringList& cosas, const QString& distribucion = {});
    /// Claves de video sueltas («program», «guest:1»): las traduce a cosas.
    Q_INVOKABLE int abrirVideo(const QStringList& claves, const QString& distribucion = {});
    Q_INVOKABLE void cerrar(int id);
    /// Cierra la ventana donde está sola esa cosa (si hay).
    Q_INVOKABLE void cerrarCosa(const QString& cosa);
    /// La ventana donde está SOLA esa cosa, o 0.
    Q_INVOKABLE int ventanaDe(const QString& cosa) const;
    /// "" o el error.
    Q_INVOKABLE QString setCosas(int id, const QStringList& cosas);
    Q_INVOKABLE void setDistribucion(int id, const QString& distribucion);
    Q_INVOKABLE void setMonitor(int id, const QString& monitor);
    Q_INVOKABLE void setPantallaCompleta(int id, bool si);
    /// Solo guarda (no avisa): la ventana ya está ahí, y avisar peleaba con el arrastre.
    Q_INVOKABLE void setGeometria(int id, int x, int y, int w, int h);
    /// La fila como mapa (los nombres de los roles), o vacío.
    Q_INVOKABLE QVariantMap ventana(int id) const;

    QStringList afuera() const;
    int cantidad() const { return rowCount(); }
    const std::vector<FilaVentana>& ventanas() const { return filas_; }

   signals:
    void cambio();
    /// Se pidió abrir algo que ya estaba abierto: la ventana se trae al frente.
    void traerAlFrente(int id);

   private:
    int filaDe(int id) const;
    QString titulo(const FilaVentana& f) const;
    /// "" o por qué no se pueden mostrar esas cosas en una ventana.
    QString validar(QStringList& cosas) const;
    void tocada(int fila, const QList<int>& roles = {});
    /// Los monitores migrados por nombre del sistema, a su id estable.
    void traducirMonitores();
    void insertar(FilaVentana f);
    void cargar();
    void guardar() const;

    std::filesystem::path archivo_;
    CatalogoVisible* catalogo_ = nullptr;
    MonitoresModel* monitores_ = nullptr;
    std::vector<FilaVentana> filas_;
    int proximo_ = 1;
};

} // namespace ba::app
