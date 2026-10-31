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

#include <functional>
#include <vector>

namespace ba::app {

class AppRuntime;
namespace sala {
class ConfigSala;
}

class SettingsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QStringList sections READ sections CONSTANT)
    /// El borrador entero (clave -> valor): para que la vista muestre u oculte campos
    /// según otro (p. ej. el dominio de pinggy según «Dirección pública»).
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
    /// El idioma guardado no es el que se está usando: cambia al reiniciar el programa.
    Q_PROPERTY(bool idiomaPendiente READ idiomaPendiente NOTIFY idiomaPendienteChanged)
    /// Los codificadores de video que se pueden elegir ([{value, label}], los de
    /// «encoder.video»): también los usa la página Salidas para el de cada salida.
    Q_PROPERTY(QVariantList codificadores READ codificadores CONSTANT)
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
        ModifiedRole,
        AvanzadoRole ///< de los «avanzados» (ajustes/Registro): se ven con «Mostrar ajustes avanzados»
    };

    explicit SettingsModel(AppRuntime& rt, QObject* parent = nullptr);
    ~SettingsModel() override;
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool dirty() const;
    bool idiomaPendiente() const;
    QStringList sections() const;
    QVariantMap draft() const { return draft_; }
    QVariantList codificadores() const;

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariant value(const QString& key) const;
    /// El valor guardado (el que rige), sin lo que está a medio editar.
    QVariant savedValue(const QString& key) const { return saved_.value(key); }
    /// Aplica y guarda. :returns: resumen legible de lo que paso.
    Q_INVOKABLE QString apply();
    Q_INVOKABLE void revert() override;
    /// Vuelve a los valores por defecto la seccion (en el borrador).
    Q_INVOKABLE void resetSection(const QString& section);
    /// El alcance de un ajuste (live, gateway, motor, restart; "" si no existe).
    Q_INVOKABLE QString alcance(const QString& key) const;
    /// Si la sección tiene campos que no son avanzados (si no, se muestran siempre).
    Q_INVOKABLE bool tieneBasicos(const QString& section) const;
    /// Si la sección tiene avanzados (el interruptor se muestra solo donde sirve).
    Q_INVOKABLE bool tieneAvanzados(const QString& section) const;
    /// La etiqueta de una sección en el idioma del operador. El nombre de la sección
    /// es la clave (la página la compara, el MCP la busca): se traduce solo al mostrarla.
    /// Una que no está en la tabla sale tal cual.
    Q_INVOKABLE QString seccionVisible(const QString& seccion) const;
    /// Si el último apply() no aplicó (en vivo) o no pudo guardar. Para no buscar
    /// «EN VIVO» o «ERROR» en el resumen, que va en el idioma del operador.
    bool fallo() const { return fallo_; }
    /// Modo en vivo (T48): mientras devuelva true, lo que reinicia el gateway o el
    /// programa no se aplica (corta a los invitados o el aire).
    void setEnVivo(std::function<bool()> fn) { enVivo_ = std::move(fn); }
    /// Cambiar el cupo de invitados sin reiniciar la sala (T33). "" o el error.
    void setCupoEnCaliente(std::function<std::string(int)> fn) { cupoEnCaliente_ = std::move(fn); }
    /// La configuración de la sala en caliente (F3-06): sin esto, todo lo de la sala
    /// queda guardado para cuando arranque.
    void setConfigSala(sala::ConfigSala* c) { configSala_ = c; }

   signals:
    void dirtyChanged();
    void draftChanged();
    void applied(const QString& summary);
    void idiomaPendienteChanged();
    /// El reinicio de la sala que pidió apply() terminó (corre aparte: no traba la
    /// interfaz). `error` vacío = hecho.
    void salaReiniciada(const QString& error);
    /// La sala no tomó algo en caliente (corre aparte, como salaReiniciada).
    void avisoSala(const QString& texto);

   private:
    std::function<std::string(int)> cupoEnCaliente_;
    sala::ConfigSala* configSala_ = nullptr;
    int observador_ = 0;     ///< el aviso «Ajustes» del runtime (otro formulario aplicó)
    bool aplicando_ = false; ///< apply() en curso: su propio aviso no lo recarga
    bool fallo_ = false;     ///< el último apply() no aplicó o no guardó (fallo())
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
        QString scope; ///< live | gateway (reinicia el servidor) | motor (rearma el motor: corta lo que sale) | restart
        QVariant def;
        bool avanzado = false;
        int destino = 0; ///< avanzados: 0 app.json, 1 gateway.json, 2 launcher.json ("ajustes")
    };
    void load(); // configuracion actual -> borrador
    /// Otro aplicó: lo guardado se relee y los campos que no se estaban editando lo toman.
    void traerDeAfuera();
    QVariantMap current() const;
    QVariantMap currentBasicos() const; ///< los de siempre (sin los avanzados)
    int rowOfKey(const QString& key) const;

    AppRuntime& rt_;
    std::function<bool()> enVivo_;
    std::string idiomaAlArrancar_; ///< el guardado al abrir el programa (idiomaPendiente)
    std::vector<Field> fields_;
    QVariantMap saved_; // valores aplicados
    QVariantMap draft_; // valores en edicion
};

} // namespace ba::app
