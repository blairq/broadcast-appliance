// src/app/video/VideoGrafos.cpp — ver VideoGrafos.hpp

#include "video/VideoGrafos.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"

#include <obs.h>

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <functional>
#include <set>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
constexpr const char* kFiltro = "BA · Grafo de video";
constexpr const char* kTipoSalida = "ba_video_salida";
/// Filtros de OBS que no van como tarjeta: el propio grafo y los internos.
const std::set<std::string> kNoTarjeta = {"ba_video_grafo", "ba_audio_rack"};

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).toVariant().toMap().value("v");
}
json deVariant(const QVariant& v) {
    return json::parse(QJsonDocument::fromVariant(QVariantMap{{"v", v}}).toJson(QJsonDocument::Compact).toStdString(),
                       nullptr, false)
        .value("v", json());
}
QString q(const std::string& s) { return QString::fromStdString(s); }
QString q(const char* s) { return QString::fromUtf8(s != nullptr ? s : ""); }

/// 0xAABBGGRR de libobs -> "#RRGGBB".
QString colorHex(long long c) {
    const int r = static_cast<int>(c & 0xFF), g = static_cast<int>((c >> 8) & 0xFF), b = static_cast<int>((c >> 16) & 0xFF);
    return QStringLiteral("#%1%2%3").arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(b, 2, 16, QChar('0')).toUpper();
}
} // namespace

VideoGrafos::VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent),
      rt_(rt),
      archivo_(dataRoot / "Config" / "video-grafo.json"),
      archivoViejo_(dataRoot / "Config" / "video-grafos.json") {
    cargar();
    revision_.setInterval(1500);
    connect(&revision_, &QTimer::timeout, this, &VideoGrafos::sincronizar);
    revision_.start();
    QTimer::singleShot(0, this, &VideoGrafos::sincronizar);
}

bool VideoGrafos::esEntrada(const std::string& n) const {
    BroadcastEngine& e = rt_.engine();
    const std::string type = e.sourceTypeId(n);
    if (type == inputs::kVideoType) return true;
    return inputs::isNetworkInput(type, json::parse(e.sourceSettingsJson(n), nullptr, false));
}

QStringList VideoGrafos::entradas() const { return entradas_; }

QVariantList VideoGrafos::entradasInfo() const {
    QVariantList out;
    for (const QString& e : entradas_) {
        const std::string n = e.toStdString();
        const std::string tipo = rt_.engine().sourceTypeId(n);
        const QString grupo = tipo == inputs::kVideoType ? QStringLiteral("Cámaras")
                              : tipo == "ba_ndi_input"   ? QStringLiteral("NDI")
                                                         : QStringLiteral("Red");
        int tarjetas = 0;
        for (const auto& nd : grafo_.nodos())
            if (nd.grupo == n && nd.tipo != "entrada" && nd.tipo != "escena") ++tarjetas;
        out << QVariantMap{{"nombre", e}, {"grupo", grupo}, {"tarjetas", tarjetas}};
    }
    return out;
}

void VideoGrafos::seleccionar(const QString& entrada) {
    if (entrada == entrada_) return;
    entrada_ = entrada;
    emit cambio();
}

json VideoGrafos::conPublicadas(json d) const {
    // Si cada salida ya existe como fuente.
    for (auto& n : d["nodos"])
        if (n.value("tipo", std::string()) == "salida") {
            const std::string nombre = n["params"].value("nombre", std::string());
            n["publicada"] = !nombre.empty() && rt_.engine().sourceTypeId(nombre) == kTipoSalida;
        }
    return d;
}

json VideoGrafos::describir(const std::string& entrada) const {
    if (!entrada.empty() && !esEntrada(entrada)) return {{"error", "«" + entrada + "» no es una entrada de video"}};
    json d = conPublicadas(grafo_.describir(entrada, !entrada.empty()));
    if (!entrada.empty()) d["entrada"] = entrada;
    return d;
}

QVariantMap VideoGrafos::grafo() const {
    if (entrada_.isEmpty()) return {};
    return aVariant(describir(entrada_.toStdString())).toMap();
}

QVariantMap VideoGrafos::estudio() const { return aVariant(describir(std::string())).toMap(); }

QVariantList VideoGrafos::catalogo() { return aVariant(GrafoVideo::catalogo()).toList(); }

std::string VideoGrafos::nombreFiltro(const std::string& filtroId) {
    const char* n = obs_source_get_display_name(filtroId.c_str());
    return n != nullptr ? n : std::string();
}

