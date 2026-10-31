// ============================================================================
// src/app/mcp/McpService.cpp — arranque del servidor MCP y herramientas del
// motor: estado, escenas, fuentes, audio, salidas y configuracion.
// (Imagenes: McpImageTools.cpp · Sala y oradores: McpRoomTools.cpp ·
//  Interfaz y sistema: McpUiTools.cpp · Ayuda: McpHelp.cpp)
// ============================================================================

#include "persist/ColaEscritura.hpp"
#include "McpRenombres.hpp"
#include "arranque/EnergiaAire.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18n.hpp"
#include "diag/Perfil.hpp"
#include "sala/CupoSala.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/Conmutador.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "mcp/McpService.hpp"
#include "ptz/ControlCamaras.hpp"
#include "Piezas.hpp" // motor::hayFabrica
#include "asamblea/Debate.hpp"
#include "asamblea/Presidencia.hpp"
#include <QRegularExpression>
#include <QDateTime>
#include <QRandomGenerator>

#include "agent/AgentViewModel.hpp"
#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"
#include "agent/Voz.hpp"
#include "mcp/McpTraza.hpp"
#include "ui/PersonasViewModel.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QTimer>

#include "fault/FallasProgramadas.hpp"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QQuickWindow>

#include <algorithm>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // diag_probar_caida: HeapCreate
#endif
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>


#include <fstream>

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/SeguridadAire.hpp"
#include "audio/Matriz.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "mcp/McpBotoneraTools.hpp"
#include "mcp/McpControlWebTools.hpp"
#include "mcp/McpEventoTools.hpp"
#include "mcp/McpTemaTools.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpPreguntaTools.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/TemaEventoViewModel.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/InformesResultado.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PropertiesModel.hpp"
#include "video/FuentesCaptura.hpp"
#include "Capturas.hpp" // del motor: hayCapturaDeVentana
#include "ui/SettingsModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "platform/Hilo.hpp"

