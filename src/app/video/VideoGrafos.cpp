// src/app/video/VideoGrafos.cpp — ver VideoGrafos.hpp

#include "video/VideoGrafos.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"

#include <obs.h>

#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <functional>
#include <set>
#include <thread>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
constexpr const char* kFiltro = "BA · Grafo de video";
constexpr const char* kTipoSalida = "ba_video_salida";
constexpr const char* kTipoVirtual = "ba_gst_fuente";
constexpr const char* kTipoCompositor = "ba_video_compositor";
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
/// Las propiedades de un módulo de GStreamer (CatalogoGst) como controles de
/// PropertiesModel.
QVariantList controlesGst(const json& props) {
    QVariantList out;
    for (const auto& p : props) {
        const std::string tipo = p.value("tipo", "");
        QString ayuda = q(p.value("descripcion", ""));
        if (!p.value("enVivo", false)) ayuda += QStringLiteral(" (GStreamer no asegura que cambie con el video andando)");
        QVariantMap r{{"name", q(p.value("nombre", ""))}, {"label", q(p.value("nombre", ""))}, {"help", ayuda}};
        const json& v = p.contains("valor") ? p["valor"] : p["defecto"];
        if (tipo == "bool") {
            r["kind"] = "bool";
            r["value"] = v.is_boolean() && v.get<bool>();
        } else if (tipo == "int" || tipo == "double") {
            const double mn = p.value("min", 0.0), mx = p.value("max", 0.0);
            r["kind"] = tipo == "int" ? "int" : "float";
            r["value"] = v.is_number() ? v.get<double>() : 0.0;
            r["min"] = mn;
            r["max"] = mx;
            r["step"] = tipo == "int" ? 1.0 : (mx - mn <= 2.0 ? 0.01 : mx - mn <= 20.0 ? 0.05 : 0.5);
        } else if (tipo == "enum") {
            r["kind"] = "list";
            QVariantList ops;
            for (const auto& o : p.value("opciones", json::array()))
                ops << QVariantMap{{"value", q(o.value("nick", ""))}, {"label", q(o.value("descripcion", ""))}};
            r["options"] = ops;
            r["value"] = v.is_string() ? q(v.get<std::string>()) : QString();
        } else if (tipo == "string" || tipo == "flags") {
            r["kind"] = "text";
            r["value"] = v.is_string() ? q(v.get<std::string>()) : QString();
            if (tipo == "flags") {
                QStringList nicks;
                for (const auto& o : p.value("opciones", json::array())) nicks << q(o.value("nick", ""));
                r["help"] = ayuda + QStringLiteral(" Valores, unidos con «+»: ") + nicks.join(", ");
            }
        } else {
            continue;
        }
        out << r;
    }
    return out;
}

