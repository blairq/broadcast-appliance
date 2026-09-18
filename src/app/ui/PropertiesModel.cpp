// ============================================================================
// src/app/ui/PropertiesModel.cpp — ver PropertiesModel.hpp
// ============================================================================

#include "ui/PropertiesModel.hpp"

#include <QColor>
#include <QVariantMap>

#include <obs.h>

#include "AppRuntime.hpp"

namespace ba::app {

namespace {

QString qs(const char* s) { return s != nullptr ? QString::fromUtf8(s) : QString(); }

obs_source_t* src(AppRuntime& rt, const QString& name) {
    return reinterpret_cast<obs_source_t*>(rt.engine().sourceHandle(name.toStdString()));
}

/// Color de libobs (0xAABBGGRR) <-> "#RRGGBB".
QString colorToHex(long long v) {
    const auto u = static_cast<unsigned long long>(v);
    return QColor(int(u & 0xFF), int((u >> 8) & 0xFF), int((u >> 16) & 0xFF)).name();
}
long long hexToColor(const QString& hex) {
    const QColor c(hex);
    return 0xFF000000LL | (static_cast<long long>(c.blue()) << 16) |
           (static_cast<long long>(c.green()) << 8) | c.red();
}

} // namespace

PropertiesModel::PropertiesModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {}

PropertiesModel::~PropertiesModel() {
    if (props_ != nullptr) {
        obs_properties_destroy(static_cast<obs_properties_t*>(props_));
    }
}

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
    if (props_ != nullptr) {
        obs_properties_destroy(static_cast<obs_properties_t*>(props_));
        props_ = nullptr;
    }
    obs_source_t* s = src(rt_, source_);
    if (s == nullptr) {
        endResetModel();
        return;
    }
    obs_properties_t* props = obs_source_properties(s);
    props_ = props;
    obs_data_t* settings = obs_source_get_settings(s);

    // Recorre (y aplana grupos) las propiedades visibles.
    const std::function<void(obs_properties_t*)> walk = [&](obs_properties_t* ps) {
        for (obs_property_t* p = obs_properties_first(ps); p != nullptr; obs_property_next(&p)) {
            if (!obs_property_visible(p)) {
                continue;
            }
            Row r;
            r.name = qs(obs_property_name(p));
            r.label = qs(obs_property_description(p));
            r.help = qs(obs_property_long_description(p));
            r.enabled = obs_property_enabled(p);
            const char* key = obs_property_name(p);
            switch (obs_property_get_type(p)) {
                case OBS_PROPERTY_BOOL:
                    r.kind = "bool";
                    r.value = obs_data_get_bool(settings, key);
                    break;
                case OBS_PROPERTY_INT:
                    r.kind = "int";
                    r.value = static_cast<qlonglong>(obs_data_get_int(settings, key));
                    r.min = obs_property_int_min(p);
                    r.max = obs_property_int_max(p);
                    r.step = obs_property_int_step(p);
                    r.suffix = qs(obs_property_int_suffix(p));
                    break;
                case OBS_PROPERTY_FLOAT:
                    r.kind = "float";
                    r.value = obs_data_get_double(settings, key);
                    r.min = obs_property_float_min(p);
                    r.max = obs_property_float_max(p);
                    r.step = obs_property_float_step(p);
                    r.suffix = qs(obs_property_float_suffix(p));
                    break;
                case OBS_PROPERTY_TEXT: {
                    const auto t = obs_property_text_type(p);
                    r.kind = t == OBS_TEXT_PASSWORD    ? "password"
                             : t == OBS_TEXT_MULTILINE ? "textarea"
                             : t == OBS_TEXT_INFO      ? "info"
                                                       : "text";
                    r.value = qs(obs_data_get_string(settings, key));
                    break;
                }
                case OBS_PROPERTY_PATH:
                    r.kind = "path";
                    r.value = qs(obs_data_get_string(settings, key));
                    r.help = r.help.isEmpty() ? qs(obs_property_path_filter(p)) : r.help;
                    break;
                case OBS_PROPERTY_LIST: {
                    r.kind = "list";
                    const auto fmt = obs_property_list_format(p);
                    const size_t n = obs_property_list_item_count(p);
                    for (size_t i = 0; i < n; ++i) {
                        QVariant v;
                        if (fmt == OBS_COMBO_FORMAT_INT) {
                            v = static_cast<qlonglong>(obs_property_list_item_int(p, i));
                        } else if (fmt == OBS_COMBO_FORMAT_FLOAT) {
                            v = obs_property_list_item_float(p, i);
                        } else {
                            v = qs(obs_property_list_item_string(p, i));
                        }
                        r.options << QVariantMap{{"value", v},
                                                 {"label", qs(obs_property_list_item_name(p, i))}};
                    }
                    if (fmt == OBS_COMBO_FORMAT_INT) {
                        r.value = static_cast<qlonglong>(obs_data_get_int(settings, key));
                    } else if (fmt == OBS_COMBO_FORMAT_FLOAT) {
                        r.value = obs_data_get_double(settings, key);
                    } else {
                        r.value = qs(obs_data_get_string(settings, key));
                    }
                    break;
                }
                case OBS_PROPERTY_COLOR:
                case OBS_PROPERTY_COLOR_ALPHA:
                    r.kind = "color";
                    r.value = colorToHex(obs_data_get_int(settings, key));
                    break;
                case OBS_PROPERTY_FONT: {
                    r.kind = "font";
                    obs_data_t* font = obs_data_get_obj(settings, key);
                    r.value = QVariantMap{{"face", qs(obs_data_get_string(font, "face"))},
                                          {"size", static_cast<qlonglong>(obs_data_get_int(font, "size"))}};
                    obs_data_release(font);
                    break;
                }
                case OBS_PROPERTY_BUTTON: r.kind = "button"; break;
                case OBS_PROPERTY_GROUP:
                    r.kind = "header";
                    rows_.push_back(r);
                    walk(obs_property_group_content(p));
                    continue;
                default: continue; // listas editables, frame rate: pendientes
            }
            rows_.push_back(r);
        }
    };
    walk(props);
    obs_data_release(settings);
    endResetModel();
}

