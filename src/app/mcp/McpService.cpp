// ============================================================================
// src/app/mcp/McpService.cpp — arranque del servidor MCP y herramientas del
// motor: lo general y el estado; las familias (escenas, fuentes, audio, salidas,
// configuracion…), cada una en su archivo (ver mcp/McpService.hpp).
// (Imagenes: McpImageTools.cpp · Sala y oradores: McpRoomTools.cpp ·
//  Interfaz y sistema: McpUiTools.cpp · Ayuda: McpHelp.cpp)
// ============================================================================

#include "McpRenombres.hpp"
#include "arranque/EnergiaAire.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18nTexto.hpp"
#include "diag/Perfil.hpp"
#include "sala/CupoSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/Conmutador.hpp"
#include "mcp/McpService.hpp"
#include "Piezas.hpp"
#include <QRandomGenerator>
#include "agent/AgentViewModel.hpp"
#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"
#include "agent/Voz.hpp"
#include "mcp/McpTraza.hpp"
#include "ui/PersonasViewModel.hpp"
#include <QBuffer>
#include <QDir>
#include <QJsonDocument>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <fstream>
#include "arranque/RuntimeMotor.hpp"
#include "arranque/PlanoControl.hpp"
#include "ajustes/Ajustes.hpp"
#include <QThread>
#include <QThreadPool>
#include "emision/InputSpecs.hpp"
#include "emision/SeguridadAire.hpp"
#include "emision/VentanillaMotor.hpp"
#include "mcp/McpBotoneraTools.hpp"
#include "mcp/McpControlWebTools.hpp"
#include "mcp/McpEventoTools.hpp"
#include "mcp/McpTemaTools.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpPreguntaTools.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "agent/Notificador.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/SystemViewModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/TemaEventoViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/RecursosViewModel.hpp"
#include "control/BusEventos.hpp"
#include "video/ElementosGst.hpp"
#include "ui/SettingsModel.hpp"
#include "mcp/InterfazMcp.hpp"
#include "platform/Hilo.hpp"
#include "ui/AppViewModel.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/EscenasViewModel.hpp"
#include "ui/EstadoAireViewModel.hpp"
#include "ui/SalaViewModel.hpp"
#include "ui/SalidasViewModel.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

std::string avisoReiniciaMotor() {
    return QT_TRANSLATE_NOOP("mcp", "OJO: REINICIA EL MOTOR. Por unos segundos se corta TODO lo que sale (el programa, la "
                              "sala, las pantallas, lo que ven los invitados) y las cámaras se vuelven a abrir. Con una "
                              "transmisión o una grabación andando no se aplica. No lo uses para arreglar una cámara: "
                              "decíselo antes al operador y pedile que confirme.");
}

namespace {

/// Lo que hay que mirar ANTES de preguntarle nada a quien espera.
///
/// POR QUE VA PRIMERO. Si el sistema ya sabe quien es, preguntarle el nombre
/// otra vez es hacerle perder el tiempo a alguien que ya estuvo —y le dice, sin
/// querer, que aca no lo conoce nadie—. Y si casi lo reconoce, lo que falta no
/// es una pregunta: es que se ponga de frente a la camara.
///
/// Vacio si este equipo no tiene reconocimiento: no tiene sentido mandarle al
/// asistente instrucciones sobre herramientas que no estan registradas.
QString reconocerPrimero(const QString& uid) {
    return QString::fromStdString(ba::i18n::llenar(
        ba::i18n::traducir(
            "mcp",
            "ANTES DE PREGUNTARLE NADA, mira `rostro_lobby` y busca su `uid` (%1):\n"
            "- Si viene con `nombre`, YA SABES QUIEN ES: saludala por su nombre, confirmale en UNA "
            "sola pregunta el cargo que figura en su ficha, y no le vuelvas a preguntar lo que ya "
            "esta ahi. Preguntarle el nombre a alguien que el sistema reconoce es hacerle perder el "
            "tiempo.\n"
            "- Si `parecido` esta cerca del corte (0.40 o mas) pero sin `nombre`, casi seguro ES "
            "alguien de la libreta y lo que falta es una cara mejor: deciselo por `chat_send` con "
            "las palabras de `sugerencia` (por ejemplo «mira de frente a la camara»), espera unos "
            "segundos y volve a mirar `rostro_lobby` ANTES de preguntarle quien es.\n"
            "- Si no la reconoce, atendela como siempre.\n\n"),
        {uid.toStdString()}));
}

/// Y lo que hay que hacer al final, para que la proxima vez ya la conozca.
QString guardarLaCara(const QString& uid) {
    return QString::fromStdString(ba::i18n::llenar(
        ba::i18n::traducir("mcp",
                           " Al final, cuando tengas su nombre y su cargo, guardale la cara: mira "
                           "`rostro_lobby` para sacar su `feed` (uid %1) y llama `rostro_guardar {nombre, cargo, "
                           "feed}` —si ya tenia ficha, `rostro_sumar {id, feed}`—. Si `rostro_calidad` dice que "
                           "la cara no sirve, pedile primero lo que diga `sugerencia`. Asi la proxima vez que "
                           "venga la reconoce sola y no hay que preguntarle todo de nuevo."),
        {uid.toStdString()}));
}

} // namespace

// ============================================================================
// Utilidades
// ============================================================================

std::string encodeImage(const QImage& src, const std::string& format, int maxWidth, int quality,
                        std::string& mime) {
    QImage img = src;
    if (maxWidth > 0 && img.width() > maxWidth) img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    const bool png = format == "png";
    if (!png && img.hasAlphaChannel()) img = img.convertToFormat(QImage::Format_RGB32);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    const bool ok = img.save(&buf, png ? "PNG" : "JPEG", png ? -1 : std::clamp(quality, 10, 100));
    mime = png ? "image/png" : "image/jpeg";
    if (!ok || bytes.isEmpty()) { // sin el plugin de JPEG: PNG siempre esta
        bytes.clear();
        buf.seek(0);
        img.save(&buf, "PNG");
        mime = "image/png";
    }
    return bytes.toBase64().toStdString();
}

/// Quién puede recibir una imagen DENTRO de su conversación.
///
/// SOLO EL ROL QUE MIRA. El asistente principal aguanta muy pocas imágenes en
/// su contexto y, pasadas esas, la sesión se cuelga —no da error: se cuelga, en
/// el medio de una transmisión—. Una regla en el prompt («no captures si no te
/// lo piden») no alcanza: se lo vio disparar veinte herramientas de una y meter
/// una captura al final sin que nadie se la pidiera. Entonces la capacidad no
/// existe para él: las capturas le vuelven como un archivo, y para mirarlas
/// está `imagen_preguntar`, que se lo encarga a un agente aparte.
bool puedeVerImagenes(McpCore& core) { return core.contextoEnCurso().rol == "vision"; }

/// Dónde quedan las capturas para que las mire otro. Una por archivo, con un
/// nombre que no se repite. (Declarada en el header: la usa `imagen_preguntar`.)
QString guardarCaptura(const QImage& img, const std::filesystem::path& raiz, const json& args,
                       int defaultWidth) {
    (void)raiz; // las capturas son de la máquina (Runtime/), no de la configuración
    const QString dir = QString::fromStdWString((ba::platform::dataRoot() / "Runtime" / "capturas").wstring());
    QDir().mkpath(dir);
    const int maxW = args.value("maxWidth", defaultWidth);
    QImage e = img;
    if (maxW > 0 && e.width() > maxW) e = e.scaledToWidth(maxW, Qt::SmoothTransformation);
    const QString archivo = dir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "-" +
                            QString::number(QRandomGenerator::global()->bounded(1000, 9999)) + ".jpg";
    return e.save(archivo, "JPG", args.value("quality", 85)) ? archivo : QString();
}

namespace {
/// La imagen EN la conversación: solo para el rol que mira (ver addImage con destino).
void addImageEnLinea(ToolResult& r, const QImage& img, const json& args, int defaultWidth) {
    std::string mime;
    const int maxW = args.value("maxWidth", defaultWidth);
    const std::string b64 =
        encodeImage(img, args.value("format", std::string("jpeg")), maxW, args.value("quality", 85), mime);
    r.image(b64, mime);
}
} // namespace

DestinoImagen destinoImagen(McpCore& core, const std::filesystem::path& raiz) {
    return DestinoImagen{puedeVerImagenes(core), raiz};
}

void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth, McpCore& core,
              const std::filesystem::path& raiz) {
    addImage(r, img, args, defaultWidth, destinoImagen(core, raiz));
}