/// Las entradas virtuales: patrones de videotestsrc al tamaño del lienzo.
struct Virtual {
    const char* id;
    const char* nombre;
    const char* descripcion;
    const char* origen; ///< el pipeline, antes de las caps
};
const Virtual kVirtuales[] = {
    {"barras-hd", "Barras HD (SMPTE RP 219)", "Las barras de color de HD: para calibrar monitores y probar la cadena.",
     "videotestsrc pattern=smpte-rp-219 is-live=true"},
    {"barras", "Barras SMPTE (100 %)", "Las barras clásicas al 100 %.", "videotestsrc pattern=smpte100 is-live=true"},
    {"barras-75", "Barras SMPTE (75 %)", "Barras al 75 %: las de ajuste de la TV analógica.",
     "videotestsrc pattern=smpte75 is-live=true"},
    {"barras-hora", "Barras con la hora", "Barras HD con la hora encima: se ve si la señal está viva o congelada.",
     "videotestsrc pattern=smpte-rp-219 is-live=true ! clockoverlay time-format=\"%H:%M:%S\" font-desc=\"Sans 40\" "
     "halignment=center valignment=bottom"},
    {"negro", "Negro", "Una pantalla negra (para tapar una salida sin sacarla).", "videotestsrc pattern=black is-live=true"},
    {"blanco", "Blanco", "Una pantalla blanca.", "videotestsrc pattern=white is-live=true"},
    {"verde", "Verde croma", "Verde para probar una llave de croma.", "videotestsrc pattern=green is-live=true"},
    {"zona", "Placa de zonas", "Círculos concéntricos (zone plate): muestran el aliasing de un escalado.",
     "videotestsrc pattern=zone-plate kx2=20 ky2=20 kt=1 is-live=true"},
    {"damero", "Damero", "Cuadros de 8 píxeles: nitidez y escalado.", "videotestsrc pattern=checkers-8 is-live=true"},
    {"pelota", "Pelota en movimiento", "Una pelota que se mueve: para ver cuadros perdidos o trabados.",
     "videotestsrc pattern=ball is-live=true"},
    {"ruido", "Ruido", "Nieve de TV.", "videotestsrc pattern=snow is-live=true"},
};

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
    // El catálogo de GStreamer, en un hilo: iniciarlo revisa sus plugins (la
    // primera vez, segundos) y no puede trabar la interfaz.
    QFile f(QStringLiteral(":/video/gst-modulos.json"));
    json docs = json::object();
    if (f.open(QIODevice::ReadOnly)) docs = json::parse(f.readAll().toStdString(), nullptr, false);
    QPointer<VideoGrafos> yo = this;
    std::thread([yo, docs]() {
        auto c = std::make_shared<CatalogoGst>(docs);
        (void)c->modulos(); // se arma acá, no en el primer pedido
        QMetaObject::invokeMethod(
            qApp,
            [yo, c]() {
                if (!yo) return;
                yo->gst_ = c;
                emit yo->gstListo();
            },
            Qt::QueuedConnection);
    }).detach();
}

bool VideoGrafos::esEntrada(const std::string& n) const {
    BroadcastEngine& e = rt_.engine();
    const std::string type = e.sourceTypeId(n);
    if (type == inputs::kVideoType || type == kTipoVirtual) return true;
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
                              : tipo == kTipoVirtual     ? QStringLiteral("Virtuales")
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
    for (auto& n : d["nodos"]) {
        const std::string tipo = n.value("tipo", std::string());
        if (tipo == "salida" || tipo == "compositor") {
            const std::string nombre = n["params"].value("nombre", std::string());
            n["publicada"] = !nombre.empty() && rt_.engine().sourceTypeId(nombre) == (tipo == "salida" ? kTipoSalida : kTipoCompositor);
        }
    }
    return d;
}

json VideoGrafos::erroresDe(const std::string& entrada) const {
    json out = json::object();
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return out;
    if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
        calldata_t cd = {};
        if (proc_handler_call(obs_source_get_proc_handler(f), "estado", &cd)) {
            const char* e = calldata_string(&cd, "errores");
            if (e != nullptr) out = json::parse(e, nullptr, false);
        }
        calldata_free(&cd);
        obs_source_release(f);
    }
    obs_source_release(src);
    return out.is_object() ? out : json::object();
}

