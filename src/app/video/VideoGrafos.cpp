// src/app/video/VideoGrafos.cpp — ver VideoGrafos.hpp

#include "video/VideoGrafos.hpp"

#include "video/Diagnostico.hpp"
#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include "video/Sincronia.hpp"

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "InputSpecs.hpp"

#include <obs.h>
#include <graphics/matrix4.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QImage>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump()))
        .toVariant()
        .toMap()
        .value("v");
}
json deVariant(const QVariant& v) {
    return json::parse(
               QJsonDocument::fromVariant(QVariantMap{{"v", v}}).toJson(QJsonDocument::Compact).toStdString(),
               nullptr, false)
        .value("v", json());
}
QString q(const std::string& s) { return QString::fromStdString(s); }
QString q(const char* s) { return QString::fromUtf8(s != nullptr ? s : ""); }

/// 0xAABBGGRR de libobs -> "#RRGGBB".
QString colorHex(long long c) {
    const int r = static_cast<int>(c & 0xFF), g = static_cast<int>((c >> 8) & 0xFF),
              b = static_cast<int>((c >> 16) & 0xFF);
    return QStringLiteral("#%1%2%3")
        .arg(r, 2, 16, QChar('0'))
        .arg(g, 2, 16, QChar('0'))
        .arg(b, 2, 16, QChar('0'))
        .toUpper();
}
/// Las propiedades de un módulo de GStreamer (CatalogoGst) como controles de
/// PropertiesModel.
QVariantList controlesGst(const json& props) {
    QVariantList out;
    for (const auto& p : props) {
        const std::string tipo = p.value("tipo", "");
        QString ayuda = q(p.value("descripcion", ""));
        if (!p.value("enVivo", false))
            ayuda += QStringLiteral(" (GStreamer no asegura que cambie con el video andando)");
        QVariantMap r{
            {"name", q(p.value("nombre", ""))}, {"label", q(p.value("nombre", ""))}, {"help", ayuda}};
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
                ops << QVariantMap{{"value", q(o.value("nick", ""))},
                                   {"label", q(o.value("descripcion", ""))}};
            r["options"] = ops;
            r["value"] = v.is_string() ? q(v.get<std::string>()) : QString();
        } else if (tipo == "string" || tipo == "flags") {
            r["kind"] = "text";
            r["value"] = v.is_string() ? q(v.get<std::string>()) : QString();
            if (tipo == "flags") {
                QStringList nicks;
                for (const auto& o : p.value("opciones", json::array()))
                    nicks << q(o.value("nick", ""));
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
    {"barras-hd", "Barras HD (SMPTE RP 219)",
     "Las barras de color de HD: para calibrar monitores y probar la cadena.",
     "videotestsrc pattern=smpte-rp-219 is-live=true"},
    {"barras", "Barras SMPTE (100 %)", "Las barras clásicas al 100 %.",
     "videotestsrc pattern=smpte100 is-live=true"},
    {"barras-75", "Barras SMPTE (75 %)", "Barras al 75 %: las de ajuste de la TV analógica.",
     "videotestsrc pattern=smpte75 is-live=true"},
    {"barras-hora", "Barras con la hora",
     "Barras HD con la hora encima: se ve si la señal está viva o congelada.",
     "videotestsrc pattern=smpte-rp-219 is-live=true ! clockoverlay time-format=\"%H:%M:%S\" "
     "font-desc=\"Sans 40\" "
     "halignment=center valignment=bottom"},
    {"negro", "Negro", "Una pantalla negra (para tapar una salida sin sacarla).",
     "videotestsrc pattern=black is-live=true"},
    {"blanco", "Blanco", "Una pantalla blanca.", "videotestsrc pattern=white is-live=true"},
    {"verde", "Verde croma", "Verde para probar una llave de croma.",
     "videotestsrc pattern=green is-live=true"},
    {"zona", "Placa de zonas", "Círculos concéntricos (zone plate): muestran el aliasing de un escalado.",
     "videotestsrc pattern=zone-plate kx2=20 ky2=20 kt=1 is-live=true"},
    {"damero", "Damero", "Cuadros de 8 píxeles: nitidez y escalado.",
     "videotestsrc pattern=checkers-8 is-live=true"},
    {"pelota", "Pelota en movimiento", "Una pelota que se mueve: para ver cuadros perdidos o trabados.",
     "videotestsrc pattern=ball is-live=true"},
    {"ruido", "Ruido", "Nieve de TV.", "videotestsrc pattern=snow is-live=true"},
};

} // namespace

namespace {
const char* desentrelazadoDe(obs_deinterlace_mode m) {
    switch (m) {
        case OBS_DEINTERLACE_MODE_DISABLE: return "no";
        case OBS_DEINTERLACE_MODE_DISCARD: return "descartar";
        case OBS_DEINTERLACE_MODE_RETRO: return "retro";
        case OBS_DEINTERLACE_MODE_BLEND: return "mezclar";
        case OBS_DEINTERLACE_MODE_BLEND_2X: return "mezclar2x";
        case OBS_DEINTERLACE_MODE_LINEAR: return "lineal";
        case OBS_DEINTERLACE_MODE_LINEAR_2X: return "lineal2x";
        case OBS_DEINTERLACE_MODE_YADIF: return "yadif";
        case OBS_DEINTERLACE_MODE_YADIF_2X: return "yadif2x";
    }
    return "no";
}
obs_deinterlace_mode desentrelazadoDesde(const std::string& m) {
    if (m == "descartar") return OBS_DEINTERLACE_MODE_DISCARD;
    if (m == "retro") return OBS_DEINTERLACE_MODE_RETRO;
    if (m == "mezclar") return OBS_DEINTERLACE_MODE_BLEND;
    if (m == "mezclar2x") return OBS_DEINTERLACE_MODE_BLEND_2X;
    if (m == "lineal") return OBS_DEINTERLACE_MODE_LINEAR;
    if (m == "lineal2x") return OBS_DEINTERLACE_MODE_LINEAR_2X;
    if (m == "yadif") return OBS_DEINTERLACE_MODE_YADIF;
    if (m == "yadif2x") return OBS_DEINTERLACE_MODE_YADIF_2X;
    return OBS_DEINTERLACE_MODE_DISABLE;
}
} // namespace

VideoGrafos::VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), archivo_(dataRoot / "Config" / "video-grafo.json"),
      archivoViejo_(dataRoot / "Config" / "video-grafos.json") {
    cargar();
    revision_.setInterval(1500);
    connect(&revision_, &QTimer::timeout, this, &VideoGrafos::sincronizar);
    revision_.start();
    QTimer::singleShot(0, this, &VideoGrafos::sincronizar);
    // El programador de superresolución (programa / preview): rápido, para que
    // la red se prenda apenas una cámara pasa a preview.
    programadorTimer_.setInterval(250);
    connect(&programadorTimer_, &QTimer::timeout, this, &VideoGrafos::evaluarSuperres);
    programadorTimer_.start();
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
    const json s = json::parse(e.sourceSettingsJson(n), nullptr, false);
    // Los micrófonos y el audio de los invitados también son ba_gst_fuente, pero
    // no tienen video: no van en el grafo de video.
    if (type == kTipoVirtual && s.is_object() && (s.contains("microfono") || s.contains("anillo") || s.contains("voz")))
        return false;
    if (type == inputs::kVideoType || type == kTipoVirtual) return true; // las cámaras de GStreamer son kTipoVirtual
    return inputs::isNetworkInput(type, s);
}

QStringList VideoGrafos::entradas() const { return entradas_; }

QVariantList VideoGrafos::entradasInfo() const {
    QVariantList out;
    for (const QString& e : entradas_) {
        const std::string n = e.toStdString();
        const std::string tipo = inputs::tipoLogico(rt_.engine().sourceTypeId(n),
                                                    json::parse(rt_.engine().sourceSettingsJson(n), nullptr, false));
        const QString grupo = tipo == inputs::kVideoType ? QStringLiteral("Cámaras")
                              : tipo == "ba_guest_video" ? QStringLiteral("Invitados")
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
            n["publicada"] = !nombre.empty() && rt_.engine().sourceTypeId(nombre) ==
                                                    (tipo == "salida" ? kTipoSalida : kTipoCompositor);
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
    if (!out.is_object()) out = json::object();
    // Las que corren en la cadena de GStreamer de la entrada.
    if (enEntrada_.count(entrada) && !enEntrada_.at(entrada).empty())
        for (const auto& f : filtrosDeEntrada(entrada)) {
            const std::string id = f.value("id", std::string());
            if (!f.value("andando", false))
                out[id] = "el elemento " + f.value("elemento", std::string()) + " no está en la cadena";
            else if (f.contains("error"))
                out[id] = f["error"];
        }
    return out;
}

json VideoGrafos::describir(const std::string& entrada) const {
    if (!entrada.empty() && !esEntrada(entrada))
        return {{"error", "«" + entrada + "» no es una entrada de video"}};
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
        if (!(flags & OBS_SOURCE_VIDEO) || (flags & OBS_SOURCE_ASYNC) || (flags & OBS_SOURCE_CAP_DISABLED))
            continue;
        const std::string nombre = nombreFiltro(fid);
        if (nombre.empty()) continue;
        out << QVariantMap{{"id", q(fid)}, {"nombre", q(nombre)}, {"propio", fid.rfind("ba_", 0) == 0}};
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("nombre").toString().localeAwareCompare(b.toMap().value("nombre").toString()) <
               0;
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
        return controlesGst(gst().propiedades(params.value("elemento", std::string()),
                                              params.value("propiedades", json::object())));
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
                        const QVariant v =
                            fmt == OBS_COMBO_FORMAT_INT
                                ? QVariant(static_cast<qlonglong>(obs_property_list_item_int(p, i)))
                            : fmt == OBS_COMBO_FORMAT_FLOAT
                                ? QVariant(obs_property_list_item_float(p, i))
                                : QVariant(q(obs_property_list_item_string(p, i)));
                        ops << QVariantMap{{"value", v}, {"label", q(obs_property_list_item_name(p, i))}};
                    }
                    r["options"] = ops;
                    r["value"] = fmt == OBS_COMBO_FORMAT_INT
                                     ? QVariant(static_cast<qlonglong>(obs_data_get_int(d, k)))
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
                default: continue; // botones, fuentes, listas editables: no tienen sentido acá
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

QVariantList VideoGrafos::propiedadesModuloCon(const QString& elemento, const QVariantMap& valores) const {
    // Los valores guardados vienen como texto (así van a GStreamer): se pasan al tipo de cada propiedad.
    json act = json::object();
    for (auto it = valores.begin(); it != valores.end(); ++it) {
        const QString t = it.value().toString();
        bool okN = false;
        const double n = t.toDouble(&okN);
        if (t == "true" || t == "false")
            act[it.key().toStdString()] = t == "true";
        else if (okN)
            act[it.key().toStdString()] = n;
        else
            act[it.key().toStdString()] = t.toStdString();
    }
    return controlesGst(gst().propiedades(elemento.toStdString(), act));
}

QVariantList VideoGrafos::modulosAudio() const { return aVariant(gst().modulosAudio()).toList(); }

QString VideoGrafos::versionGst() const {
    return gst().disponible() ? q(gst().version()) : q("Sin GStreamer: " + gst().error());
}

QVariantList VideoGrafos::virtuales() {
    QVariantList out;
    for (const auto& v : kVirtuales)
        out << QVariantMap{{"id", v.id},
                           {"nombre", q(v.nombre)},
                           {"descripcion", q(v.descripcion)},
                           {"pipeline", q(v.origen)}};
    return out;
}

std::string VideoGrafos::agregarModuloEn(GrafoVideo& g, const std::string& elemento, const std::string& grupo,
                                         const std::string& despues, const json& propiedades,
                                         std::string* error) const {
    // `baplugin`: una tarjeta de un plugin de Python. Lo registra el módulo de
    // video (no está en el catálogo de la app): no se valida acá.
    // Los de superresolución (también del módulo de video): agrandan x2.
    static const std::map<std::string, std::string> kSuperres{{"bavtsuperres", "Superresolución · Apple"},
                                                              {"bafsr", "Superresolución · FSR"},
                                                              {"baonnxsr", "Superresolución · red"}};
    const auto sr = kSuperres.find(elemento);
    const json m = elemento == "baplugin"
                       ? json{{"tarjeta", true},
                              {"titulo", "Plugin · " + (propiedades.is_object()
                                                            ? propiedades.value("tarjeta", std::string())
                                                            : std::string())}}
                   : sr != kSuperres.end() ? json{{"tarjeta", true}, {"titulo", sr->second}}
                                           : gst().modulo(elemento);
    if (m.empty()) {
        if (error)
            *error =
                gst().disponible() ? "no existe el módulo de GStreamer «" + elemento + "»" : gst().error();
        return {};
    }
    if (!m.value("tarjeta", false)) {
        if (error)
            *error = "«" + elemento + "» no va como tarjeta de filtro (" +
                     m.value("categoria", std::string()) +
                     "): " + m.value("uso", m.value("descripcion", std::string()));
        return {};
    }
    const std::string id = g.agregar(
        "gst", grupo, despues,
        json{{"elemento", elemento}, {"propiedades", propiedades.is_object() ? propiedades : json::object()}},
        error);
    if (!id.empty())
        g.ajustar(id, json{{"etiqueta", m.value("titulo", m.value("nombre", elemento))}}, nullptr);
    return id;
}

std::string VideoGrafos::agregarSuperres(const std::string& entrada, const std::string& motor,
                                         const std::string& modelo, const std::string& despues) {
    const std::map<std::string, std::string> elementos{
        {"apple", "bavtsuperres"}, {"fsr", "bafsr"}, {"onnx", "baonnxsr"}};
    const auto it = elementos.find(motor);
    if (it == elementos.end()) return "motor: apple, fsr u onnx";
    if (!motoresSuperres().contains(QString::fromStdString(motor)))
        return "este sistema no tiene el motor «" + motor + "»";
    if (motor == "onnx" && modelo.empty()) return "el motor onnx necesita `modelo` (la ruta del .onnx)";
    // La tarjeta «Superresolución» (el plugin basuperres), con el motor elegido.
    return editar([&](GrafoVideo& g) {
        std::string e;
        const std::string id =
            g.agregar("superres", entrada.empty() ? entrada_.toStdString() : entrada, despues,
                      json{{"motor", motor}, {"modelo", motor == "onnx" ? modelo : std::string()}}, &e);
        (void)id;
        return e;
    });
}

std::string VideoGrafos::superresPlugin(const std::string& entrada, const std::string& plugin, const json& extra) {
    const std::string e = entrada.empty() ? entrada_.toStdString() : entrada;
    return editar([&](GrafoVideo& g) -> std::string {
        // Si ya tiene una tarjeta de superresolución, se le cambia el plugin.
        for (const auto& n : g.nodos())
            if (n.grupo == e && n.tipo == "superres") {
                json p = n.params;
                p["plugin"] = plugin;
                p["activo"] = true;
                for (const auto& [k, v] : extra.items()) p[k] = v;
                std::string err;
                g.ajustar(n.id, p, &err);
                return err;
            }
        std::string err;
        json p{{"plugin", plugin}};
        for (const auto& [k, v] : extra.items()) p[k] = v;
        g.agregar("superres", e, "", p, &err);
        return err;
    });
}

QString VideoGrafos::agregarSuperresUi(const QString& motor, const QString& modelo, const QString& despues) {
    return q(agregarSuperres({}, motor.toStdString(), modelo.toStdString(), despues.toStdString()));
}

QString VideoGrafos::agregarModulo(const QString& elemento, const QString& despues) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        agregarModuloEn(g, elemento.toStdString(), entrada_.toStdString(), despues.toStdString(),
                        json::object(), &e);
        return e;
    }));
}