void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth, const DestinoImagen& destino) {
    if (destino.ver) {
        addImageEnLinea(r, img, args, defaultWidth);
        return;
    }
    const QString archivo = guardarCaptura(img, destino.raiz, args, defaultWidth);
    if (archivo.isEmpty()) {
        r.text(ba::i18n::traducir("mcp", "(no se pudo guardar la captura)"));
        return;
    }
    r.text(ba::i18n::llenar(
        ba::i18n::traducir("mcp", "Captura guardada en %1 (%2x%3). NO te la mando acá: una imagen en esta "
                                  "conversación cuelga la sesión. Para saber qué se ve, usá `imagen_preguntar` "
                                  "con una pregunta concreta."),
        {archivo.toStdString(), std::to_string(img.width()), std::to_string(img.height())}));
    if (r.structured.is_null()) r.structured = json::object();
    json archivos = r.structured.value("capturas", json::array());
    archivos.push_back(archivo.toStdString());
    r.structured["capturas"] = archivos;
}

bool parseLayout(const json& v, float& x, float& y, float& w, float& h, std::string& err, std::string* nota) {
    if (v.is_object()) {
        x = v.value("x", 0.0f);
        y = v.value("y", 0.0f);
        w = v.value("w", 1.0f);
        h = v.value("h", 1.0f);
        // Van en fracciones del lienzo (0..1). Con algún valor mayor que 4 es
        // evidente que vinieron en PÍXELES (un 640 en fracciones manda el cuadro
        // fuera de pantalla y la escena sale negra): se convierten con el lienzo.
        if (std::max({std::abs(x), std::abs(y), w, h}) > 4.0f) {
            int lw = 0, lh = 0;
            if (!lienzoActual(lw, lh)) {
                err = ba::i18n::traducir("mcp", "layout {x,y,w,h} va en fracciones del lienzo (0..1), no en píxeles");
                return false;
            }
            const float bw = static_cast<float>(lw), bh = static_cast<float>(lh);
            if (nota != nullptr)
                *nota = ba::i18n::llenar(ba::i18n::traducir("mcp", "layout en píxeles: convertido a fracciones del "
                                                                   "lienzo de %1x%2 (la próxima, mandá 0..1)"),
                                         {std::to_string(lw), std::to_string(lh)});
            x /= bw;
            w /= bw;
            y /= bh;
            h /= bh;
        }
        if (w <= 0 || h <= 0) {
            err = ba::i18n::traducir("mcp", "w y h deben ser > 0 (fracciones del lienzo, 1 = todo)");
            return false;
        }
        return true;
    }
    if (!v.is_string()) {
        err = ba::i18n::traducir("mcp", "layout debe ser un preset (texto) o {x,y,w,h} en fracciones del lienzo");
        return false;
    }
    const std::string p = v.get<std::string>();
    struct Preset {
        const char* name;
        float x, y, w, h;
    };
    static const Preset kPresets[] = {
        {"full", 0, 0, 1, 1},
        {"left", 0, 0, 0.5f, 1},
        {"right", 0.5f, 0, 0.5f, 1},
        {"top", 0, 0, 1, 0.5f},
        {"bottom", 0, 0.5f, 1, 0.5f},
        {"center", 0.2f, 0.2f, 0.6f, 0.6f},
        {"top-left", 0, 0, 0.5f, 0.5f},
        {"top-right", 0.5f, 0, 0.5f, 0.5f},
        {"bottom-left", 0, 0.5f, 0.5f, 0.5f},
        {"bottom-right", 0.5f, 0.5f, 0.5f, 0.5f},
        {"pip-tl", 0.03f, 0.04f, 0.28f, 0.28f},
        {"pip-tr", 0.69f, 0.04f, 0.28f, 0.28f},
        {"pip-bl", 0.03f, 0.68f, 0.28f, 0.28f},
        {"pip-br", 0.69f, 0.68f, 0.28f, 0.28f},
        {"lower-third", 0.05f, 0.72f, 0.9f, 0.2f},
        {"third-1", 0, 0, 1.0f / 3, 1},
        {"third-2", 1.0f / 3, 0, 1.0f / 3, 1},
        {"third-3", 2.0f / 3, 0, 1.0f / 3, 1},
    };
    for (const Preset& pr : kPresets) {
        if (p == pr.name) {
            x = pr.x;
            y = pr.y;
            w = pr.w;
            h = pr.h;
            return true;
        }
    }
    // grid-<n>-<i>: celda i (1..n) de una grilla de n (2, 3, 4, 6, 9).
    int n = 0;
    int i = 0;
    if (std::sscanf(p.c_str(), "grid-%d-%d", &n, &i) == 2 && n >= 1 && n <= 16 && i >= 1 && i <= n) {
        const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
        const int rows = (n + cols - 1) / cols;
        w = 1.0f / static_cast<float>(cols);
        h = 1.0f / static_cast<float>(rows);
        x = static_cast<float>((i - 1) % cols) * w;
        y = static_cast<float>((i - 1) / cols) * h;
        return true;
    }
    err = ba::i18n::llenar(
        ba::i18n::traducir("mcp", "preset desconocido '%1'. Validos: full, left, right, top, bottom, center, "
                                  "top-left, top-right, bottom-left, bottom-right, pip-tl, pip-tr, pip-bl, pip-br, "
                                  "lower-third, third-1..3, grid-<n>-<i> (ej. grid-4-2), o {x,y,w,h}."),
        {p});
    return false;
}

// Qt <-> json SIN pasar por texto (antes cada conversión escribía el JSON y lo volvía a
// leer: settings_list lo hacía por cada valor de cada fila). Los números como los dejaba
// el texto: el que es entero, entero; el resto, double.
namespace {
json deQJson(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Bool: return v.toBool();
        case QJsonValue::Double: {
            const double d = v.toDouble();
            const qint64 i = v.toInteger();
            if (static_cast<double>(i) == d) return static_cast<std::int64_t>(i);
            return d;
        }
        case QJsonValue::String: return v.toString().toStdString();
        case QJsonValue::Array: {
            json out = json::array();
            for (const QJsonValue& x : v.toArray()) out.push_back(deQJson(x));
            return out;
        }
        case QJsonValue::Object: {
            json out = json::object();
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) out[it.key().toStdString()] = deQJson(it.value());
            return out;
        }
        case QJsonValue::Null:
        case QJsonValue::Undefined: break;
    }
    return nullptr;
}

QJsonValue aQJson(const json& j) {
    switch (j.type()) {
        case json::value_t::boolean: return j.get<bool>();
        case json::value_t::number_integer: return static_cast<qint64>(j.get<std::int64_t>());
        case json::value_t::number_unsigned: return static_cast<qint64>(j.get<std::uint64_t>());
        case json::value_t::number_float: return j.get<double>();
        case json::value_t::string: return QString::fromStdString(j.get<std::string>());
        case json::value_t::array: {
            QJsonArray a;
            for (const json& x : j) a.append(aQJson(x));
            return a;
        }
        case json::value_t::object: {
            QJsonObject o;
            for (auto it = j.begin(); it != j.end(); ++it) o.insert(QString::fromStdString(it.key()), aQJson(it.value()));
            return o;
        }
        default: break;
    }
    return QJsonValue::Null;
}
} // namespace

json toJson(const QJsonObject& o) { return deQJson(o); }
json toJson(const QJsonArray& a) { return deQJson(a); }
QJsonObject toQt(const json& j) { return j.is_object() ? aQJson(j).toObject() : QJsonObject(); }

json qvToJson(const QVariant& v) { return deQJson(QJsonValue::fromVariant(v)); }

QVariant jsonToQv(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return static_cast<qlonglong>(v.get<long long>());
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) return QString::fromStdString(v.get<std::string>());
    return aQJson(v).toVariant();
}

void enPool(QObject* volver, std::function<ToolResult()> trabajo, Done done) {
    static QThreadPool* pool = []() {
        auto* p = new QThreadPool(); // vive lo que el proceso (lo usan herramientas hasta el final)
        p->setObjectName("mcp-aparte");
        p->setMaxThreadCount(2);
        return p;
    }();
    QPointer<QObject> v(volver);
    pool->start([v, trabajo = std::move(trabajo), done = std::move(done)]() mutable {
        ba::platform::nombrarHilo("mcp-aparte");
        ToolResult r;
        try {
            r = trabajo();
        } catch (const std::exception& e) {
            r = ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La herramienta fallo: %1"), {e.what()}));
        }
        if (QObject* o = v.data(); o != nullptr)
            QMetaObject::invokeMethod(o, [done = std::move(done), r = std::move(r)]() { done(r); }, Qt::QueuedConnection);
        else
            done(r);
    });
}

