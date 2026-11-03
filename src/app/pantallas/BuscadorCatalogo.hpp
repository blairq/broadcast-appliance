// ============================================================================
// src/app/pantallas/BuscadorCatalogo.hpp — buscar en el catálogo de lo que se puede mostrar
//
// El catálogo (pantallas/Catalogo.hpp) puede tener muchísimas cosas: cada escena,
// cada invitado y su pantalla, cada cámara, cada página, cada distribución de
// pantallas guardada. Elegir de un ComboBox con cientos de filas no se puede; el
// selector (controls/SelectorCosas.qml, un modal con búsqueda y vista previa)
// mira ESTE modelo: un QSortFilterProxyModel sobre el MISMO catálogo, que filtra
// en C++ (nada se recorre en QML) y que la ListView virtualiza.
//
//   texto     todas las palabras, sin tildes ni mayúsculas (FiltroCatalogo::plano),
//             contra el título, el nombre del grupo («Escena», «Distribución») y el id.
//             Se aplica con un rebote corto: escribir rápido no filtra en cada tecla.
//   grupo     uno solo («escena», «video», «distribucion»…) o "" = todos.
//   donde     "monitor" (lo que se dibuja en esta máquina), "red" (lo que va a una
//             TV) o "todo".
//   excluir   grupos que este selector no ofrece (una ventana de una distribución no
//             puede mostrar otra distribución).
//
// Lo elegido también es del modelo (`elegido`, `actual`): la vista previa se enlaza
// a `actual` y se actualiza sola si la cosa cambia (una escena renombrada) o
// desaparece. `grupos` dice qué grupos hay con cuántas cosas cada uno según lo
// escrito, para los botones de filtro por tipo.
//
// Se lee el catálogo por los NOMBRES de sus roles (itemId, grupo, grupoTitulo,
// titulo, etiqueta, local, red): así esto no depende de la clase del catálogo y la
// prueba de carga de la interfaz lo usa con un modelo cualquiera.
// ============================================================================
#pragma once

#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

class BuscadorCatalogo : public QSortFilterProxyModel {
    Q_OBJECT
    /// El catálogo (Studio.catalogo).
    Q_PROPERTY(QObject* fuente READ fuente WRITE setFuente NOTIFY fuenteCambio)
    Q_PROPERTY(QString texto READ texto WRITE setTexto NOTIFY textoCambio)
    Q_PROPERTY(QString grupo READ grupo WRITE setGrupo NOTIFY grupoCambio)
    Q_PROPERTY(QString donde READ donde WRITE setDonde NOTIFY dondeCambio)
    Q_PROPERTY(QStringList excluir READ excluir WRITE setExcluir NOTIFY excluirCambio)
    /// Cuántas se ven (con lo escrito y el grupo).
    Q_PROPERTY(int cantidad READ cantidad NOTIFY resultadoCambio)
    /// Cuántas hay para este selector (con `donde` y `excluir`, sin lo escrito ni el grupo).
    Q_PROPERTY(int total READ total NOTIFY resultadoCambio)
    /// [{grupo, titulo, cuenta}]: los grupos que hay, en el orden del catálogo, con
    /// cuántas coinciden con lo escrito (sin acotar por el grupo elegido).
    Q_PROPERTY(QVariantList grupos READ grupos NOTIFY resultadoCambio)
    /// El id de la elegida ("" = ninguna).
    Q_PROPERTY(QString elegido READ elegido WRITE setElegido NOTIFY elegidoCambio)
    /// Su fila en lo que se ve (-1 si lo escrito la deja afuera o no hay).
    Q_PROPERTY(int filaElegida READ filaElegida NOTIFY elegidoCambio)
    /// La elegida como mapa (los nombres de los roles del catálogo), o vacío si ya no está.
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY elegidoCambio)

   public:
    explicit BuscadorCatalogo(QObject* parent = nullptr);

    QObject* fuente() const { return sourceModel(); }
    void setFuente(QObject* f);
    QString texto() const { return texto_; }
    void setTexto(const QString& t);
    QString grupo() const { return grupo_; }
    void setGrupo(const QString& g);
    QString donde() const { return donde_; }
    void setDonde(const QString& d);
    QStringList excluir() const { return excluir_; }
    void setExcluir(const QStringList& e);
    int cantidad() const { return rowCount(); }
    int total() const { return total_; }
    QVariantList grupos() const { return grupos_; }
    QString elegido() const { return elegido_; }
    void setElegido(const QString& id);
    int filaElegida() const;
    QVariantMap actual() const;

    /// Elige la de esa fila de lo que se ve (un clic en la lista).
    Q_INVOKABLE void elegirFila(int fila);
    /// Corre la elegida (las flechas): -1 arriba, +1 abajo. Sin elegida, la primera.
    Q_INVOKABLE void mover(int delta);
    /// Aplica ya lo escrito, sin esperar el rebote (Enter, y las pruebas).
    Q_INVOKABLE void filtrarYa();

    /// Las palabras de una búsqueda, ya planas (sin tildes ni mayúsculas).
    static QStringList palabras(const QString& texto);
    /// Si todas las palabras están en `plano` (ya plano). Sin palabras, sí.
    static bool coincide(const QStringList& palabras, const QString& plano);
    /// El texto donde se busca una cosa, plano: título, grupo legible e id.
    static QString textoDeBusqueda(const QString& titulo, const QString& grupoTitulo, const QString& id);

   signals:
    void fuenteCambio();
    void textoCambio();
    void grupoCambio();
    void dondeCambio();
    void excluirCambio();
    void resultadoCambio();
    void elegidoCambio();

   protected:
    bool filterAcceptsRow(int fila, const QModelIndex& padre) const override;

   private:
    /// Si la fila entra por `donde` y `excluir` (lo fijo del selector).
    bool entraFijo(int fila) const;
    bool coincideTexto(int fila) const;
    QString grupoDe(int fila) const;
    void refiltrar();
    void recontar();

    QTimer rebote_;
    QString texto_, textoAplicado_, grupo_, donde_ = QStringLiteral("todo"), elegido_;
    QStringList excluir_, palabras_;
    int total_ = 0;
    QVariantList grupos_;
    // Los roles del catálogo, por nombre (-1 = no los tiene).
    int rolId_ = -1, rolGrupo_ = -1, rolGrupoTitulo_ = -1, rolTitulo_ = -1, rolLocal_ = -1, rolRed_ = -1;
};

} // namespace ba::app