QVariantList VideoGrafos::filtrosObs() const {
    QVariantList out;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
        const std::string fid = id;
        if (kNoTarjeta.count(fid)) continue;
        const uint32_t flags = obs_get_source_output_flags(id);
        // De video, que se dibujan (no los asíncronos) y que no están retirados.
        if (!(flags & OBS_SOURCE_VIDEO) || (flags & OBS_SOURCE_ASYNC) || (flags & OBS_SOURCE_CAP_DISABLED)) continue;
        const std::string nombre = nombreFiltro(fid);
        if (nombre.empty()) continue;
        out << QVariantMap{{"id", q(fid)}, {"nombre", q(nombre)}, {"propio", fid.rfind("ba_", 0) == 0}};
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("nombre").toString().localeAwareCompare(b.toMap().value("nombre").toString()) < 0;
    });
    return out;
}

QVariantList VideoGrafos::propiedades(const QString& idNodo) const {
    QVariantList out;
    const GrafoVideo::Nodo* n = grafo_.nodo(idNodo.toStdString());
    if (n == nullptr || n->tipo != "obs") return out;
    const std::string fid = n->params.value("filtro", std::string());
    obs_properties_t* props = obs_get_source_properties(fid.c_str());
    if (props == nullptr) return out;
    // Los valores: los de fábrica del filtro, con lo que tiene la tarjeta encima.
    obs_data_t* d = obs_get_source_defaults(fid.c_str());
    obs_data_t* aj = obs_data_create_from_json(n->params.value("ajustes", json::object()).dump().c_str());
    if (d != nullptr && aj != nullptr) obs_data_apply(d, aj);
    const std::function<void(obs_properties_t*)> walk = [&](obs_properties_t* ps) {
        for (obs_property_t* p = obs_properties_first(ps); p != nullptr; obs_property_next(&p)) {
            if (!obs_property_visible(p)) continue;
            const char* k = obs_property_name(p);
            QVariantMap r{{"name", q(k)},
                          {"label", obs_property_description(p) ? q(obs_property_description(p)) : q(k)},
                          {"help", q(obs_property_long_description(p))}};
            switch (obs_property_get_type(p)) {
            case OBS_PROPERTY_BOOL:
                r["kind"] = "bool";
                r["value"] = obs_data_get_bool(d, k);
                break;
            case OBS_PROPERTY_INT:
                r["kind"] = "int";
                r["value"] = static_cast<qlonglong>(obs_data_get_int(d, k));
                r["min"] = obs_property_int_min(p);
                r["max"] = obs_property_int_max(p);
                r["step"] = obs_property_int_step(p);
                r["suffix"] = q(obs_property_int_suffix(p));
                break;
            case OBS_PROPERTY_FLOAT:
                r["kind"] = "float";
                r["value"] = obs_data_get_double(d, k);
                r["min"] = obs_property_float_min(p);
                r["max"] = obs_property_float_max(p);
                r["step"] = obs_property_float_step(p);
                r["suffix"] = q(obs_property_float_suffix(p));
                break;
            case OBS_PROPERTY_TEXT:
                r["kind"] = obs_property_text_type(p) == OBS_TEXT_INFO ? "info" : "text";
                r["value"] = q(obs_data_get_string(d, k));
                break;
            case OBS_PROPERTY_PATH:
                r["kind"] = "path";
                r["value"] = q(obs_data_get_string(d, k));
                r["filtro"] = q(obs_property_path_filter(p));
                break;
            case OBS_PROPERTY_LIST: {
                r["kind"] = "list";
                const auto fmt = obs_property_list_format(p);
                QVariantList ops;
                for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
                    const QVariant v = fmt == OBS_COMBO_FORMAT_INT     ? QVariant(static_cast<qlonglong>(obs_property_list_item_int(p, i)))
                                       : fmt == OBS_COMBO_FORMAT_FLOAT ? QVariant(obs_property_list_item_float(p, i))
                                                                       : QVariant(q(obs_property_list_item_string(p, i)));
                    ops << QVariantMap{{"value", v}, {"label", q(obs_property_list_item_name(p, i))}};
                }
                r["options"] = ops;
                r["value"] = fmt == OBS_COMBO_FORMAT_INT     ? QVariant(static_cast<qlonglong>(obs_data_get_int(d, k)))
                             : fmt == OBS_COMBO_FORMAT_FLOAT ? QVariant(obs_data_get_double(d, k))
                                                             : QVariant(q(obs_data_get_string(d, k)));
                break;
            }
            case OBS_PROPERTY_COLOR:
            case OBS_PROPERTY_COLOR_ALPHA:
                r["kind"] = "color";
                r["value"] = colorHex(obs_data_get_int(d, k));
                break;
            case OBS_PROPERTY_GROUP:
                out << QVariantMap{{"name", q(k)}, {"label", r["label"]}, {"kind", "header"}};
                walk(obs_property_group_content(p));
                continue;
            default:
                continue; // botones, fuentes, listas editables: no tienen sentido acá
            }
            out << r;
        }
    };
    walk(props);
    obs_data_release(aj);
    obs_data_release(d);
    obs_properties_destroy(props);
    return out;
}

