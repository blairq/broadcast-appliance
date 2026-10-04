// ============================================================================
// src/app/mcp/McpService.cpp — arranque del servidor MCP y herramientas del
// motor: estado, escenas, fuentes, audio, salidas y configuracion.
// (Imagenes: McpImageTools.cpp · Sala y oradores: McpRoomTools.cpp ·
//  Interfaz y sistema: McpUiTools.cpp · Ayuda: McpHelp.cpp)
// ============================================================================

#include "mcp/McpService.hpp"
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
#include <memory>
#include <set>

#include <obs.h>

#include "AppRuntime.hpp"
#include "AppSettings.hpp"
#include "InputSpecs.hpp"
#include "mcp/McpEventoTools.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpPreguntaTools.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
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
            obs_video_info ovi{};
            if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) {
                err = "layout {x,y,w,h} va en fracciones del lienzo (0..1), no en píxeles";
                return false;
            }
            const float bw = static_cast<float>(ovi.base_width), bh = static_cast<float>(ovi.base_height);
            if (nota != nullptr)
                *nota = "layout en píxeles: convertido a fracciones del lienzo de " + std::to_string(ovi.base_width) +
                        "x" + std::to_string(ovi.base_height) + " (la próxima, mandá 0..1)";
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
                CallContext{sesion, ""});
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
                                if (obs_source_t* s = obs_get_source_by_name(n.c_str())) {
                                    const char* id = obs_source_get_unversioned_id(s);
                                    if (id != nullptr && std::string(id) == inputs::kAudioInType) mic = n;
                                    obs_source_release(s);
                                }
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
    core_.setApprover(
        [this](const Tool& t, const json& args, const std::string& reason, std::function<void(bool)> decide) {
            auto* msgs = static_cast<OperatorMessages*>(vm_.messages());
            std::string a = args.empty() ? std::string("(sin argumentos)") : args.dump(2);
            if (a.size() > 1200) a = a.substr(0, 1200) + "\n…";
            const int id = msgs->post(json{{"kind", "question"},
                                           {"approval", true},
                                           {"priority", t.destructive ? "critical" : "warning"},
                                           {"title", "¿Aprobás esta acción del asistente?"},
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
    const QString token =
        loadOrCreateToken(QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring()));
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
    core_.addCategory("estado", "Estado", "Vistazo general: que esta al aire, salidas, salud.");
    core_.addCategory("escenas", "Escenas",
                      "Escenas: listar, cambiar la del aire, crear, definir completas, exportar.");
    core_.addCategory("fuentes", "Fuentes",
                      "Lo que compone cada escena: camaras, imagenes, textos, invitados...");
    core_.addCategory("audio", "Audio", "Mezclador: volumen, silencio, niveles.");
    core_.addCategory("salidas", "Salidas", "Transmision (RTMP) y grabacion.");
    core_.addCategory("config", "Configuracion",
                      "Todos los ajustes del appliance (video, codificacion, sala, red...).");
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
                     {"cpuPercent", vm.cpuPercent()},
                     {"programPeakDb", {vm.programPeakL(), vm.programPeakR()}},
                     {"canvas", vm.canvasLabel().toStdString()},
                     {"gatewayAlive", vm.gatewayAlive()},
                     {"lastError", st.lastError},
                     {"room", room}}));
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
        t.name = "scene_take";
        t.title = "TAKE: sacar al aire la escena armada";
        t.category = "escenas";
        t.description = "Pone al aire la escena que estaba armada en el bus de preview, con la transicion "
                        "configurada. Deja armada la que salia, para poder volver de un golpe.";
        t.details = "Sin nada armado no hace nada y lo dice: armar primero con `scene_preview`.\n\n"
                    "Que el plano que SALE del aire quede armado es como funciona cualquier mesa: "
                    "despues de un take, volver a lo anterior es un solo take mas.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [&rt, &vm](const json&, Done done) {
            const QString armada = vm.escenaArmada();
            if (armada.isEmpty())
                return done(ToolResult::error(
                    "No hay nada armado. Arma el proximo plano con scene_preview {\"name\": ...}."));
            vm.take();
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
            "Cada fuente: {type, name, settings?, layout?, visible?, volume?, muted?}. `type` es el id de "
            "libobs (ver source_types): p. ej. image_source {file}, color_source_v3 {color, width, "
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
            "Tipos de fuente que esta instalacion puede crear (id de libobs, nombre, si tiene video "
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
                           {"type", {{"type", "string"}, {"description", "Id de libobs (source_types)."}}},
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
              {"rename", {{"type", "string"}, {"description", "Nuevo nombre (en todas las escenas)."}}}}},
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
            "OJO: una fuente SILENCIADA (`audio_set muted`) no suena ni por el monitor —en libobs el "
            "silencio es antes de todo—. Para probar un microfono cerrado hay que quitarle el silencio "
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
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"server", {{"type", "string"}, {"description", "rtmp://... (opcional)."}}},
              {"key", {{"type", "string"}, {"description", "Clave de transmision (solo en memoria)."}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (rt.engine().status().streaming) return done(ToolResult::ok("Ya esta transmitiendo."));
            if (!rt.startStreaming(a.value("server", std::string()), a.value("key", std::string())))
                return done(ToolResult::error("No arranco: " + rt.lastError() +
                                              ". Si falta la clave: pasala en `key` o en la variable " +
                                              rt.config().streamKeyEnv + "."));
            emit vm.statusChanged();
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
            "BAJA del aire: corta la transmision que esta saliendo. Detiene la transmision en vivo.";
        t.run = [&rt, &vm](const json&, Done done) {
            rt.stopStreaming();
            emit vm.statusChanged();
            done(ToolResult::ok("Transmision detenida."));
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
            "EL AUDIO tiene un limite propio: libobs mezcla en 6 pistas y la 0 es la del programa, asi "
            "que solo las primeras CINCO entradas con audio graban sonido. De la sexta en adelante se "
            "graba el video sin audio, y la respuesta lo dice en `audioPropio`.\n\n"
            "Los archivos van a Recordings/entradas/, con el nombre de la fuente adentro.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"arrancar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"parar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"todas",
               {{"type", "boolean"}, {"description", "Con `arrancar`: todas las de la escena al aire."}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            std::vector<std::string> arrancar;
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
            if (!rt.startRecording()) return done(ToolResult::error("No arranco: " + rt.lastError()));
            emit vm.statusChanged();
            done(ToolResult::ok("Grabando en " + rt.config().rec.directory + "."));
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
                for (const auto& r : rows)
                    if (r.value("section", "") == section) f.push_back(r);
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
