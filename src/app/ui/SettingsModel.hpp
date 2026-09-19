// ============================================================================
// src/app/ui/SettingsModel.hpp — Configuracion completa (capa ViewModel)
//
// Patron: ESQUEMA DECLARATIVO. Cada opcion del appliance es una fila con
// clave, seccion, etiqueta, tipo, rango, opciones, ayuda y ALCANCE:
//   live      se aplica en el momento (si no hay salidas activas cuando hace
//             falta, queda para el proximo arranque)
//   gateway   al aplicar se reinicia B (relee gateway.json)
//   restart   rige desde el proximo arranque (launcher.json)
// La vista arma los formularios de forma generica segun `kind`; agregar una
// opcion es agregar una linea al esquema en el .cpp.
//
// Edicion transaccional: `setValue` escribe en un BORRADOR; `apply` aplica y
// guarda; `revert` descarta. `dirty` indica cambios sin aplicar.
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>

#include <vector>

namespace ba::app {

class AppRuntime;

class SettingsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QStringList sections READ sections CONSTANT)
   public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        SectionRole,
        LabelRole,
        KindRole, ///< "int" "bool" "text" "secret" "path" "choice"
        ValueRole,
        OptionsRole, ///< para "choice": lista de {value, label}
        MinRole,
        MaxRole,
        SuffixRole,
        HelpRole,
        ScopeRole, ///< "live" "gateway" "restart"
        ModifiedRole
    };

    explicit SettingsModel(AppRuntime& rt, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool dirty() const;
    QStringList sections() const;

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariant value(const QString& key) const;
    /// Aplica y guarda. :returns: resumen legible de lo que paso.
    Q_INVOKABLE QString apply();
    Q_INVOKABLE void revert() override;
    /// Vuelve a los valores por defecto la seccion (en el borrador).
    Q_INVOKABLE void resetSection(const QString& section);

   signals:
    void dirtyChanged();
    void applied(const QString& summary);

   private:
    struct Field {
        QString key;
        QString section;
        QString label;
        QString kind;
        QVariantList options;
        double min = 0;
        double max = 0;
        QString suffix;
        QString help;
        QString scope;
        QVariant def;
    };
    void load(); // configuracion actual -> borrador
    QVariantMap current() const;
    int rowOfKey(const QString& key) const;

    AppRuntime& rt_;
    std::vector<Field> fields_;
    QVariantMap saved_; // valores aplicados
    QVariantMap draft_; // valores en edicion
};

} // namespace ba::app