std::string VideoGrafos::crearEntradaVirtual(const std::string& nombre, const std::string& preset,
                                             const std::string& escena) {
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
            origen += " ! video/x-raw,width=" + std::to_string(ovi.base_width) +
                      ",height=" + std::to_string(ovi.base_height) +
                      ",framerate=" + std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den);
    }
    SourceSpec spec;
    spec.id = kTipoVirtual;
    spec.name = nombre;
    json ajustes = relojParaFuente();
    ajustes["pipeline"] = origen;
    ajustes["latencia"] = 0; // generada acá: no hay red que esperar
    spec.settingsJson = ajustes.dump();
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? "no se pudo crear" : rt_.lastError();
    rt_.save();
    sincronizar();
    return {};
}

QString VideoGrafos::crearVirtual(const QString& nombre, const QString& preset, const QString& escena) {
    return q(crearEntradaVirtual(nombre.toStdString(), preset.toStdString(), escena.toStdString()));
}

void VideoGrafos::compensarAudio(const std::string& entrada) {
    // Las tarjetas del grafo (superresolución, módulos de GStreamer, plugins)
    // demoran el video: el audio de la entrada se atrasa lo MISMO, medido (lo
    // viejo que es el cuadro que se dibuja: depende de cuánto tarda cada red).
    // Sin medición todavía, un cuadro por tarjeta. Se suma a lo que haya puesto
    // el operador (se lleva aparte lo que se agregó acá).
    obs_video_info ovi{};
    const double cuadroMs =
        obs_get_video_info(&ovi) && ovi.fps_num > 0 ? 1000.0 * ovi.fps_den / ovi.fps_num : 33.3;
    const json medidas = demorasDe(entrada);
    double total = 0;
    for (const auto& n : grafo_.nodos()) {
        if (n.grupo != entrada || (n.tipo != "superres" && n.tipo != "gst" && n.tipo != "plugin")) continue;
        // Las que corren en la cadena de GStreamer de la entrada: su colchón
        // ya atrasa el audio lo mismo que el video.
        if (enEntrada_.count(entrada) && enEntrada_.at(entrada).count(n.id)) continue;
        const json p = grafo_.efectivos(n);
        if (!p.value("activo", true)) continue;
        const double m = medidas.value(n.id, 0.0);
        if (m > 0)
            total += m;
        else if (p.value("esperar", 0) == 0)
            total += cuadroMs;
    }
    const int deseado = static_cast<int>(std::lround(total));
    const int antes = compAudio_.count(entrada) ? compAudio_[entrada] : 0;
    // Sin saltos chicos (el audio no se corre por cada vaivén de la medición).
    if (std::abs(deseado - antes) < 8) return;
    const int actual = rt_.engine().sourceSyncOffset(entrada);
    if (rt_.engine().setSourceSyncOffset(entrada, actual - antes + deseado)) compAudio_[entrada] = deseado;
}

nlohmann::json VideoGrafos::demorasDe(const std::string& entrada) const {
    json out = json::object();
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return out;
    if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
        calldata_t cd = {};
        if (proc_handler_call(obs_source_get_proc_handler(f), "estado", &cd)) {
            const char* d = calldata_string(&cd, "demoras");
            if (d != nullptr) out = json::parse(d, nullptr, false);
        }
        calldata_free(&cd);
        obs_source_release(f);
    }
    obs_source_release(src);
    return out.is_object() ? out : json::object();
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
        for (const QString& e : entradas_)
            aplicar(e.toStdString());
        actualizarEstudio();
        emit cambio();
    }
    return {};
}

