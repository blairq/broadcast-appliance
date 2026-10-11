// ============================================================================
// src/app/mcp/McpImageTools.cpp — Herramientas de imagen (vision)
//
// Todo lo que se puede VER, como imagen para un modelo con vision:
//   capture_program    el programa (lo que sale al aire), a resolucion completa
//   capture_source     cualquier fuente o escena (tambien una que NO esta al
//                      aire: sirve para revisar antes de cambiar)
//   capture_multiview  todas las escenas en una grilla con su nombre
//   capture_feed       un video crudo de A (camara de un invitado, lobby, fila,
//                      pantalla compartida, feed del operador)
//   capture_person     la imagen de una persona de la sala, por nombre o id
//   capture_people     todas las personas de la sala, una imagen cada una
//   capture_sequence   N cuadros separados en el tiempo (analisis de video:
//                      movimiento, gestos, si alguien habla, cortes)
//   capture_ui         captura de la propia app (principal, salidas y pestañas sueltas)
// ============================================================================

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

#include "agent/Despachador.hpp"
#include "platform/Paths.hpp"
#include "agent/ModeloPropio.hpp"
#include "agent/ModelosLocales.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"
#include "video/Scopes.hpp"
#include "video/VideoGrafos.hpp"
#include <QBuffer>
#include <thread>

