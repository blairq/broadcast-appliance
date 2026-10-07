// ============================================================================
// src/app/mcp/McpService.cpp — arranque del servidor MCP y herramientas del
// motor: estado, escenas, fuentes, audio, salidas y configuracion.
// (Imagenes: McpImageTools.cpp · Sala y oradores: McpRoomTools.cpp ·
//  Interfaz y sistema: McpUiTools.cpp · Ayuda: McpHelp.cpp)
// ============================================================================

#include "mcp/McpService.hpp"
#include "Piezas.hpp" // motor::hayFabrica
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
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>


#include <fstream>

#include "AppRuntime.hpp"
#include "AppSettings.hpp"
#include "InputSpecs.hpp"
#include "mcp/McpBotoneraTools.hpp"
#include "mcp/McpEventoTools.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpPreguntaTools.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/OperatorMessages.hpp"
#include "ui/PropertiesModel.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

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
    return "ANTES DE PREGUNTARLE NADA, mira `rostro_lobby` y busca su `uid` (" + uid +
           "):\n"
           "- Si viene con `nombre`, YA SABES QUIEN ES: saludala por su nombre, confirmale en UNA "
           "sola pregunta el cargo que figura en su ficha, y no le vuelvas a preguntar lo que ya "
           "esta ahi. Preguntarle el nombre a alguien que el sistema reconoce es hacerle perder el "
           "tiempo.\n"
           "- Si `parecido` esta cerca del corte (0.40 o mas) pero sin `nombre`, casi seguro ES "
           "alguien de la libreta y lo que falta es una cara mejor: deciselo por `chat_send` con "
           "las palabras de `sugerencia` (por ejemplo «mira de frente a la camara»), espera unos "
           "segundos y volve a mirar `rostro_lobby` ANTES de preguntarle quien es.\n"
           "- Si no la reconoce, atendela como siempre.\n\n";
}