void VideoGrafos::nombrarSalidas(GrafoVideo& g) const {
    std::set<std::string> usados;
    for (const auto& s : g.salidas())
        usados.insert(s);
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
            if (!usados.count(nombre) && (tipo.empty() || tipo == (comp ? kTipoCompositor : kTipoSalida)))
                break;
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
        const std::string id =
            g.agregar("obs", entrada_.toStdString(), despues.toStdString(),
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
QString VideoGrafos::conectar(const QString& desde, const QString& salida, const QString& hacia,
                              const QString& entrada) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.conectar(desde.toStdString(), salida.toStdString(), hacia.toStdString(), entrada.toStdString(), &e);
        return e;
    }));
}
QString VideoGrafos::desconectar(const QString& desde, const QString& salida, const QString& hacia,
                                 const QString& entrada) {
    return q(editar([&](GrafoVideo& g) {
        g.desconectar(desde.toStdString(), salida.toStdString(), hacia.toStdString(), entrada.toStdString());
        return std::string();
    }));
}
QString VideoGrafos::ordenar() {
    return q(editar([&](GrafoVideo& g) {
        constexpr int kCol = 220, kSep = 24, kEntreFranjas = 60;
        // El alto de una tarjeta, como la dibuja el Patchbay (cabecera + filas de puertos + cuerpo).
        const auto alto = [&g](const GrafoVideo::Nodo& n) {
            const size_t filas = std::max<size_t>({g.puertosEntrada(n).size(), g.puertosSalida(n).size(), 1});
            return 34 + static_cast<int>(filas) * 24 + 40;
        };
        std::vector<const GrafoVideo::Nodo*> controles, otrosEstudio;
        std::map<std::string, std::vector<const GrafoVideo::Nodo*>> grupos;
        std::vector<std::string> orden;
        for (const QString& e : entradas_)
            orden.push_back(e.toStdString());
        for (const auto& n : g.nodos()) {
            if (n.grupo.empty()) {
                (n.tipo == "control" ? controles : otrosEstudio).push_back(&n);
                continue;
            }
            if (std::find(orden.begin(), orden.end(), n.grupo) == orden.end()) orden.push_back(n.grupo);
            grupos[n.grupo].push_back(&n);
        }
        const int xBase = controles.empty() ? 0 : kCol + 40;
        std::map<std::string, std::pair<int, int>> pos;
        int y = 0, maxCol = 0;
        for (const std::string& gr : orden) {
            const auto it = grupos.find(gr);
            if (it == grupos.end()) continue;
            std::set<std::string> ids;
            for (const auto* n : it->second)
                ids.insert(n->id);
            // La profundidad de cada tarjeta: el camino más largo desde una sin entradas del grupo.
            std::map<std::string, std::vector<std::string>> hacia;
            std::map<std::string, int> entrantes;
            for (const auto& c : g.cables())
                if (ids.count(c.desde) && ids.count(c.hacia)) {
                    hacia[c.desde].push_back(c.hacia);
                    ++entrantes[c.hacia];
                }
            std::map<std::string, int> prof;
            std::vector<std::string> cola;
            for (const auto& id : ids)
                if (!entrantes.count(id)) {
                    prof[id] = 0;
                    cola.push_back(id);
                }
            for (size_t k = 0; k < cola.size(); ++k)
                for (const auto& h : hacia[cola[k]]) {
                    prof[h] = std::max(prof[h], prof[cola[k]] + 1);
                    if (--entrantes[h] == 0) cola.push_back(h);
                }
            for (const auto& id : ids)
                if (!prof.count(id)) prof[id] = 0; // en un ciclo: al principio
            // La escena, siempre en la última columna de su franja.
            int ultima = 0;
            for (const auto& [id, p] : prof)
                ultima = std::max(ultima, p);
            for (const auto* n : it->second)
                if (n->tipo == "escena") prof[n->id] = std::max(prof[n->id], ultima);
            std::map<int, int> altoCol;
            int altoFranja = 0;
            // Primero la cadena principal (la que llega a la escena), después las ramas.
            std::vector<const GrafoVideo::Nodo*> nodos = it->second;
            std::stable_sort(
                nodos.begin(), nodos.end(), [](const GrafoVideo::Nodo* a, const GrafoVideo::Nodo* b) {
                    const auto rama = [](const GrafoVideo::Nodo* n) {
                        return n->tipo == "salida" || n->tipo == "rostros" || n->tipo == "sincro";
                    };
                    return rama(a) < rama(b);
                });
            for (const auto* n : nodos) {
                const int col = prof[n->id];
                pos[n->id] = {xBase + col * kCol, y + altoCol[col]};
                altoCol[col] += alto(*n) + kSep;
                altoFranja = std::max(altoFranja, altoCol[col]);
                maxCol = std::max(maxCol, col);
            }
            y += altoFranja + kEntreFranjas;
        }
        int yc = 0;
        for (const auto* n : controles) {
            pos[n->id] = {0, yc};
            yc += alto(*n) + kSep;
        }
        int yo = 0;
        for (const auto* n : otrosEstudio) {
            pos[n->id] = {xBase + (maxCol + 1) * kCol + 40, yo};
            yo += alto(*n) + kSep;
        }
        for (const auto& [id, p] : pos)
            g.mover(id, p.first, p.second);
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
    // Una tarjeta de un plugin de Python (gst `baplugin`): sus ajustes viven en
    // la propiedad `parametros` (JSON en texto). Se aceptan como objeto
    // (`{"parametros": {"fuerza": 0.5}}`) o sueltos (`{"fuerza": 0.5}`), y se
    // mezclan con los que ya tiene.
    if (n->tipo == "gst" && n->params.value("elemento", std::string()) == "baplugin") {
        json cambios =
            p.contains("parametros") && p["parametros"].is_object() ? p["parametros"] : json::object();
        for (auto it = p.begin(); it != p.end(); ++it)
            if (it.key() != "parametros" && it.key() != "propiedades" && it.key() != "esperar" &&
                it.key() != "elemento")
                cambios[it.key()] = it.value();
        if (!cambios.empty()) {
            const json pr0 = n->params.value("propiedades", json::object());
            json actuales = json::parse(pr0.value("parametros", std::string("{}")), nullptr, false);
            if (!actuales.is_object()) actuales = json::object();
            actuales.update(cambios);
            json q2 = json::object();
            if (p.contains("esperar")) q2["esperar"] = p["esperar"];
            json pr = p.value("propiedades", json::object());
            pr["parametros"] = actuales.dump();
            q2["propiedades"] = pr;
            p = q2;
        }
    }
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
    const std::string tipo = n == nullptr           ? ""
                             : n->tipo == "control" ? n->params.value("tarjeta", std::string())
                                                    : n->tipo;
    const bool esGst = tipo == "gst";
    const char* donde = esGst ? "propiedades" : "ajustes";
    if (esGst) {
        // GStreamer lee el valor como texto: un entero que vino como 3.0 va como 3.
        if (v.is_number_float() &&
            v.get<double>() == static_cast<double>(static_cast<long long>(v.get<double>())))
            v = static_cast<long long>(v.get<double>());
    } else if (v.is_string() && v.get<std::string>().size() == 7 && v.get<std::string>()[0] == '#') {
        // Los colores llegan como "#RRGGBB" y libobs los quiere 0xAABBGGRR.
        const long long rgb = std::stoll(v.get<std::string>().substr(1), nullptr, 16);
        v = 0xFF000000LL | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
    return q(editar([&](GrafoVideo& g) {
        return ajustarEn(g, id.toStdString(), json{{donde, {{clave.toStdString(), v}}}});
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
    if (n == nullptr || (n->tipo != "salida" && n->tipo != "compositor"))
        return "«" + id + "» no es una salida ni un compositor del grafo";
    const bool comp = n->tipo == "compositor";
    const std::string nombre = n->params.value("nombre", std::string());
    const std::string tipo = rt_.engine().sourceTypeId(nombre);
    if (!tipo.empty() && tipo != (comp ? kTipoCompositor : kTipoSalida))
        return "ya hay otra fuente que se llama «" + nombre + "»";
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
    for (const QString& e : entradas_)
        aplicar(e.toStdString());
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

void VideoGrafos::reaplicarEntradas() {
    for (const QString& e : entradas_) aplicar(e.toStdString());
}

json VideoGrafos::estadoLegalizador() const {
    json out = json::object();
    for (const QString& q : entradas_) {
        const std::string e = q.toStdString();
        if (rt_.engine().sourceTypeId(e) != kTipoVirtual) continue;
        std::string estado = "falta";
        for (const auto& f : filtrosDeEntrada(e))
            if (f.value("elemento", std::string()) == "balegal")
                estado = f.contains("error") ? f["error"].dump() : f.value("andando", false) ? "andando" : "falta";
        out[e] = estado;
    }
    return out;
}

void VideoGrafos::setLegalizar(const std::string& modo) {
    const std::string m = modo == "pal" || modo == "ntsc" || modo == "rango" ? modo : std::string("no");
    if (m == legalizar_) return;
    legalizar_ = m;
    reaplicarEntradas();
}

void VideoGrafos::aplicar(const std::string& entrada) {
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return;
    // El desentrelazado de una tarjeta `normalizar` va a la FUENTE (libobs es
    // quien tiene los campos). Sin tarjeta, se deja como estaba, salvo que lo
    // hubiera puesto esta.
    if ((obs_source_get_output_flags(src) & OBS_SOURCE_ASYNC) != 0) {
        std::string modo, campo = "arriba";
        for (const auto& n : grafo_.nodos())
            if (n.grupo == entrada && n.tipo == "normalizar") {
                modo = n.params.value("desentrelazar", std::string("no"));
                campo = n.params.value("campo", std::string("arriba"));
            }
        if (!modo.empty()) {
            obs_source_set_deinterlace_mode(src, desentrelazadoDesde(modo));
            obs_source_set_deinterlace_field_order(src, campo == "abajo" ? OBS_DEINTERLACE_FIELD_ORDER_BOTTOM
                                                                         : OBS_DEINTERLACE_FIELD_ORDER_TOP);
            desentrelazadas_.insert(entrada);
        } else if (desentrelazadas_.erase(entrada) > 0) {
            obs_source_set_deinterlace_mode(src, OBS_DEINTERLACE_MODE_DISABLE);
        }
    }
    json para = grafo_.paraFiltro(entrada);
    // Una entrada de GStreamer: sus tarjetas de imagen corren en su cadena.
    if (rt_.engine().sourceTypeId(entrada) == kTipoVirtual) {
        json filtros = pasarAEntrada(entrada, para);
        // Legalizar para TV: al final de la cadena, después de todas las tarjetas.
        if (legalizar_ != "no") {
            const std::string compuesto = legalizar_ == "pal" ? "1.33" : legalizar_ == "ntsc" ? "1.2" : "0";
            if (!filtros.contains("video")) filtros["video"] = json::array();
            filtros["video"].push_back({{"id", "legalizar"},
                                        {"elemento", "balegal"},
                                        {"propiedades", {{"modo", "1"}, {"compuesto", compuesto}}}});
        }
        const std::string js = filtros.value("video", json::array()).empty() ? std::string() : filtros.dump();
        const auto ya = filtrosAplicados_.find(entrada);
        if (ya == filtrosAplicados_.end() || ya->second != js) {
            obs_data_t* actual = obs_source_get_settings(src);
            const bool igual = js == obs_data_get_string(actual, "filtros");
            obs_data_release(actual);
            if (!igual) {
                blog(LOG_INFO, "[video-grafo] «%s»: filtros en la entrada: %s", entrada.c_str(), js.c_str());
                obs_data_t* d = obs_data_create();
                obs_data_set_string(d, "filtros", js.c_str());
                actualizarFuente(src, d);
                obs_data_release(d);
            }
            filtrosAplicados_[entrada] = js;
        }
    } else {
        enEntrada_.erase(entrada);
    }
    // Los plugins de superresolución, traducidos a lo de esta máquina.
    if (resolverPlugin_)
        for (auto& n : para["nodos"]) {
            if (n.value("tipo", "") != "superres") continue;
            json& p = n["params"];
            const std::string id = p.value("plugin", std::string());
            if (id.empty()) continue;
            std::string err;
            const json r = resolverPlugin_(id, &err);
            if (!r.is_object() || r.empty()) {
                // Sin el plugin aquí: pasa sin tocar (y se avisa en el estado).
                p["activo"] = false;
                continue;
            }
            for (auto it = r.begin(); it != r.end(); ++it) p[it.key()] = it.value();
        }
    // Solo «in -> out»: no hace falta el filtro.
    const bool trivial = para["nodos"].size() == 2 && para["cables"].size() == 1 &&
                         para["cables"][0].value("desde", "") == "in" &&
                         para["cables"][0].value("hacia", "") == "out";
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
        if (obs_source_t* g =
                f != nullptr ? obs_source_get_ref(f) : obs_source_get_filter_by_name(src, kFiltro)) {
            obs_source_filter_set_order(src, g, OBS_ORDER_MOVE_BOTTOM);
            obs_source_release(g);
        }
    }
    if (f != nullptr) obs_source_release(f);
    obs_source_release(src);
    compensarAudio(entrada);
}

namespace {
/// Un número con decimales sin el locale (para propiedades de GStreamer).
std::string decimal(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v);
    for (char* c = b; *c; ++c)
        if (*c == ',') *c = '.';
    return b;
}
} // namespace

json VideoGrafos::pasarAEntrada(const std::string& entrada, json& para) {
    json video = json::array();
    bool alfa = false;
    std::set<std::string>& movidas = enEntrada_[entrada];
    movidas.clear();
    auto& nodos = para["nodos"];
    auto& cables = para["cables"];
    while (true) {
        // La entrada tiene que alimentar UNA sola tarjeta (si no, la imagen sin
        // tocar hace falta en otra parte del grafo).
        std::vector<size_t> desdeIn;
        for (size_t i = 0; i < cables.size(); ++i)
            if (cables[i].value("desde", "") == "in") desdeIn.push_back(i);
        if (desdeIn.size() != 1) break;
        const std::string id = cables[desdeIn[0]].value("hacia", "");
        auto it = std::find_if(nodos.begin(), nodos.end(), [&](const json& n) { return n.value("id", "") == id; });
        if (it == nodos.end()) break;
        const std::string tipo = it->value("tipo", "");
        const json p = it->value("params", json::object());
        json f = {{"id", id}};
        json pr = json::object();
        if (tipo == "color") {
            f["elemento"] = "bacolor";
            for (const char* k : {"brillo", "contraste", "saturacion", "gamma"})
                if (p.contains(k) && p[k].is_number()) pr[k] = decimal(p[k].get<double>());
        } else if (tipo == "croma") {
            f["elemento"] = "bacroma";
            pr["clave"] = std::to_string(p.value("clave", 0x00B140));
            for (const char* k : {"similitud", "suavidad", "derrame"})
                if (p.contains(k) && p[k].is_number()) pr[k] = decimal(p[k].get<double>());
            alfa = true;
        } else if (tipo == "recorte") {
            f["elemento"] = "barecorte";
            bool recorta = false;
            for (const char* k : {"izquierda", "arriba", "derecha", "abajo"}) {
                const double v = p.value(k, 0.0);
                pr[k] = decimal(v);
                recorta = recorta || v > 0;
            }
            pr["espejo-h"] = p.value("espejoH", false) ? "true" : "false";
            pr["espejo-v"] = p.value("espejoV", false) ? "true" : "false";
            alfa = alfa || recorta;
        } else if (tipo == "gst") {
            const std::string el = p.value("elemento", std::string());
            if (el.empty()) break;
            f["elemento"] = el;
            for (const auto obj_ = p.value("propiedades", json::object()); const auto& [k, v] : obj_.items())
                pr[k] = v.is_string() ? v.get<std::string>()
                        : v.is_boolean() ? std::string(v.get<bool>() ? "true" : "false")
                        : v.is_number_integer() ? std::to_string(v.get<long long>())
                        : v.is_number() ? decimal(v.get<double>())
                                        : v.dump();
        } else if (tipo == "superres") {
            f["elemento"] = "basuperres";
            json q = p;
            const std::string plugin = p.value("plugin", std::string());
            bool activo = p.value("activo", true);
            if (!plugin.empty() && resolverPlugin_) {
                std::string err;
                const json r = resolverPlugin_(plugin, &err);
                if (r.is_object() && !r.empty())
                    for (const auto& [k, v] : r.items()) q[k] = v;
                else
                    activo = false; // sin el plugin acá: pasa sin tocar
            }
            // Un plugin que ES un elemento (contrato abierto): va directo, con
            // la memoria que pide (la cadena sube y baja de la placa alrededor).
            if (activo && q.contains("elemento")) {
                f["elemento"] = q["elemento"];
                f["propiedades"] = q.value("propiedades", json::object());
                if (!q.value("memoria", std::string()).empty()) f["memoria"] = q["memoria"];
                if (q.contains("memoria_salida")) f["memoria_salida"] = q["memoria_salida"];
                video.push_back(f);
                movidas.insert(id);
                cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
                for (auto& c : cables)
                    if (c.value("desde", "") == id) c["desde"] = "in";
                nodos.erase(it);
                continue;
            }
            const std::string motor = q.value("motor", std::string("auto"));
            if (!motor.empty() && motor != "auto") pr["motor"] = motor;
            if (!q.value("modelo", std::string()).empty()) pr["modelo"] = q.value("modelo", std::string());
            // Las variantes compiladas para cada tamaño (el motor usa la del que llega).
            if (!q.value("variantes", std::string()).empty()) pr["variantes"] = q.value("variantes", std::string());
            if (q.value("escala_red", 2) > 2) pr["escala-red"] = std::to_string(q.value("escala_red", 2));
            if (!q.value("proveedor", std::string()).empty()) pr["proveedor"] = q.value("proveedor", std::string());
            pr["nitidez"] = decimal(q.value("nitidez", 0.2));
            // CoreML: dónde corre la red (auto = el Neural Engine).
            static const std::map<std::string, std::string> kUnidades{
                {"todas", "1"}, {"ane", "2"}, {"gpu", "3"}, {"cpu", "4"}};
            if (const auto u = kUnidades.find(q.value("unidades", std::string())); u != kUnidades.end())
                pr["unidades"] = u->second;
            pr["activo"] = activo ? "true" : "false";
        } else {
            break; // lo demás (salidas, tomas, rostros, retardo, filtros de OBS…) sigue en libobs
        }
        if (tipo != "superres" && !p.value("activo", true)) {
            // Apagada: no se agrega a la cadena (pasa sin tocar), pero igual sale del grafo.
        } else {
            f["propiedades"] = pr;
            video.push_back(f);
        }
        movidas.insert(id);
        // Recablear: lo que salía de la tarjeta ahora sale de la entrada.
        cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
        for (auto& c : cables)
            if (c.value("desde", "") == id) c["desde"] = "in";
        nodos.erase(it);
    }
    return {{"video", video}, {"alfa", alfa}};
}

json VideoGrafos::filtrosDeEntrada(const std::string& entrada) const {
    json out = json::array();
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return out;
    calldata_t cd = {};
    if (proc_handler_call(obs_source_get_proc_handler(src), "estado", &cd)) {
        const char* info = calldata_string(&cd, "info");
        const json j = info != nullptr ? json::parse(info, nullptr, false) : json();
        if (j.is_object()) out = j.value("filtros", json::array());
    }
    calldata_free(&cd);
    obs_source_release(src);
    return out;
}

namespace {
/// El id de la tarjeta `rostros` de una entrada ("" si no tiene).
std::string tarjetaRostros(const GrafoVideo& g, const std::string& entrada) {
    for (const auto& n : g.nodos())
        if (n.grupo == entrada && n.tipo == "rostros") return n.id;
    return {};
}
} // namespace

bool VideoGrafos::cuadroRostros(const std::string& entrada, std::vector<std::uint8_t>& bgra, int& ancho,
                                int& alto, std::uint64_t& serie) const {
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
                    const std::string nuevo =
                        g.agregar("rostros", entrada, "", json{{"uso", it->second}, {"auto", true}}, &e);
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

void VideoGrafos::actualizarFuente(::obs_source* src, ::obs_data* cambios) {
    // Por AppRuntime: cambia la fuente Y la copia de sus ajustes que se guarda.
    // Con obs_source_update directo, el cambio andaba hasta reiniciar.
    const char* json = obs_data_get_json(cambios);
    if (!rt_.updateSourceSettings(obs_source_get_name(src), json != nullptr ? json : "{}"))
        obs_source_update(src, cambios);
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

// --- El reloj del estudio ------------------------------------------------------------

json VideoGrafos::reloj() const { return reloj_; }

json VideoGrafos::relojParaFuente() const {
    return {{"reloj", reloj_.value("tipo", std::string("sistema"))},
            {"reloj_servidor", reloj_.value("servidor", std::string())},
            {"reloj_puerto", reloj_.value("puerto", 0)},
            {"reloj_dominio", reloj_.value("dominio", 0)}};
}

std::string VideoGrafos::configurarReloj(const json& cambios) {
    json r = reloj_;
    for (const char* k : {"tipo", "servidor", "puerto", "dominio", "latencia_ms", "publicar_puerto"})
        if (cambios.contains(k)) r[k] = cambios[k];
    const std::string tipo = r.value("tipo", std::string("sistema"));
    if (tipo != "sistema" && tipo != "ntp" && tipo != "ptp" && tipo != "red")
        return "reloj desconocido «" + tipo + "»: sistema, ntp, ptp o red";
    if ((tipo == "ntp" || tipo == "red") && r.value("servidor", std::string()).empty())
        return "con reloj " + tipo + " hace falta `servidor`";
    // Publicar el reloj de este aparato (para que otros usen `red` contra él).
    const int publicar = r.value("publicar_puerto", 0);
    if (publicar != reloj_.value("publicar_puerto", 0)) {
        calldata_t cd = {};
        calldata_set_int(&cd, "puerto", publicar);
        const bool hay = proc_handler_call(obs_get_proc_handler(), "ba_reloj_publicar", &cd);
        const std::string e = hay ? calldata_string(&cd, "error") : "este programa no trae GStreamer";
        const bool ok = hay && calldata_bool(&cd, "ok");
        calldata_free(&cd);
        if (!ok) return "no se pudo publicar el reloj: " + e;
    }
    reloj_ = r;
    QSaveFile f(QString::fromStdString((archivo_.parent_path() / "video-reloj.json").string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromStdString(reloj_.dump(2)));
        f.commit();
    }
    // A todas las fuentes de GStreamer (las de red, con la latencia del estudio
    // si no tienen una propia).
    const json rf = relojParaFuente();
    for (const std::string& n : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(n) != kTipoVirtual) continue;
        obs_source_t* src = obs_get_source_by_name(n.c_str());
        if (src == nullptr) continue;
        obs_data_t* actual = obs_source_get_settings(src);
        obs_data_t* d = obs_data_create_from_json(rf.dump().c_str());
        if (*obs_data_get_string(actual, "uri") != '\0' && !obs_data_get_bool(actual, "latencia_propia") &&
            reloj_.contains("latencia_ms"))
            obs_data_set_int(d, "latencia", reloj_.value("latencia_ms", 200));
        actualizarFuente(src, d); // en vivo Y en la configuración guardada (si no, se perdía al reiniciar)
        obs_data_release(d);
        obs_data_release(actual);
        obs_source_release(src);
    }
    rt_.save();
    return {};
}

// --- Estado de una entrada -------------------------------------------------------------

json VideoGrafos::estadoEntrada(const std::string& entrada) const {
    if (!esEntrada(entrada)) return {{"error", "«" + entrada + "» no es una entrada de video"}};
    json out{{"entrada", entrada}, {"tipo", rt_.engine().sourceTypeId(entrada)}};
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return out;
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    out["lienzo"] = {{"ancho", ovi.base_width},
                     {"alto", ovi.base_height},
                     {"fps", ovi.fps_den > 0 ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 0.0}};
    out["ancho"] = obs_source_get_width(src);
    out["alto"] = obs_source_get_height(src);
    out["activa"] = obs_source_active(src);
    out["mostrandose"] = obs_source_showing(src);
    const bool async = (obs_source_get_output_flags(src) & OBS_SOURCE_ASYNC) != 0;
    out["asincronica"] = async;
    if (async) {
        out["desentrelazado"] = desentrelazadoDe(obs_source_get_deinterlace_mode(src));
        out["campo"] = obs_source_get_deinterlace_field_order(src) == OBS_DEINTERLACE_FIELD_ORDER_BOTTOM
                           ? "abajo"
                           : "arriba";
    }
    obs_data_t* aj = obs_source_get_settings(src);
    out["ajustes"] = json::parse(obs_data_get_json(aj), nullptr, false);
    obs_data_release(aj);
    // Una fuente de GStreamer (red o virtual): todo lo que mide.
    calldata_t cd = {};
    if (proc_handler_call(obs_source_get_proc_handler(src), "estado", &cd)) {
        const char* info = calldata_string(&cd, "info");
        if (info != nullptr) out["gstreamer"] = json::parse(info, nullptr, false);
        const char* e = calldata_string(&cd, "error");
        if (e != nullptr && *e) out["error"] = e;
        out["andando"] = calldata_bool(&cd, "andando");
    }
    calldata_free(&cd);
    obs_source_release(src);
    // Lo que agrega su sub-grafo: latencia y lo que dicen las tarjetas.
    int cuadros = 0;
    double ms = 0;
    const json medidas = demorasDe(entrada);
    for (const auto& n : grafo_.nodos()) {
        if (n.grupo != entrada) continue;
        const json p = grafo_.efectivos(n);
        const bool enGst = enEntrada_.count(entrada) && enEntrada_.at(entrada).count(n.id);
        if ((n.tipo == "gst" || n.tipo == "superres" || n.tipo == "plugin") && !enGst) {
            const double m = medidas.value(n.id, 0.0);
            if (m > 0)
                ms += m; // medida
            else if (p.value("esperar", 0) == 0)
                ++cuadros;
        }
        if (n.tipo == "retardo") {
            if (p.value("ms", 0) > 0)
                ms += p.value("ms", 0);
            else
                cuadros += p.value("cuadros", 0);
        }
    }
    const double cuadroMs =
        out["lienzo"].value("fps", 30.0) > 0 ? 1000.0 / out["lienzo"].value("fps", 30.0) : 33.3;
    double latencia = ms + cuadros * cuadroMs;
    if (out.contains("gstreamer"))
        latencia += out["gstreamer"].value("latencia_ms", 0) + out["gstreamer"].value("desfase_ms", 0);
    if (out.contains("gstreamer") && out["gstreamer"].is_object())
        latencia += out["gstreamer"].value("compensacion_ms", 0);
    out["latencia_grafo_ms"] = std::round(latencia);
    if (enEntrada_.count(entrada) && !enEntrada_.at(entrada).empty()) {
        json l = json::array();
        for (const auto& id : enEntrada_.at(entrada))
            l.push_back(id);
        out["tarjetas_en_gstreamer"] = l;
    }
    if (!medidas.empty()) out["demoras_tarjetas_ms"] = medidas;
    if (estadoProgramador_.contains(entrada)) out["superres_programador"] = estadoProgramador_[entrada];
    if (compAudio_.count(entrada)) out["audio_atrasado_ms"] = compAudio_.at(entrada);
    const json errs = erroresDe(entrada), avs = avisosDe(entrada);
    if (!errs.empty()) out["errores_tarjetas"] = errs;
    if (!avs.empty()) out["avisos_tarjetas"] = avs;
    return out;
}

json VideoGrafos::avisosDe(const std::string& entrada) const {
    json out = json::object();
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return out;
    if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
        calldata_t cd = {};
        if (proc_handler_call(obs_source_get_proc_handler(f), "estado", &cd)) {
            const char* a = calldata_string(&cd, "avisos");
            if (a != nullptr) out = json::parse(a, nullptr, false);
        }
        calldata_free(&cd);
        obs_source_release(f);
    }
    obs_source_release(src);
    return out.is_object() ? out : json::object();
}

// --- Entradas de red -----------------------------------------------------------------

std::string VideoGrafos::crearEntradaRed(const std::string& nombre, const std::string& uri, int latenciaMs,
                                         const std::string& escena, int audio, int normalizar, int ntpSync,
                                         int superres, const std::string& sincronia) {
    if (!sincronia.empty() && sincronia != "colchon" && sincronia != "gstreamer")
        return "`sincronia`: colchon o gstreamer";
    if (nombre.empty()) return "falta el nombre";
    // Si ya existe (de GStreamer), se actualiza: otra URI, otro colchón.
    if (rt_.engine().sourceTypeId(nombre) == kTipoVirtual) {
        obs_source_t* src = obs_get_source_by_name(nombre.c_str());
        if (src == nullptr) return "no existe «" + nombre + "»";
        obs_data_t* d = obs_data_create();
        if (!uri.empty()) obs_data_set_string(d, "uri", uri.c_str());
        if (latenciaMs >= 0) {
            obs_data_set_int(d, "latencia", latenciaMs);
            obs_data_set_bool(d, "latencia_propia", true);
        }
        if (audio >= 0) obs_data_set_bool(d, "audio", audio == 1);
        if (normalizar >= 0) obs_data_set_bool(d, "normalizar", normalizar == 1);
        if (ntpSync >= 0) obs_data_set_bool(d, "ntp_sync", ntpSync == 1);
        if (superres >= 0) obs_data_set_bool(d, "superres", superres == 1);
        if (!sincronia.empty()) obs_data_set_string(d, "sincronia", sincronia.c_str());
        actualizarFuente(src, d); // en vivo Y en la configuración guardada (si no, se perdía al reiniciar)
        obs_data_release(d);
        obs_source_release(src);
        rt_.save();
        return {};
    }
    if (uri.empty()) return "falta `uri` (srt://, rtmp://, rtsp://, udp://, http://)";
    if (!rt_.engine().sourceTypeId(nombre).empty())
        return "ya hay una fuente que se llama «" + nombre + "» (y no es de red)";
    if (escena.empty()) return "falta `escena`: dónde ponerla (para no tocar la que está al aire sin querer)";
    if (!asegurarEscena(escena)) return "no se pudo crear la escena «" + escena + "»";
    json ajustes = relojParaFuente();
    ajustes["uri"] = uri;
    ajustes["audio"] = audio != 0;
    ajustes["normalizar"] = normalizar != 0;
    if (ntpSync >= 0) ajustes["ntp_sync"] = ntpSync == 1;
    if (superres >= 0) ajustes["superres"] = superres == 1;
    if (!sincronia.empty()) ajustes["sincronia"] = sincronia;
    ajustes["latencia"] = latenciaMs >= 0 ? latenciaMs : reloj_.value("latencia_ms", 200);
    ajustes["latencia_propia"] = latenciaMs >= 0;
    SourceSpec spec;
    spec.id = kTipoVirtual;
    spec.name = nombre;
    spec.settingsJson = ajustes.dump();
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? "no se pudo crear" : rt_.lastError();
    rt_.save();
    sincronizar();
    return {};
}

std::string VideoGrafos::agregarTarjetaGst(const std::string& entrada, const std::string& elemento,
                                           const json& propiedades, json* hecho) {
    if (!esEntrada(entrada)) return "«" + entrada + "» no es una entrada de video";
    std::string id;
    const std::string e = editar([&](GrafoVideo& g) {
        std::string err;
        id = agregarModuloEn(g, elemento, entrada, "", propiedades, &err);
        return err;
    });
    if (!e.empty()) return e;
    if (hecho != nullptr)
        *hecho = {
            {"entrada", entrada}, {"tarjeta", id}, {"elemento", elemento}, {"propiedades", propiedades}};
    return {};
}

std::string VideoGrafos::crearEntradaPipeline(const std::string& nombre, const std::string& video,
                                              const std::string& audio, const std::string& escena,
                                              int latenciaMs, int normalizar, const json& extra) {
    if (nombre.empty()) return "falta el nombre";
    if (video.empty() && audio.empty()) return "falta el pipeline (de video y/o de audio)";
    json ajustes = extra.is_object() ? extra : json::object();
    ajustes["uri"] = "";
    ajustes["pipeline"] = video;
    ajustes["pipeline_audio"] = audio;
    ajustes["audio"] = !audio.empty();
    if (normalizar >= 0) ajustes["normalizar"] = normalizar == 1;
    if (latenciaMs >= 0) {
        ajustes["latencia"] = latenciaMs;
        ajustes["latencia_propia"] = true;
    }
    if (rt_.engine().sourceTypeId(nombre) == kTipoVirtual) {
        obs_source_t* src = obs_get_source_by_name(nombre.c_str());
        if (src == nullptr) return "no existe «" + nombre + "»";
        obs_data_t* d = obs_data_create_from_json(ajustes.dump().c_str());
        actualizarFuente(src, d); // en vivo Y en la configuración guardada (si no, se perdía al reiniciar)
        obs_data_release(d);
        obs_source_release(src);
        rt_.save();
        return {};
    }
    if (!rt_.engine().sourceTypeId(nombre).empty())
        return "ya hay una fuente que se llama «" + nombre + "» (y no es de GStreamer)";
    if (escena.empty()) return "falta `escena`: dónde ponerla (para no tocar la que está al aire sin querer)";
    if (!asegurarEscena(escena)) return "no se pudo crear la escena «" + escena + "»";
    json todo = relojParaFuente();
    todo.update(ajustes);
    if (!todo.contains("normalizar")) todo["normalizar"] = false;
    if (!todo.contains("latencia")) {
        todo["latencia"] = 60;
        todo["latencia_propia"] = true;
    }
    SourceSpec spec;
    spec.id = kTipoVirtual;
    spec.name = nombre;
    spec.settingsJson = todo.dump();
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? "no se pudo crear" : rt_.lastError();
    rt_.save();
    sincronizar();
    return {};
}

// --- Sincronía y red, para la interfaz -------------------------------------------------

QVariantMap VideoGrafos::relojUi() const { return aVariant(reloj()).toMap(); }

QString VideoGrafos::configurarRelojUi(const QVariantMap& cambios) {
    return q(configurarReloj(deVariant(cambios)));
}

QVariantList VideoGrafos::estadoRed() const {
    QVariantList out;
    for (const QString& e : entradas_) {
        if (rt_.engine().sourceTypeId(e.toStdString()) != kTipoVirtual) continue;
        json st = estadoEntrada(e.toStdString());
        st["diagnostico"] = diagnosticar(st);
        out << aVariant(st);
    }
    return out;
}

QVariantList VideoGrafos::diagnosticoUi() const {
    QVariantList out;
    for (const QString& e : entradas_) {
        json st = estadoEntrada(e.toStdString());
        const json d = diagnosticar(st);
        if (d.empty()) continue;
        out << aVariant(json{{"entrada", e.toStdString()}, {"diagnostico", d}});
    }
    return out;
}

void VideoGrafos::medirUi(const QString& a, const QString& b, bool aplicar) {
    QPointer<VideoGrafos> yo(this);
    medirDesfase(a.toStdString(), b.toStdString(), 6, aplicar, [yo](json r) {
        if (yo) emit yo->medicion(aVariant(r).toMap());
    });
}

QString VideoGrafos::desfaseUi(const QString& entrada, double ms) {
    json hecho;
    return q(ajustarDesfase(entrada.toStdString(), ms, false, &hecho));
}

QString VideoGrafos::superresUi(const QString& entrada, bool activa) {
    if (activa && !haySuperres()) return "Este sistema no tiene superresolución (necesita macOS 26).";
    return q(crearEntradaRed(entrada.toStdString(), {}, -1, {}, -1, -1, -1, activa ? 1 : 0));
}

QString VideoGrafos::sincroniaUi(const QString& entrada, const QString& modo) {
    return q(crearEntradaRed(entrada.toStdString(), {}, -1, {}, -1, -1, -1, -1, modo.toStdString()));
}

std::string VideoGrafos::superresMotor(const std::string& entrada, const std::string& motor,
                                       const std::string& modelo) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return "«" + entrada + "» no es una entrada de red";
    if (!motor.empty() && motor != "apple" && motor != "fsr" && motor != "onnx")
        return "motor: apple, fsr u onnx (o vacío para apagar)";
    const QStringList hay = motoresSuperres();
    if (!motor.empty() && !hay.contains(QString::fromStdString(motor)))
        return "este sistema no tiene el motor «" + motor + "»";
    if (motor == "onnx" && modelo.empty()) return "el motor onnx necesita `modelo` (la ruta del .onnx)";
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return "no existe «" + entrada + "»";
    obs_data_t* d = obs_data_create();
    obs_data_set_bool(d, "superres", false); // el interruptor viejo: lo manda el motor
    obs_data_set_string(d, "superres_motor", motor.c_str());
    obs_data_set_string(d, "superres_modelo", motor == "onnx" ? modelo.c_str() : "");
    actualizarFuente(src, d);
    obs_data_release(d);
    obs_source_release(src);
    rt_.save();
    return {};
}

namespace {
bool hayElemento(const char* elemento);
} // namespace

std::string VideoGrafos::controlEntrada(const std::string& entrada, const json& control) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return "«" + entrada + "» no es una entrada de GStreamer (red, pipeline o plugin)";
    if (!control.is_object() || control.empty()) return "`control` tiene que ser un objeto con campos";
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return "no existe «" + entrada + "»";
    calldata_t cd = {};
    const std::string js = control.dump();
    calldata_set_string(&cd, "json", js.c_str());
    const bool llamo = proc_handler_call(obs_source_get_proc_handler(src), "control", &cd);
    const bool ok = llamo && calldata_bool(&cd, "ok");
    calldata_free(&cd);
    obs_source_release(src);
    return ok ? std::string() : "la entrada no aceptó el control";
}

namespace {
/// Lo que se ve de cada entrada en una escena: el ancho mayor en pantalla (px del lienzo).
struct Recorrido {
    const VideoGrafos* vg;
    const GrafoVideo* grafo;
    std::function<std::string(const std::string&)> entradaDe;
    std::map<std::string, double>* anchos;
    double factor;
    double lienzoAncho;
    int profundidad;
};

void recorrerEscena(obs_scene_t* escena, Recorrido r);

bool alItem(obs_scene_t*, obs_sceneitem_t* item, void* d) {
    auto* r = static_cast<Recorrido*>(d);
    if (!obs_sceneitem_visible(item)) return true;
    obs_source_t* src = obs_sceneitem_get_source(item);
    if (src == nullptr) return true;
    matrix4 m;
    obs_sceneitem_get_box_transform(item, &m);
    const double anchoCaja = std::hypot(m.x.x, m.x.y) * r->factor;
    if (obs_sceneitem_is_group(item)) {
        // Los de un grupo: con la escala del grupo.
        vec2 esc;
        obs_sceneitem_get_scale(item, &esc);
        Recorrido sub = *r;
        sub.factor = r->factor * esc.x;
        obs_sceneitem_group_enum_items(item, alItem, &sub);
        return true;
    }
    if (obs_scene_t* anidada = obs_scene_from_source(src); anidada != nullptr && r->profundidad < 6) {
        // Una escena dentro de otra: su lienzo entra en la caja.
        Recorrido sub = *r;
        sub.factor = r->lienzoAncho > 0 ? anchoCaja / r->lienzoAncho : r->factor;
        sub.profundidad = r->profundidad + 1;
        recorrerEscena(anidada, sub);
        return true;
    }
    const std::string e = r->entradaDe(obs_source_get_name(src));
    if (!e.empty()) (*r->anchos)[e] = std::max((*r->anchos)[e], anchoCaja);
    return true;
}

void recorrerEscena(obs_scene_t* escena, Recorrido r) { obs_scene_enum_items(escena, alItem, &r); }
} // namespace

void VideoGrafos::evaluarSuperres() {
    if (!obs_initialized()) return;
    // Las entradas con superresolución en su cadena de GStreamer.
    std::set<std::string> conSr;
    for (const auto& [entrada, ids] : enEntrada_)
        for (const auto& id : ids)
            if (const auto* n = grafo_.nodo(id); n != nullptr && n->tipo == "superres") conSr.insert(entrada);
    if (conSr.empty()) {
        estadoProgramador_ = json::object();
        presupuesto_ = json::object();
        return;
    }
    const json& cfg = programador_;
    const bool activo = cfg.value("activo", true);
    const bool automatica = cfg.value("capacidad", std::string("auto")) != "fija";
    const int tope = std::clamp(cfg.value("max_activas", 3), 0, 16);
    const double usoPct = std::clamp(cfg.value("uso_pct", 85.0), 10.0, 100.0);
    const int transicion = std::clamp(cfg.value("transicion_ms", 400), 0, 10000);
    const bool enPreview = cfg.value("en_preview", true);
    const std::string sinTurno = cfg.value("sin_turno", std::string("barato")) == "reposo" ? "reposo" : "barato";
    const double umbral = std::clamp(cfg.value("umbral_aumento", 1.1), 0.5, 4.0);
    const json forzar = cfg.value("forzar", json::object());
    obs_video_info ovi{};
    obs_get_video_info(&ovi);
    const double cuadroMs = ovi.fps_num > 0 ? 1000.0 * ovi.fps_den / ovi.fps_num : 33.3;

    // Lo medido de cada red (cada ~1 s: el estado de la cadena cuesta algo).
    if (ticsProgramador_++ % 4 == 0)
        for (const std::string& e : conSr)
            for (const auto& f : filtrosDeEntrada(e)) {
                if (f.value("elemento", std::string()) != "basuperres") continue;
                const json info = f.value("info", json::object());
                medidasSr_[e] = {info.value("ms-por-cuadro", 0.0), info.value("acelerador", std::string("?"))};
            }

    // Qué fuente es de qué entrada: la entrada misma, o una salida de su grafo.
    const auto entradaDe = [this](const std::string& nombre) -> std::string {
        if (esEntrada(nombre)) return nombre;
        if (rt_.engine().sourceTypeId(nombre) == kTipoSalida)
            for (const auto& n : grafo_.nodos())
                if (n.tipo == "salida" && n.params.value("nombre", std::string()) == nombre) return n.grupo;
        return {};
    };
    const auto anchosDe = [&](const std::string& escena) {
        std::map<std::string, double> anchos;
        if (escena.empty()) return anchos;
        obs_source_t* src = obs_get_source_by_name(escena.c_str());
        if (src == nullptr) return anchos;
        if (obs_scene_t* sc = obs_scene_from_source(src))
            recorrerEscena(sc, Recorrido{this, &grafo_, entradaDe, &anchos, 1.0, static_cast<double>(ovi.base_width), 0});
        obs_source_release(src);
        return anchos;
    };
    const std::string escPrograma = rt_.engine().currentScene();
    const std::string escPreview = rt_.engine().scenePreviewName("armada");
    const auto enPrograma = anchosDe(escPrograma);
    const auto enPreviewMap = enPreview ? anchosDe(escPreview) : std::map<std::string, double>{};

    struct Cand {
        std::string entrada;
        double prioridad;
    };
    std::vector<Cand> candidatos;
    json estado = json::object();
    std::map<std::string, std::string> quiero;
    for (const std::string& e : conSr) {
        const double ap = enPrograma.count(e) ? enPrograma.at(e) : 0.0;
        const double av = enPreviewMap.count(e) ? enPreviewMap.at(e) : 0.0;
        obs_source_t* src = obs_get_source_by_name(e.c_str());
        const double ancho = src != nullptr ? obs_source_get_width(src) : 0.0;
        if (src != nullptr) obs_source_release(src);
        // Cuánto se agranda en pantalla respecto de la cámara (la fuente sale x2).
        const double aumento = ancho > 0 ? std::max(ap, av) / (ancho / 2.0) : 0.0;
        const auto med = medidasSr_.count(e) ? medidasSr_[e] : std::pair<double, std::string>{0.0, "?"};
        const std::string forzado = forzar.value(e, std::string("auto"));
        estado[e] = {{"programa", ap > 0},
                     {"preview", av > 0},
                     {"aumento", std::round(aumento * 100) / 100},
                     {"ms", std::round(med.first * 10) / 10},
                     {"acelerador", med.second},
                     {"forzado", forzado}};
        const bool seVe = ap > 0 || av > 0;
        quiero[e] = activo ? "reposo" : "activo";
        if (!activo) continue;
        if (forzado == "siempre") {
            candidatos.push_back({e, 100.0}); // primero, consuma lo que consuma
        } else if (forzado != "nunca" && seVe && aumento > umbral) {
            double pr = (ap > 0 ? 2.0 : 0.0) + (av > 0 ? 1.0 : 0.0) + std::min(aumento, 4.0) / 4.0;
            if (modoSr_.count(e) && modoSr_[e] == "activo") pr += 0.05; // estabilidad
            candidatos.push_back({e, pr});
        } else if (forzado == "nunca" && seVe && aumento > umbral) {
            quiero[e] = sinTurno;
        }
    }
    std::sort(candidatos.begin(), candidatos.end(), [](const Cand& a, const Cand& b) { return a.prioridad > b.prioridad; });

    // La capacidad: cada acelerador corre de a una red por vez; en un cuadro le
    // entra lo que sumen sus redes hasta el % de uso. Lo que ya está activo tiene
    // un margen (15 %) para no oscilar con la medición; lo nuevo entra si cabe y
    // pasaron 2 s del último cambio (que la medición se asiente).
    const double disponible = cuadroMs * usoPct / 100.0;
    std::map<std::string, double> usado;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    const bool puedeSumar = ahora - ultimoCambioSr_ > 2000;
    int activas = 0;
    for (int pasada = 0; pasada < 2 && activo; ++pasada)
        for (const auto& c : candidatos) {
            const bool yaActiva = modoSr_.count(c.entrada) && modoSr_[c.entrada] == "activo";
            if ((pasada == 0) != yaActiva || quiero[c.entrada] == "activo") continue;
            const auto med = medidasSr_.count(c.entrada) ? medidasSr_[c.entrada] : std::pair<double, std::string>{0.0, "?"};
            // Sin medición todavía: lo de una red compacta en la Mac.
            const double ms = med.first > 0 ? med.first : 35.0;
            const bool forzada = c.prioridad >= 100.0;
            bool entra = forzada;
            if (!entra) {
                if (activas >= tope) {
                    entra = false;
                } else if (!automatica) {
                    entra = true;
                } else {
                    const double margen = yaActiva ? 1.15 : 1.0;
                    // Un acelerador sin nada prendido: la de más prioridad entra aunque no
                    // llegue a cada cuadro (procesa los que puede; los demás salen con el
                    // agrandado común: nunca traba la entrada).
                    const bool primeraDelAcelerador = usado.count(med.second) == 0 || usado[med.second] == 0.0;
                    entra = (usado[med.second] + ms <= disponible * margen || primeraDelAcelerador) &&
                            (yaActiva || puedeSumar);
                }
            }
            if (entra) {
                quiero[c.entrada] = "activo";
                usado[med.second] += ms;
                ++activas;
            } else {
                quiero[c.entrada] = sinTurno;
            }
        }
    // La alarma «no llega»: una red activa que tarda más que el umbral (de fábrica, el
    // 80 % del cuadro) durante 2 s. Con el bypass automático pasa sola al modo barato
    // (sin trabar la entrada) y vuelve a probar después de 15 s, 30 s, 1 min, 2 min…
    const bool bypassAuto = cfg.value("bypass_auto", true);
    const double umbralPct = std::clamp(cfg.value("umbral_pct", 80.0), 30.0, 200.0);
    for (const std::string& e : conSr) {
        Tarde& t = tarde_[e];
        const bool activa = modoSr_.count(e) && modoSr_[e] == "activo";
        const double ms = medidasSr_.count(e) ? medidasSr_[e].first : 0.0;
        const bool lenta = activa && ms > cuadroMs * umbralPct / 100.0;
        if (lenta) {
            if (t.desde == 0) t.desde = ahora;
        } else {
            t.desde = 0;
        }
        if (t.veces > 0 && ahora - t.ultimaVez > 10 * 60 * 1000) t.veces = 0; // 10 min tranquila: de cero
        if (t.desde != 0 && ahora - t.desde >= 2000 && !t.avisado) {
            t.avisado = true;
            t.ultimaVez = ahora;
            if (bypassAuto) {
                ++t.veces;
                t.bypassHasta = ahora + std::min<qint64>(15000LL << std::min(t.veces - 1, 4), 120000);
            }
            blog(LOG_WARNING, "[video-grafo] superresolución de «%s» no llega: %.1f ms por cuadro (cuadro %.1f ms)%s",
                 e.c_str(), ms, cuadroMs, bypassAuto ? "; pasada a barato" : "");
            emit superresTarde(QString::fromStdString(e), ms, cuadroMs, bypassAuto);
        }
        if (!lenta && ahora >= t.bypassHasta) t.avisado = false;
        const bool enBypass = bypassAuto && ahora < t.bypassHasta;
        if (enBypass && quiero[e] == "activo") quiero[e] = sinTurno;
        estado[e]["tarde"] = (t.desde != 0 && ahora - t.desde >= 2000) || enBypass;
        if (enBypass) estado[e]["bypass_s"] = (t.bypassHasta - ahora + 999) / 1000;
    }

    presupuesto_ = json::object();
    for (const auto& [acc, ms] : usado)
        presupuesto_[acc] = {{"usado_ms", std::round(ms * 10) / 10}, {"disponible_ms", std::round(disponible * 10) / 10}};

    for (const auto& [e, modo] : quiero) {
        estado[e]["modo"] = modo;
        if (modoSr_.count(e) && modoSr_[e] == modo) continue;
        if (controlEntrada(e, json{{"superres", modo}, {"transicion_ms", transicion}}).empty()) {
            if (modo == "activo") ultimoCambioSr_ = ahora;
            modoSr_[e] = modo;
            blog(LOG_INFO, "[video-grafo] superresolución de «%s»: %s", e.c_str(), modo.c_str());
        }
    }
    for (auto it = modoSr_.begin(); it != modoSr_.end();)
        it = conSr.count(it->first) ? std::next(it) : modoSr_.erase(it);
    estadoProgramador_ = estado;
}

json VideoGrafos::programador() const {
    json out = programador_;
    out["entradas"] = estadoProgramador_;
    out["presupuesto"] = presupuesto_;
    obs_video_info ovi{};
    if (obs_get_video_info(&ovi) && ovi.fps_num > 0) out["cuadro_ms"] = std::round(10000.0 * ovi.fps_den / ovi.fps_num) / 10;
    out["programa"] = rt_.engine().currentScene();
    out["preview"] = rt_.engine().scenePreviewName("armada");
    return out;
}

std::string VideoGrafos::configurarProgramador(const json& cambios) {
    if (!cambios.is_object()) return "se esperaba un objeto";
    for (const char* k : {"activo", "capacidad", "max_activas", "uso_pct", "transicion_ms", "en_preview", "sin_turno",
                          "umbral_aumento", "bypass_auto", "umbral_pct"})
        if (cambios.contains(k)) programador_[k] = cambios[k];
    // Forzar por entrada: {"Cámara 1": "siempre"|"nunca"|"auto"} (auto la saca de la lista).
    if (cambios.contains("forzar") && cambios["forzar"].is_object()) {
        json f = programador_.value("forzar", json::object());
        for (const auto& [e, v] : cambios["forzar"].items()) {
            const std::string x = v.is_string() ? v.get<std::string>() : "auto";
            if (x == "siempre" || x == "nunca")
                f[e] = x;
            else
                f.erase(e);
        }
        programador_["forzar"] = f;
    }
    programador_["max_activas"] = std::clamp(programador_.value("max_activas", 3), 0, 16);
    programador_["uso_pct"] = std::clamp(programador_.value("uso_pct", 85.0), 10.0, 100.0);
    programador_["transicion_ms"] = std::clamp(programador_.value("transicion_ms", 400), 0, 10000);
    programador_["umbral_aumento"] = std::clamp(programador_.value("umbral_aumento", 1.1), 0.5, 4.0);
    programador_["umbral_pct"] = std::clamp(programador_.value("umbral_pct", 80.0), 30.0, 200.0);
    if (cambios.contains("bypass_auto")) tarde_.clear(); // con el interruptor recién tocado, de cero
    if (programador_.value("capacidad", std::string("auto")) != "fija") programador_["capacidad"] = "auto";
    if (programador_.value("sin_turno", std::string("barato")) != "reposo") programador_["sin_turno"] = "barato";
    QSaveFile f(QString::fromStdString((archivo_.parent_path() / "superres-programador.json").string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromStdString(programador_.dump(1)));
        f.commit();
    }
    modoSr_.clear(); // que vuelva a mandar todo con lo nuevo
    evaluarSuperres();
    return {};
}

QVariantMap VideoGrafos::programadorUi() const { return aVariant(programador()).toMap(); }

QString VideoGrafos::configurarProgramadorUi(const QVariantMap& cambios) {
    const json j = json::parse(QJsonDocument::fromVariant(cambios).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return q(configurarProgramador(j.is_object() ? j : json::object()));
}

std::string VideoGrafos::filtrosAudio(const std::string& entrada, const json& filtros) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return "«" + entrada + "» no es una entrada de GStreamer (red, pipeline o plugin)";
    if (!filtros.is_array()) return "`filtros` tiene que ser una lista de {elemento, propiedades}";
    json limpios = json::array();
    int i = 0;
    for (const auto& f : filtros) {
        const std::string el = f.is_object() ? f.value("elemento", std::string()) : std::string();
        if (el.empty()) return "a un filtro le falta `elemento`";
        if (!hayElemento(el.c_str())) return "GStreamer no tiene el elemento «" + el + "»";
        json x = {{"id", f.value("id", "a" + std::to_string(i++))}, {"elemento", el}};
        json pr = json::object();
        for (const auto obj_ = f.value("propiedades", json::object()); const auto& [k, v] : obj_.items())
            pr[k] = v.is_string() ? v.get<std::string>() : v.dump();
        x["propiedades"] = pr;
        limpios.push_back(x);
    }
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return "no existe «" + entrada + "»";
    obs_data_t* d = obs_data_create();
    obs_data_set_string(d, "filtros_audio", limpios.empty() ? "" : limpios.dump().c_str());
    actualizarFuente(src, d);
    obs_data_release(d);
    obs_source_release(src);
    rt_.save();
    return {};
}

json VideoGrafos::filtrosAudioDe(const std::string& entrada) const {
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return json::array();
    obs_data_t* d = obs_source_get_settings(src);
    const json j = json::parse(obs_data_get_string(d, "filtros_audio"), nullptr, false);
    obs_data_release(d);
    obs_source_release(src);
    return j.is_array() ? j : json::array();
}

namespace {
/// Ajuste de la mesa (MCP/interfaz) → propiedad de baautomixer.
const std::map<std::string, std::string> kAjustesMesa{{"umbral_db", "umbral-db"},     {"hold_ms", "hold-ms"},
                                                      {"piso_hold_db", "piso-hold-db"}, {"max_abiertos", "max-abiertos"},
                                                      {"ataque_ms", "ataque-ms"},     {"liberacion_ms", "liberacion-ms"}};
} // namespace

json VideoGrafos::estudioLatencia(int tope) {
    calldata_t cd = {};
    calldata_set_int(&cd, "tope_ms", tope);
    json out{{"latencia_ms", 0}, {"tope_ms", 0}, {"automatico", true}, {"tarde", json::array()}};
    if (proc_handler_call(obs_get_proc_handler(), "ba_estudio_latencia", &cd)) {
        out["latencia_ms"] = calldata_int(&cd, "latencia_ms");
        out["tope_ms"] = calldata_int(&cd, "tope_actual_ms");
        out["automatico"] = calldata_bool(&cd, "automatico");
        const char* t = calldata_string(&cd, "tarde");
        const json tj = json::parse(t != nullptr ? t : "[]", nullptr, false);
        out["tarde"] = tj.is_array() ? tj : json::array();
    }
    calldata_free(&cd);
    if (tope >= -1) {
        rt_.config().estudioTopeMs = tope;
        rt_.save();
    }
    return out;
}

QVariantMap VideoGrafos::estudioLatenciaUi() { return aVariant(estudioLatencia()).toMap(); }
void VideoGrafos::ponerTopeEstudioUi(int tope) { estudioLatencia(tope); }

std::string VideoGrafos::automixer(const std::string& entrada, const std::string& grupo) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return "«" + entrada + "» no es una entrada de GStreamer (los micrófonos del equipo lo son)";
    json l = json::array();
    json propsMesa = json::object();
    for (const auto& f : filtrosAudioDe(entrada))
        if (f.value("elemento", std::string()) != "baautomixer") l.push_back(f);
    if (!grupo.empty()) {
        // Si la mesa ya tiene micrófonos, el nuevo entra con sus ajustes.
        for (const auto& m : automixerEstado())
            if (m.value("grupo", std::string()) == grupo)
                for (const auto& [k, prop] : kAjustesMesa)
                    if (m["ajustes"].contains(k)) propsMesa[prop] = m["ajustes"][k];
        propsMesa["grupo"] = grupo;
        l.push_back({{"id", "automixer"}, {"elemento", "baautomixer"}, {"propiedades", propsMesa}});
    }
    return filtrosAudio(entrada, l);
}

std::string VideoGrafos::automixerAjustes(const std::string& grupo, const json& ajustes) {
    if (!ajustes.is_object() || ajustes.empty()) return "faltan los ajustes";
    for (const auto& [k, v] : ajustes.items())
        if (!kAjustesMesa.count(k) || !v.is_number())
            return "ajuste desconocido «" + k + "» (umbral_db, hold_ms, piso_hold_db, max_abiertos, ataque_ms, liberacion_ms)";
    int n = 0;
    for (const std::string& nombre : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(nombre) != kTipoVirtual) continue;
        json l = filtrosAudioDe(nombre);
        bool toca = false;
        for (auto& f : l)
            if (f.value("elemento", std::string()) == "baautomixer" &&
                f.value("propiedades", json::object()).value("grupo", std::string("mesa")) == grupo) {
                for (const auto& [k, v] : ajustes.items())
                    f["propiedades"][kAjustesMesa.at(k)] = k == "max_abiertos" ? json(v.get<int>()) : json(v.get<double>());
                toca = true;
            }
        if (!toca) continue;
        if (const std::string e = filtrosAudio(nombre, l); !e.empty()) return e;
        ++n;
    }
    return n > 0 ? std::string() : "la mesa «" + grupo + "» no tiene micrófonos";
}

json VideoGrafos::automixerEstado() const {
    std::map<std::string, json> mesas;
    for (const std::string& nombre : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(nombre) != kTipoVirtual) continue;
        for (const auto& f : filtrosAudioDe(nombre)) {
            if (f.value("elemento", std::string()) != "baautomixer") continue;
            const json pr = f.value("propiedades", json::object());
            const std::string g = pr.value("grupo", std::string("mesa"));
            json& mesa = mesas[g];
            if (mesa.is_null()) mesa = {{"grupo", g}, {"ajustes", json::object()}, {"microfonos", json::array()}};
            for (const auto& [k, prop] : kAjustesMesa)
                if (pr.contains(prop)) {
                    const json& v = pr[prop];
                    mesa["ajustes"][k] = v.is_string() ? json::parse(v.get<std::string>(), nullptr, false) : v;
                }
            json m{{"nombre", nombre}};
            // Lo que informa el elemento andando (canal, ganancia-db, nivel-db).
            for (const auto& vivo : filtrosDeEntrada(nombre))
                if (vivo.value("elemento", std::string()) == "baautomixer") {
                    m["andando"] = vivo.value("andando", false);
                    const json info = vivo.value("info", json::object());
                    if (info.contains("canal")) m["canal"] = info["canal"];
                    if (info.contains("ganancia-db")) m["ganancia_db"] = std::round(info["ganancia-db"].get<double>() * 10) / 10;
                    if (info.contains("nivel-db")) m["nivel_db"] = std::round(info["nivel-db"].get<double>() * 10) / 10;
                }
            mesa["microfonos"].push_back(m);
        }
    }
    json out = json::array();
    for (auto& [g, m] : mesas) out.push_back(m);
    return out;
}

QVariantMap VideoGrafos::automixerUi() const {
    json mics = json::array();
    for (const auto& m : rt_.config().audioInputs)
        if (m.id == kTipoVirtual) mics.push_back(m.name);
    return aVariant(json{{"mesas", automixerEstado()}, {"microfonos", mics}}).toMap();
}

QString VideoGrafos::automixerPonerUi(const QString& entrada, const QString& grupo) {
    return q(automixer(entrada.toStdString(), grupo.trimmed().toStdString()));
}

QString VideoGrafos::automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes) {
    const json j = json::parse(QJsonDocument::fromVariant(ajustes).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return q(automixerAjustes(grupo.toStdString(), j.is_object() ? j : json::object()));
}

QVariantList VideoGrafos::filtrosAudioUi(const QString& entrada) const {
    return aVariant(filtrosAudioDe(entrada.toStdString())).toList();
}

QString VideoGrafos::agregarFiltroAudioUi(const QString& entrada, const QString& cual) {
    static const std::map<std::string, json> kComunes{
        {"compresor", {{"elemento", "audiodynamic"}, {"propiedades", {{"mode", "compressor"}, {"characteristics", "soft-knee"}, {"ratio", 0.5}, {"threshold", 0.25}}}}},
        {"pasa_altos", {{"elemento", "audiocheblimit"}, {"propiedades", {{"mode", "high-pass"}, {"cutoff", 80.0}, {"poles", 4}}}}},
        {"eco", {{"elemento", "audioecho"}, {"propiedades", {{"delay", 250000000}, {"max-delay", 500000000}, {"intensity", 0.3}, {"feedback", 0.2}}}}},
        {"volumen", {{"elemento", "volume"}, {"propiedades", {{"volume", 1.5}}}}},
        {"automixer", {{"elemento", "baautomixer"}, {"propiedades", {{"grupo", "mesa"}}}}}};
    const auto it = kComunes.find(cual.toStdString());
    if (it == kComunes.end()) return "filtro desconocido";
    json l = filtrosAudioDe(entrada.toStdString());
    json f = it->second;
    f["id"] = cual.toStdString() + std::to_string(l.size());
    l.push_back(f);
    return q(filtrosAudio(entrada.toStdString(), l));
}

QString VideoGrafos::ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros) {
    const json l = json::parse(QJsonDocument::fromVariant(filtros).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return q(filtrosAudio(entrada.toStdString(), l.is_array() ? l : json::array()));
}

QString VideoGrafos::quitarFiltroAudioUi(const QString& entrada, int indice) {
    json l = filtrosAudioDe(entrada.toStdString());
    if (indice < 0 || indice >= static_cast<int>(l.size())) return "no hay ese filtro";
    l.erase(l.begin() + indice);
    return q(filtrosAudio(entrada.toStdString(), l));
}

QString VideoGrafos::superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo) {
    return q(superresMotor(entrada.toStdString(), motor.toStdString(), modelo.toStdString()));
}

