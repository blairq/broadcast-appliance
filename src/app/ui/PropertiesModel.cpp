// ============================================================================
// src/app/ui/PropertiesModel.cpp — ver PropertiesModel.hpp
// ============================================================================

#include "ui/PropertiesModel.hpp"
#include "ui/Formulario.hpp"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include "arranque/RuntimeMotor.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "i18n/I18nTexto.hpp"
#include "video/Camaras.hpp"

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

json aJson(const QVariant& v) {
    switch (v.typeId()) {
        case QMetaType::Bool: return v.toBool();
        case QMetaType::Int:
        case QMetaType::LongLong: return v.toLongLong();
        case QMetaType::Double: return v.toDouble();
        case QMetaType::QString: return v.toString().toStdString();
        default: break;
    }
    if (!v.isValid()) return nullptr;
    const QJsonValue j = QJsonValue::fromVariant(v);
    if (j.isArray()) return json::parse(QJsonDocument(j.toArray()).toJson().toStdString(), nullptr, false);
    if (j.isObject()) return json::parse(QJsonDocument(j.toObject()).toJson().toStdString(), nullptr, false);
    return v.toString().toStdString();
}

/// Etiqueta legible de una clave sin tipo registrado ("show_cursor" → "Show cursor").
QString etiquetaDe(const std::string& k) {
    QString l = qs(k);
    l.replace('_', ' ');
    if (!l.isEmpty()) l[0] = l[0].toUpper();
    return l;
}

} // namespace

PropertiesModel::PropertiesModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent)
    : QAbstractListModel(parent), rt_(rt), ventanilla_(ventanilla) {
    // La fuente desapareció o cambió de tipo (la borraron, se abrió otro evento): se relee.
    ventanilla_.escuchar(this, [this](unsigned partes) {
        if ((partes & FotoMotor::Escenas) == 0 || source_.isEmpty()) return;
        if (ventanilla_.foto().tipoDe(source_.toStdString()) != tipo_) releer();
    });
}

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
        case CampoRole: {
            formulario::Fila f{r.name, r.label, r.kind, r.value, r.options, r.min, r.max, r.step, r.suffix, r.help};
            f.enabled = r.enabled;
            return formulario::deFila(f);
        }
        default: return {};
    }
}

QHash<int, QByteArray> PropertiesModel::roleNames() const {
    // La vista dibuja la fila con `campo` (el MCP tiene sus claves en filasJson).
    return {{NameRole, "name"}, {CampoRole, "campo"}};
}

void PropertiesModel::setSourceName(const QString& name) {
    if (name == source_) {
        return;
    }
    source_ = name;
    emit sourceNameChanged();
    // Otra fuente: las filas de la anterior no valen mientras llega su lectura.
    if (!rows_.empty()) {
        beginResetModel();
        rows_.clear();
        endResetModel();
    }
    tipo_.clear();
    releer();
}

void PropertiesModel::releer() {
    const unsigned esta = ++lecturas_;
    if (source_.isEmpty()) return;
    ventanilla_.pedir(
        this, "propiedades", [&rt = rt_, n = source_.toStdString()]() { return leer(rt, n); },
        [this, esta](Lectura l) {
            if (esta == lecturas_) ponerFilas(std::move(l)); // si no, ya se pidió otra
        });
}

void PropertiesModel::ponerFilas(Lectura l) {
    tipo_ = l.tipo;
    std::vector<Row> nuevas = filas(l);
    // Las mismas filas (lo común: cambió un valor): solo se avisa lo que cambió, y la
    // vista no pierde el foco ni el desplazamiento. Si cambió su forma, se rehacen.
    bool misma = nuevas.size() == rows_.size();
    for (std::size_t i = 0; misma && i < nuevas.size(); ++i)
        misma = nuevas[i].name == rows_[i].name && nuevas[i].kind == rows_[i].kind;
    if (!misma) {
        beginResetModel();
        rows_ = std::move(nuevas);
        endResetModel();
        return;
    }
    for (std::size_t i = 0; i < nuevas.size(); ++i) {
        Row& a = rows_[i];
        const Row& n = nuevas[i];
        QList<int> roles;
        if (a.value != n.value) roles << ValueRole;
        if (a.options != n.options) roles << OptionsRole;
        if (a.label != n.label) roles << LabelRole;
        if (a.help != n.help) roles << HelpRole;
        if (a.enabled != n.enabled) roles << EnabledRole;
        if (a.min != n.min || a.max != n.max || a.step != n.step || a.suffix != n.suffix)
            roles << MinRole << MaxRole << StepRole << SuffixRole;
        if (roles.isEmpty()) continue;
        roles << CampoRole;
        a = n;
        const QModelIndex ix = index(static_cast<int>(i), 0);
        emit dataChanged(ix, ix, roles);
    }
}