json VideoGrafos::describir(const std::string& entrada) const {
    if (!entrada.empty() && !esEntrada(entrada)) return {{"error", "«" + entrada + "» no es una entrada de video"}};
    json d = conPublicadas(grafo_.describir(entrada, !entrada.empty()));
    // Por qué no corre una tarjeta (lo informa el filtro de su entrada).
    std::map<std::string, json> errores;
    for (auto& n : d["nodos"]) {
        const std::string g = n.value("grupo", std::string());
        if (g.empty()) continue;
        if (!errores.count(g)) errores[g] = erroresDe(g);
        const std::string id = n.value("id", std::string());
        if (errores[g].contains(id)) n["error"] = errores[g][id];
    }
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
    if (n == nullptr) return out;
    // Un controlador muestra los de su molde; una tarjeta, con los que corre.
    const bool ctl = n->tipo == "control";
    const std::string tipo = ctl ? n->params.value("tarjeta", std::string()) : n->tipo;
    const json params = ctl ? n->params.value("params", json::object()) : grafo_.efectivos(*n);
    if (tipo == "gst")
        return controlesGst(gst().propiedades(params.value("elemento", std::string()), params.value("propiedades", json::object())));
    if (tipo != "obs") return out;
    const std::string fid = params.value("filtro", std::string());
    obs_properties_t* props = obs_get_source_properties(fid.c_str());
    if (props == nullptr) return out;
    // Los valores: los de fábrica del filtro, con lo que tiene la tarjeta encima.
    obs_data_t* d = obs_get_source_defaults(fid.c_str());
    obs_data_t* aj = obs_data_create_from_json(params.value("ajustes", json::object()).dump().c_str());
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

const CatalogoGst& VideoGrafos::gst() const { return *gst_; }

QVariantList VideoGrafos::modulosGst() const { return aVariant(gst().modulos()).toList(); }

QVariantList VideoGrafos::propiedadesModulo(const QString& elemento) const {
    return controlesGst(gst().propiedades(elemento.toStdString(), json::object()));
}

QString VideoGrafos::versionGst() const {
    return gst().disponible() ? q(gst().version()) : q("Sin GStreamer: " + gst().error());
}

QVariantList VideoGrafos::virtuales() {
    QVariantList out;
    for (const auto& v : kVirtuales)
        out << QVariantMap{{"id", v.id}, {"nombre", q(v.nombre)}, {"descripcion", q(v.descripcion)}, {"pipeline", q(v.origen)}};
    return out;
}

std::string VideoGrafos::agregarModuloEn(GrafoVideo& g, const std::string& elemento, const std::string& grupo,
                                         const std::string& despues, const json& propiedades, std::string* error) const {
    const json m = gst().modulo(elemento);
    if (m.empty()) {
        if (error) *error = gst().disponible() ? "no existe el módulo de GStreamer «" + elemento + "»" : gst().error();
        return {};
    }
    if (!m.value("tarjeta", false)) {
        if (error)
            *error = "«" + elemento + "» no va como tarjeta de filtro (" + m.value("categoria", std::string()) + "): " +
                     m.value("uso", m.value("descripcion", std::string()));
        return {};
    }
    const std::string id = g.agregar("gst", grupo, despues,
                                     json{{"elemento", elemento}, {"propiedades", propiedades.is_object() ? propiedades : json::object()}},
                                     error);
    if (!id.empty()) g.ajustar(id, json{{"etiqueta", m.value("titulo", m.value("nombre", elemento))}}, nullptr);
    return id;
}

QString VideoGrafos::agregarModulo(const QString& elemento, const QString& despues) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        agregarModuloEn(g, elemento.toStdString(), entrada_.toStdString(), despues.toStdString(), json::object(), &e);
        return e;
    }));
}

std::string VideoGrafos::crearEntradaVirtual(const std::string& nombre, const std::string& preset, const std::string& escena) {
    if (nombre.empty()) return "falta el nombre";
    if (!rt_.engine().sourceTypeId(nombre).empty()) return "ya hay una fuente que se llama «" + nombre + "»";
    std::string origen;
    if (preset.rfind("pipeline:", 0) == 0) {
        origen = preset.substr(9);
    } else {
        for (const auto& v : kVirtuales)
            if (preset == v.id) origen = v.origen;
        if (origen.empty()) return "no existe la entrada virtual «" + preset + "» (ver virtuales)";
        // Al tamaño y la cadencia del lienzo.
        obs_video_info ovi{};
        if (obs_get_video_info(&ovi))
            origen += " ! video/x-raw,width=" + std::to_string(ovi.base_width) + ",height=" + std::to_string(ovi.base_height) +
                      ",framerate=" + std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den);
    }
    SourceSpec spec;
    spec.id = kTipoVirtual;
    spec.name = nombre;
    spec.settingsJson = json{{"pipeline", origen}}.dump();
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? "no se pudo crear" : rt_.lastError();
    rt_.save();
    sincronizar();
    return {};
}