namespace {
/// Si GStreamer tiene ese elemento (sin GStreamer en este sistema, ninguno).
bool hayElemento(const char* elemento) {
#if defined(BA_HAVE_GST)
    GstElementFactory* f = gst_element_factory_find(elemento);
    if (f == nullptr) return false;
    gst_object_unref(f);
    return true;
#else
    (void)elemento;
    return false;
#endif
}
} // namespace

QStringList VideoGrafos::motoresSuperres() const {
    QStringList l;
    for (const auto& [motor, elemento] :
         {std::pair{"apple", "bavtsuperres"}, {"fsr", "bafsr"}, {"onnx", "baonnxsr"}, {"coreml", "bacoremlsr"}, {"glsl", "baglsl"}})
        if (hayElemento(elemento)) l << motor;
    return l;
}

bool VideoGrafos::capturarCuadro(const std::string& entrada, QImage& cuadro) const {
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!rt_.engine().captureSource(entrada, 3840, rgba, w, h) || w <= 0 || h <= 0) return false;
    cuadro = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888);
    return !cuadro.isNull();
}

std::string VideoGrafos::uriDe(const std::string& entrada) const {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual) return {};
    obs_source_t* src = obs_get_source_by_name(entrada.c_str());
    if (src == nullptr) return {};
    obs_data_t* d = obs_source_get_settings(src);
    std::string u = obs_data_get_string(d, "uri");
    obs_data_release(d);
    obs_source_release(src);
    return u;
}