PropertiesModel::Lectura PropertiesModel::leer(AppRuntime& rt, const std::string& nombre) {
    Lectura l;
    BroadcastEngine& e = rt.engine();
    if (!e.sourceExists(nombre)) return l;
    l.existe = true;
    l.tipo = e.sourceTypeId(nombre);
    l.ajustes = fuentes::conDefectos(l.tipo, json::parse(e.sourceSettingsJson(nombre), nullptr, false));
    // Una cámara, o una «Entrada por GStreamer» recién agregada (sin nada): la
    // cámara y su formato en una lista. Antes no tenía ninguna fila (los ajustes
    // estaban vacíos) y GStreamer mostraba su video de prueba: «aparece algo pero
    // no anda». Sin cámara, también el pipeline propio.
    const json& a = l.ajustes;
    const auto vaciaGst = [&]() {
        if (!a.is_object()) return true;
        for (const char* k : {"microfono", "anillo", "pipeline_audio", "video_invitado", "virtual", "fisica", "ndi", "voz", "uri"})
            if (a.contains(k)) return false;
        return a.value("pipeline", std::string()).empty();
    };
    l.conCamara = l.tipo == inputs::kMicType && (inputs::esCamara(l.tipo, a) || vaciaGst());
    if (!l.conCamara) return l;
    // Los dispositivos del equipo (el monitor de GStreamer): también acá, en el dueño.
    for (const auto& d : video::camarasDelEquipo(e)) l.camaras.emplace_back(d.value, d.name);
    const std::string cam = a.is_object() ? a.value("camara", std::string()) : std::string();
    if (!cam.empty() || inputs::esCamara(l.tipo, a)) {
        const auto au = video::modoAutomatico(cam, &l.porque);
        if (au.ancho > 0) l.automatico = au.etiqueta();
        for (const auto& mo : video::modosDeCamara(cam))
            if (mo.usable()) l.modos.emplace_back(mo.valor(), mo.etiqueta());
    }
    return l;
}

std::vector<PropertiesModel::Row> PropertiesModel::filas(const Lectura& l) {
    std::vector<Row> rows;
    if (!l.existe) return rows;
    const std::string& tipo = l.tipo;
    const json& ajustes = l.ajustes;
    if (l.conCamara) {
        const std::string cam = ajustes.is_object() ? ajustes.value("camara", std::string()) : std::string();
        Row c;
        c.name = "camara";
        c.label = tr("Cámara o capturadora");
        c.kind = "list";
        c.help = tr("Las cámaras USB y capturadoras conectadas a esta máquina.");
        bool esta = false;
        for (const auto& [valor, nombre] : l.camaras) {
            c.options << QVariantMap{{"value", qs(valor)}, {"label", qs(nombre)}};
            esta = esta || valor == cam;
        }
        if (!cam.empty() && !esta) c.options << QVariantMap{{"value", qs(cam)}, {"label", tr("(no está conectada)")}};
        if (c.options.isEmpty()) c.help = tr("No se encontró ninguna cámara ni capturadora conectada.");
        c.value = qs(cam);
        rows.push_back(c);
        if (!cam.empty() || inputs::esCamara(tipo, ajustes)) {
            Row m;
            m.name = "camara_modo";
            m.label = "Formato";
            m.kind = "list";
            m.help = l.porque.empty() ? tr("Tamaño, cuadros por segundo y formato (crudo, MJPEG, H.264), con quién lo "
                                           "decodifica. Automático: el que conviene para el lienzo.")
                                      : tr("Tamaño, cuadros por segundo y formato (crudo, MJPEG, H.264), con quién lo "
                                           "decodifica. Automático: el que conviene para el lienzo (%1).")
                                            .arg(qs(l.porque));
            m.options << QVariantMap{
                {"value", QString()},
                {"label", !l.automatico.empty() ? tr("Automático: %1").arg(qs(l.automatico)) : tr("Automático")}};
            for (const auto& [valor, etiqueta] : l.modos) m.options << QVariantMap{{"value", qs(valor)}, {"label", qs(etiqueta)}};
            m.value = qs(ajustes.value("camara_modo", std::string()));
            rows.push_back(m);
        } else {
            Row pl;
            pl.name = "pipeline";
            pl.label = tr("O un pipeline de GStreamer");
            pl.kind = "text";
            pl.help = tr("Cualquier origen: «rtspsrc location=rtsp://… ! decodebin», «videotestsrc»…");
            pl.value = QString();
            rows.push_back(pl);
        }
        return rows;
    }
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
            rows.push_back(r);
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
            rows.push_back(r);
        }
    }
    return rows;
}