namespace ba::app::mcp {

namespace {

/// Una fuente o escena ("" = el programa) como QImage, sin que espere la
/// interfaz (el MCP corre en su hilo): prepara acá, el cuadro se espera en otro hilo y `listo` se llama de
/// vuelta en el hilo de la interfaz, con la imagen (o nula y el error).
void renderAparte(AppRuntime& rt, const std::string& name, int maxWidth,
                  std::function<void(QImage img, std::string err)> listo) {
    rt.engine().capturarAparte(
        name, maxWidth,
        [listo = std::move(listo)](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) mutable {
            QImage img;
            if (e.empty() && w > 0 && h > 0) img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
            QMetaObject::invokeMethod(
                qApp, [listo = std::move(listo), img, e]() mutable { listo(img, e); }, Qt::QueuedConnection);
        });
}

bool feedImage(StudioViewModel& vm, const std::string& key, QImage& out) {
    auto* frames = static_cast<FrameService*>(vm.frames());
    std::uint64_t serial = 0;
    return frames->latest(QString::fromStdString(key), out, serial) && !out.isNull();
}

const json kImageProps = {
    {"maxWidth",
     {{"type", "integer"}, {"minimum", 64}, {"maximum", 3840}, {"description", "Ancho maximo en px."}}},
    {"format", {{"type", "string"}, {"enum", {"jpeg", "png"}}, {"default", "jpeg"}}},
    {"quality", {{"type", "integer"}, {"minimum", 10}, {"maximum", 100}, {"default", 85}}}};

json withImageProps(json props) {
    for (auto it = kImageProps.begin(); it != kImageProps.end(); ++it)
        props[it.key()] = it.value();
    return json{{"type", "object"}, {"properties", props}};
}

/// Grilla de imagenes con rotulos (para vistas multiples).
QImage contactSheet(const std::vector<std::pair<QString, QImage>>& items, int cellW, int cols,
                    const QString& highlight) {
    const int cellH = cellW * 9 / 16;
    const int labelH = 28;
    const int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
    QImage sheet(cols * cellW, rows * (cellH + labelH), QImage::Format_RGB32);
    sheet.fill(QColor(12, 12, 14));
    QPainter p(&sheet);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont f = p.font();
    f.setPixelSize(16);
    f.setBold(true);
    p.setFont(f);
    for (size_t i = 0; i < items.size(); ++i) {
        const int cx = static_cast<int>(i) % cols * cellW;
        const int cy = static_cast<int>(i) / cols * (cellH + labelH);
        const QImage& img = items[i].second;
        if (!img.isNull()) {
            const QImage s = img.scaled(cellW - 8, cellH - 8, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(cx + (cellW - s.width()) / 2, cy + (cellH - s.height()) / 2, s);
        } else {
            p.setPen(QColor(120, 120, 130));
            p.drawText(QRect(cx, cy, cellW, cellH), Qt::AlignCenter, "(sin imagen)");
        }
        const bool live = items[i].first == highlight;
        if (live) {
            p.setPen(QPen(QColor(255, 59, 48), 4));
            p.drawRect(cx + 2, cy + 2, cellW - 4, cellH - 4);
        }
        p.setPen(live ? QColor(255, 90, 80) : QColor(235, 235, 240));
        p.drawText(QRect(cx + 6, cy + cellH, cellW - 12, labelH), Qt::AlignLeft | Qt::AlignVCenter,
                   (live ? "● AL AIRE — " : "") + items[i].first);
    }
    return sheet;
}

} // namespace

/// Le pregunta a TU servidor por una imagen, directo, sin agente en el medio.
///
/// POR QUE DIRECTO Y NO POR UN AGENTE. La idea era encargarle la mirada a un
/// agente de un tiro para que la imagen no entrara al contexto del que opera.
/// Funciona, pero arrancar `opencode run` cuesta minutos —medido: 188 s sin
/// usar una sola herramienta— y eso en vivo no sirve. Un pedido suelto al
/// mismo servidor contesta en 0,6 s (medido) y cumple el requisito MEJOR: la
/// imagen no entra a ninguna conversacion, ni siquiera a la del agente que
/// mira. Es un pedido sin estado: una imagen, una pregunta, un texto.
///
/// Devuelve el texto, o deja el motivo en `error`.
std::string mirarConElModeloPropio(AppRuntime& rt, const QImage& img, const std::string& que,
                                   std::string& error) {
    const agent::ModeloPropio mp = agent::ModeloPropio::leer(rt.dataRoot() / "Config" / "modelo-propio.json");
    if (!mp.completo()) {
        error = "no hay un modelo propio configurado";
        return {};
    }
    QByteArray clave;
    const QString cred = QString::fromStdWString((ba::platform::credentialsDir()).wstring());
    if (!ba::app::cred::read(cred, "modelo-propio-api-key", clave)) {
        error = "falta la clave del modelo propio";
        return {};
    }

    // 768 px: alcanza de sobra para decir si hay alguien, si el cuadro esta en
    // negro o que dice un rotulo, y mantiene el pedido chico. Una captura de
    // 1600 px no se ve mejor, se paga mas cara.
    QImage chica = img.width() > 768 ? img.scaledToWidth(768, Qt::SmoothTransformation) : img;
    QByteArray jpg;
    QBuffer buf(&jpg);
    buf.open(QIODevice::WriteOnly);
    if (!chica.save(&buf, "JPG", 85)) {
        error = "no se pudo codificar la imagen";
        return {};
    }

    json contenido = json::array();
    contenido.push_back(json{{"type", "text"}, {"text", que}});
    contenido.push_back(
        json{{"type", "image_url"},
             {"image_url", {{"url", "data:image/jpeg;base64," + jpg.toBase64().toStdString()}}}});
    json body{
        {"model", mp.modelo},
        {"messages",
         json::array({json{{"role", "system"},
                           {"content", "Mirás una imagen y contestás la pregunta, en dos o tres líneas. "
                                       "Decís lo que VES: si no se ve, decís que no se ve. No inventás "
                                       "lo que debería haber. No describís el resto."}},
                      json{{"role", "user"}, {"content", contenido}}})},
        {"max_tokens", 300},
        {"temperature", 0}};
    // Sin pensar: para mirar una imagen no hace falta, y en vivo el tiempo es
    // lo que importa.
    body["chat_template_kwargs"] = json{{"enable_thinking", false}, {"reasoning_effort", "low"}};

    std::string url = mp.url;
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    net::HttpRequest req;
    req.method = "POST";
    req.url = url + "/chat/completions";
    req.headers = {{"Authorization", "Bearer " + clave.trimmed().toStdString()},
                   {"Content-Type", "application/json"}};
    req.body = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    req.timeoutMs = 45000;
    const net::HttpResponse r = net::HttpsClient::request(req);
    if (r.status == 0) {
        error = "no contesta: " + r.error;
        return {};
    }
    if (r.status >= 400) {
        error = "el servidor contesto " + std::to_string(r.status);
        return {};
    }
    try {
        const json d = json::parse(r.body);
        const std::string texto = d["choices"][0]["message"].value("content", std::string());
        if (texto.empty()) error = "el modelo contesto vacio";
        return texto;
    } catch (const json::exception& e) {
        error = std::string("no se entendio la respuesta: ") + e.what();
        return {};
    }
}

void McpService::registerImageTools() {
    core_.addCategory(
        "imagenes", "Imágenes y video (visión)",
        "Ver el programa, cualquier fuente o escena, cada persona, la app, y secuencias en el tiempo.");
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    auto* self = this;
    // Por referencia local: los lambdas de las herramientas no capturan `this`.
    McpCore& core = core_;

    {
        Tool t;
        t.name = "capture_program";
        t.title = "Ver el programa";
        t.category = "imagenes";
        t.readOnly = true;
        t.description = "Una foto de lo que esta saliendo al aire AHORA: para ver si se ve negro, congelado "
                        "o mal encuadrado. Imagen de lo que esta saliendo al aire ahora (la escena del "
                        "programa, render completo del motor). Por defecto 1280 px de ancho, JPEG.";
        t.inputSchema = withImageProps(json::object());
        t.run = [&rt, &vm, &core](const json& a, Done done) {
            renderAparte(rt, "", a.value("maxWidth", 1280), [&rt, &vm, &core, a, done](QImage img, std::string err) {
                if (img.isNull() && !feedImage(vm, "program", img))
                    return done(ToolResult::error("No hay imagen del programa: " + err));
                ToolResult r;
                r.text("Programa — escena '" + rt.engine().currentScene() + "' (" + std::to_string(img.width()) +
                       "x" + std::to_string(img.height()) + ")");
                addImage(r, img, a, 1280, core, rt.dataRoot());
                done(r);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_scope";
        t.title = "Instrumentos de video (forma de onda, vectorscopio…)";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Forma de onda (`onda`), RGB parade (`parade`), vectorscopio (`vector`) o histograma "
            "(`histograma`) del programa o de una `fuente`, más cuánto de la imagen está fuera de rango: "
            "`legalidad` = % de negros aplastados, blancos quemados y puntos fuera de la gama compuesta PAL "
            "(lo que balegal corrige). La imagen se guarda en disco (no viaja en la conversación).";
        t.inputSchema = withImageProps(json{
            {"fuente", {{"type", "string"}, {"description", "Fuente o escena; sin esto, el programa."}}},
            {"tipo", {{"type", "string"}, {"enum", {"onda", "parade", "vector", "histograma"}}, {"default", "onda"}}}});
        t.run = [&rt, &vm, &core](const json& a, Done done) {
            video::TipoScope tipo = video::TipoScope::Onda;
            if (!video::tipoScopeDeTexto(QString::fromStdString(a.value("tipo", std::string("onda"))), tipo))
                return done(ToolResult::error("`tipo`: onda, parade, vector o histograma"));
            const std::string fuente = a.value("fuente", std::string());
            renderAparte(rt, fuente, 960, [&rt, &vm, &core, a, done, fuente, tipo](QImage img, std::string err) {
            if (img.isNull() && (!fuente.empty() || !feedImage(vm, "program", img)))
                return done(ToolResult::error("No hay imagen: " + err));
            const video::Legalidad l = video::medirLegalidad(img);
            const QImage s = video::dibujarScope(img, tipo, tipo == video::TipoScope::Vector ? 420 : 640,
                                                 tipo == video::TipoScope::Vector ? 420 : 360);
            ToolResult r;
            r.text((fuente.empty() ? std::string("Programa") : "'" + fuente + "'") + " — " +
                   a.value("tipo", std::string("onda")));
            r.data(json{{"legalidad",
                         {{"negros_aplastados_pct", std::round(l.lumaBajaPct * 10) / 10},
                          {"blancos_quemados_pct", std::round(l.lumaAltaPct * 10) / 10},
                          {"fuera_de_gama_pct", std::round(l.cromaPct * 10) / 10}}},
                        {"legalizador", vm.videoGrafos() != nullptr ? vm.videoGrafos()->legalizar() : std::string("no")}});
            addImage(r, s, a, s.width(), core, rt.dataRoot());
            done(r);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_legalizar";
        t.title = "Legalizar para TV";
        t.category = "imagenes";
        t.description =
            "Pasa cada entrada por el legalizador broadcast-safe (balegal, en la cadena de GStreamer de la "
            "entrada): `modo` no | pal (luma/croma en rango y gama compuesta +133 %) | ntsc (más estricto, "
            "+120 %) | rango (solo luma y croma). Para entregar a un canal. Sin argumentos informa.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"modo", {{"type", "string"}, {"enum", {"no", "pal", "ntsc", "rango"}}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("modo")) {
                rt.config().legalizar = a.value("modo", std::string("no"));
                rt.save();
                if (vm.videoGrafos() != nullptr) vm.videoGrafos()->setLegalizar(rt.config().legalizar);
            }
            json r{{"modo", rt.config().legalizar}};
            if (vm.videoGrafos() != nullptr && rt.config().legalizar != "no")
                r["entradas"] = vm.videoGrafos()->estadoLegalizador();
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_source";
        t.title = "Ver una fuente o escena";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Imagen actual de una fuente (camara, imagen, texto, invitado...) o de una escena completa, "
            "este o no al aire. Ideal para revisar una escena antes de cambiarla, o verificar lo que "
            "armaste con scene_define.";
        t.inputSchema =
            withImageProps(json{{"name", {{"type", "string"}, {"description", "Fuente o escena."}}}});
        t.inputSchema["required"] = {"name"};
        t.run = [&rt, &core](const json& a, Done done) {
            // Una fuente recién creada (una cámara de red que todavía conecta)
            // da negro el primer instante: se reintenta un poco antes de
            // contestar con una imagen que engaña.
            auto intento = std::make_shared<std::function<void(int)>>();
            *intento = [&rt, &core, a, done, intento](int n) {
                renderAparte(rt, a["name"], a.value("maxWidth", 1280), [&rt, &core, a, done, intento, n](QImage img, std::string err) {
                if (img.isNull()) {
                    QTimer::singleShot(0, [intento]() { *intento = nullptr; });
                    return done(ToolResult::error(err));
                }
                bool negra = !img.isNull();
                const QImage chica = img.scaled(32, 18);
                for (int y = 0; y < chica.height() && negra; ++y)
                    for (int x = 0; x < chica.width() && negra; ++x)
                        if (qGray(chica.pixel(x, y)) > 6) negra = false;
                if (negra && n < 8) {
                    QTimer::singleShot(250, [intento, n]() {
                        if (*intento) (*intento)(n + 1);
                    });
                    return;
                }
                QTimer::singleShot(0, [intento]() { *intento = nullptr; });
                ToolResult r;
                r.text("'" + a["name"].get<std::string>() + "' (" + std::to_string(img.width()) + "x" +
                       std::to_string(img.height()) + ")" +
                       (negra ? " — sale NEGRA (sin señal todavía, o la imagen es negra de verdad)" : ""));
                addImage(r, img, a, 1280, core, rt.dataRoot());
                done(r);
                });
            };
            (*intento)(0);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_multiview";
        t.title = "Vista multiple de escenas";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Una sola imagen con TODAS las escenas en grilla, cada una con su nombre; la del aire, "
            "marcada en rojo. Como el multiview de un switcher.";
        t.inputSchema = withImageProps(
            json{{"cellWidth", {{"type", "integer"}, {"minimum", 160}, {"maximum", 960}, {"default", 400}}}});
        t.run = [&rt, &core](const json& a, Done done) {
            const int cellW = a.value("cellWidth", 400);
            const std::vector<std::string> nombres = rt.engine().sceneNames();
            if (nombres.empty()) return done(ToolResult::error("No hay escenas."));
            // Una escena por vez, sin que espere la interfaz (renderAparte); al
            // final, la grilla.
            auto juntas = std::make_shared<std::vector<std::pair<QString, QImage>>>();
            auto paso = std::make_shared<std::function<void(std::size_t)>>();
            *paso = [&rt, &core, a, done, cellW, nombres, juntas, paso](std::size_t i) {
            if (i < nombres.size()) {
                renderAparte(rt, nombres[i], cellW, [juntas, nombres, i, paso](QImage img, std::string) {
                    juntas->emplace_back(QString::fromStdString(nombres[i]), img);
                    if (*paso) (*paso)(i + 1);
                });
                return;
            }
            QTimer::singleShot(0, [paso]() { *paso = nullptr; }); // corta el ciclo de referencias
            const auto& items = *juntas;
            const int cols =
                std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(items.size())))));
            ToolResult r;
            r.text(std::to_string(items.size()) + " escena(s); al aire: " + rt.engine().currentScene());
            addImage(r, contactSheet(items, cellW, cols, QString::fromStdString(rt.engine().currentScene())),
                     a, 0, core, rt.dataRoot());
            done(r);
            };
            (*paso)(0);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_feed";
        t.title = "Ver un video de entrada";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Ultimo cuadro de un video que llega a la app: program, guest:N (camara del invitado del "
            "lugar N), share:N (su pantalla), lobby:K (preview del lobby), backstage:K (fila de "
            "oradores), operator (con lo que te ve el lobby). feeds_list muestra cuales hay.";
        t.inputSchema = withImageProps(json{{"key", {{"type", "string"}}}});
        t.inputSchema["required"] = {"key"};
        t.run = [&vm, &rt, &core](const json& a, Done done) {
            QImage img;
            if (!feedImage(vm, a["key"], img))
                return done(
                    ToolResult::error("Sin imagen en '" + a["key"].get<std::string>() +
                                      "' (nadie publica ese video ahora). feeds_list muestra los validos."));
            ToolResult r;
            r.text(a["key"].get<std::string>() + " (" + std::to_string(img.width()) + "x" +
                   std::to_string(img.height()) + ")");
            addImage(r, img, a, 960, core, rt.dataRoot());
            done(r);
        };
        core_.addTool(std::move(t));
    }

    // --- Personas ----------------------------------------------------------------
    // Una persona: su video vivo si A lo recibe; si no, la foto que manda su
    // navegador (la pide a B).
    auto personImage =
        std::make_shared<std::function<void(const json&, int, std::function<void(QImage, std::string)>)>>();
    *personImage = [&vm](const json& p, int maxW, std::function<void(QImage, std::string)> cb) {
        (void)maxW;
        const std::string key = personFeedKey(p);
        QImage img;
        if (!key.empty() && feedImage(vm, key, img)) return cb(img, "video en vivo (" + key + ")");
        if (!p.value("hasAvatar", false)) return cb(QImage(), "sin imagen (camara apagada o sin medios)");
        auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
        conf->client().getRaw("/api/v1/participants/" + QString::fromStdString(p.value("id", "")) + "/avatar",
                              [cb](bool ok, const QByteArray& jpeg) {
                                  QImage a;
                                  if (ok) a.loadFromData(jpeg, "JPEG");
                                  cb(a, ok ? "foto de hace unos segundos (sin video en vivo en la app)"
                                           : "sin imagen");
                              });
    };
    {
        Tool t;
        t.name = "capture_person";
        t.title = "Ver a una persona";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Imagen de una persona de la sala (por nombre o id): su camara en vivo si esta al aire, en "
            "el lobby o entre los primeros de la fila; si no, la foto reciente de su navegador. Con "
            "includeScreen tambien su pantalla compartida.";
        t.inputSchema =
            withImageProps(json{{"participant", {{"type", "string"}, {"description", "Nombre o id."}}},
                                {"includeScreen", {{"type", "boolean"}, {"default", false}}}});
        t.inputSchema["required"] = {"participant"};
        t.run = [&vm, &rt, &core, personImage](const json& a, Done done) {
            json p;
            std::string err;
            if (!resolveParticipant(roomSnapshot(vm), a["participant"], p, err))
                return done(ToolResult::error(err));
            (*personImage)(p, a.value("maxWidth", 960),
                           [&vm, &rt, &core, a, p, done](QImage img, std::string how) {
                               ToolResult r;
                               r.text(p.value("name", "") + " — " + p.value("state", "") +
                                      (p.value("slot", 0) > 0 ? " (al aire)" : "") + " — " + how);
                               if (!img.isNull()) addImage(r, img, a, 960, core, rt.dataRoot());
                               if (a.value("includeScreen", false) && p.value("screen", false) &&
                                   p.value("slot", 0) > 0) {
                                   QImage s;
                                   if (feedImage(vm, "share:" + std::to_string(p.value("slot", 0)), s)) {
                                       r.text("Su pantalla compartida:");
                                       addImage(r, s, a, 1280, core, rt.dataRoot());
                                   }
                               }
                               if (img.isNull()) r.isError = true;
                               done(r);
                           });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_people";
        t.title = "Ver a todas las personas";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Una imagen por cada persona de la sala (lobby, adentro y fila de oradores), cada una "
            "precedida por su nombre, estado y de donde sale la imagen. Default 480 px.";
        t.inputSchema = withImageProps(json{
            {"state", {{"type", "string"}, {"enum", {"all", "waiting", "admitted"}}, {"default", "all"}}}});
        t.run = [&vm, &rt, &core, personImage](const json& a, Done done) {
            const json snap = roomSnapshot(vm);
            const std::string only = a.value("state", std::string("all"));
            std::vector<json> people;
            for (const auto& p : snap.value("participants", json::array()))
                if (only == "all" || p.value("state", "") == only) people.push_back(p);
            if (people.empty()) return done(ToolResult::ok("No hay nadie en la sala."));
            struct Acc {
                ToolResult r;
                size_t pending;
                std::vector<std::pair<std::string, QImage>> got;
            };
            auto acc = std::make_shared<Acc>();
            acc->pending = people.size();
            acc->got.resize(people.size());
            for (size_t i = 0; i < people.size(); ++i) {
                const json p = people[i];
                (*personImage)(p, a.value("maxWidth", 480),
                               [acc, i, p, a, done, &core, &rt](QImage img, std::string how) {
                                   acc->got[i] = {p.value("name", "") + " (" + p.value("id", "") + ") — " +
                                                      p.value("state", "") +
                                                      (p.value("slot", 0) > 0
                                                           ? ", al aire en el lugar " +
                                                                 std::to_string(p.value("slot", 0))
                                                           : "") +
                                                      " — " + how,
                                                  img};
                                   if (--acc->pending > 0) return;
                                   for (const auto& [label, im] : acc->got) {
                                       acc->r.text(label);
                                       if (!im.isNull()) addImage(acc->r, im, a, 480, core, rt.dataRoot());
                                   }
                                   done(acc->r);
                               });
            }
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_sequence";
        t.title = "Secuencia de cuadros (video)";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Toma `frames` imagenes separadas `intervalMs` de: el programa, una fuente/escena, un video "
            "(clave de feeds_list) o una persona. Para analizar movimiento, si alguien habla o hace "
            "gestos, si un video avanza, transiciones, congelamientos.";
        t.inputSchema = withImageProps(json{
            {"target",
             {{"type", "string"},
              {"description", "'program', nombre de fuente/escena, o clave de video (guest:1...)."}}},
            {"participant", {{"type", "string"}, {"description", "En vez de target: una persona."}}},
            {"frames", {{"type", "integer"}, {"minimum", 2}, {"maximum", 16}, {"default", 4}}},
            {"intervalMs", {{"type", "integer"}, {"minimum", 50}, {"maximum", 10000}, {"default", 500}}}});
        t.run = [&rt, &vm, self, &core](const json& a, Done done) {
            std::string target = a.value("target", std::string("program"));
            if (a.contains("participant")) {
                json p;
                std::string err;
                if (!resolveParticipant(roomSnapshot(vm), a["participant"], p, err))
                    return done(ToolResult::error(err));
                target = personFeedKey(p);
                if (target.empty())
                    return done(ToolResult::error(p.value("name", "") +
                                                  " no tiene video en vivo en la app ahora "
                                                  "(capture_person da su foto)."));
            }
            const int maxW = a.value("maxWidth", 640);
            // Sin que espere la interfaz: la imagen llega a `listo` (ver renderAparte).
            const auto grab = [&rt, &vm, target, maxW](std::function<void(QImage, std::string)> listo) {
                if (target == "program") return renderAparte(rt, "", maxW, std::move(listo));
                if (target.find(':') != std::string::npos) {
                    QImage img;
                    if (feedImage(vm, target, img)) return listo(img, std::string());
                    return listo(QImage(), "sin imagen en " + target);
                }
                renderAparte(rt, target, maxW, std::move(listo));
            };
            struct Seq {
                ToolResult r;
                int left;
                int n = 0;
                qint64 t0 = 0;
            };
            auto seq = std::make_shared<Seq>();
            seq->left = a.value("frames", 4);
            const int interval = a.value("intervalMs", 500);
            seq->r.text("Secuencia de " + std::to_string(seq->left) + " cuadros de '" + target + "' cada " +
                        std::to_string(interval) + " ms:");
            auto step = std::make_shared<std::function<void()>>();
            QPointer<McpService> guard(self);
            *step = [seq, step, grab, a, done, interval, maxW, guard, &core, &rt]() {
                if (!guard) return;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (seq->n == 0) seq->t0 = now;
                grab([seq, step, a, done, interval, maxW, guard, &core, &rt, now](QImage img, std::string err) {
                if (!guard) return;
                if (!img.isNull()) {
                    seq->r.text("t=" + std::to_string(now - seq->t0) + " ms");
                    json args = a;
                    args["maxWidth"] = maxW;
                    addImage(seq->r, img, args, maxW, core, rt.dataRoot());
                } else {
                    seq->r.text("t=" + std::to_string(now - seq->t0) + " ms: " + err);
                }
                ++seq->n;
                if (--seq->left <= 0) {
                    done(seq->r);
                    *step = nullptr; // corta el ciclo de referencias
                    return;
                }
                QTimer::singleShot(interval, [step]() {
                    if (*step) (*step)();
                });
                });
            };
            (*step)();
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "imagen_analizar";
        t.title = "Que se ve: se lo pregunta a un agente que mira";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Mira lo que se VE (una camara, el programa, una escena o la propia app) y contesta la "
            "pregunta concreta que le hagas. Solo video, nada de audio. Devuelve TEXTO.";
        t.details = "ESTA ES LA UNICA FORMA DE MIRAR, y no es un rodeo: tu conversacion aguanta muy pocas "
                    "imagenes y, pasadas esas, se cuelga —no da error, se cuelga, en el medio de una "
                    "transmision en vivo—. Por eso las herramientas `capture_*` te devuelven un archivo y "
                    "no la imagen. Aca la mira un agente aparte, con esa imagen y nada mas, y te contesta "
                    "en texto. Si el se cuelga, vos seguis.\n\n"
                    "PREGUNTA CONCRETA. `que` es un contrato: decile exactamente que tiene que mirar y que "
                    "tenes que saber. «¿Se ve a alguien o esta el cuadro en negro?», «¿el rotulo dice "
                    "Ana Perez?», «¿cuantas personas hay?». Un «describime esto» devuelve una descripcion "
                    "que no sirve para decidir nada.\n\n"
                    "UNA IMAGEN POR LLAMADA. Si necesitas comparar dos momentos, llamala dos veces y "
                    "compara vos los dos textos.\n\n"
                    "QUIEN MIRA. Si hay un modelo local cargado con vision (Configuracion -> Modelos "
                    "locales), mira el, en esta maquina y en alrededor de un segundo; si no, el modelo "
                    "propio. No la uses para mirar cada dos segundos.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"que"}},
            {"properties",
             {{"que",
               {{"type", "string"},
                {"description", "la pregunta concreta que tiene que contestar mirando la imagen"}}},
              {"fuente",
               {{"type", "string"},
                {"description",
                 "que mirar: 'programa' (lo que sale al aire, por defecto), 'interfaz' (la "
                 "pantalla de la app), el NOMBRE de una escena o fuente (cualquiera, este o no al aire: no "
                 "hace falta armarla), o una clave de feeds_list (guest:1, lobby:2, armada...)"}}}}}};
        t.examples = {R"({"que": "Se ve a alguien o esta el cuadro en negro?"})",
                      R"({"que": "El rotulo de abajo dice el nombre correcto?", "fuente": "programa"})",
                      R"({"que": "Cuantas personas hay en cuadro?", "fuente": "guest:1"})"};
        t.run = [&vm, &rt, self](const json& a, Done done) {
            const std::string que = a.value("que", std::string());
            if (que.empty())
                return done(ToolResult::error("Decile QUE tiene que mirar: sin pregunta concreta "
                                              "devuelve una descripcion que no sirve para decidir."));
            const std::string fuente = a.value("fuente", std::string("programa"));

            // Con la imagen en la mano, lo que sigue (se llama ya, o cuando llega
            // el cuadro: renderAparte, que no hace esperar a la interfaz).
            const auto seguir = [&vm, &rt, a, done, que, fuente](QImage img) {
            if (img.isNull()) return done(ToolResult::error("No se pudo tomar la imagen."));

            const QString archivo = guardarCaptura(img, rt.dataRoot(), a, 1280);
            if (archivo.isEmpty())
                return done(ToolResult::error("No se pudo guardar la captura para que la miren."));

            const std::string donde =
                fuente == "interfaz" ? "la pantalla de la aplicacion" : "el video de '" + fuente + "'";
            const QImage copia = img;
            // Al servidor propio, en un hilo aparte (el cliente HTTPS bloquea y
            // esto corre en el hilo de la interfaz). Ver `mirarConElModeloPropio`.
            const auto conElPropio = [&rt, copia, que, donde, archivo, done](const std::string& antes) {
                std::thread([&rt, copia, que, donde, archivo, done, antes]() {
                    std::string error;
                    const std::string texto = mirarConElModeloPropio(rt, copia, que, error);
                    ToolResult r;
                    if (texto.empty()) {
                        r = ToolResult::error("No pude mirar la imagen (" + antes + error +
                                              "). La captura quedo en " + archivo.toStdString() + ".");
                    } else {
                        r.text(texto);
                        r.structured = json{
                            {"miro", donde}, {"con", "modelo propio"}, {"captura", archivo.toStdString()}};
                    }
                    // La respuesta vuelve al hilo de la interfaz: `done` termina
                    // contestando un pedido MCP, que vive alla.
                    QMetaObject::invokeMethod(qApp, [done, r]() { done(r); }, Qt::QueuedConnection);
                }).detach();
            };
            // Primero el modelo local, si esta cargado y trae la torre de vision:
            // mira en la GPU de esta maquina, sin red, en un segundo. Si falla, el
            // servidor propio como siempre.
            agent::ModelosLocales* ml = vm.modelosLocales();
            if (ml == nullptr || !ml->listo() || !ml->conVision()) return conElPropio("");
            const QString pregunta =
                QString::fromStdString(que) +
                "\nContesta en castellano, concreto, en una o dos oraciones. Si no se ve, decilo.";
            ml->ver(archivo, pregunta, 160, {}, [conElPropio, donde, archivo, done](const json& j) {
                const std::string texto = j.value("texto", std::string());
                if (j.contains("error") || texto.empty())
                    return conElPropio("modelo local: " + j.value("error", std::string("sin respuesta")) +
                                       "; ");
                ToolResult r;
                r.text(texto);
                r.structured = json{{"miro", donde},
                                    {"con", "modelo local"},
                                    {"ms", j.value("ms_imagen", 0.0)},
                                    {"captura", archivo.toStdString()}};
                done(r);
            });
            };
            if (fuente == "interfaz") {
                QQuickWindow* w = self->mainWindow_ ? self->mainWindow_() : nullptr;
                if (w == nullptr) return done(ToolResult::error("La interfaz grafica no esta abierta."));
                return seguir(w->grabWindow());
            }
            if (fuente == "programa" || fuente == "program") {
                return renderAparte(rt, "", 1280, [&vm, seguir, done](QImage img, std::string err) {
                    if (img.isNull() && !feedImage(vm, "program", img))
                        return done(ToolResult::error("No hay imagen del programa: " + err));
                    seguir(img);
                });
            }
            QImage img;
            if (feedImage(vm, fuente, img)) return seguir(img);
            // Una escena o una fuente por su nombre: se dibuja aparte (no toca
            // el programa ni la armada).
            renderAparte(rt, fuente, 1280, [seguir, done, fuente](QImage img, std::string err) {
                if (img.isNull())
                    return done(ToolResult::error("Sin imagen en '" + fuente +
                                                  "': no es una escena, una fuente ni una "
                                                  "clave de feeds_list (" +
                                                  err + ")."));
                seguir(img);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_ui";
        t.title = "Captura de la app";
        t.category = "imagenes";
        t.readOnly = true;
        t.description =
            "Captura de pantalla de la propia aplicacion: la ventana principal (lo que ve el operador) "
            "y, con window=all, tambien las demas ventanas abiertas: las de salida de video y las "
            "pestañas que el operador saco a su propio monitor (ver ui_page_window).";
        t.inputSchema = withImageProps(
            json{{"window", {{"type", "string"}, {"enum", {"main", "all"}}, {"default", "main"}}}});
        t.run = [self, &rt, &core](const json& a, Done done) {
            QQuickWindow* main = self->mainWindow_ ? self->mainWindow_() : nullptr;
            if (main == nullptr) return done(ToolResult::error("La interfaz grafica no esta abierta."));
            ToolResult r;
            r.text("Ventana principal");
            addImage(r, main->grabWindow(), a, 1600, core, rt.dataRoot());
            if (a.value("window", std::string("main")) == "all") {
                int n = 1;
                for (QWindow* w : QGuiApplication::topLevelWindows()) {
                    auto* q = qobject_cast<QQuickWindow*>(w);
                    if (q == nullptr || q == main || !q->isVisible()) continue;
                    // El titulo dice cual es: no hace falta distinguir aca entre
                    // una salida de video y una pestaña suelta.
                    r.text("Ventana " + std::to_string(n++) + ": " + q->title().toStdString());
                    addImage(r, q->grabWindow(), a, 1600, core, rt.dataRoot());
                }
            }
            done(r);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