bool VideoGrafos::haySuperres() const { return hayElemento("bavtsuperres"); }

QString VideoGrafos::nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena,
                                     int latenciaMs, bool normalizar) {
    return q(crearEntradaRed(nombre.trimmed().toStdString(), uri.trimmed().toStdString(), latenciaMs,
                             escena.trimmed().toStdString(), -1, normalizar ? 1 : 0, -1));
}

QStringList VideoGrafos::escenasUi() const {
    QStringList out;
    for (const auto& s : rt_.engine().sceneNames())
        out << QString::fromStdString(s);
    return out;
}

// --- Desfases --------------------------------------------------------------------------

std::string VideoGrafos::ajustarDesfase(const std::string& entrada, double ms, bool relativo, json* hecho) {
    if (!esEntrada(entrada)) return "«" + entrada + "» no es una entrada de video";
    // Una fuente de GStreamer: su colchón se corre en vivo (para adelante o para atrás).
    if (rt_.engine().sourceTypeId(entrada) == kTipoVirtual) {
        obs_source_t* src = obs_get_source_by_name(entrada.c_str());
        if (src == nullptr) return "no existe «" + entrada + "»";
        obs_data_t* actual = obs_source_get_settings(src);
        const long long antes = obs_data_get_int(actual, "desfase");
        const long long ahora = relativo ? antes + std::llround(ms) : std::llround(ms);
        obs_data_t* d = obs_data_create();
        obs_data_set_int(d, "desfase", ahora);
        actualizarFuente(src, d); // en vivo Y en la configuración guardada (si no, se perdía al reiniciar)
        obs_data_release(d);
        obs_data_release(actual);
        obs_source_release(src);
        rt_.save();
        if (hecho != nullptr)
            *hecho = {{"entrada", entrada},
                      {"como", "desfase de la fuente"},
                      {"antes_ms", antes},
                      {"ahora_ms", ahora}};
        return {};
    }
    // Otra entrada (una cámara): solo se puede ATRASAR, con una tarjeta retardo
    // al principio del sub-grafo (la de sincronía, que se reusa).
    std::string id;
    double antes = 0;
    for (const auto& n : grafo_.nodos())
        if (n.grupo == entrada && n.tipo == "retardo" && n.params.value("sincronia", false)) {
            id = n.id;
            antes = n.params.value("ms", 0);
        }
    const double ahora = relativo ? antes + ms : ms;
    if (ahora < 0)
        return "«" + entrada + "» es una cámara: no se la puede adelantar (" +
               std::to_string(std::lround(ahora)) + " ms). Atrasá la otra entrada.";
    const std::string e = editar([&](GrafoVideo& g) {
        std::string err;
        if (id.empty()) {
            const std::string nuevo = g.agregar("retardo", entrada, GrafoVideo::idEntrada(entrada),
                                                json{{"ms", std::lround(ahora)}, {"sincronia", true}}, &err);
            if (!nuevo.empty()) g.ajustar(nuevo, json{{"etiqueta", "Sincronía (retardo)"}}, nullptr);
        } else {
            g.ajustar(id, json{{"ms", std::lround(ahora)}}, &err);
        }
        return err;
    });
    if (!e.empty()) return e;
    if (hecho != nullptr)
        *hecho = {{"entrada", entrada},
                  {"como", "retardo en el grafo"},
                  {"antes_ms", antes},
                  {"ahora_ms", std::lround(ahora)}};
    return {};
}

