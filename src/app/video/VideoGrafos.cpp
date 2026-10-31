// src/app/video/VideoGrafos.cpp — ver VideoGrafos.hpp

#include "video/VideoGrafos.hpp"
#include "platform/Paths.hpp"

#include "video/Diagnostico.hpp"
#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include "video/Sincronia.hpp"

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18n.hpp"
#include "emision/BroadcastEngine.hpp"
#include "Piezas.hpp" // motor::redElectricaDelSistema, cadenciasDeLaRed
#include "emision/InputSpecs.hpp"

#include "emision/FuentesApp.hpp"
#include "comun/HoraMonotonica.hpp"
#include "Motor.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QImage>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

#include "ajustes/Ajustes.hpp"
#include "platform/Hilo.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

namespace ba::app::video {

using json = nlohmann::json;

namespace {
/// El id de la tarjeta `rostros` de una entrada ("" si no tiene).
std::string tarjetaRostros(const GrafoVideo& g, const std::string& entrada);
constexpr const char* kTipoSalida = "ba_video_salida";
constexpr const char* kTipoVirtual = "ba_gst_fuente";
constexpr const char* kTipoCompositor = "ba_video_compositor";

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
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

/// Las propiedades de un módulo de GStreamer (CatalogoGst) como controles de
/// PropertiesModel.
QVariantList controlesGst(const json& props) {
    QVariantList out;
    for (const auto& p : props) {
        const std::string tipo = p.value("tipo", "");
        QString ayuda = q(p.value("descripcion", ""));
        if (!p.value("enVivo", false))
            ayuda = QCoreApplication::translate("video", "%1 (GStreamer no asegura que cambie con el video andando)")
                        .arg(ayuda);
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
                r["help"] = QCoreApplication::translate("video", "%1 Valores, unidos con «+»: %2").arg(ayuda, nicks.join(", "));
            }
        } else {
            continue;
        }
        out << r;
    }
    return out;
}

/// GStreamer lee los valores como texto: un entero que vino de la interfaz como 3.0 va como 3.
json paraGst(json v) {
    if (v.is_number_float() && v.get<double>() == static_cast<double>(static_cast<long long>(v.get<double>())))
        v = static_cast<long long>(v.get<double>());
    return v;
}

/// Las entradas virtuales: patrones de videotestsrc al tamaño del lienzo.
struct Virtual {
    const char* id;
    const char* nombre;
    const char* descripcion;
    const char* origen; ///< el pipeline, antes de las caps
};
const Virtual kVirtuales[] = {
    {"barras-hd", QT_TRANSLATE_NOOP("video", "Barras HD (SMPTE RP 219)"),
     QT_TRANSLATE_NOOP("video", "Las barras de color de HD: para calibrar monitores y probar la cadena."),
     "videotestsrc pattern=smpte-rp-219 is-live=true"},
    {"barras", QT_TRANSLATE_NOOP("video", "Barras SMPTE (100 %)"), QT_TRANSLATE_NOOP("video", "Las barras clásicas al 100 %."),
     "videotestsrc pattern=smpte100 is-live=true"},
    {"barras-75", QT_TRANSLATE_NOOP("video", "Barras SMPTE (75 %)"),
     QT_TRANSLATE_NOOP("video", "Barras al 75 %: las de ajuste de la TV analógica."),
     "videotestsrc pattern=smpte75 is-live=true"},
    {"barras-hora", QT_TRANSLATE_NOOP("video", "Barras con la hora"),
     QT_TRANSLATE_NOOP("video", "Barras HD con la hora encima: se ve si la señal está viva o congelada."),
     "videotestsrc pattern=smpte-rp-219 is-live=true ! clockoverlay time-format=\"%H:%M:%S\" "
     "font-desc=\"Sans 40\" "
     "halignment=center valignment=bottom"},
    {"negro", QT_TRANSLATE_NOOP("video", "Negro"),
     QT_TRANSLATE_NOOP("video", "Una pantalla negra (para tapar una salida sin sacarla)."),
     "videotestsrc pattern=black is-live=true"},
    {"blanco", QT_TRANSLATE_NOOP("video", "Blanco"), QT_TRANSLATE_NOOP("video", "Una pantalla blanca."),
     "videotestsrc pattern=white is-live=true"},
    {"verde", QT_TRANSLATE_NOOP("video", "Verde croma"), QT_TRANSLATE_NOOP("video", "Verde para probar una llave de croma."),
     "videotestsrc pattern=green is-live=true"},
    {"zona", QT_TRANSLATE_NOOP("video", "Placa de zonas"),
     QT_TRANSLATE_NOOP("video", "Círculos concéntricos (zone plate): muestran el aliasing de un escalado."),
     "videotestsrc pattern=zone-plate kx2=20 ky2=20 kt=1 is-live=true"},
    {"damero", QT_TRANSLATE_NOOP("video", "Damero"), QT_TRANSLATE_NOOP("video", "Cuadros de 8 píxeles: nitidez y escalado."),
     "videotestsrc pattern=checkers-8 is-live=true"},
    {"pelota", QT_TRANSLATE_NOOP("video", "Pelota en movimiento"),
     QT_TRANSLATE_NOOP("video", "Una pelota que se mueve: para ver cuadros perdidos o trabados."),
     "videotestsrc pattern=ball is-live=true"},
    {"ruido", QT_TRANSLATE_NOOP("video", "Ruido"), QT_TRANSLATE_NOOP("video", "Nieve de TV."),
     "videotestsrc pattern=snow is-live=true"},
};

} // namespace


double VideoGrafos::cuadroMsLienzo() const {
    const VideoConfig& v = rt_.engine().videoConfig();
    return v.fpsNum > 0 ? 1000.0 * v.fpsDen / v.fpsNum : 33.3;
}

VideoGrafos::VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), archivo_(dataRoot / "Config" / "video-grafo.json"),
      archivoViejo_(dataRoot / "Config" / "video-grafos.json") {
    cargar();
    // Mejorar imagen o el PTZ digital cambiaron en una entrada: se rearma su cadena ya.
    QPointer<VideoGrafos> self = this;
    fuentes::ponerAvisoExtras([self](const std::string& entrada) {
        QMetaObject::invokeMethod(
            qApp,
            [self, entrada]() {
                if (self && self->entradas_.contains(QString::fromStdString(entrada))) self->aplicar(entrada);
            },
            Qt::QueuedConnection);
    });
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
        ba::platform::nombrarHilo("grafos-catalogo");
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

bool VideoGrafos::tieneSenal(const json& st) {
    const json g = st.value("gstreamer", json::object());
    if (!g.is_object()) return false;
    // Recién rearmada a pedido (se prendió el PTZ, se agregó una tarjeta): la
    // cámara se vuelve a abrir y tarda unos segundos en dar cuadros. No es una
    // caída: 12 s de gracia (una que de verdad se cayó sigue sin señal después).
    const double rearmada = g.value("rearmada_hace_s", -1.0);
    if (rearmada >= 0.0 && rearmada < 12.0) return true;
    const double sin = g.value("sin_datos_s", -1.0);
    return sin >= 0.0 && sin < 1.5;
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
        const json ajustes = json::parse(rt_.engine().sourceSettingsJson(n), nullptr, false);
        const std::string tipo = inputs::tipoLogico(rt_.engine().sourceTypeId(n), ajustes);
        // Los remotos de la asamblea (el que tiene la palabra y lo que comparte)
        // aparte de los invitados de la conferencia.
        const char* gente = inputs::grupoDeGente(tipo, ajustes);
        const QString grupo = tipo == inputs::kVideoType ? QStringLiteral("Cámaras")
                              : gente != nullptr         ? QString::fromUtf8(gente)
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
        if (tipo == "salida" || GrafoVideo::esComposicion(tipo)) {
            const std::string nombre = n["params"].value("nombre", std::string());
            n["publicada"] = !nombre.empty() && rt_.engine().sourceTypeId(nombre) ==
                                                    (tipo == "salida" ? kTipoSalida : kTipoCompositor);
        }
    }
    return d;
}

json VideoGrafos::erroresDe(const std::string& entrada) const {
    json out = json::object();
    // Las tarjetas que quedaron fuera de la cadena de la entrada: el motor
    // todavía no las corre (salidas, tomas, rostros, compositores…).
    const auto it = sinCorrer_.find(entrada);
    if (it != sinCorrer_.end())
        for (const auto& [id, motivo] : it->second) out[id] = motivo;
    // Las que corren en la cadena de GStreamer de la entrada.
    if (enEntrada_.count(entrada) && !enEntrada_.at(entrada).empty())
        for (const auto& f : filtrosDeEntrada(entrada)) {
            const std::string id = f.value("id", std::string());
            if (!enEntrada_.at(entrada).count(id)) continue;
            if (!f.value("andando", false))
                out[id] = tr("el elemento %1 no está en la cadena").arg(q(f.value("elemento", std::string()))).toStdString();
            else if (f.contains("error"))
                out[id] = f["error"];
        }
    return out;
}