std::string VideoGrafos::editar(const std::function<std::string(GrafoVideo&)>& f) {
    GrafoVideo antes = grafo_;
    const std::string error = f(grafo_);
    if (!error.empty()) {
        grafo_ = antes;
        return error;
    }
    nombrarSalidas(grafo_);
    if (grafo_.aJson() != antes.aJson()) {
        guardar();
        for (const QString& e : entradas_) aplicar(e.toStdString());
        emit cambio();
    }
    return {};
}

void VideoGrafos::nombrarSalidas(GrafoVideo& g) const {
    std::set<std::string> usados;
    for (const auto& s : g.salidas()) usados.insert(s);
    std::vector<std::pair<std::string, std::string>> sinNombre;
    for (const auto& n : g.nodos())
        if (n.tipo == "salida" && n.params.value("nombre", std::string()).empty()) sinNombre.emplace_back(n.id, n.grupo);
    for (const auto& [id, grupo] : sinNombre) {
        // «Cámara 1 · salida», «Cámara 1 · salida 2»… sin chocar con otra fuente.
        const std::string base = (grupo.empty() ? std::string("Estudio") : grupo) + " · salida";
        std::string nombre;
        for (int i = 1;; ++i) {
            nombre = i == 1 ? base : base + " " + std::to_string(i);
            const std::string tipo = rt_.engine().sourceTypeId(nombre);
            if (!usados.count(nombre) && (tipo.empty() || tipo == kTipoSalida)) break;
        }
        usados.insert(nombre);
        g.ajustar(id, json{{"nombre", nombre}}, nullptr);
    }
}

QString VideoGrafos::agregar(const QString& tipo, const QString& despues) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.agregar(tipo.toStdString(), entrada_.toStdString(), despues.toStdString(), json::object(), &e);
        return e;
    }));
}
QString VideoGrafos::agregarFiltro(const QString& filtroId, const QString& despues) {
    const std::string nombre = nombreFiltro(filtroId.toStdString());
    if (nombre.empty()) return "No existe el filtro «" + filtroId + "»";
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        const std::string id = g.agregar("obs", entrada_.toStdString(), despues.toStdString(),
                                         json{{"filtro", filtroId.toStdString()}, {"ajustes", json::object()}}, &e);
        if (!id.empty()) g.ajustar(id, json{{"etiqueta", nombre}}, nullptr);
        return e;
    }));
}
QString VideoGrafos::quitar(const QString& id) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.quitar(id.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.conectar(desde.toStdString(), salida.toStdString(), hacia.toStdString(), entrada.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    return q(editar([&](GrafoVideo& g) {
        g.desconectar(desde.toStdString(), salida.toStdString(), hacia.toStdString(), entrada.toStdString());
        return std::string();
    }));
}
QString VideoGrafos::mover(const QString& id, int x, int y) {
    return q(editar([&](GrafoVideo& g) {
        g.mover(id.toStdString(), x, y);
        return std::string();
    }));
}
QString VideoGrafos::ajustar(const QString& id, const QVariantMap& params) {
    const json p = deVariant(params);
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.ajustar(id.toStdString(), p, &e);
        return e;
    }));
}
QString VideoGrafos::ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor) {
    json v = deVariant(valor);
    // Los colores llegan como "#RRGGBB" y libobs los quiere 0xAABBGGRR.
    if (v.is_string() && v.get<std::string>().size() == 7 && v.get<std::string>()[0] == '#') {
        const long long rgb = std::stoll(v.get<std::string>().substr(1), nullptr, 16);
        v = 0xFF000000LL | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.ajustar(id.toStdString(), json{{"ajustes", {{clave.toStdString(), v}}}}, &e);
        return e;
    }));
}
QString VideoGrafos::limpiar() {
    return q(editar([&](GrafoVideo& g) {
        g.limpiar(entrada_.toStdString());
        return std::string();
    }));
}
QString VideoGrafos::aEscena(const QString& id, const QString& escena) {
    return q(ponerEnEscena(id.toStdString(), escena.toStdString()));
}