void VideoGrafos::medirDesfase(const std::string& a, const std::string& b, int segundos, bool aplicar,
                               std::function<void(json)> listo) {
    for (const std::string& e : {a, b})
        if (!esEntrada(e)) return listo({{"error", "«" + e + "» no es una entrada de video"}});
    if (a == b) return listo({{"error", "son la misma entrada"}});
    // Una tarjeta `sincro` antes de la escena de cada una (mide lo que sale).
    std::string err = editar([&](GrafoVideo& g) {
        std::string e;
        for (const std::string& x : {a, b}) {
            bool hay = false;
            for (const auto& n : g.nodos())
                if (n.grupo == x && n.tipo == "sincro") hay = true;
            if (!hay && g.agregar("sincro", x, "", json{{"auto", true}}, &e).empty()) return e;
        }
        return std::string();
    });
    if (!err.empty()) return listo({{"error", err}});
    const int ms = std::clamp(segundos, 3, 30) * 1000;
    auto inicio = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch());
    auto timer = new QTimer(this);
    timer->setInterval(400);
    QPointer<VideoGrafos> yo = this;
    connect(timer, &QTimer::timeout, this, [this, yo, timer, a, b, ms, inicio, aplicar, listo]() {
        // Pedir las huellas las mantiene tomando; al final se usan.
        const auto pedir = [this](const std::string& e, std::vector<std::uint64_t>* t,
                                  std::vector<std::uint8_t>* g) {
            std::string id;
            for (const auto& n : grafo_.nodos())
                if (n.grupo == e && n.tipo == "sincro") id = n.id;
            obs_source_t* src = obs_get_source_by_name(e.c_str());
            if (src == nullptr || id.empty()) {
                obs_source_release(src);
                return;
            }
            if (obs_source_t* f = obs_source_get_filter_by_name(src, kFiltro)) {
                calldata_t cd = {};
                calldata_set_string(&cd, "nodo", id.c_str());
                calldata_set_ptr(&cd, "horas", t);
                calldata_set_ptr(&cd, "grillas", g);
                proc_handler_call(obs_source_get_proc_handler(f), "sincro", &cd);
                calldata_free(&cd);
                obs_source_release(f);
            }
            obs_source_release(src);
        };
        if (QDateTime::currentMSecsSinceEpoch() - *inicio < ms) {
            pedir(a, nullptr, nullptr);
            pedir(b, nullptr, nullptr);
            return;
        }
        timer->stop();
        timer->deleteLater();
        std::vector<std::uint64_t> ta, tb;
        std::vector<std::uint8_t> ga, gb;
        pedir(a, &ta, &ga);
        pedir(b, &tb, &gb);
        const Medicion m = ::ba::app::video::medirDesfase(ta, ga, tb, gb);
        // Las tarjetas que se pusieron para medir, se van.
        editar([&](GrafoVideo& g) {
            std::vector<std::string> sacar;
            for (const auto& n : g.nodos())
                if (n.tipo == "sincro" && n.params.value("auto", false) && (n.grupo == a || n.grupo == b))
                    sacar.push_back(n.id);
            for (const auto& id : sacar)
                g.quitar(id, nullptr);
            return std::string();
        });
        json r{{"a", a}, {"b", b}, {"cuadros_a", ta.size()}, {"cuadros_b", tb.size()}};
        if (!m.ok) {
            r["error"] = m.motivo;
            r["medido"] = false;
            return listo(r);
        }
        r["medido"] = true;
        r["desfase_ms"] = std::lround(m.desfaseMs);
        const bool alineadas = std::fabs(m.desfaseMs) < m.cuadroMs * 0.5;
        r["significa"] = alineadas ? "están sincronizadas (menos de medio cuadro de diferencia)"
                         : m.desfaseMs > 0
                             ? "«" + b + "» atrasa " + std::to_string(std::lround(m.desfaseMs)) +
                                   " ms respecto de «" + a + "»"
                             : "«" + a + "» atrasa " + std::to_string(std::lround(-m.desfaseMs)) +
                                   " ms respecto de «" + b + "»";
        r["confianza"] = std::round(m.confianza * 100) / 100;
        r["metodo"] = m.metodo;
        r["resolucion_ms"] = std::round(m.cuadroMs);
        if (aplicar && std::fabs(m.desfaseMs) >= m.cuadroMs * 0.5) {
            // Se atrasa la que va adelantada; si es de GStreamer se puede adelantar la otra.
            const std::string adelantada = m.desfaseMs > 0 ? a : b, atrasada = m.desfaseMs > 0 ? b : a;
            const double d = std::fabs(m.desfaseMs);
            json hecho;
            std::string e = ajustarDesfase(adelantada, d, true, &hecho);
            if (!e.empty() && rt_.engine().sourceTypeId(atrasada) == kTipoVirtual)
                e = ajustarDesfase(atrasada, -d, true, &hecho);
            if (e.empty()) {
                r["aplicado"] = hecho;
                // Se vuelve a medir sola: lo que importa es el «después».
                return medirDesfase(a, b, 5, false, [r, listo](json despues) mutable {
                    r["despues"] = {
                        {"desfase_ms", despues.value("desfase_ms", 0)},
                        {"significa", despues.value("significa", despues.value("error", std::string()))},
                        {"confianza", despues.value("confianza", 0.0)}};
                    listo(r);
                });
            }
            r["no_aplicado"] = e;
        }
        listo(r);
    });
    timer->start();
}

