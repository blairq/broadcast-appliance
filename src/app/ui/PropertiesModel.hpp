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
// EL MOTOR ES DEL PLANO DE CONTROL (docs/arquitectura/05-motor-del-plano.md): lo que
// hace falta para armar las filas (el tipo, los ajustes, las cámaras del equipo y sus
// modos) se LEE en el dueño (`leer`) y vuelve por la ventanilla; los cambios se PIDEN
// (`aplicar`) y, mientras vuelven, la fila muestra el valor pedido. Al volver se relee:
// si las filas son las mismas (lo común) solo avisa las que cambiaron (dataChanged);
// si cambió su forma (una lista que depende de otro valor), las rehace.
//
// Las funciones estáticas no necesitan un modelo: el MCP (source_properties,
// source_set_property) las usa con las mismas reglas, sin tocar el panel de la vista.
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QVariant>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ba::app {

class AppRuntime;
class VentanillaMotor;

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
    /// Lo que hace falta para armar las filas de una fuente (se lee en el dueño del motor).
    struct Lectura {
        bool existe = false;
        std::string tipo;
        nlohmann::json ajustes = nlohmann::json::object(); ///< con los valores por defecto del tipo
        bool conCamara = false; ///< la fila de la cámara (una cámara o una entrada de GStreamer vacía)
        std::vector<std::pair<std::string, std::string>> camaras; ///< las del equipo: {valor, nombre}
        std::vector<std::pair<std::string, std::string>> modos;   ///< los de la elegida: {valor, etiqueta}
        std::string automatico, porque; ///< el modo automático y el porqué
    };

    PropertiesModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~PropertiesModel() override;
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString sourceName() const { return source_; }
    void setSourceName(const QString& name);

    Q_INVOKABLE void setValue(const QString& prop, const QVariant& value);
    Q_INVOKABLE void press(const QString& prop); // propiedades tipo boton

    // ---- Sin modelo (el MCP), desde la interfaz: lo del motor se pide por la ventanilla ----
    /// Las filas de la fuente `nombre` (leída en el dueño; armadas en la interfaz, porque
    /// las opciones de algunos tipos son de la interfaz: los PDF del evento). `listo` corre
    /// en el hilo de `quien` (Lectura::existe dice si estaba).
    static void leerFilas(VentanillaMotor& v, AppRuntime& rt, QObject* quien, const std::string& nombre,
                          std::function<void(const Lectura&, std::vector<Row>)> listo);
    /// Cambia la propiedad `prop` (el valor como lo manda la vista o el MCP) o aprieta el
    /// botón (`apretar`); en `listo`, "" o el error y los ajustes como quedaron.
    static void cambiar(VentanillaMotor& v, AppRuntime& rt, QObject* quien, const std::string& nombre,
                        const std::string& prop, const QVariant& valor, bool apretar,
                        std::function<void(const std::string& error, const std::string& ajustes)> listo);
    // ---- Puras ----
    static std::vector<Row> filas(const Lectura& l);
    /// Las filas con las claves de los roles (name, label, kind, value, options, minimum…).
    static nlohmann::json filasJson(const std::vector<Row>& filas);

   signals:
    void sourceNameChanged();

   private:
    /// EN EL DUEÑO: lo que hay de la fuente `nombre`.
    static Lectura leer(AppRuntime& rt, const std::string& nombre);
    /// EN EL DUEÑO: cambia la propiedad `prop` de tipo `kind` (la fila) o aprieta el botón.
    /// "" o el error.
    static std::string aplicar(AppRuntime& rt, const std::string& nombre, const std::string& prop, const QString& kind,
                               const QVariant& valor, bool apretar);
    /// Pide la lectura de la fuente al dueño; al volver, ponerFilas.
    void releer();
    void ponerFilas(Lectura l);
    /// El cambio de ajustes que pide `valor` para la fila `kind` (las fuentes, sobre lo que hay).
    static nlohmann::json cambioDe(const QString& kind, const std::string& clave, const QVariant& valor,
                                   const nlohmann::json& ajustes);

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    QString source_;
    std::string tipo_; ///< el de la última lectura (si cambia en la foto, se relee)
    std::vector<Row> rows_;
    unsigned lecturas_ = 0; ///< para quedarse solo con la última lectura pedida
};

} // namespace ba::app