QString VideoGrafos::crearVirtual(const QString& nombre, const QString& preset, const QString& escena) {
    return q(crearEntradaVirtual(nombre.toStdString(), preset.toStdString(), escena.toStdString()));
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
        actualizarEstudio();
        emit cambio();
    }
    return {};
}

void VideoGrafos::nombrarSalidas(GrafoVideo& g) const {
    std::set<std::string> usados;
    for (const auto& s : g.salidas()) usados.insert(s);
    std::vector<std::pair<std::string, std::string>> sinNombre;
    for (const auto& n : g.nodos())
        if ((n.tipo == "salida" || n.tipo == "compositor") && n.params.value("nombre", std::string()).empty())
            sinNombre.emplace_back(n.id, n.grupo);
        else if (n.tipo == "compositor")
            usados.insert(n.params.value("nombre", std::string()));
    for (const auto& [id, grupo] : sinNombre) {
        // «Cámara 1 · salida», «Cámara 1 · salida 2», «Estudio · compositor»…
        // sin chocar con otra fuente.
        const bool comp = g.nodo(id)->tipo == "compositor";
        const std::string base = comp ? std::string("Estudio · compositor")
                                      : (grupo.empty() ? std::string("Estudio") : grupo) + " · salida";
        std::string nombre;
        for (int i = 1;; ++i) {
            nombre = i == 1 ? base : base + " " + std::to_string(i);
            const std::string tipo = rt_.engine().sourceTypeId(nombre);
            if (!usados.count(nombre) && (tipo.empty() || tipo == (comp ? kTipoCompositor : kTipoSalida))) break;
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
std::string VideoGrafos::ajustarEn(GrafoVideo& g, const std::string& id, json p) {
    const GrafoVideo::Nodo* n = g.nodo(id);
    if (n == nullptr) return "no existe la tarjeta «" + id + "»";
    std::string e;
    // La etiqueta y lo local son de la tarjeta; lo demás, de quien la maneja.
    for (const char* propio : {"etiqueta", "local"})
        if (p.contains(propio)) {
            if (!g.ajustar(id, json{{propio, p[propio]}}, &e)) return e;
            p.erase(propio);
        }
    if (p.empty()) return {};
    std::string destino = id;
    if (const GrafoVideo::Nodo* c = g.controlador(id)) destino = c->id;
    const GrafoVideo::Nodo* d = g.nodo(destino);
    if (d->tipo == "control" && !p.contains("params")) p = json{{"params", p}};
    g.ajustar(destino, p, &e);
    return e;
}

QString VideoGrafos::ajustar(const QString& id, const QVariantMap& params) {
    const json p = deVariant(params);
    return q(editar([&](GrafoVideo& g) { return ajustarEn(g, id.toStdString(), p); }));
}

QString VideoGrafos::controlar(const QString& id) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.controlarDesde(id.toStdString(), &e);
        return e;
    }));
}

QString VideoGrafos::agregarControlada(const QString& control, const QString& entrada) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.agregarControlada(control.toStdString(), entrada.toStdString(), "", &e);
        return e;
    }));
}

