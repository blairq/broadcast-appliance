// ============================================================================
// src/app/ui/PropertiesModel.cpp — ver PropertiesModel.hpp
// ============================================================================

#include "ui/PropertiesModel.hpp"

#include <QColor>
#include <QVariantMap>

#include "AppRuntime.hpp"
#include "FuentesApp.hpp"

#include <nlohmann/json.hpp>

namespace ba::app {

namespace {

using json = nlohmann::json;

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Color de los ajustes (0xAABBGGRR, como OBS) <-> "#RRGGBB".
QString colorToHex(long long v) {
    const auto u = static_cast<unsigned long long>(v);
    return QColor(int(u & 0xFF), int((u >> 8) & 0xFF), int((u >> 16) & 0xFF)).name();
}
long long hexToColor(const QString& hex) {
    const QColor c(hex);
    return 0xFF000000LL | (static_cast<long long>(c.blue()) << 16) |
           (static_cast<long long>(c.green()) << 8) | c.red();
}

QVariant aVariant(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return static_cast<qlonglong>(v.get<long long>());
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) return qs(v.get<std::string>());
    return {};
}

/// Etiqueta legible de una clave sin tipo registrado ("show_cursor" → "Show cursor").
QString etiquetaDe(const std::string& k) {
    QString l = qs(k);
    l.replace('_', ' ');
    if (!l.isEmpty()) l[0] = l[0].toUpper();
    return l;
}

} // namespace

PropertiesModel::PropertiesModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {}

PropertiesModel::~PropertiesModel() = default;

int PropertiesModel::rowCount(const QModelIndex&) const { return static_cast<int>(rows_.size()); }

QVariant PropertiesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Row& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case NameRole: return r.name;
        case LabelRole: return r.label;
        case KindRole: return r.kind;
        case ValueRole: return r.value;
        case OptionsRole: return r.options;
        case MinRole: return r.min;
        case MaxRole: return r.max;
        case StepRole: return r.step;
        case SuffixRole: return r.suffix;
        case EnabledRole: return r.enabled;
        case HelpRole: return r.help;
        default: return {};
    }
}

QHash<int, QByteArray> PropertiesModel::roleNames() const {
    return {{NameRole, "name"},       {LabelRole, "label"},     {KindRole, "kind"},   {ValueRole, "value"},
            {OptionsRole, "options"}, {MinRole, "minimum"},     {MaxRole, "maximum"}, {StepRole, "step"},
            {SuffixRole, "suffix"},   {EnabledRole, "enabled"}, {HelpRole, "help"}};
}

void PropertiesModel::setSourceName(const QString& name) {
    if (name == source_) {
        return;
    }
    source_ = name;
    emit sourceNameChanged();
    rebuild();
}

void PropertiesModel::rebuild() {
    beginResetModel();
    rows_.clear();
    const std::string nombre = source_.toStdString();
    const BroadcastEngine& e = rt_.engine();
    if (!e.sourceExists(nombre)) {
        endResetModel();
        return;
    }
    const std::string tipo = e.sourceTypeId(nombre);
    const json ajustes = fuentes::conDefectos(tipo, json::parse(e.sourceSettingsJson(nombre), nullptr, false));
    fuentes::Tipo t;
    if (fuentes::tipo(tipo, t) && t.props) {
        for (const auto& p : t.props(ajustes)) {
            Row r;
            r.name = qs(p.nombre);
            r.label = qs(p.etiqueta);
            r.kind = qs(p.tipo);
            r.min = p.min;
            r.max = p.max;
            r.step = p.paso;
            r.suffix = qs(p.sufijo);
            r.help = qs(p.ayuda);
            for (const auto& o : p.opciones)
                r.options << QVariantMap{{"value", aVariant(o.value("value", json()))},
                                         {"label", qs(o.value("label", std::string()))}};
            const json v = ajustes.value(p.nombre, json());
            if (p.tipo == "color") {
                r.value = colorToHex(v.is_number() ? v.get<long long>() : 0xFFFFFFFFLL);
            } else if (p.tipo == "font") {
                r.value = QVariantMap{{"face", qs(v.is_object() ? v.value("face", std::string()) : "")},
                                      {"size", static_cast<qlonglong>(v.is_object() ? v.value("size", 0) : 0)}};
            } else {
                r.value = aVariant(v);
            }
            rows_.push_back(r);
        }
    } else if (ajustes.is_object()) {
        // Un tipo sin propiedades registradas: lo que tiene, editable por su forma.
        for (auto it = ajustes.begin(); it != ajustes.end(); ++it) {
            Row r;
            r.name = qs(it.key());
            r.label = etiquetaDe(it.key());
            if (it->is_boolean()) {
                r.kind = "bool";
            } else if (it->is_number_integer()) {
                r.kind = "int";
                r.min = -1e9;
                r.max = 1e9;
            } else if (it->is_number()) {
                r.kind = "float";
                r.min = -1e9;
                r.max = 1e9;
                r.step = 0.01;
            } else if (it->is_string()) {
                const std::string& k = it.key();
                r.kind = k == "file" || k == "archivo" || k == "local_file" || k == "path" ? "path"
                         : it->get<std::string>().size() > 60                                ? "textarea"
                                                                                             : "text";
            } else {
                continue; // objetos y listas: por MCP (source_settings)
            }
            r.value = aVariant(*it);
            rows_.push_back(r);
        }
    }
    endResetModel();
}

void PropertiesModel::setValue(const QString& prop, const QVariant& value) {
    const std::string nombre = source_.toStdString();
    if (!rt_.engine().sourceExists(nombre)) {
        return;
    }
    const std::string key = prop.toStdString();
    QString kind;
    for (const auto& r : rows_) {
        if (r.name == prop) {
            kind = r.kind;
        }
    }
    json cambio = json::object();
    if (kind == "bool") {
        cambio[key] = value.toBool();
    } else if (kind == "int") {
        cambio[key] = value.toLongLong();
    } else if (kind == "float") {
        cambio[key] = value.toDouble();
    } else if (kind == "color") {
        cambio[key] = hexToColor(value.toString());
    } else if (kind == "font") {
        const QVariantMap m = value.toMap();
        json f = json::parse(rt_.engine().sourceSettingsJson(nombre), nullptr, false).value(key, json::object());
        if (!f.is_object()) f = json::object();
        f["face"] = m.value("face").toString().toStdString();
        f["size"] = m.value("size").toLongLong();
        cambio[key] = f;
    } else if (kind == "list" && (value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::Int)) {
        cambio[key] = value.toLongLong();
    } else if (kind == "list" && value.typeId() == QMetaType::Double) {
        cambio[key] = value.toDouble();
    } else {
        cambio[key] = value.toString().toStdString();
    }
    // Aplica y sincroniza la configuracion (todas las escenas que la usan).
    rt_.updateSourceSettings(nombre, cambio.dump());
    // Las filas pueden depender de los valores (listas que cambian): se rehacen.
    rebuild();
}

void PropertiesModel::press(const QString& prop) {
    fuentes::Tipo t;
    const std::string nombre = source_.toStdString();
    if (!fuentes::tipo(rt_.engine().sourceTypeId(nombre), t)) return;
    auto it = t.botones.find(prop.toStdString());
    if (it == t.botones.end()) return;
    it->second(nombre);
    rebuild();
}

} // namespace ba::app