json VideoGrafos::entradasDeHost(const std::string& host) const {
    json out = json::array();
    for (const std::string& n : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(n) != kTipoVirtual) continue;
        const json aj = json::parse(rt_.engine().sourceSettingsJson(n), nullptr, false);
        const std::string uri = aj.is_object() ? aj.value("uri", std::string()) : std::string();
        const QUrl u(QString::fromStdString(uri));
        if (!uri.empty() && u.host().toStdString() == host) {
            QUrl sin = u;
            sin.setUserInfo({});
            out.push_back({{"entrada", n}, {"url", sin.toString().toStdString()}});
        }
    }
    return out;
}

std::string VideoGrafos::sumarAEscena(const std::string& nombre, const std::string& escena) {
    if (rt_.engine().sourceTypeId(nombre).empty()) return "no existe «" + nombre + "»";
    if (!asegurarEscena(escena)) return "no se pudo crear la escena «" + escena + "»";
    SourceSpec spec;
    spec.id = rt_.engine().sourceTypeId(nombre);
    spec.name = nombre;
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? "no se pudo" : rt_.lastError();
    rt_.save();
    return {};
}

bool VideoGrafos::asegurarEscena(const std::string& escena) {
    if (escena.empty()) return false;
    for (const auto& n : rt_.engine().sceneNames())
        if (n == escena) return true;
    const bool ok = rt_.addScene(escena);
    if (ok) rt_.save();
    return ok;
}

