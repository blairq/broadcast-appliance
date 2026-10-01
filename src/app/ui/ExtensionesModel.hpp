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

#include <memory>

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>

namespace ba::app {

class AppRuntime;

class ExtensionesModel : public QAbstractListModel {
    Q_OBJECT
    /// Cuántas hay (para que la interfaz no muestre el menú vacío).
    Q_PROPERTY(int cuantas READ rowCount NOTIFY cambio)

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
    /// La ficha de una, por id (vacío si no está).
    Q_INVOKABLE QVariantMap ficha(const QString& id) const;
    /// La URL del QML de esa extensión, para el Loader. Vacía si no se puede.
    Q_INVOKABLE QString vista(const QString& id) const;
    /// El nombre lindo de un `kind` de tarjeta («ext:reloj» → «Reloj»).
    Q_INVOKABLE QString tituloDeKind(const QString& kind) const;
    /// Los `kind` de tarjeta que ofrecen las extensiones usables.
    Q_INVOKABLE QStringList kinds() const;

    /// Avisa que se está por cargar esa tarjeta, para que el diario de
    /// arranque sepa a quién culpar si el programa no vuelve. Lo llama el
    /// Loader de QML justo antes de poner el `source`.
    Q_INVOKABLE void cargando(const QString& id);
    /// La tarjeta cargó (bien o mal, pero el programa sigue vivo).
    Q_INVOKABLE void cargada(const QString& id, bool bien, const QString& error = {});
    /// Vuelve a prender una que el diario había apagado.
    Q_INVOKABLE void prender(const QString& id);

    /// La carpeta donde viven, para mostrarla y para que el asistente escriba ahí.
    Q_INVOKABLE QString carpeta() const;

   signals:
    void cambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