std::string VideoGrafos::ponerEnEscena(const std::string& id, const std::string& escena) {
    const GrafoVideo::Nodo* n = grafo_.nodo(id);
    if (n == nullptr || n->tipo != "salida") return "«" + id + "» no es una salida del grafo";
    const std::string nombre = n->params.value("nombre", std::string());
    const std::string tipo = rt_.engine().sourceTypeId(nombre);
    if (!tipo.empty() && tipo != kTipoSalida) return "ya hay otra fuente que se llama «" + nombre + "»";
    SourceSpec spec;
    spec.id = kTipoSalida;
    spec.name = nombre;
    spec.settingsJson = json{{"fuente", n->grupo}}.dump();
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? "no se pudo agregar" : rt_.lastError();
    rt_.save();
    emit cambio();
    return {};
}

void VideoGrafos::sincronizar() {
    if (!obs_initialized()) return;
    QStringList hay;
    for (const std::string& n : rt_.engine().sourceNames())
        if (esEntrada(n)) hay << q(n);
    hay.sort();
    bool cambiaron = hay != entradas_;
    entradas_ = hay;
    // Cada entrada tiene su sub-grafo (entrada -> escena); el de una entrada
    // que se fue queda guardado, por si vuelve con el mismo nombre.
    bool nuevos = false;
    for (const QString& e : entradas_)
        if (grafo_.asegurarEntrada(e.toStdString())) nuevos = true;
    if (nuevos) guardar();
    if (entrada_.isEmpty() && !entradas_.isEmpty()) entrada_ = entradas_.first();
    for (const QString& e : entradas_) aplicar(e.toStdString());
    if (cambiaron || nuevos) emit cambio();
}

void VideoGrafos::aplicar(const std::string& entrada) {
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return;
    const json para = grafo_.paraFiltro(entrada);
    // Solo «in -> out»: no hace falta el filtro.
    const bool trivial = para["nodos"].size() == 2 && para["cables"].size() == 1 &&
                         para["cables"][0].value("desde", "") == "in" && para["cables"][0].value("hacia", "") == "out";
    obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro);
    if (trivial) {
        if (f != nullptr) obs_source_filter_remove(src, f);
        aplicado_.erase(entrada);
    } else {
        const std::string js = para.dump();
        if (f == nullptr) {
            obs_data_t* s = obs_data_create();
            obs_data_set_string(s, "grafo", js.c_str());
            obs_source_t* nuevo = obs_source_create_private("ba_video_grafo", kFiltro, s);
            obs_data_release(s);
            if (nuevo != nullptr) {
                obs_source_filter_add(src, nuevo);
                obs_source_release(nuevo);
            }
            aplicado_[entrada] = js;
        } else if (aplicado_[entrada] != js) {
            obs_data_t* s = obs_data_create();
            obs_data_set_string(s, "grafo", js.c_str());
            obs_source_update(f, s);
            obs_data_release(s);
            aplicado_[entrada] = js;
        }
        // El último en aplicarse («abajo» en libobs: índice 0), después del
        // PTZ digital, que entra también abajo cuando se lo prende.
        if (obs_source_t* g = f != nullptr ? obs_source_get_ref(f) : obs_source_get_filter_by_name(src, kFiltro)) {
            obs_source_filter_set_order(src, g, OBS_ORDER_MOVE_BOTTOM);
            obs_source_release(g);
        }
    }
    if (f != nullptr) obs_source_release(f);
    obs_source_release(src);
}

void VideoGrafos::guardar() const {
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QByteArray::fromStdString(grafo_.aJson().dump(2)));
    f.commit();
}

void VideoGrafos::cargar() {
    // El de ahora; si no está, el de antes (un grafo por entrada), migrado.
    for (const auto& ruta : {archivo_, archivoViejo_}) {
        QFile f(QString::fromStdString(ruta.string()));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const json j = json::parse(f.readAll().toStdString(), nullptr, false);
        if (!j.is_object()) continue;
        grafo_ = GrafoVideo::desde(j);
        if (ruta == archivoViejo_) guardar();
        return;
    }
}

} // namespace ba::app::video
