// ============================================================================
// src/app/mcp/McpImageTools.cpp — Herramientas de imagen (vision)
//
// Todo lo que se puede VER, como imagen para un modelo con vision:
//   foto_programa    el programa (lo que sale al aire), a resolucion completa
//   foto_fuente     cualquier fuente o escena (tambien una que NO esta al
//                      aire: sirve para revisar antes de cambiar)
//   foto_todas_escenas  todas las escenas en una grilla con su nombre
//   capture_feed       un video crudo de A (camara de un invitado, lobby, fila,
//                      pantalla compartida, feed del operador)
//   foto_invitado     la imagen de una persona de la sala, por nombre o id
//   foto_todos_invitados     todas las personas de la sala, una imagen cada una
//   capture_sequence   N cuadros separados en el tiempo (analisis de video:
//                      movimiento, gestos, si alguien habla, cortes)
//   capture_ui         captura de la propia app (principal, salidas y pestañas sueltas)
// ============================================================================

#include <QFont>
#include "i18n/I18n.hpp"
#include "ajustes/Ajustes.hpp"
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
#include "agent/Mirada.hpp"
#include "agent/ModelosLocales.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"
#include "ui/ConferenceClient.hpp"
#include "sala/ParticipantesSala.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/VideoViewModel.hpp"
#include "video/Scopes.hpp"
#include "video/VideoGrafos.hpp"
#include "platform/Hilo.hpp"
#include <QBuffer>
#include <thread>

