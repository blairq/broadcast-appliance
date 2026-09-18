// ============================================================================
// src/app/ui/PropertiesModel.hpp — Propiedades de una fuente (capa ViewModel)
//
// Adaptador del sistema de propiedades de libobs (obs_properties): cada fuente
// describe sus opciones (camara, resolucion, archivo, texto, color...) y este
// modelo las convierte en filas genericas que la vista dibuja por tipo. Asi
// CUALQUIER fuente de cualquier plugin se configura sin codigo a medida.
//
// Tipos que expone (`kind`): bool, int, float, text, textarea, password, path,
// list, color, font, button, info. Los grupos se aplanan con un encabezado.
//
// Los cambios se aplican en vivo (obs_source_update) y quedan en la
// configuracion via AppRuntime. Si una propiedad cambia a otras (p. ej. elegir
// una camara recarga sus resoluciones) el modelo se reconstruye.
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
    void* props_ = nullptr; // obs_properties_t* vigente (para botones)
};

} // namespace ba::app