namespace ba::app::mcp {

std::string avisoReiniciaMotor() {
    return ba::i18n::traducir("mcp", "OJO: REINICIA EL MOTOR. Por unos segundos se corta TODO lo que sale (el programa, la "
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

void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth) {
    std::string mime;
    const int maxW = args.value("maxWidth", defaultWidth);
    const std::string b64 =
        encodeImage(img, args.value("format", std::string("jpeg")), maxW, args.value("quality", 85), mime);
    r.image(b64, mime);
}

void addImage(ToolResult& r, const QImage& img, const json& args, int defaultWidth, McpCore& core,
              const std::filesystem::path& raiz) {
    if (puedeVerImagenes(core)) {
        addImage(r, img, args, defaultWidth);
        return;
    }
    const QString archivo = guardarCaptura(img, raiz, args, defaultWidth);
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

int parseSeconds(const json& v) {
    if (v.is_number()) return static_cast<int>(std::lround(v.get<double>()));
    if (!v.is_string()) return 0;
    const std::string s = v.get<std::string>();
    int m = 0;
    int sec = 0;
    if (std::sscanf(s.c_str(), "%d:%d", &m, &sec) == 2) return m * 60 + sec;
    double d = 0;
    char unit = 0;
    const int n = std::sscanf(s.c_str(), "%lf%c", &d, &unit);
    if (n >= 1) {
        if (n == 2 && (unit == 'm' || unit == 'M')) return static_cast<int>(std::lround(d * 60));
        return static_cast<int>(std::lround(d));
    }
    return 0;
}

json toJson(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
json toJson(const QJsonArray& a) {
    return json::parse(QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
QJsonObject toQt(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
}

json qvToJson(const QVariant& v) {
    const QJsonValue jv = QJsonValue::fromVariant(v);
    QJsonDocument doc(QJsonArray{jv});
    const json arr = json::parse(doc.toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return arr.is_array() && !arr.empty() ? arr[0] : json();
}

QVariant jsonToQv(const json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return static_cast<qlonglong>(v.get<long long>());
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) return QString::fromStdString(v.get<std::string>());
    return QJsonDocument::fromJson(QByteArray::fromStdString(v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).toVariant();
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

McpService::McpService(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), rt_(rt), vm_(vm), props_(std::make_unique<PropertiesModel>(rt)),
      settings_(std::make_unique<SettingsModel>(rt)) {
    // El cupo en caliente, como en la GUI: sin esto, settings_set con el cupo
    // reiniciaba la sala (y cortaba a todos los invitados).
    settings_->setCupoEnCaliente([this](int n) { return vm_.cupoSala().cambiar(n); });
    registerHelp();
    registerTools();
    registerImageTools();
    registerRoomTools();
    registerUiTools();
    registerYouTubeTools();
    registerTwitchTools();
    registerFacebookTools();
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
    vm_.setIntakeSink([this](const QString& id, const QString& name, bool yaAdentro) -> QString {
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
    // El mapa de categorias va en las `instructions` del initialize (lo arma
    // McpCore en cada initialize, porque depende del rol del cliente) y
    // cuesta unos 400 tokens.
    applySecurity();
    // Con todas las herramientas ya registradas (ver verificarJuegos). Los nombres
    // viejos de las renombradas en bloque van a su Tool::antes, y para eso tienen
    // que existir (ver McpRenombres.cpp).
    registrarRenombres(core_);
    core_.verificarJuegos();
}

bool McpService::roomHasUntrusted() {
    const json s = roomSnapshot(vm_);
    return !s.value("participants", json::array()).empty();
}

void McpService::applyApprovalPolicy() {
    const std::string p = rt_.config().mcpApproval;
    core_.setApprovalPolicy(p == "siempre" ? ApprovalPolicy::Always
                            : p == "nunca" ? ApprovalPolicy::Never
                                           : ApprovalPolicy::Untrusted);
}

void McpService::applySecurity() {
    // La salvaguarda cubre lo que viene de los invitados de la sala (WebRTC):
    // lo que escriben, sus nombres y datos, sus archivos y su video. El chat
    // de YouTube/Twitch/Facebook (y el Chat en vivo en pantalla) NO la dispara:
    // es texto del publico, que el asistente trata como dato (ver la guia).
    // Devuelven SIEMPRE archivos de invitados.
    // (invitados_chat_leer y guest_chat_pending marcan su respuesta solo si traen mensajes.)
    static const std::set<std::string> kAlways{"share_list", "compartida_revisar"};
    // Devuelven nombres, datos o video de invitados cuando hay alguien en la sala.
    static const std::set<std::string> kWithPeople{"room_status",    "remotos_fila_ver",   "foto_programa",
                                                   "foto_fuente", "foto_todas_escenas", "capture_feed",
                                                   "foto_invitado", "foto_todos_invitados",    "capture_sequence",
                                                   "capture_ui",     "feeds_list",        "dashboard_list"};
    // Su efecto lo decide una persona con un click: no necesitan aprobacion.
    // Mediados por la persona (ella decide en la barra) o limitados a enlaces
    // fijos de la guia: no hacen nada por si solos.
    static const std::set<std::string> kMediated{"operator_message", "operator_ask", "guest_reply_suggest",
                                                 "account_credentials_request", "account_open"};
    core_.forEachTool([this](Tool& t) {
        if (kAlways.count(t.name) != 0) t.untrustedOutput = true;
        if (kMediated.count(t.name) != 0) t.mediated = true;
        if (kWithPeople.count(t.name) != 0) {
            auto orig = t.run;
            t.run = [this, orig](const json& a, Done done) {
                const bool people = roomHasUntrusted();
                orig(a, [done, people](ToolResult r) {
                    if (people) r.untrusted = true;
                    done(r);
                });
            };
        }
    });
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
    const auto hayAire = [this]() { return rt_.engine().haySalida(); };
    core_.setHayAire(hayAire);
    core_.aire().setCondicion([this](const std::string&, const json& a) {
        const std::string e = a.value("entrada", std::string());
        if (e.empty()) return true;
        return rt_.engine().fuenteAlAire(e);
    });
    // El guion del evento llama herramientas (p. ej. el cue Pantalla): lo corre
    // el operador, así que cuenta como una persona.
    if (auto* ev = static_cast<EventoViewModel*>(vm_.evento()))
        ev->setHerramienta([this](const std::string& h, const json& args, std::function<void(bool, const std::string&)> fin) {
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
                                           {"priority", deAire || t.destructive ? "critical" : "warning"},
                                           {"title", deAire ? ba::i18n::traducir("mcp", "ACCIÓN DE AIRE: ¿la confirmás?")
                                                            : ba::i18n::traducir("mcp", "¿Aprobás esta acción del asistente?")},
                                           {"text", t.title + " (" + t.name + ")"},
                                           {"details", a},
                                           {"reason", reason},
                                           {"options", {aprobar, ba::i18n::traducir("mcp", "Rechazar")}},
                                           {"allowText", false}});
            msgs->waitAnswer(id, std::max(1, rt_.config().mcpAprobacionMin) * 60 * 1000, [msgs, id, decide, aprobar](json item) { // Configuración → Asistente (MCP)
                if (item.is_null()) msgs->dismiss(id); // vencio: la tarjeta se va y queda rechazada
                decide(!item.is_null() && item.value("answer", "") == aprobar);
            });
        });
}

McpService::~McpService() = default;

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
    if (!http_->listen(port)) {
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
    StudioViewModel& vm = vm_;
    const auto persist = [&rt]() { rt.save(); };
    // La invariante, puesta una vez y para todas: lo que se toca por MCP queda
    // guardado. El despachador la aplica a TODA herramienta que modifica algo,
    // asi ninguna superficie nueva depende de que su autor se acuerde. La vista
    // no se avisa desde aca: el runtime avisa sus cambios (AppRuntime::observar)
    // y StudioViewModel se entera solo. Ver McpCore::setSincronizador.
    core_.setSincronizador(persist);
    const auto sceneOr = [&rt](const json& a) {
        const std::string s = a.value("scene", std::string());
        return s.empty() ? rt.engine().currentScene() : s;
    };
    const auto sceneExists = [&rt](const std::string& s) {
        const auto names = rt.engine().sceneNames();
        return std::find(names.begin(), names.end(), s) != names.end();
    };
    const auto noScene = [&rt](const std::string& s) {
        std::string names;
        for (const auto& n : rt.engine().sceneNames())
            names += (names.empty() ? "" : ", ") + n;
        return ToolResult::error(
            ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'. Escenas: %2."), {s, names}));
    };
    const json sceneProp = {{"type", "string"},
                            {"description", ba::i18n::traducir("mcp", "Escena (por defecto, la que esta al aire).")}};
    const json layoutProp = {
        {"description", ba::i18n::traducir("mcp", "Ubicacion: preset (full, left, right, top, bottom, center, top-left, top-right, "
                        "bottom-left, bottom-right, pip-tl, pip-tr, pip-bl, pip-br, lower-third, third-1..3, "
                        "grid-<n>-<i>) u objeto {x,y,w,h} en fracciones del lienzo (0..1).")}};

    // --- Perfil (solo en un build con BA_PERFILAR: ver diag/Perfil) ----------------
    if (ba::diag::Perfil::activo()) {
        Tool t;
        t.name = "perfil";
        t.title = ba::i18n::traducir("mcp", "Dónde se va la CPU y la GPU (build de diagnóstico)");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "La última medición (cada segundo) de la app y del gateway: CPU total en núcleos (1,0 = un núcleo "
            "entero), memoria, los hilos que más consumen con su nombre (los de GStreamer se llaman como su "
            "elemento: así se ve qué parte del video gasta) y, en Windows, la GPU del proceso por motor (3D, "
            "VideoDecode, VideoEncode, Copy). El historial está en Logs/perfil-app.jsonl y "
            "Logs/perfil-gateway.jsonl; cada 10 s un resumen en el registro (log_leer, componente perfil).");
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
    {
        Tool t;
        t.name = "status";
        t.title = ba::i18n::traducir("mcp", "Estado general");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Estado del appliance: escena al aire, transmision y grabacion (con tiempos), fps, cuadros "
            "perdidos, codificador, CPU, niveles del programa, gateway, conferencia y oradores al aire. "
            "`en_vivo`: si sale algo de verdad (transmisión o grabación): es LO que dice si estás en vivo; "
            "el guion del evento corriendo sin transmitir no lo es. `salidas`: cada salida del aparato con su escena, "
            "su armada, si transmite o graba y su negro/placa (los campos de arriba son los de la por omisión). "
            "`disco`: para cuántas horas alcanza el disco al bitrate de todo lo que se graba (grabación, copia e ISO de "
            "todas las salidas, por disco) y si alguna copia va al mismo disco que su grabación. "
            "`modo_seguro`: si el launcher la arrancó en modo seguro tras varias caídas (nivel 1-3, qué piezas están "
            "apagadas y si las escenas se restauraron). `energia`: si se cortó la luz (batería o UPS, % y minutos que "
            "quedan) y si ya se cerraron las grabaciones por eso. "
            "Buen primer llamado.");
        t.run = [&rt, &vm](const json&, Done done) {
            const EngineStatus st = rt.engine().status();
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
            // El modo seguro escalonado (F1-10): nivel, qué se apagó y si las escenas volvieron.
            const ba::fault::ModoSeguro& ms = ba::fault::delProceso();
            json apagadas = json::array();
            for (ba::fault::Pieza p : ms.apagadas()) apagadas.push_back(ba::fault::clave(p));
            done(ToolResult().data(
                json{{"currentScene", rt.engine().currentScene()},
                     {"modo_seguro",
                      {{"activo", ms.activo()}, {"nivel", ms.nivel}, {"apagadas", apagadas}, {"escenas", !ms.escenasVacias()}}},
                     {"transition",
                      {{"type", rt.config().transition.type}, {"ms", rt.config().transition.durationMs}}},
                     {"streaming", st.streaming},
                     {"streamTime", vm.streamTime().toStdString()},
                     {"streamKbps", vm.streamKbps()},
                     {"streamServer", vm.streamServer().toStdString()},
                     {"streamKeyAvailable", vm.streamKeyAvailable()},
                     {"recording", st.recording},
                     {"recordTime", vm.recordTime().toStdString()},
                     // EN VIVO = sale algo de verdad (transmisión, señal limpia o la grabación si
                     // el modo en vivo la cuenta): lo mismo que usan la app y los bloqueos. El
                     // guion del evento corriendo sin transmitir NO es estar en vivo.
                     {"en_vivo", vm.modoVivo()},
                     {"fps", st.fps},
                     {"droppedFrames", st.droppedFrames},
                     {"totalFrames", st.totalFrames},
                     {"videoEncoder", st.videoEncoder},
                     {"encoder_respaldo_activo", rt.engine().encoderRespaldoActivo()},
                     {"cpuPercent", vm.cpuPercent()},
                     {"programPeakDb", {vm.programPeakL(), vm.programPeakR()}},
                     {"canvas", vm.canvasLabel().toStdString()},
                     {"gatewayAlive", vm.gatewayAlive()},
                     {"lastError", st.lastError},
                     {"latido", rt.latido().estado()},
                     {"salidas", resumenSalidas(rt, vm)},
                     {"disco", rt.discoGrabacion()},
                     // El corte de luz (F1-11): batería/UPS, lo que queda y qué hizo la política.
                     {"energia", vm.energia() != nullptr ? vm.energia()->estado() : json()},
                     {"room", room}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_ftb";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Fundido a negro (FTB)");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "manda la salida a negro");
        t.aireSi = AireSi::AlActivar;
        t.description = ba::i18n::traducir("mcp", "Fundido a negro de TODO lo que sale (encima del programa, sin tocar las escenas). `activar` "
                        "true/false, `ms` de fundido (de fábrica 800), `audio`: bajar (de fábrica: el audio baja con "
                        "el negro) o mantener. Activarlo al aire es una acción de aire (pide confirmación si lo pide "
                        "algo automático); sacarlo, no. Cada salida tiene su negro (`salida`).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}},
                           {"audio", {{"type", "string"}, {"enum", {"bajar", "mantener"}}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            auto& s = vm.seguridadAire();
            const std::string salida = core_.salidaDe(a);
            if (const std::string e =
                    s.ftb(a.value("activar", false), a.value("ms", 800), a.value("audio", std::string("bajar")), salida);
                !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data(s.estado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_placa";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Placa de seguridad");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "tapa la salida con una placa");
        t.aireSi = AireSi::AlActivar;
        t.description = ba::i18n::traducir("mcp", "Una placa encima de todo lo que sale (\"Cuarto intermedio\", \"Volvemos enseguida\", \"Minuto "
                        "de silencio\"…), con `texto`, una `imagen` opcional (ruta) y `ms` de fundido (600). No depende "
                        "de las escenas. Activarla al aire pide confirmación si la pide algo automático; sacarla, no. "
                        "Sobrevive a una caída de la app. Cada salida tiene su placa (`salida`).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"texto", {{"type", "string"}}},
                           {"imagen", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            auto& s = vm.seguridadAire();
            const std::string salida = core_.salidaDe(a);
            if (const std::string e = s.placa(a.value("activar", false), a.value("texto", std::string()),
                                              a.value("imagen", std::string()), a.value("ms", 600), salida);
                !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data(s.estado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_placas_listar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Textos de placa y estado");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los textos de placa de siempre y si ahora hay FTB o placa al aire.");
        t.run = [this, &vm](const json& a, Done done) {
            json textos = json::array();
            for (const QString& s : vm.textosPlaca()) textos.push_back(s.toStdString());
            done(ToolResult().data(json{{"textos", textos}, {"estado", vm.seguridadAire().estado(core_.salidaDe(a))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escenas_ajustes";
        t.title = ba::i18n::traducir("mcp", "Ajustes del editor de escenas");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`corte_directo_editor`: el botón de cada escena en el editor corta directo (true) o la arma "
                        "en el preview para salir con TAKE (false, de fábrica). Sin argumentos, los muestra.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"corte_directo_editor", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("corte_directo_editor")) vm.setCorteDirectoEditor(a.value("corte_directo_editor", false));
            done(ToolResult().data(json{{"corte_directo_editor", vm.corteDirectoEditor()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_aire_lista";
        t.title = ba::i18n::traducir("mcp", "Herramientas de aire");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Las herramientas que cambian el aire (cortar, cambiar de escena, sacar a alguien, parar la "
            "transmisión…). Con la transmisión o la grabación andando, si las pide algo automático (asistente, "
            "plugin, receta, disparador, voz) esperan SIEMPRE que una persona confirme. `clase`: aire (siempre) "
            "o aire_si (según los argumentos: settings de gateway/video/encoder, mute del máster, sacar un "
            "orador, PTZ de la cámara al aire). `hay_aire`: si ahora cuentan. En vivo, la voz y los disparadores "
            "solo leen o hacen lo reversible sin preguntar.");
        t.run = [this](const json&, Done done) {
            done(ToolResult().data(json{{"hay_aire", core_.hayAire()}, {"herramientas", core_.aire().lista()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_aire_ajustar";
        t.title = ba::i18n::traducir("mcp", "Sumar o sacar una herramienta de las de aire");
        t.category = "estado";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`clase`: aire (pide confirmación en vivo) o normal. Las de la lista fija no bajan. Solo "
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
        t.title = ba::i18n::traducir("mcp", "Política del supervisor (interfaz trabada)");
        t.category = "estado";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "El supervisor relanza la app si deja de latir. El latido sale de la interfaz Y de un hilo que mira "
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

    // --- Escenas -------------------------------------------------------------------
    {
        Tool t;
        t.name = "scene_list";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Listar escenas");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "El listado de escenas que existen: cual esta al aire, en que carpeta esta cada una. "
            "Para el contenido de UNA escena, capa por capa, no es esta. "
            "Todas las escenas con sus capas (de abajo hacia arriba), cual esta al aire y en que "
            "carpeta esta cada una (`folder`, \"\" = suelta). Las carpetas solo ordenan la lista de la "
            "pestaña Escenas: no cambian nada de la emision.\n\n"
            "`live` es la que esta AL AIRE; `armada` es la del bus de preview, la que sale con el "
            "proximo `preview_al_aire`. Son los dos buses de una mesa de switcher. Con `salida`, los de esa salida "
            "(cada una tiene su aire y su armada; las escenas son de todas).");
        t.run = [this, &rt, &vm](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            const std::string armada = conmutadorDe(core_, vm, a).armada();
            const std::string alAire = rt.engine().currentScene(salida);
            json scenes = json::array();
            for (const std::string& n : rt.orderedSceneNames()) {
                scenes.push_back(json{{"name", n},
                                      {"live", n == alAire},
                                      {"armada", n == armada},
                                      {"folder", rt.sceneFolder(n)},
                                      {"sources", rt.engine().sceneSources(n)}});
            }
            done(ToolResult().data(json{{"current", alAire},
                                        {"armada", armada},
                                        {"folders", rt.sceneFolders()},
                                        {"scenes", scenes}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_folder";
        t.title = ba::i18n::traducir("mcp", "Mover una escena a una carpeta");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "QUE ES: las carpetas agrupan escenas en la lista de la pestaña Escenas (arbol que se "
            "pliega). PARA QUE: ordenar cuando hay muchas (bloques del programa, placas, invitados). "
            "COMO SE USA: `folder` con el nombre de la carpeta (se crea sola si no existe) o vacio para "
            "sacar la escena de toda carpeta. No cambia nada de la emision ni del contenido de la "
            "escena; `scene_list` informa la carpeta de cada una.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", {{"type", "string"}}}, {"folder", {{"type", "string"}}}}},
                         {"required", {"scene"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.setSceneFolder(a.value("scene", ""), a.value("folder", "")))
                return done(ToolResult::error(rt.lastError()));
            rt.save();
            done(ToolResult().data(json{{"scene", a.value("scene", "")},
                                        {"folder", a.value("folder", "")},
                                        {"folders", rt.sceneFolders()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_move";
        t.title = ba::i18n::traducir("mcp", "Reordenar una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "QUE ES: cambia el ORDEN de la lista de escenas (y de paso la carpeta). PARA QUE: dejar las "
            "escenas en el orden del programa. COMO SE USA: `before` es la escena delante de la cual "
            "queda (vacio = al final); ademas hereda su carpeta. No cambia nada de la emision. El orden "
            "que informa `scene_list` es el que ve el operador.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", {{"type", "string"}}}, {"before", {{"type", "string"}}}}},
                         {"required", {"scene"}}};
        t.run = [&rt](const json& a, Done done) {
            if (!rt.moveScene(a.value("scene", ""), a.value("before", "")))
                return done(ToolResult::error(rt.lastError()));
            rt.save();
            done(ToolResult().data(json{{"orden", rt.orderedSceneNames()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_switch";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Poner una escena al aire (corte directo)");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia la escena al aire");
        t.description = ba::i18n::traducir("mcp", "Pone una escena al aire YA, sin pasar por el preview: es «corta a la camara 2», "
                        "«pone la apertura». Cambia la escena del programa (con la transicion configurada). "
                        "Lo que se ve y oye en la "
                        "transmision cambia EN EL ACTO, sin pasar por el bus de preview.");
        t.details =
            ba::i18n::traducir("mcp", "Es el corte de urgencia: sale ya. Para lo demas, la forma de trabajar de una mesa es "
            "`scene_preview` (armar el proximo plano) -> mirarlo con `foto_fuente` -> `preview_al_aire`. "
            "Dos pasos parece mas lento y es al reves: lo que esta roto se ve ANTES de salir al aire, "
            "no despues.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Nombre de la escena.")}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Entrevista"})"};
        t.run = [this, &vm, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            if (const std::string e = conmutadorDe(core_, vm, a).poner(name); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Al aire: %1"), {name})));
        };
        core_.addTool(std::move(t));
    }
    // --- El bus de preview: armar, mirar, TAKE --------------------------------
    {
        Tool t;
        t.name = "scene_preview";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Armar la proxima escena (sin sacarla al aire)");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Pone una escena en el bus de PREVIEW: queda armada para el proximo `preview_al_aire`. NO cambia "
            "lo que se ve en la transmision.");
        t.details =
            ba::i18n::traducir("mcp", "Asi se opera una mesa de switcher: se arma el proximo plano, se MIRA (`foto_fuente` con "
            "el mismo nombre da la foto de como va a salir), y recien ahi `preview_al_aire`.\n\n"
            "Armar la escena que YA esta al aire no tiene sentido (el take cortaria a lo mismo) y se "
            "rechaza.\n\n"
            "`scene_list` dice cual esta armada y cual al aire.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.examples = {R"({"name": "06 Debate invitados"})"};
        t.run = [this, &vm, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            if (const std::string e = conmutadorDe(core_, vm, a).armar(name); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se armo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Armada: %1. Mirala con foto_fuente y sacala con preview_al_aire."), {name})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_cut";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "CUT: corte seco a la armada");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la armada al aire");
        t.description = ba::i18n::traducir("mcp", "Pone al aire la escena armada con un CORTE (sin transicion), aunque la global sea un fundido. "
                        "Deja armada la que salia.");
        t.run = [this, &vm](const json& a, Done done) {
            escenas::Conmutador& c = conmutadorDe(core_, vm, a);
            const std::string armada = c.armada();
            if (armada.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay nada armado (scene_preview).")));
            if (const std::string e = c.take("cut", 0); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "AL AIRE (corte): %1"), {armada})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transicion_manual";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "T-bar: transicion a mano");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "lleva la transición a mano (T-bar)");
        t.description = ba::i18n::traducir("mcp", "Lleva la transicion hacia la escena armada a mano: `t` de 0 a 1 (la palanca). Con `soltar`, "
                        "termina: pasada la mitad queda al aire, si no vuelve. Para un fader de la botonera: "
                        "{\"t\": \"{{valor}}\"} con el fader de 0 a 1.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"t"}},
                         {"properties", {{"t", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}, {"soltar", {{"type", "boolean"}}}}}};
        t.run = [this, &rt, &vm](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (const std::string e = conmutadorDe(core_, vm, a).tbar(a.value("t", 0.0), a.value("soltar", false)); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data({{"al_aire", rt.engine().currentScene(salida)}, {"hacia", rt.engine().manualHacia(salida)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escena_transicion";
        t.title = ba::i18n::traducir("mcp", "Transicion de entrada de una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "La transicion con la que ENTRA la `escena` al aire (`tipo`: cut, fade, swipe, slide, "
                        "fade_to_color; vacio = la global) y su duracion `ms`. Un take con transicion explicita manda sobre esta.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"escena"}},
                         {"properties",
                          {{"escena", {{"type", "string"}}},
                           {"tipo", {{"type", "string"}, {"enum", {"", "cut", "fade", "swipe", "slide", "fade_to_color"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.conmutador().setTransicionEntrada(a.value("escena", std::string()),
                                                                       a.value("tipo", std::string()), a.value("ms", -1));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_afv";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Audio que sigue al video");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Con `sigue_video`, la `fuente` sube (a su volumen) cuando entra al aire una escena que la tiene y "
                        "baja a cero cuando sale, con la duracion de la transicion. Sin `sigue_video`, lista las que lo tienen. "
                        "Es de cada salida: mueve el fader de la fuente en la mezcla de esa `salida`.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"fuente", {{"type", "string"}}}, {"sigue_video", {{"type", "boolean"}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            escenas::Conmutador& c = conmutadorDe(core_, vm, a);
            if (a.contains("fuente") && a.contains("sigue_video"))
                c.setAfv(a.value("fuente", std::string()), a.value("sigue_video", false));
            json l = json::array();
            for (const std::string& f : c.afvFuentes()) l.push_back(f);
            done(ToolResult().data({{"sigue_video", l}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "preview_al_aire";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "TAKE: sacar al aire la escena armada");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "pasa el preview al aire");
        t.description = ba::i18n::traducir("mcp", "Pasa al aire la escena que estaba ARMADA en preview, con la transición configurada. Deja armada la "
            "que salia, para poder volver de un golpe.");
        t.details = ba::i18n::traducir("mcp", "Sin nada armado no hace nada y lo dice: armar primero con `scene_preview`.\n\n"
                    "Que el plano que SALE del aire quede armado es como funciona cualquier mesa: "
                    "despues de un take, volver a lo anterior es un solo take mas.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"transicion", {{"type", "string"}, {"enum", {"cut", "fade", "swipe", "slide", "fade_to_color"}},
                                           {"description", ba::i18n::traducir("mcp", "otra transicion solo para este take (si no, la de entrada de la escena o la global)")}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            escenas::Conmutador& c = conmutadorDe(core_, vm, a);
            const std::string armada = c.armada();
            if (armada.empty())
                return done(ToolResult::error(
                    ba::i18n::traducir("mcp", "No hay nada armado. Arma el proximo plano con scene_preview {\"name\": ...}.")));
            if (const std::string e = c.take(a.value("transicion", std::string()), a.value("ms", -1)); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "AL AIRE: %1. Queda armada: %2."), {armada, c.armada()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_create";
        t.title = ba::i18n::traducir("mcp", "Crear escena vacia");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Crea una escena vacia. Para crearla con contenido de una vez, usa scene_define.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            if (!rt.addScene(a["name"].get<std::string>())) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena creada: %1"), {a["name"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_copy";
        t.title = ba::i18n::traducir("mcp", "Copiar una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Crea una escena nueva con todas las fuentes de otra.");
        t.details = ba::i18n::traducir("mcp", "Es como se arman las escenas parecidas sin repetir el trabajo: se hace UNA bien —la de "
                    "entrevista, con su camara, su zocalo y su fondo— y las demas salen de copiarla y "
                    "cambiarle lo que difiere.\n\n"
                    "Las fuentes se RECREAN, no se comparten: quedan dos escenas de verdad independientes, y "
                    "mover algo en la copia no mueve el original.\n\n"
                    "La copia nunca queda protegida, aunque el original lo este: para eso se copia. Si "
                    "alguna fuente no se pudo recrear, la escena igual queda creada y se dice cual fallo: "
                    "una escena a medias se arregla a mano, una que desaparecio sin explicacion no.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"from", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "La escena que se copia.")}}},
                           {"to", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Nombre de la copia.")}}}}},
                         {"required", {"from", "to"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string from = a["from"].get<std::string>();
            const std::string to = a["to"].get<std::string>();
            if (!rt.copyScene(from, to)) return done(ToolResult::error(rt.lastError()));
            persist();
            // `lastError` con la copia hecha = alguna fuente no se pudo
            // recrear. Se dice, pero no es un error: la escena existe.
            const std::string aviso = rt.lastError();
            done(ToolResult::ok(
                aviso.empty() ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena «%1» copiada de «%2»."), {to, from})
                              : ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena «%1» copiada de «%2». Ojo: %3"),
                                                 {to, from, aviso})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_protect";
        t.title = ba::i18n::traducir("mcp", "Proteger una escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Una escena protegida no se puede borrar ni renombrar.");
        t.details = ba::i18n::traducir("mcp", "Para la escena BASE de la que se copian las demas —la que si alguien borra se pierde el "
                    "trabajo de todos— y para las que no se tocan en vivo.\n\n"
                    "Copiarla si se puede, y editar sus fuentes tambien: lo que se protege es que "
                    "desaparezca o cambie de nombre, que es lo que rompe todo lo que la nombra (el guion, "
                    "los vinculos de rotulos, las recetas).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", {{"type", "string"}}}, {"protected", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"scene"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string s = a["scene"].get<std::string>();
            const bool p = a.value("protected", true);
            if (!rt.setSceneProtected(s, p)) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(p ? ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» queda protegida: no se borra ni se renombra."), {s})
                                  : ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» ya no esta protegida."), {s})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_delete";
        t.title = ba::i18n::traducir("mcp", "Borrar escena");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una escena");
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra una escena (las fuentes compartidas con otras escenas siguen existiendo).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt, persist, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            if (!rt.removeScene(name)) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena borrada: %1"), {name})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_rename";
        t.title = ba::i18n::traducir("mcp", "Renombrar escena");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"from", {{"type", "string"}}}, {"to", {{"type", "string"}}}}},
                         {"required", {"from", "to"}}};
        t.description = ba::i18n::traducir("mcp", "Cambia el nombre de una escena.");
        t.run = [&rt, persist, sceneExists, noScene](const json& a, Done done) {
            if (!sceneExists(a["from"])) return done(noScene(a["from"]));
            if (!rt.renameScene(a["from"], a["to"])) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Renombrada a %1"), {a["to"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_export";
        t.title = ba::i18n::traducir("mcp", "Exportar definicion de escenas");
        t.category = "escenas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Definicion completa (tipo, settings, ubicacion, visibilidad, volumen) de una escena o de "
            "todas, en el mismo formato que acepta scene_define: sirve para copiar, versionar o "
            "modificar escenas.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Una escena (vacio = todas).")}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string only = a.value("name", std::string());
            json out = json::array();
            for (const SceneConfig& sc : rt.config().scenes) {
                if (!only.empty() && sc.name != only) continue;
                json sources = json::array();
                for (const SourceSpec& s : sc.sources) {
                    const std::string live = rt.engine().sourceSettingsJson(s.name);
                    sources.push_back(
                        json{{"type", s.id},
                             {"name", s.name},
                             {"settings", json::parse(live.empty() ? s.settingsJson : live, nullptr, false)},
                             {"layout", {{"x", s.rect.x}, {"y", s.rect.y}, {"w", s.rect.w}, {"h", s.rect.h}}},
                             {"visible", s.visible},
                             {"volume", s.volume},
                             {"muted", s.muted}});
                }
                out.push_back(json{{"name", sc.name}, {"sources", sources}});
            }
            if (!only.empty() && out.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'."), {only})));
            done(ToolResult().data(json{{"scenes", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_define";
        t.porSalida = true; // `switch`: al aire en esa salida
        t.title = ba::i18n::traducir("mcp", "Definir una escena completa");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Crea (o rehace con replace=true) una escena con todas sus fuentes de una vez, en orden de "
            "abajo hacia arriba. Una fuente con el nombre de otra que ya existe se COMPARTE (misma "
            "camara en dos escenas); si trae settings, se actualizan. Al rehacer, las que vuelven a venir "
            "en la lista se CONSERVAN (con su camara, grafo y filtros): solo se quitan las que no estan. "
            "`layout` en fracciones del lienzo (0..1); si llega en pixeles se convierte y se avisa.");
        t.details =
            ba::i18n::traducir("mcp", "Cada fuente: {type, name, settings?, layout?, visible?, volume?, muted?}. `type` es el id del "
            "tipo (ver source_types): p. ej. image_source {file}, color_source_v3 {color, width, "
            "height}, text_ft2_source_v2 {text, font:{face,size}, color1}, ffmpeg_source {local_file, "
            "looping}, v4l2_input (camara), ba_guest_video {slot, channel: guest|share}, ba_subtitulos "
            "{flujo: \"Orador · en\", "
            "lineas, caracteres, ancho} (subtitulos de una salida de texto). `layout`: preset "
            "o {x,y,w,h}. Consejo: exporta una escena con scene_export, modificala y pasala aca.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}}},
              {"sources",
               {{"type", "array"},
                {"description", ba::i18n::traducir("mcp", "Fuentes de abajo hacia arriba.")},
                {"items",
                 {{"type", "object"},
                  {"properties",
                   {{"type", {{"type", "string"}}},
                    {"name", {{"type", "string"}}},
                    {"settings", {{"type", "object"}}},
                    {"layout", layoutProp},
                    {"visible", {{"type", "boolean"}}},
                    {"volume", {{"type", "number"}, {"description", ba::i18n::traducir("mcp", "Ganancia lineal (1 = 0 dB).")}}},
                    {"muted", {{"type", "boolean"}}}}},
                  {"required", {"type", "name"}}}}}},
              {"replace",
               {{"type", "boolean"},
                {"description", ba::i18n::traducir("mcp", "Si existe, rehacerla (las fuentes que se repiten se conservan).")},
                {"default", false}}},
              {"switch",
               {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "Ponerla al aire al terminar.")}, {"default", false}}}}},
            {"required", {"name", "sources"}}};
        // La tipografia del ejemplo sale de kDefaultFontFace: si aca quedara
        // fija en "Sans", el asistente copiaria el ejemplo y en Windows crearia
        // texto invisible (FT2 no resuelve ese alias de fontconfig).
        t.examples = {std::string(R"({"name": "Titulo", "sources": [
  {"type": "color_source_v3", "name": "Fondo azul", "settings": {"color": 4282400832, "width": 1920, "height": 1080}, "layout": "full"},
  {"type": "text_ft2_source_v2", "name": "Titulo principal", "settings": {"text": "Bienvenidos", "font": {"face": ")") +
                      kDefaultFontFace +
                      R"(", "size": 96}}, "layout": {"x": 0.1, "y": 0.4, "w": 0.8, "h": 0.2}}
], "switch": true})"};
        t.run = [this, &rt, &vm, persist, sceneExists](const json& a, Done done) {
            const std::string name = a["name"];
            std::vector<std::string> log;
            // Las que vienen en la lista nueva: si ya están en la escena, se
            // CONSERVAN (vaciar y volver a agregar borraba una entrada que vivía
            // solo acá y la recreaba vacía, sin su cámara ni su grafo).
            std::set<std::string> listadas;
            for (const json& s : a["sources"])
                listadas.insert(s.value("name", std::string()));
            std::vector<std::string> antes;
            if (sceneExists(name)) {
                if (!a.value("replace", false))
                    return done(
                        ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La escena '%1' ya existe: usa replace=true para "
                                                                               "rehacerla, u otro nombre."),
                                                           {name})));
                antes = rt.engine().sceneSources(name);
                int quitadas = 0;
                for (const std::string& s : antes)
                    if (!listadas.count(s)) {
                        rt.removeSource(name, s);
                        ++quitadas;
                    }
                log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "rehecha (%1 quitada(s); las que siguen, conservadas)"), {std::to_string(quitadas)}));
            } else if (!rt.addScene(name)) {
                return done(ToolResult::error(rt.lastError()));
            }
            int added = 0;
            for (const json& s : a["sources"]) {
                SourceSpec spec;
                spec.id = s.value("type", std::string());
                spec.name = s.value("name", std::string());
                spec.settingsJson = s.contains("settings") ? s["settings"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : "{}";
                spec.visible = s.value("visible", true);
                spec.volume = s.value("volume", 1.0f);
                spec.muted = s.value("muted", false);
                std::string err, nota;
                if (s.contains("layout") &&
                    !parseLayout(s["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err, &nota)) {
                    log.push_back(spec.name + ": " + err);
                    continue;
                }
                if (!nota.empty()) log.push_back(spec.name + ": " + nota);
                if (std::find(antes.begin(), antes.end(), spec.name) != antes.end()) {
                    // Ya estaba en la escena: se le aplica lo que trae el pedido.
                    if (s.contains("layout")) rt.setSourceRect(name, spec.name, spec.rect);
                    rt.setSourceVisible(name, spec.name, spec.visible);
                    if (s.contains("settings")) {
                        json cur = json::parse(rt.engine().sourceSettingsJson(spec.name), nullptr, false);
                        if (!cur.is_object()) cur = json::object();
                        cur.merge_patch(s["settings"]);
                        rt.updateSourceSettings(spec.name, cur.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                    }
                    if (s.contains("volume")) rt.setVolume(spec.name, spec.volume);
                    if (s.contains("muted")) rt.setMuted(spec.name, spec.muted);
                    ++added;
                    log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: conservada (ya estaba)"), {spec.name}));
                    continue;
                }
                const bool existed = !rt.engine().sourceTypeId(spec.name).empty();
                if (!rt.addSource(name, spec)) {
                    log.push_back(spec.name + ": " + rt.lastError());
                    continue;
                }
                if (existed && s.contains("settings")) rt.updateSourceSettings(spec.name, spec.settingsJson);
                if (existed) {
                    rt.setVolume(spec.name, spec.volume);
                    rt.setMuted(spec.name, spec.muted);
                }
                ++added;
                if (existed) log.push_back(ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: compartida (ya existia)"), {spec.name}));
            }
            // El orden, el de la lista (de abajo hacia arriba): cada una, arriba de todo.
            {
                const auto ahora = rt.engine().sceneSources(name);
                for (const json& s : a["sources"]) {
                    const std::string n = s.value("name", std::string());
                    if (std::find(ahora.begin(), ahora.end(), n) == ahora.end()) continue;
                    for (std::size_t i = 0; i < ahora.size(); ++i)
                        rt.moveSource(name, n, 1);
                }
            }
            // Por el conmutador (modelo), como scene_switch: transición de entrada y AFV.
            if (a.value("switch", false))
                if (const std::string e = conmutadorDe(core_, vm, a).poner(name); !e.empty()) // `switch`: en su salida
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena armada, pero no salió al aire: %1"), {e})));
            persist();
            std::string msg = ba::i18n::llenar(ba::i18n::traducir("mcp", "Escena '%1': %2 fuente(s)."), {name, std::to_string(added)});
            for (const auto& l : log)
                msg += "\n- " + l;
            msg += "\n" + ba::i18n::llenar(ba::i18n::traducir("mcp", "Verifica como quedo con foto_fuente {\"name\": \"%1\"}."), {name});
            done(ToolResult::ok(msg));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transition_set";
        t.title = ba::i18n::traducir("mcp", "Transicion entre escenas");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia la transición");
        t.description = ba::i18n::traducir("mcp", "Tipo y duracion de la transicion al cambiar de escena.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"type", {{"type", "string"}, {"enum", {"cut", "fade", "swipe", "slide", "fade_to_color"}}}},
              {"durationMs", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}}}}},
            {"required", {"type"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            if (!rt.setTransition(a["type"], a.value("durationMs", rt.config().transition.durationMs)))
                return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Transicion: %1"), {a["type"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }

    // --- Fuentes ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "source_types";
        t.title = ba::i18n::traducir("mcp", "Tipos de fuente disponibles");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Tipos de fuente que esta instalacion puede crear (id, nombre, si tiene video "
            "y/o audio). El id es el `type` de crear_fuente / scene_define.");
        t.run = [&rt](const json&, Done done) {
            json out = json::array();
            for (const SourceType& s : rt.engine().inputTypes())
                out.push_back(
                    json{{"type", s.id}, {"name", s.displayName}, {"video", s.video}, {"audio", s.audio}});
            done(ToolResult().data(json{{"types", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escena_fuentes_ver";
        t.title = ba::i18n::traducir("mcp", "Fuentes de una escena");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description = ba::i18n::traducir("mcp", "Las fuentes que hay ADENTRO de una escena, una por una; no son las capas del DSK. Fuentes de una "
            "escena (de abajo hacia arriba) con tipo, visibilidad, ubicacion en el lienzo, volumen y silencio.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"scene", sceneProp}}}};
        t.run = [&rt, sceneOr, sceneExists, noScene](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            if (!sceneExists(scene)) return done(noScene(scene));
            json out = json::array();
            for (const std::string& n : rt.engine().sceneSources(scene)) {
                json s{{"name", n},
                       {"type", rt.engine().sourceTypeId(n)},
                       {"visible", rt.engine().sourceVisible(scene, n)},
                       {"volume", rt.engine().sourceVolume(n)},
                       {"muted", rt.engine().sourceMuted(n)}};
                if (SourceSpec* spec = rt.findSpec(scene, n))
                    s["layout"] = {
                        {"x", spec->rect.x}, {"y", spec->rect.y}, {"w", spec->rect.w}, {"h", spec->rect.h}};
                if (const EfectosCapa ef = rt.engine().sourceEfectos(scene, n); !ef.vacio()) s["efectos"] = ef.aJson();
                out.push_back(s);
            }
            done(ToolResult().data(json{{"scene", scene}, {"sources", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "crear_fuente";
        t.title = ba::i18n::traducir("mcp", "Agregar fuente");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
             ba::i18n::traducir("mcp", "CREA una fuente nueva en una escena: texto, título, zócalo, cámara, color, invitado; un archivo de "
            "medios va con medio_poner. Si ya existe una fuente con ese nombre (en otra escena) se agrega LA "
            "MISMA (compartida).");
        t.details =
            ba::i18n::traducir("mcp", "Settings mas comunes: image_source {file: ruta}, ffmpeg_source {local_file, looping: true}, "
            "color_source_v3 {color: ABGR entero, width, height}, text_ft2_source_v2 {text, font: {face, "
            "size}, color1}, v4l2_input {device_id}. Para ver todas las opciones de una fuente ya creada: "
            "source_properties.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"scene", sceneProp},
                           {"type", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Id del tipo (source_types).")}}},
                           {"name", {{"type", "string"}}},
                           {"settings", {{"type", "object"}}},
                           {"layout", layoutProp},
                           {"visible", {{"type", "boolean"}, {"default", true}}},
                           {"efectos",
                            {{"type", "object"},
                             {"description", ba::i18n::traducir("mcp", "transparencia, borde difuso, esquinas y sombra de entrada (lo mismo que "
                                             "source_update {efectos}).")}}}}},
                         {"required", {"type", "name"}}};
        t.examples = {
            R"({"type": "image_source", "name": "Logo", "settings": {"file": "/home/yo/logo.png"}, "layout": "pip-tr"})"};
        t.run = [&rt, persist, sceneOr, sceneExists, noScene](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            if (!sceneExists(scene)) return done(noScene(scene));
            SourceSpec spec;
            spec.id = a["type"];
            spec.name = a["name"];
            spec.settingsJson = a.contains("settings") ? a["settings"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : "{}";
            spec.visible = a.value("visible", true);
            spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            if (a.contains("efectos")) {
                std::string e;
                if (!spec.efectos.aplicar(a["efectos"], &e)) return done(ToolResult::error(e));
            }
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Agregada '%1' a '%2'."), {spec.name, scene})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "captura_opciones";
        t.title = ba::i18n::traducir("mcp", "Qué pantallas y qué aplicaciones se pueden capturar");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Las PANTALLAS conectadas (para screen_capture: su `monitor`) y las VENTANAS de aplicaciones abiertas "
            "(para window_capture: su `ventana`, que es «aplicación\\ntítulo»), y si este sistema puede capturar "
            "una ventana sola. Para elegir antes de crear_fuente.");
        t.examples = {R"({})"};
        t.run = [](const json&, Done done) {
            json ps = json::array(), vs = json::array();
            for (const auto& p : video::pantallasDelEquipo())
                ps.push_back({{"monitor", p.nombre}, {"etiqueta", p.etiqueta}, {"principal", p.principal},
                              {"ancho", p.pxAncho}, {"alto", p.pxAlto}});
            for (const auto& w : video::ventanasParaElegir()) vs.push_back(w);
            std::string porque;
            const bool hay = ba::motor::hayCapturaDeVentana(&porque);
            json d{{"pantallas", ps}, {"ventanas", vs}, {"captura_de_ventana", hay}};
            if (!hay) d["por_que_no"] = porque;
            d["como"] = "crear_fuente {type: screen_capture, settings: {monitor}} o {type: window_capture, settings: {ventana}}";
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_remove";
        t.title = ba::i18n::traducir("mcp", "Quitar fuente de una escena");
        t.category = "fuentes";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una fuente (puede estar al aire)");
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Saca la fuente de la escena (si esta en otras escenas, ahi sigue).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", sceneProp}, {"name", {{"type", "string"}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, persist, sceneOr](const json& a, Done done) {
            if (!rt.removeSource(sceneOr(a), a["name"])) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Quitada: %1"), {a["name"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_update";
        t.title = ba::i18n::traducir("mcp", "Modificar una fuente en una escena");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Cambia una fuente: mostrar/ocultar, mover o cambiar tamaño (layout), orden (order), cambiar "
            "el texto de un titulo o zocalo, la imagen, transparencia, borde difuso o sombra (efectos), etc. "
            "De una vez lo que se indique: visibilidad, ubicacion (layout), orden (order), "
            "settings (se MEZCLAN con los actuales), efectos (se MEZCLAN), volumen, silencio o nombre.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", sceneProp},
              {"name", {{"type", "string"}}},
              {"visible", {{"type", "boolean"}}},
              {"layout", layoutProp},
              {"order",
               {{"type", json::array({"string", "integer"})},
                {"description", ba::i18n::traducir("mcp", "top | bottom | up | down | pasos (+ sube).")}}},
              {"settings", {{"type", "object"}, {"description", ba::i18n::traducir("mcp", "Solo las claves a cambiar.")}}},
              {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
              {"muted", {{"type", "boolean"}}},
              {"rename", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Nuevo nombre (en todas las escenas).")}}},
              {"recorte",
               {{"type", "object"},
                {"description",
                 ba::i18n::traducir("mcp", "Lo que se ve de la fuente en ESTA escena: {izquierda, arriba, derecha, abajo} en fracciones de "
                 "su tamaño (0..1). Con el layout, amplía una zona (p. ej. comparar el mismo detalle). Todo en 0 "
                 "lo saca.")},
                {"properties",
                 {{"izquierda", {{"type", "number"}}},
                  {"arriba", {{"type", "number"}}},
                  {"derecha", {{"type", "number"}}},
                  {"abajo", {{"type", "number"}}}}}}},
              {"efectos",
               {{"type", "object"},
                {"description",
                 ba::i18n::traducir("mcp", "Efectos de la fuente en ESTA escena (solo lo que se indique; lo demas queda). Medidas en pixeles "
                 "del lienzo. opacidad 0..1 (transparencia de todo el elemento); borde: borde difuso (pixeles que se "
                 "funden a transparente); esquinas: radio de las esquinas redondeadas; sombra: {activa, color \"#RRGGBB\", alfa 0..1 (su opacidad), difuminado "
                 "(radio), distancia, angulo (hacia donde cae: 0 derecha, 90 abajo, 45 abajo a la derecha), tamano "
                 "(mas grande o mas chica que el elemento)}. Pasar numeros de la sombra la prende; "
                 "{\"sombra\": false} la apaga. Resplandor: distancia 0, color claro, difuminado grande.")},
                {"properties",
                 {{"opacidad", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                  {"borde", {{"type", "number"}, {"minimum", 0}}},
                  {"esquinas", {{"type", "number"}, {"minimum", 0}}},
                  {"sombra",
                   {{"type", json::array({"object", "boolean"})},
                    {"properties",
                     {{"activa", {{"type", "boolean"}}},
                      {"color", {{"type", "string"}}},
                      {"alfa", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                      {"difuminado", {{"type", "number"}, {"minimum", 0}}},
                      {"distancia", {{"type", "number"}, {"minimum", 0}}},
                      {"angulo", {{"type", "number"}}},
                      {"tamano", {{"type", "number"}}}}}}}}}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Logo", "layout": "pip-br", "visible": true})",
                      R"({"name": "Camara", "efectos": {"sombra": {"alfa": 0.7, "difuminado": 25, "distancia": 12, "angulo": 45}}})",
                      R"({"name": "Zocalo", "efectos": {"opacidad": 0.85, "borde": 20}})",
                      R"({"name": "Titulo principal", "settings": {"text": "Ultimo momento"}})",
                      R"({"name": "Camara", "order": "top"})"};
        t.run = [&rt, persist, sceneOr](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            std::string name = a["name"];
            const auto items = rt.engine().sceneSources(scene);
            if (std::find(items.begin(), items.end(), name) == items.end())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' no esta en la escena '%2'. escena_fuentes_ver la muestra."),
                                                               {name, scene})));
            std::vector<std::string> did;
            std::vector<std::string> failed;
            if (a.contains("visible")) {
                (rt.setSourceVisible(scene, name, a["visible"]) ? did : failed).push_back("visible");
            }
            if (a.contains("layout")) {
                CanvasRect r;
                std::string err;
                std::string nota;
                if (!parseLayout(a["layout"], r.x, r.y, r.w, r.h, err, &nota)) return done(ToolResult::error(err));
                (rt.setSourceRect(scene, name, r) ? did : failed).push_back(nota.empty() ? "layout" : "layout (" + nota + ")");
            }
            if (a.contains("recorte") && a["recorte"].is_object()) {
                const json& c = a["recorte"];
                (rt.setSourceCrop(scene, name, c.value("izquierda", 0.0f), c.value("arriba", 0.0f),
                                  c.value("derecha", 0.0f), c.value("abajo", 0.0f))
                     ? did
                     : failed)
                    .push_back("recorte");
            }
            if (a.contains("efectos")) {
                EfectosCapa ef = rt.engine().sourceEfectos(scene, name);
                std::string err;
                if (!ef.aplicar(a["efectos"], &err))
                    failed.push_back("efectos (" + err + ")");
                else
                    (rt.setSourceEfectos(scene, name, ef) ? did : failed).push_back("efectos");
            }
            if (a.contains("order")) {
                const json& o = a["order"];
                int steps = 0;
                if (o.is_number())
                    steps = o.get<int>();
                else if (o == "up")
                    steps = 1;
                else if (o == "down")
                    steps = -1;
                else if (o == "top")
                    steps = static_cast<int>(items.size());
                else if (o == "bottom")
                    steps = -static_cast<int>(items.size());
                for (int i = 0; i < std::abs(steps); ++i)
                    rt.moveSource(scene, name, steps > 0 ? 1 : -1);
                did.push_back("order");
            }
            if (a.contains("settings")) {
                json cur = json::parse(rt.engine().sourceSettingsJson(name), nullptr, false);
                if (!cur.is_object()) cur = json::object();
                cur.merge_patch(a["settings"]);
                (rt.updateSourceSettings(name, cur.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) ? did : failed).push_back("settings");
            }
            if (a.contains("volumeDb")) {
                const float mul = static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0));
                (rt.setVolume(name, mul) ? did : failed).push_back("volumen");
            }
            if (a.contains("muted")) (rt.setMuted(name, a["muted"]) ? did : failed).push_back("silencio");
            if (a.contains("rename")) {
                const std::string to = a["rename"];
                if (rt.renameSource(name, to)) {
                    name = to;
                    did.push_back("nombre");
                } else {
                    failed.push_back("nombre (" + rt.lastError() + ")");
                }
            }
            persist();
            std::string msg = "'" + name + "': ";
            for (const auto& d : did)
                msg += d + " ✓ ";
            for (const auto& f : failed)
                msg += f + " ✗ ";
            done(failed.empty() ? ToolResult::ok(msg) : ToolResult::error(msg));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_properties";
        t.title = ba::i18n::traducir("mcp", "Propiedades de una fuente");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Todas las opciones que la fuente expone (las mismas del panel Propiedades): nombre, "
            "etiqueta, tipo, valor actual, opciones posibles, rango. Sirve para cualquier plugin.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        PropertiesModel* props = props_.get();
        t.run = [&rt, props](const json& a, Done done) {
            if (rt.engine().sourceTypeId(a["name"]).empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente '%1'."), {a["name"].get<std::string>()})));
            props->setSourceName(QString::fromStdString(a["name"]));
            done(ToolResult().data(json{{"source", a["name"]},
                                        {"type", rt.engine().sourceTypeId(a["name"])},
                                        {"properties", modelRows(*props)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_set_property";
        t.title = ba::i18n::traducir("mcp", "Cambiar una propiedad (o apretar un boton)");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Cambia una propiedad de source_properties por su `name` (se aplica en vivo), o aprieta una "
            "propiedad tipo boton (press=true), p. ej. 'refrescar dispositivos'.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", {{"type", "string"}}},
                           {"property", {{"type", "string"}}},
                           {"value", {{"description", ba::i18n::traducir("mcp", "Valor (tipo segun la propiedad).")}}},
                           {"press", {{"type", "boolean"}}}}},
                         {"required", {"source", "property"}}};
        PropertiesModel* props = props_.get();
        t.run = [&rt, props, persist](const json& a, Done done) {
            if (rt.engine().sourceTypeId(a["source"]).empty())
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente '%1'."), {a["source"].get<std::string>()})));
            props->setSourceName(QString::fromStdString(a["source"]));
            const QString prop = QString::fromStdString(a["property"]);
            if (a.value("press", false)) {
                props->press(prop);
            } else {
                if (!a.contains("value")) return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta `value` (o press=true).")));
                props->setValue(prop, jsonToQv(a["value"]));
            }
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho. Settings ahora: %1"), {rt.engine().sourceSettingsJson(a["source"])})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guest_source_add";
        t.title = ba::i18n::traducir("mcp", "Poner a un invitado en una escena");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Agrega la camara (o la pantalla compartida) del invitado del lugar `slot` a una escena. Es "
            "la misma fuente en todas las escenas; se ve quien este en ese lugar de la conferencia.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", sceneProp},
              {"slot", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}},
              {"kind", {{"type", "string"}, {"enum", {"camera", "screen"}}, {"default", "camera"}}},
              {"layout", layoutProp}}},
            {"required", {"slot"}}};
        t.run = [&rt, persist, sceneOr](const json& a, Done done) {
            const int slot = a["slot"];
            if (slot > rt.config().guests)
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Hay %1 lugares de invitado (ajuste guests.count)."),
                                                               {std::to_string(rt.config().guests)})));
            const bool share = a.value("kind", std::string("camera")) == "screen";
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot) + (share ? " — pantalla" : "");
            spec.settingsJson = json{{"slot", slot}, {"channel", share ? "share" : "guest"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            spec.rect = share ? CanvasRect{0, 0, 1, 1} : CanvasRect{0.5f, 0.5f, 0.5f, 0.5f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            const std::string scene = sceneOr(a);
            const auto items = rt.engine().sceneSources(scene);
            if (std::find(items.begin(), items.end(), spec.name) != items.end()) {
                rt.setSourceRect(scene, spec.name, spec.rect);
            } else if (!rt.addSource(scene, spec)) {
                return done(ToolResult::error(rt.lastError()));
            }
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' en '%2'."), {spec.name, scene})));
        };
        core_.addTool(std::move(t));
    }

    // --- Audio ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "mixer_list";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Mezclador");
        t.category = "audio";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Foto de los niveles del mezclador, para leer. "
                        "Fuentes de audio con volumen (dB), silencio, si se esta escuchando por el monitor "
                        "(`escuchando`, ver audio_escuchar) y nivel pico actual (dBFS), mas el nivel del "
                        "programa. -100 = silencio; cerca de 0 = saturando. Cada salida tiene su mezcla: con `salida`, la de esa.");
        t.run = [this, &rt, &vm](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            json out = json::array();
            for (const std::string& n : rt.engine().audioSources()) {
                const float mul = rt.engine().sourceVolume(n, salida);
                out.push_back(json{{"name", n},
                                   {"volumeDb", mul > 0 ? 20.0 * std::log10(mul) : -100.0},
                                   {"muted", rt.engine().sourceMuted(n, salida)},
                                   // Cual se esta escuchando por el monitor
                                   // (audio_escuchar). Sin esto el PFL es
                                   // invisible: queda una fuente sonando en
                                   // los auriculares y nadie sabe cual.
                                   {"escuchando", rt.engine().sourceMonitoring(n)},
                                   {"pfl", rt.engine().pfl() == n},
                                   {"peakDb", rt.engine().sourcePeakDb(n)}});
            }
            // El pico de la por omisión lo lee la vista (leerlo acá se lo robaría); el de
            // las demás, el motor (nadie más lo lee todavía).
            float l = vm.programPeakL(), r = vm.programPeakR();
            if (!salida.empty()) rt.engine().programPeaksDb(l, r, salida);
            done(ToolResult().data(json{{"sources", out}, {"programPeakDb", {l, r}}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_set";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Volumen / silencio");
        t.category = "audio";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia el audio (mute del máster corta el sonido)");
        t.aireSi = AireSi::MasterOPrograma;
        t.reversible = true;
        t.description =
            ba::i18n::traducir("mcp", "Silencia, sube o baja el audio de una fuente. Es con lo que se CORTA un acople, se baja lo que "
            "satura, se sube lo que no se escucha o se cierra un microfono que quedo abierto. Cambia el "
            "volumen (dB, 0 = sin cambio, negativo baja) y/o silencia una fuente de audio. En la mezcla de la "
            "`salida` (cada una tiene la suya; sin ella, la por omisión).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
                           {"muted", {{"type", "boolean"}}}}},
                         {"required", {"name"}}};
        t.run = [this, &rt, persist](const json& a, Done done) {
            const std::string n = a["name"];
            const std::string salida = core_.salidaDe(a);
            bool ok = true;
            if (a.contains("volumeDb"))
                ok = rt.setVolume(n, static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0)), salida) &&
                     ok;
            if (a.contains("muted")) ok = rt.setMuted(n, a["muted"], salida) && ok;
            if (!ok)
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente de audio '%1' (mixer_list las muestra)."), {n})));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Audio de '%1' actualizado."), {n})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_escuchar";
        t.title = ba::i18n::traducir("mcp", "Escuchar una fuente por el monitor (PFL), sin tocar el aire");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Manda una fuente de audio al monitor del operador para escucharla. NO cambia lo que sale "
            "al aire: lo que estaba saliendo sigue saliendo igual.");
        t.details =
            ba::i18n::traducir("mcp", "Es el PFL de una consola: antes de abrir un microfono se lo escucha para ver si esta ahi, "
            "si zumba, si el invitado ya volvio. Sin esto habia que ponerlo al aire para enterarse.\n\n"
            "Hay UN PFL a la vez: escuchar una reemplaza a la anterior. Sin argumentos se apaga. "
            "Se SUMA a la escucha fija (lo que la matriz manda a los auriculares a propósito, como la "
            "voz del asistente): esa no se toca y sigue sonando. El PFL no se guarda.\n\n"
            "OJO: una fuente SILENCIADA (`audio_set muted`) no suena ni por el monitor —el silencio es "
            "antes de todo—. Para probar un microfono cerrado hay que quitarle el silencio "
            "y bajarle el volumen, no silenciarlo.\n\n"
            "Que se oiga depende de que el equipo tenga un monitor de audio configurado; sin monitor "
            "esto no falla pero tampoco suena. `audio_sources` dice cual se esta escuchando.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "la fuente; vacio o sin esto = apagar")}}},
              {"escuchar",
               {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "false para apagar esta (por defecto true)")}}}}}};
        t.examples = {R"({"name": "Microfono del atril"})", R"({})"};
        t.run = [&rt, &vm](const json& a, Done done) {
            const std::string n = a.value("name", std::string());
            const bool on = a.value("escuchar", true) && !n.empty();
            auto* matriz = qobject_cast<audio::Matriz*>(vm.matriz());
            if (matriz == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La matriz de audio no está disponible.")));
            // El PFL es uno solo y no toca la escucha fija: la regla es de la Matriz (la
            // misma del botón PFL de la interfaz); acá solo se arma la respuesta.
            const std::string antes = rt.engine().pfl();
            if (!on) {
                if (const QString e = matriz->ponerPfl({}); !e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok(antes.empty() ? ba::i18n::traducir("mcp", "No había ninguna en PFL.")
                                                         : ba::i18n::llenar(ba::i18n::traducir("mcp", "Dejo de escucharse: %1."), {antes})));
            }
            if (!rt.engine().sourceExists(n))
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente de audio '%1' (audio_sources las lista)."), {n})));
            if (const QString e = matriz->ponerPfl(QString::fromStdString(n)); !e.isEmpty())
                return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(
                antes.empty() || antes == n
                    ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Escuchando «%1» por el monitor. Lo que sale al aire no cambio."), {n})
                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "Escuchando «%1» por el monitor. Lo que sale al aire no cambio. "
                                                                 "(se dejo de escuchar %2)"),
                                       {n, antes})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "audio_sincro_medir";
        t.title = ba::i18n::traducir("mcp", "Medir la sincronía labios/voz con aplausos");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Mide cuánto hay que atrasar el audio para que coincida con el video, con aplausos frente a la "
            "cámara: durante `segundos` (de 4 a 30, 10 de fábrica) mira lo que SALE (cuadros del programa y "
            "mezcla de audio) y empareja cada aplauso (pico de audio, cambio brusco en la imagen). La cámara "
            "tiene que estar en el programa y su micrófono sonando. Avisá a la persona que aplauda 4 o 5 veces, "
            "separadas. Con `aplicar_a` (una fuente de audio) y una medición confiable, le suma el retardo "
            "medido al que ya tenía (audio_sincro). Devuelve retardo_ms, dispersión y si es confiable.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"segundos", {{"type", "integer"}, {"minimum", 4}, {"maximum", 30}}},
                           {"aplicar_a", {{"type", "string"}}}}}};
        t.run = [&rt, &vm, persist](const json& a, Done done) {
            const std::string fuente = a.value("aplicar_a", std::string());
            const QString e = vm.medirSincro(a.value("segundos", 10), [&rt, fuente, persist, done](const json& r0) {
                json r = r0;
                if (!fuente.empty() && r.value("confiable", false)) {
                    const int nuevo = std::clamp(rt.engine().sourceSyncOffset(fuente) + r.value("retardo_ms", 0), -5000, 5000);
                    if (rt.engine().setSourceSyncOffset(fuente, nuevo)) {
                        persist();
                        r["aplicado"] = {{"fuente", fuente}, {"ms", nuevo}};
                    } else {
                        r["aplicado_error"] = rt.engine().lastError();
                    }
                } else if (!fuente.empty()) {
                    r["aplicado"] = false;
                }
                done(ToolResult().data(r));
            });
            if (!e.isEmpty()) done(ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_medidores";
        t.title = ba::i18n::traducir("mcp", "Balística de los medidores");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cómo se mueven todos los medidores de la interfaz: `balistica` = pico (instantáneo), ppm "
                        "(EBU tipo IIb: sube al instante y cae 24 dB en 2,8 s) o vu (integra ~300 ms: el volumen "
                        "percibido). Sin argumentos dice cuál está.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"balistica", {{"type", "string"}, {"enum", {"pico", "ppm", "vu"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("balistica")) vm.setBalistica(QString::fromStdString(a.value("balistica", std::string("pico"))));
            done(ToolResult().data(json{{"balistica", vm.balistica().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_sincro";
        t.title = ba::i18n::traducir("mcp", "Sincronizar labios y voz");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Atrasa el audio de una fuente para que coincida con su video. Sin `ms`, dice el retardo que "
            "tiene puesto; '+40' / '-40' lo cambia sobre el que tiene.");
        t.details =
            ba::i18n::traducir("mcp", "Para que sirve: una camara IP llega con 200 ms a 2 s de retardo (codificacion + red + "
            "buffer), mientras que un microfono por la placa de audio llega casi al instante. Se escucha "
            "antes de ver los labios, y eso se nota muchisimo.\n\n"
            "`ms` positivo ATRASA el audio (lo normal con una camara IP); negativo lo adelanta, que sirve "
            "cuando el audio llega despues que el video. Se acota a +-5 s: mas que eso no es una camara "
            "con retardo, es un error.\n\n"
            "Como encontrar el numero: poner un valor, mirar con `foto_programa` a alguien hablando y "
            "corregir. El ojo humano es el juez. Un metodo mas fino -medirlo con un aplauso- esta "
            "escrito pero todavia no conectado a las fuentes.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", -5000}, {"maximum", 5000}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string n = a["name"];
            if (!a.contains("ms")) {
                if (rt.engine().sourceNames().empty())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "El motor todavia no tiene fuentes.")));
                return done(ToolResult().data(json{{"name", n}, {"ms", rt.engine().sourceSyncOffset(n)}}));
            }
            // Un numero lo deja en eso; «+40» / «-40» lo cambia sobre el actual; «=200», en 200.
            int ms = 0;
            if (a["ms"].is_number()) {
                ms = static_cast<int>(std::lround(a["ms"].get<double>()));
            } else if (a["ms"].is_string()) {
                std::string v = a["ms"].get<std::string>();
                while (!v.empty() && v.front() == ' ') v.erase(0, 1);
                const bool cambio = !v.empty() && (v.front() == '+' || v.front() == '-');
                if (!v.empty() && v.front() == '=') v.erase(0, 1);
                try {
                    ms = static_cast<int>(std::lround(std::stod(v)));
                } catch (const std::exception&) {
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "`ms` tiene que ser un numero (o '+40' / '-40').")));
                }
                if (cambio) ms += rt.engine().sourceSyncOffset(n);
            } else {
                return done(ToolResult::error(ba::i18n::traducir("mcp", "`ms` tiene que ser un numero (o '+40' / '-40').")));
            }
            ms = std::clamp(ms, -5000, 5000);
            if (!rt.engine().setSourceSyncOffset(n, ms))
                return done(ToolResult::error(rt.engine().lastError()));
            persist();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "El audio de '%1' va %2 ms respecto del video."), {n, std::to_string(ms)})));
        };
        core_.addTool(std::move(t));
    }

    // --- Salidas -------------------------------------------------------------------
    {
        Tool t;
        t.name = "stream_start";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Salir al aire (transmitir)");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "arranca o cambia la transmisión");
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Empieza a transmitir por RTMP. Sin argumentos usa el servidor configurado y la clave de la "
            "variable de entorno. La clave NUNCA se guarda en disco.");
        t.details = ba::i18n::traducir("mcp", "Si YA esta transmitiendo: al mismo destino no hace nada (lo dice); a OTRO destino da error "
                    "con `activo_en` (no cambia nada), salvo `cambiar_en_caliente`: true, que corta y arranca al "
                    "nuevo. `destino`: el id de un destino adicional (stream_destinos) o \"todos\": arranca esos, "
                    "ademas del principal (cada destino sale con el programa de SU salida). Con `salida` (otra que la por "
                    "omisión), su transmision principal: ahí `server` y `key` son obligatorios (la clave no se guarda).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"server", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "rtmp://... (opcional).")}}},
              {"key", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Clave de transmision (solo en memoria).")}}},
              {"cambiar_en_caliente", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "estando al aire, pasar a este destino")}}},
              {"destino", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "id de un destino adicional o \"todos\"")}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (a.contains("destino")) {
                const std::string e = rt.iniciarDestino(a.value("destino", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().text(ba::i18n::traducir("mcp", "Destino iniciado.")).data(json{{"destinos", rt.destinos()}}));
            }
            // La misma operación del modelo que el botón (el vivo queda en el bus y el histórico).
            if (!rt.salirAlAireManual(a.value("server", std::string()), a.value("key", std::string()),
                                      a.value("cambiar_en_caliente", false), salida)) {
                if (rt.engine().ultimoInicio() == BroadcastEngine::Inicio::YaOtroDestino) {
                    ToolResult r = ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Ya esta transmitiendo a %1: NO se cambio nada. Para pasar a "
                                                                                       "este destino: `cambiar_en_caliente`: true "
                                                                                       "(corta un instante)."),
                                                                    {rt.engine().destinoActivo(salida)}));
                    r.structured = json{{"activo_en", rt.engine().destinoActivo(salida)}};
                    return done(r);
                }
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No arranco: %1. Si falta la clave: pasala en `key` o en la "
                                                                               "variable %2."),
                                                           {rt.lastError(), rt.config().streamKeyEnv})));
            }
            if (rt.engine().ultimoInicio() == BroadcastEngine::Inicio::YaIgual)
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Ya estaba transmitiendo a ese destino: no cambio nada.")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Transmitiendo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_stop";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Cortar la transmision");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la transmisión");
        t.destructive = true;
        t.description =
            ba::i18n::traducir("mcp", "BAJA del aire: corta la transmision que esta saliendo. Detiene la transmision en vivo. Con "
            "`destino` (id o \"todos\"), corta solo ese destino adicional y el principal sigue. Con `salida`, la "
            "transmision principal de esa salida (las demás siguen).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"destino", {{"type", "string"}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            if (a.contains("destino")) {
                rt.detenerDestino(a.value("destino", std::string()));
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Destino cortado; el resto sigue.")));
            }
            rt.stopStreaming(core_.salidaDe(a));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Transmision detenida.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Loudness de lo que sale");
        t.category = "audio";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El loudness del PROGRAMA (la mezcla que va a la transmision y la grabacion), EBU R128 / "
                        "BS.1770-4: `m` momentaneo (400 ms), `s` corto (3 s), `i` integrado desde el reset (arranca "
                        "solo al transmitir o grabar), `tp` true peak maximo (dBTP), `lra` rango, `gr_db` lo que "
                        "baja el limitador del master, `objetivo` y `fuera_de_objetivo` (integrado a mas de 2 LU o TP "
                        "arriba del techo). audio_measure es por fuente; esto es lo que sale. Cada salida tiene el suyo (`salida`).");
        t.run = [this, &rt](const json& a, Done done) {
            done(ToolResult().data(rt.engine().loudness(core_.salidaDe(a)).json()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_reset";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Reiniciar el integrado del loudness");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Vuelve a cero el integrado, el LRA y el true peak maximo del programa.");
        t.run = [this, &rt](const json& a, Done done) {
            rt.engine().loudness(core_.salidaDe(a)).reiniciar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Reiniciado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_objetivo";
        t.title = ba::i18n::traducir("mcp", "Objetivo de loudness");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`perfil`: r128 (-23 LUFS / -1 dBTP, de fabrica), a85 (-24 LKFS / -2), plataforma16 (-16 / -1), "
                        "plataforma14 (-14 / -1) o custom con `lufs` y `tp`.") +
                        " " +
                        ba::i18n::traducir("mcp", "`agc` true: el programa se lleva solo al objetivo, despacio (1 dB por segundo como mucho, "
                        "hasta 6 dB arriba o abajo, quieto en las pausas), antes del limitador; de fabrica no. Si una "
                        "transmision va con AAC por debajo de 128 kbps, el techo baja a -2 dBTP.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}, {"enum", {"r128", "a85", "plataforma16", "plataforma14", "custom"}}}},
                           {"lufs", {{"type", "number"}}},
                           {"tp", {{"type", "number"}}},
                           {"agc", {{"type", "boolean"}}}}}};
        t.run = [&vm, &rt](const json& a, Done done) {
            json o{{"perfil", a.value("perfil", std::string("r128"))}};
            if (a.contains("lufs")) o["lufs"] = a.value("lufs", -23.0);
            if (a.contains("tp")) o["tp"] = a.value("tp", -1.0);
            if (a.contains("agc")) o["agc"] = a.value("agc", false);
            if (const std::string e = vm.objetivoLoudness().poner(o); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(rt.engine().loudness().json()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_info_aprobar";
        t.title = ba::i18n::traducir("mcp", "Aprobar el cargo que escribio un invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Lo que un invitado escribe de si mismo al entrar (`info`) NO sale en el zocalo hasta que una "
                        "persona lo aprueba: con esto, `participant` (id o nombre) y un `texto` corregido opcional. Si "
                        "esta en el cuerpo (cuerpo_listar), sale de ahi y no hace falta. Confirmalo con el operador.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"participant"}},
                         {"properties", {{"participant", {{"type", "string"}}}, {"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.aprobarInfoInvitado(QString::fromStdString(a.value("participant", std::string())),
                                                      QString::fromStdString(a.value("texto", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Aprobado: el zocalo ya lo usa.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_token_rotar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Rotar el token del MCP");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cambia ya el token del servidor MCP (el viejo deja de valer). El puente ba-mcp lo relee solo; un "
                        "cliente HTTP directo lo vuelve a pedir con `ba-mcp --token`.");
        t.run = [this](const json&, Done done) {
            const QString nuevo = rotarToken(QString::fromStdWString((ba::platform::credentialsDir()).wstring()));
            if (nuevo.isEmpty() || !http_) return done(ToolResult::error(ba::i18n::traducir("mcp", "no se pudo guardar el token nuevo")));
            http_->setToken(nuevo);
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Token rotado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguridad_estado";
        t.title = ba::i18n::traducir("mcp", "Seguridad: estado y ajustes");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Lo que protege los accesos: si el token del MCP se rota en cada arranque, si la conferencia cambia de "
                        "PIN al empezar una sesion en vivo en un dia nuevo (el PIN es uno por dia: los reinicios "
                        "del mismo dia lo conservan), y el modo en vivo. Con `rotar_token_mcp` o `pin_por_sesion` "
                        "los cambia (fuera de vivo).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"rotar_token_mcp", {{"type", "boolean"}}}, {"pin_por_sesion", {{"type", "boolean"}}}}}};
        t.run = [this](const json& a, Done done) {
            auto& seg = vm_.seguridad();
            if (a.contains("rotar_token_mcp"))
                if (const std::string e = seg.setRotarTokenMcp(a.value("rotar_token_mcp", true)); !e.empty())
                    return done(ToolResult::error(e));
            if (a.contains("pin_por_sesion"))
                if (const std::string e = seg.setPinPorSesion(a.value("pin_por_sesion", true)); !e.empty())
                    return done(ToolResult::error(e));
            const json j = seg.estado();
            done(ToolResult().data({{"rotar_token_mcp", j.value("rotar_token_mcp", true)},
                                    {"pin_por_sesion", j.value("pin_por_sesion", true)},
                                    {"modo_vivo", vm_.modoVivoEstado()},
                                    {"passphrase", ba::i18n::traducir("mcp", "todavia no (ver docs/planes/DECISIONES-PENDIENTES.md)")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_estado";
        t.title = ba::i18n::traducir("mcp", "Subtitulos ocultos (CC)");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los subtitulos ocultos de la transmision (el espectador los prende en su reproductor): si estan "
                        "prendidos, de que salida de texto salen (texto_flujos), el metodo (608 en el video, la API de "
                        "YouTube, o los dos), cuantas lineas se mandaron y las ultimas.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(qvToJson(vm.cc().estado()))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_activar";
        t.title = ba::i18n::traducir("mcp", "Prender los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`flujo`: la salida de texto (la transcripcion o una traduccion; texto_flujos). `metodo`: 608 (de "
                        "fabrica: CEA-608 dentro del video de la transmision y de cada destino), youtube_http (la API de "
                        "subtitulos de YouTube: `url` de ingesta del evento) o ambos.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"flujo"}},
                         {"properties",
                          {{"flujo", {{"type", "string"}}},
                           {"metodo", {{"type", "string"}, {"enum", {"608", "youtube_http", "ambos"}}}},
                           {"url", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            QVariantMap m{{"flujo", QString::fromStdString(a.value("flujo", std::string()))},
                          {"metodo", QString::fromStdString(a.value("metodo", std::string("608")))}};
            if (a.contains("url")) m["url"] = QString::fromStdString(a.value("url", std::string()));
            const QString e = vm.cc().activar(m);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(qvToJson(vm.cc().estado())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_desactivar";
        t.title = ba::i18n::traducir("mcp", "Apagar los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Deja de mandar subtitulos ocultos.");
        t.run = [&vm](const json&, Done done) {
            vm.cc().desactivar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Apagados.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_probar";
        t.title = ba::i18n::traducir("mcp", "Probar los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Manda un `texto` de prueba por lo configurado (608 necesita una transmision activa).");
        t.inputSchema = {{"type", "object"}, {"required", {"texto"}}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.cc().probar(QString::fromStdString(a.value("texto", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Mandado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario";
        t.title = ba::i18n::traducir("mcp", "Vocabulario de la voz a texto");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los terminos que la transcripcion escribe bien aunque el modelo no los conozca (nombres del "
                        "cuerpo, localidades, terminos de asamblea, los del operador), por grupo (activo o no), y los "
                        "ultimos reemplazos hechos. La correccion es por sonido y solo con confianza alta.");
        t.run = [&vm](const json&, Done done) {
            json d = vm.vocabulario().todo();
            d["reemplazos"] = vm.vocabulario().ultimosReemplazos();
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar un termino al vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Agrega (o cambia) el `termino` bien escrito, con `variantes` (como lo escribe mal el modelo, "
                        "opcional), en el `grupo` (operador de fabrica; cuerpo, localidades, asamblea u otro nuevo). "
                        "Con `grupo` y `activo` y sin `termino`, prende o apaga el grupo. Con `del_cuerpo`: true, pone "
                        "en el grupo cuerpo los nombres y apellidos de cuerpo_listar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"termino", {{"type", "string"}}},
                           {"variantes", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"grupo", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}},
                           {"del_cuerpo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("del_cuerpo", false))
                return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Nombres del cuerpo: %1."),
                                                                {std::to_string(vm.vocabulario().cargarCuerpo(vm.cuerpo().cuerpo()))})));
            const std::string termino = a.value("termino", std::string()), grupo = a.value("grupo", std::string());
            if (termino.empty() && !grupo.empty() && a.contains("activo")) {
                vm.vocabulario().activarGrupo(grupo, a.value("activo", true));
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
            }
            std::vector<std::string> v;
            for (const auto& x : a.value("variantes", json::array()))
                if (x.is_string()) v.push_back(x.get<std::string>());
            const std::string e = vm.vocabulario().agregar(termino, v, grupo);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Agregado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_quitar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Quitar un termino del vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Saca el `termino` (de cualquier grupo).");
        t.inputSchema = {{"type", "object"}, {"required", {"termino"}}, {"properties", {{"termino", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.vocabulario().quitar(a.value("termino", std::string())))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no esta en el vocabulario")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_probar";
        t.title = ba::i18n::traducir("mcp", "Probar el vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Pasa un `texto` (como lo escribiria el modelo) por la correccion y devuelve el resultado y los "
                        "reemplazos.");
        t.inputSchema = {{"type", "object"}, {"required", {"texto"}}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            json r = json::array();
            const std::string t2 = vm.vocabulario().corregir(a.value("texto", std::string()), &r);
            done(ToolResult().data({{"texto", t2}, {"reemplazos", r}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "entrada_respaldo";
        t.title = ba::i18n::traducir("mcp", "Que hacer si una entrada se cae al aire");
        t.category = "video";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Si la entrada `entrada` se queda sin señal mas de `ms` (de fabrica 1000) estando en la escena al "
                        "aire: `al_perder_aire` avisar (de fabrica), armar la `escena` de respaldo en el preview o cortar "
                        "a ella. Sin `al_perder_aire`, muestra lo configurado y las entradas que ahora no tienen señal.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"entrada", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}},
                           {"al_perder_aire", {{"type", "string"}, {"enum", {"avisar", "armar", "cortar"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 200}, {"maximum", 60000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& resp = vm.respaldoEntradas(); // el modelo, el mismo que usa la GUI
            const std::string e = a.value("entrada", std::string());
            if (a.contains("al_perder_aire")) {
                if (e.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta `entrada`")));
                const std::string err = resp.setRespaldo(e, a.value("escena", std::string()),
                                                         a.value("al_perder_aire", std::string()), a.value("ms", 1000));
                if (!err.empty()) return done(ToolResult::error(err));
            }
            json sin = json::array();
            for (const QString& s : resp.sinSenal()) sin.push_back(s.toStdString());
            done(ToolResult().data(json{{"sin_senal", sin}, {"respaldo", e.empty() ? json() : resp.respaldo(e)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_bancas_ver";
        t.title = ba::i18n::traducir("mcp", "Bancas y fila de oradores presenciales");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los oradores PRESENCIALES del recinto: las bancas, la fila, quién habla y hace cuánto; no son los "
            "oradores remotos. remotos_fila_ver es la fila de los remotos (invitados conectados).");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.bancas().estadoJson())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_cola";
        t.title = ba::i18n::traducir("mcp", "Sumar o sacar una banca de la fila");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`agregar` o `quitar`: banca, id o nombre del cuerpo.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"agregar", {{"type", "string"}}}, {"quitar", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("agregar")) {
                const QString e = vm.bancas().agregar(QString::fromStdString(a.value("agregar", std::string())));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            if (a.contains("quitar")) vm.bancas().quitar(QString::fromStdString(a.value("quitar", std::string())));
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_al_aire";
        t.title = ba::i18n::traducir("mcp", "Poner al aire a un orador presencial");
        t.category = "oradores";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "pone a un orador al aire (en directo corta la escena)");
        t.aireSi = AireSi::BancaDirecto;
        t.description = ba::i18n::traducir("mcp", "Una banca al aire, en una sola accion: la PTZ va al preset de su banca (solo si esa camara no "
                        "esta al aire), la escena de esa camara queda ARMADA en el preview (`modo` armar, de fabrica: "
                        "sale con TAKE) o se corta (directo), el zocalo toma su nombre y cargo del cuerpo ({orador}), "
                        "arranca su reloj y se abre su microfono (y se cierra el de la anterior). `banca`: banca, id o "
                        "nombre. Sin `banca`: la primera de la fila. Si esa persona habla desde el navegador (sesion "
                        "remota, en la sala o afuera), no se mueve ninguna camara: se le da la palabra en su celular "
                        "con la cuenta regresiva. `asignado_s`: el tiempo de cada uno (queda guardado).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"banca", {{"type", "string"}}},
                           {"modo", {{"type", "string"}, {"enum", {"armar", "directo"}}}},
                           {"asignado_s", {{"type", "integer"}, {"minimum", 0}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("asignado_s")) vm.bancas().setAsignado(a.value("asignado_s", 180));
            const QString modo = QString::fromStdString(a.value("modo", std::string("armar")));
            const QString e = a.contains("banca") ? vm.bancas().alAire(QString::fromStdString(a.value("banca", std::string())), modo)
                                                  : vm.bancas().siguiente(modo);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            json r = vm.bancas().estadoJson();
            done(ToolResult().text(r.value("aviso", std::string()).empty()
                                       ? ba::i18n::traducir("mcp", "Listo.")
                                       : ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo, con un aviso: %1"), {r.value("aviso", std::string())})).data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_tiempo";
        t.title = ba::i18n::traducir("mcp", "Tiempo del que tiene la palabra");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`sumar_s` le suma (o resta, negativo) tiempo al que habla ahora (presencial o remoto: su celular "
                        "se actualiza); `asignado_s` cambia el tiempo de cada turno de aca en adelante. A un minuto y al "
                        "terminar avisa al operador, en el celular del remoto y en la unidad de la central.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"sumar_s", {{"type", "integer"}}}, {"asignado_s", {{"type", "integer"}, {"minimum", 10}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("asignado_s")) vm.bancas().setAsignado(a.value("asignado_s", 180));
            if (a.contains("sumar_s")) vm.bancas().sumarTiempo(a.value("sumar_s", 0));
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguir_microfono_estado";
        t.title = ba::i18n::traducir("mcp", "Seguir al microfono: estado");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Si esta prendido, el modo (proponer o automatico), el umbral y los tiempos, los microfonos de "
                        "banca abiertos ahora y la propuesta («Banca 12 – Perez pidio la palabra»), si hay.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.seguirMicrofono().estado())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguir_microfono";
        t.title = ba::i18n::traducir("mcp", "Seguir al microfono abierto");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cuando un microfono de banca (cuerpo: microfono.fuente) pasa `umbral_db` durante `sostener_ms`, "
                        "propone poner esa banca al aire. `modo` proponer (de fabrica: el operador acepta) o automatico "
                        "(la arma sola en el preview; nunca corta, y no mueve una camara al aire). `accion` aceptar "
                        "(arma la propuesta) o descartar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"proponer", "automatico"}}}},
                           {"fuente", {{"type", "string"}, {"enum", {"niveles", "conferencia"}},
                                       {"description", ba::i18n::traducir("mcp", "niveles de los microfonos (de fabrica) o el sistema de conferencia del recinto (plugin conferencia-recinto)")}}},
                           {"umbral_db", {{"type", "number"}, {"minimum", -60}, {"maximum", 0}}},
                           {"sostener_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 10000}}},
                           {"soltar_ms", {{"type", "integer"}, {"minimum", 200}, {"maximum", 30000}}},
                           {"accion", {{"type", "string"}, {"enum", {"aceptar", "descartar"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& s = vm.seguirMicrofono();
            json c = a;
            c.erase("accion");
            if (!c.empty())
                if (const std::string e = s.configurar(c); !e.empty()) return done(ToolResult::error(e));
            const std::string acc = a.value("accion", std::string());
            if (acc == "aceptar")
                if (const std::string e = s.aceptar(); !e.empty()) return done(ToolResult::error(e));
            if (acc == "descartar") s.descartar();
            done(ToolResult().data(s.estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_cargar";
        t.title = ba::i18n::traducir("mcp", "Cargar el orden del dia");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Carga los puntos de la sesion desde `texto`: una linea por punto («1. Expte. 123/24 – Titulo», "
                        "«2) Titulo») o CSV («n;expediente;titulo»). Reemplaza los que habia (el uso de la palabra sigue). "
                        "Con `limpiar`: true empieza una sesion nueva (borra puntos y tiempos).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"texto", {{"type", "string"}}}, {"limpiar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("limpiar", false)) vm.ordenDelDia().limpiar();
            if (!a.contains("texto")) return done(ToolResult::ok(ba::i18n::traducir("mcp", "Sesion nueva.")));
            int n = 0;
            const std::string e = vm.ordenDelDia().cargar(a.value("texto", std::string()), &n);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Cargados %1 puntos."), {std::to_string(n)})).data({{"puntos", vm.ordenDelDia().puntos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_ver";
        t.title = ba::i18n::traducir("mcp", "Orden del dia");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los puntos con su estado (pendiente, en_tratamiento, votado, comision, retirado), cuando se "
                        "trataron y el resultado; el punto actual; el uso de la palabra por orador (y por punto); y, si "
                        "se esta grabando, los capitulos por punto para pegar en YouTube.");
        t.run = [&vm](const json&, Done done) {
            auto& o = vm.ordenDelDia();
            done(ToolResult().data({{"puntos", o.puntos()},
                                    {"actual", o.actual()},
                                    {"tiempos_palabra", o.tiemposPalabra(vm.remotosPalabra())},
                                    {"capitulos", o.capitulosTexto(vm.recordStartMs())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_punto";
        t.title = ba::i18n::traducir("mcp", "Cambiar el estado de un punto");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "El punto `n` pasa a `estado` (en_tratamiento: cierra el anterior, pone «Punto N: …» en {punto} "
                        "para los zocalos y lo marca para el acta y los capitulos), con `resultado` opcional («aprobado "
                        "por unanimidad»). Con `siguiente`: true, el proximo pendiente pasa a tratamiento.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"n", {{"type", "integer"}, {"minimum", 1}}},
                           {"estado", {{"type", "string"}, {"enum", {"pendiente", "en_tratamiento", "votado", "comision", "retirado"}}}},
                           {"resultado", {{"type", "string"}}},
                           {"siguiente", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& o = vm.ordenDelDia();
            const std::string e = a.value("siguiente", false)
                                      ? o.siguiente()
                                      : o.punto(a.value("n", 0), a.value("estado", std::string("en_tratamiento")),
                                                a.value("resultado", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"actual", o.actual()}}));
        };
        core_.addTool(std::move(t));
    }
    // --- Los informes del resultado de la sesión (asamblea/InformesResultado) ---------
    {
        Tool t;
        t.name = "informe_plantillas";
        t.title = ba::i18n::traducir("mcp", "Las plantillas del informe del resultado");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las plantillas del informe del resultado de la sesión ({id, nombre}) y los campos que "
                        "se pueden usar. Con `id`, también su texto y cómo queda rellenada con lo de ahora (`vista`).");
        t.details = ba::i18n::traducir("mcp", "Una plantilla es Markdown con campos: {{cuerpo}}, {{evento}}, {{fecha}}, {{hora}}, "
                    "{{generado}}, {{quorum.estado}}, {{quorum.presentes}}, {{quorum.necesarios}}, {{quorum.miembros}}, "
                    "{{padron.presentes}}, {{padron.total}}. Las listas se repiten entre {{#lista}} y {{/lista}} y adentro se "
                    "usan los campos de cada uno: presentes (nombre, papel, por_donde, hora), puntos (n, expediente, titulo, "
                    "estado, resultado), votaciones (asunto, afirmativos, negativos, abstenciones, presentes, mayoria, "
                    "resultado), palabra (nombre, tiempo). {{^lista}}…{{/lista}} sale solo si está vacía y {{#lista.0}}…"
                    "{{/lista.0}} una vez si tiene alguno (para el encabezado de una tabla). Un tag solo en su renglón no deja "
                    "renglón vacío, así las filas de una tabla quedan juntas.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            json out{{"plantillas", inf->plantillas()}, {"campos", asamblea::InformesResultado::campos()}};
            const std::string id = a.value("id", std::string());
            if (!id.empty()) {
                const std::string texto = inf->texto(id);
                if (texto.empty())
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una plantilla «%1»."), {id})));
                out["texto"] = texto;
                out["vista"] = inf->vista(id);
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "informe_plantilla_guardar";
        t.title = ba::i18n::traducir("mcp", "Escribir una plantilla del informe del resultado");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Escribe una plantilla del informe del resultado (Markdown con campos: ver "
                        "informe_plantillas). Con `id` cambia esa (y su `nombre`, si viene); sin `id` crea una nueva con `nombre`. "
                        "`quitar`: true la borra. El operador la ve y la cambia en Asamblea → Informes → Plantillas.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}}, {"nombre", {{"type", "string"}}}, {"texto", {{"type", "string"}}},
                           {"quitar", {{"type", "boolean"}}}}}};
        t.examples = {R"({"nombre": "Acta para la prensa", "texto": "# {{cuerpo}} — {{fecha}}\n\n{{#puntos}}\n- {{n}}. {{titulo}}: **{{resultado}}**\n{{/puntos}}"})"};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            std::string id = a.value("id", std::string());
            if (a.value("quitar", false)) {
                const std::string e = inf->quitar(id);
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().data({{"plantillas", inf->plantillas()}}));
            }
            const std::string texto = a.value("texto", std::string());
            if (texto.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Falta el `texto` de la plantilla.")));
            if (id.empty()) {
                std::string err;
                id = inf->crear(a.value("nombre", std::string()), texto, &err);
                if (id.empty()) return done(ToolResult::error(err));
            } else if (const std::string e = inf->guardar(id, texto, a.value("nombre", std::string())); !e.empty()) {
                return done(ToolResult::error(e));
            }
            done(ToolResult().data({{"id", id}, {"vista", inf->vista(id)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "informe_generar";
        t.title = ba::i18n::traducir("mcp", "Generar el informe del resultado en PDF");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Rellena la plantilla `plantilla` (su id; sin ella, la primera) con lo de ahora y la "
                        "guarda en PDF en la carpeta del evento (informes/), con el Markdown al lado. Con `soloVer`: true "
                        "devuelve el texto sin escribir nada.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"plantilla", {{"type", "string"}}}, {"soloVer", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* inf = vm.informes();
            if (!inf) return done(ToolResult::error(ba::i18n::traducir("mcp", "Los informes no están disponibles en esta sesión.")));
            std::string id = a.value("plantilla", std::string());
            if (id.empty() && !inf->plantillas().empty()) id = inf->plantillas()[0].value("id", std::string());
            if (inf->texto(id).empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay una plantilla «%1»."), {id})));
            if (a.value("soloVer", false)) return done(ToolResult().text(inf->vista(id)));
            const json r = inf->generar(id);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Informe guardado: %1"), {r.value("archivo", std::string())})).data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_orador_terminar";
        t.title = ba::i18n::traducir("mcp", "Terminar al orador presencial");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Termina a UN orador presencial: cierra el micrófono de su banca y para su reloj; no calla a los "
            "demás.");
        t.run = [&vm](const json&, Done done) {
            vm.bancas().terminar();
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "presidencia_callar_todos";
        t.title = ba::i18n::traducir("mcp", "Silenciar a todos");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Calla a TODOS a la vez: cierra todos los micrófonos de la central menos el de la presidencia y "
            "termina al que está hablando. Lo mismo que el botón de prioridad del presidente en su unidad.");
        t.run = [&vm](const json&, Done done) {
            // La misma operación que el botón y las consolas; el error es el de la central.
            if (vm.presidencia() == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La presidencia no está disponible.")));
            vm.presidencia()->silenciarTodos("mcp", [done](const std::string& e) {
                done(e.empty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Todos callados.")) : ToolResult::error(e));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "presidencia_hablar";
        t.title = ba::i18n::traducir("mcp", "Hablo yo (la presidencia)");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`activo: true` calla a todos (como presidencia_callar_todos) y abre el micrófono de la unidad de "
                        "presidente de la central; false lo cierra. Hace falta la central conectada.");
        t.inputSchema = {{"type", "object"}, {"required", {"activo"}}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (vm.presidencia() == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La presidencia no está disponible.")));
            const bool activo = a.value("activo", true);
            // Contesta cuando contesta la central (abrió o no el micrófono).
            vm.presidencia()->hablaPresidencia(activo, "mcp", [done, activo](const std::string& e) {
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult::ok(activo ? ba::i18n::traducir("mcp", "Todos callados; habla la presidencia.")
                                           : ba::i18n::traducir("mcp", "La presidencia cerró su micrófono.")));
            });
        };
        core_.addTool(std::move(t));
    }
    // --- El debate entre dos (asamblea/Debate) --------------------------------------
    if (vm.debate() != nullptr) {
        asamblea::Debate* deb = vm.debate();
        {
            Tool t;
            t.name = "debate_estado";
            t.title = ba::i18n::traducir("mcp", "El debate en curso");
            t.category = "oradores";
            t.clase = Clase::Lectura;
            t.description = ba::i18n::traducir("mcp", "Si hay un debate: los dos (A y B, de la sala o remotos, con el canal de cada remoto), de qué "
                            "tipo es (sala_sala, sala_remoto, remoto_remoto), a quién le toca, cuánto le queda y la "
                            "escena; y la configuración (la escena de cada tipo, el tiempo de cada turno).");
            t.run = [deb](const json&, Done done) { done(ToolResult().data(deb->estadoJson())); };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "debate_iniciar";
            t.title = ba::i18n::traducir("mcp", "Empezar un debate entre dos");
            t.category = "oradores";
            t.clase = Clase::Escritura;
            t.description =
                ba::i18n::traducir("mcp", "Debate entre `a` y `b` (banca, id o nombre del Cuerpo): dos de la sala, dos remotos o uno y uno. Termina "
                "al orador que estaba; con alguno de la sala pone la central en modo operador con dos micrófonos (y la "
                "devuelve como estaba al terminar); a los remotos les da la palabra todo el debate (A por «Remoto con la "
                "palabra», B por «Remoto con la palabra 2»); lleva las cámaras de los de la sala a su banca y arma la "
                "escena configurada para ese tipo (debate_config). `turno`: a (de fábrica), b, ambos o nadie.");
            t.inputSchema = {{"type", "object"},
                             {"required", {"a", "b"}},
                             {"properties",
                              {{"a", {{"type", "string"}}},
                               {"b", {{"type", "string"}}},
                               {"turno", {{"type", "string"}, {"enum", {"a", "b", "ambos", "nadie"}}}}}}};
            t.run = [deb](const json& a, Done done) {
                const std::string e = deb->iniciar(a.value("a", std::string()), a.value("b", std::string()),
                                                   a.value("turno", std::string("a")));
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().data(deb->estadoJson()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "debate_turno";
            t.title = ba::i18n::traducir("mcp", "Dar el turno en el debate");
            t.category = "oradores";
            t.clase = Clase::Escritura;
            t.description = ba::i18n::traducir("mcp", "`turno`: a, b, ambos o nadie. Al que no le toca se le cierra el micrófono (en la central, o su "
                            "audio en la mezcla y en su celular); su imagen sigue. Arranca el reloj del turno. `sumar`: "
                            "segundos de más (o de menos) al turno en curso.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"turno", {{"type", "string"}, {"enum", {"a", "b", "ambos", "nadie"}}}},
                               {"sumar", {{"type", "integer"}}}}}};
            t.run = [deb](const json& a, Done done) {
                if (a.contains("turno"))
                    if (const std::string e = deb->turno(a.value("turno", std::string())); !e.empty())
                        return done(ToolResult::error(e));
                if (a.contains("sumar")) deb->sumar(a.value("sumar", 0));
                done(ToolResult().data(deb->estadoJson()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "debate_terminar";
            t.title = ba::i18n::traducir("mcp", "Terminar el debate");
            t.category = "oradores";
            t.clase = Clase::Escritura;
            t.description = ba::i18n::traducir("mcp", "Cierra los micrófonos de los dos, les saca la palabra a los remotos y devuelve la central "
                            "como estaba. La escena no cambia.");
            t.run = [deb](const json&, Done done) {
                deb->terminar();
                done(ToolResult::ok(ba::i18n::traducir("mcp", "Debate terminado.")));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "debate_config";
            t.title = ba::i18n::traducir("mcp", "Configurar el debate");
            t.category = "oradores";
            t.clase = Clase::Escritura;
            t.description = ba::i18n::traducir("mcp", "`escenas`: {sala_sala, sala_remoto, remoto_remoto} = la escena que se arma para cada tipo de "
                            "debate (para dos remotos, una que muestre «Remoto con la palabra» y «Remoto con la palabra 2»). "
                            "`turno_s`: segundos de cada turno (0 = sin reloj). `directo`: true corta la escena al aire en "
                            "vez de armarla. Sin argumentos, la muestra.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"escenas",
                                {{"type", "object"},
                                 {"properties",
                                  {{"sala_sala", {{"type", "string"}}},
                                   {"sala_remoto", {{"type", "string"}}},
                                   {"remoto_remoto", {{"type", "string"}}}}}}},
                               {"turno_s", {{"type", "integer"}, {"minimum", 0}}},
                               {"directo", {{"type", "boolean"}}}}}};
            t.run = [deb, &rt](const json& a, Done done) {
                if (a.contains("escenas") && a["escenas"].is_object()) {
                    const auto n = rt.engine().sceneNames();
                    for (const auto& [k, v] : a["escenas"].items())
                        if (v.is_string() && !v.get<std::string>().empty() &&
                            std::find(n.begin(), n.end(), v.get<std::string>()) == n.end())
                            return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay ninguna escena «%1» (scene_list)"), {v.get<std::string>()})));
                }
                if (!a.empty())
                    if (const std::string e = deb->configurar(a); !e.empty()) return done(ToolResult::error(e));
                done(ToolResult().data(deb->config()));
            };
            core_.addTool(std::move(t));
        }
    }
    {
        Tool t;
        t.name = "cuerpo_listar";
        t.title = ba::i18n::traducir("mcp", "Integrantes del cuerpo");
        t.category = "evento";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El cuerpo deliberativo: nombre e integrantes con banca, cargo oficial, bloque, distrito, "
                        "preset PTZ de su banca (camara/posicion), microfono y si estan VALIDADOS (validado_por). Es "
                        "la fuente unica para los zocalos, las bancas de la fila de oradores y el PTZ por banca.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.cuerpo().cuerpo().todo())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_crear";
        t.title = ba::i18n::traducir("mcp", "Nombrar el cuerpo");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Pone el nombre del cuerpo (p. ej. «Asamblea 2025-2029»). No borra integrantes.");
        t.inputSchema = {{"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            vm.cuerpo().setNombre(QString::fromStdString(a.value("nombre", std::string())));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_importar";
        t.title = ba::i18n::traducir("mcp", "Importar la planilla de integrantes");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Importa una planilla CSV (coma o punto y coma, con encabezado: banca, nombre, cargo, bloque, "
                        "distrito, camara, posicion, microfono). Los que ya estan (por banca o nombre) se actualizan. "
                        "`reemplazar`: borra los de antes. cuerpo_plantilla escribe una de ejemplo.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties", {{"archivo", {{"type", "string"}}}, {"reemplazar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const json r = vm.cuerpo().cuerpo().importarCsv(a.value("archivo", std::string()), a.value("reemplazar", false));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_plantilla";
        t.title = ba::i18n::traducir("mcp", "Planilla de integrantes de ejemplo");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Escribe una planilla CSV de ejemplo (encabezado y dos filas) en Exportado/ y devuelve la ruta.");
        t.run = [&vm](const json&, Done done) {
            const QString r = vm.cuerpo().plantilla();
            if (r.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no se pudo escribir la planilla")));
            done(ToolResult().data(json{{"archivo", r.toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_editar";
        t.title = ba::i18n::traducir("mcp", "Agregar o cambiar un integrante del cuerpo");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Con `id` lo cambia; sin `id`, lo crea (`nombre` obligatorio). Campos: nombre, cargo_oficial, "
                        "bloque, distrito, banca (unica), camara + posicion (el preset PTZ de su banca), microfono (la "
                        "fuente de audio de su banca), dcn_unidad (la unidad de la central Bosch DCN de su banca; vacío la "
                        "saca), persona_id (la ficha de Personas), prioridad (número: mayor = antes en una grilla de "
                        "personas ordenada por prioridad; \"\" la saca). Si cambia el nombre o el cargo, "
                        "deja de estar validado (integrante_validar).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"cargo_oficial", {{"type", "string"}}},
                           {"bloque", {{"type", "string"}}},
                           {"distrito", {{"type", "string"}}},
                           {"banca", {{"type", "string"}}},
                           {"camara", {{"type", "string"}}},
                           {"posicion", {{"type", "string"}}},
                           {"microfono", {{"type", "string"}}},
                           {"dcn_unidad", {{"type", "string"}}},
                           {"persona_id", {{"type", "string"}}},
                           {"prioridad", {{"type", {"integer", "string"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            QVariantMap m = QJsonDocument::fromJson(QByteArray::fromStdString(a.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
            const QString e = vm.cuerpo().guardar(m);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(vm.cuerpo().cuerpo().buscar(a.value("banca", a.value("nombre", a.value("id", std::string()))))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_ver";
        t.title = ba::i18n::traducir("mcp", "Ver un integrante del cuerpo");
        t.category = "evento";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Busca por id, banca o nombre.");
        t.inputSchema = {{"type", "object"}, {"required", {"clave"}}, {"properties", {{"clave", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const json r = vm.cuerpo().cuerpo().buscar(a.value("clave", std::string()));
            if (r.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay nadie con esa banca, id o nombre (cuerpo_listar)")));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_validar";
        t.title = ba::i18n::traducir("mcp", "Validar los datos de un integrante");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Marca que una persona revisó el nombre y el cargo de un integrante (lo que sale en el zocalo). "
                        "Confirmalo con el operador: es lo que hace que salga al aire sin aviso.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.cuerpo().validar(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no existe ese id")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Validado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_borrar";
        t.title = ba::i18n::traducir("mcp", "Sacar a un integrante del cuerpo");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra un integrante (`id`).");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.cuerpo().borrar(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no existe ese id")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_foto";
        t.title = ba::i18n::traducir("mcp", "La foto de un integrante");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "La foto de un integrante (`banca`, id o nombre), que es la cara de su botón en la botonera: "
                        "`archivo` (una imagen: se recorta cuadrada) o `capturar: true` (lleva la cámara de su banca a "
                        "su preset, si no está al aire, y toma lo que se ve; tarda unos segundos y el resultado llega "
                        "como aviso). `quitar: true` la saca. `faltantes: true` (sin banca) saca una por una las de "
                        "todos los que no tienen foto y tienen cámara en su banca.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"banca", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}},
                           {"capturar", {{"type", "boolean"}}},
                           {"quitar", {{"type", "boolean"}}},
                           {"faltantes", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("faltantes", false)) {
                const int n = vm.cuerpo().capturarFaltantes();
                return done(ToolResult::ok(
                    n == 0 ? ba::i18n::traducir("mcp", "No falta ninguna foto (de los que tienen cámara en su banca).")
                           : ba::i18n::llenar(ba::i18n::traducir("mcp", "Sacando %1 foto(s), una por una; cada una llega como aviso."),
                                              {std::to_string(n)})));
            }
            if (a.value("banca", std::string()).empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta `banca` (o `faltantes: true`)")));
            const json i = vm.cuerpo().cuerpo().buscar(a.value("banca", std::string()));
            if (i.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay nadie con esa banca, id o nombre en el cuerpo")));
            const QString id = QString::fromStdString(i.value("id", std::string()));
            if (a.value("quitar", false)) {
                vm.cuerpo().borrarFoto(id);
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Foto quitada.")));
            }
            if (a.value("capturar", false)) {
                vm.cuerpo().capturarFoto(id);
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Capturando con la cámara de su banca (unos segundos).")));
            }
            const QString e = vm.cuerpo().subirFoto(id, QString::fromStdString(a.value("archivo", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Foto cargada.")));
        };
        core_.addTool(std::move(t));
    }
    // --- El recinto: la grilla de bancas (asamblea/Cuerpo) ------------------------
    {
        Tool t;
        t.name = "recinto_ver";
        t.title = ba::i18n::traducir("mcp", "La grilla del recinto");
        t.category = "evento";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las bancas como se ven desde la cabina: {filas, columnas, bancas: [{banca, fila, columna, "
                        "x?, y?, integrante: {id, nombre, bloque, preset_ptz, validado}}], presidencia?: {banca, integrante}, "
                        "sin_lugar: [bancas del cuerpo que no están en la grilla]}. Fila 0 = la de adelante; columna 0 = "
                        "la izquierda desde la cabina. x, y: la banca se ubicó a mano en el hemiciclo (en anchos de "
                        "banca desde el centro de la presidencia, y negativa hacia arriba).");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.cuerpo().cuerpo().recinto())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_editar";
        t.title = ba::i18n::traducir("mcp", "Armar la grilla del recinto");
        t.category = "evento";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cambia la grilla: `filas` y `columnas`; `auto: true` acomoda todas las bancas del cuerpo en "
                        "orden (de adelante para atrás, de izquierda a derecha); `mover: {banca, fila, columna}` una "
                        "sola (fila -1 la saca); `bancas: [{banca, fila, columna}]` reemplaza la lista entera. Si algo "
                        "no cierra (fuera de la grilla, dos en un lugar), no cambia nada y dice por qué. "
                        "`distribucion`: cómo se dibuja (grilla, hemiciclo —cada fila un arco alrededor de la "
                        "presidencia— o lista). `presidencia: banca` la pone en la mesa de la presidencia, fuera de "
                        "la grilla (\"\" la saca). En el hemiciclo, `posicion: {banca, x, y}` la ubica a mano (en "
                        "anchos de banca desde el centro de la presidencia, y negativa hacia arriba; x null la vuelve "
                        "a su arco) y `posiciones_auto: true` las vuelve todas a su arco.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"distribucion", {{"type", "string"}, {"enum", {"grilla", "hemiciclo", "lista"}}}},
                           {"filas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 40}}},
                           {"columnas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}}},
                           {"auto", {{"type", "boolean"}}},
                           {"mover",
                            {{"type", "object"},
                             {"properties",
                              {{"banca", {{"type", "string"}}}, {"fila", {{"type", "integer"}}}, {"columna", {{"type", "integer"}}}}}}},
                           {"bancas", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                           {"presidencia", {{"type", "string"}}},
                           {"posicion",
                            {{"type", "object"},
                             {"properties",
                              {{"banca", {{"type", "string"}}}, {"x", {{"type", {"number", "null"}}}}, {"y", {{"type", {"number", "null"}}}}}}}},
                           {"posiciones_auto", {{"type", "boolean"}}}}}};
        t.examples = {R"({"filas": 4, "columnas": 9, "auto": true})", R"({"mover": {"banca": "12", "fila": 0, "columna": 4}})",
                      R"({"presidencia": "1"})", R"({"posicion": {"banca": "12", "x": -3.5, "y": -4}})"};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.cuerpo().cuerpo().setRecinto(a);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(vm.cuerpo().cuerpo().recinto()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_apuntar";
        t.title = ba::i18n::traducir("mcp", "Apuntar la cámara a una banca");
        t.category = "camaras";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Lleva la PTZ a la toma de la banca (por banca, id o nombre) para tenerla lista, sin armar ni "
                        "abrir micrófonos. No mueve la cámara que está al aire. Para ponerla al aire: banca_al_aire; "
                        "para pedirlo de palabra (banca, persona o posición): enfocar.");
        t.inputSchema = {{"type", "object"}, {"required", {"banca"}}, {"properties", {{"banca", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.bancas().apuntar(QString::fromStdString(a.value("banca", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Cámara en camino a la banca %1."), {a.value("banca", std::string())})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "enfocar";
        t.title = ba::i18n::traducir("mcp", "Enfocar una banca, a una persona o una posición");
        t.category = "camaras";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "lleva una cámara a una banca, persona o posición (si está al aire, se ve)");
        t.aireSi = AireSi::CamaraForzada;
        t.description =
            ba::i18n::traducir("mcp", "Lleva la cámara a lo que se pida DE PALABRA: «banca 10», «10», el nombre de un integrante "
            "(«Pepito López», o solo «López»; sin importar tildes ni mayúsculas) o el nombre de una "
            "posición de cualquier cámara («Presidencia», «Plano general»). La banca usa SU toma (cámara "
            "y posición de la banca, configuradas en las bancas). Solo apunta: no da la palabra (eso es "
            "banca_al_aire, que además abre el micrófono, arranca el reloj y arma la escena del orador). "
            "Con la cámara al aire no se mueve, salvo `forzar` (pide confirmación). `solo_ver`: dice qué "
            "entendió (banca, persona o posición, y qué cámara) sin mover nada; si es ambiguo, da los "
            "candidatos.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"que"}},
                         {"properties",
                          {{"que", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "«banca 10», un nombre o el de una posición")}}},
                           {"forzar", {{"type", "boolean"}}},
                           {"solo_ver", {{"type", "boolean"}}}}}};
        t.examples = {R"({"que": "banca 10"})", R"({"que": "Pepito López"})", R"({"que": "Presidencia"})"};
        t.run = [&vm](const json& a, Done done) {
            const std::string que = a.value("que", std::string());
            const json r = vm.bancas().resolver(que);
            if (a.value("solo_ver", false) || r.contains("error")) {
                if (r.contains("error") && !a.value("solo_ver", false)) {
                    ToolResult e = ToolResult::error(r.value("error", std::string()));
                    e.structured = r;
                    return done(e);
                }
                return done(ToolResult().data(r));
            }
            const QString e = vm.bancas().enfocar(QString::fromStdString(que), a.value("forzar", false));
            if (!e.isEmpty()) {
                ToolResult err = ToolResult::error(e.toStdString());
                err.structured = json{{"camara_al_aire", vm.bancas().enfocarFrenoPorAire()}, {"entendido", r}};
                return done(err);
            }
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Enfocando: %1 (%2)."),
                                                 {r.value("posicion", std::string()), r.value("camara", std::string())}))
                     .data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_toma";
        t.title = ba::i18n::traducir("mcp", "La cámara y la posición de una banca");
        t.category = "asamblea";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Asocia la banca `banca` a una cámara (`camara`, la entrada) y a una de sus posiciones "
            "(`posicion`, nombre o id: ptz_posiciones las da). La toma es de la BANCA, no de la persona: "
            "si alguien cambia de banca, queda. La usan banca_al_aire, el debate, enfocar y la fuente "
            "«Banca con la palabra». `camara` vacía la saca.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"banca", "camara"}},
                         {"properties",
                          {{"banca", {{"type", "string"}}}, {"camara", {{"type", "string"}}}, {"posicion", {{"type", "string"}}}}}};
        t.examples = {R"({"banca": "10", "camara": "Cámara del recinto", "posicion": "Banca 10"})"};
        t.run = [&vm](const json& a, Done done) {
            const std::string banca = a.value("banca", std::string()), camara = a.value("camara", std::string()),
                              posicion = a.value("posicion", std::string());
            if (!camara.empty()) {
                auto* cc = qobject_cast<ptz::ControlCamaras*>(vm.camaras());
                const QVariantMap d = cc ? cc->datosPosicion(QString::fromStdString(camara), QString::fromStdString(posicion))
                                         : QVariantMap{};
                if (!d.value("existe").toBool())
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» no tiene una posición «%2» (ptz_posiciones da "
                                                                                   "las que hay; ptz_posicion_crear la crea)"),
                                                               {camara, posicion})));
            }
            const std::string e = vm.cuerpo().cuerpo().setToma(banca, camara, posicion);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(camara.empty() ? ba::i18n::llenar(ba::i18n::traducir("mcp", "La banca %1 quedó sin toma."), {banca})
                                               : ba::i18n::llenar(ba::i18n::traducir("mcp", "Banca %1: %2 (%3)."), {banca, posicion, camara})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_retorno";
        t.title = ba::i18n::traducir("mcp", "Lo que oye cada invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Mix-minus: cada invitado recibe el programa SIN su propia voz (de fabrica), asi no se escucha "
                        "con retardo ni hace eco. `retorno`: mix_minus o programa (el programa entero). Por `slot` "
                        "(1..invitados). Sin `retorno`, dice lo de cada slot.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"slot", {{"type", "integer"}, {"minimum", 1}}},
                           {"retorno", {{"type", "string"}, {"enum", {"mix_minus", "programa"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& r = vm.retornos();
            if (a.contains("retorno"))
                if (const std::string e = r.poner(a.value("slot", 0), a.value("retorno", std::string())); !e.empty())
                    return done(ToolResult::error(e));
            done(ToolResult().data(json{{"retornos", r.todos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_disco";
        t.title = ba::i18n::traducir("mcp", "Disco de la grabacion");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Cuanto disco queda y para cuantas horas alcanza al bitrate de todo lo que se graba "
                        "(grabación, copia, señal limpia e ISO de todas las salidas, repartido por disco: manda el que antes "
                        "se llena; sin grabar, lo que sumaría arrancar la grabación); si alguna copia va al mismo disco que su "
                        "grabación (`copia_mismo_volumen`, `mismo_volumen`); y el estado del ultimo pase a MP4 (remux).");
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"disco", rt.discoGrabacion()}, {"remux", rt.engine().remuxEstado()},
                                        {"apuros", rt.engine().discoEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "disco_prioridad";
        t.title = ba::i18n::traducir("mcp", "El disco, para la grabacion del programa");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cuando el disco no da abasto (lo grabado espera en memoria para entrar, o el registro no puede "
                        "escribir): `priorizar` true le da el disco a la grabacion del programa (pausa las ISO, la "
                        "repeticion, la copia si va al mismo disco y la senal limpia a disco; el registro se junta en "
                        "memoria); false sigue con todo. Cuando el disco anda bien 30 s, todo sigue solo. `modo`: "
                        "preguntar (avisa y ofrece, de fabrica), auto (lo hace solo) o nunca (solo avisa). Sin "
                        "argumentos: el estado {apuros, atraso_s, registro_trabado_ms, prioridad, automatica, modo, pausadas}.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"priorizar", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"preguntar", "auto", "nunca"}}}}}}};
        if (rt.config().modoPrueba)
            t.inputSchema["properties"]["probar_trabado_ms"] = {
                {"type", "integer"}, {"description", ba::i18n::traducir("mcp", "Solo con --test: la grabación del programa, como con el disco trabado tantos ms.")}};
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("probar_trabado_ms") && rt.config().modoPrueba)
                rt.engine().simularDiscoTrabado(std::clamp(a.value("probar_trabado_ms", 0), 0, 120000));
            if (a.contains("modo")) {
                const std::string e = rt.engine().setModoDisco(a.value("modo", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            json out{{"disco", rt.engine().discoEstado()}};
            if (a.contains("priorizar")) {
                rt.engine().priorizarPrograma(a.value("priorizar", false));
                // Se aplica en el hilo de salidas (sin esperar): el estado nuevo, en la próxima consulta.
                out["pedido"] = a.value("priorizar", false) ? ba::i18n::traducir("mcp", "darle el disco al programa")
                                                         : ba::i18n::traducir("mcp", "seguir con todo");
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    if (rt.config().modoPrueba) {
        // Solo con --test: para probar que una caída deja su volcado bien hecho
        // (diag/Caidas.cpp + launcher/Volcados.cpp). Tira la app abajo.
        Tool t;
        t.name = "diag_probar_caida";
        t.title = ba::i18n::traducir("mcp", "Probar una caida (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "SOLO EN MODO PRUEBA. Tira la app abajo a proposito para probar los volcados: "
                        "`tipo` acceso (violacion de acceso), heap (corrupcion del heap) o abort. "
                        "El launcher la relanza; el volcado y su .json quedan en Logs/dumps.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}, {"enum", {"acceso", "heap", "abort"}}}}}},
                         {"required", {"tipo"}}};
        t.run = [](const json& a, Done done) {
            const std::string tipo = a.value("tipo", std::string());
            done(ToolResult().data(json{{"cayendo", tipo}}));
            // En otro hilo y un momento después: que la respuesta llegue.
            std::thread([tipo]() {
                ba::platform::nombrarHilo("caida-prueba");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (tipo == "acceso") {
                    volatile int* p = nullptr;
                    *p = 1;
                } else if (tipo == "heap") {
#if defined(_WIN32)
                    // Un heap propio de Windows: el doble HeapFree lo detecta siempre
                    // (0xC0000374, como la caída real); el del CRT a veces no se entera.
                    HANDLE h = HeapCreate(0, 0, 0);
                    void* p = HeapAlloc(h, 0, 64);
                    HeapFree(h, 0, p);
                    HeapFree(h, 0, p);
#else
                    char* p = static_cast<char*>(std::malloc(32));
                    std::free(p);
                    std::free(p);
#endif
                } else {
                    std::abort();
                }
            }).detach();
        };
        core_.addTool(std::move(t));
    }
    if (rt.config().modoPrueba) {
        // Solo con --test: la interfaz trabada a propósito (un bucle ocupado en su
        // hilo), para probar lo que tiene que seguir andando sin ella: el latido, el
        // aire y la alarma al control web y a los notificadores (F1-13, banco de caos).
        Tool t;
        t.name = "diag_trabar_ui";
        t.title = ba::i18n::traducir("mcp", "Trabar la interfaz (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "SOLO EN MODO PRUEBA. Traba el hilo de la interfaz `segundos` (1 a 60) con un "
                        "bucle ocupado, medio segundo después de contestar. Mientras dure, nada de la interfaz "
                        "(ni este MCP) contesta.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}}}}},
                         {"required", {"segundos"}}};
        t.run = [](const json& a, Done done) {
            const int s = std::clamp(a.value("segundos", 5), 1, 60);
            done(ToolResult().data(json{{"trabando_s", s}}));
            QTimer::singleShot(500, qApp, [s]() {
                const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(s);
                while (std::chrono::steady_clock::now() < fin) {
                }
            });
        };
        core_.addTool(std::move(t));
    }
    if (rt.config().modoPrueba) {
        // Solo con --test: el banco de caos (tools/caos/) rompe a propósito el codificador
        // o el sumidero de una salida a los N s (FallasProgramadas → motor/Fallas.hpp).
        Tool t;
        t.name = "diag_falla";
        t.title = ba::i18n::traducir("mcp", "Inyectar una falla en una salida (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "SOLO EN MODO PRUEBA. Programa una falla: `pieza` codificador (deja de codificar, "
                        "como un codificador de placa que se cae) o sumidero (no puede escribir: disco o conexión) de "
                        "`salida` grabacion o transmision, dentro de `en_s` segundos (0 = ya). La primera de ese tipo "
                        "que pase se la lleva, una vez. `salida` video con `pieza` gpu: la GPU del video se pierde (el mezclador "
                        "avisa el error de una GPU perdida y la app rearma el motor). `limpiar`: borra lo programado. Siempre "
                        "devuelve lo programado y lo ya disparado.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"pieza", {{"type", "string"}, {"enum", {"codificador", "sumidero", "gpu"}}}},
                           {"salida", {{"type", "string"}, {"enum", {"grabacion", "transmision", "video"}}}},
                           {"en_s", {{"type", "number"}, {"minimum", 0}, {"maximum", 3600}}},
                           {"limpiar", {{"type", "boolean"}}}}}};
        t.run = [](const json& a, Done done) {
            auto& f = ba::fault::FallasProgramadas::instancia();
            if (a.value("limpiar", false)) f.limpiar();
            if (a.contains("pieza") || a.contains("salida")) {
                const std::string e = f.programar(a.value("salida", std::string()), a.value("pieza", std::string()),
                                                  static_cast<std::int64_t>(a.value("en_s", 0.0) * 1000));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data(f.estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_remux";
        t.title = ba::i18n::traducir("mcp", "Pasar una grabacion a MP4/MOV");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Pasa un `archivo` .mkv a `formato` (mp4, de fabrica, o mov) sin recodificar, en otro hilo "
                        "(record_disco dice como va). `borrar_mkv`: borrar el original si salio bien (de fabrica false).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties",
                          {{"archivo", {{"type", "string"}}},
                           {"formato", {{"type", "string"}, {"enum", {"mp4", "mov"}}}},
                           {"borrar_mkv", {{"type", "boolean"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string f = a.value("archivo", std::string());
            std::error_code ec;
            if (!std::filesystem::exists(f, ec)) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no existe %1"), {f})));
            if (!rt.engine().remux(f, a.value("formato", std::string("mp4")), a.value("borrar_mkv", false)))
                return done(ToolResult::error(rt.engine().lastError()));
            done(ToolResult().text(ba::i18n::traducir("mcp", "Pasando; record_disco dice como va.")).data(rt.engine().remuxEstado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_ajustes";
        t.title = ba::i18n::traducir("mcp", "Ajustes de la grabacion");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "`contenedor`: mkv (de fabrica, sobrevive a un corte), mp4, mov o flv (mp4/mov se graban en MKV "
                        "y se pasan al terminar); `remux_al_terminar`. `calidad`: como_stream (comparte el encode de "
                        "la transmisión) o archivo (codificador propio a calidad constante `crf`, 14-28; un encode "
                        "más). `pistas`: las pistas EXTRA de la Matriz (2-6) que se graban aparte, cada una con lo "
                        "que se le cruce ahí (la 1, el programa, va siempre). `copia_dir`: segunda grabación igual en "
                        "otra carpeta/disco (\"\" = sin copia). Rige en la próxima grabación. Sin argumentos, los "
                        "muestra junto con la grabación en curso.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"contenedor", {{"type", "string"}, {"enum", {"mkv", "mp4", "mov", "flv"}}}},
                           {"remux_al_terminar", {{"type", "boolean"}}},
                           {"calidad", {{"type", "string"}, {"enum", {"como_stream", "archivo"}}}},
                           {"crf", {{"type", "integer"}, {"minimum", 10}, {"maximum", 35}}},
                           {"pistas", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 2}, {"maximum", 6}}}}},
                           {"copia_dir", {{"type", "string"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            RecordConfig& r = rt.config().rec;
            if (a.contains("contenedor")) r.container = a.value("contenedor", std::string("mkv"));
            if (a.contains("remux_al_terminar")) r.remuxAlTerminar = a.value("remux_al_terminar", true);
            if (a.contains("calidad")) r.calidad = a.value("calidad", std::string("como_stream"));
            if (a.contains("crf")) r.calidadCrf = std::clamp(a.value("crf", 18), 10, 35);
            if (a.contains("pistas") && a["pistas"].is_array()) {
                r.pistas.clear();
                for (const json& p : a["pistas"])
                    if (p.is_number_integer() && p.get<int>() >= 2 && p.get<int>() <= 6) r.pistas.push_back(p.get<int>());
            }
            if (a.contains("copia_dir")) {
                const std::filesystem::path c = a.value("copia_dir", std::string());
                r.copiaDir = c.empty() || c.is_absolute() ? c.string() : (rt.dataRoot() / c).string();
            }
            if (!a.empty()) rt.save();
            done(ToolResult().data(json{{"contenedor", r.container},
                                        {"remux_al_terminar", r.remuxAlTerminar},
                                        {"calidad", r.calidad},
                                        {"crf", r.calidadCrf},
                                        {"pistas", r.pistas},
                                        {"copia_dir", r.copiaDir},
                                        {"en_curso", rt.engine().grabacionEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destinos";
        t.title = ba::i18n::traducir("mcp", "Destinos de la transmision");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los destinos adicionales (respaldo, otras plataformas), que comparten los codificadores del "
                        "principal: cada uno con su estado (activo, reconectando, cuadros perdidos) y si tiene clave "
                        "guardada. El principal es el de stream_start sin `destino`.");
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"principal", rt.engine().status().streaming ? rt.engine().destinoActivo() : ""},
                                        {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_agregar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Agregar o cambiar un destino de transmision");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Un destino adicional: `nombre`, `url` (rtmp://, rtmps://, srt:// o rist://), `clave` (se "
                        "guarda cifrada; mejor que la cargue el operador en Salidas > + Destino…, donde no la ves; en "
                        "SRT es la passphrase, en RTMP la stream key), `respaldo`. SRT: `modo` (caller, listener, "
                        "rendezvous) y `latencia_ms` (120 en LAN, 4× el ping por internet). `url_respaldo`: el punto "
                        "de ingest de respaldo de la plataforma (la misma clave; sale a los dos a la vez con el mismo "
                        "codificador; \"\" lo saca). Con `id` de uno existente, lo cambia (stream_destino_editar es lo mismo).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"url_respaldo", {{"type", "string"}}},
                           {"modo", {{"type", "string"}, {"enum", {"caller", "listener", "rendezvous"}}}},
                           {"latencia_ms", {{"type", "integer"}, {"minimum", 20}, {"maximum", 8000}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            // Sale con el programa de la salida pedida (o la de quien llama; "" = la por omisión).
            json d = a;
            d["salida"] = core_.salidaDe(a);
            std::string id;
            const std::string e = rt.guardarDestino(d, &id);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"id", id}, {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_editar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Cambiar un destino de transmision");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cambia `nombre`, `url`, `url_respaldo` (el ingest de respaldo, misma clave; "
                                                  "\"\" lo saca), `clave` o `respaldo` del destino `id`; con `salida`, "
                                                  "lo pasa a tomar el programa de esa salida.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"url_respaldo", {{"type", "string"}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            json d = a;
            if (a.contains("salida")) d["salida"] = core_.salidaDe(a); // por nombre o id → id
            const std::string e = rt.guardarDestino(d);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar un destino de transmision");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra un destino (si transmite, lo corta)");
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra el destino `id` (si estaba transmitiendo, lo corta) y su clave guardada.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string e = rt.borrarDestino(a.value("id", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_ajustes";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Repetición: cuántos segundos guardar");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Guarda en memoria los últimos `segundos` del programa (5 a 600; 0 la apaga) para "
                        "replay_guardar. Usa el mismo encode que la transmisión. Sin argumentos informa. Cada salida tiene la "
                        "suya (`salida`; la de las demás no queda guardada para el próximo arranque).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (a.contains("segundos"))
                if (const std::string e = rt.ponerReplay(a.value("segundos", 0), salida); !e.empty())
                    return done(ToolResult::error(e));
            done(ToolResult().data(rt.engine().replayEstado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_guardar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Repetir: guardar los últimos segundos");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Escribe a disco los últimos segundos de la repetición (Recordings/repeticiones/) y "
                        "devuelve el archivo. Con `y_armar: true` lo pone en la escena «Repetición» y la deja "
                        "armada para el take (no la saca al aire).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"y_armar", {{"type", "boolean"}}}}}};
        t.run = [this, &rt, &vm](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            const std::string antes = rt.engine().ultimoReplay(salida);
            if (!rt.engine().guardarReplay(salida)) return done(ToolResult::error(rt.engine().lastError()));
            const bool armar = a.value("y_armar", false);
            vm.esperarReplay(
                antes, 8000,
                [&vm, armar, done, salida](const QString& f) {
                    if (f.isEmpty())
                        return done(ToolResult::error(ba::i18n::traducir("mcp", "el clip no llegó a escribirse en 8 s")));
                    json r{{"archivo", f.toStdString()}};
                    if (armar) {
                        const QString e = vm.armarRepeticion(f, salida);
                        r["armada"] = e.isEmpty();
                        if (!e.isEmpty()) r["error_al_armar"] = e.toStdString();
                    }
                    done(ToolResult().data(r));
                },
                salida);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_limpia";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Señal limpia (clean feed)");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "El programa SIN las capas globales (zócalos DSK, logo) ni la placa de seguridad: lo que "
                        "piden un canal o una cadena que ponen su propia gráfica. Sin argumentos informa. "
                        "`activar: true` la arranca: con `destino` (id de stream_destinos: rtmp/srt/rist, con su "
                        "clave guardada) la manda ahí; sin destino la graba en Recordings/limpia/. `activar: false` "
                        "la corta. Es un encode más (x264, en la CPU): mirá `status.cpu`.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"activar", {{"type", "boolean"}}}, {"destino", {{"type", "string"}}}}}};
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (a.contains("activar")) {
                if (a.value("activar", false)) {
                    const std::string e = rt.iniciarSenalLimpia(a.value("destino", std::string()), salida);
                    if (!e.empty()) return done(ToolResult::error(e));
                } else {
                    rt.detenerSenalLimpia(salida);
                }
            }
            done(ToolResult().data(rt.engine().cleanFeedEstado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salidas_profesionales";
        t.title = ba::i18n::traducir("mcp", "Qué salidas profesionales hay");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Qué salidas de contribución trae este equipo y cuáles no, para no prometer lo que no hay.");
        t.run = [&rt](const json&, Done done) {
            const bool ts = motor::hayFabrica("mpegtsmux") && motor::hayFabrica("srtsink");
            done(ToolResult().data(json{
                {"rtmp", motor::hayFabrica("rtmp2sink")},
                {"srt", ts},
                {"rist", false},
                {"senal_limpia", true},
                {"senal_limpia_estado", rt.engine().cleanFeedEstado()},
                {"ndi_salida", false},
                {"decklink_salida", false},
                {"nota", ba::i18n::traducir("mcp", "SRT y RIST salen por el muxer MPEG-TS (stream_destino_agregar con srt:// o "
                                            "rist://). NDI y DeckLink de salida no vienen en el paquete (sus SDK no "
                                            "se pueden redistribuir).")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grabacion_individual";
        t.title = ba::i18n::traducir("mcp", "Grabar cada entrada por separado");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Graba una entrada a su propio archivo, ademas del programa.");
        t.details =
            ba::i18n::traducir("mcp", "Para un evento que despues hay que EDITAR: el programa sirve para subirlo esa misma "
            "noche, y los archivos por entrada para armar la version buena sin haber perdido nada.\n\n"
            "Sin argumentos informa que se esta grabando. Con `arrancar` o `parar` (listas de nombres "
            "de fuente) lo cambia. `todas: true` con `arrancar` toma todas las fuentes de video de la "
            "escena que esta al aire.\n\n"
            "LO QUE CUESTA, y conviene decirlo antes de prender ocho: cada entrada es UNA CODIFICACION "
            "MAS, con su render propio. Ocho camaras son ocho encodes simultaneos y eso no lo hace "
            "cualquier maquina; si la CPU se satura, lo primero que se degrada es el programa, que es "
            "lo que sale al aire. Empeza por las que de verdad vas a editar y mira `status.cpu`.\n\n"
            "EL AUDIO: cada una graba el de su propia entrada (despues de su rack); una entrada sin audio "
            "graba solo el video, y la respuesta lo dice en `audioPropio`.\n\n"
            "Los archivos van a Recordings/entradas/, con el nombre de la fuente adentro.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"arrancar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"parar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"todas",
               {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "Con `arrancar`: todas las de la escena al aire.")}}},
              {"invitado",
               {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Id o nombre de un invitado de la conferencia: graba su fuente.")}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            std::vector<std::string> arrancar;
            if (a.contains("invitado")) {
                const QString f = vm.fuenteDeInvitado(QString::fromStdString(a.value("invitado", std::string())));
                if (f.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ese invitado no tiene una fuente en el programa (¿esta admitido?)")));
                arrancar.push_back(f.toStdString());
            }
            if (a.contains("arrancar") && a["arrancar"].is_array())
                for (const json& s : a["arrancar"])
                    if (s.is_string()) arrancar.push_back(s.get<std::string>());
            if (a.value("todas", false))
                for (const std::string& s : rt.engine().sceneSources(rt.engine().currentScene()))
                    arrancar.push_back(s);

            json fallaron = json::array();
            // Con las reglas del modelo (disco, tope), las mismas que la GUI.
            for (const std::string& s : arrancar)
                if (const std::string e = rt.grabarAparte(s); !e.empty())
                    fallaron.push_back(json{{"fuente", s}, {"motivo", e}});
            if (a.contains("parar") && a["parar"].is_array())
                for (const json& s : a["parar"])
                    if (s.is_string()) rt.stopSourceRecording(s.get<std::string>());

            json grabando = json::array();
            for (const auto& g : rt.engine().sourceRecordings())
                grabando.push_back(json{{"fuente", g.source},
                                        {"archivo", g.path},
                                        {"mb", [&]() {
                                             std::error_code ec;
                                             const auto b = std::filesystem::file_size(g.path, ec);
                                             return ec ? 0.0 : static_cast<double>(b) / 1e6;
                                         }()},
                                        {"audioPropio", g.audioPropio},
                                        {"activa", g.activa}});
            done(ToolResult().data(json{{"grabando", grabando},
                                        {"noSePudo", fallaron},
                                        {"tope", BroadcastEngine::kMaxIndividuales}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_start";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Grabar");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "EMPEZAR a grabar el programa en disco, con la carpeta y el formato de la "
                        "configuracion. Es para cuando todavia no se esta grabando.");
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (rt.engine().grabacionEstado(salida).value("activa", false))
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Ya esta grabando.")));
            const json d = rt.discoGrabacion();
            if (d.value("horas", -1.0) >= 0 && d.value("horas", -1.0) < AppRuntime::kDiscoMinimoH)
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Queda disco para menos de 15 minutos a este bitrate: no se arranca sola. "
                                              "Liberá espacio o que el operador lo arranque desde Salidas.")));
            if (!rt.startRecording(salida))
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No arranco: %1"), {rt.lastError()})));
            // La grabación arranca en el hilo de salidas (la interfaz no espera el
            // codificador): se mira cada 200 ms, sin trabar nada, hasta que tiene archivo.
            auto mirar = std::make_shared<std::function<void(int)>>();
            *mirar = [&rt, d, done, mirar, salida](int n) {
                const json g = rt.engine().grabacionEstado(salida);
                const std::string archivo = g.value("archivo", std::string());
                if (g.value("activa", false) && !archivo.empty()) {
                    QTimer::singleShot(0, [mirar]() { *mirar = nullptr; });
                    return done(ToolResult()
                                    .text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Grabando en %1."), {archivo}))
                                    .data(json{{"archivo", archivo},
                                               {"contenedor_real", std::filesystem::path(archivo).extension().string()},
                                               {"contenedor_pedido", rt.config().rec.container},
                                               {"horas_restantes", d.value("horas", -1.0)}}));
                }
                if (n >= 25 || !rt.engine().outputsActive()) {
                    QTimer::singleShot(0, [mirar]() { *mirar = nullptr; });
                    return done(rt.engine().outputsActive()
                                    ? ToolResult::ok(ba::i18n::traducir("mcp", "Pedido: la grabación está arrancando (todavía sin archivo)."))
                                    : ToolResult::error(ba::i18n::traducir("mcp", "La grabación no arrancó: mirá el aviso en la interfaz.")));
                }
                QTimer::singleShot(200, [mirar, n]() {
                    if (*mirar) (*mirar)(n + 1);
                });
            };
            (*mirar)(0);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_stop";
        t.porSalida = true;
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Detener grabacion");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la grabación");
        t.description = ba::i18n::traducir("mcp", "PARA de grabar: cierra el archivo y deja de escribir en disco. Con `salida`, la grabación de esa salida.");
        t.run = [this, &rt](const json& a, Done done) {
            rt.stopRecording(core_.salidaDe(a));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Grabacion detenida.")));
        };
        core_.addTool(std::move(t));
    }

    // --- Configuracion -------------------------------------------------------------
    {
        Tool t;
        t.name = "settings_list";
        t.title = ba::i18n::traducir("mcp", "Ajustes");
        t.category = "config";
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Todos los ajustes (o los de una seccion) con clave, valor, tipo, opciones, rango, ayuda y "
            "alcance (live = en el momento, gateway = reinicia la conferencia, restart = al proximo arranque).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"section", {{"type", "string"}}}}}};
        SettingsModel* settings = settings_.get();
        t.run = [settings](const json& a, Done done) {
            settings->revert(); // lo vigente, sin borradores viejos
            json rows = modelRows(*settings);
            const std::string section = a.value("section", std::string());
            if (!section.empty()) {
                json f = json::array();
                // Sin acentos ni mayúsculas: «Codificacion» (como se llamaba
                // antes) sigue encontrando «Codificación».
                const auto plano = [](const std::string& x) {
                    return QString::fromStdString(x).normalized(QString::NormalizationForm_D)
                        .remove(QRegularExpression("\\p{Mn}")).toCaseFolded();
                };
                for (const auto& r : rows)
                    if (plano(r.value("section", "")) == plano(section)) f.push_back(r);
                rows = f;
            }
            json sections = json::array();
            for (const QString& s : settings->sections())
                sections.push_back(s.toStdString());
            done(ToolResult().data(json{{"sections", sections}, {"settings", rows}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "settings_set";
        t.title = ba::i18n::traducir("mcp", "Cambiar ajustes");
        t.category = "config";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia ajustes (los de video reinician el motor y los de gateway la conferencia: cortan)");
        t.aireSi = AireSi::AjustesQueCortan;
        t.description =
            ba::i18n::traducir("mcp", "Cambia uno o varios ajustes por clave y los aplica y guarda. Los de alcance 'gateway' "
            "reinician la conferencia (los invitados se reconectan solos); los 'motor' (el lienzo, la salida, los "
            "cuadros por segundo, el formato y el color: video.width, video.height, video.outputWidth, "
            "video.outputHeight, video.fps, video.formato, video.color, video.rango) REINICIAN EL MOTOR; los "
            "'restart', al proximo arranque.") + "\n\n" + avisoReiniciaMotor();
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"values", {{"type", "object"}, {"description", ba::i18n::traducir("mcp", "{clave: valor} (ver settings_list).")}}},
              {"resetSection",
               {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Volver a los valores por defecto de una seccion.")}}}}}};
        t.examples = {R"({"values": {"encoder.bitrate": 4500, "video.fps": "30"}})",
                      R"({"values": {"room.lobby": false}})"};
        SettingsModel* settings = settings_.get();
        t.run = [settings, &vm](const json& a, Done done) {
            // En vivo (sale algo de verdad: StudioViewModel::modoVivo), lo que reinicia el
            // gateway o el motor no se aplica: lo decide apply(), igual que en la app. Correr
            // el guion sin transmitir no es estar en vivo (antes este chequeo lo confundía).
            // La salvaguarda y el propio servidor MCP solo se tocan desde la
            // interfaz: un asistente (quiza manipulado) no puede aflojarlos.
            if (a.value("resetSection", std::string()) == "Asistente (MCP)")
                return done(ToolResult::error(
                    ba::i18n::traducir("mcp", "La seccion Asistente (MCP) solo se cambia desde la app (Configuracion).")));
            if (a.contains("values") && a["values"].is_object())
                for (auto it = a["values"].begin(); it != a["values"].end(); ++it)
                    if (it.key().rfind("mcp.", 0) == 0)
                        return done(
                            ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' solo se cambia desde la app (Configuracion -> "
                                                                                   "Asistente): es la salvaguarda del propio "
                                                                                   "asistente."),
                                                               {it.key()})));
            settings->revert();
            if (a.contains("resetSection")) settings->resetSection(QString::fromStdString(a["resetSection"]));
            std::vector<std::string> unknown;
            if (a.contains("values") && a["values"].is_object()) {
                for (auto it = a["values"].begin(); it != a["values"].end(); ++it) {
                    const QString key = QString::fromStdString(it.key());
                    if (!settings->value(key).isValid()) {
                        unknown.push_back(it.key());
                        continue;
                    }
                    settings->setValue(key, jsonToQv(it.value()));
                }
            }
            if (!unknown.empty()) {
                std::string u;
                for (const auto& k : unknown)
                    u += " " + k;
                settings->revert();
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Claves desconocidas:%1. settings_list muestra las validas."), {u})));
            }
            // Lo que rearma el motor se dice en la respuesta (y al operador, en la app).
            std::vector<std::string> motor;
            if (a.contains("values") && a["values"].is_object())
                for (auto it = a["values"].begin(); it != a["values"].end(); ++it)
                    if (settings->alcance(QString::fromStdString(it.key())) == "motor" &&
                        settings->value(QString::fromStdString(it.key())) != settings->savedValue(QString::fromStdString(it.key())))
                        motor.push_back(it.key());
            // La vista se recarga sola (AppRuntime avisa «Ajustes» al aplicar).
            const QString summary = settings->apply();
            if (settings->fallo()) return done(ToolResult::error(summary.toStdString()));
            std::string texto = ba::i18n::llenar(ba::i18n::traducir("mcp", "Aplicado. %1"), {summary.toStdString()});
            if (!motor.empty()) {
                std::string ks;
                for (const auto& k : motor) ks += (ks.empty() ? "" : ", ") + k;
                texto = ba::i18n::llenar(ba::i18n::traducir("mcp", "⚠ El motor se reinició por %1: se cortó por unos segundos todo lo que "
                                                                  "sale y las cámaras se volvieron a abrir. Avisale al operador."),
                                         {ks}) + "\n" + texto;
                vm.notify(QString::fromStdString(ba::i18n::llenar(
                              ba::i18n::traducir("mcp", "El asistente cambió %1: el motor se reinició (se cortó lo que sale)."), {ks})),
                          "warn");
            }
            done(ToolResult::ok(texto));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "app_save";
        t.title = ba::i18n::traducir("mcp", "Guardar configuracion");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Guarda escenas y ajustes en disco (las herramientas ya guardan solas; esto fuerza).");
        t.run = [&rt](const json&, Done done) {
            done(rt.save() ? ToolResult::ok(ba::i18n::traducir("mcp", "Guardado.")) : ToolResult::error(rt.lastError()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