json modelRows(const QAbstractItemModel& m) {
    json out = json::array();
    const QHash<int, QByteArray> roles = m.roleNames();
    for (int r = 0; r < m.rowCount(); ++r) {
        json row = json::object();
        for (auto it = roles.begin(); it != roles.end(); ++it)
            row[it.value().toStdString()] = qvToJson(m.data(m.index(r, 0), it.key()));
        out.push_back(row);
    }
    return out;
}

// ============================================================================
// Servicio
// ============================================================================

McpService::McpService(AppRuntime& rt, InterfazMcp& vm, QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm),
      settings_(std::make_unique<SettingsModel>(rt, vm.ventanilla())) {
    // El cupo en caliente, como en la GUI: sin esto, settings_set con el cupo
    // reiniciaba la sala (y cortaba a todos los invitados).
    settings_->setCupoEnCaliente([this](int n) { return vm_.cupoSala().cambiar(n); });
    settings_->setConfigSala(&vm_.configSala()); // y lo demás de la sala, también en caliente
    registerHelp();
    registerTools();
    registerImageTools();
    registerRoomTools();
    registerPruebaSalaTools();
    registerPruebaAsambleaTools();
    registerEnlaceTools();
    registerCapacidadTools();
    registerUiTools();
    registerPlataformaTools();
    registerAlertTools();
    registerAssistantTools();
    // Lo dictado pasa por el modelo local: necesita saber qué es cada
    // herramienta (título, si pide datos) y poder hacer las que no piden nada.
    if (auto* voz = qobject_cast<agent::Voz*>(vm_.voz()); voz != nullptr) {
        // Una herramienta, por el núcleo, con lo que devuelve como JSON (el
        // estructurado o el texto) y si fue error.
        // Dos sesiones: la que LEE el estado (lee nombres de invitados, y el
        // núcleo la marca contaminada) no es la que ACTÚA. Si fueran la misma,
        // cada acción de voz pediría aprobación a mano. La frase la dijo el
        // operador; los nombres leídos solo se usan para validar.
        const auto llamarEn = [this](const std::string& sesion, const std::string& nombre, const json& args,
                                     std::function<void(bool, const json&, const std::string&)> fin) {
            core_.callTool(
                json{{"name", nombre}, {"arguments", args}},
                [fin](const json& r) {
                    std::string texto;
                    for (const auto& c : r.value("content", json::array()))
                        if (c.value("type", "") == "text") texto += c.value("text", "");
                    json datos = r.value("structuredContent", json());
                    if (datos.is_null()) datos = json::parse(texto, nullptr, false);
                    fin(!r.value("isError", false), datos.is_discarded() ? json() : datos, texto);
                },
                CallContext{sesion, "", "voz"});
        };
        const auto llamar = [llamarEn](const std::string& nombre, const json& args,
                                       std::function<void(bool, const json&, const std::string&)> fin) {
            llamarEn("voz-modelo-local", nombre, args, std::move(fin));
        };
        const auto leer = [llamarEn](const std::string& nombre, const json& args,
                                     std::function<void(bool, const json&, const std::string&)> fin) {
            llamarEn("voz-modelo-local-estado", nombre, args, std::move(fin));
        };
        voz->setHerramientas(
            [this](const QString& nombre) {
                agent::Voz::FichaHerramienta f;
                const Tool* t = core_.findTool(nombre.toStdString());
                if (t == nullptr) return f;
                f.existe = true;
                f.titulo = QString::fromStdString(t->title);
                const json props = t->inputSchema.value("properties", json::object());
                f.requiereDatos = props.is_object() && !props.empty();
                f.lectura = t->readOnly;
                f.ficha = json{
                    {"title", t->title}, {"description", t->description}, {"inputSchema", t->inputSchema}};
                return f;
            },
            [voz, llamar](const QString& nombre, const json& args,
                          std::function<void(bool, const QString&)> fin) {
                llamar(nombre.toStdString(), args, [voz, fin](bool ok, const json&, const std::string& t) {
                    const std::string texto = t; // entero: una pregunta se contesta con esto
                    QMetaObject::invokeMethod(voz,
                                              [fin, ok, texto]() { fin(ok, QString::fromStdString(texto)); });
                });
            },
            // El estado que ve el modelo para completar argumentos: los nombres
            // que existen y el volumen de cada fuente de audio.
            [leer, rtp = &rt_](std::function<void(json)> listo) {
                auto estado = std::make_shared<json>(json::object());
                leer("scene_list", json::object(), [=](bool, const json& sl, const std::string&) {
                    json escenas = json::array();
                    for (const auto& e : sl.value("scenes", json::array()))
                        escenas.push_back(e.value("name", std::string()));
                    (*estado)["escena_actual"] = sl.value("current", std::string());
                    (*estado)["escena_armada"] = sl.value("armada", std::string());
                    (*estado)["escenas"] = escenas;
                    // Con más de una salida: cuáles hay y la del asistente (la por omisión,
                    // que es la de sus llamadas: CallContext::salida vacía). Con una, nada.
                    if (const auto sal = rtp->engine().status().salidas; sal.size() > 1) {
                        json l = json::array();
                        for (const auto& x : sal) l.push_back({{"id", x.id}, {"nombre", x.nombre}});
                        (*estado)["salidas"] = l;
                        (*estado)["salida"] = sal.front().id;
                    }
                    leer("escena_fuentes_ver", json::object(), [=](bool, const json& src, const std::string&) {
                        json fuentes = json::array();
                        for (const auto& f : src.value("sources", json::array()))
                            fuentes.push_back({{"nombre", f.value("name", std::string())},
                                               {"visible", f.value("visible", true)}});
                        (*estado)["fuentes_de_la_escena_actual"] = fuentes;
                        leer("mixer_list", json::object(), [=](bool, const json& mx, const std::string&) {
                            json audio = json::array();
                            for (const auto& a : mx.value("sources", json::array()))
                                audio.push_back(
                                    {{"nombre", a.value("name", std::string())},
                                     {"volumen_db", std::round(a.value("volumeDb", 0.0) * 10.0) / 10.0},
                                     {"silenciada", a.value("muted", false)}});
                            // El micrófono del operador primero: es el que se toca cuando
                            // el pedido no nombra fuente (así se entrenó el modelo v2). Si no
                            // hay uno elegido, el primer micrófono del equipo.
                            std::string mic = rtp->config().operatorAudio;
                            for (std::size_t i = 0; mic.empty() && i < audio.size(); ++i) {
                                const std::string n = audio[i].value("nombre", std::string());
                                const std::string id = rtp->engine().sourceTypeId(n);
                                if (!id.empty() &&
                                    inputs::esMicrofono(id, json::parse(rtp->engine().sourceSettingsJson(n), nullptr, false)))
                                    mic = n;
                            }
                            for (std::size_t i = 1; i < audio.size(); ++i)
                                if (!mic.empty() && audio[i].value("nombre", std::string()) == mic) {
                                    json primero = audio[i];
                                    audio.erase(audio.begin() + static_cast<std::ptrdiff_t>(i));
                                    audio.insert(audio.begin(), primero);
                                    break;
                                }
                            (*estado)["audio"] = audio;
                            leer("room_status", json::object(),
                                 [=](bool, const json& rs, const std::string&) {
                                     json gente = json::array();
                                     for (const auto& x : rs.value("participants", json::array()))
                                         gente.push_back({{"id", x.value("id", std::string())},
                                                          {"nombre", x.value("name", std::string())},
                                                          {"estado", x.value("state", std::string())}});
                                     (*estado)["participantes"] = gente;
                                     leer("grafico_listar", json::object(),
                                          [=](bool, const json& gr, const std::string&) {
                                              json graficos = json::array();
                                              for (const auto& g : gr.value("graficos", json::array()))
                                                  graficos.push_back(g.value("nombre", std::string()));
                                              (*estado)["graficos"] = graficos;
                                              leer("carruseles_listar", json::object(),
                                                   [=](bool, const json& la, const std::string&) {
                                                       json carruseles = json::array();
                                                       for (const auto& c :
                                                            la.value("carruseles", json::array()))
                                                           carruseles.push_back(
                                                               c.is_string()
                                                                   ? c.get<std::string>()
                                                                   : c.value("nombre", std::string()));
                                                       (*estado)["carruseles"] = carruseles;
                                                       listo(*estado);
                                                   });
                                          });
                                 });
                        });
                    });
                });
            });
    }
    // El operador delega en el asistente la atencion de alguien del lobby: se
    // habilita la exencion para ESA persona y se le pasa la tarea al agente.
    vm_.sala().setIntakeSink([this](const QString& id, const QString& name, bool yaAdentro) -> QString {
        // QUIÉN ATIENDE es una sola regla (sala::quienAtiende, con test): la
        // recepción si está prendida; si no, el de automatización; nunca el del
        // evento. Y si se lo atiende HABLANDO lo decide la atención
        // (ui/AtencionViewModel): acá solo se le agrega al encargo cómo hablarle.
        // Al que ya entró solo no se lo atiende hablando: la atención es del lobby.
        AtencionViewModel* atencion = vm_.atencionVm();
        const auto modoPara = [&](sala::Atendedor quien) -> QString {
            if (yaAdentro || atencion == nullptr) return {};
            return AtencionViewModel::instruccion(atencion->empezarSala(id, name, quien));
        };
        agent::Despachador& d = vm_.despachador();
        const agent::Rol* rolRecepcion = d.roles().porId("recepcion");
        auto* ag = static_cast<agent::AgentViewModel*>(vm_.agenteAutomatizacion());
        const sala::Atendedor quien =
            sala::quienAtiende(rolRecepcion != nullptr && rolRecepcion->activo, ag != nullptr && ag->running());
        if (quien == sala::Atendedor::Recepcion) {
            core_.startIntake(id.toStdString(), name.toStdString());
            const QString hablando = modoPara(sala::Atendedor::Recepcion);
            std::string err;
            d.encargar("recepcion",
                       (yaAdentro ? ba::i18n::llenar(ba::i18n::traducir("mcp",
                                                                        "«%1» YA ENTRO sola (estaba en la "
                                                                        "lista de auto-aceptar del evento): no le "
                                                                        "pidas los datos de cero, mostrale los que ya "
                                                                        "tenemos para que confirme. "),
                                                     {name.toStdString()})
                                  : reconocerPrimero(id).toStdString()) +
                           ba::i18n::llenar(ba::i18n::traducir("mcp", "El operador te pide que atiendas a la persona "
                                                                      "que espera en el lobby (id %1, se anoto como "
                                                                      "«%2»)."),
                                            {id.toStdString(), name.toStdString()}) +
                           hablando.toStdString(),
                       "lobby:" + id.toStdString(), 0, &err);
            return QString::fromStdString(err);
        }
        // Sin recepción, el de AUTOMATIZACIÓN (trabaja solo, en segundo plano): el del evento
        // es la charla con el operador y no tiene que recibir encargos del lobby.
        if (quien == sala::Atendedor::Nadie) return QString::fromStdString(sala::porQueNadie(quien));
        core_.startIntake(id.toStdString(), name.toStdString());
        const QString hablando = modoPara(sala::Atendedor::Automatizacion);
        if (yaAdentro) {
            // YA ESTA ADENTRO: entro sola porque estaba en la lista del
            // evento. Pedirle los datos de cero a alguien que el sistema
            // reconocio y dejo pasar es contradecirse en la misma frase.
            ag->send(QString::fromStdString(ba::i18n::llenar(ba::i18n::traducir("mcp",
                     "«%1» (id %2) YA ENTRO A LA CONFERENCIA sola: el sistema la reconocio y estaba en la lista de "
                     "auto-aceptar del evento. NO le pidas los datos de cero. Hablale por "
                     "`chat_send`, saludala por su nombre, deciles que ya esta adentro, y mostrale "
                     "los datos que ya tenemos de ella (`participant_profile` y `rostro_listar`) "
                     "para que confirme si siguen bien o si cambio algo —el cargo sobre todo, que "
                     "es lo que va al aire—. Si te corrige algo, guardalo con `participant_profile` "
                     "y, si cambio su cargo, tambien con `rostro_guardar {id, nombre, cargo}`. "
                     "Avisame con `operator_ask` SOLO si algo no cierra. Manejate sola."),
                     {name.toStdString(), id.toStdString()})));
            return {};
        }
        ag->send(reconocerPrimero(id) + QString::fromStdString(ba::i18n::llenar(ba::i18n::traducir("mcp",
                 "Atendé vos a la persona que espera en el lobby (id %1, se anotó como «%2»). Hablale por chat con `chat_send` usándola como destinatario, presentate en una línea y "
                 "pedile, de a una pregunta por vez y en español rioplatense: su nombre y apellido tal como "
                 "quiere que se lea en pantalla, su cargo o de qué va a hablar, cómo quiere que la presenten "
                 "al aire, y un teléfono o correo de contacto por si se corta. Leé sus respuestas con "
                 "`invitados_chat_leer`. A medida que las tengas, completá su ficha con `participant_profile`. "
                 "Cuando los tengas, ANTES de consultarme a mí, avisale por `chat_send` que ya "
                 "está: algo como «Listo, ya tengo todo. Se lo paso a producción y te aviso apenas "
                 "me confirmen». Del otro lado no se ve nada de lo que pasa acá, y quedarse "
                 "esperando en silencio frente a una cámara, sin saber si alguien lo está "
                 "mirando, se siente como que lo dejaron colgado. "
                 "Recién después mostrámelos todos juntos con `operator_ask` "
                 "(opciones: «Hacer pasar» y «Todavía no») pasando `photoOf` con su nombre, asi la "
                 "tarjeta lleva su foto. "
                 "Si contesto «Hacer pasar», admitila con `participant_admit` Y avisale por chat que "
                 "ya entra; si no, dejala esperando y avisale con cortesía que todavía no. "
                 "Contestale SIEMPRE, salga como salga: el silencio es la peor respuesta. Mientras dure esto "
                 "no me preguntes nada más: manejate sola."),
                 {id.toStdString(), name.toStdString()})) +
                 guardarLaCara(id) + hablando);
        return {};
    });
    registerSystemTools();
    registerInputTools();
    registerMediaTools();
    registerLaminaTools();
    registerOperatorTools();
    registerAccountTools();
    registerAudioTools();
    registerGraficoTools();
    registerRegistroTools();
    registerRecetaTools();
    registerProcedimientoTools();
    registerAgenteTools();
    registerModeloTools();
    registerTraduccionTools();
    registerMatrizTools();
    registerCalibracionTools();
    registerPtzTools();
    registerVideoTools();
    registerTiemposTools();
    registerRedTools();
    registerPluginTools();
    registerDcnTools();
    registerSesionTools();
    registerAsambleaTools();
    registerConfiguracionesTools();
    registerDocumentosTools();
    registerEquiposTools();
    registerPrompterTools();
    registerPantallaTools();
    registerQrTools();
    registerGrillaTools();
    registerVentanaTools();
    registerRostroTools();
    registerAtencionTools();
    registerTotemTools();
    registerControlTools();
    registerSalidasTools();
    registerVivosTools();
    // El mapa de categorias va en las `instructions` del initialize (lo arma
    // McpCore en cada initialize, porque depende del rol del cliente) y
    // cuesta unos 400 tokens.
    applySecurity();
    // N4: el límite de frecuencia por sesión de lo que cambia algo (ver McpCore::LimiteFrecuencia).
    core_.setLimiteFrecuencia({ba::ajustes::numero("mcp.limitePorSegundo", 10.0), ba::ajustes::entero("mcp.limiteRafaga", 30),
                               ba::ajustes::numero("mcp.limiteAirePorSegundo", 2.0), ba::ajustes::entero("mcp.limiteAireRafaga", 8)});
    // Con todas las herramientas ya registradas (ver verificarJuegos). Los nombres
    // viejos de las renombradas en bloque van a su Tool::antes, y para eso tienen
    // que existir (ver McpRenombres.cpp).
    registrarRenombres(core_);
    core_.verificarJuegos();
}