json PropertiesModel::filasJson(const std::vector<Row>& filas) {
    json out = json::array();
    for (const Row& r : filas)
        out.push_back(json{{"name", r.name.toStdString()},
                           {"label", r.label.toStdString()},
                           {"kind", r.kind.toStdString()},
                           {"value", aJson(r.value)},
                           {"options", aJson(r.options)},
                           {"minimum", r.min},
                           {"maximum", r.max},
                           {"step", r.step},
                           {"suffix", r.suffix.toStdString()},
                           {"enabled", r.enabled},
                           {"help", r.help.toStdString()}});
    return out;
}

json PropertiesModel::cambioDe(const QString& kind, const std::string& key, const QVariant& value, const json& ajustes) {
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
        json f = ajustes.is_object() ? ajustes.value(key, json::object()) : json::object();
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
    return cambio;
}

std::string PropertiesModel::aplicar(AppRuntime& rt, const std::string& nombre, const std::string& prop,
                                     const QString& kind, const QVariant& valor, bool apretar) {
    BroadcastEngine& e = rt.engine();
    if (!e.sourceExists(nombre)) return ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente '%1'."), {nombre});
    if (apretar) {
        fuentes::Tipo t;
        if (!fuentes::tipo(e.sourceTypeId(nombre), t)) return {};
        const auto it = t.botones.find(prop);
        if (it != t.botones.end()) it->second(nombre);
        return {};
    }
    // La fuente se completa sobre sus ajustes (sin los por defecto del tipo).
    const json cambio = cambioDe(kind, prop, valor, json::parse(e.sourceSettingsJson(nombre), nullptr, false));
    // Aplica y sincroniza la configuracion (todas las escenas que la usan).
    if (!rt.updateSourceSettings(nombre, cambio.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        return rt.lastError();
    return {};
}

void PropertiesModel::leerFilas(VentanillaMotor& v, AppRuntime& rt, QObject* quien, const std::string& nombre,
                                std::function<void(const Lectura&, std::vector<Row>)> listo) {
    v.pedir(quien, "propiedades", [&rt, nombre]() { return leer(rt, nombre); },
            [listo = std::move(listo)](Lectura l) { listo(l, filas(l)); });
}

void PropertiesModel::cambiar(VentanillaMotor& v, AppRuntime& rt, QObject* quien, const std::string& nombre,
                              const std::string& prop, const QVariant& valor, bool apretar,
                              std::function<void(const std::string&, const std::string&)> listo) {
    struct Hecho {
        std::string error, ajustes;
    };
    const auto pedirCambio = [&v, &rt, quien, nombre, prop, valor, apretar, listo](const QString& kind) {
        v.pedir(
            quien, "propiedad",
            [&rt, nombre, prop, kind, valor, apretar]() {
                Hecho h;
                h.error = aplicar(rt, nombre, prop, kind, valor, apretar);
                if (h.error.empty()) h.ajustes = rt.engine().sourceSettingsJson(nombre);
                return h;
            },
            [listo](Hecho h) { listo(h.error, h.ajustes); });
    };
    if (apretar) return pedirCambio({});
    // El tipo de la fila (cómo se escribe el valor) sale de las filas, que se arman acá.
    leerFilas(v, rt, quien, nombre, [pedirCambio, prop, nombre, listo](const Lectura& l, std::vector<Row> fs) {
        if (!l.existe) return listo(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente '%1'."), {nombre}), {});
        QString kind;
        for (const Row& r : fs)
            if (r.name.toStdString() == prop) kind = r.kind;
        pedirCambio(kind);
    });
}

void PropertiesModel::setValue(const QString& prop, const QVariant& value) {
    if (source_.isEmpty()) return;
    QString kind;
    // Mientras vuelve, la fila muestra lo pedido (no salta al valor viejo).
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        Row& r = rows_[i];
        if (r.name != prop) continue;
        kind = r.kind;
        // Un número borrado en el campo no es un valor: queda lo que estaba.
        if ((r.kind == "int" || r.kind == "float") && value.typeId() == QMetaType::QString) return;
        if (r.kind == "font" || r.kind == "color" || r.value == value) continue;
        r.value = value;
        const QModelIndex ix = index(static_cast<int>(i), 0);
        emit dataChanged(ix, ix, {ValueRole, CampoRole});
    }
    ventanilla_.pedir(
        this, "propiedad",
        [&rt = rt_, n = source_.toStdString(), p = prop.toStdString(), kind, value]() {
            return aplicar(rt, n, p, kind, value, false);
        },
        // Las filas pueden depender de los valores (listas que cambian): se relee.
        [this](const std::string&) { releer(); });
}

void PropertiesModel::press(const QString& prop) {
    if (source_.isEmpty()) return;
    ventanilla_.pedir(
        this, "propiedad_boton",
        [&rt = rt_, n = source_.toStdString(), p = prop.toStdString()]() { return aplicar(rt, n, p, {}, {}, true); },
        [this](const std::string&) { releer(); });
}

} // namespace ba::app