void PropertiesModel::setValue(const QString& prop, const QVariant& value) {
    obs_source_t* s = src(rt_, source_);
    if (s == nullptr) {
        return;
    }
    obs_data_t* settings = obs_source_get_settings(s);
    const QByteArray key = prop.toUtf8();
    QString kind;
    for (const auto& r : rows_) {
        if (r.name == prop) {
            kind = r.kind;
        }
    }
    if (kind == "bool") {
        obs_data_set_bool(settings, key, value.toBool());
    } else if (kind == "int") {
        obs_data_set_int(settings, key, value.toLongLong());
    } else if (kind == "float") {
        obs_data_set_double(settings, key, value.toDouble());
    } else if (kind == "color") {
        obs_data_set_int(settings, key, hexToColor(value.toString()));
    } else if (kind == "font") {
        const QVariantMap m = value.toMap();
        obs_data_t* font = obs_data_get_obj(settings, key);
        if (font == nullptr) {
            font = obs_data_create();
        }
        obs_data_set_string(font, "face", m.value("face").toString().toUtf8());
        obs_data_set_int(font, "size", m.value("size").toLongLong());
        obs_data_set_obj(settings, key, font);
        obs_data_release(font);
    } else if (kind == "list" &&
               (value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::Int)) {
        obs_data_set_int(settings, key, value.toLongLong());
    } else if (kind == "list" && value.typeId() == QMetaType::Double) {
        obs_data_set_double(settings, key, value.toDouble());
    } else {
        obs_data_set_string(settings, key, value.toString().toUtf8());
    }
    // Aplica y sincroniza la configuracion (todas las escenas que la usan).
    const std::string json = obs_data_get_json(settings);
    obs_data_release(settings);
    rt_.updateSourceSettings(source_.toStdString(), json);
    // Algunas propiedades dependen de otras (elegir camara -> resoluciones).
    obs_property_t* p = obs_properties_get(static_cast<obs_properties_t*>(props_), key);
    obs_data_t* fresh = obs_source_get_settings(s);
    const bool changedLayout = p != nullptr && obs_property_modified(p, fresh);
    obs_data_release(fresh);
    if (changedLayout) {
        rebuild();
    } else {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].name == prop) {
                rows_[i].value = value;
                emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {ValueRole});
            }
        }
    }
}

void PropertiesModel::press(const QString& prop) {
    obs_property_t* p = props_ != nullptr
                            ? obs_properties_get(static_cast<obs_properties_t*>(props_), prop.toUtf8())
                            : nullptr;
    obs_source_t* s = src(rt_, source_);
    if (p != nullptr && s != nullptr && obs_property_button_clicked(p, s)) {
        rebuild();
    }
}

} // namespace ba::app