void McpService::applyApprovalPolicy() {
    // De la configuración que publica el dueño (la foto): se llama en la interfaz.
    const std::string p = vm_.ventanilla().foto().configuracion.app->mcpApproval;
    core_.setApprovalPolicy(p == "siempre" ? ApprovalPolicy::Always
                            : p == "nunca" ? ApprovalPolicy::Never
                                           : ApprovalPolicy::Untrusted);
}

void McpService::applySecurity() {
    // La salvaguarda cubre lo que viene de los invitados de la sala (WebRTC) —lo que
    // escriben, sus nombres y datos, sus archivos y su video— y el chat del público
    // (YouTube, Twitch, Facebook, la web): texto de afuera, no instrucciones. Cada
    // herramienta lo DECLARA en su bloque (untrustedOutput: siempre; untrustedConGente:
    // si hay alguien en la sala; mediated: lo decide una persona con un click), así el
    // inventario lo ve y no hay una lista aparte que se olvide de una.
    // «Hay gente» lo sabe la interfaz (la copia de la sala de ConferenceClient avisa al
    // cambiar); el núcleo lo lee desde el hilo de cada herramienta.
    auto* conf = static_cast<ConferenceViewModel*>(vm_.conference());
    const auto mirarSala = [this, conf]() { hayGente_.store(conf->client().resumen().conectados > 0); };
    mirarSala();
    connect(&conf->client(), &ConferenceClient::salaCambio, this, [mirarSala](unsigned partes) {
        if ((partes & ::ba::app::sala::Participantes) != 0) mirarSala();
    });
    core_.setHayGente([this]() { return hayGente_.load(); });
    applyApprovalPolicy();
    connect(static_cast<SettingsModel*>(vm_.settings()), &SettingsModel::applied, this,
            [this](const QString&) { applyApprovalPolicy(); });
    // La aprobacion la da una persona en la barra de abajo: la tarjeta la arma
    // el servidor con la herramienta y los argumentos exactos.
    // Atencion delegada: una llamada es "de esa persona" si el nombre o id que
    // trae resuelve contra la sala al id que el operador delego.
    core_.setIntakeMatcher([this](const std::string& tool, const json& args) {
        const std::vector<std::string> ids = core_.intakes();
        if (ids.empty() || !args.is_object()) return false;
        static const std::map<std::string, const char*> kArg{{"chat_send", "to"},
                                                             {"participant_profile", "participant"},
                                                             {"participant_admit", "participant"},
                                                             {"participant_talk", "participant"}};
        const auto it = kArg.find(tool);
        if (it == kArg.end()) return false;
        const std::string who = args.value(it->second, std::string());
        if (who.empty() || who == "all") return false;
        // El que espera en la sesión remota y se lo atiende hablando: decirle algo
        // (chat_send) es la atención misma, solo a él, mientras dura (termina al habilitarlo).
        if (AtencionViewModel* at = vm_.atencionVm();
            tool == "chat_send" && at != nullptr && at->atiendeHablandoEnAsamblea(QString::fromStdString(who)))
            return true;
        json p;
        std::string err;
        if (!resolveParticipant(roomSnapshot(vm_), who, p, err)) return false;
        const std::string id = p.value("id", "");
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    });
    // Guardas de aire (McpAire): qué es "hay aire" y cuándo una PTZ cuenta
    // (la cámara está en la escena al aire).
    core_.aire().cargar(rt_.dataRoot() / "Config" / "mcp-aire.json");
    // Hay aire = sale algo (transmisión, grabación, señal limpia); el guion del
    // evento corriendo sin salida no lo es.
    const auto hayAire = [this]() { return rt_.haySalida(); };
    core_.setHayAire(hayAire);
    core_.aire().setCondicion([this](const std::string&, const json& a) {
        const std::string e = a.value("entrada", std::string());
        if (e.empty()) return true;
        return rt_.engine().fuenteAlAire(e);
    });
    // El guion del evento llama herramientas (p. ej. el cue Pantalla): lo corre
    // el operador, así que cuenta como una persona.
    if (auto* ev = static_cast<EventoViewModel*>(vm_.evento()))
        ev->corridaVm().setHerramienta([this](const std::string& h, const json& args, std::function<void(bool, const std::string&)> fin) {
            core_.callTool({{"name", h}, {"arguments", args}},
                           [fin](const json& r) {
                               std::string texto;
                               for (const auto& c : r.value("content", json::array()))
                                   if (c.value("type", "") == "text") texto += c.value("text", "");
                               fin(!r.value("isError", false), texto);
                           },
                           CallContext{"evento", "", "evento", true});
        });
    for (QObject* o : {vm_.agent(), vm_.agenteAutomatizacion()}) {
        auto* ag = static_cast<agent::AgentViewModel*>(o);
        if (ag == nullptr) continue;
        ag->setHayAire(hayAire);
        ag->setAireInfo([this]() {
            const json j{{"hay_aire", core_.hayAire()}, {"herramientas", core_.aire().lista()}};
            return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
        });
        // Si sale algo o no cambia con lo que sale (grabar, transmitir: Studio.aire).
        QObject::connect(&vm_.aire(), &EstadoAireViewModel::aireChanged, ag, &agent::AgentViewModel::aireCambio);
        // (Y las demás salidas: cada una con lo suyo, en la lista de salidas.)
        QObject::connect(&vm_.salidas(), &SalidasViewModel::cambio, ag, &agent::AgentViewModel::aireCambio);
    }
    core_.setApprover(
        [this](const Tool& t, const json& args, const std::string& reason, std::function<void(bool)> decide) {
            auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
            // Lo que parezca una clave no se muestra en la tarjeta (queda en pantalla y en el chat).
            std::function<json(const json&)> tapar = [&tapar](const json& j) -> json {
                if (!j.is_object()) return j;
                json o = json::object();
                for (auto it = j.begin(); it != j.end(); ++it) {
                    std::string k = it.key();
                    for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const bool secreta = k.find("token") != std::string::npos || k.find("clave") != std::string::npos ||
                                         k.find("key") != std::string::npos || k.find("secret") != std::string::npos ||
                                         k.find("password") != std::string::npos || k == "pin";
                    o[it.key()] = secreta && it.value().is_string() ? json(ba::i18n::traducir("mcp", "«tapado»")) : tapar(it.value());
                }
                return o;
            };
            std::string a = args.empty() ? ba::i18n::traducir("mcp", "(sin argumentos)") : tapar(args).dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
            if (a.size() > 1200) a = a.substr(0, 1200) + "\n…";
            const bool deAire = reason.rfind("ACCIÓN DE AIRE", 0) == 0;
            // La respuesta es la opción elegida, tal como se mostró: se compara con la misma etiqueta.
            const std::string aprobar = ba::i18n::traducir("mcp", "Aprobar");
            const int id = msgs->post(json{{"kind", "question"},
                                           {"approval", true},
                                           {"aire", deAire},
                                           {"priority", deAire || McpCore::esDestructiva(t, args) ? "critical" : "warning"},
                                           {"title", deAire ? ba::i18n::traducir("mcp", "ACCIÓN DE AIRE: ¿la confirmás?")
                                                            : ba::i18n::traducir("mcp", "¿Aprobás esta acción del asistente?")},
                                           {"text", t.title + " (" + t.name + ")"},
                                           {"details", a},
                                           {"reason", reason},
                                           {"options", {aprobar, ba::i18n::traducir("mcp", "Rechazar")}},
                                           {"allowText", false}});
            const int minutos = vm_.ventanilla().foto().configuracion.app->mcpAprobacionMin; // Configuración → Asistente (MCP)
            msgs->waitAnswer(id, std::max(1, minutos) * 60 * 1000, [msgs, id, decide, aprobar](json item) {
                if (item.is_null()) msgs->dismiss(id); // vencio: la tarjeta se va y queda rechazada
                decide(!item.is_null() && item.value("answer", "") == aprobar);
            });
        });
}