/// Y lo que hay que hacer al final, para que la proxima vez ya la conozca.
QString guardarLaCara(const QString& uid) {
    return QString(" Al final, cuando tengas su nombre y su cargo, guardale la cara: mira "
                   "`rostro_lobby` para sacar su `feed` (uid ") +
           uid +
           ") y llama `rostro_guardar {nombre, cargo, feed}` —si ya tenia ficha, `rostro_sumar "
           "{id, feed}`—. Si `rostro_calidad` dice que la cara no sirve, pedile primero lo que "
           "diga `sugerencia`. Asi la proxima vez que venga la reconoce sola y no hay que "
           "preguntarle todo de nuevo.";
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
/// está `imagen_analizar`, que se lo encarga a un agente aparte.
bool puedeVerImagenes(McpCore& core) { return core.contextoEnCurso().rol == "vision"; }

/// Dónde quedan las capturas para que las mire otro. Una por archivo, con un
/// nombre que no se repite. (Declarada en el header: la usa `imagen_analizar`.)
QString guardarCaptura(const QImage& img, const std::filesystem::path& raiz, const json& args,
                       int defaultWidth) {
    const QString dir = QString::fromStdWString((raiz / "Runtime" / "capturas").wstring());
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
        r.text("(no se pudo guardar la captura)");
        return;
    }
    r.text("Captura guardada en " + archivo.toStdString() + " (" + std::to_string(img.width()) + "x" +
           std::to_string(img.height()) +
           "). NO te la mando acá: una imagen en esta conversación cuelga la sesión. Para saber "
           "qué se ve, usá `imagen_analizar` con una pregunta concreta.");
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
                err = "layout {x,y,w,h} va en fracciones del lienzo (0..1), no en píxeles";
                return false;
            }
            const float bw = static_cast<float>(lw), bh = static_cast<float>(lh);
            if (nota != nullptr)
                *nota = "layout en píxeles: convertido a fracciones del lienzo de " + std::to_string(lw) + "x" +
                        std::to_string(lh) + " (la próxima, mandá 0..1)";
            x /= bw;
            w /= bw;
            y /= bh;
            h /= bh;
        }
        if (w <= 0 || h <= 0) {
            err = "w y h deben ser > 0 (fracciones del lienzo, 1 = todo)";
            return false;
        }
        return true;
    }
    if (!v.is_string()) {
        err = "layout debe ser un preset (texto) o {x,y,w,h} en fracciones del lienzo";
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
    err =
        "preset desconocido '" + p +
        "'. Validos: full, left, right, top, bottom, center, top-left, top-right, bottom-left, bottom-right, "
        "pip-tl, pip-tr, pip-bl, pip-br, lower-third, third-1..3, grid-<n>-<i> (ej. grid-4-2), o {x,y,w,h}.";
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object();
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(v.dump())).toVariant();
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
                    leer("source_list", json::object(), [=](bool, const json& src, const std::string&) {
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
                                              leer("laminas_listar", json::object(),
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
        // Si la recepcion esta prendida, es de ella: un agente de un tiro con
        // su juego de herramientas, y el asistente interactivo no se entera.
        agent::Despachador& d = vm_.despachador();
        if (const agent::Rol* r = d.roles().porId("recepcion"); r != nullptr && r->activo) {
            core_.startIntake(id.toStdString(), name.toStdString());
            std::string err;
            d.encargar("recepcion",
                       (yaAdentro ? "«" + name.toStdString() +
                                        "» YA ENTRO sola (estaba en la "
                                        "lista de auto-aceptar del evento): no le pidas los datos de "
                                        "cero, mostrale los que ya tenemos para que confirme. "
                                  : reconocerPrimero(id).toStdString()) +
                           "El operador te pide que atiendas a la persona que espera en el lobby (id " +
                           id.toStdString() + ", se anoto como «" + name.toStdString() + "»).",
                       "lobby:" + id.toStdString(), 0, &err);
            return QString::fromStdString(err);
        }
        auto* ag = static_cast<agent::AgentViewModel*>(vm_.agent());
        if (ag == nullptr || !ag->running()) return "El asistente no está corriendo (pestaña ✦).";
        core_.startIntake(id.toStdString(), name.toStdString());
        if (yaAdentro) {
            // YA ESTA ADENTRO: entro sola porque estaba en la lista del
            // evento. Pedirle los datos de cero a alguien que el sistema
            // reconocio y dejo pasar es contradecirse en la misma frase.
            ag->send("«" + name + "» (id " + id +
                     ") YA ENTRO A LA SALA sola: el sistema la reconocio y estaba en la lista de "
                     "auto-aceptar del evento. NO le pidas los datos de cero. Hablale por "
                     "`chat_send`, saludala por su nombre, deciles que ya esta adentro, y mostrale "
                     "los datos que ya tenemos de ella (`participant_profile` y `rostro_listar`) "
                     "para que confirme si siguen bien o si cambio algo —el cargo sobre todo, que "
                     "es lo que va al aire—. Si te corrige algo, guardalo con `participant_profile` "
                     "y, si cambio su cargo, tambien con `rostro_guardar {id, nombre, cargo}`. "
                     "Avisame con `operator_ask` SOLO si algo no cierra. Manejate sola.");
            return {};
        }
        ag->send(reconocerPrimero(id) + "Atendé vos a la persona que espera en el lobby (id " + id +
                 ", se anotó como «" + name +
                 "»). Hablale por chat con `chat_send` usándola como destinatario, presentate en una línea y "
                 "pedile, de a una pregunta por vez y en español rioplatense: su nombre y apellido tal como "
                 "quiere que se lea en pantalla, su cargo o de qué va a hablar, cómo quiere que la presenten "
                 "al aire, y un teléfono o correo de contacto por si se corta. Leé sus respuestas con "
                 "`chat_history`. A medida que las tengas, completá su ficha con `participant_profile`. "
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
                 "no me preguntes nada más: manejate sola." +
                 guardarLaCara(id));
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
    registerAgenteTools();
    registerModeloTools();
    registerTraduccionTools();
    registerMatrizTools();
    registerPtzTools();
    registerVideoTools();
    registerRedTools();
    registerPluginTools();
    registerDcnTools();
    registerSesionTools();
    registerEquiposTools();
    registerPrompterTools();
    registerPantallaTools();
    registerQrTools();
    registerVentanaTools();
    registerRostroTools();
    // El mapa de categorias va en las `instructions` del initialize (lo arma
    // McpCore en cada initialize, porque depende del rol del cliente) y
    // cuesta unos 400 tokens.
    applySecurity();
    // Con todas las herramientas ya registradas (ver verificarJuegos).
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
    // (chat_history y guest_chat_pending marcan su respuesta solo si traen mensajes.)
    static const std::set<std::string> kAlways{"share_list", "share_view"};
    // Devuelven nombres, datos o video de invitados cuando hay alguien en la sala.
    static const std::set<std::string> kWithPeople{"room_status",    "speakers_status",   "capture_program",
                                                   "capture_source", "capture_multiview", "capture_feed",
                                                   "capture_person", "capture_people",    "capture_sequence",
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
        json p;
        std::string err;
        if (!resolveParticipant(roomSnapshot(vm_), who, p, err)) return false;
        const std::string id = p.value("id", "");
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    });
    // Guardas de aire (McpAire): qué es "hay aire" y cuándo una PTZ cuenta
    // (la cámara está en la escena al aire).
    core_.aire().cargar(rt_.dataRoot() / "Config" / "mcp-aire.json");
    const auto hayAire = [this]() {
        const EngineStatus st = rt_.engine().status();
        if (st.streaming || st.recording) return true;
        auto* ev = static_cast<EventoViewModel*>(vm_.evento());
        return ev != nullptr && ev->enVivo();
    };
    core_.setHayAire(hayAire);
    core_.aire().setCondicion([this](const std::string&, const json& a) {
        const std::string e = a.value("entrada", std::string());
        if (e.empty()) return true;
        const auto ss = rt_.engine().sceneSources(rt_.engine().currentScene());
        return std::find(ss.begin(), ss.end(), e) != ss.end();
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
    if (auto* ag = static_cast<agent::AgentViewModel*>(vm_.agent())) {
        ag->setHayAire(hayAire);
        ag->setAireInfo([this]() {
            const json j{{"hay_aire", core_.hayAire()}, {"herramientas", core_.aire().lista()}};
            return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
        });
    }
    core_.setApprover(
        [this](const Tool& t, const json& args, const std::string& reason, std::function<void(bool)> decide) {
            auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
            std::string a = args.empty() ? std::string("(sin argumentos)") : args.dump(2);
            if (a.size() > 1200) a = a.substr(0, 1200) + "\n…";
            const bool deAire = reason.rfind("ACCIÓN DE AIRE", 0) == 0;
            const int id = msgs->post(json{{"kind", "question"},
                                           {"approval", true},
                                           {"aire", deAire},
                                           {"priority", deAire || t.destructive ? "critical" : "warning"},
                                           {"title", deAire ? "ACCIÓN DE AIRE: ¿la confirmás?"
                                                            : "¿Aprobás esta acción del asistente?"},
                                           {"text", t.title + " (" + t.name + ")"},
                                           {"details", a},
                                           {"reason", reason},
                                           {"options", {"Aprobar", "Rechazar"}},
                                           {"allowText", false}});
            msgs->waitAnswer(id, 10 * 60 * 1000, [msgs, id, decide](json item) {
                if (item.is_null()) msgs->dismiss(id); // vencio: la tarjeta se va y queda rechazada
                decide(!item.is_null() && item.value("answer", "") == "Aprobar");
            });
        });
}

McpService::~McpService() = default;

bool McpService::start(quint16 port) {
    // La traza completa arranca con el servidor: lo que pase antes no existe
    // todavia para nadie de afuera.
    Traza::la().abrir(rt_.dataRoot() / "Logs");
    // T47: de fábrica, un token nuevo en cada arranque (el puente ba-mcp lo lee de
    // Credenciales/ en cada pedido; un cliente HTTP directo lo vuelve a pedir con `ba-mcp --token`).
    const QString cred = QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring());
    bool rotar = true;
    {
        std::ifstream f(rt_.dataRoot() / "Config" / "seguridad.json");
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) rotar = j.value("rotar_token_mcp", true);
    }
    const QString token = rotar ? rotarToken(cred) : loadOrCreateToken(cred);
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
        writePrivateJson(QString::fromStdString((runtime / "mcp.json").string()),
                         QJsonDocument(o).toJson(QJsonDocument::Compact));
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
    core_.addCategory("estado", "Estado", "Vistazo general: qué está al aire, salidas, salud.");
    core_.addCategory("escenas", "Escenas",
                      "Escenas: listar, cambiar la del aire, crear, definir completas, exportar.");
    core_.addCategory("fuentes", "Fuentes",
                      "Lo que compone cada escena: cámaras, imágenes, textos, invitados...");
    core_.addCategory("audio", "Audio", "Mezclador: volumen, silencio, niveles.");
    core_.addCategory("salidas", "Salidas", "Transmisión (RTMP) y grabación.");
    core_.addCategory("config", "Configuración",
                      "Todos los ajustes del appliance (video, codificación, sala, red...).");
    // La botonera y el bus de eventos (ver McpBotoneraTools.cpp).
    registerBotoneraTools(core_, vm_);
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

    // Las preguntas del publico: viven en B, y esto es el lado de A que las
    // modera (ver McpPreguntaTools.cpp).
    registerPreguntaTools(core_, vm_, rt_);

    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    const auto persist = [&rt, &vm]() {
        rt.save();
        vm.refreshModels();
    };
    // La invariante, puesta una vez y para todas: lo que se toca por MCP se ve
    // en la interfaz. El despachador la aplica a TODA herramienta que modifica
    // algo, asi ninguna superficie nueva depende de que su autor se acuerde.
    // Ver McpCore::setSincronizador.
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
        return ToolResult::error("No existe la escena '" + s + "'. Escenas: " + names + ".");
    };
    const json sceneProp = {{"type", "string"},
                            {"description", "Escena (por defecto, la que esta al aire)."}};
    const json layoutProp = {
        {"description", "Ubicacion: preset (full, left, right, top, bottom, center, top-left, top-right, "
                        "bottom-left, bottom-right, pip-tl, pip-tr, pip-bl, pip-br, lower-third, third-1..3, "
                        "grid-<n>-<i>) u objeto {x,y,w,h} en fracciones del lienzo (0..1)."}};

    // --- Estado --------------------------------------------------------------------
    {
        Tool t;
        t.name = "status";
        t.title = "Estado general";
        t.category = "estado";
        t.readOnly = true;
        t.idempotent = true;
        t.description =
            "Estado del appliance: escena al aire, transmision y grabacion (con tiempos), fps, cuadros "
            "perdidos, codificador, CPU, niveles del programa, gateway, sala y oradores al aire. "
            "Buen primer llamado.";
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
            done(ToolResult().data(
                json{{"currentScene", rt.engine().currentScene()},
                     {"transition",
                      {{"type", rt.config().transition.type}, {"ms", rt.config().transition.durationMs}}},
                     {"streaming", st.streaming},
                     {"streamTime", vm.streamTime().toStdString()},
                     {"streamKbps", vm.streamKbps()},
                     {"streamServer", vm.streamServer().toStdString()},
                     {"streamKeyAvailable", vm.streamKeyAvailable()},
                     {"recording", st.recording},
                     {"recordTime", vm.recordTime().toStdString()},
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
                     {"room", room}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_ftb";
        t.title = "Fundido a negro (FTB)";
        t.category = "escenas";
        t.description = "Fundido a negro de TODO lo que sale (encima del programa, sin tocar las escenas). `activar` "
                        "true/false, `ms` de fundido (de fábrica 800), `audio`: bajar (de fábrica: el audio baja con "
                        "el negro) o mantener. Activarlo al aire es una acción de aire (pide confirmación si lo pide "
                        "algo automático); sacarlo, no.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}},
                           {"audio", {{"type", "string"}, {"enum", {"bajar", "mantener"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.ftbUi(a.value("activar", false), a.value("ms", 800),
                                       QString::fromStdString(a.value("audio", std::string("bajar"))));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(qvToJson(vm.estadoPlacaUi())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_placa";
        t.title = "Placa de seguridad";
        t.category = "escenas";
        t.description = "Una placa encima de todo lo que sale (\"Cuarto intermedio\", \"Volvemos enseguida\", \"Minuto "
                        "de silencio\"…), con `texto`, una `imagen` opcional (ruta) y `ms` de fundido (600). No depende "
                        "de las escenas. Activarla al aire pide confirmación si la pide algo automático; sacarla, no. "
                        "Sobrevive a una caída de la app.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"texto", {{"type", "string"}}},
                           {"imagen", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.placaUi(a.value("activar", false), QString::fromStdString(a.value("texto", std::string())),
                                         QString::fromStdString(a.value("imagen", std::string())), a.value("ms", 600));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(qvToJson(vm.estadoPlacaUi())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_placas_listar";
        t.title = "Textos de placa y estado";
        t.category = "escenas";
        t.readOnly = true;
        t.description = "Los textos de placa de siempre y si ahora hay FTB o placa al aire.";
        t.run = [&vm](const json&, Done done) {
            json textos = json::array();
            for (const QString& s : vm.textosPlaca()) textos.push_back(s.toStdString());
            done(ToolResult().data(json{{"textos", textos}, {"estado", qvToJson(vm.estadoPlacaUi())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escenas_ajustes";
        t.title = "Ajustes del editor de escenas";
        t.category = "escenas";
        t.description = "`corte_directo_editor`: el botón de cada escena en el editor corta directo (true) o la arma "
                        "en el preview para salir con TAKE (false, de fábrica). Sin argumentos, los muestra.";
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
        t.title = "Herramientas de aire";
        t.category = "estado";
        t.readOnly = true;
        t.idempotent = true;
        t.description =
            "Las herramientas que cambian el aire (cortar, cambiar de escena, sacar a alguien, parar la "
            "transmisión…). Con la transmisión o la grabación andando, si las pide algo automático (asistente, "
            "plugin, receta, disparador, voz) esperan SIEMPRE que una persona confirme. `clase`: aire (siempre) "
            "o aire_si (según los argumentos: settings de gateway/video/encoder, mute del máster, sacar un "
            "orador, PTZ de la cámara al aire). `hay_aire`: si ahora cuentan. En vivo, la voz y los disparadores "
            "solo leen o hacen lo reversible sin preguntar.";
        t.run = [this](const json&, Done done) {
            done(ToolResult().data(json{{"hay_aire", core_.hayAire()}, {"herramientas", core_.aire().lista()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_aire_ajustar";
        t.title = "Sumar o sacar una herramienta de las de aire";
        t.category = "estado";
        t.description = "`clase`: aire (pide confirmación en vivo) o normal. Las de la lista fija no bajan. Solo "
                        "fuera del aire (con la transmisión o la grabación andando, no se cambia).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"herramienta", "clase"}},
                         {"properties",
                          {{"herramienta", {{"type", "string"}}},
                           {"clase", {{"type", "string"}, {"enum", {"aire", "normal"}}}}}}};
        t.run = [this](const json& a, Done done) {
            if (core_.hayAire()) return done(ToolResult::error("Al aire no se cambian las guardas de aire."));
            const std::string e = core_.aire().ajustar(a.value("herramienta", std::string()), a.value("clase", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(core_.aire().lista()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "supervisor_politica";
        t.title = "Política del supervisor (interfaz trabada)";
        t.category = "estado";
        t.description =
            "El supervisor relanza la app si deja de latir. El latido sale de la interfaz Y de un hilo que mira "
            "los cuadros del motor: si la interfaz se traba y el motor sigue, el aire NO se corta. Con la "
            "interfaz trabada más de `espera_ui_s` (10 a 3600, de fábrica 60) y `matar_si_ui_trabada_sin_aire` "
            "(de fábrica true), se relanza solo si no hay transmisión ni grabación. Sin argumentos, la muestra "
            "(con el estado: motor_vivo, ui_viva, ui_trabada_ms, ultima_traba_ms).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"espera_ui_s", {{"type", "integer"}, {"minimum", 10}, {"maximum", 3600}}},
                           {"matar_si_ui_trabada_sin_aire", {{"type", "boolean"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("espera_ui_s") || a.contains("matar_si_ui_trabada_sin_aire")) {
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
        t.title = "Listar escenas";
        t.category = "escenas";
        t.readOnly = true;
        t.idempotent = true;
        t.description =
            "El listado de escenas que existen: cual esta al aire, en que carpeta esta cada una. "
            "Para el contenido de UNA escena, capa por capa, no es esta. "
            "Todas las escenas con sus capas (de abajo hacia arriba), cual esta al aire y en que "
            "carpeta esta cada una (`folder`, \"\" = suelta). Las carpetas solo ordenan la lista de la "
            "pestaña Escenas: no cambian nada de la emision.\n\n"
            "`live` es la que esta AL AIRE; `armada` es la del bus de preview, la que sale con el "
            "proximo `scene_take`. Son los dos buses de una mesa de switcher.";
        t.run = [&rt, &vm](const json&, Done done) {
            const QString armada = vm.escenaArmada();
            json scenes = json::array();
            for (const std::string& n : rt.orderedSceneNames()) {
                scenes.push_back(json{{"name", n},
                                      {"live", n == rt.engine().currentScene()},
                                      {"armada", QString::fromStdString(n) == armada},
                                      {"folder", rt.sceneFolder(n)},
                                      {"sources", rt.engine().sceneSources(n)}});
            }
            done(ToolResult().data(json{{"current", rt.engine().currentScene()},
                                        {"armada", armada.toStdString()},
                                        {"folders", rt.sceneFolders()},
                                        {"scenes", scenes}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_folder";
        t.title = "Mover una escena a una carpeta";
        t.category = "escenas";
        t.description =
            "QUE ES: las carpetas agrupan escenas en la lista de la pestaña Escenas (arbol que se "
            "pliega). PARA QUE: ordenar cuando hay muchas (bloques del programa, placas, invitados). "
            "COMO SE USA: `folder` con el nombre de la carpeta (se crea sola si no existe) o vacio para "
            "sacar la escena de toda carpeta. No cambia nada de la emision ni del contenido de la "
            "escena; `scene_list` informa la carpeta de cada una.";
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
        t.title = "Reordenar una escena";
        t.category = "escenas";
        t.description =
            "QUE ES: cambia el ORDEN de la lista de escenas (y de paso la carpeta). PARA QUE: dejar las "
            "escenas en el orden del programa. COMO SE USA: `before` es la escena delante de la cual "
            "queda (vacio = al final); ademas hereda su carpeta. No cambia nada de la emision. El orden "
            "que informa `scene_list` es el que ve el operador.";
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
        t.title = "Poner una escena al aire (corte directo)";
        t.category = "escenas";
        t.description = "Pone una escena al aire YA, sin pasar por el preview: es «corta a la camara 2», "
                        "«pone la apertura». Cambia la escena del programa (con la transicion configurada). "
                        "Lo que se ve y oye en la "
                        "transmision cambia EN EL ACTO, sin pasar por el bus de preview.";
        t.details =
            "Es el corte de urgencia: sale ya. Para lo demas, la forma de trabajar de una mesa es "
            "`scene_preview` (armar el proximo plano) -> mirarlo con `capture_source` -> `scene_take`. "
            "Dos pasos parece mas lento y es al reves: lo que esta roto se ve ANTES de salir al aire, "
            "no despues.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", "Nombre de la escena."}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Entrevista"})"};
        t.run = [&rt, &vm, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            vm.takeScene(QString::fromStdString(name));
            if (rt.engine().currentScene() != name)
                return done(ToolResult::error("No se pudo: " + rt.lastError()));
            done(ToolResult::ok("Al aire: " + name));
        };
        core_.addTool(std::move(t));
    }
    // --- El bus de preview: armar, mirar, TAKE --------------------------------
    {
        Tool t;
        t.name = "scene_preview";
        t.title = "Armar la proxima escena (sin sacarla al aire)";
        t.category = "escenas";
        t.description =
            "Pone una escena en el bus de PREVIEW: queda armada para el proximo `scene_take`. NO cambia "
            "lo que se ve en la transmision.";
        t.details =
            "Asi se opera una mesa de switcher: se arma el proximo plano, se MIRA (`capture_source` con "
            "el mismo nombre da la foto de como va a salir), y recien ahi `scene_take`.\n\n"
            "Armar la escena que YA esta al aire no tiene sentido (el take cortaria a lo mismo) y se "
            "rechaza.\n\n"
            "`scene_list` dice cual esta armada y cual al aire.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.examples = {R"({"name": "06 Debate invitados"})"};
        t.run = [&rt, &vm, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            vm.armarEscena(QString::fromStdString(name));
            if (vm.escenaArmada().toStdString() != name)
                return done(ToolResult::error("No se armo: «" + name + "» ya esta al aire."));
            done(ToolResult::ok("Armada: " + name + ". Mirala con capture_source y sacala con scene_take."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_cut";
        t.title = "CUT: corte seco a la armada";
        t.category = "escenas";
        t.description = "Pone al aire la escena armada con un CORTE (sin transicion), aunque la global sea un fundido. "
                        "Deja armada la que salia.";
        t.run = [&rt, &vm](const json&, Done done) {
            const QString armada = vm.escenaArmada();
            if (armada.isEmpty()) return done(ToolResult::error("No hay nada armado (scene_preview)."));
            vm.takeCon("cut", 0);
            if (rt.engine().currentScene() != armada.toStdString()) return done(ToolResult::error("No se pudo: " + rt.lastError()));
            done(ToolResult::ok("AL AIRE (corte): " + armada.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transicion_manual";
        t.title = "T-bar: transicion a mano";
        t.category = "escenas";
        t.description = "Lleva la transicion hacia la escena armada a mano: `t` de 0 a 1 (la palanca). Con `soltar`, "
                        "termina: pasada la mitad queda al aire, si no vuelve. Para un fader de la botonera: "
                        "{\"t\": \"{{valor}}\"} con el fader de 0 a 1.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"t"}},
                         {"properties", {{"t", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}, {"soltar", {{"type", "boolean"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            vm.tbar(a.value("t", 0.0), a.value("soltar", false));
            done(ToolResult().data({{"al_aire", rt.engine().currentScene()}, {"hacia", rt.engine().manualHacia()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "escena_transicion";
        t.title = "Transicion de entrada de una escena";
        t.category = "escenas";
        t.description = "La transicion con la que ENTRA la `escena` al aire (`tipo`: cut, fade, swipe, slide, "
                        "fade_to_color; vacio = la global) y su duracion `ms`. Un take con transicion explicita manda sobre esta.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"escena"}},
                         {"properties",
                          {{"escena", {{"type", "string"}}},
                           {"tipo", {{"type", "string"}, {"enum", {"", "cut", "fade", "swipe", "slide", "fade_to_color"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.setTransicionEntrada(QString::fromStdString(a.value("escena", std::string())),
                                                      QString::fromStdString(a.value("tipo", std::string())), a.value("ms", -1));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Listo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_afv";
        t.title = "Audio que sigue al video";
        t.category = "audio";
        t.description = "Con `sigue_video`, la `fuente` sube (a su volumen) cuando entra al aire una escena que la tiene y "
                        "baja a cero cuando sale, con la duracion de la transicion. Sin `sigue_video`, lista las que lo tienen.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"fuente", {{"type", "string"}}}, {"sigue_video", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("fuente") && a.contains("sigue_video"))
                vm.setAfv(QString::fromStdString(a.value("fuente", std::string())), a.value("sigue_video", false));
            json l = json::array();
            for (const QString& f : vm.afvFuentes()) l.push_back(f.toStdString());
            done(ToolResult().data({{"sigue_video", l}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_take";
        t.title = "TAKE: sacar al aire la escena armada";
        t.category = "escenas";
        t.description = "Pone al aire la escena que estaba armada en el bus de preview, con la transicion "
                        "configurada. Deja armada la que salia, para poder volver de un golpe.";
        t.details = "Sin nada armado no hace nada y lo dice: armar primero con `scene_preview`.\n\n"
                    "Que el plano que SALE del aire quede armado es como funciona cualquier mesa: "
                    "despues de un take, volver a lo anterior es un solo take mas.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"transicion", {{"type", "string"}, {"enum", {"cut", "fade", "swipe", "slide", "fade_to_color"}},
                                           {"description", "otra transicion solo para este take (si no, la de entrada de la escena o la global)"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            const QString armada = vm.escenaArmada();
            if (armada.isEmpty())
                return done(ToolResult::error(
                    "No hay nada armado. Arma el proximo plano con scene_preview {\"name\": ...}."));
            vm.takeCon(QString::fromStdString(a.value("transicion", std::string())), a.value("ms", -1));
            if (rt.engine().currentScene() != armada.toStdString())
                return done(ToolResult::error("No se pudo: " + rt.lastError()));
            done(ToolResult::ok("AL AIRE: " + armada.toStdString() +
                                ". Queda armada: " + vm.escenaArmada().toStdString() + "."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_create";
        t.title = "Crear escena vacia";
        t.category = "escenas";
        t.description = "Crea una escena vacia. Para crearla con contenido de una vez, usa scene_define.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            if (!rt.addScene(a["name"].get<std::string>())) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok("Escena creada: " + a["name"].get<std::string>()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_copy";
        t.title = "Copiar una escena";
        t.category = "escenas";
        t.description = "Crea una escena nueva con todas las fuentes de otra.";
        t.details = "Es como se arman las escenas parecidas sin repetir el trabajo: se hace UNA bien —la de "
                    "entrevista, con su camara, su zocalo y su fondo— y las demas salen de copiarla y "
                    "cambiarle lo que difiere.\n\n"
                    "Las fuentes se RECREAN, no se comparten: quedan dos escenas de verdad independientes, y "
                    "mover algo en la copia no mueve el original.\n\n"
                    "La copia nunca queda protegida, aunque el original lo este: para eso se copia. Si "
                    "alguna fuente no se pudo recrear, la escena igual queda creada y se dice cual fallo: "
                    "una escena a medias se arregla a mano, una que desaparecio sin explicacion no.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"from", {{"type", "string"}, {"description", "La escena que se copia."}}},
                           {"to", {{"type", "string"}, {"description", "Nombre de la copia."}}}}},
                         {"required", {"from", "to"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string from = a["from"].get<std::string>();
            const std::string to = a["to"].get<std::string>();
            if (!rt.copyScene(from, to)) return done(ToolResult::error(rt.lastError()));
            persist();
            // `lastError` con la copia hecha = alguna fuente no se pudo
            // recrear. Se dice, pero no es un error: la escena existe.
            const std::string aviso = rt.lastError();
            done(ToolResult::ok("Escena «" + to + "» copiada de «" + from + "»." +
                                (aviso.empty() ? "" : " Ojo: " + aviso)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_protect";
        t.title = "Proteger una escena";
        t.category = "escenas";
        t.description = "Una escena protegida no se puede borrar ni renombrar.";
        t.details = "Para la escena BASE de la que se copian las demas —la que si alguien borra se pierde el "
                    "trabajo de todos— y para las que no se tocan en vivo.\n\n"
                    "Copiarla si se puede, y editar sus fuentes tambien: lo que se protege es que "
                    "desaparezca o cambie de nombre, que es lo que rompe todo lo que la nombra (el guion, "
                    "los vinculos de rotulos, las recetas).";
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
            done(ToolResult::ok(p ? "«" + s + "» queda protegida: no se borra ni se renombra."
                                  : "«" + s + "» ya no esta protegida."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_delete";
        t.title = "Borrar escena";
        t.category = "escenas";
        t.destructive = true;
        t.description = "Borra una escena (las fuentes compartidas con otras escenas siguen existiendo).";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.run = [&rt, persist, sceneExists, noScene](const json& a, Done done) {
            const std::string name = a["name"];
            if (!sceneExists(name)) return done(noScene(name));
            if (!rt.removeScene(name)) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok("Escena borrada: " + name));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_rename";
        t.title = "Renombrar escena";
        t.category = "escenas";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"from", {{"type", "string"}}}, {"to", {{"type", "string"}}}}},
                         {"required", {"from", "to"}}};
        t.description = "Cambia el nombre de una escena.";
        t.run = [&rt, persist, sceneExists, noScene](const json& a, Done done) {
            if (!sceneExists(a["from"])) return done(noScene(a["from"]));
            if (!rt.renameScene(a["from"], a["to"])) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok("Renombrada a " + a["to"].get<std::string>()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_export";
        t.title = "Exportar definicion de escenas";
        t.category = "escenas";
        t.readOnly = true;
        t.idempotent = true;
        t.description =
            "Definicion completa (tipo, settings, ubicacion, visibilidad, volumen) de una escena o de "
            "todas, en el mismo formato que acepta scene_define: sirve para copiar, versionar o "
            "modificar escenas.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", "Una escena (vacio = todas)."}}}}}};
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
                return done(ToolResult::error("No existe la escena '" + only + "'."));
            done(ToolResult().data(json{{"scenes", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_define";
        t.title = "Definir una escena completa";
        t.category = "escenas";
        t.description =
            "Crea (o rehace con replace=true) una escena con todas sus fuentes de una vez, en orden de "
            "abajo hacia arriba. Una fuente con el nombre de otra que ya existe se COMPARTE (misma "
            "camara en dos escenas); si trae settings, se actualizan. Al rehacer, las que vuelven a venir "
            "en la lista se CONSERVAN (con su camara, grafo y filtros): solo se quitan las que no estan. "
            "`layout` en fracciones del lienzo (0..1); si llega en pixeles se convierte y se avisa.";
        t.details =
            "Cada fuente: {type, name, settings?, layout?, visible?, volume?, muted?}. `type` es el id del "
            "tipo (ver source_types): p. ej. image_source {file}, color_source_v3 {color, width, "
            "height}, text_ft2_source_v2 {text, font:{face,size}, color1}, ffmpeg_source {local_file, "
            "looping}, v4l2_input (camara), ba_guest_video {slot, channel: guest|share}, ba_subtitulos "
            "{flujo: \"Orador · en\", "
            "lineas, caracteres, ancho} (subtitulos de una salida de texto). `layout`: preset "
            "o {x,y,w,h}. Consejo: exporta una escena con scene_export, modificala y pasala aca.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}}},
              {"sources",
               {{"type", "array"},
                {"description", "Fuentes de abajo hacia arriba."},
                {"items",
                 {{"type", "object"},
                  {"properties",
                   {{"type", {{"type", "string"}}},
                    {"name", {{"type", "string"}}},
                    {"settings", {{"type", "object"}}},
                    {"layout", layoutProp},
                    {"visible", {{"type", "boolean"}}},
                    {"volume", {{"type", "number"}, {"description", "Ganancia lineal (1 = 0 dB)."}}},
                    {"muted", {{"type", "boolean"}}}}},
                  {"required", {"type", "name"}}}}}},
              {"replace",
               {{"type", "boolean"},
                {"description", "Si existe, rehacerla (las fuentes que se repiten se conservan)."},
                {"default", false}}},
              {"switch",
               {{"type", "boolean"}, {"description", "Ponerla al aire al terminar."}, {"default", false}}}}},
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
        t.run = [&rt, persist, sceneExists](const json& a, Done done) {
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
                        ToolResult::error("La escena '" + name +
                                          "' ya existe: usa replace=true para rehacerla, u otro nombre."));
                antes = rt.engine().sceneSources(name);
                int quitadas = 0;
                for (const std::string& s : antes)
                    if (!listadas.count(s)) {
                        rt.removeSource(name, s);
                        ++quitadas;
                    }
                log.push_back("rehecha (" + std::to_string(quitadas) + " quitada(s); las que siguen, conservadas)");
            } else if (!rt.addScene(name)) {
                return done(ToolResult::error(rt.lastError()));
            }
            int added = 0;
            for (const json& s : a["sources"]) {
                SourceSpec spec;
                spec.id = s.value("type", std::string());
                spec.name = s.value("name", std::string());
                spec.settingsJson = s.contains("settings") ? s["settings"].dump() : "{}";
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
                        rt.updateSourceSettings(spec.name, cur.dump());
                    }
                    if (s.contains("volume")) rt.setVolume(spec.name, spec.volume);
                    if (s.contains("muted")) rt.setMuted(spec.name, spec.muted);
                    ++added;
                    log.push_back(spec.name + ": conservada (ya estaba)");
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
                if (existed) log.push_back(spec.name + ": compartida (ya existia)");
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
            if (a.value("switch", false)) rt.setScene(name);
            persist();
            std::string msg = "Escena '" + name + "': " + std::to_string(added) + " fuente(s).";
            for (const auto& l : log)
                msg += "\n- " + l;
            msg += "\nVerifica como quedo con capture_source {\"name\": \"" + name + "\"}.";
            done(ToolResult::ok(msg));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transition_set";
        t.title = "Transicion entre escenas";
        t.category = "escenas";
        t.description = "Tipo y duracion de la transicion al cambiar de escena.";
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
            done(ToolResult::ok("Transicion: " + a["type"].get<std::string>()));
        };
        core_.addTool(std::move(t));
    }

    // --- Fuentes ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "source_types";
        t.title = "Tipos de fuente disponibles";
        t.category = "fuentes";
        t.readOnly = true;
        t.idempotent = true;
        t.description =
            "Tipos de fuente que esta instalacion puede crear (id, nombre, si tiene video "
            "y/o audio). El id es el `type` de source_add / scene_define.";
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
        t.name = "source_list";
        t.title = "Fuentes de una escena";
        t.category = "fuentes";
        t.readOnly = true;
        t.idempotent = true;
        t.description = "Lo que hay ADENTRO de una escena: sus capas, una por una. Fuentes de una escena (de "
                        "abajo hacia arriba) con tipo, visibilidad, ubicacion en el "
                        "lienzo, volumen y silencio.";
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
                out.push_back(s);
            }
            done(ToolResult().data(json{{"scene", scene}, {"sources", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_add";
        t.title = "Agregar fuente";
        t.category = "fuentes";
        t.description =
            "Agrega una fuente a una escena (arriba de todo): texto, titulo, zocalo, imagen, logo, "
            "video, camara, color, invitado... Si ya existe una fuente con ese nombre "
            "(en otra escena) se agrega LA MISMA (compartida).";
        t.details =
            "Settings mas comunes: image_source {file: ruta}, ffmpeg_source {local_file, looping: true}, "
            "color_source_v3 {color: ABGR entero, width, height}, text_ft2_source_v2 {text, font: {face, "
            "size}, color1}, v4l2_input {device_id}. Para ver todas las opciones de una fuente ya creada: "
            "source_properties.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"scene", sceneProp},
                           {"type", {{"type", "string"}, {"description", "Id del tipo (source_types)."}}},
                           {"name", {{"type", "string"}}},
                           {"settings", {{"type", "object"}}},
                           {"layout", layoutProp},
                           {"visible", {{"type", "boolean"}, {"default", true}}}}},
                         {"required", {"type", "name"}}};
        t.examples = {
            R"({"type": "image_source", "name": "Logo", "settings": {"file": "/home/yo/logo.png"}, "layout": "pip-tr"})"};
        t.run = [&rt, persist, sceneOr, sceneExists, noScene](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            if (!sceneExists(scene)) return done(noScene(scene));
            SourceSpec spec;
            spec.id = a["type"];
            spec.name = a["name"];
            spec.settingsJson = a.contains("settings") ? a["settings"].dump() : "{}";
            spec.visible = a.value("visible", true);
            spec.rect = CanvasRect{0.1f, 0.1f, 0.8f, 0.8f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok("Agregada '" + spec.name + "' a '" + scene + "'."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_remove";
        t.title = "Quitar fuente de una escena";
        t.category = "fuentes";
        t.destructive = true;
        t.description = "Saca la fuente de la escena (si esta en otras escenas, ahi sigue).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"scene", sceneProp}, {"name", {{"type", "string"}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, persist, sceneOr](const json& a, Done done) {
            if (!rt.removeSource(sceneOr(a), a["name"])) return done(ToolResult::error(rt.lastError()));
            persist();
            done(ToolResult::ok("Quitada: " + a["name"].get<std::string>()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "source_update";
        t.title = "Modificar una fuente en una escena";
        t.category = "fuentes";
        t.description =
            "Cambia una fuente: mostrar/ocultar, mover o cambiar tamaño (layout), orden (order), cambiar "
            "el texto de un titulo o zocalo, la imagen, etc. De una vez lo que se indique: visibilidad, "
            "ubicacion (layout), orden (order), "
            "settings (se MEZCLAN con los actuales), volumen, silencio o nombre.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"scene", sceneProp},
              {"name", {{"type", "string"}}},
              {"visible", {{"type", "boolean"}}},
              {"layout", layoutProp},
              {"order",
               {{"type", json::array({"string", "integer"})},
                {"description", "top | bottom | up | down | pasos (+ sube)."}}},
              {"settings", {{"type", "object"}, {"description", "Solo las claves a cambiar."}}},
              {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
              {"muted", {{"type", "boolean"}}},
              {"rename", {{"type", "string"}, {"description", "Nuevo nombre (en todas las escenas)."}}},
              {"recorte",
               {{"type", "object"},
                {"description",
                 "Lo que se ve de la fuente en ESTA escena: {izquierda, arriba, derecha, abajo} en fracciones de "
                 "su tamaño (0..1). Con el layout, amplía una zona (p. ej. comparar el mismo detalle). Todo en 0 "
                 "lo saca."},
                {"properties",
                 {{"izquierda", {{"type", "number"}}},
                  {"arriba", {{"type", "number"}}},
                  {"derecha", {{"type", "number"}}},
                  {"abajo", {{"type", "number"}}}}}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Logo", "layout": "pip-br", "visible": true})",
                      R"({"name": "Titulo principal", "settings": {"text": "Ultimo momento"}})",
                      R"({"name": "Camara", "order": "top"})"};
        t.run = [&rt, persist, sceneOr](const json& a, Done done) {
            const std::string scene = sceneOr(a);
            std::string name = a["name"];
            const auto items = rt.engine().sceneSources(scene);
            if (std::find(items.begin(), items.end(), name) == items.end())
                return done(ToolResult::error("'" + name + "' no esta en la escena '" + scene +
                                              "'. source_list la muestra."));
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
                (rt.updateSourceSettings(name, cur.dump()) ? did : failed).push_back("settings");
            }
            if (a.contains("volumeDb")) {
                const float mul = static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0));
                (rt.setVolume(name, mul) ? did : failed).push_back("volumen");
            }
            if (a.contains("muted")) (rt.setMuted(name, a["muted"]) ? did : failed).push_back("silencio");
            if (a.contains("rename")) {
                const std::string to = a["rename"];
                if (rt.engine().renameSource(name, to)) {
                    for (auto& sc : rt.config().scenes)
                        for (auto& s : sc.sources)
                            if (s.name == name) s.name = to;
                    name = to;
                    did.push_back("nombre");
                } else {
                    failed.push_back("nombre (" + rt.engine().lastError() + ")");
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
        t.title = "Propiedades de una fuente";
        t.category = "fuentes";
        t.readOnly = true;
        t.description =
            "Todas las opciones que la fuente expone (las mismas del panel Propiedades): nombre, "
            "etiqueta, tipo, valor actual, opciones posibles, rango. Sirve para cualquier plugin.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        PropertiesModel* props = props_.get();
        t.run = [&rt, props](const json& a, Done done) {
            if (rt.engine().sourceTypeId(a["name"]).empty())
                return done(ToolResult::error("No existe la fuente '" + a["name"].get<std::string>() + "'."));
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
        t.title = "Cambiar una propiedad (o apretar un boton)";
        t.category = "fuentes";
        t.description =
            "Cambia una propiedad de source_properties por su `name` (se aplica en vivo), o aprieta una "
            "propiedad tipo boton (press=true), p. ej. 'refrescar dispositivos'.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", {{"type", "string"}}},
                           {"property", {{"type", "string"}}},
                           {"value", {{"description", "Valor (tipo segun la propiedad)."}}},
                           {"press", {{"type", "boolean"}}}}},
                         {"required", {"source", "property"}}};
        PropertiesModel* props = props_.get();
        t.run = [&rt, props, persist](const json& a, Done done) {
            if (rt.engine().sourceTypeId(a["source"]).empty())
                return done(
                    ToolResult::error("No existe la fuente '" + a["source"].get<std::string>() + "'."));
            props->setSourceName(QString::fromStdString(a["source"]));
            const QString prop = QString::fromStdString(a["property"]);
            if (a.value("press", false)) {
                props->press(prop);
            } else {
                if (!a.contains("value")) return done(ToolResult::error("Falta `value` (o press=true)."));
                props->setValue(prop, jsonToQv(a["value"]));
            }
            persist();
            done(ToolResult::ok("Hecho. Settings ahora: " + rt.engine().sourceSettingsJson(a["source"])));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guest_source_add";
        t.title = "Poner a un invitado en una escena";
        t.category = "fuentes";
        t.description =
            "Agrega la camara (o la pantalla compartida) del invitado del lugar `slot` a una escena. Es "
            "la misma fuente en todas las escenas; se ve quien este en ese lugar de la sala.";
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
                return done(ToolResult::error("Hay " + std::to_string(rt.config().guests) +
                                              " lugares de invitado (ajuste guests.count)."));
            const bool share = a.value("kind", std::string("camera")) == "screen";
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot) + (share ? " — pantalla" : "");
            spec.settingsJson = json{{"slot", slot}, {"channel", share ? "share" : "guest"}}.dump();
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
            done(ToolResult::ok("'" + spec.name + "' en '" + scene + "'."));
        };
        core_.addTool(std::move(t));
    }

    // --- Audio ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "mixer_list";
        t.title = "Mezclador";
        t.category = "audio";
        t.readOnly = true;
        t.description = "Foto de los niveles del mezclador, para leer. "
                        "Fuentes de audio con volumen (dB), silencio, si se esta escuchando por el monitor "
                        "(`escuchando`, ver audio_escuchar) y nivel pico actual (dBFS), mas el nivel del "
                        "programa. -100 = silencio; cerca de 0 = saturando.";
        t.run = [&rt, &vm](const json&, Done done) {
            json out = json::array();
            for (const std::string& n : rt.engine().audioSources()) {
                const float mul = rt.engine().sourceVolume(n);
                out.push_back(json{{"name", n},
                                   {"volumeDb", mul > 0 ? 20.0 * std::log10(mul) : -100.0},
                                   {"muted", rt.engine().sourceMuted(n)},
                                   // Cual se esta escuchando por el monitor
                                   // (audio_escuchar). Sin esto el PFL es
                                   // invisible: queda una fuente sonando en
                                   // los auriculares y nadie sabe cual.
                                   {"escuchando", rt.engine().sourceMonitoring(n)},
                                   {"peakDb", rt.engine().sourcePeakDb(n)}});
            }
            done(ToolResult().data(
                json{{"sources", out}, {"programPeakDb", {vm.programPeakL(), vm.programPeakR()}}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_set";
        t.title = "Volumen / silencio";
        t.category = "audio";
        t.description =
            "Silencia, sube o baja el audio de una fuente. Es con lo que se CORTA un acople, se baja lo que "
            "satura, se sube lo que no se escucha o se cierra un microfono que quedo abierto. Cambia el "
            "volumen (dB, 0 = sin cambio, negativo baja) y/o silencia una fuente de audio.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
                           {"muted", {{"type", "boolean"}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string n = a["name"];
            bool ok = true;
            if (a.contains("volumeDb"))
                ok =
                    rt.setVolume(n, static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0))) &&
                    ok;
            if (a.contains("muted")) ok = rt.setMuted(n, a["muted"]) && ok;
            if (!ok)
                return done(
                    ToolResult::error("No existe la fuente de audio '" + n + "' (mixer_list las muestra)."));
            persist();
            done(ToolResult::ok("Audio de '" + n + "' actualizado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_escuchar";
        t.title = "Escuchar una fuente por el monitor (PFL), sin tocar el aire";
        t.category = "audio";
        t.description =
            "Manda una fuente de audio al monitor del operador para escucharla. NO cambia lo que sale "
            "al aire: lo que estaba saliendo sigue saliendo igual.";
        t.details =
            "Es el PFL de una consola: antes de abrir un microfono se lo escucha para ver si esta ahi, "
            "si zumba, si el invitado ya volvio. Sin esto habia que ponerlo al aire para enterarse.\n\n"
            "Es EXCLUSIVO, como el solo de una mesa: escuchar una apaga la anterior. Sin argumentos "
            "apaga la que este sonando.\n\n"
            "OJO: una fuente SILENCIADA (`audio_set muted`) no suena ni por el monitor —el silencio es "
            "antes de todo—. Para probar un microfono cerrado hay que quitarle el silencio "
            "y bajarle el volumen, no silenciarlo.\n\n"
            "Que se oiga depende de que el equipo tenga un monitor de audio configurado; sin monitor "
            "esto no falla pero tampoco suena. `audio_sources` dice cual se esta escuchando.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}, {"description", "la fuente; vacio o sin esto = apagar"}}},
              {"escuchar",
               {{"type", "boolean"}, {"description", "false para apagar esta (por defecto true)"}}}}}};
        t.examples = {R"({"name": "Microfono del atril"})", R"({})"};
        t.run = [&rt](const json& a, Done done) {
            const std::string n = a.value("name", std::string());
            const bool on = a.value("escuchar", true) && !n.empty();
            // Exclusivo: se apagan todas y se prende la pedida. Asi «escuchar»
            // siempre quiere decir lo mismo, y nunca quedan dos sonando juntas
            // sin que nadie se acuerde de la primera.
            std::vector<std::string> apagadas;
            for (const std::string& f : rt.engine().audioSources()) {
                if (f == n && on) continue;
                if (rt.engine().sourceMonitoring(f)) {
                    rt.engine().setSourceMonitoring(f, false);
                    apagadas.push_back(f);
                }
            }
            if (!on) {
                return done(ToolResult::ok(apagadas.empty()
                                               ? "No se estaba escuchando ninguna."
                                               : "Dejo de escucharse: " + apagadas.front() + "."));
            }
            if (!rt.engine().setSourceMonitoring(n, true))
                return done(
                    ToolResult::error("No existe la fuente de audio '" + n + "' (audio_sources las lista)."));
            done(ToolResult::ok("Escuchando «" + n + "» por el monitor. Lo que sale al aire no cambio." +
                                (apagadas.empty() ? "" : " (se dejo de escuchar " + apagadas.front() + ")")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "audio_sincro_medir";
        t.title = "Medir la sincronía labios/voz con aplausos";
        t.category = "audio";
        t.description =
            "Mide cuánto hay que atrasar el audio para que coincida con el video, con aplausos frente a la "
            "cámara: durante `segundos` (de 4 a 30, 10 de fábrica) mira lo que SALE (cuadros del programa y "
            "mezcla de audio) y empareja cada aplauso (pico de audio, cambio brusco en la imagen). La cámara "
            "tiene que estar en el programa y su micrófono sonando. Avisá a la persona que aplauda 4 o 5 veces, "
            "separadas. Con `aplicar_a` (una fuente de audio) y una medición confiable, le suma el retardo "
            "medido al que ya tenía (audio_sincro). Devuelve retardo_ms, dispersión y si es confiable.";
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
        t.title = "Balística de los medidores";
        t.category = "audio";
        t.description = "Cómo se mueven todos los medidores de la interfaz: `balistica` = pico (instantáneo), ppm "
                        "(EBU tipo IIb: sube al instante y cae 24 dB en 2,8 s) o vu (integra ~300 ms: el volumen "
                        "percibido). Sin argumentos dice cuál está.";
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
        t.title = "Sincronizar labios y voz";
        t.category = "audio";
        t.description =
            "Atrasa el audio de una fuente para que coincida con su video. Sin `ms`, dice el retardo que "
            "tiene puesto.";
        t.details =
            "Para que sirve: una camara IP llega con 200 ms a 2 s de retardo (codificacion + red + "
            "buffer), mientras que un microfono por la placa de audio llega casi al instante. Se escucha "
            "antes de ver los labios, y eso se nota muchisimo.\n\n"
            "`ms` positivo ATRASA el audio (lo normal con una camara IP); negativo lo adelanta, que sirve "
            "cuando el audio llega despues que el video. Se acota a +-5 s: mas que eso no es una camara "
            "con retardo, es un error.\n\n"
            "Como encontrar el numero: poner un valor, mirar con `capture_program` a alguien hablando y "
            "corregir. El ojo humano es el juez. Un metodo mas fino -medirlo con un aplauso- esta "
            "escrito pero todavia no conectado a las fuentes.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", -5000}, {"maximum", 5000}}}}},
                         {"required", {"name"}}};
        t.run = [&rt, persist](const json& a, Done done) {
            const std::string n = a["name"];
            if (!a.contains("ms")) {
                if (rt.engine().sourceNames().empty())
                    return done(ToolResult::error("El motor todavia no tiene fuentes."));
                return done(ToolResult().data(json{{"name", n}, {"ms", rt.engine().sourceSyncOffset(n)}}));
            }
            if (!rt.engine().setSourceSyncOffset(n, a["ms"].get<int>()))
                return done(ToolResult::error(rt.engine().lastError()));
            persist();
            done(ToolResult::ok("El audio de '" + n + "' va " + std::to_string(a["ms"].get<int>()) +
                                " ms respecto del video."));
        };
        core_.addTool(std::move(t));
    }

    // --- Salidas -------------------------------------------------------------------
    {
        Tool t;
        t.name = "stream_start";
        t.title = "Salir al aire (transmitir)";
        t.category = "salidas";
        t.openWorld = true;
        t.description =
            "Empieza a transmitir por RTMP. Sin argumentos usa el servidor configurado y la clave de la "
            "variable de entorno. La clave NUNCA se guarda en disco.";
        t.details = "Si YA esta transmitiendo: al mismo destino no hace nada (lo dice); a OTRO destino da error "
                    "con `activo_en` (no cambia nada), salvo `cambiar_en_caliente`: true, que corta y arranca al "
                    "nuevo. `destino`: el id de un destino adicional (stream_destinos) o \"todos\": arranca esos, "
                    "ademas del principal.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"server", {{"type", "string"}, {"description", "rtmp://... (opcional)."}}},
              {"key", {{"type", "string"}, {"description", "Clave de transmision (solo en memoria)."}}},
              {"cambiar_en_caliente", {{"type", "boolean"}, {"description", "estando al aire, pasar a este destino"}}},
              {"destino", {{"type", "string"}, {"description", "id de un destino adicional o \"todos\""}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("destino")) {
                const std::string e = rt.iniciarDestino(a.value("destino", std::string()));
                emit vm.destinosChanged();
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().text("Destino iniciado.").data(json{{"destinos", rt.destinos()}}));
            }
            if (!rt.startStreaming(a.value("server", std::string()), a.value("key", std::string()),
                                   a.value("cambiar_en_caliente", false))) {
                if (rt.engine().ultimoInicio() == BroadcastEngine::Inicio::YaOtroDestino) {
                    ToolResult r = ToolResult::error("Ya esta transmitiendo a " + rt.engine().destinoActivo() +
                                                     ": NO se cambio nada. Para pasar a este destino: "
                                                     "`cambiar_en_caliente`: true (corta un instante).");
                    r.structured = json{{"activo_en", rt.engine().destinoActivo()}};
                    return done(r);
                }
                return done(ToolResult::error("No arranco: " + rt.lastError() +
                                              ". Si falta la clave: pasala en `key` o en la variable " +
                                              rt.config().streamKeyEnv + "."));
            }
            emit vm.statusChanged();
            if (rt.engine().ultimoInicio() == BroadcastEngine::Inicio::YaIgual)
                return done(ToolResult::ok("Ya estaba transmitiendo a ese destino: no cambio nada."));
            done(ToolResult::ok("Transmitiendo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_stop";
        t.title = "Cortar la transmision";
        t.category = "salidas";
        t.destructive = true;
        t.description =
            "BAJA del aire: corta la transmision que esta saliendo. Detiene la transmision en vivo. Con "
            "`destino` (id o \"todos\"), corta solo ese destino adicional y el principal sigue.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"destino", {{"type", "string"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("destino")) {
                rt.detenerDestino(a.value("destino", std::string()));
                emit vm.destinosChanged();
                return done(ToolResult::ok("Destino cortado; el resto sigue."));
            }
            rt.stopStreaming();
            emit vm.statusChanged();
            done(ToolResult::ok("Transmision detenida."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness";
        t.title = "Loudness de lo que sale";
        t.category = "audio";
        t.readOnly = true;
        t.description = "El loudness del PROGRAMA (la mezcla que va a la transmision y la grabacion), EBU R128 / "
                        "BS.1770-4: `m` momentaneo (400 ms), `s` corto (3 s), `i` integrado desde el reset (arranca "
                        "solo al transmitir o grabar), `tp` true peak maximo (dBTP), `lra` rango, `gr_db` lo que "
                        "baja el limitador del master, `objetivo` y `fuera_de_objetivo` (integrado a mas de 2 LU o TP "
                        "arriba del techo). audio_measure es por fuente; esto es lo que sale.";
        t.run = [&rt](const json&, Done done) { done(ToolResult().data(rt.engine().loudness().json())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_reset";
        t.title = "Reiniciar el integrado del loudness";
        t.category = "audio";
        t.description = "Vuelve a cero el integrado, el LRA y el true peak maximo del programa.";
        t.run = [&rt](const json&, Done done) {
            rt.engine().loudness().reiniciar();
            done(ToolResult::ok("Reiniciado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_objetivo";
        t.title = "Objetivo de loudness";
        t.category = "audio";
        t.description = "`perfil`: r128 (-23 LUFS / -1 dBTP, de fabrica), a85 (-24 LKFS / -2), plataforma16 (-16 / -1), "
                        "plataforma14 (-14 / -1) o custom con `lufs` y `tp`.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}, {"enum", {"r128", "a85", "plataforma16", "plataforma14", "custom"}}}},
                           {"lufs", {{"type", "number"}}},
                           {"tp", {{"type", "number"}}}}}};
        t.run = [&vm, &rt](const json& a, Done done) {
            QVariantMap m;
            m["perfil"] = QString::fromStdString(a.value("perfil", std::string("r128")));
            if (a.contains("lufs")) m["lufs"] = a.value("lufs", -23.0);
            if (a.contains("tp")) m["tp"] = a.value("tp", -1.0);
            const QString e = vm.objetivoLoudnessUi(m);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(rt.engine().loudness().json()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_info_aprobar";
        t.title = "Aprobar el cargo que escribio un invitado";
        t.category = "sala";
        t.description = "Lo que un invitado escribe de si mismo al entrar (`info`) NO sale en el zocalo hasta que una "
                        "persona lo aprueba: con esto, `participant` (id o nombre) y un `texto` corregido opcional. Si "
                        "esta en el cuerpo (cuerpo_listar), sale de ahi y no hace falta. Confirmalo con el operador.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"participant"}},
                         {"properties", {{"participant", {{"type", "string"}}}, {"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.aprobarInfoInvitado(QString::fromStdString(a.value("participant", std::string())),
                                                      QString::fromStdString(a.value("texto", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Aprobado: el zocalo ya lo usa."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_token_rotar";
        t.title = "Rotar el token del MCP";
        t.category = "config";
        t.description = "Cambia ya el token del servidor MCP (el viejo deja de valer). El puente ba-mcp lo relee solo; un "
                        "cliente HTTP directo lo vuelve a pedir con `ba-mcp --token`.";
        t.run = [this](const json&, Done done) {
            const QString nuevo = rotarToken(QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring()));
            if (nuevo.isEmpty() || !http_) return done(ToolResult::error("no se pudo guardar el token nuevo"));
            http_->setToken(nuevo);
            done(ToolResult::ok("Token rotado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguridad_estado";
        t.title = "Seguridad: estado y ajustes";
        t.category = "config";
        t.description = "Lo que protege los accesos: si el token del MCP se rota en cada arranque, si la sala cambia de "
                        "PIN al empezar cada sesion en vivo, y el modo en vivo. Con `rotar_token_mcp` o `pin_por_sesion` "
                        "los cambia (fuera de vivo).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"rotar_token_mcp", {{"type", "boolean"}}}, {"pin_por_sesion", {{"type", "boolean"}}}}}};
        t.run = [this](const json& a, Done done) {
            const auto ruta = rt_.dataRoot() / "Config" / "seguridad.json";
            json j = json::object();
            {
                std::ifstream f(ruta);
                const json x = json::parse(f, nullptr, false);
                if (x.is_object()) j = x;
            }
            if (a.contains("rotar_token_mcp") || a.contains("pin_por_sesion")) {
                if (vm_.modoVivo()) return done(ToolResult::error("no se cambia en vivo"));
                for (const char* k : {"rotar_token_mcp", "pin_por_sesion"})
                    if (a.contains(k)) j[k] = a.value(k, true);
                std::ofstream o(ruta);
                o << j.dump(1);
            }
            done(ToolResult().data({{"rotar_token_mcp", j.value("rotar_token_mcp", true)},
                                    {"pin_por_sesion", j.value("pin_por_sesion", true)},
                                    {"modo_vivo", vm_.modoVivoEstado()},
                                    {"passphrase", "todavia no (ver docs/planes/DECISIONES-PENDIENTES.md)"}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_estado";
        t.title = "Subtitulos ocultos (CC)";
        t.category = "traduccion";
        t.readOnly = true;
        t.description = "Los subtitulos ocultos de la transmision (el espectador los prende en su reproductor): si estan "
                        "prendidos, de que salida de texto salen (texto_flujos), el metodo (608 en el video, la API de "
                        "YouTube, o los dos), cuantas lineas se mandaron y las ultimas.";
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(qvToJson(vm.cc().estado()))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_activar";
        t.title = "Prender los subtitulos ocultos";
        t.category = "traduccion";
        t.description = "`flujo`: la salida de texto (la transcripcion o una traduccion; texto_flujos). `metodo`: 608 (de "
                        "fabrica: CEA-608 dentro del video de la transmision y de cada destino), youtube_http (la API de "
                        "subtitulos de YouTube: `url` de ingesta del evento) o ambos.";
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
        t.title = "Apagar los subtitulos ocultos";
        t.category = "traduccion";
        t.description = "Deja de mandar subtitulos ocultos.";
        t.run = [&vm](const json&, Done done) {
            vm.cc().desactivar();
            done(ToolResult::ok("Apagados."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_probar";
        t.title = "Probar los subtitulos ocultos";
        t.category = "traduccion";
        t.description = "Manda un `texto` de prueba por lo configurado (608 necesita una transmision activa).";
        t.inputSchema = {{"type", "object"}, {"required", {"texto"}}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.cc().probar(QString::fromStdString(a.value("texto", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Mandado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario";
        t.title = "Vocabulario de la voz a texto";
        t.category = "traduccion";
        t.readOnly = true;
        t.description = "Los terminos que la transcripcion escribe bien aunque el modelo no los conozca (nombres del "
                        "cuerpo, localidades, terminos de asamblea, los del operador), por grupo (activo o no), y los "
                        "ultimos reemplazos hechos. La correccion es por sonido y solo con confianza alta.";
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
        t.title = "Agregar un termino al vocabulario";
        t.category = "traduccion";
        t.description = "Agrega (o cambia) el `termino` bien escrito, con `variantes` (como lo escribe mal el modelo, "
                        "opcional), en el `grupo` (operador de fabrica; cuerpo, localidades, asamblea u otro nuevo). "
                        "Con `grupo` y `activo` y sin `termino`, prende o apaga el grupo. Con `del_cuerpo`: true, pone "
                        "en el grupo cuerpo los nombres y apellidos de cuerpo_listar.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"termino", {{"type", "string"}}},
                           {"variantes", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"grupo", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}},
                           {"del_cuerpo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("del_cuerpo", false))
                return done(ToolResult::ok("Nombres del cuerpo: " + std::to_string(vm.vocabularioDelCuerpoUi()) + "."));
            const std::string termino = a.value("termino", std::string()), grupo = a.value("grupo", std::string());
            if (termino.empty() && !grupo.empty() && a.contains("activo")) {
                vm.vocabulario().activarGrupo(grupo, a.value("activo", true));
                return done(ToolResult::ok("Listo."));
            }
            std::vector<std::string> v;
            for (const auto& x : a.value("variantes", json::array()))
                if (x.is_string()) v.push_back(x.get<std::string>());
            const std::string e = vm.vocabulario().agregar(termino, v, grupo);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok("Agregado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_quitar";
        t.title = "Quitar un termino del vocabulario";
        t.category = "traduccion";
        t.description = "Saca el `termino` (de cualquier grupo).";
        t.inputSchema = {{"type", "object"}, {"required", {"termino"}}, {"properties", {{"termino", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.vocabulario().quitar(a.value("termino", std::string())))
                return done(ToolResult::error("no esta en el vocabulario"));
            done(ToolResult::ok("Quitado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_probar";
        t.title = "Probar el vocabulario";
        t.category = "traduccion";
        t.readOnly = true;
        t.description = "Pasa un `texto` (como lo escribiria el modelo) por la correccion y devuelve el resultado y los "
                        "reemplazos.";
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
        t.title = "Que hacer si una entrada se cae al aire";
        t.category = "video";
        t.description = "Si la entrada `entrada` se queda sin señal mas de `ms` (de fabrica 1000) estando en la escena al "
                        "aire: `al_perder_aire` avisar (de fabrica), armar la `escena` de respaldo en el preview o cortar "
                        "a ella. Sin `al_perder_aire`, muestra lo configurado y las entradas que ahora no tienen señal.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"entrada", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}},
                           {"al_perder_aire", {{"type", "string"}, {"enum", {"avisar", "armar", "cortar"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 200}, {"maximum", 60000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = QString::fromStdString(a.value("entrada", std::string()));
            if (a.contains("al_perder_aire")) {
                if (e.isEmpty()) return done(ToolResult::error("falta `entrada`"));
                QVariantMap r{{"escena", QString::fromStdString(a.value("escena", std::string()))},
                              {"al_perder_aire", QString::fromStdString(a.value("al_perder_aire", std::string()))},
                              {"ms", a.value("ms", 1000)}};
                const QString err = vm.setRespaldoEntrada(e, r);
                if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            }
            json sin = json::array();
            for (const QString& s : vm.sinSenal()) sin.push_back(s.toStdString());
            done(ToolResult().data(json{{"sin_senal", sin},
                                        {"respaldo", e.isEmpty() ? json() : qvToJson(vm.respaldoEntrada(e))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "bancas_listar";
        t.title = "Bancas y fila de oradores presenciales";
        t.category = "oradores";
        t.readOnly = true;
        t.description = "Los oradores PRESENCIALES (desde su banca, sin conexion): todas las bancas del cuerpo, la fila, "
                        "quien habla (al_aire) y hace cuanto (usado_s de asignado_s). speakers_status es la fila de los "
                        "remotos (invitados conectados).";
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.bancas().estadoJson())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_cola";
        t.title = "Sumar o sacar una banca de la fila";
        t.category = "oradores";
        t.description = "`agregar` o `quitar`: banca, id o nombre del cuerpo.";
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
        t.title = "Poner al aire a un orador presencial";
        t.category = "oradores";
        t.description = "Una banca al aire, en una sola accion: la PTZ va al preset de su banca (solo si esa camara no "
                        "esta al aire), la escena de esa camara queda ARMADA en el preview (`modo` armar, de fabrica: "
                        "sale con TAKE) o se corta (directo), el zocalo toma su nombre y cargo del cuerpo ({orador}), "
                        "arranca su reloj y se abre su microfono (y se cierra el de la anterior). `banca`: banca, id o "
                        "nombre. Sin `banca`: la primera de la fila. Si esa persona habla desde el navegador (sesion "
                        "remota, en la sala o afuera), no se mueve ninguna camara: se le da la palabra en su celular "
                        "con la cuenta regresiva. `asignado_s`: el tiempo de cada uno (queda guardado).";
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
            done(ToolResult().text(r.value("aviso", std::string()).empty() ? "Listo." : "Listo, con un aviso: " + r.value("aviso", std::string())).data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_tiempo";
        t.title = "Tiempo del que tiene la palabra";
        t.category = "oradores";
        t.description = "`sumar_s` le suma (o resta, negativo) tiempo al que habla ahora (presencial o remoto: su celular "
                        "se actualiza); `asignado_s` cambia el tiempo de cada turno de aca en adelante. A un minuto y al "
                        "terminar avisa al operador, en el celular del remoto y en la unidad de la central.";
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
        t.title = "Seguir al microfono: estado";
        t.category = "oradores";
        t.readOnly = true;
        t.description = "Si esta prendido, el modo (proponer o automatico), el umbral y los tiempos, los microfonos de "
                        "banca abiertos ahora y la propuesta («Banca 12 – Perez pidio la palabra»), si hay.";
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.seguirMicrofono().estado())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguir_microfono";
        t.title = "Seguir al microfono abierto";
        t.category = "oradores";
        t.description = "Cuando un microfono de banca (cuerpo: microfono.fuente) pasa `umbral_db` durante `sostener_ms`, "
                        "propone poner esa banca al aire. `modo` proponer (de fabrica: el operador acepta) o automatico "
                        "(la arma sola en el preview; nunca corta, y no mueve una camara al aire). `accion` aceptar "
                        "(arma la propuesta) o descartar.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"proponer", "automatico"}}}},
                           {"fuente", {{"type", "string"}, {"enum", {"niveles", "conferencia"}},
                                       {"description", "niveles de los microfonos (de fabrica) o el sistema de conferencia del recinto (plugin conferencia-recinto)"}}},
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
        t.title = "Cargar el orden del dia";
        t.category = "oradores";
        t.description = "Carga los puntos de la sesion desde `texto`: una linea por punto («1. Expte. 123/24 – Titulo», "
                        "«2) Titulo») o CSV («n;expediente;titulo»). Reemplaza los que habia (el uso de la palabra sigue). "
                        "Con `limpiar`: true empieza una sesion nueva (borra puntos y tiempos).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"texto", {{"type", "string"}}}, {"limpiar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("limpiar", false)) vm.ordenDelDia().limpiar();
            if (!a.contains("texto")) return done(ToolResult::ok("Sesion nueva."));
            int n = 0;
            const std::string e = vm.ordenDelDia().cargar(a.value("texto", std::string()), &n);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text("Cargados " + std::to_string(n) + " puntos.").data({{"puntos", vm.ordenDelDia().puntos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_ver";
        t.title = "Orden del dia";
        t.category = "oradores";
        t.readOnly = true;
        t.description = "Los puntos con su estado (pendiente, en_tratamiento, votado, comision, retirado), cuando se "
                        "trataron y el resultado; el punto actual; el uso de la palabra por orador (y por punto); y, si "
                        "se esta grabando, los capitulos por punto para pegar en YouTube.";
        t.run = [&vm](const json&, Done done) {
            auto& o = vm.ordenDelDia();
            done(ToolResult().data({{"puntos", o.puntos()},
                                    {"actual", o.actual()},
                                    {"tiempos_palabra", o.tiemposPalabra(vm.remotosPalabra())},
                                    {"capitulos", vm.capitulosPuntosUi().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_punto";
        t.title = "Cambiar el estado de un punto";
        t.category = "oradores";
        t.description = "El punto `n` pasa a `estado` (en_tratamiento: cierra el anterior, pone «Punto N: …» en {punto} "
                        "para los zocalos y lo marca para el acta y los capitulos), con `resultado` opcional («aprobado "
                        "por unanimidad»). Con `siguiente`: true, el proximo pendiente pasa a tratamiento.";
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
    {
        Tool t;
        t.name = "banca_terminar";
        t.title = "Terminar al orador presencial";
        t.category = "oradores";
        t.description = "Saca del aire al orador presencial: cierra su microfono y para su reloj (la escena no cambia).";
        t.run = [&vm](const json&, Done done) {
            vm.bancas().terminar();
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_listar";
        t.title = "Integrantes del cuerpo";
        t.category = "evento";
        t.readOnly = true;
        t.description = "El cuerpo deliberativo: nombre e integrantes con banca, cargo oficial, bloque, distrito, "
                        "preset PTZ de su banca (camara/posicion), microfono y si estan VALIDADOS (validado_por). Es "
                        "la fuente unica para los zocalos, las bancas de la fila de oradores y el PTZ por banca.";
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.cuerpo().cuerpo().todo())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_crear";
        t.title = "Nombrar el cuerpo";
        t.category = "evento";
        t.description = "Pone el nombre del cuerpo (p. ej. «Asamblea 2025-2029»). No borra integrantes.";
        t.inputSchema = {{"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            vm.cuerpo().setNombre(QString::fromStdString(a.value("nombre", std::string())));
            done(ToolResult::ok("Listo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_importar";
        t.title = "Importar la planilla de integrantes";
        t.category = "evento";
        t.description = "Importa una planilla CSV (coma o punto y coma, con encabezado: banca, nombre, cargo, bloque, "
                        "distrito, camara, posicion, microfono). Los que ya estan (por banca o nombre) se actualizan. "
                        "`reemplazar`: borra los de antes. cuerpo_plantilla escribe una de ejemplo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties", {{"archivo", {{"type", "string"}}}, {"reemplazar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const json r = vm.cuerpo().cuerpo().importarCsv(a.value("archivo", std::string()), a.value("reemplazar", false));
            emit vm.cuerpo().cambio();
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuerpo_plantilla";
        t.title = "Planilla de integrantes de ejemplo";
        t.category = "evento";
        t.description = "Escribe una planilla CSV de ejemplo (encabezado y dos filas) en Exportado/ y devuelve la ruta.";
        t.run = [&vm](const json&, Done done) {
            const QString r = vm.cuerpo().plantilla();
            if (r.isEmpty()) return done(ToolResult::error("no se pudo escribir la planilla"));
            done(ToolResult().data(json{{"archivo", r.toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_editar";
        t.title = "Agregar o cambiar un integrante del cuerpo";
        t.category = "evento";
        t.description = "Con `id` lo cambia; sin `id`, lo crea (`nombre` obligatorio). Campos: nombre, cargo_oficial, "
                        "bloque, distrito, banca (unica), camara + posicion (el preset PTZ de su banca), microfono (la "
                        "fuente de audio de su banca), dcn_unidad (la unidad de la central Bosch DCN de su banca; vacío la "
                        "saca), persona_id (la ficha de Personas). Si cambia el nombre o el cargo, "
                        "deja de estar validado (integrante_validar).";
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
                           {"persona_id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            QVariantMap m = QJsonDocument::fromJson(QByteArray::fromStdString(a.dump())).object().toVariantMap();
            const QString e = vm.cuerpo().guardar(m);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(vm.cuerpo().cuerpo().buscar(a.value("banca", a.value("nombre", a.value("id", std::string()))))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_ver";
        t.title = "Ver un integrante del cuerpo";
        t.category = "evento";
        t.readOnly = true;
        t.description = "Busca por id, banca o nombre.";
        t.inputSchema = {{"type", "object"}, {"required", {"clave"}}, {"properties", {{"clave", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const json r = vm.cuerpo().cuerpo().buscar(a.value("clave", std::string()));
            if (r.empty()) return done(ToolResult::error("no hay nadie con esa banca, id o nombre (cuerpo_listar)"));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_validar";
        t.title = "Validar los datos de un integrante";
        t.category = "evento";
        t.description = "Marca que una persona revisó el nombre y el cargo de un integrante (lo que sale en el zocalo). "
                        "Confirmalo con el operador: es lo que hace que salga al aire sin aviso.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.cuerpo().validar(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error("no existe ese id"));
            done(ToolResult::ok("Validado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_borrar";
        t.title = "Sacar a un integrante del cuerpo";
        t.category = "evento";
        t.destructive = true;
        t.description = "Borra un integrante (`id`).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.cuerpo().borrar(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error("no existe ese id"));
            done(ToolResult::ok("Borrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "integrante_foto";
        t.title = "La foto de un integrante";
        t.category = "evento";
        t.description = "La foto de un integrante (`banca`, id o nombre), que es la cara de su botón en la botonera: "
                        "`archivo` (una imagen: se recorta cuadrada) o `capturar: true` (lleva la cámara de su banca a "
                        "su preset, si no está al aire, y toma lo que se ve; tarda unos segundos y el resultado llega "
                        "como aviso). `quitar: true` la saca.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"banca"}},
                         {"properties",
                          {{"banca", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}},
                           {"capturar", {{"type", "boolean"}}},
                           {"quitar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const json i = vm.cuerpo().cuerpo().buscar(a.value("banca", std::string()));
            if (i.empty()) return done(ToolResult::error("no hay nadie con esa banca, id o nombre en el cuerpo"));
            const QString id = QString::fromStdString(i.value("id", std::string()));
            if (a.value("quitar", false)) {
                vm.cuerpo().borrarFoto(id);
                return done(ToolResult::ok("Foto quitada."));
            }
            if (a.value("capturar", false)) {
                vm.cuerpo().capturarFoto(id);
                return done(ToolResult::ok("Capturando con la cámara de su banca (unos segundos)."));
            }
            const QString e = vm.cuerpo().subirFoto(id, QString::fromStdString(a.value("archivo", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Foto cargada."));
        };
        core_.addTool(std::move(t));
    }
    // --- El recinto: la grilla de bancas (asamblea/Cuerpo) ------------------------
    {
        Tool t;
        t.name = "recinto_ver";
        t.title = "La grilla del recinto";
        t.category = "evento";
        t.readOnly = true;
        t.description = "Las bancas como se ven desde la cabina: {filas, columnas, bancas: [{banca, fila, columna, "
                        "integrante: {id, nombre, bloque, preset_ptz, validado}}], sin_lugar: [bancas del cuerpo que "
                        "no están en la grilla]}. Fila 0 = la de adelante; columna 0 = la izquierda desde la cabina.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.cuerpo().cuerpo().recinto())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_editar";
        t.title = "Armar la grilla del recinto";
        t.category = "evento";
        t.description = "Cambia la grilla: `filas` y `columnas`; `auto: true` acomoda todas las bancas del cuerpo en "
                        "orden (de adelante para atrás, de izquierda a derecha); `mover: {banca, fila, columna}` una "
                        "sola (fila -1 la saca); `bancas: [{banca, fila, columna}]` reemplaza la lista entera. Si algo "
                        "no cierra (fuera de la grilla, dos en un lugar), no cambia nada y dice por qué. "
                        "`distribucion`: cómo se dibuja (grilla, hemiciclo —cada fila un arco alrededor de la "
                        "presidencia— o lista).";
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
                           {"bancas", {{"type", "array"}, {"items", {{"type", "object"}}}}}}}};
        t.examples = {R"({"filas": 4, "columnas": 9, "auto": true})", R"({"mover": {"banca": "12", "fila": 0, "columna": 4}})"};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.cuerpo().cuerpo().setRecinto(a);
            if (!e.empty()) return done(ToolResult::error(e));
            emit vm.cuerpo().cambio();
            done(ToolResult().data(vm.cuerpo().cuerpo().recinto()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_apuntar";
        t.title = "Apuntar la cámara a una banca";
        t.category = "camaras";
        t.description = "Lleva la PTZ al preset de la banca (por banca, id o nombre) para tenerla lista, sin armar ni "
                        "abrir micrófonos. No mueve la cámara que está al aire. Para ponerla al aire: banca_al_aire.";
        t.inputSchema = {{"type", "object"}, {"required", {"banca"}}, {"properties", {{"banca", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.bancas().apuntar(QString::fromStdString(a.value("banca", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Cámara en camino a la banca " + a.value("banca", std::string()) + "."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_retorno";
        t.title = "Lo que oye cada invitado";
        t.category = "sala";
        t.description = "Mix-minus: cada invitado recibe el programa SIN su propia voz (de fabrica), asi no se escucha "
                        "con retardo ni hace eco. `retorno`: mix_minus o programa (el programa entero). Por `slot` "
                        "(1..invitados). Sin `retorno`, dice lo de cada slot.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"slot", {{"type", "integer"}, {"minimum", 1}}},
                           {"retorno", {{"type", "string"}, {"enum", {"mix_minus", "programa"}}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("retorno")) {
                const QString e = vm.setRetornoInvitadoUi(a.value("slot", 0), QString::fromStdString(a.value("retorno", std::string())));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            json out = json::object();
            for (int s = 1; s <= rt.config().guests; ++s) out[std::to_string(s)] = rt.engine().retornoInvitado(s);
            done(ToolResult().data(json{{"retornos", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_disco";
        t.title = "Disco de la grabacion";
        t.category = "salidas";
        t.readOnly = true;
        t.description = "Cuanto disco queda en la carpeta de grabacion y para cuantas horas alcanza al bitrate "
                        "configurado; y el estado del ultimo pase a MP4 (remux).";
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"disco", rt.discoGrabacion()}, {"remux", rt.engine().remuxEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_remux";
        t.title = "Pasar una grabacion a MP4/MOV";
        t.category = "salidas";
        t.description = "Pasa un `archivo` .mkv a `formato` (mp4, de fabrica, o mov) sin recodificar, en otro hilo "
                        "(record_disco dice como va). `borrar_mkv`: borrar el original si salio bien (de fabrica false).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties",
                          {{"archivo", {{"type", "string"}}},
                           {"formato", {{"type", "string"}, {"enum", {"mp4", "mov"}}}},
                           {"borrar_mkv", {{"type", "boolean"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string f = a.value("archivo", std::string());
            std::error_code ec;
            if (!std::filesystem::exists(f, ec)) return done(ToolResult::error("no existe " + f));
            if (!rt.engine().remux(f, a.value("formato", std::string("mp4")), a.value("borrar_mkv", false)))
                return done(ToolResult::error(rt.engine().lastError()));
            done(ToolResult().text("Pasando; record_disco dice como va.").data(rt.engine().remuxEstado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_ajustes";
        t.title = "Ajustes de la grabacion";
        t.category = "salidas";
        t.description = "`contenedor`: mkv (de fabrica, sobrevive a un corte), mp4, mov o flv (mp4/mov se graban en MKV "
                        "y se pasan al terminar); `remux_al_terminar`. `calidad`: como_stream (comparte el encode de "
                        "la transmisión) o archivo (codificador propio a calidad constante `crf`, 14-28; un encode "
                        "más). `pistas`: las pistas EXTRA de la Matriz (2-6) que se graban aparte, cada una con lo "
                        "que se le cruce ahí (la 1, el programa, va siempre). `copia_dir`: segunda grabación igual en "
                        "otra carpeta/disco (\"\" = sin copia). Rige en la próxima grabación. Sin argumentos, los "
                        "muestra junto con la grabación en curso.";
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
        t.title = "Destinos de la transmision";
        t.category = "salidas";
        t.readOnly = true;
        t.description = "Los destinos adicionales (respaldo, otras plataformas), que comparten los codificadores del "
                        "principal: cada uno con su estado (activo, reconectando, cuadros perdidos) y si tiene clave "
                        "guardada. El principal es el de stream_start sin `destino`.";
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"principal", rt.engine().status().streaming ? rt.engine().destinoActivo() : ""},
                                        {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_agregar";
        t.title = "Agregar o cambiar un destino de transmision";
        t.category = "salidas";
        t.description = "Un destino adicional: `nombre`, `url` (rtmp://, rtmps://, srt:// o rist://), `clave` (se "
                        "guarda cifrada; mejor que la cargue el operador en Salidas > + Destino…, donde no la ves; en "
                        "SRT es la passphrase, en RTMP la stream key), `respaldo`. SRT: `modo` (caller, listener, "
                        "rendezvous) y `latencia_ms` (120 en LAN, 4× el ping por internet). Con `id` de uno existente, "
                        "lo cambia (stream_destino_editar es lo mismo).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"caller", "listener", "rendezvous"}}}},
                           {"latencia_ms", {{"type", "integer"}, {"minimum", 20}, {"maximum", 8000}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            std::string id;
            const std::string e = rt.guardarDestino(a, &id);
            if (!e.empty()) return done(ToolResult::error(e));
            emit vm.destinosChanged();
            done(ToolResult().data(json{{"id", id}, {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_editar";
        t.title = "Cambiar un destino de transmision";
        t.category = "salidas";
        t.description = "Cambia `nombre`, `url`, `clave` o `respaldo` del destino `id`.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            const std::string e = rt.guardarDestino(a);
            if (!e.empty()) return done(ToolResult::error(e));
            emit vm.destinosChanged();
            done(ToolResult().data(json{{"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_borrar";
        t.title = "Borrar un destino de transmision";
        t.category = "salidas";
        t.destructive = true;
        t.description = "Borra el destino `id` (si estaba transmitiendo, lo corta) y su clave guardada.";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            const std::string e = rt.borrarDestino(a.value("id", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            emit vm.destinosChanged();
            done(ToolResult::ok("Borrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_ajustes";
        t.title = "Repetición: cuántos segundos guardar";
        t.category = "salidas";
        t.description = "Guarda en memoria los últimos `segundos` del programa (5 a 600; 0 la apaga) para "
                        "replay_guardar. Usa el mismo encode que la transmisión. Sin argumentos informa.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}}}}}};
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("segundos")) {
                rt.config().replaySegundos = std::clamp(a.value("segundos", 0), 0, 600);
                rt.save();
                const std::string nota = rt.aplicarReplay();
                if (rt.config().replaySegundos > 0 && !rt.engine().replayEstado().value("activa", false))
                    return done(ToolResult::error(nota));
            }
            done(ToolResult().data(rt.engine().replayEstado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_guardar";
        t.title = "Repetir: guardar los últimos segundos";
        t.category = "salidas";
        t.description = "Escribe a disco los últimos segundos de la repetición (Recordings/repeticiones/) y "
                        "devuelve el archivo. Con `y_armar: true` lo pone en la escena «Repetición» y la deja "
                        "armada para el take (no la saca al aire).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"y_armar", {{"type", "boolean"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            const std::string antes = rt.engine().ultimoReplay();
            if (!rt.engine().guardarReplay()) return done(ToolResult::error(rt.engine().lastError()));
            const bool armar = a.value("y_armar", false);
            vm.esperarReplay(antes, 8000, [&vm, armar, done](const QString& f) {
                if (f.isEmpty()) return done(ToolResult::error("el clip no llegó a escribirse en 8 s"));
                json r{{"archivo", f.toStdString()}};
                if (armar) {
                    const QString e = vm.armarRepeticion(f);
                    r["armada"] = e.isEmpty();
                    if (!e.isEmpty()) r["error_al_armar"] = e.toStdString();
                }
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_limpia";
        t.title = "Señal limpia (clean feed)";
        t.category = "salidas";
        t.description = "El programa SIN las capas globales (zócalos DSK, logo) ni la placa de seguridad: lo que "
                        "piden un canal o una cadena que ponen su propia gráfica. Sin argumentos informa. "
                        "`activar: true` la arranca: con `destino` (id de stream_destinos: rtmp/srt/rist, con su "
                        "clave guardada) la manda ahí; sin destino la graba en Recordings/limpia/. `activar: false` "
                        "la corta. Es un encode más (x264, en la CPU): mirá `status.cpu`.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"activar", {{"type", "boolean"}}}, {"destino", {{"type", "string"}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("activar")) {
                if (a.value("activar", false)) {
                    const std::string e = rt.iniciarSenalLimpia(a.value("destino", std::string()));
                    if (!e.empty()) return done(ToolResult::error(e));
                } else {
                    rt.engine().stopCleanFeed();
                }
                emit vm.statusChanged();
            }
            done(ToolResult().data(rt.engine().cleanFeedEstado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salidas_profesionales";
        t.title = "Qué salidas profesionales hay";
        t.category = "salidas";
        t.readOnly = true;
        t.description = "Qué salidas de contribución trae este equipo y cuáles no, para no prometer lo que no hay.";
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
                {"nota", "SRT y RIST salen por el muxer MPEG-TS (stream_destino_agregar con srt:// o rist://). "
                         "NDI y DeckLink de salida no vienen en el paquete (sus SDK no se pueden redistribuir)."}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grabacion_individual";
        t.title = "Grabar cada entrada por separado";
        t.category = "salidas";
        t.description = "Graba una entrada a su propio archivo, ademas del programa.";
        t.details =
            "Para un evento que despues hay que EDITAR: el programa sirve para subirlo esa misma "
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
            "Los archivos van a Recordings/entradas/, con el nombre de la fuente adentro.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"arrancar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"parar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"todas",
               {{"type", "boolean"}, {"description", "Con `arrancar`: todas las de la escena al aire."}}},
              {"invitado",
               {{"type", "string"}, {"description", "Id o nombre de un invitado de la sala: graba su fuente."}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            std::vector<std::string> arrancar;
            if (a.contains("invitado")) {
                const QString f = vm.fuenteDeInvitado(QString::fromStdString(a.value("invitado", std::string())));
                if (f.isEmpty()) return done(ToolResult::error("ese invitado no tiene una fuente en el programa (¿esta admitido?)"));
                arrancar.push_back(f.toStdString());
            }
            if (a.contains("arrancar") && a["arrancar"].is_array())
                for (const json& s : a["arrancar"])
                    if (s.is_string()) arrancar.push_back(s.get<std::string>());
            if (a.value("todas", false))
                for (const std::string& s : rt.engine().sceneSources(rt.engine().currentScene()))
                    arrancar.push_back(s);

            json fallaron = json::array();
            for (const std::string& s : arrancar)
                if (!rt.startSourceRecording(s))
                    fallaron.push_back(json{{"fuente", s}, {"motivo", rt.lastError()}});
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
            emit vm.statusChanged();
            done(ToolResult().data(json{{"grabando", grabando},
                                        {"noSePudo", fallaron},
                                        {"tope", BroadcastEngine::kMaxIndividuales}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_start";
        t.title = "Grabar";
        t.category = "salidas";
        t.description = "EMPEZAR a grabar el programa en disco, con la carpeta y el formato de la "
                        "configuracion. Es para cuando todavia no se esta grabando.";
        t.run = [&rt, &vm](const json&, Done done) {
            if (rt.engine().status().recording) return done(ToolResult::ok("Ya esta grabando."));
            const json d = rt.discoGrabacion();
            if (d.value("horas", -1.0) >= 0 && d.value("horas", -1.0) < 0.25)
                return done(ToolResult::error("Queda disco para menos de 15 minutos a este bitrate: no se arranca sola. "
                                              "Liberá espacio o que el operador lo arranque desde Salidas."));
            if (!rt.startRecording()) return done(ToolResult::error("No arranco: " + rt.lastError()));
            emit vm.statusChanged();
            return done(ToolResult()
                            .text("Grabando en " + rt.engine().archivoGrabacion() + ".")
                            .data(json{{"archivo", rt.engine().archivoGrabacion()},
                                       {"contenedor_real", std::filesystem::path(rt.engine().archivoGrabacion()).extension().string()},
                                       {"contenedor_pedido", rt.config().rec.container},
                                       {"horas_restantes", d.value("horas", -1.0)}}));
            emit vm.statusChanged();
            done(ToolResult::ok("Grabando en " + rt.carpetaGrabacion() + "."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_stop";
        t.title = "Detener grabacion";
        t.category = "salidas";
        t.description = "PARA de grabar: cierra el archivo y deja de escribir en disco.";
        t.run = [&rt, &vm](const json&, Done done) {
            rt.stopRecording();
            emit vm.statusChanged();
            done(ToolResult::ok("Grabacion detenida."));
        };
        core_.addTool(std::move(t));
    }

    // --- Configuracion -------------------------------------------------------------
    {
        Tool t;
        t.name = "settings_list";
        t.title = "Ajustes";
        t.category = "config";
        t.readOnly = true;
        t.description =
            "Todos los ajustes (o los de una seccion) con clave, valor, tipo, opciones, rango, ayuda y "
            "alcance (live = en el momento, gateway = reinicia la sala, restart = al proximo arranque).";
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
        t.title = "Cambiar ajustes";
        t.category = "config";
        t.description =
            "Cambia uno o varios ajustes por clave y los aplica y guarda. Los de alcance 'gateway' "
            "reinician la sala (los invitados se reconectan solos); los 'restart', al proximo arranque.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"values", {{"type", "object"}, {"description", "{clave: valor} (ver settings_list)."}}},
              {"resetSection",
               {{"type", "string"}, {"description", "Volver a los valores por defecto de una seccion."}}}}}};
        t.examples = {R"({"values": {"encoder.bitrate": 4500, "video.fps": "30"}})",
                      R"({"values": {"room.lobby": false}})"};
        SettingsModel* settings = settings_.get();
        t.run = [settings, &vm](const json& a, Done done) {
            // Modo evento: con el guion corriendo de verdad, un ajuste que
            // reinicia la sala saca a todos los invitados en el peor momento
            // posible. El operador puede hacerlo igual desde la app (sabe lo
            // que hace y ve el aviso); el asistente, no.
            if (auto* ev = static_cast<EventoViewModel*>(vm.evento()); ev != nullptr && ev->enVivo())
                return done(
                    ToolResult::error("El evento esta corriendo: los ajustes no se tocan hasta que termine. "
                                      "Si de verdad hace falta, lo hace el operador desde la app."));
            // La salvaguarda y el propio servidor MCP solo se tocan desde la
            // interfaz: un asistente (quiza manipulado) no puede aflojarlos.
            if (a.value("resetSection", std::string()) == "Asistente (MCP)")
                return done(ToolResult::error(
                    "La seccion Asistente (MCP) solo se cambia desde la app (Configuracion)."));
            if (a.contains("values") && a["values"].is_object())
                for (auto it = a["values"].begin(); it != a["values"].end(); ++it)
                    if (it.key().rfind("mcp.", 0) == 0)
                        return done(
                            ToolResult::error("'" + it.key() +
                                              "' solo se cambia desde la app (Configuracion -> Asistente): "
                                              "es la salvaguarda del propio asistente."));
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
                    ToolResult::error("Claves desconocidas:" + u + ". settings_list muestra las validas."));
            }
            const QString summary = settings->apply();
            auto* ui = static_cast<SettingsModel*>(vm.settings());
            if (!ui->dirty()) ui->revert(); // la vista muestra lo nuevo
            vm.refreshModels();
            done(ToolResult::ok("Aplicado. " + summary.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "app_save";
        t.title = "Guardar configuracion";
        t.category = "config";
        t.description = "Guarda escenas y ajustes en disco (las herramientas ya guardan solas; esto fuerza).";
        t.run = [&rt](const json&, Done done) {
            done(rt.save() ? ToolResult::ok("Guardado.") : ToolResult::error(rt.lastError()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