QString VideoGrafos::soltarControl(const QString& id) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.soltar(id.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor) {
    json v = deVariant(valor);
    const GrafoVideo::Nodo* n = grafo_.nodo(id.toStdString());
    const std::string tipo = n == nullptr ? "" : n->tipo == "control" ? n->params.value("tarjeta", std::string()) : n->tipo;
    const bool esGst = tipo == "gst";
    const char* donde = esGst ? "propiedades" : "ajustes";
    if (esGst) {
        // GStreamer lee el valor como texto: un entero que vino como 3.0 va como 3.
        if (v.is_number_float() && v.get<double>() == static_cast<double>(static_cast<long long>(v.get<double>())))
            v = static_cast<long long>(v.get<double>());
    } else if (v.is_string() && v.get<std::string>().size() == 7 && v.get<std::string>()[0] == '#') {
        // Los colores llegan como "#RRGGBB" y libobs los quiere 0xAABBGGRR.
        const long long rgb = std::stoll(v.get<std::string>().substr(1), nullptr, 16);
        v = 0xFF000000LL | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
    return q(editar([&](GrafoVideo& g) { return ajustarEn(g, id.toStdString(), json{{donde, {{clave.toStdString(), v}}}}); }));
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
    if (n == nullptr || (n->tipo != "salida" && n->tipo != "compositor"))
        return "«" + id + "» no es una salida ni un compositor del grafo";
    const bool comp = n->tipo == "compositor";
    const std::string nombre = n->params.value("nombre", std::string());
    const std::string tipo = rt_.engine().sourceTypeId(nombre);
    if (!tipo.empty() && tipo != (comp ? kTipoCompositor : kTipoSalida)) return "ya hay otra fuente que se llama «" + nombre + "»";
    SourceSpec spec;
    spec.id = comp ? kTipoCompositor : kTipoSalida;
    spec.name = nombre;
    spec.settingsJson = comp ? json{{"nodo", n->id}, {"config", grafo_.configCompositor(n->id).dump()}}.dump()
                             : json{{"fuente", n->grupo}}.dump();
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
    // que se fue queda guardado si tenía tarjetas (por si vuelve con el mismo
    // nombre); si estaba vacío, se borra.
    bool nuevos = false;
    for (const std::string& e : grafo_.entradas())
        if (!hay.contains(q(e)) && grafo_.trivial(e)) {
            grafo_.quitarEntrada(e);
            nuevos = true;
        }
    for (const QString& e : entradas_)
        if (grafo_.asegurarEntrada(e.toStdString())) nuevos = true;
    if (nuevos) guardar();
    if (entrada_.isEmpty() && !entradas_.isEmpty()) entrada_ = entradas_.first();
    for (const QString& e : entradas_) aplicar(e.toStdString());
    actualizarEstudio();
    // Si cambió por qué alguna tarjeta no corre, la interfaz se entera.
    json errores = json::object();
    for (const QString& e : entradas_) {
        const json x = erroresDe(e.toStdString());
        if (!x.empty()) errores[e.toStdString()] = x;
    }
    const std::string vistos = errores.dump();
    if (vistos != erroresVistos_) {
        erroresVistos_ = vistos;
        cambiaron = true;
    }
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

namespace {
/// El id de la tarjeta `rostros` de una entrada ("" si no tiene).
std::string tarjetaRostros(const GrafoVideo& g, const std::string& entrada) {
    for (const auto& n : g.nodos())
        if (n.grupo == entrada && n.tipo == "rostros") return n.id;
    return {};
}
} // namespace

bool VideoGrafos::cuadroRostros(const std::string& entrada, std::vector<std::uint8_t>& bgra, int& ancho, int& alto,
                                std::uint64_t& serie) const {
    const std::string id = tarjetaRostros(grafo_, entrada);
    if (id.empty()) return false;
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return false;
    bool ok = false;
    if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
        calldata_t cd = {};
        calldata_set_string(&cd, "nodo", id.c_str());
        calldata_set_ptr(&cd, "destino", &bgra);
        if (proc_handler_call(obs_source_get_proc_handler(f), "analisis", &cd)) {
            ancho = static_cast<int>(calldata_int(&cd, "ancho"));
            alto = static_cast<int>(calldata_int(&cd, "alto"));
            serie = static_cast<std::uint64_t>(calldata_int(&cd, "serie"));
            ok = true;
        }
        calldata_free(&cd);
        obs_source_release(f);
    }
    obs_source_release(src);
    return ok;
}

void VideoGrafos::marcarCaras(const std::string& entrada, const json& cajas) const {
    const std::string id = tarjetaRostros(grafo_, entrada);
    if (id.empty()) return;
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return;
    if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
        calldata_t cd = {};
        calldata_set_string(&cd, "nodo", id.c_str());
        calldata_set_string(&cd, "cajas", cajas.dump().c_str());
        proc_handler_call(obs_source_get_proc_handler(f), "caras", &cd);
        calldata_free(&cd);
        obs_source_release(f);
    }
    obs_source_release(src);
}