McpService::~McpService() {
    // Un cambio que todavía esperaba juntarse con otros (setSincronizador) no se pierde al cerrar.
    if (guardadoPendiente_->exchange(false)) rt_.save();
    // El transporte vive en el hilo de control mientras el plano anda: si todavía está
    // allá (el plano sigue), se borra allá (sus sockets son de ese hilo).
    if (http_ && http_->thread() != QThread::currentThread() && plano_ != nullptr && plano_->andando()) {
        McpHttp* h = http_.release();
        QMetaObject::invokeMethod(h, [h]() { delete h; }, Qt::BlockingQueuedConnection);
    }
    if (pool_) pool_->waitForDone(5000);
    if (poolSinInterfaz_) poolSinInterfaz_->waitForDone(5000);
}

void McpService::setPlano(PlanoControl& plano) {
    plano_ = &plano;
    pool_ = std::make_unique<QThreadPool>();
    pool_->setMaxThreadCount(4);
    pool_->setObjectName("mcp-foto");
    McpCore::Ejecutores e;
    e.interfaz = [&plano](McpCore::Tarea t) { plano.encolarEnInterfaz(std::move(t)); };
    e.control = [&plano](McpCore::Tarea t) { plano.encolar(std::move(t)); };
    e.foto = [p = pool_.get()](McpCore::Tarea t) { p->start(std::move(t)); };
    // Lo que escribe sin la interfaz o tarda (ndi_sources espera segundos a la red): aparte,
    // así cuatro búsquedas a la vez no dejan sin hilo a status, help y room_status.
    poolSinInterfaz_ = std::make_unique<QThreadPool>();
    poolSinInterfaz_->setMaxThreadCount(4);
    poolSinInterfaz_->setObjectName("mcp-sin-interfaz");
    e.sinInterfaz = [p = poolSinInterfaz_.get()](McpCore::Tarea t) { p->start(std::move(t)); };
    e.despues = [&plano](int ms, McpCore::Tarea t) { plano.despues(ms, std::move(t)); };
    e.enInterfaz = [&plano]() { return plano.enInterfaz(); };
    e.enControl = [&plano]() { return plano.enControl(); };
    e.topeInterfazMs = ba::ajustes::entero("mcp.topeInterfazMs", 5000);
    core_.setEjecutores(std::move(e));
    // Las de control tocan el motor y su aire, que la interfaz también usa: corren con el
    // turno del motor (emision/TurnoMotor.hpp). La interfaz lo tiene solo mientras está
    // adentro del motor; si no lo suelta en el tope (trabada ADENTRO, un rearme), «ocupado».
    const TurnoMotor& turno = rt_.turno();
    core_.setTurno({[&turno](int ms) { return turno.tomarHasta(ms); }, [&turno]() { turno.soltar(); },
                    [&turno]() { return turno.estado(); }});
    // Lo que se sabe sin la interfaz, para contestar cuando está ocupada: el documento de
    // aire (qué sale de cada salida, el negro, la placa, la línea), el latido y el plano.
    core_.setFotoDeEmergencia([this]() {
        const salidas::AlAire a = rt_.documentoAire().foto();
        return json{{"aire", salidas::aJson(a)},
                    {"hay_aire", rt_.documentoAire().hayAire()},
                    {"latido", rt_.latido().estado()},
                    {"plano", plano_ != nullptr ? plano_->estado() : json()}};
    });
}