json VideoGrafos::describir(const std::string& entrada) const {
    if (!entrada.empty() && !esEntrada(entrada))
        return {{"error", tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString()}};
    json d = conPublicadas(grafo_.describir(entrada, !entrada.empty()));
    // Por qué no corre una tarjeta (lo informa el filtro de su entrada).
    std::map<std::string, json> errores;
    // Los del estudio (compositor, Mezcla de GStreamer): el error de su escena interna en el motor.
    for (auto& n : d["nodos"]) {
        const std::string g = n.value("grupo", std::string());
        if (g.empty()) {
            if (GrafoVideo::esComposicion(n.value("tipo", std::string()))) {
                const std::string e = rt_.engine().errorDeCompositor(n.value("params", json::object()).value("nombre", std::string()));
                if (!e.empty()) n["error"] = e;
            }
            continue;
        }
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

std::string VideoGrafos::nombreFiltro(const std::string& filtroId) { return filtroId; }

QVariantList VideoGrafos::filtrosObs() const {
    // Los filtros de OBS se fueron con libobs: las tarjetas de imagen son
    // elementos de GStreamer (modulosGst).
    return {};
}

QVariantList VideoGrafos::propiedades(const QString& idNodo) const {
    QVariantList out;
    const GrafoVideo::Nodo* n = grafo_.nodo(idNodo.toStdString());
    if (n == nullptr) return out;
    // Un controlador muestra los de su molde; una tarjeta, con los que corre.
    const bool ctl = n->tipo == "control";
    const std::string tipo = ctl ? n->params.value("tarjeta", std::string()) : n->tipo;
    const json params = ctl ? n->params.value("params", json::object()) : grafo_.efectivos(*n);
    // Un módulo de GStreamer (una tarjeta o una Mezcla de GStreamer): las de su elemento.
    if (tipo == "gst" || tipo == "mezcla")
        return controlesGst(gst().propiedades(params.value("elemento", std::string()),
                                              params.value("propiedades", json::object())));
    return out; // las tarjetas «obs» ya no corren (no hay libobs)
}

QVariantList VideoGrafos::propiedadesPad(const QString& idNodo, const QString& puerto) const {
    const GrafoVideo::Nodo* n = grafo_.nodo(idNodo.toStdString());
    if (n == nullptr || n->tipo != "mezcla") return {};
    const std::string pu = puerto.toStdString();
    const json pads = n->params.value("pads", json::object());
    return controlesGst(gst().propiedadesPad(n->params.value("elemento", std::string()), GrafoVideo::plantillaDePuerto(*n, pu),
                                             pads.is_object() ? pads.value(pu, json::object()) : json::object()));
}

QString VideoGrafos::ajustarPad(const QString& id, const QString& puerto, const QString& clave, const QVariant& valor) {
    const json v = paraGst(deVariant(valor));
    return q(editar([&](GrafoVideo& g) {
        const GrafoVideo::Nodo* n = g.nodo(id.toStdString());
        if (n == nullptr || n->tipo != "mezcla") return tr("«%1» no es una Mezcla de GStreamer").arg(id).toStdString();
        if (GrafoVideo::plantillaDePuerto(*n, puerto.toStdString()).empty())
            return tr("«%1» no tiene la entrada «%2»").arg(q(n->etiqueta), puerto).toStdString();
        return ajustarEn(g, id.toStdString(), json{{"pads", {{puerto.toStdString(), {{clave.toStdString(), v}}}}}});
    }));
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
    return gst().disponible() ? q(gst().version()) : tr("Sin GStreamer: %1").arg(q(gst().error()));
}

QVariantList VideoGrafos::virtuales() {
    QVariantList out;
    for (const auto& v : kVirtuales)
        out << QVariantMap{{"id", v.id},
                           {"nombre", QCoreApplication::translate("video", v.nombre)},
                           {"descripcion", QCoreApplication::translate("video", v.descripcion)},
                           {"pipeline", q(v.origen)}};
    return out;
}

std::string VideoGrafos::agregarModuloEn(GrafoVideo& g, const std::string& elemento, const std::string& grupo,
                                         const std::string& despues, const json& propiedades,
                                         std::string* error) const {
    // `baplugin`: una tarjeta de un plugin de Python. Lo registra el módulo de
    // video (no está en el catálogo de la app): no se valida acá.
    // Los de superresolución (también del módulo de video): agrandan x2.
    static const std::map<std::string, const char*> kSuperres{
        {"bavtsuperres", QT_TRANSLATE_NOOP("VideoGrafos", "Superresolución · Apple")},
        {"bafsr", QT_TRANSLATE_NOOP("VideoGrafos", "Superresolución · FSR")},
        {"baonnxsr", QT_TRANSLATE_NOOP("VideoGrafos", "Superresolución · red")}};
    const auto sr = kSuperres.find(elemento);
    const json m = elemento == "baplugin"
                       ? json{{"tarjeta", true},
                              {"titulo", tr("Plugin · %1")
                                             .arg(q(propiedades.is_object() ? propiedades.value("tarjeta", std::string())
                                                                            : std::string()))
                                             .toStdString()}}
                   : sr != kSuperres.end()
                       ? json{{"tarjeta", true},
                              {"titulo", QCoreApplication::translate("VideoGrafos", sr->second).toStdString()}}
                                           : gst().modulo(elemento);
    if (m.empty()) {
        if (error)
            *error = gst().disponible() ? tr("no existe el módulo de GStreamer «%1»").arg(q(elemento)).toStdString()
                                        : gst().error();
        return {};
    }
    // Los nuestros que van con una tarjeta propia del grafo (superres, rostros, seguir):
    // pedir el módulo suelto pone esa tarjeta, que lo configura.
    if (const std::string tipo = m.value("comoTarjeta", std::string()); !tipo.empty())
        return g.agregar(tipo, grupo, despues, json::object(), error);
    if (!m.value("tarjeta", false)) {
        if (error)
            *error = tr("«%1» no va como tarjeta de filtro (%2): %3")
                         .arg(q(elemento), q(m.value("categoria", std::string())),
                              q(m.value("uso", m.value("descripcion", std::string()))))
                         .toStdString();
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

std::string VideoGrafos::agregarMezclaEn(GrafoVideo& g, const std::string& elemento, const json& propiedades,
                                         std::string* error) const {
    const json m = gst().modulo(elemento);
    if (m.empty()) {
        if (error)
            *error = gst().disponible() ? tr("no existe el módulo de GStreamer «%1»").arg(q(elemento)).toStdString()
                                        : gst().error();
        return {};
    }
    if (!m.value("mezcla", false)) {
        if (error)
            *error = tr("«%1» no junta varias entradas (%2): va como tarjeta (tipo gst) o no va")
                         .arg(q(elemento), q(m.value("categoria", std::string())))
                         .toStdString();
        return {};
    }
    // Los puertos son los del módulo (sus plantillas de pads de video): se guardan con el
    // nodo, así el grafo los sabe sin preguntarle a GStreamer.
    const std::string id = g.agregar("mezcla", "", "",
                                     json{{"elemento", elemento},
                                          {"propiedades", propiedades.is_object() ? propiedades : json::object()},
                                          {"plantillas", m.value("entradas", json::array())}},
                                     error);
    if (!id.empty()) g.ajustar(id, json{{"etiqueta", m.value("titulo", m.value("nombre", elemento))}}, nullptr);
    return id;
}

QString VideoGrafos::agregarMezcla(const QString& elemento) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        agregarMezclaEn(g, elemento.toStdString(), json::object(), &e);
        return e;
    }));
}

std::string VideoGrafos::agregarSuperres(const std::string& entrada, const std::string& motor,
                                         const std::string& modelo, const std::string& despues) {
    const std::map<std::string, std::string> elementos{
        {"apple", "bavtsuperres"}, {"fsr", "bafsr"}, {"onnx", "baonnxsr"}};
    const auto it = elementos.find(motor);
    if (it == elementos.end()) return tr("motor: apple, fsr u onnx").toStdString();
    if (!motoresSuperres().contains(QString::fromStdString(motor)))
        return tr("este sistema no tiene el motor «%1»").arg(q(motor)).toStdString();
    if (motor == "onnx" && modelo.empty()) return tr("el motor onnx necesita `modelo` (la ruta del .onnx)").toStdString();
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
    if (nombre.empty()) return tr("falta el nombre").toStdString();
    if (!rt_.engine().sourceTypeId(nombre).empty()) return tr("ya hay una fuente que se llama «%1»").arg(q(nombre)).toStdString();
    std::string origen;
    if (preset.rfind("pipeline:", 0) == 0) {
        origen = preset.substr(9);
    } else {
        for (const auto& v : kVirtuales)
            if (preset == v.id) origen = v.origen;
        if (origen.empty()) return tr("no existe la entrada virtual «%1» (ver virtuales)").arg(q(preset)).toStdString();
        // Al tamaño y la cadencia del lienzo.
        const VideoConfig& v = rt_.engine().videoConfig();
        origen += " ! video/x-raw,width=" + std::to_string(v.baseWidth) + ",height=" + std::to_string(v.baseHeight) +
                  ",framerate=" + std::to_string(v.fpsNum) + "/" + std::to_string(v.fpsDen);
    }
    SourceSpec spec;
    spec.id = kTipoVirtual;
    spec.name = nombre;
    json ajustes = relojParaFuente();
    ajustes["pipeline"] = origen;
    ajustes["latencia"] = 0; // generada acá: no hay red que esperar
    spec.settingsJson = ajustes.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? tr("no se pudo crear").toStdString() : rt_.lastError();
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
    const double cuadroMs = cuadroMsLienzo();
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
    // Lo que tarda cada tarjeta que corre en la cadena (lo informa su elemento).
    json out = json::object();
    for (const auto& f : filtrosDeEntrada(entrada))
        if (f.contains("ms") && f["ms"].is_number()) out[f.value("id", std::string())] = f["ms"];
    return out;
}