namespace ba::app::mcp {

namespace {

/// Una fuente o escena ("" = el programa) como QImage, sin que espere la
/// interfaz (el MCP corre en su hilo): la captura la arranca el dueño del motor (su
/// agenda, con el turno), el cuadro se espera en otro hilo y `listo` se llama de vuelta en
/// el hilo de la interfaz, con la imagen (o nula y el error).
void renderAparte(AppRuntime& rt, const std::string& name, int maxWidth,
                  std::function<void(QImage img, std::string err)> listo) {
    rt.despuesEnDueno([&rt, name, maxWidth, listo = std::move(listo)]() mutable {
        rt.engine().capturarAparte(
            name, maxWidth,
            [listo = std::move(listo)](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) mutable {
                QImage img;
                if (e.empty() && w > 0 && h > 0) img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
                QMetaObject::invokeMethod(
                    qApp, [listo = std::move(listo), img, e]() mutable { listo(img, e); }, Qt::QueuedConnection);
            });
    });
}

bool feedImage(StudioViewModel& vm, const std::string& key, QImage& out) {
    auto* frames = static_cast<FrameService*>(vm.frames());
    std::uint64_t serial = 0;
    return frames->latest(QString::fromStdString(key), out, serial) && !out.isNull();
}

const json kImageProps = {
    {"maxWidth",
     {{"type", "integer"}, {"minimum", 64}, {"maximum", 3840}, {"description", QT_TRANSLATE_NOOP("mcp", "Ancho maximo en px.")}}},
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
            p.drawText(QRect(cx, cy, cellW, cellH), Qt::AlignCenter, QString::fromStdString(ba::i18n::traducir("mcp", "(sin imagen)")));
        }
        const bool live = items[i].first == highlight;
        if (live) {
            p.setPen(QPen(QColor(255, 59, 48), 4));
            p.drawRect(cx + 2, cy + 2, cellW - 4, cellH - 4);
        }
        p.setPen(live ? QColor(255, 90, 80) : QColor(235, 235, 240));
        p.drawText(QRect(cx + 6, cy + cellH, cellW - 12, labelH), Qt::AlignLeft | Qt::AlignVCenter,
                   (live ? QString::fromStdString(ba::i18n::llenar(ba::i18n::traducir("mcp", "● AL AIRE — %1"), {items[i].first.toStdString()}))
                        : items[i].first));
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
/// mira. Es un pedido sin estado: una imagen, una pregunta, un texto. El pedido
/// en sí lo arma agent/Mirada (lo usa también el tótem para leer el dorso
/// de un documento).
///
/// Devuelve el texto, o deja el motivo en `error`.
std::string mirarConElModeloPropio(AppRuntime& rt, const QImage& img, const std::string& que,
                                   std::string& error) {
    agent::PedidoMirada p;
    p.sistema = ba::i18n::traducir("mcp", "Mirás una imagen y contestás la pregunta, en dos o tres líneas. "
                "Decís lo que VES: si no se ve, decís que no se ve. No inventás "
                "lo que debería haber. No describís el resto.");
    p.pregunta = que;
    // 768 px: alcanza de sobra para decir si hay alguien, si el cuadro esta en
    // negro o que dice un rotulo, y mantiene el pedido chico. Una captura de
    // 1600 px no se ve mejor, se paga mas cara.
    p.anchoMax = 768;
    p.maxTokens = 300;
    return agent::mirarConElModeloPropio(rt.dataRoot(), img, p, error);
}

void McpService::registerImageTools() {
    core_.addCategory(
        "imagenes", ba::i18n::traducir("mcp", "Imágenes y video (visión)"),
        ba::i18n::traducir("mcp", "Ver el programa, cualquier fuente o escena, cada persona, la app, y secuencias en el tiempo."));
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    auto* self = this;
    // Por referencia local: los lambdas de las herramientas no capturan `this`.
    McpCore& core = core_;

    {
        Tool t;
        t.name = "foto_programa";
        t.title = ba::i18n::traducir("mcp", "Ver el programa");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Una foto de lo que sale AL AIRE ahora, para ver si está negro, congelado o mal encuadrado. Imagen de "
            "lo que esta saliendo al aire ahora (la escena del programa, render completo del motor). Por defecto "
            "1280 px de ancho, JPEG.");
        t.inputSchema = withImageProps(json::object());
        t.run = [&rt, &vm, &core](const json& a, Done done) {
            renderAparte(rt, "", a.value("maxWidth", 1280), [&rt, &vm, &core, a, done](QImage img, std::string err) {
                if (img.isNull() && !feedImage(vm, "program", img))
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay imagen del programa: %1"), {err})));
                ToolResult r;
                r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Programa — escena '%1' (%2x%3)"),
                                       {vm.currentScene().toStdString(), std::to_string(img.width()),
                                        std::to_string(img.height())}));
                addImage(r, img, a, 1280, core, rt.dataRoot());
                done(r);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_scope";
        t.title = ba::i18n::traducir("mcp", "Instrumentos de video (forma de onda, vectorscopio…)");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Forma de onda (`onda`), RGB parade (`parade`), vectorscopio (`vector`) o histograma "
            "(`histograma`) del programa o de una `fuente`, más cuánto de la imagen está fuera de rango: "
            "`legalidad` = % de negros aplastados, blancos quemados y puntos fuera de la gama compuesta PAL "
            "(lo que balegal corrige). La imagen se guarda en disco (no viaja en la conversación).");
        t.inputSchema = withImageProps(json{
            {"fuente", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Fuente o escena; sin esto, el programa.")}}},
            {"tipo", {{"type", "string"}, {"enum", {"onda", "parade", "vector", "histograma"}}, {"default", "onda"}}}});
        t.run = [&rt, &vm, &core](const json& a, Done done) {
            video::TipoScope tipo = video::TipoScope::Onda;
            if (!video::tipoScopeDeTexto(QString::fromStdString(a.value("tipo", std::string("onda"))), tipo))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "`tipo`: onda, parade, vector o histograma")));
            const std::string fuente = a.value("fuente", std::string());
            renderAparte(rt, fuente, 960, [&rt, &vm, &core, a, done, fuente, tipo](QImage img, std::string err) {
            if (img.isNull() && (!fuente.empty() || !feedImage(vm, "program", img)))
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay imagen: %1"), {err})));
            const video::Legalidad l = video::medirLegalidad(img);
            const QImage s = video::dibujarScope(img, tipo, tipo == video::TipoScope::Vector ? 420 : 640,
                                                 tipo == video::TipoScope::Vector ? 420 : 360);
            ToolResult r;
            r.text((fuente.empty() ? ba::i18n::traducir("mcp", "Programa") : "'" + fuente + "'") + " — " +
                   a.value("tipo", std::string("onda")));
            r.data(json{{"legalidad",
                         {{"negros_aplastados_pct", std::round(l.lumaBajaPct * 10) / 10},
                          {"blancos_quemados_pct", std::round(l.lumaAltaPct * 10) / 10},
                          {"fuera_de_gama_pct", std::round(l.cromaPct * 10) / 10}}},
                        {"legalizador", vm.videoVista() != nullptr ? vm.videoVista()->legalizar() : std::string("no")}});
            addImage(r, s, a, s.width(), core, rt.dataRoot());
            done(r);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_legalizar";
        t.title = ba::i18n::traducir("mcp", "Legalizar para TV");
        t.category = "imagenes";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Pasa cada entrada por el legalizador broadcast-safe (balegal, en la cadena de GStreamer de la "
            "entrada): `modo` no | pal (luma/croma en rango y gama compuesta +133 %) | ntsc (más estricto, "
            "+120 %) | rango (solo luma y croma). Para entregar a un canal. Sin argumentos informa.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"modo", {{"type", "string"}, {"enum", {"no", "pal", "ntsc", "rango"}}}}}}};
        // La configuración y el grafo de video son del dueño del motor: corre allá.
        t.hilo = Hilo::Control;
        t.run = [&rt, &vm](const json& a, Done done) {
            if (a.contains("modo")) {
                rt.config().legalizar = a.value("modo", std::string("no"));
                rt.save();
                rt.fotoSucia(FotoMotor::Ajustes);
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
        t.name = "foto_fuente";
        t.title = ba::i18n::traducir("mcp", "Ver una fuente o escena");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "La foto de UNA fuente (cámara, imagen, texto, invitado) o de una escena entera, esté o no al aire. "
            "Ideal para revisar una escena antes de cambiarla, o verificar lo que armaste con scene_define.");
        t.inputSchema =
            withImageProps(json{{"name", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Fuente o escena.")}}}});
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
                       (negra ? " — " + ba::i18n::traducir("mcp", "sale NEGRA (sin señal todavía, o la imagen es negra de verdad)") : ""));
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
        t.name = "foto_todas_escenas";
        t.title = ba::i18n::traducir("mcp", "Vista multiple de escenas");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "Una sola foto con TODAS las escenas en grilla, cada una con su nombre; la del aire, marcada en rojo. "
            "Como el multiview de un switcher.");
        t.inputSchema = withImageProps(
            json{{"cellWidth", {{"type", "integer"}, {"minimum", 160}, {"maximum", 960}, {"default", 400}}}});
        t.run = [&rt, &vm, &core](const json& a, Done done) {
            const int cellW = a.value("cellWidth", 400);
            // Las escenas y la del aire, de la foto del motor.
            std::vector<std::string> nombres;
            if (VideoViewModel* vv = vm.videoVista())
                for (const QString& n : vv->escenas()) nombres.push_back(n.toStdString());
            if (nombres.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay escenas.")));
            // Una escena por vez, sin que espere la interfaz (renderAparte); al
            // final, la grilla.
            auto juntas = std::make_shared<std::vector<std::pair<QString, QImage>>>();
            auto paso = std::make_shared<std::function<void(std::size_t)>>();
            *paso = [&rt, &vm, &core, a, done, cellW, nombres, juntas, paso](std::size_t i) {
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
            r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "%n escena; al aire: %1", nullptr, static_cast<int>(items.size())),
                                   {vm.currentScene().toStdString()}));
            addImage(r, contactSheet(items, cellW, cols, vm.currentScene()), a, 0, core, rt.dataRoot());
            done(r);
            };
            (*paso)(0);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_feed";
        t.title = ba::i18n::traducir("mcp", "Ver un video de entrada");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Ultimo cuadro de un video que llega a la app: program, guest:N (camara del invitado del "
            "lugar N), share:N (su pantalla), lobby:K (preview del lobby), backstage:K (fila de "
            "oradores), operator (con lo que te ve el lobby). feeds_list muestra cuales hay.");
        t.inputSchema = withImageProps(json{{"key", {{"type", "string"}}}});
        t.inputSchema["required"] = {"key"};
        t.run = [&vm, &rt, &core](const json& a, Done done) {
            QImage img;
            if (!feedImage(vm, a["key"], img))
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Sin imagen en '%1' (nadie publica ese video ahora). feeds_list muestra los validos."),
                                                       {a["key"].get<std::string>()})));
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
        if (!key.empty() && feedImage(vm, key, img)) return cb(img, ba::i18n::llenar(ba::i18n::traducir("mcp", "video en vivo (%1)"), {key}));
        if (!p.value("hasAvatar", false)) return cb(QImage(), ba::i18n::traducir("mcp", "sin imagen (camara apagada o sin medios)"));
        auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
        // La misma foto que muestran las tarjetas de la GUI (modelo: sala::ParticipantesSala).
        conf->participantes().foto(p.value("id", std::string()), [cb](bool ok, const QByteArray& jpeg) {
            QImage a;
            if (ok) a.loadFromData(jpeg, "JPEG");
            cb(a, ok ? ba::i18n::traducir("mcp", "foto de hace unos segundos (sin video en vivo en la app)") : ba::i18n::traducir("mcp", "sin imagen"));
        });
    };
    {
        Tool t;
        t.name = "foto_invitado";
        t.title = ba::i18n::traducir("mcp", "Ver a una persona");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "La foto de UNA persona de la conferencia, por nombre o id: su cámara en vivo o su última imagen. Con "
            "includeScreen tambien su pantalla compartida.");
        t.inputSchema =
            withImageProps(json{{"participant", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Nombre o id.")}}},
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
                                      (p.value("slot", 0) > 0 ? " " + ba::i18n::traducir("mcp", "(al aire)") : "") + " — " + how);
                               if (!img.isNull()) addImage(r, img, a, 960, core, rt.dataRoot());
                               if (a.value("includeScreen", false) && p.value("screen", false) &&
                                   p.value("slot", 0) > 0) {
                                   QImage s;
                                   if (feedImage(vm, "share:" + std::to_string(p.value("slot", 0)), s)) {
                                       r.text(ba::i18n::traducir("mcp", "Su pantalla compartida:"));
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
        t.name = "foto_todos_invitados";
        t.title = ba::i18n::traducir("mcp", "Ver a todas las personas");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "Una foto de CADA persona de la conferencia (lobby, adentro y fila de oradores), con su nombre y estado. "
            "Default 480 px.");
        t.inputSchema = withImageProps(json{
            {"state", {{"type", "string"}, {"enum", {"all", "waiting", "admitted"}}, {"default", "all"}}}});
        t.run = [&vm, &rt, &core, personImage](const json& a, Done done) {
            const json snap = roomSnapshot(vm);
            const std::string only = a.value("state", std::string("all"));
            std::vector<json> people;
            for (const auto& p : snap.value("participants", json::array()))
                if (only == "all" || p.value("state", "") == only) people.push_back(p);
            if (people.empty()) return done(ToolResult::ok(ba::i18n::traducir("mcp", "No hay nadie en la conferencia.")));
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
                                                           ? ", " + ba::i18n::llenar(ba::i18n::traducir("mcp", "al aire en el lugar %1"),
                                                                                 {std::to_string(p.value("slot", 0))})
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
        t.title = ba::i18n::traducir("mcp", "Secuencia de cuadros (video)");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Toma `frames` imagenes separadas `intervalMs` de: el programa, una fuente/escena, un video "
            "(clave de feeds_list) o una persona. Para analizar movimiento, si alguien habla o hace "
            "gestos, si un video avanza, transiciones, congelamientos.");
        t.inputSchema = withImageProps(json{
            {"target",
             {{"type", "string"},
              {"description", QT_TRANSLATE_NOOP("mcp", "'program', nombre de fuente/escena, o clave de video (guest:1...).")}}},
            {"participant", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "En vez de target: una persona.")}}},
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
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 no tiene video en vivo en la app ahora "
                                                                       "(foto_invitado da su foto)."),
                                                  {p.value("name", "")})));
            }
            const int maxW = a.value("maxWidth", 640);
            // Sin que espere la interfaz: la imagen llega a `listo` (ver renderAparte).
            const auto grab = [&rt, &vm, target, maxW](std::function<void(QImage, std::string)> listo) {
                if (target == "program") return renderAparte(rt, "", maxW, std::move(listo));
                if (target.find(':') != std::string::npos) {
                    QImage img;
                    if (feedImage(vm, target, img)) return listo(img, std::string());
                    return listo(QImage(), ba::i18n::llenar(ba::i18n::traducir("mcp", "sin imagen en %1"), {target}));
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
            seq->r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Secuencia de %1 cuadros de '%2' cada %3 ms:"),
                                        {std::to_string(seq->left), target, std::to_string(interval)}));
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
        t.name = "imagen_preguntar";
        t.title = ba::i18n::traducir("mcp", "Que se ve: se lo pregunta a un agente que mira");
        t.category = "imagenes";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "Contesta una PREGUNTA concreta sobre lo que se ve en una cámara, el programa, una escena o la app; "
            "no devuelve la foto. Solo video, nada de audio. Devuelve TEXTO.");
        t.details = QT_TRANSLATE_NOOP("mcp", "ESTA ES LA UNICA FORMA DE MIRAR, y no es un rodeo: tu conversacion aguanta muy pocas "
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
                    "propio. No la uses para mirar cada dos segundos.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"que"}},
            {"properties",
             {{"que",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "la pregunta concreta que tiene que contestar mirando la imagen")}}},
              {"fuente",
               {{"type", "string"},
                {"description",
                 QT_TRANSLATE_NOOP("mcp", "que mirar: 'programa' (lo que sale al aire, por defecto), 'interfaz' (la "
                 "pantalla de la app), el NOMBRE de una escena o fuente (cualquiera, este o no al aire: no "
                 "hace falta armarla), o una clave de feeds_list (guest:1, lobby:2, armada...)")}}}}}};
        t.examples = {R"({"que": "Se ve a alguien o esta el cuadro en negro?"})",
                      R"({"que": "El rotulo de abajo dice el nombre correcto?", "fuente": "programa"})",
                      R"({"que": "Cuantas personas hay en cuadro?", "fuente": "guest:1"})"};
        t.run = [&vm, &rt, self](const json& a, Done done) {
            const std::string que = a.value("que", std::string());
            if (que.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Decile QUE tiene que mirar: sin pregunta concreta "
                                              "devuelve una descripcion que no sirve para decidir.")));
            const std::string fuente = a.value("fuente", std::string("programa"));

            // Con la imagen en la mano, lo que sigue (se llama ya, o cuando llega
            // el cuadro: renderAparte, que no hace esperar a la interfaz).
            const auto seguir = [&vm, &rt, a, done, que, fuente](QImage img) {
            if (img.isNull()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo tomar la imagen.")));

            const QString archivo = guardarCaptura(img, rt.dataRoot(), a, 1280);
            if (archivo.isEmpty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo guardar la captura para que la miren.")));

            const std::string donde =
                fuente == "interfaz" ? ba::i18n::traducir("mcp", "la pantalla de la aplicacion")
                                     : ba::i18n::llenar(ba::i18n::traducir("mcp", "el video de '%1'"), {fuente});
            const QImage copia = img;
            // Al servidor propio, en un hilo aparte (el cliente HTTPS bloquea y
            // esto corre en el hilo de la interfaz). Ver `mirarConElModeloPropio`.
            const auto conElPropio = [&rt, copia, que, donde, archivo, done](const std::string& antes) {
                std::thread([&rt, copia, que, donde, archivo, done, antes]() {
                    ba::platform::nombrarHilo("imagen-modelo");
                    std::string error;
                    const std::string texto = mirarConElModeloPropio(rt, copia, que, error);
                    ToolResult r;
                    if (texto.empty()) {
                        r = ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No pude mirar la imagen (%1%2). La captura quedo en %3."),
                                                               {antes, error, archivo.toStdString()}));
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
                QString::fromStdString(que) + "\n" +
                //: «castellano» es el idioma de la traducción: poné el nombre de tu idioma
                QString::fromStdString(ba::i18n::traducir("mcp", "Contesta en castellano, concreto, en una o dos oraciones. Si no se ve, decilo."));
            ml->ver(archivo, pregunta, 160, {}, [conElPropio, donde, archivo, done](const json& j) {
                const std::string texto = j.value("texto", std::string());
                if (j.contains("error") || texto.empty())
                    return conElPropio(ba::i18n::llenar(ba::i18n::traducir("mcp", "modelo local: %1"),
                                                       {j.value("error", ba::i18n::traducir("mcp", "sin respuesta"))}) +
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
                if (w == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La interfaz grafica no esta abierta.")));
                return seguir(w->grabWindow());
            }
            if (fuente == "programa" || fuente == "program") {
                return renderAparte(rt, "", 1280, [&vm, seguir, done](QImage img, std::string err) {
                    if (img.isNull() && !feedImage(vm, "program", img))
                        return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay imagen del programa: %1"), {err})));
                    seguir(img);
                });
            }
            QImage img;
            if (feedImage(vm, fuente, img)) return seguir(img);
            // Una escena o una fuente por su nombre: se dibuja aparte (no toca
            // el programa ni la armada).
            renderAparte(rt, fuente, 1280, [seguir, done, fuente](QImage img, std::string err) {
                if (img.isNull())
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Sin imagen en '%1': no es una escena, una fuente ni una "
                                                                       "clave de feeds_list (%2)."),
                                                  {fuente, err})));
                seguir(img);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capture_ui";
        t.title = ba::i18n::traducir("mcp", "Captura de la app");
        t.category = "imagenes";
        t.hilo = Hilo::Interfaz;
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Captura de pantalla de la propia aplicacion: la ventana principal (lo que ve el operador) "
            "y, con window=all, tambien las demas ventanas abiertas: las de salida de video y las "
            "pestañas que el operador saco a su propio monitor (ver ui_page_window).");
        t.inputSchema = withImageProps(
            json{{"window", {{"type", "string"}, {"enum", {"main", "all"}}, {"default", "main"}}}});
        t.run = [self, &rt, &core](const json& a, Done done) {
            QQuickWindow* main = self->mainWindow_ ? self->mainWindow_() : nullptr;
            if (main == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La interfaz grafica no esta abierta.")));
            ToolResult r;
            r.text(ba::i18n::traducir("mcp", "Ventana principal"));
            addImage(r, main->grabWindow(), a, 1600, core, rt.dataRoot());
            if (a.value("window", std::string("main")) == "all") {
                int n = 1;
                for (QWindow* w : QGuiApplication::topLevelWindows()) {
                    auto* q = qobject_cast<QQuickWindow*>(w);
                    if (q == nullptr || q == main || !q->isVisible()) continue;
                    // El titulo dice cual es: no hace falta distinguir aca entre
                    // una salida de video y una pestaña suelta.
                    r.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Ventana %1: %2"), {std::to_string(n++), q->title().toStdString()}));
                    addImage(r, q->grabWindow(), a, 1600, core, rt.dataRoot());
                }
            }
            done(r);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