bool McpService::start(quint16 port) {
    // La traza completa arranca con el servidor: lo que pase antes no existe
    // todavia para nadie de afuera.
    Traza::la().abrir(ba::platform::dataRoot() / "Logs");
    // T47: de fábrica, un token nuevo en cada arranque (el puente ba-mcp lo lee de
    // Credenciales/ en cada pedido; un cliente HTTP directo lo vuelve a pedir con `ba-mcp --token`).
    const QString cred = QString::fromStdWString((ba::platform::credentialsDir()).wstring());
    const QString token = vm_.seguridad().rotarTokenMcp() ? rotarToken(cred) : loadOrCreateToken(cred);
    if (token.isEmpty()) return false;
    http_ = std::make_unique<McpHttp>(core_, token);
    bool escucha = false;
    if (plano_ != nullptr && plano_->andando()) {
        // Al hilo de control: con la interfaz trabada, el servidor sigue contestando. El
        // puerto se abre allá (los sockets son del hilo del servidor).
        // Adoptado: al detener el plano vuelve a este hilo y se borra acá, sin carreras.
        plano_->adoptar(http_.get());
        QMetaObject::invokeMethod(http_.get(), [&]() { escucha = http_->listen(port); }, Qt::BlockingQueuedConnection);
    } else {
        escucha = http_->listen(port);
    }
    if (!escucha) {
        if (http_->thread() != QThread::currentThread()) {
            McpHttp* h = http_.release();
            QMetaObject::invokeMethod(h, [h]() { delete h; }, Qt::BlockingQueuedConnection);
        }
        http_.reset();
        return false;
    }
    // Para el puente stdio (ba-mcp): donde escucha. El token NO va aca (queda
    // cifrado en Credenciales/; el puente lo lee de ahi).
    const auto runtime = ba::platform::stateDir(); // dentro del bundle portable
    if (!runtime.empty()) {
        const QJsonObject o{{"port", http_->port()},
                            {"url", url()},
                            {"pid", static_cast<qint64>(QCoreApplication::applicationPid())}};
        // Solo-dueño, por el escritor durable (persist/ColaEscritura.hpp).
        ba::persist::cola().escribir(runtime / "mcp.json", QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(),
                                     {ba::persist::Durabilidad::Normal, ba::persist::Prioridad::Normal,
                                      ba::persist::Permisos::Privados, {}});
    }
    return true;
}

quint16 McpService::port() const { return http_ ? http_->port() : 0; }
QString McpService::url() const { return QString("http://127.0.0.1:%1/mcp").arg(port()); }
QString McpService::token() const { return http_ ? http_->token() : QString(); }

// ============================================================================
// Lo que leen las herramientas de la foto (McpFoto.hpp), con por qué es seguro
// ============================================================================