std::string VideoGrafos::editar(const std::function<std::string(GrafoVideo&)>& f) {
    GrafoVideo antes = grafo_;
    std::string error = f(grafo_);
    // Uno que ya venía repetido (guardado así de antes) no traba los demás cambios.
    if (error.empty() && nombresRepetidos(antes).empty()) error = nombresRepetidos(grafo_);
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

std::string VideoGrafos::nombresRepetidos(const GrafoVideo& g) {
    // Las salidas (y compositores) son fuentes por su nombre, de todo el estudio: dos con
    // el mismo y una le robaba el video a la otra. Las tomas, por entrada.
    std::set<std::string> salidas;
    std::set<std::pair<std::string, std::string>> tomas;
    for (const auto& n : g.nodos()) {
        const std::string nombre = n.params.value("nombre", std::string());
        if (nombre.empty()) continue; // nombrarSalidas le pone uno libre
        if ((n.tipo == "salida" || GrafoVideo::esComposicion(n.tipo)) && !salidas.insert(nombre).second)
            return tr("ya hay una salida que se llama «%1»: cada una con su nombre").arg(q(nombre)).toStdString();
        if (n.tipo == "toma" && !tomas.insert({n.grupo, nombre}).second)
            return tr("esa entrada ya tiene una toma «%1»").arg(q(nombre)).toStdString();
    }
    return {};
}

void VideoGrafos::nombrarSalidas(GrafoVideo& g) const {
    std::set<std::string> usados;
    for (const auto& s : g.salidas())
        usados.insert(s);
    std::vector<std::pair<std::string, std::string>> sinNombre;
    for (const auto& n : g.nodos())
        if ((n.tipo == "salida" || GrafoVideo::esComposicion(n.tipo)) && n.params.value("nombre", std::string()).empty())
            sinNombre.emplace_back(n.id, n.grupo);
        else if (GrafoVideo::esComposicion(n.tipo))
            usados.insert(n.params.value("nombre", std::string()));
    for (const auto& [id, grupo] : sinNombre) {
        // «Cámara 1 · salida», «Cámara 1 · salida 2», «Estudio · compositor», «Mezcla · smpte»…
        // sin chocar con otra fuente.
        const GrafoVideo::Nodo* nd = g.nodo(id);
        const bool comp = GrafoVideo::esComposicion(nd->tipo);
        const std::string base = nd->tipo == "panoramica" ? std::string("Panorámica")
                                 : nd->tipo == "mezcla"   ? "Mezcla · " + nd->params.value("elemento", std::string())
                                 : comp                   ? std::string("Estudio · compositor")
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
    if (nombre.empty()) return tr("No existe el filtro «%1»").arg(filtroId);
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
QString VideoGrafos::reubicar(const QString& id, const QString& despues) {
    return q(editar([&](GrafoVideo& g) {
        std::string e;
        g.reubicar(id.toStdString(), despues.toStdString(), &e);
        return e;
    }));
}

QString VideoGrafos::correr(const QString& id, int paso) {
    return q(editar([&](GrafoVideo& g) {
        // La cadena de la entrada, en orden: de la entrada a la escena.
        const GrafoVideo::Nodo* n = g.nodo(id.toStdString());
        if (n == nullptr) return tr("no existe la tarjeta «%1»").arg(id).toStdString();
        std::vector<std::string> cadena;
        std::string actual = GrafoVideo::idEntrada(n->grupo);
        for (int guarda = 0; guarda < 64; ++guarda) {
            cadena.push_back(actual);
            std::string sig;
            for (const auto& c : g.cables())
                if (c.desde == actual && c.entrada == "in") {
                    const GrafoVideo::Nodo* h = g.nodo(c.hacia);
                    if (h != nullptr && h->grupo == n->grupo && h->tipo != "salida") sig = c.hacia;
                }
            if (sig.empty()) break;
            actual = sig;
        }
        const auto it = std::find(cadena.begin(), cadena.end(), id.toStdString());
        if (it == cadena.end()) return tr("«%1» no está en la cadena de la entrada").arg(id).toStdString();
        const long i = it - cadena.begin();
        std::string e;
        if (paso < 0) {
            if (i <= 1) return tr("ya está primera").toStdString();
            g.reubicar(id.toStdString(), cadena[static_cast<size_t>(i - 2)], &e);
        } else {
            // Después de la siguiente (que no sea la escena).
            if (i + 1 >= static_cast<long>(cadena.size()) || g.nodo(cadena[static_cast<size_t>(i + 1)])->tipo == "escena")
                return tr("ya está última").toStdString();
            g.reubicar(id.toStdString(), cadena[static_cast<size_t>(i + 1)], &e);
        }
        return e;
    }));
}

QVariantMap VideoGrafos::eventosDeEntrada() const {
    QVariantMap out;
    const std::string entrada = entrada_.toStdString();
    if (entrada.empty()) return out;
    for (const auto& f : filtrosDeEntrada(entrada)) {
        if (!f.contains("eventos") || !f["eventos"].is_object()) continue;
        std::string id = f.value("id", std::string());
        // El PTZ va en la cadena con id «ptz»: su tarjeta es la de tipo ptz de la entrada.
        if (id == "ptz")
            for (const auto& n : grafo_.nodos())
                if (n.grupo == entrada && n.tipo == "ptz") id = n.id;
        QVariantList l;
        for (const auto& [k, v] : f["eventos"].items()) {
            const auto dos = k.find(':');
            l << QVariantMap{{"direccion", QString::fromStdString(k.substr(0, dos))},
                             {"evento", QString::fromStdString(dos == std::string::npos ? k : k.substr(dos + 1))},
                             {"n", static_cast<double>(v.value("n", 0ULL))},
                             {"hace_ms", static_cast<double>(v.value("hace_ms", 0LL))}};
        }
        out[QString::fromStdString(id)] = l;
    }
    return out;
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
std::string VideoGrafos::ajustarEn(GrafoVideo& g, const std::string& id, json p) const {
    const GrafoVideo::Nodo* n = g.nodo(id);
    if (n == nullptr) return tr("no existe la tarjeta «%1»").arg(q(id)).toStdString();
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
    // Un módulo de GStreamer (tarjeta o mezcla): una propiedad que el módulo no tiene se rechaza
    // acá, con las que sí tiene. Si no, la cadena del motor no arrancaba y el error que quedaba era
    // un «general stream error» que no decía qué estaba mal.
    if ((n->tipo == "gst" || n->tipo == "mezcla") && p.contains("propiedades") && p["propiedades"].is_object() &&
        n->params.value("elemento", std::string()) != "baplugin") {
        const std::string elemento = n->params.value("elemento", std::string());
        const json lista = gst().propiedades(elemento, json::object());
        if (!lista.empty()) {
            std::set<std::string> tiene;
            for (const auto& x : lista) tiene.insert(x.value("nombre", std::string()));
            for (auto it = p["propiedades"].begin(); it != p["propiedades"].end(); ++it)
                if (!tiene.count(it.key())) {
                    std::string cuales;
                    for (const auto& t : tiene) cuales += (cuales.empty() ? "" : ", ") + t;
                    return tr("«%1» no tiene la propiedad «%2» (tiene: %3)").arg(q(elemento), q(it.key()), q(cuales)).toStdString();
                }
        }
    }
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
            pr["parametros"] = actuales.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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
    const bool esGst = tipo == "gst" || tipo == "mezcla";
    const char* donde = esGst ? "propiedades" : "ajustes";
    if (esGst) {
        v = paraGst(v);
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

std::string VideoGrafos::ponerEnEscena(const std::string& id, const std::string& escena, bool* otraConEseNombre) {
    const GrafoVideo::Nodo* n = grafo_.nodo(id);
    if (n == nullptr || (n->tipo != "salida" && !GrafoVideo::esComposicion(n->tipo)))
        return tr("«%1» no es una salida ni un compositor (o una mezcla) del grafo").arg(q(id)).toStdString();
    const bool comp = GrafoVideo::esComposicion(n->tipo);
    const std::string nombre = n->params.value("nombre", std::string());
    const std::string tipo = rt_.engine().sourceTypeId(nombre);
    if (!tipo.empty() && tipo != (comp ? kTipoCompositor : kTipoSalida)) {
        if (otraConEseNombre != nullptr) *otraConEseNombre = true;
        return tr("ya hay otra fuente que se llama «%1»").arg(q(nombre)).toStdString();
    }
    SourceSpec spec;
    spec.id = comp ? kTipoCompositor : kTipoSalida;
    spec.name = nombre;
    spec.settingsJson = comp ? json{{"nodo", n->id}, {"config", grafo_.configCompositor(n->id).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
                             : json{{"fuente", n->grupo}, {"productor", productorDe(nombre)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const std::string sc = escena.empty() ? rt_.engine().currentScene() : escena;
    if (!rt_.addSource(sc, spec)) return rt_.lastError().empty() ? tr("no se pudo agregar").toStdString() : rt_.lastError();
    rt_.save();
    emit cambio();
    return {};
}

void VideoGrafos::sincronizar() {
    if (rt_.engine().motor() == nullptr) return;
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
    // Los ids de las tarjetas salen del nombre «aplanado» («Ajuste: barras» y «Ajuste —
    // barras» dan lo mismo): el sub-grafo guardado de una entrada que ya no está se
    // descarta si choca con el de una que sí está (si no, las dos comparten tarjetas).
    std::set<std::string> idsVivos;
    for (const QString& e : hay) idsVivos.insert(GrafoVideo::idEntrada(e.toStdString()));
    for (const std::string& e : grafo_.entradas())
        if (!hay.contains(q(e)) && (grafo_.trivial(e) || idsVivos.count(GrafoVideo::idEntrada(e)))) {
            grafo_.quitarEntrada(e);
            nuevos = true;
        }
    for (const QString& e : entradas_) {
        if (grafo_.asegurarEntrada(e.toStdString())) nuevos = true;
        // Toda entrada tiene su PTZ (una tarjeta más, que se mueve donde se quiera).
        if (grafo_.asegurarPtz(e.toStdString())) nuevos = true;
    }
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
    // Y los del estudio (la escena interna de un compositor o una Mezcla de GStreamer).
    for (const auto& n : grafo_.nodos())
        if (n.grupo.empty() && GrafoVideo::esComposicion(n.tipo))
            if (const std::string e = rt_.engine().errorDeCompositor(n.params.value("nombre", std::string())); !e.empty())
                errores["estudio"][n.id] = e;
    const std::string vistos = errores.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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
                estado = f.contains("error") ? f["error"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : f.value("andando", false) ? "andando" : "falta";
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
    if (!rt_.engine().sourceExists(entrada)) return;
    // Cámaras PTZ dice si el PTZ digital está prendido en esta entrada (su filtro «ptz»).
    {
        bool ptz = false;
        for (const auto& f : fuentes::filtrosExtra(entrada)) ptz = ptz || f.value("id", std::string()) == "ptz";
        if (ptz)
            ptzHabilitado_.insert(entrada);
        else
            ptzHabilitado_.erase(entrada);
    }
    json para = grafo_.paraFiltro(entrada);
    // Las tarjetas de imagen corren en la cadena de GStreamer de la entrada.
    json filtros = pasarAEntrada(entrada, para);
    if (!filtros.contains("video")) filtros["video"] = json::array();
    // Lo que agregan otras piezas (mejorar imagen) antes de las tarjetas. El PTZ
    // ya no va acá: es una tarjeta más (ver pasarAEntrada); lo que pide Cámaras
    // PTZ es solo si está prendido.
    json video = json::array();
    filtros.erase("rostros");
    for (const auto& f : fuentes::filtrosExtra(entrada)) {
        if (f.value("id", std::string()) == "ptz") continue;
        video.push_back(f);
    }
    for (const auto& f : filtros["video"]) video.push_back(f);
    // Legalizar para TV: al final de la cadena, después de todas las tarjetas.
    if (legalizar_ != "no") {
        const std::string compuesto = legalizar_ == "pal" ? "1.33" : legalizar_ == "ntsc" ? "1.2" : "0";
        video.push_back({{"id", "legalizar"},
                         {"elemento", "balegal"},
                         {"propiedades", {{"modo", "1"}, {"compuesto", compuesto}}}});
    }
    // La tarjeta retardo: el video de la entrada sale más tarde (el audio no).
    const long retardo = filtros.value("retardo_ms", 0L);
    filtros.erase("retardo_ms");
    {
        const json actual = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
        if (actual.is_object() && actual.value("desfase_video", 0L) != retardo)
            rt_.updateSourceSettings(entrada, json{{"desfase_video", retardo}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    // La tarjeta Normalizar: ajustes de la entrada (sin ella, lo de fábrica).
    {
        const json n = filtros.value("normalizar", json::object());
        filtros.erase("normalizar");
        const json actual = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
        json cambia = json::object();
        for (const char* k : {"color_real", "desentrelazar", "campo"}) {
            const std::string fabrica = std::string(k) == "color_real" ? "" : "auto";
            const std::string quiero = n.value(k, fabrica);
            const std::string hay = actual.is_object() ? actual.value(k, fabrica) : fabrica;
            if (quiero != hay) cambia[k] = quiero;
        }
        if (!cambia.empty()) {
            ba::diag::registro().linea(
                ba::diag::Nivel::Info, "video-grafo", 0,
                ba::i18n::llenar(ba::i18n::traducir("video-grafo", "«%1»: normalizar: %2"),
                                 {entrada, cambia.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)}));
            rt_.updateSourceSettings(entrada, cambia.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
    }
    filtros["video"] = video;
    const std::string js = video.empty() ? std::string() : filtros.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    // Contra lo que la entrada tiene DE VERDAD (no contra lo último que se le mandó):
    // si la fuente se rehízo (se sacó y se volvió a agregar con el mismo nombre),
    // en la próxima revisión recupera sus tarjetas (el PTZ quedaba afuera para siempre).
    {
        const json actual = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
        const json previo = actual.is_object() ? actual.value("filtros", json()) : json();
        const std::string previoJs = previo.is_string() ? previo.get<std::string>() : previo.is_null() ? "" : previo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (previoJs != js) {
            ba::diag::registro().linea(
                ba::diag::Nivel::Info, "video-grafo", 0,
                ba::i18n::llenar(ba::i18n::traducir("video-grafo", "«%1»: filtros en la entrada: %2"), {entrada, js}));
            rt_.updateSourceSettings(entrada, json{{"filtros", js}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
    }
    // Las Salidas de esta entrada: que su escena lea el punto de la cadena del que cuelgan.
    for (const auto& n : grafo_.nodos()) {
        if (n.tipo != "salida" || n.grupo != entrada) continue;
        const std::string nombre = n.params.value("nombre", std::string());
        if (nombre.empty() || rt_.engine().sourceTypeId(nombre) != kTipoSalida) continue;
        const json actual = json::parse(rt_.engine().sourceSettingsJson(nombre), nullptr, false);
        const std::string prod = productorDe(nombre);
        if (!actual.is_object() || actual.value("productor", std::string()) != prod || actual.value("fuente", std::string()) != entrada)
            rt_.updateSourceSettings(nombre, json{{"fuente", entrada}, {"productor", prod}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
    // TRAER: lo que llega a la escena de esta entrada desde afuera (un compositor,
    // o la toma de otra entrada, que esa entrada publica con un basalida).
    std::string traerComp, traerProd;
    for (const auto& n : para["nodos"]) {
        if (n.value("tipo", std::string()) != "traer") continue;
        const json p = n.value("params", json::object());
        if (p.contains("compositor")) {
            if (const GrafoVideo::Nodo* c = grafo_.nodo(p.value("compositor", std::string())))
                traerComp = c->params.value("nombre", std::string());
        } else if (p.contains("toma")) {
            traerProd = p.value("fuente", std::string()) + ":toma:" + p.value("toma", std::string());
        }
    }
    rt_.engine().setTraer(entrada, traerComp, traerProd);
    // Lo que queda del grafo (salvo «in -> out») todavía no corre en el motor.
    std::map<std::string, std::string>& sin = sinCorrer_[entrada];
    sin.clear();
    for (const auto& n : para["nodos"]) {
        const std::string tipo = n.value("tipo", std::string());
        if (tipo == "traer" && (!traerComp.empty() || !traerProd.empty())) continue;
        // Una toma (lo que va a un compositor): el compositor del motor usa la salida de la entrada.
        if (tipo == "in" || tipo == "out" || tipo == "toma") continue;
        sin[n.value("id", std::string())] =
            tipo == "traer"
                ? tr("el motor todavía no lleva imagen a la escena de otra entrada (traer)").toStdString()
                : tr("la tarjeta «%1» todavía no corre en el motor de GStreamer (solo las de la cadena: color, croma, "
                     "recorte, superresolución, módulos)")
                      .arg(q(tipo))
                      .toStdString();
    }
    if (sin.empty()) sinCorrer_.erase(entrada);
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
    json rostros; // la tarjeta Rostros (va al principio de la cadena)
    bool alfa = false;
    double retardoMs = 0;
    // La tarjeta Normalizar no es un elemento: le dice a la entrada la colorimetría
    // de verdad y cómo desentrelazar (ver motor/Entrada, capssetter y deinterlace).
    json normalizar{{"color_real", ""}, {"desentrelazar", "auto"}, {"campo", "auto"}};
    std::set<std::string>& movidas = enEntrada_[entrada];
    movidas.clear();
    auto& nodos = para["nodos"];
    auto& cables = para["cables"];
    while (true) {
        // UNA SALIDA colgada de este punto (la tarjeta Salida, para publicar la
        // imagen de acá con nombre): si la cadena sigue, un basalida en este
        // lugar; si es el final, la escena de la Salida lee la salida de la entrada.
        {
            std::vector<size_t> aSalida, otros;
            for (size_t i = 0; i < cables.size(); ++i) {
                if (cables[i].value("desde", "") != "in") continue;
                const std::string h = cables[i].value("hacia", "");
                const auto n = std::find_if(nodos.begin(), nodos.end(), [&](const json& x) { return x.value("id", "") == h; });
                const bool publica = n != nodos.end() && (n->value("tipo", "") == "salida" ||
                                                          (n->value("tipo", "") == "toma" &&
                                                           n->value("params", json::object()).value("escena", false)));
                (publica ? aSalida : otros).push_back(i);
            }
            if (!aSalida.empty()) {
                bool sigue = false;
                for (size_t i : otros) {
                    const std::string h = cables[i].value("hacia", "");
                    const auto n = std::find_if(nodos.begin(), nodos.end(), [&](const json& x) { return x.value("id", "") == h; });
                    sigue = sigue || (n != nodos.end() && n->value("tipo", "") != "out" && n->value("tipo", "") != "escena");
                }
                std::set<std::string> quitar;
                for (size_t i : aSalida) {
                    const std::string sid = cables[i].value("hacia", "");
                    const auto n = std::find_if(nodos.begin(), nodos.end(), [&](const json& x) { return x.value("id", "") == sid; });
                    const std::string nombre = n->value("params", json::object()).value("nombre", std::string());
                    if (n->value("tipo", "") == "toma") {
                        // Una toma que va a la escena de otra entrada (traer): siempre
                        // se publica acá, con un nombre que la otra entrada sabe armar.
                        video.push_back(json{{"id", sid}, {"elemento", "basalida"},
                                             {"propiedades", {{"productor", entrada + ":toma:" + nombre}}}});
                        quitar.insert(sid);
                        continue;
                    }
                    if (sigue && !nombre.empty()) {
                        const std::string prod = entrada + ":salida:" + nombre;
                        video.push_back(json{{"id", sid}, {"elemento", "basalida"}, {"propiedades", {{"productor", prod}}}});
                        productorSalida_[nombre] = prod;
                        movidas.insert(sid);
                    } else if (!nombre.empty()) {
                        productorSalida_[nombre] = "";
                    }
                    quitar.insert(sid);
                }
                cables.erase(std::remove_if(cables.begin(), cables.end(),
                                            [&](const json& c) { return quitar.count(c.value("hacia", "")) > 0; }),
                             cables.end());
                nodos.erase(std::remove_if(nodos.begin(), nodos.end(),
                                           [&](const json& x) { return quitar.count(x.value("id", "")) > 0; }),
                            nodos.end());
            }
        }
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
        if (tipo == "rostros") {
            // ROSTROS: el elemento barostros, pero AL PRINCIPIO de la cadena (antes
            // del PTZ digital, ver aplicar): mira la toma entera, así sus caras
            // sirven para encuadrar (baptz las sigue) y para el overlay.
            const std::string uso = p.value("uso", std::string());
            json rp = json::object();
            rp["activo"] = p.value("activo", true) ? "true" : "false";
            rp["fps"] = decimal(p.contains("fps") ? p.value("fps", 5.0)
                                                  : 1000.0 / std::max(100, p.value("cada", 200))); // `cada` (ms): las viejas
            // Personas (y el seguimiento por persona) necesitan el vector de cada cara.
            rp["reconocer"] = (p.value("reconocer", false) || !uso.empty()) ? "true" : "false";
            rp["detector"] = p.value("detector", std::string("auto"));
            rp["confianza"] = decimal(p.value("confianza", 0.7));
            rp["marcar"] = p.value("marcar", false) ? "true" : "false";
            rp["estilo"] = p.value("estilo", std::string("recuadro"));
            rp["color"] = std::to_string(static_cast<unsigned long long>(p.value("color", 0xFFFFC83CLL)) & 0xFFFFFFFFULL);
            rp["grosor"] = std::to_string(p.value("grosor", 4));
            rp["texto"] = p.value("texto", std::string());
            rp["texto-posicion"] = p.value("texto_posicion", std::string("arriba"));
            rp["texto-tamano"] = decimal(p.value("texto_tamano", 0.035));
            // Dos tarjetas (la que pone Personas sola y una del usuario): UN elemento.
            // El overlay y los ajustes, de la del usuario; reconocer, si cualquiera lo pide.
            if (!rostros.is_null()) {
                const bool reconocerAntes = rostros["propiedades"].value("reconocer", std::string()) == "true";
                if (p.value("auto", false)) rp = rostros["propiedades"]; // la de antes manda
                if (reconocerAntes) rp["reconocer"] = "true";
                if (!uso.empty()) rp["reconocer"] = "true";
            }
            rostros = json{{"id", id}, {"elemento", "barostros"}, {"propiedades", rp}};
            // En SU lugar de la cadena (la primera que aparece; las demás la ajustan).
            bool ya = false;
            for (auto& v : video)
                if (v.value("elemento", std::string()) == "barostros") {
                    v["propiedades"] = rp;
                    ya = true;
                }
            if (!ya) video.push_back(rostros);
            movidas.insert(id);
            cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
            for (auto& c : cables)
                if (c.value("desde", "") == id) c["desde"] = "in";
            nodos.erase(it);
            continue;
        }
        if (tipo == "seguir") {
            // EL SEGUIMIENTO (baseguir): no toca la imagen; lee las caras de un
            // Rostros que esté antes y manda el encuadre (ver seguir.cpp).
            if (p.value("activo", true)) {
                json sp = json::object();
                sp["objetivo"] = p.value("objetivo", std::string());
                sp["zoom"] = decimal(p.value("zoom", 0.0));
                sp["tamano-cara"] = decimal(p.value("tamano_cara", 0.0));
                sp["aire"] = decimal(p.value("aire", 0.12));
                sp["suavidad"] = decimal(p.value("suavidad", 0.6));
                sp["zona-muerta"] = decimal(p.value("zona_muerta", 0.04));
                sp["perdida-s"] = decimal(p.value("perdida_s", 3.0));
                video.push_back(json{{"id", id}, {"elemento", "baseguir"}, {"propiedades", sp}});
            }
            movidas.insert(id);
            cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
            for (auto& c : cables)
                if (c.value("desde", "") == id) c["desde"] = "in";
            nodos.erase(it);
            continue;
        }
        if (tipo == "ptz") {
            // EL PTZ DIGITAL (baptz) en el lugar de su tarjeta. Su id es «ptz»: Cámaras
            // PTZ lo maneja por nombre (ba_f_ptz). Va siempre que la tarjeta esté
            // activa, prendido o no en Cámaras PTZ: quieto y sin zoom pasa los cuadros
            // sin tocarlos (passthrough), y así prenderlo o apagarlo no rearma la
            // cámara (rearmarla son unos segundos sin cuadros: saltaba la alarma).
            // UNO por entrada: dos tarjetas «ptz» (un grafo viejo que quedó de una fuente
            // borrada con el mismo nombre) daban dos elementos ba_f_ptz y la cadena no
            // enganchaba nunca («could not link ba_f_ptz to ba_f_ptz»).
            bool yaHay = false;
            for (const auto& v : video) yaHay = yaHay || v.value("id", std::string()) == "ptz";
            if (p.value("activo", true) && !yaHay)
                video.push_back(json{{"id", "ptz"}, {"elemento", "baptz"}});
            movidas.insert(id);
            cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
            for (auto& c : cables)
                if (c.value("desde", "") == id) c["desde"] = "in";
            nodos.erase(it);
            continue;
        }
        if (tipo == "normalizar") {
            if (p.value("activo", true)) {
                const std::string md = p.value("matriz_declarada", std::string("709")), mr = p.value("matriz_real", md);
                const std::string rd = p.value("rango_declarado", std::string("limitado")), rr = p.value("rango_real", rd);
                if (md != mr || rd != rr) {
                    // GstVideoColorimetry «rango:matriz:transferencia:primarios», o su nombre.
                    const bool completo = rr == "completo";
                    if (mr == "601") normalizar["color_real"] = completo ? "1:4:5:4" : "2:4:5:4";
                    else if (mr == "2020") normalizar["color_real"] = completo ? "1:6:13:7" : "2:6:13:7";
                    else normalizar["color_real"] = completo ? "1:3:5:1" : "2:3:5:1";
                }
                static const std::map<std::string, std::string> kMetodo{
                    {"descartar", "linear"}, {"retro", "scalerbob"}, {"mezclar", "vfir"},
                    {"lineal", "linear"},    {"yadif", "yadif"},     {"yadif2x", "yadif"}};
                const auto m = kMetodo.find(p.value("desentrelazar", std::string("no")));
                if (m != kMetodo.end()) {
                    normalizar["desentrelazar"] = m->second;
                    normalizar["campo"] = p.value("campo", std::string("arriba")) == "abajo" ? "bff" : "tff";
                }
            }
            movidas.insert(id);
            cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
            for (auto& c : cables)
                if (c.value("desde", "") == id) c["desde"] = "in";
            nodos.erase(it);
            continue;
        }
        // Modo seguro (F1-10): la superresolución no entra a la cadena (pasa sin tocar).
        // La tarjeta queda en el grafo guardado: vuelve con el arranque normal.
        const bool srApagada = tipo == "superres" && ba::fault::delProceso().apagada(ba::fault::Pieza::Superres);
        if (tipo == "retardo" || tipo == "sincro" || srApagada) {
            // No son elementos. Retardo: el motor corre la salida de video de la
            // entrada (desfase_video). Sincro: la medición mira lo que sale de la
            // entrada (medirDesfase).
            if (tipo == "retardo" && p.value("activo", true))
                retardoMs += p.value("ms", 0) > 0 ? static_cast<double>(p.value("ms", 0))
                                                  : p.value("cuadros", 0) * cuadroMsLienzo();
            movidas.insert(id);
            cables.erase(cables.begin() + static_cast<long>(desdeIn[0]));
            for (auto& c : cables)
                if (c.value("desde", "") == id) c["desde"] = "in";
            nodos.erase(it);
            continue;
        }
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
        } else if (tipo == "vista360") {
            // ba360; en la GPU el motor pone su par (ba360gl, ba360d3d12): no baja a la CPU.
            f["elemento"] = "ba360";
            for (const auto pr360 = GrafoVideo::propiedades360(p, 0); const auto& [k, v] : pr360.items())
                pr[k] = v.is_string() ? v.get<std::string>()
                        : v.is_number_integer() ? std::to_string(v.get<long long>())
                                                : decimal(v.get<double>());
        } else if (tipo == "gst") {
            const std::string el = p.value("elemento", std::string());
            if (el.empty()) break;
            f["elemento"] = el;
            for (const auto obj_ = p.value("propiedades", json::object()); const auto& [k, v] : obj_.items())
                pr[k] = v.is_string() ? v.get<std::string>()
                        : v.is_boolean() ? std::string(v.get<bool>() ? "true" : "false")
                        : v.is_number_integer() ? std::to_string(v.get<long long>())
                        : v.is_number() ? decimal(v.get<double>())
                                        : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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
                // Con Vulkan (ncnn), la red float: las NV12 del laboratorio no van en ncnn.
                if (q.value("proveedor", std::string()) == "vulkan" && q.contains("modelo_float")) {
                    q["modelo"] = q["modelo_float"];
                    q["variantes"] = "";
                }
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
            // Lo guardado puede ser relativo a los datos o una ruta vieja de antes de mover la carpeta.
            if (!q.value("modelo", std::string()).empty())
                pr["modelo"] = ba::platform::rutaDeDatos(q.value("modelo", std::string())).string();
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
            break; // lo demás (salidas, tomas, rostros…) todavía no corre en el motor (ver aplicar)
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
    return {{"video", video}, {"alfa", alfa}, {"retardo_ms", std::lround(retardoMs)}, {"normalizar", normalizar}};
}

std::vector<std::pair<int, int>> VideoGrafos::tamanosDeEntradas() const {
    std::vector<std::pair<int, int>> out;
    for (const QString& e : entradas_) {
        int w = 0, h = 0;
        if (rt_.engine().sourceSize(e.toStdString(), w, h) && w > 0 && h > 0) out.push_back({w, h});
    }
    return out;
}

std::string VideoGrafos::productorDe(const std::string& salida) const {
    const auto it = productorSalida_.find(salida);
    return it == productorSalida_.end() ? std::string() : it->second;
}

json VideoGrafos::filtrosDeEntrada(const std::string& entrada) const {
    const json e = rt_.engine().estadoEntrada(entrada);
    return e.is_object() ? e.value("filtros", json::array()) : json::array();
}

namespace {
/// El id de la tarjeta `rostros` de una entrada ("" si no tiene).
std::string tarjetaRostros(const GrafoVideo& g, const std::string& entrada) {
    for (const auto& n : g.nodos())
        if (n.grupo == entrada && n.tipo == "rostros") return n.id;
    return {};
}
} // namespace

std::string VideoGrafos::configurarRostros(const std::map<std::string, std::string>& quiero) {
    return editar([&](GrafoVideo& g) {
        std::string e;
        for (const std::string& entrada : g.entradas()) {
            const std::string id = tarjetaRostros(g, entrada);
            const auto it = quiero.find(entrada);
            if (it != quiero.end()) {
                if (id.empty()) {
                    // Justo después de la entrada: ve la toma entera (antes del PTZ), así
                    // sigue viendo a la gente que sale del encuadre y el seguimiento la
                    // puede ir a buscar. El usuario la mueve donde quiera.
                    const std::string nuevo = g.agregar("rostros", entrada, GrafoVideo::idEntrada(entrada),
                                                        json{{"uso", it->second}, {"auto", true}}, &e);
                    if (nuevo.empty()) return e;
                } else if (g.nodo(id)->params.value("uso", std::string()) != it->second) {
                    g.ajustar(id, json{{"uso", it->second}}, &e);
                }
            } else if (!id.empty()) {
                const GrafoVideo::Nodo* n = g.nodo(id);
                if (n->params.value("auto", false))
                    g.quitar(id, &e);
                else if (!n->params.value("uso", std::string()).empty())
                    g.ajustar(id, json{{"uso", ""}}, &e);
            }
        }
        return std::string();
    });
}



std::string VideoGrafos::configurarSeguir(const std::string& entrada, const std::string& objetivo) {
    return editar([&](GrafoVideo& g) {
        std::string e;
        std::string id, rostros;
        for (const auto& n : g.nodos())
            if (n.grupo == entrada) {
                if (n.tipo == "seguir") id = n.id;
                if (n.tipo == "rostros") rostros = n.id;
            }
        if (id.empty()) {
            if (objetivo.empty()) return std::string();
            // Después de Rostros (lee sus caras); sin Rostros, después de la entrada.
            id = g.agregar("seguir", entrada, rostros.empty() ? GrafoVideo::idEntrada(entrada) : rostros,
                           json{{"objetivo", objetivo}, {"auto", true}}, &e);
            return id.empty() ? e : std::string();
        }
        // Soltar: la que puso la app sola se va; la del usuario queda, sin objetivo.
        if (objetivo.empty() && g.nodo(id)->params.value("auto", false)) {
            g.quitar(id, &e);
            return e;
        }
        g.ajustar(id, json{{"objetivo", objetivo}}, &e);
        return e;
    });
}

void VideoGrafos::actualizarEstudio() {
    // Las fuentes de los compositores (escenas internas del motor), con su
    // configuración al día. En el motor una entrada da cuadros siempre: no hace
    // falta «mostrar» las que alimentan un compositor.
    for (const std::string& nombre : rt_.engine().sourceNames()) {
        if (rt_.engine().sourceTypeId(nombre) != kTipoCompositor) continue;
        const json s = json::parse(rt_.engine().sourceSettingsJson(nombre), nullptr, false);
        if (!s.is_object()) continue;
        const std::string config = grafo_.configCompositor(s.value("nodo", std::string())).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (config != s.value("config", std::string())) rt_.updateSourceSettings(nombre, json{{"config", config}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
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
        return tr("reloj desconocido «%1»: sistema, ntp, ptp o red").arg(q(tipo)).toStdString();
    if ((tipo == "ntp" || tipo == "red") && r.value("servidor", std::string()).empty())
        return tr("con reloj %1 hace falta `servidor`").arg(q(tipo)).toStdString();
    // Publicar el reloj de este aparato (para que otros usen `red` contra él).
    const int publicar = r.value("publicar_puerto", 0);
    if (publicar != reloj_.value("publicar_puerto", 0)) {
        const std::string e = rt_.engine().publicarReloj(publicar);
        if (!e.empty()) return tr("no se pudo publicar el reloj: %1").arg(q(e)).toStdString();
    }
    reloj_ = r;
    QSaveFile f(QString::fromStdString((archivo_.parent_path() / "video-reloj.json").string()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QByteArray::fromStdString(reloj_.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
        f.commit();
    }
    // Todo el estudio corre con UN reloj (el del motor): cambiarlo rearma el motor.
    if (!rt_.engine().ponerReloj(tipo, r.value("servidor", std::string()), r.value("puerto", 0), r.value("dominio", 0)))
        return tr("no se pudo cambiar el reloj: %1").arg(q(rt_.engine().lastError())).toStdString();
    // Las de red, con la latencia del estudio si no tienen una propia.
    if (reloj_.contains("latencia_ms"))
        for (const std::string& n : rt_.engine().sourceNames()) {
            if (rt_.engine().sourceTypeId(n) != kTipoVirtual) continue;
            const json actual = json::parse(rt_.engine().sourceSettingsJson(n), nullptr, false);
            if (!actual.is_object() || actual.value("uri", std::string()).empty() || actual.value("latencia_propia", false))
                continue;
            rt_.updateSourceSettings(n, json{{"latencia", reloj_.value("latencia_ms", 200)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
    rt_.save();
    return {};
}

// --- Estado de una entrada -------------------------------------------------------------

json VideoGrafos::estadoEntrada(const std::string& entrada) const {
    if (!esEntrada(entrada)) return {{"error", tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString()}};
    json out{{"entrada", entrada}, {"tipo", rt_.engine().sourceTypeId(entrada)}};
    const json e = rt_.engine().estadoEntrada(entrada);
    if (!e.is_object()) return out;
    const VideoConfig& v = rt_.engine().videoConfig();
    out["lienzo"] = {{"ancho", v.baseWidth},
                     {"alto", v.baseHeight},
                     {"fps", v.fpsDen > 0 ? static_cast<double>(v.fpsNum) / v.fpsDen : 0.0}};
    // La red eléctrica del país y las cadencias que no parpadean con ella (F2-17): lo que
    // se le ofrece al operador, no se cambia solo.
    {
        const int hz = motor::redElectricaDelSistema();
        json cad = json::array();
        for (const auto& [n, d] : motor::cadenciasDeLaRed(hz)) cad.push_back(textoDeFps(n, d));
        out["lienzo"]["red_hz"] = hz;
        out["lienzo"]["cadencias_de_la_red"] = cad;
    }
    const json sale = e.value("sale", json::object());
    out["ancho"] = sale.is_object() ? sale.value("ancho", 0) : 0;
    out["alto"] = sale.is_object() ? sale.value("alto", 0) : 0;
    out["activa"] = e.value("anda", false);
    out["mostrandose"] = e.value("anda", false);
    out["ajustes"] = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
    // Todo lo que mide el motor de la entrada.
    out["gstreamer"] = e;
    if (!e.value("error", std::string()).empty()) out["error"] = e["error"];
    out["andando"] = e.value("anda", false);
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
    // Lo que avisa el motor al armar la cadena (elementos o propiedades que no hay).
    json out = json::object();
    const json e = rt_.engine().estadoEntrada(entrada);
    if (e.is_object())
        for (const auto& a : e.value("avisos", json::array()))
            if (a.is_string()) out["cadena"].push_back(a);
    return out;
}

// --- Entradas de red -----------------------------------------------------------------

std::string VideoGrafos::crearEntradaRed(const std::string& nombre, const std::string& uri, int latenciaMs,
                                         const std::string& escena, int audio, int normalizar, int ntpSync,
                                         int superres, const std::string& sincronia) {
    if (!sincronia.empty() && sincronia != "colchon" && sincronia != "gstreamer")
        return tr("`sincronia`: colchon o gstreamer").toStdString();
    if (nombre.empty()) return tr("falta el nombre").toStdString();
    // Si ya existe (de GStreamer), se actualiza: otra URI, otro colchón.
    if (rt_.engine().sourceTypeId(nombre) == kTipoVirtual) {
        json d = json::object();
        if (!uri.empty()) d["uri"] = uri;
        if (latenciaMs >= 0) {
            d["latencia"] = latenciaMs;
            d["latencia_propia"] = true;
        }
        if (audio >= 0) d["audio"] = audio == 1;
        if (normalizar >= 0) d["normalizar"] = normalizar == 1;
        if (ntpSync >= 0) d["ntp_sync"] = ntpSync == 1;
        if (superres >= 0) d["superres"] = superres == 1;
        if (!sincronia.empty()) d["sincronia"] = sincronia;
        // En vivo Y en la configuración guardada (si no, se perdía al reiniciar).
        if (!rt_.updateSourceSettings(nombre, d.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) return rt_.lastError();
        rt_.save();
        return {};
    }
    if (uri.empty()) return tr("falta `uri` (srt://, rtmp://, rtsp://, udp://, http://)").toStdString();
    if (!rt_.engine().sourceTypeId(nombre).empty())
        return tr("ya hay una fuente que se llama «%1» (y no es de red)").arg(q(nombre)).toStdString();
    if (escena.empty()) return tr("falta `escena`: dónde ponerla (para no tocar la que está al aire sin querer)").toStdString();
    EscenaNueva escenaNueva{*this, escena};
    if (!asegurarEscena(escena, &escenaNueva.creada)) return tr("no se pudo crear la escena «%1»").arg(q(escena)).toStdString();
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
    spec.settingsJson = ajustes.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? tr("no se pudo crear").toStdString() : rt_.lastError();
    rt_.save();
    sincronizar();
    return {};
}

std::string VideoGrafos::agregarTarjetaGst(const std::string& entrada, const std::string& elemento,
                                           const json& propiedades, json* hecho) {
    if (!esEntrada(entrada)) return tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString();
    std::string id;
    const std::string e = editar([&](GrafoVideo& g) {
        std::string err;
        // La MISMA tarjeta de un plugin (mismo plugin y tarjeta) en esa entrada se
        // ajusta, no se agrega otra: antes cada plugin_tarjeta (p. ej. para cambiar
        // un parámetro) sumaba una copia más a la cadena.
        if (elemento == "baplugin" && propiedades.is_object())
            for (const auto& n : g.nodos()) {
                if (n.grupo != entrada || n.tipo != "gst" || n.params.value("elemento", std::string()) != "baplugin") continue;
                const json pr = n.params.value("propiedades", json::object());
                if (!pr.is_object() || pr.value("plugin", std::string()) != propiedades.value("plugin", std::string()) ||
                    pr.value("tarjeta", std::string()) != propiedades.value("tarjeta", std::string()))
                    continue;
                id = n.id;
                g.ajustar(n.id, json{{"propiedades", propiedades}}, &err);
                return err;
            }
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
    if (nombre.empty()) return tr("falta el nombre").toStdString();
    // Una cámara del equipo (`camara` en extra) no necesita pipeline: la abre el motor.
    const bool conCamara = extra.is_object() && extra.contains("camara");
    if (video.empty() && audio.empty() && !conCamara) return tr("falta el pipeline (de video y/o de audio)").toStdString();
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
        if (!rt_.updateSourceSettings(nombre, ajustes.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) return rt_.lastError();
        rt_.save();
        return {};
    }
    if (!rt_.engine().sourceTypeId(nombre).empty())
        return tr("ya hay una fuente que se llama «%1» (y no es de GStreamer)").arg(q(nombre)).toStdString();
    if (escena.empty()) return tr("falta `escena`: dónde ponerla (para no tocar la que está al aire sin querer)").toStdString();
    EscenaNueva escenaNueva{*this, escena};
    if (!asegurarEscena(escena, &escenaNueva.creada)) return tr("no se pudo crear la escena «%1»").arg(q(escena)).toStdString();
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
    spec.settingsJson = todo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? tr("no se pudo crear").toStdString() : rt_.lastError();
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
    if (activa && !haySuperres()) return tr("Este sistema no tiene superresolución (necesita macOS 26).");
    return q(crearEntradaRed(entrada.toStdString(), {}, -1, {}, -1, -1, -1, activa ? 1 : 0));
}

QString VideoGrafos::sincroniaUi(const QString& entrada, const QString& modo) {
    return q(crearEntradaRed(entrada.toStdString(), {}, -1, {}, -1, -1, -1, -1, modo.toStdString()));
}

std::string VideoGrafos::superresMotor(const std::string& entrada, const std::string& motor,
                                       const std::string& modelo) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return tr("«%1» no es una entrada de red").arg(q(entrada)).toStdString();
    if (!motor.empty() && motor != "apple" && motor != "fsr" && motor != "onnx")
        return tr("motor: apple, fsr u onnx (o vacío para apagar)").toStdString();
    const QStringList hay = motoresSuperres();
    if (!motor.empty() && !hay.contains(QString::fromStdString(motor)))
        return tr("este sistema no tiene el motor «%1»").arg(q(motor)).toStdString();
    if (motor == "onnx" && modelo.empty()) return tr("el motor onnx necesita `modelo` (la ruta del .onnx)").toStdString();
    // El interruptor viejo (superres) lo manda el motor.
    if (!rt_.updateSourceSettings(entrada, json{{"superres", false},
                                                {"superres_motor", motor},
                                                {"superres_modelo", motor == "onnx" ? modelo : std::string()}}
                                               .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        return rt_.lastError();
    rt_.save();
    return {};
}

namespace {
bool hayElemento(const char* elemento);
} // namespace

std::string VideoGrafos::controlEntrada(const std::string& entrada, const json& control) {
    if (!rt_.engine().sourceExists(entrada)) return tr("no existe «%1»").arg(q(entrada)).toStdString();
    if (!control.is_object() || control.empty()) return tr("`control` tiene que ser un objeto con campos").toStdString();
    return rt_.engine().controlEntrada(entrada, control);
}


void VideoGrafos::evaluarSuperres() {
    if (rt_.engine().motor() == nullptr) return;
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
    const double cuadroMs = cuadroMsLienzo();
    const double lienzoAncho = rt_.engine().videoConfig().baseWidth;

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
        // Lo que se ve de cada entrada en la escena: el ancho mayor en pantalla (px del lienzo).
        std::map<std::string, double> anchos;
        if (escena.empty()) return anchos;
        for (const auto& [fuente, rect] : rt_.engine().itemsVisibles(escena)) {
            const std::string e = entradaDe(fuente);
            if (!e.empty()) anchos[e] = std::max(anchos[e], static_cast<double>(rect.w) * lienzoAncho);
        }
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
        int sw = 0, sh = 0;
        rt_.engine().sourceSize(e, sw, sh);
        const double ancho = sw;
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
        // Por qué queda como queda, en palabras (lo muestra la tarjeta): antes
        // quedaba en reposo o barata sin decir nada y parecía que no andaba.
        char ax[32];
        std::snprintf(ax, sizeof ax, "%.2f", aumento);
        if (!activo) {
            estado[e]["motivo"] = tr("el programador está apagado: trabaja siempre").toStdString();
            continue;
        }
        if (forzado == "siempre") {
            candidatos.push_back({e, 100.0}); // primero, consuma lo que consuma
            estado[e]["motivo"] = tr("forzada a trabajar siempre").toStdString();
        } else if (forzado != "nunca" && seVe && aumento > umbral) {
            double pr = (ap > 0 ? 2.0 : 0.0) + (av > 0 ? 1.0 : 0.0) + std::min(aumento, 4.0) / 4.0;
            if (modoSr_.count(e) && modoSr_[e] == "activo") pr += 0.05; // estabilidad
            candidatos.push_back({e, pr});
            estado[e]["motivo"] =
                (ap > 0 ? tr("al aire, agrandada x%1") : tr("en preview, agrandada x%1")).arg(q(ax)).toStdString();
        } else if (forzado == "nunca") {
            if (seVe && aumento > umbral) quiero[e] = sinTurno;
            estado[e]["motivo"] = tr("forzada a no trabajar").toStdString();
        } else if (!seVe) {
            estado[e]["motivo"] =
                tr("no está al aire ni en preview: se prende sola cuando se vea (o elegí «Siempre»)").toStdString();
        } else {
            estado[e]["motivo"] = tr("en pantalla casi no se agranda (x%1): no hace falta").arg(q(ax)).toStdString();
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
            // Sin medición en vivo todavía: lo que midió el banco de pruebas para ese
            // plugin a ese tamaño; si nunca se midió, lo de una red compacta en la Mac.
            double ms = med.first;
            if (ms <= 0 && estimadorSr_) {
                std::string plugin;
                for (const auto& n : grafo_.nodos())
                    if (n.grupo == c.entrada && n.tipo == "superres") plugin = n.params.value("plugin", std::string());
                int w = 0, h = 0;
                rt_.engine().sourceSize(c.entrada, w, h);
                if (!plugin.empty()) ms = estimadorSr_(plugin, w, h);
            }
            if (ms <= 0) ms = 35.0;
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
                estado[c.entrada]["motivo"] = activas >= tope
                                                  ? tr("ya trabajan %1 (el tope)").arg(tope).toStdString()
                                                  : tr("no le alcanza la placa al lado de las otras").toStdString();
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
        if (t.veces > 0 && ahora - t.ultimaVez > ajustes::entero("superres.tranquilaMin", 10) * 60 * 1000) t.veces = 0; // tranquila: de cero
        if (t.desde != 0 && ahora - t.desde >= ajustes::entero("superres.lentaMs", 2000) && !t.avisado) {
            t.avisado = true;
            t.ultimaVez = ahora;
            if (bypassAuto) {
                ++t.veces;
                t.bypassHasta = ahora + std::min<qint64>(static_cast<qint64>(ajustes::sAMs("superres.baratoS", 15)) << std::min(t.veces - 1, 4),
                                                         static_cast<qint64>(ajustes::sAMs("superres.baratoMaxS", 120)));
            }
            const std::string msTxt = QString::number(ms, 'f', 1).toStdString(),
                              cuadroTxt = QString::number(cuadroMs, 'f', 1).toStdString();
            ba::diag::registro().linea(
                ba::diag::Nivel::Aviso, "video-grafo", 0,
                ba::i18n::llenar(bypassAuto ? ba::i18n::traducir("video-grafo", "superresolución de «%1» no llega: %2 ms "
                                                                                "por cuadro (cuadro %3 ms); pasada a barato")
                                            : ba::i18n::traducir("video-grafo", "superresolución de «%1» no llega: %2 ms "
                                                                                "por cuadro (cuadro %3 ms)"),
                                 {e, msTxt, cuadroTxt}));
            emit superresTarde(QString::fromStdString(e), ms, cuadroMs, bypassAuto);
        }
        if (!lenta && ahora >= t.bypassHasta) t.avisado = false;
        // Forzada a «siempre» no se saca sola aunque no llegue: es para ver de
        // verdad lo que hace (una red de 1 cuadro por segundo se ve a 1 cuadro por
        // segundo). Antes se pasaba a barata a los 2 s y nunca se veía nada.
        const bool siempre = forzar.value(e, std::string("auto")) == "siempre";
        const bool enBypass = bypassAuto && !siempre && ahora < t.bypassHasta;
        if (enBypass && quiero[e] == "activo") quiero[e] = sinTurno;
        if (lenta) {
            estado[e]["motivo"] =
                (enBypass ? tr("no llega: %1 ms por cuadro (un cuadro dura %2 ms); pasada a barata, vuelve a probar sola")
                          : tr("no llega: %1 ms por cuadro (un cuadro dura %2 ms)"))
                    .arg(QString::number(ms, 'f', 0), QString::number(cuadroMs, 'f', 0))
                    .toStdString();
        } else if (enBypass) {
            estado[e]["motivo"] = tr("no llegaba a tiempo: barata un rato, vuelve a probar en %1 s (o elegí «Siempre»)")
                                      .arg((t.bypassHasta - ahora + 999) / 1000)
                                      .toStdString();
        }
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
            ba::diag::registro().linea(
                ba::diag::Nivel::Info, "video-grafo", 0,
                ba::i18n::llenar(ba::i18n::traducir("video-grafo", "superresolución de «%1»: %2"), {e, modo}));
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
    out["cuadro_ms"] = std::round(10.0 * cuadroMsLienzo()) / 10;
    out["programa"] = rt_.engine().currentScene();
    out["preview"] = rt_.engine().scenePreviewName("armada");
    return out;
}

std::string VideoGrafos::configurarProgramador(const json& cambios) {
    if (!cambios.is_object()) return tr("se esperaba un objeto").toStdString();
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
        f.write(QByteArray::fromStdString(programador_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
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
        return tr("«%1» no es una entrada de GStreamer (red, pipeline o plugin)").arg(q(entrada)).toStdString();
    if (!filtros.is_array()) return tr("`filtros` tiene que ser una lista de {elemento, propiedades}").toStdString();
    json limpios = json::array();
    // Los id, únicos (se editan y se nombran por él): uno repetido es un error; los que
    // faltan, «a0», «a1»… sin repetir uno que vino puesto.
    std::set<std::string> ids;
    for (const auto& f : filtros)
        if (f.is_object() && f.contains("id") && f["id"].is_string())
            if (!ids.insert(f["id"].get<std::string>()).second) return tr("dos filtros con el id «%1»").arg(q(f["id"].get<std::string>())).toStdString();
    int i = 0;
    for (const auto& f : filtros) {
        const std::string el = f.is_object() ? f.value("elemento", std::string()) : std::string();
        if (el.empty()) return tr("a un filtro le falta `elemento`").toStdString();
        if (!hayElemento(el.c_str())) return "GStreamer no tiene el elemento «" + el + "»";
        std::string id = f.contains("id") && f["id"].is_string() ? f["id"].get<std::string>() : std::string();
        if (id.empty()) {
            do id = "a" + std::to_string(i++);
            while (ids.count(id) > 0);
            ids.insert(id);
        }
        json x = {{"id", id}, {"elemento", el}};
        json pr = json::object();
        for (const auto obj_ = f.value("propiedades", json::object()); const auto& [k, v] : obj_.items())
            pr[k] = v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        x["propiedades"] = pr;
        limpios.push_back(x);
    }
    if (!rt_.updateSourceSettings(entrada, json{{"filtros_audio", limpios.empty() ? std::string() : limpios.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        return rt_.lastError();
    rt_.save();
    return {};
}

json VideoGrafos::filtrosAudioDe(const std::string& entrada) const {
    const json s = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
    const json f = s.is_object() ? s.value("filtros_audio", json()) : json();
    const json j = f.is_string() ? json::parse(f.get<std::string>(), nullptr, false) : f;
    return j.is_array() ? j : json::array();
}

namespace {
/// Ajuste de la mesa (MCP/interfaz) → propiedad de baautomixer.
const std::map<std::string, std::string> kAjustesMesa{{"umbral_db", "umbral-db"},     {"hold_ms", "hold-ms"},
                                                      {"piso_hold_db", "piso-hold-db"}, {"max_abiertos", "max-abiertos"},
                                                      {"ataque_ms", "ataque-ms"},     {"liberacion_ms", "liberacion-ms"}};
} // namespace

json VideoGrafos::estudioLatencia(int tope) {
    if (tope >= -1) rt_.engine().ponerTopeEstudio(tope);
    json out = rt_.engine().latenciaEstudio();
    if (tope >= -1) {
        rt_.config().estudioTopeMs = tope;
        rt_.save();
    }
    return out;
}

QVariantMap VideoGrafos::estudioLatenciaUi() { return aVariant(estudioLatencia()).toMap(); }
void VideoGrafos::ponerTopeEstudioUi(int tope) { estudioLatencia(tope); }

std::string VideoGrafos::claseSincronia(const std::string& entrada, const std::string& clase) {
    if (rt_.engine().sourceTypeId(entrada).empty()) return tr("no hay una entrada «%1»").arg(q(entrada)).toStdString();
    json v;
    if (clase == "sala") v = true;
    else if (clase == "propia") v = false;
    else if (clase == "auto") v = nullptr; // sin elegir: la de su tipo
    else return tr("clase: sala, propia o auto").toStdString();
    if (!rt_.updateSourceSettings(entrada, json{{"estudio", v}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        return rt_.lastError();
    rt_.save();
    return {};
}

QString VideoGrafos::bajarLatenciaUi() {
    return rt_.engine().bajarLatenciaEstudio() ? QString() : tr("ya está en lo que piden las entradas sincronizadas");
}

std::string VideoGrafos::automixer(const std::string& entrada, const std::string& grupo) {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual)
        return tr("«%1» no es una entrada de GStreamer (los micrófonos del equipo lo son)").arg(q(entrada)).toStdString();
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
    if (!ajustes.is_object() || ajustes.empty()) return tr("faltan los ajustes").toStdString();
    for (const auto& [k, v] : ajustes.items())
        if (!kAjustesMesa.count(k) || !v.is_number())
            return tr("ajuste desconocido «%1» (umbral_db, hold_ms, piso_hold_db, max_abiertos, ataque_ms, liberacion_ms)").arg(q(k)).toStdString();
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
    return n > 0 ? std::string() : tr("la mesa «%1» no tiene micrófonos").arg(q(grupo)).toStdString();
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
    if (it == kComunes.end()) return tr("filtro desconocido");
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
    if (indice < 0 || indice >= static_cast<int>(l.size())) return tr("no hay ese filtro");
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

void VideoGrafos::capturarCuadroAparte(const std::string& entrada, std::function<void(QImage)> listo) const {
    rt_.engine().capturarAparte(
        entrada, 3840, [listo = std::move(listo)](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) mutable {
            QImage cuadro;
            if (e.empty() && w > 0 && h > 0)
                cuadro = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888);
            QMetaObject::invokeMethod(qApp, [listo = std::move(listo), cuadro]() { listo(cuadro); }, Qt::QueuedConnection);
        });
}

std::string VideoGrafos::uriDe(const std::string& entrada) const {
    if (rt_.engine().sourceTypeId(entrada) != kTipoVirtual) return {};
    const json s = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
    return s.is_object() ? s.value("uri", std::string()) : std::string();
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
    if (!esEntrada(entrada)) return tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString();
    // En el motor toda entrada se corre en vivo (para adelante o para atrás): el offset de su salida.
    {
        const json actual = json::parse(rt_.engine().sourceSettingsJson(entrada), nullptr, false);
        const long long antes = actual.is_object() ? actual.value("desfase", 0LL) : 0LL;
        const long long ahora = relativo ? antes + std::llround(ms) : std::llround(ms);
        // En vivo Y en la configuración guardada (si no, se perdía al reiniciar).
        if (!rt_.updateSourceSettings(entrada, json{{"desfase", ahora}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) return rt_.lastError();
        rt_.save();
        if (hecho != nullptr)
            *hecho = {{"entrada", entrada},
                      {"como", tr("desfase de la fuente").toStdString()},
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
        return tr("«%1» es una cámara: no se la puede adelantar (%2 ms). Atrasá la otra entrada.")
            .arg(q(entrada))
            .arg(std::lround(ahora))
            .toStdString();
    const std::string e = editar([&](GrafoVideo& g) {
        std::string err;
        if (id.empty()) {
            const std::string nuevo = g.agregar("retardo", entrada, GrafoVideo::idEntrada(entrada),
                                                json{{"ms", std::lround(ahora)}, {"sincronia", true}}, &err);
            if (!nuevo.empty()) g.ajustar(nuevo, json{{"etiqueta", tr("Sincronía (retardo)").toStdString()}}, nullptr);
        } else {
            g.ajustar(id, json{{"ms", std::lround(ahora)}}, &err);
        }
        return err;
    });
    if (!e.empty()) return e;
    if (hecho != nullptr)
        *hecho = {{"entrada", entrada},
                  {"como", tr("retardo en el grafo").toStdString()},
                  {"antes_ms", antes},
                  {"ahora_ms", std::lround(ahora)}};
    return {};
}

void VideoGrafos::medirDesfase(const std::string& a, const std::string& b, int segundos, bool aplicar,
                               std::function<void(json)> listo) {
    for (const std::string& e : {a, b})
        if (!esEntrada(e)) return listo({{"error", tr("«%1» no es una entrada de video").arg(q(e)).toStdString()}});
    if (a == b) return listo({{"error", tr("son la misma entrada").toStdString()}});
    motor::Motor* mot = rt_.engine().motor();
    if (mot == nullptr) return listo({{"error", tr("el motor no está andando").toStdString()}});
    // La huella de cada cuadro de lo que sale de cada entrada: una grilla de luma
    // de 16x9 (promedios de bloque) con su hora. Una vista chica del motor por entrada (con el reloj del
    // estudio: cada cuadro llega a su hora).
    struct Huellas {
        std::mutex mu;
        std::vector<std::uint64_t> t[2];
        std::vector<std::uint8_t> g[2];
    };
    auto h = std::make_shared<Huellas>();
    const std::string claves[2] = {"entrada:" + a + "@sincro", "entrada:" + b + "@sincro"};
    for (int k = 0; k < 2; ++k) {
        if (!mot->verPedir(claves[k], 160, 90).empty()) {
            if (k == 1) mot->verSoltar(claves[0]);
            return listo({{"error", tr("no se puede mirar «%1»").arg(q(k == 0 ? a : b)).toStdString()}});
        }
        mot->verAvisar(claves[k], [h, k](const motor::Cuadro& c) {
            if (c.ancho < 16 || c.alto < 9) return;
            // El promedio de luma de cada bloque (una pelota chica no se pierde).
            std::uint8_t grilla[16 * 9];
            for (int gy = 0; gy < 9; ++gy)
                for (int gx = 0; gx < 16; ++gx) {
                    const int x0 = gx * c.ancho / 16, x1 = (gx + 1) * c.ancho / 16;
                    const int y0 = gy * c.alto / 9, y1 = (gy + 1) * c.alto / 9;
                    std::uint32_t suma = 0, n = 0;
                    for (int y = y0; y < y1; ++y) {
                        const std::uint8_t* px = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x0) * 4;
                        for (int x = x0; x < x1; ++x, px += 4, ++n) suma += (54u * px[2] + 183u * px[1] + 19u * px[0]) >> 8;
                    }
                    grilla[gy * 16 + gx] = static_cast<std::uint8_t>(n > 0 ? suma / n : 0);
                }
            std::lock_guard<std::mutex> l(h->mu);
            h->t[k].push_back(ba::app::ahoraNs());
            h->g[k].insert(h->g[k].end(), grilla, grilla + 16 * 9);
        });
    }
    const int ms = std::clamp(segundos, 3, 30) * 1000;
    QPointer<VideoGrafos> yo = this;
    QTimer::singleShot(ms, this, [this, yo, a, b, h, aplicar, listo, k0 = claves[0], k1 = claves[1]]() {
        if (motor::Motor* m2 = rt_.engine().motor()) {
            for (const auto& k : {k0, k1}) {
                m2->verAvisar(k, nullptr);
                m2->verSoltar(k);
            }
        }
        std::vector<std::uint64_t> ta, tb;
        std::vector<std::uint8_t> ga, gb;
        {
            std::lock_guard<std::mutex> l(h->mu);
            ta = h->t[0];
            tb = h->t[1];
            ga = h->g[0];
            gb = h->g[1];
        }
        const Medicion m = ::ba::app::video::medirDesfase(ta, ga, tb, gb);
        json r{{"a", a}, {"b", b}, {"cuadros_a", ta.size()}, {"cuadros_b", tb.size()}};
        if (!m.ok) {
            r["error"] = m.motivo;
            r["medido"] = false;
            return listo(r);
        }
        r["medido"] = true;
        r["desfase_ms"] = std::lround(m.desfaseMs);
        const bool alineadas = std::fabs(m.desfaseMs) < m.cuadroMs * 0.5;
        r["significa"] = alineadas ? tr("están sincronizadas (menos de medio cuadro de diferencia)").toStdString()
                         : m.desfaseMs > 0
                             ? tr("«%1» atrasa %2 ms respecto de «%3»")
                                   .arg(q(b))
                                   .arg(std::lround(m.desfaseMs))
                                   .arg(q(a))
                                   .toStdString()
                             : tr("«%1» atrasa %2 ms respecto de «%3»")
                                   .arg(q(a))
                                   .arg(std::lround(-m.desfaseMs))
                                   .arg(q(b))
                                   .toStdString();
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
    if (rt_.engine().sourceTypeId(nombre).empty()) return tr("no existe «%1»").arg(q(nombre)).toStdString();
    EscenaNueva escenaNueva{*this, escena};
    if (!asegurarEscena(escena, &escenaNueva.creada)) return tr("no se pudo crear la escena «%1»").arg(q(escena)).toStdString();
    SourceSpec spec;
    spec.id = rt_.engine().sourceTypeId(nombre);
    spec.name = nombre;
    if (!rt_.addSource(escena, spec)) return rt_.lastError().empty() ? tr("no se pudo").toStdString() : rt_.lastError();
    rt_.save();
    return {};
}

bool VideoGrafos::asegurarEscena(const std::string& escena, bool* creada) {
    if (creada != nullptr) *creada = false;
    if (escena.empty()) return false;
    for (const auto& n : rt_.engine().sceneNames())
        if (n == escena) return true;
    const bool ok = rt_.addScene(escena);
    if (ok) rt_.save();
    if (creada != nullptr) *creada = ok;
    return ok;
}

VideoGrafos::EscenaNueva::~EscenaNueva() {
    // La creó este pedido y no le quedó nada adentro: el pedido falló después.
    if (creada && grafos.rt_.engine().sceneSources(nombre).empty() && grafos.rt_.removeScene(nombre)) grafos.rt_.save();
}

bool VideoGrafos::mostrarEnEscena(const std::string& nombre, const std::string& escena, bool visible) {
    const bool ok = rt_.setSourceVisible(escena, nombre, visible);
    if (ok) rt_.save();
    return ok;
}

json VideoGrafos::paramsPreset(const std::string& preset, int cuantas, double campo) {
    json p = json::object();
    if (preset == "360") {
        // Dos ojos de pez de 190° espalda con espalda; sale la vista (el PTZ).
        for (int i = 1; i <= 2; ++i) {
            const std::string b = "lente" + std::to_string(i) + "_";
            p[b + "tipo"] = "ojo";
            p[b + "campo"] = campo > 0 ? campo : 190.0;
            p[b + "yaw"] = i == 1 ? 0.0 : 180.0;
            for (const char* k : {"pitch", "roll", "k1", "k2"}) p[b + k] = 0.0;
            p[b + "x"] = p[b + "y"] = p[b + "radio"] = 0.5;
        }
        p["ver"] = "vista";
        p["ancho"] = 1920;
        p["alto"] = 1080;
        p["mezcla"] = 5.0;
    } else if (preset == "fila") {
        // Comunes de `campo` grados, una al lado de la otra con un 10% que se pisa; sale la
        // tira con todas, del tamaño de la suma de sus resoluciones.
        const double c = campo > 0 ? campo : 60.0, paso = c * 0.9;
        for (int i = 0; i < cuantas; ++i) {
            const std::string b = "lente" + std::to_string(i + 1) + "_";
            p[b + "tipo"] = "normal";
            p[b + "campo"] = c;
            p[b + "yaw"] = (i - (cuantas - 1) / 2.0) * paso;
            for (const char* k : {"pitch", "roll", "k1", "k2"}) p[b + k] = 0.0;
            p[b + "x"] = p[b + "y"] = 0.5;
        }
        p["ver"] = "tira";
        p["yaw"] = 0.0;
        p["pitch"] = 0.0;
        p["tira_campo"] = std::min(360.0, paso * cuantas);
        p["ancho"] = 0;
        p["alto"] = 0;
        p["mezcla"] = std::min(10.0, c * 0.05);
    }
    return p;
}

std::string VideoGrafos::presetPanoramica(const std::string& id, const std::string& preset, double campo) {
    const GrafoVideo::Nodo* n = grafo_.nodo(id);
    if (n == nullptr || n->tipo != "panoramica") return tr("«%1» no es una Panorámica").arg(q(id)).toStdString();
    if (preset != "360" && preset != "fila") return tr("preset: 360 o fila").toStdString();
    const int cuantas = std::max(preset == "360" ? 2 : 1, grafo_.lentesConectadas(id));
    const json p = paramsPreset(preset, cuantas, campo);
    return editar([&](GrafoVideo& g) {
        std::string e;
        return g.ajustar(id, p, &e) ? std::string() : e;
    });
}

std::string VideoGrafos::armarPanoramica(const std::vector<std::string>& camaras, const std::string& preset, double campo,
                                         const std::string& nombre, const std::string& escena, const json& params,
                                         json* hecho) {
    if (camaras.empty()) return tr("falta `camaras` (de 1 a %1)").arg(kMaxLentes360).toStdString();
    if (static_cast<int>(camaras.size()) > kMaxLentes360)
        return tr("hasta %1 cámaras (una textura no pasa de 16384 de ancho)").arg(kMaxLentes360).toStdString();
    for (size_t i = 0; i < camaras.size(); ++i) {
        if (!esEntrada(camaras[i])) return tr("«%1» no es una entrada de video").arg(q(camaras[i])).toStdString();
        for (size_t j = 0; j < i; ++j)
            if (camaras[i] == camaras[j]) return tr("«%1» está dos veces").arg(q(camaras[i])).toStdString();
    }
    if (preset == "360" && camaras.size() != 2) return tr("el preset 360 es de dos lentes").toStdString();
    std::string id;
    const std::string err = editar([&](GrafoVideo& g) {
        std::string e;
        for (const auto& n : g.nodos())
            if (n.tipo == "panoramica" && !nombre.empty() && n.params.value("nombre", std::string()) == nombre) id = n.id;
        if (id.empty()) {
            id = g.agregar("panoramica", "", "", json::object(), &e);
            if (id.empty()) return e;
        } else {
            std::vector<GrafoVideo::Cable> viejos;
            for (const auto& c : g.cables())
                if (c.hacia == id) viejos.push_back(c);
            for (const auto& c : viejos) g.desconectar(c.desde, c.salida, c.hacia, c.entrada);
        }
        // Primero lo del preset, encima lo que se pidió.
        json p = preset.empty() ? json::object() : paramsPreset(preset, static_cast<int>(camaras.size()), campo);
        if (params.is_object())
            for (auto it = params.begin(); it != params.end(); ++it) p[it.key()] = it.value();
        if (!nombre.empty()) p["nombre"] = nombre;
        if (!p.empty() && !g.ajustar(id, p, &e)) return e;
        if (!nombre.empty()) g.ajustar(id, json{{"etiqueta", nombre}}, nullptr);
        // De cada cámara, lo que llega a su escena (con sus tarjetas): la k-ésima a in_k.
        for (size_t i = 0; i < camaras.size(); ++i) {
            std::string desde = GrafoVideo::idEntrada(camaras[i]);
            for (const auto& c : g.cables())
                if (c.hacia == GrafoVideo::idEscena(camaras[i]) && c.entrada == "in") desde = c.desde;
            if (g.conectar(desde, "out", id, "in_" + std::to_string(i), &e).empty()) return e;
        }
        return std::string();
    });
    if (!err.empty()) return err;
    json ocultas = json::array();
    if (!escena.empty()) {
        EscenaNueva escenaNueva{*this, escena};
        if (!asegurarEscena(escena, &escenaNueva.creada)) return tr("no se pudo crear la escena «%1»").arg(q(escena)).toStdString();
        bool otraConEseNombre = false;
        const std::string pe = ponerEnEscena(id, escena, &otraConEseNombre);
        if (!pe.empty() && !otraConEseNombre && pe.find("ya hay") == std::string::npos) return pe;
        for (const auto& e : camaras)
            if (mostrarEnEscena(e, escena, false)) ocultas.push_back(e);
    }
    const GrafoVideo::Nodo* n = grafo_.nodo(id);
    const json p = n ? n->params : json::object();
    const int la = p.value("lente_ancho", 1920), al = p.value("lente_alto", 1080);
    if (hecho != nullptr)
        *hecho = {{"tarjeta", id},
                  {"fuente", p.value("nombre", std::string())},
                  {"camaras", camaras},
                  {"lienzo", std::to_string(static_cast<int>(camaras.size()) * la) + "x" + std::to_string(al)},
                  {"ocultas", ocultas},
                  {"calibrar",
                   tr("video_tarjeta_ajustar sobre la tarjeta: ver=entrada para encajar centros y círculos; "
                      "ver=tira o panorama con guia=esfera para cómo encajan (lenteN_yaw, _pitch, _roll, _campo) "
                      "hasta que el horizonte quede derecho y sin costuras; guía rectángulo o círculo encima de "
                      "algo que lo sea para la deformación (lenteN_k1, _k2)")
                       .toStdString()}};
    return {};
}

json VideoGrafos::capasDeDisposicion(const std::string& disposicion, const std::vector<std::string>& puertos,
                                     std::string* error) {
    const auto falla = [error](const std::string& m) {
        if (error != nullptr) *error = m;
        return json();
    };
    const std::string d = disposicion.empty() ? std::string("mosaico") : disposicion;
    if (d != "mosaico" && d != "pip" && d != "lado")
        return falla(tr("no conozco la disposición «%1» (mosaico, pip o lado)").arg(q(disposicion)).toStdString());
    if (d != "mosaico" && puertos.size() < 2) return falla(tr("hacen falta al menos dos entradas").toStdString());
    const size_t n = puertos.size();
    json capas = json::object();
    for (size_t i = 0; i < n; ++i) {
        json k;
        if (d == "pip") {
            k = i == 0 ? json{{"x", 0}, {"y", 0}, {"ancho", 1}, {"alto", 1}, {"orden", 0}}
                       : json{{"x", 0.68},
                              {"y", 0.06 + 0.3 * static_cast<double>(i - 1)},
                              {"ancho", 0.28},
                              {"alto", 0.28},
                              {"orden", static_cast<int>(i)}};
        } else if (d == "lado") {
            const double w = 1.0 / static_cast<double>(n);
            // Cada una en su franja de toda la altura: el encaje la centra (con
            // dos de 16:9 queda en y 0,25 y alto 0,5; con otras, sin estirarlas).
            k = {{"x", w * static_cast<double>(i)}, {"y", 0}, {"ancho", w}, {"alto", 1}};
        } else if (n == 3) {
            // Tres en una grilla de 2x2 dejan un hueco negro: la primera
            // arriba al centro y las otras dos abajo.
            k = i == 0 ? json{{"x", 0.25}, {"y", 0}, {"ancho", 0.5}, {"alto", 0.5}}
                       : json{{"x", 0.5 * static_cast<double>(i - 1)}, {"y", 0.5}, {"ancho", 0.5}, {"alto", 0.5}};
        } else {
            k = nullptr; // mosaico: sin posición, la grilla
        }
        capas[puertos[i]] = k;
    }
    return capas;
}

std::string VideoGrafos::disponerEn(GrafoVideo& g, const std::string& comp, const std::string& disposicion,
                                    const std::vector<std::string>& puertos) {
    std::string e;
    const json capas = capasDeDisposicion(disposicion, puertos, &e);
    if (capas.is_null()) return e;
    const GrafoVideo::Nodo* n = g.nodo(comp);
    if (n == nullptr || n->tipo != "compositor") return tr("«%1» no es un compositor").arg(q(comp)).toStdString();
    // Las capas se mezclan clave por clave: primero se borran todas las que
    // había (si no, quedaría el `orden` de un pip o la opacidad de antes).
    json borrar = json::object();
    const json capasAntes = n->params.value("capas", json::object()); // con nombre: .items() de un temporal lo usa destruido
    for (const auto& [puerto, k] : capasAntes.items()) borrar[puerto] = nullptr;
    if (!borrar.empty() && !g.ajustar(comp, json{{"capas", borrar}}, &e)) return e;
    if (!capas.empty() && !g.ajustar(comp, json{{"capas", capas}}, &e)) return e;
    return {};
}

std::string VideoGrafos::aplicarDisposicion(const std::string& compositor, const std::string& disposicion) {
    return editar([&](GrafoVideo& g) {
        const GrafoVideo::Nodo* n = g.nodo(compositor);
        if (n == nullptr || n->tipo != "compositor") return tr("«%1» no es un compositor").arg(q(compositor)).toStdString();
        // Sus entradas conectadas, en el orden de sus puertos (la primera es el fondo del pip).
        std::vector<std::string> puertos;
        for (const auto& p : g.puertosEntrada(*n))
            if (!p.pedido && p.tipo == "video") puertos.push_back(p.id);
        return disponerEn(g, compositor, disposicion, puertos);
    });
}

std::string VideoGrafos::armarMosaico(const std::string& escena, const std::vector<std::string>& entradas,
                                      const std::string& disposicion, const std::string& nombre,
                                      bool ocultarSueltas, json* hecho) {
    if (escena.empty()) return tr("falta `escena`").toStdString();
    EscenaNueva escenaNueva{*this, escena};
    if (!asegurarEscena(escena, &escenaNueva.creada)) return tr("no se pudo crear la escena «%1»").arg(q(escena)).toStdString();
    if (entradas.size() < 2) return tr("hacen falta al menos dos entradas").toStdString();
    for (const auto& e : entradas)
        if (!esEntrada(e)) return tr("«%1» no es una entrada de video").arg(q(e)).toStdString();
    {
        std::string e;
        if (capasDeDisposicion(disposicion, entradas, &e).is_null()) return e;
    }
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
        const VideoConfig& v = rt_.engine().videoConfig();
        json params{{"ancho", v.baseWidth > 0 ? v.baseWidth : 1920}, {"alto", v.baseHeight > 0 ? v.baseHeight : 1080}};
        if (!nombre.empty()) params["nombre"] = nombre;
        if (!g.ajustar(comp, params, &e)) return e;
        if (!nombre.empty()) g.ajustar(comp, json{{"etiqueta", nombre}}, nullptr);
        // De cada entrada, lo que llega a su escena (con sus tarjetas).
        std::vector<std::string> puertos;
        for (const auto& entrada : entradas) {
            std::string desde = GrafoVideo::idEntrada(entrada);
            for (const auto& c : g.cables())
                if (c.hacia == GrafoVideo::idEscena(entrada) && c.entrada == "in") desde = c.desde;
            const std::string puerto = g.conectar(desde, "out", comp, "", &e);
            if (puerto.empty()) return e;
            puertos.push_back(puerto);
        }
        // La misma disposición que el botón de la página Video.
        return disponerEn(g, comp, disposicion, puertos);
    });
    if (!err.empty()) return err;
    bool otraConEseNombre = false;
    const std::string pe = ponerEnEscena(comp, escena, &otraConEseNombre);
    if (!pe.empty() && !otraConEseNombre && pe.find("ya hay") == std::string::npos) return pe;
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
             tr("las entradas ocultas siguen alimentando el mosaico (el compositor las mantiene activas)").toStdString()}};
    return {};
}

void VideoGrafos::guardar() const {
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    QSaveFile f(QString::fromStdString(archivo_.string()));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QByteArray::fromStdString(grafo_.aJson().dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
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