bool VideoGrafos::mostrarEnEscena(const std::string& nombre, const std::string& escena, bool visible) {
    const bool ok = rt_.setSourceVisible(escena, nombre, visible);
    if (ok) rt_.save();
    return ok;
}

std::string VideoGrafos::armarMosaico(const std::string& escena, const std::vector<std::string>& entradas,
                                      const std::string& disposicion, const std::string& nombre,
                                      bool ocultarSueltas, json* hecho) {
    if (escena.empty()) return "falta `escena`";
    if (!asegurarEscena(escena)) return "no se pudo crear la escena «" + escena + "»";
    if (entradas.size() < 2) return "hacen falta al menos dos entradas";
    for (const auto& e : entradas)
        if (!esEntrada(e)) return "«" + e + "» no es una entrada de video";
    std::string comp;
    const std::string err = editar([&](GrafoVideo& g) {
        std::string e;
        // Si ya hay un compositor con ese nombre, se rearma ese.
        for (const auto& n : g.nodos())
            if (n.tipo == "compositor" && !nombre.empty() &&
                n.params.value("nombre", std::string()) == nombre)
                comp = n.id;
        if (comp.empty()) {
            comp = g.agregar("compositor", "", "", json::object(), &e);
            if (comp.empty()) return e;
        } else {
            std::vector<GrafoVideo::Cable> viejos;
            for (const auto& c : g.cables())
                if (c.hacia == comp) viejos.push_back(c);
            for (const auto& c : viejos)
                g.desconectar(c.desde, c.salida, c.hacia, c.entrada);
        }
        obs_video_info ovi{};
        obs_get_video_info(&ovi);
        json params{{"ancho", ovi.base_width ? ovi.base_width : 1920},
                    {"alto", ovi.base_height ? ovi.base_height : 1080}};
        if (!nombre.empty()) params["nombre"] = nombre;
        if (!g.ajustar(comp, params, &e)) return e;
        if (!nombre.empty()) g.ajustar(comp, json{{"etiqueta", nombre}}, nullptr);
        // De cada entrada, lo que llega a su escena (con sus tarjetas).
        json capas = json::object();
        for (size_t i = 0; i < entradas.size(); ++i) {
            std::string desde = GrafoVideo::idEntrada(entradas[i]);
            for (const auto& c : g.cables())
                if (c.hacia == GrafoVideo::idEscena(entradas[i]) && c.entrada == "in") desde = c.desde;
            const std::string puerto = g.conectar(desde, "out", comp, "", &e);
            if (puerto.empty()) return e;
            json k;
            if (disposicion == "pip") {
                k = i == 0 ? json{{"x", 0}, {"y", 0}, {"ancho", 1}, {"alto", 1}, {"orden", 0}}
                           : json{{"x", 0.68},
                                  {"y", 0.06 + 0.3 * static_cast<double>(i - 1)},
                                  {"ancho", 0.28},
                                  {"alto", 0.28},
                                  {"orden", static_cast<int>(i)}};
            } else if (disposicion == "lado") {
                const double w = 1.0 / static_cast<double>(entradas.size());
                // Cada una en su franja de toda la altura: el encaje la centra.
                k = {{"x", w * static_cast<double>(i)}, {"y", 0}, {"ancho", w}, {"alto", 1}};
            } else if (entradas.size() == 3) {
                // Tres en una grilla de 2x2 dejan un hueco negro: la primera
                // arriba al centro y las otras dos abajo.
                k = i == 0 ? json{{"x", 0.25}, {"y", 0}, {"ancho", 0.5}, {"alto", 0.5}}
                           : json{{"x", 0.5 * static_cast<double>(i - 1)},
                                  {"y", 0.5},
                                  {"ancho", 0.5},
                                  {"alto", 0.5}};
            } else {
                k = nullptr; // mosaico: sin posición, la grilla
            }
            capas[puerto] = k;
        }
        g.ajustar(comp, json{{"capas", capas}}, nullptr);
        return std::string();
    });
    if (!err.empty()) return err;
    const std::string pe = ponerEnEscena(comp, escena);
    if (!pe.empty() && pe.find("ya hay") == std::string::npos) return pe;
    json ocultas = json::array();
    if (ocultarSueltas)
        for (const auto& e : entradas)
            if (mostrarEnEscena(e, escena, false)) ocultas.push_back(e);
    const GrafoVideo::Nodo* n = grafo_.nodo(comp);
    if (hecho != nullptr)
        *hecho = {
            {"compositor", comp},
            {"fuente", n ? n->params.value("nombre", std::string()) : std::string()},
            {"disposicion", disposicion.empty() ? "mosaico" : disposicion},
            {"entradas", entradas},
            {"ocultas", ocultas},
            {"nota",
             "las entradas ocultas siguen alimentando el mosaico (el compositor las mantiene activas)"}};
    return {};
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
    {
        QFile r(QString::fromStdString((archivo_.parent_path() / "video-reloj.json").string()));
        if (r.open(QIODevice::ReadOnly)) {
            const json j = json::parse(r.readAll().toStdString(), nullptr, false);
            if (j.is_object()) reloj_ = j;
        }
    }
    {
        QFile r(QString::fromStdString((archivo_.parent_path() / "superres-programador.json").string()));
        if (r.open(QIODevice::ReadOnly)) {
            const json j = json::parse(r.readAll().toStdString(), nullptr, false);
            if (j.is_object())
                for (const auto& [k, v] : j.items()) programador_[k] = v;
        }
    }
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