FuentesDeFoto McpService::fuentesDeFoto() {
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;
    FuentesDeFoto f;
    // El documento de aire y el latido: «desde cualquier hilo» (arranque/RuntimeAire.hpp; ya los lee
    // la foto de emergencia desde el control). La foto de las salidas la publica el hilo
    // de salidas bajo su candado; la lista de salidas, el reloj del motor.
    f.aire = [&rt]() { return salidas::aJson(rt.documentoAire().foto()); };
    f.hayAire = [&rt]() { return rt.haySalida(); };
    f.latido = [&rt]() { return rt.latido().estado(); };
    f.salidasMotor = [&rt]() { return rt.engine().fotoSalidas(); };
    f.salidasPublicadas = [&rt]() { return rt.salidasPublicadas(); };
    // Lo pone el launcher por argumentos al arrancar y no cambia más.
    f.modoSeguro = []() {
        const ba::fault::ModoSeguro& ms = ba::fault::delProceso();
        json apagadas = json::array();
        for (ba::fault::Pieza p : ms.apagadas()) apagadas.push_back(ba::fault::clave(p));
        return json{{"activo", ms.activo()}, {"nivel", ms.nivel}, {"apagadas", apagadas}, {"escenas", !ms.escenasVacias()}};
    };
    // El muestreador mide en su hilo y guarda bajo su candado; RecursosViewModel vive lo
    // que la app (el puntero se toma acá, en la interfaz).
    if (RecursosViewModel* rec = vm.recursos(); rec != nullptr) {
        f.recursos = [rec](int hist, int paso) { return rec->muestreador().informe(hist, paso); };
        f.servicio = [rec]() { return rec->fotoServicio(); };
    }
    f.derivaPlacas = []() { return ba::motor::derivaDePlacas(); };
    f.elementosGst = []() { return video::revisarElementosGst(); };
    control::BusEventos& bus = vm.bus();
    f.temas = [&bus](const std::string& patron) { return bus.temas(patron); };
    f.ultimoTema = [&bus](const std::string& tema) { return bus.ultimo(tema); };
    f.logs = ba::platform::dataRoot() / "Logs";
    // Lo que solo sabe la interfaz: corre en ella (CopiaDeLaInterfaz). Lo del motor, de su
    // foto (la interfaz no entra al motor); el disco, lo último que se pidió (lee el disco:
    // se pide de nuevo para la próxima).
    f.statusInterfaz = [&vm]() {
        const FotoMotor& fo = vm.ventanilla().foto();
        const FotoMotor::EstadoAire& st = fo.estado;
        const FotoMotor::Salida* po = fo.salida(std::string());
        vm.emision().pedirDisco();
        json room = json::object();
        auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
        room["online"] = conf->online();
        room["name"] = conf->room().toStdString();
        room["waiting"] = static_cast<JsonListModel*>(conf->waiting())->count();
        room["inside"] = static_cast<JsonListModel*>(conf->inside())->count();
        room["speakerMode"] = conf->speakerMode();
        json onAir = json::array();
        for (const QVariant& v : conf->onAir())
            onAir.push_back(qvToJson(v));
        room["speakersOnAir"] = onAir;
        return json{{"currentScene", po != nullptr ? po->alAire : std::string()},
                    {"transition", {{"type", fo.ajustes.transicion}, {"ms", fo.ajustes.transicionMs}}},
                    {"streaming", st.transmite},
                    {"streamTime", vm.aire().streamTime().toStdString()},
                    {"streamKbps", vm.aire().streamKbps()},
                    {"streamServer", vm.streamServer().toStdString()},
                    {"streamKeyAvailable", vm.streamKeyAvailable()},
                    {"recording", st.graba},
                    {"recordTime", vm.aire().recordTime().toStdString()},
                    // EN VIVO = sale algo de verdad (transmisión, señal limpia o la grabación si
                    // el modo en vivo la cuenta): lo mismo que usan la app y los bloqueos. El
                    // guion del evento corriendo sin transmitir NO es estar en vivo.
                    {"en_vivo", vm.modoVivo()},
                    {"fps", st.fps},
                    {"droppedFrames", st.descartados},
                    {"totalFrames", st.cuadros},
                    {"videoEncoder", st.codificador},
                    {"encoder_respaldo_activo", fo.equipo.motor.value("respaldo_codificador", false)},
                    {"cpuPercent", vm.cpuPercent()},
                    {"programPeakDb", {vm.programPeakL(), vm.programPeakR()}},
                    {"canvas", vm.canvasLabel().toStdString()},
                    {"gatewayAlive", vm.gatewayAlive()},
                    {"lastError", fo.equipo.motor.value("ultimo_error", std::string())},
                    {"salidas", resumenSalidas(fo)},
                    {"disco", qvToJson(vm.emision().discoUi())},
                    // El corte de luz (F1-11): batería/UPS, lo que queda y qué hizo la política.
                    {"energia", vm.energia() != nullptr ? vm.energia()->estado() : json()},
                    {"room", room}};
    };
    f.salidasInterfaz = [&vm]() {
        const FotoMotor& fo = vm.ventanilla().foto();
        return json{{"tope", fo.tope}, {"salidas", resumenSalidas(fo)}};
    };
    f.esperaInterfazMs = ba::ajustes::entero("mcp.esperaCopiaMs", 250);
    // La sala, preguntada a B sin la interfaz (un socket propio, bloqueante, con tope).
    f.sala = [](const std::string& ruta) -> std::optional<json> {
        const std::optional<QJsonObject> r = ConferenceClient::pedirSinInterfaz(QString::fromStdString(ruta));
        if (!r) return std::nullopt;
        json j = json::parse(QJsonDocument(*r).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (j.is_discarded()) return std::nullopt;
        return j;
    };
    // Los dueños que dan lo suyo desde cualquier hilo (los punteros se toman acá, en la
    // interfaz; viven lo que la app).
    AppViewModel* app = &vm.app();
    f.modoVivo = [app]() { return app->modoVivoEstado(); };
    f.caidas = [](int n) { return AppViewModel::caidas(n); };
    f.caidaReciente = [app]() { return app->caidaRecienteFoto(); };
    auto* sys = static_cast<SystemViewModel*>(vm.system());
    f.sistema = [sys]() { return sys->info(); };
    agent::Notificador* notif = &vm.notificador();
    f.notificadores = [notif]() { return notif->lista(); };
    return f;
}

// ============================================================================
// Lo que hacen las herramientas de aire del control (McpMando.hpp)
// ============================================================================

MandoDeAire McpService::mandoDeAire() {
    // Todo esto corre en el plano de control con el turno del motor tomado (McpCore::
    // setTurno): los conmutadores, la seguridad del aire y lo que sale del runtime lo toman
    // también (son de a uno con la interfaz) y avisan a las vistas en su hilo (enDueno).
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;
    MandoDeAire m;
    const auto conm = [&vm](const std::string& salida) -> escenas::Conmutador& { return conmutadorDe(vm, salida); };
    m.escenas = [&rt]() { return rt.engine().sceneNames(); };
    m.alAire = [&rt](const std::string& salida) { return rt.engine().currentScene(salida); };
    m.armada = [conm](const std::string& salida) { return conm(salida).armada(); };
    m.poner = [conm](const std::string& salida, const std::string& escena) { return conm(salida).poner(escena); };
    m.armar = [conm](const std::string& salida, const std::string& escena) { return conm(salida).armar(escena); };
    m.take = [conm](const std::string& salida, const std::string& tipo, int ms) { return conm(salida).take(tipo, ms); };
    m.tbar = [conm](const std::string& salida, double t, bool soltar) { return conm(salida).tbar(t, soltar); };
    m.manualHacia = [&rt](const std::string& salida) { return rt.engine().manualHacia(salida); };
    // La seguridad del aire no tiene estado propio (lo tiene el motor): se usa la de la vista.
    emision::SeguridadAire& seg = vm.seguridadAire();
    m.ftb = [&seg](const std::string& salida, bool activar, int ms, const std::string& audio) {
        return seg.ftb(activar, ms, audio, salida);
    };
    m.placa = [&seg](const std::string& salida, bool activar, const std::string& texto, const std::string& imagen, int ms) {
        return seg.placa(activar, texto, imagen, ms, salida);
    };
    m.estadoPlaca = [&seg](const std::string& salida) { return seg.estado(salida); };
    // Lo que pasó llega en el dueño (AppRuntime::alResultado), que es el hilo de la herramienta.
    const auto hechoDe = [](MandoDeAire::Hecho hecho) {
        return [hecho = std::move(hecho)](const AppRuntime::ResultadoOrden& r) {
            if (hecho) hecho({r.ok, r.error, r.archivo});
        };
    };
    m.transmitir = [&rt, hechoDe](const std::string& salida, const std::string& server, const std::string& key, bool caliente,
                                  MandoDeAire::Hecho hecho) {
        MandoDeAire::Inicio r;
        r.ok = rt.salirAlAireManual(server, key, caliente, salida, hechoDe(std::move(hecho)));
        // ultimoInicio y lastError son de este hilo (como errno): los de recién.
        const ApiMotor::Inicio i = rt.engine().ultimoInicio();
        r.yaIgual = r.ok && i == ApiMotor::Inicio::YaIgual;
        if (!r.ok && i == ApiMotor::Inicio::YaOtroDestino) r.activoEn = rt.engine().destinoActivo(salida);
        if (!r.ok) r.error = rt.lastError();
        return r;
    };
    m.cortarTransmision = [&rt](const std::string& salida) { rt.stopStreaming(salida); };
    m.iniciarDestino = [&rt](const std::string& destino) { return rt.iniciarDestino(destino); };
    m.detenerDestino = [&rt](const std::string& destino) { rt.detenerDestino(destino); };
    m.destinos = [&rt]() { return rt.destinos(); };
    m.variableClave = [&rt]() { return rt.ajustesAire().claveEnv; };
    m.grabacion = [&rt](const std::string& salida) { return rt.engine().grabacionEstado(salida); };
    m.disco = [&rt]() { return rt.discoGrabacion(); };
    m.grabar = [&rt, hechoDe](const std::string& salida, MandoDeAire::Hecho hecho) {
        return rt.startRecording(salida, hechoDe(std::move(hecho))) ? std::string() : rt.lastError();
    };
    m.pararGrabacion = [&rt](const std::string& salida) { rt.stopRecording(salida); };
    m.contenedor = [&rt]() { return rt.ajustesAire().rec.container; };
    m.discoMinimoH = AppRuntime::kDiscoMinimoH;
    return m;
}

// ============================================================================
// Herramientas del motor
// ============================================================================

void McpService::registerTools() {
    core_.addCategory("estado", ba::i18n::traducir("mcp", "Estado"), ba::i18n::traducir("mcp", "Vistazo general: qué está al aire, salidas, salud."));
    core_.addCategory("escenas", ba::i18n::traducir("mcp", "Escenas"),
                      ba::i18n::traducir("mcp", "Escenas: listar, cambiar la del aire, crear, definir completas, exportar."));
    core_.addCategory("fuentes", ba::i18n::traducir("mcp", "Fuentes"),
                      ba::i18n::traducir("mcp", "Lo que compone cada escena: cámaras, imágenes, textos, invitados..."));
    core_.addCategory("audio", ba::i18n::traducir("mcp", "Audio"), ba::i18n::traducir("mcp", "Mezclador: volumen, silencio, niveles."));
    core_.addCategory("salidas", ba::i18n::traducir("mcp", "Salidas"), ba::i18n::traducir("mcp", "Transmisión (RTMP) y grabación."));
    core_.addCategory("config", ba::i18n::traducir("mcp", "Configuración"),
                      ba::i18n::traducir("mcp", "Todos los ajustes del appliance (video, codificación, conferencia, red...)."));
    // La botonera y el bus de eventos (ver McpBotoneraTools.cpp).
    registerBotoneraTools(core_, vm_);
    // El control web (dashboard /control/): sus aparatos (ver McpControlWebTools.cpp).
    registerControlWebTools(core_, vm_);
    // El guion del evento: su propio bloque (ver McpEventoTools.cpp).
    if (auto* ev = static_cast<EventoViewModel*>(vm_.evento())) {
        registerEventoTools(core_, *ev, vm_, [this](const std::string& id) {
            auto* pv = static_cast<PersonasViewModel*>(vm_.personas());
            if (pv == nullptr || !pv->disponible()) return std::pair{false, std::string()};
            for (const QVariant& v : pv->personas()) {
                const QVariantMap m = v.toMap();
                if (m.value("id").toString().toStdString() == id)
                    return std::pair{true, m.value("nombre").toString().toStdString()};
            }
            return std::pair{false, std::string()};
        });
    }
    // El tema del evento en todas las páginas web (ver McpTemaTools.cpp).
    if (auto* te = static_cast<TemaEventoViewModel*>(vm_.temaEvento())) registerTemaTools(core_, *te);

    // Las preguntas del publico: viven en B, y esto es el lado de A que las
    // modera (ver McpPreguntaTools.cpp).
    registerPreguntaTools(core_, vm_, rt_);

    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;
    // La invariante, puesta una vez y para todas: lo que se toca por MCP queda
    // guardado. El despachador la aplica a TODA herramienta que modifica algo,
    // asi ninguna superficie nueva depende de que su autor se acuerde (y ninguna
    // guarda por su cuenta: eran dos guardados por cambio). La vista no se avisa
    // desde aca: el runtime avisa sus cambios (AppRuntime::observar) y
    // StudioViewModel se entera solo. Ver McpCore::setSincronizador.
    //
    // JUNTA LOS CAMBIOS. Cada guardado reescribe app.json, gateway.json y launcher.json:
    // un fader que el asistente mueve de a poco (audio_set) eran treinta guardados por
    // segundo. El primer cambio arma uno en el dueño de la configuración (el plano de
    // control) dentro de kJuntarMs; lo que llega mientras tanto va en ese.
    core_.setSincronizador([this, pendiente = guardadoPendiente_]() {
        constexpr int kJuntarMs = 400;
        if (pendiente->exchange(true)) return; // ya hay uno en camino
        // Sin `this`: el reloj puede vencer con el servicio ya cerrado (el runtime sigue).
        const auto guardar = [&rt = rt_, pendiente]() {
            if (!pendiente->exchange(false)) return; // ya lo guardó el cierre
            rt.save(); // lo que cambie desde acá, en el próximo
        };
        if (plano_ != nullptr && plano_->andando())
            plano_->despues(kJuntarMs, guardar);
        else
            guardar(); // sin plano (pruebas, consola): en el acto
    });

    // --- Perfil (solo en un build con BA_PERFILAR: ver diag/Perfil) ----------------
    if (ba::diag::Perfil::activo()) {
        Tool t;
        t.name = "perfil";
        t.title = ba::i18n::traducir("mcp", "Dónde se va la CPU y la GPU (build de diagnóstico)");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "La última medición (cada segundo) de la app y del gateway: CPU total en núcleos (1,0 = un núcleo "
            "entero), memoria, los hilos que más consumen con su nombre (los de GStreamer se llaman como su "
            "elemento: así se ve qué parte del video gasta) y, en Windows, la GPU del proceso por motor (3D, "
            "VideoDecode, VideoEncode, Copy). El historial está en Logs/perfil-app.jsonl y "
            "Logs/perfil-gateway.jsonl; cada 10 s un resumen en el registro (log_leer, componente perfil).");
        // Un archivo del registro y la última medición (con su candado): en el pool.
        t.hilo = Hilo::Foto;
        t.run = [](const json&, Done done) {
            json gw = json::object();
            std::ifstream f(ba::platform::dataRoot() / "Logs" / "perfil-gateway.jsonl", std::ios::binary);
            std::string linea, ultima;
            while (std::getline(f, linea))
                if (!linea.empty()) ultima = linea;
            if (!ultima.empty()) gw = json::parse(ultima, nullptr, false);
            done(ToolResult().data(json{{"app", ba::diag::Perfil::ultimo()}, {"gateway", gw}}));
        };
        core_.addTool(std::move(t));
    }

    // --- Estado --------------------------------------------------------------------
    // status, salidas_listar, sistema_recursos, los registros, el bus y la lista de aire
    // contestan con la interfaz trabada (McpFoto.cpp): leen lo de abajo, desde el pool.
    registrarHerramientasDeFoto(core_, fuentesDeFoto());
    // Lo que sale (escenas, TAKE, T-bar, el negro y la placa, transmitir y grabar), en el
    // plano de control con el turno del motor: con la interfaz trabada se hace igual
    // (McpMando.cpp; F3-03).
    registrarHerramientasDeAire(core_, mandoDeAire());
    {
        Tool t;
        t.name = "aire_placas_listar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Textos de placa y estado");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los textos de placa de siempre y si ahora hay FTB o placa al aire.");
        // SeguridadAire es del dueño del motor: en el control (los textos no necesitan la vista).
        t.hilo = Hilo::Control;
        t.run = [this, &seg = vm.seguridadAire()](const json& a, Done done) {
            const json estado = seg.estado(core_.salidaDe(a));
            json textos = json::array();
            for (const QString& s : SalidasViewModel::textosDePlaca(
                     QString::fromStdString(seg.estado().value("placa_texto", std::string()))))
                textos.push_back(s.toStdString());
            done(ToolResult().data(json{{"textos", textos}, {"estado", estado}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escenas_ajustes";
        t.title = ba::i18n::traducir("mcp", "Ajustes del editor de escenas");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`corte_directo_editor`: el botón de cada escena en el editor corta directo (true) o la arma "
                        "en el preview para salir con TAKE (false, de fábrica). Sin argumentos, los muestra.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"corte_directo_editor", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("corte_directo_editor")) vm.escenas().setCorteDirectoEditor(a.value("corte_directo_editor", false));
            done(ToolResult().data(json{{"corte_directo_editor", vm.escenas().corteDirectoEditor()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_aire_ajustar";
        t.title = ba::i18n::traducir("mcp", "Sumar o sacar una herramienta de las de aire");
        t.category = "estado";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`clase`: aire (pide confirmación en vivo) o normal. Las de la lista fija no bajan. Solo "
                        "fuera del aire (con la transmisión o la grabación andando, no se cambia).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"herramienta", "clase"}},
                         {"properties",
                          {{"herramienta", {{"type", "string"}}},
                           {"clase", {{"type", "string"}, {"enum", {"aire", "normal"}}}}}}};
        t.run = [this](const json& a, Done done) {
            if (core_.hayAire()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Al aire no se cambian las guardas de aire.")));
            const std::string e = core_.aire().ajustar(a.value("herramienta", std::string()), a.value("clase", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(core_.aire().lista()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "supervisor_politica";
        t.hilo = Hilo::Control; // el latido tiene su candado; la política, del dueño (F3-03)
        t.title = ba::i18n::traducir("mcp", "Política del supervisor (interfaz trabada)");
        t.category = "estado";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "El supervisor relanza la app si deja de latir. El latido sale de la interfaz Y de un hilo que mira "
            "los cuadros del motor: si la interfaz se traba y el motor sigue, el aire NO se corta. Con la "
            "interfaz trabada más de `espera_ui_s` (10 a 3600, de fábrica 60) y `matar_si_ui_trabada_sin_aire` "
            "(de fábrica true), se relanza solo si no hay transmisión ni grabación. La interfaz trabada CON aire "
            "es una alarma que sale sin la interfaz, al control web y a los notificadores de `notificar` "
            "(«todos», de fábrica, o una lista de ids de notificador_listar; [] = ninguno). Sin argumentos, la muestra "
            "(con el estado: motor_vivo, ui_viva, ui_trabada_ms, ultima_traba_ms).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"espera_ui_s", {{"type", "integer"}, {"minimum", 10}, {"maximum", 3600}}},
                           {"matar_si_ui_trabada_sin_aire", {{"type", "boolean"}}},
                           {"notificar", {{"type", {"string", "array"}}, {"items", {{"type", "string"}}}}}}}};
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("espera_ui_s") || a.contains("matar_si_ui_trabada_sin_aire") || a.contains("notificar")) {
                const std::string e = rt.latido().ponerPolitica(a);
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data(rt.latido().estado()));
        };
        core_.addTool(std::move(t));
    }

    // Las familias (mcp/McpService.hpp), en el orden de siempre.
    registerEscenasTools();
    registerFuentesTools();
    registerMezclaTools();
    registerTransmisionTools();
    registerInformesTools();
    registerDebateTools();
    registerRecintoTools();
    registerGrabacionTools();
    registerConfigTools();
}

} // namespace ba::app::mcp

#include "moc_McpService.cpp"
