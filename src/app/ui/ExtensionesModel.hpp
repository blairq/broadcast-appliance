// ============================================================================
// src/app/ui/ExtensionesModel.hpp — tarjetas que no vienen adentro del programa
//
// Es el primer punto de inserción de verdad: una carpeta en el disco con un
// QML adentro aparece como una tarjeta más del dashboard, al lado de las que
// vienen compiladas.
//
//     Extensiones/<id>/
//     ├── extension.json   nombre, descripción, qué necesita
//     └── tarjeta.qml      lo que se dibuja
//
// POR QUÉ EL DASHBOARD Y NO OTRA COSA. Porque ese punto ya existía y ya era
// dinámico: la tarjeta se resuelve por nombre en tiempo de ejecución y su
// disposición ya se guarda en Config/ui.json, con el encaje magnético y el
// botón «+ Tarjeta» hechos. No hubo que construir un sistema de tarjetas:
// hubo que dejar que el que ya existe lea de otro lado.
//
// UN PLUGIN NO MODIFICA LA INTERFAZ: OCUPA UN LUGAR QUE LA INTERFAZ OFRECE.
// La diferencia es todo. Lo primero no se puede sostener entre versiones; lo
// segundo sí, porque el lugar lo declara el anfitrión.
//
// QUÉ VE UNA TARJETA: el `Studio` (lo mismo que ven todas las páginas) y un
// objeto `Ext` con lo suyo. NO es un sandbox, y conviene decirlo: QML con
// acceso a Studio puede hacer bastante. El modelo de amenaza acá no es el
// código malicioso —es tu máquina, tu drive, tu asistente— sino el código
// ROTO, y para eso está el diario de arranque: una tarjeta que cuelga la
// interfaz queda apagada sola y el programa vuelve a abrir sin ella.
// ============================================================================
#pragma once

#include <functional>
#include <memory>
#include <set>

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>

namespace ba::app {

class AppRuntime;

class ExtensionesModel : public QAbstractListModel {
    Q_OBJECT
    /// Cuántas hay (para que la interfaz no muestre el menú vacío).
    Q_PROPERTY(int cuantas READ rowCount NOTIFY cambio)
    /// Los `kind` de tarjeta que ofrecen las extensiones usables (las apagadas no).
    Q_PROPERTY(QStringList kinds READ kinds NOTIFY cambio)
    /// La ficha de cada una, por id: {id: {id, nombre, descripcion, vista, problema,
    /// apagada, cargable, motivoNoCarga}}. `cargable` es la URL que se le da al Loader (""
    /// si ahora no se puede: apagada, o en vivo y no se cargó en esta sesión) y
    /// `motivoNoCarga`, por qué no (en vivo). Cambia al refrescar, al apagar o prender una
    /// y al entrar o salir del modo en vivo.
    Q_PROPERTY(QVariantMap fichas READ fichas NOTIFY cambio)
    /// El nombre lindo de cada `kind` de tarjeta de extensión: {«ext:reloj»: «Reloj»}.
    Q_PROPERTY(QVariantMap titulos READ titulos NOTIFY cambio)

   public:
    /// El prefijo que distingue una tarjeta de extensión de las de fábrica.
    /// Va en el `kind` de la tarjeta del dashboard, así que queda guardado en
    /// ui.json y sobrevive al reinicio.
    static constexpr const char* kPrefijo = "ext:";

    enum Roles {
        IdRole = Qt::UserRole + 1,
        NombreRole,
        DescripcionRole,
        VistaRole,    ///< URL del QML, lista para un Loader
        ProblemaRole, ///< por qué no se puede usar (vacío = está bien)
        ApagadaRole,  ///< la apagó el diario de arranque
    };

    explicit ExtensionesModel(AppRuntime& rt, QObject* parent = nullptr);
    ~ExtensionesModel() override;

    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Vuelve a mirar la carpeta. El asistente escribe una extensión y esto la
    /// trae sin reiniciar.
    Q_INVOKABLE void refrescar();
    /// La ficha de una, por id (vacío si no está): la de `fichas` (el MCP).
    QVariantMap ficha(const QString& id) const;
    QVariantMap fichas() const;
    QVariantMap titulos() const;
    /// Los `kind` de tarjeta que ofrecen las extensiones usables.
    QStringList kinds() const;

    /// Avisa que se está por cargar esa tarjeta, para que el diario de
    /// arranque sepa a quién culpar si el programa no vuelve. Lo llama el
    /// Loader de QML justo antes de poner el `source` (el `cargable` de su
    /// ficha): si se puede cargar, queda como cargada en esta sesión (en vivo
    /// se sigue cargando).
    Q_INVOKABLE void cargando(const QString& id);
    /// La tarjeta cargó (bien o mal, pero el programa sigue vivo).
    Q_INVOKABLE void cargada(const QString& id, bool bien, const QString& error = {});
    /// Vuelve a prender una que el diario había apagado.
    Q_INVOKABLE void prender(const QString& id);
    /// Modo en vivo (T48): mientras devuelva true, una extensión que no se cargó en
    /// esta sesión no se carga (su `cargable` es "" y `motivoNoCarga` lo explica).
    void setEnVivo(std::function<bool()> f) { enVivo_ = std::move(f); }
    /// Entró o salió el modo en vivo: cambia lo que se puede cargar (las fichas).
    void alCambiarVivo() { emit cambio(); }
    /// Se pasó del tiempo máximo cargando (el Loader asíncrono): se corta y se apaga.
    Q_INVOKABLE void tardo(const QString& id, int ms);

    /// La carpeta donde viven, para mostrarla y para que el asistente escriba ahí.
    Q_INVOKABLE QString carpeta() const;

   signals:
    void cambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    std::function<bool()> enVivo_;
    std::set<QString> yaCargadas_; ///< las que se cargaron en esta sesión
    /// Lo que se le da al Loader ("" si ahora no se puede) y por qué no (en vivo).
    QString cargable(const QString& id, const QString& vista) const;
    QString motivoNoCarga(const QString& id) const;
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
