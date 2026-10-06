// ============================================================================
// src/app/ui/PropertiesModel.hpp — Propiedades de una fuente (capa ViewModel)
//
// Cada tipo de fuente describe sus opciones (FuentesApp: camara, archivo,
// texto, color...) y este modelo las convierte en filas genericas que la vista
// dibuja por tipo. Un tipo sin propiedades registradas muestra sus ajustes tal
// cual (bool, número, texto), así cualquier fuente se configura igual.
//
// Tipos que expone (`kind`): bool, int, float, text, textarea, password, path,
// list, color, font, button, info. Los grupos se aplanan con un encabezado.
//
// Los cambios se aplican en vivo y quedan en la configuracion via AppRuntime;
// despues de cada cambio el modelo se reconstruye (las filas pueden depender
// de los valores).
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QVariant>

#include <vector>

namespace ba::app {

class AppRuntime;

class PropertiesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString sourceName READ sourceName WRITE setSourceName NOTIFY sourceNameChanged)
   public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        LabelRole,
        KindRole,
        ValueRole,
        OptionsRole,
        MinRole,
        MaxRole,
        StepRole,
        SuffixRole,
        EnabledRole,
        HelpRole
    };
    explicit PropertiesModel(AppRuntime& rt, QObject* parent = nullptr);
    ~PropertiesModel() override;
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString sourceName() const { return source_; }
    void setSourceName(const QString& name);

    Q_INVOKABLE void setValue(const QString& prop, const QVariant& value);
    Q_INVOKABLE void press(const QString& prop); // propiedades tipo boton

   signals:
    void sourceNameChanged();

   private:
    struct Row {
        QString name;
        QString label;
        QString kind;
        QVariant value;
        QVariantList options;
        double min = 0;
        double max = 0;
        double step = 1;
        QString suffix;
        bool enabled = true;
        QString help;
    };
    void rebuild();

    AppRuntime& rt_;
    QString source_;
    std::vector<Row> rows_;
};

} // namespace ba::app