std::string VideoGrafos::configurarRostros(const std::map<std::string, std::string>& quiero) {
    return editar([&](GrafoVideo& g) {
        std::string e;
        for (const std::string& entrada : g.entradas()) {
            const std::string id = tarjetaRostros(g, entrada);
            const auto it = quiero.find(entrada);
            if (it != quiero.end()) {
                if (id.empty()) {
                    // Antes de la escena: ve la imagen ya corregida (color, recorte).
                    const std::string nuevo = g.agregar("rostros", entrada, "", json{{"uso", it->second}, {"auto", true}}, &e);
                    if (nuevo.empty()) return e;
                } else if (g.nodo(id)->params.value("uso", std::string()) != it->second) {
                    g.ajustar(id, json{{"uso", it->second}}, &e);
                }
            } else if (!id.empty()) {
                const GrafoVideo::Nodo* n = g.nodo(id);
                if (n->params.value("auto", false))
                    g.quitar(id, &e);
                else if (n->params.value("uso", std::string()) != "mirar")
                    g.ajustar(id, json{{"uso", "mirar"}}, &e);
            }
        }
        return std::string();
    });
}

void VideoGrafos::actualizarEstudio() {
    // Las fuentes de los compositores, con su configuración al día.
    for (const std::string& nombre : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(nombre) != kTipoCompositor) continue;
        obs_source_t* src = obs_get_source_by_name(nombre.c_str());
        if (src == nullptr) continue;
        obs_data_t* s = obs_source_get_settings(src);
        const std::string config = grafo_.configCompositor(obs_data_get_string(s, "nodo")).dump();
        if (config != obs_data_get_string(s, "config")) {
            obs_data_t* nuevo = obs_data_create();
            obs_data_set_string(nuevo, "config", config.c_str());
            obs_source_update(src, nuevo);
            obs_data_release(nuevo);
        }
        obs_data_release(s);
        obs_source_release(src);
    }
    // Las entradas que mandan algo afuera (a un compositor, a otra escena)
    // tienen que dar cuadros aunque no estén en una escena visible.
    std::set<std::string> quiero;
    for (const auto& c : grafo_.cables()) {
        const GrafoVideo::Nodo* d = grafo_.nodo(c.desde);
        const GrafoVideo::Nodo* h = grafo_.nodo(c.hacia);
        if (d != nullptr && h != nullptr && !d->grupo.empty() && h->grupo != d->grupo && c.entrada != "ctl")
            quiero.insert(d->grupo);
    }
    // Y las que tienen una tarjeta de rostros: Personas mira por ahí.
    for (const auto& n : grafo_.nodos())
        if (n.tipo == "rostros" && !n.grupo.empty()) quiero.insert(n.grupo);
    for (const std::string& e : quiero)
        if (!mostradas_.count(e))
            if (obs_source_t* o = obs_get_source_by_name(e.c_str())) {
                obs_source_inc_showing(o);
                obs_source_release(o);
                mostradas_.insert(e);
            }
    for (auto it = mostradas_.begin(); it != mostradas_.end();) {
        if (quiero.count(*it)) {
            ++it;
            continue;
        }
        if (obs_source_t* o = obs_get_source_by_name(it->c_str())) {
            obs_source_dec_showing(o);
            obs_source_release(o);
        }
        it = mostradas_.erase(it);
    }
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
