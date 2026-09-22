// ============================================================================
// src/app/mcp/McpUiTools.cpp — Interfaz (paginas, avisos, dashboard, ventanas
// de salida) y sistema (videos disponibles, registros, salud, salir)
// ============================================================================

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QVariant>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <deque>

#include "AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "ui/RielConfig.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/DashboardModel.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/SystemViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerUiTools() {
    core_.addCategory(
        "interfaz", "Interfaz de la app",
        "Navegar paginas, avisos en pantalla, tarjetas del dashboard, fondo y ventanas de salida "
        "(multiview en otro monitor).");
    core_.addCategory("sistema", "Sistema", "Videos disponibles, registros, salud, salir.");
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    auto* self = this;
    auto* dash = static_cast<DashboardModel*>(vm.dashboard());
    auto* windows = static_cast<OutputWindowsModel*>(dash->windows());

    {
        Tool t;
        t.name = "ui_show_page";
        t.title = "Mostrar una pagina";
        t.category = "interfaz";
        t.description = "Cambia la pagina que ve el operador: dashboard, escenas, entradas, audio, sala, "
                        "guion, graficos, rotulos, registro, youtube, twitch, facebook, configuracion o "
                        "asistente.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"page",
                            {{"type", "string"},
                             {"enum",
                              {"dashboard", "escenas", "entradas", "audio", "sala", "guion", "youtube",
                               "twitch", "facebook", "configuracion", "asistente", "graficos",
                               "rotulos", "registro"}}}},
                           {"seccion",
                            {{"type", "string"},
                             {"description",
                              "Solo con page=configuracion: a que seccion ir (el nombre tal cual lo "
                              "muestra la lista, p. ej. \"Audio\", \"Gateway\", \"Barra de "
                              "navegacion\"). Sin esto queda la que estaba."}}}}},
                         {"required", {"page"}}};
        t.run = [self, &vm](const json& a, Done done) {
            static const std::map<std::string, int> kPages{
                {"dashboard", 0}, {"escenas", 1},       {"sala", 2},      {"youtube", 3},  {"twitch", 4},
                {"facebook", 5},  {"configuracion", 6}, {"asistente", 7}, {"entradas", 8}, {"audio", 9},
                {"guion", 10}, {"graficos", 11}, {"rotulos", 12}, {"registro", 13}};
            QQuickWindow* w = self->mainWindow_ ? self->mainWindow_() : nullptr;
            if (w == nullptr) return done(ToolResult::error("La interfaz grafica no esta abierta."));
            w->setProperty("page", kPages.at(a["page"]));
            // Llevarlo a la seccion ANTES de mostrar la pagina haria que se
            // vea un salto; despues, no se nota.
            if (a.contains("seccion") && a["seccion"].is_string())
                vm.setSeccionConfig(QString::fromStdString(a["seccion"].get<std::string>()));
            done(ToolResult::ok("Pagina: " + a["page"].get<std::string>()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ui_page_window";
        t.title = "Sacar una pagina a su ventana";
        t.category = "interfaz";
        t.description =
            "Abre una pagina en su PROPIA ventana, para mandarla a otro monitor.";
        t.details =
            "No la saca de la ventana principal: abre otra vista de lo mismo. Las dos se mantienen "
            "sincronizadas solas (todas las paginas leen su estado del mismo lugar), asi que el operador "
            "puede tener el guion en un monitor y las escenas en el otro.\n\n"
            "El operador hace lo mismo con clic derecho en el icono del riel. Si esa pagina ya esta "
            "afuera, no se abre otra igual.\n\n"
            "Cerrar la ventana no pierde nada: la pagina sigue en la ventana principal.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"page",
                            {{"type", "string"},
                             {"enum",
                              {"dashboard", "escenas", "entradas", "audio", "sala", "guion", "youtube",
                               "twitch", "facebook", "configuracion", "asistente", "graficos",
                               "rotulos", "registro"}}}},
                           {"seccion",
                            {{"type", "string"},
                             {"description",
                              "Solo con page=configuracion: a que seccion ir (el nombre tal cual lo "
                              "muestra la lista, p. ej. \"Audio\", \"Gateway\", \"Barra de "
                              "navegacion\"). Sin esto queda la que estaba."}}}}},
                         {"required", {"page"}}};
        t.run = [self, &vm](const json& a, Done done) {
            static const std::map<std::string, std::pair<int, const char*>> kPages{
                {"dashboard", {0, "Dashboard"}},   {"escenas", {1, "Escenas"}},
                {"sala", {2, "Sala"}},             {"youtube", {3, "YouTube"}},
                {"twitch", {4, "Twitch"}},         {"facebook", {5, "Facebook"}},
                {"configuracion", {6, "Configuración"}}, {"asistente", {7, "Asistente"}},
                {"entradas", {8, "Entradas"}},     {"audio", {9, "Audio"}},
                {"guion", {10, "Guion del evento"}},
                {"graficos", {11, "Gráficos"}},
                {"rotulos", {12, "Rótulos"}}, {"registro", {13, "Registro"}}};
            QQuickWindow* w = self->mainWindow_ ? self->mainWindow_() : nullptr;
            if (w == nullptr) return done(ToolResult::error("La interfaz grafica no esta abierta."));
            const auto& [idx, titulo] = kPages.at(a["page"]);
            QMetaObject::invokeMethod(w, "sacarAVentana", Q_ARG(QVariant, idx),
                                      Q_ARG(QVariant, QString::fromUtf8(titulo)));
            done(ToolResult::ok(std::string("\"") + titulo + "\" abierta en su propia ventana."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ui_notify";
        t.title = "Aviso en pantalla";
        t.category = "interfaz";
        t.description = "Muestra un aviso (toast) al operador en la app.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"message", {{"type", "string"}}},
              {"level",
               {{"type", "string"}, {"enum", {"info", "ok", "warn", "error"}}, {"default", "info"}}}}},
            {"required", {"message"}}};
        t.run = [&vm](const json& a, Done done) {
            emit vm.notify(QString::fromStdString(a["message"]),
                           QString::fromStdString(a.value("level", std::string("info"))));
            done(ToolResult::ok("Aviso mostrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dashboard_list";
        t.title = "Dashboard y ventanas de salida";
        t.category = "interfaz";
        t.readOnly = true;
        t.description =
            "Tarjetas del dashboard (id, tipo, posicion), tipos que se pueden agregar, fondo, ventanas de "
            "salida abiertas (videos, disposicion, monitor, pantalla completa) y monitores conectados.";
        t.run = [dash, windows](const json&, Done done) {
            json kinds = json::array();
            for (const QString& k : dash->cardKinds())
                kinds.push_back(json{{"kind", k.toStdString()}, {"title", dash->kindTitle(k).toStdString()}});
            json bg = json::array();
            for (const QString& k : dash->background())
                bg.push_back(k.toStdString());
            json screens = json::array();
            for (QScreen* s : QGuiApplication::screens())
                screens.push_back(json{
                    {"name", s->name().toStdString()},
                    {"size", std::to_string(s->size().width()) + "x" + std::to_string(s->size().height())},
                    {"primary", s == QGuiApplication::primaryScreen()}});
            done(ToolResult().data(json{{"cards", modelRows(*dash)},
                                        {"cardKinds", kinds},
                                        {"background",
                                         {{"feeds", bg},
                                          {"layout", dash->backgroundLayout().toStdString()},
                                          {"dim", dash->backgroundDim()}}},
                                        {"outputWindows", modelRows(*windows)},
                                        {"screens", screens}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dashboard_card";
        t.title = "Agregar / quitar tarjeta";
        t.category = "interfaz";
        t.description =
            "Agrega una tarjeta al dashboard (kind de dashboard_list; monitor lleva `source` = clave de "
            "video) o quita una por id. reset=true vuelve a la disposicion por defecto.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"add", {{"type", "string"}, {"description", "Tipo de tarjeta."}}},
                           {"source", {{"type", "string"}}},
                           {"remove", {{"type", "integer"}}},
                           {"reset", {{"type", "boolean"}}}}}};
        t.run = [dash](const json& a, Done done) {
            if (a.value("reset", false)) dash->resetLayout();
            if (a.contains("remove")) dash->removeCard(a["remove"]);
            if (a.contains("add")) {
                const QString kind = QString::fromStdString(a["add"]);
                if (!dash->cardKinds().contains(kind))
                    return done(ToolResult::error("Tipo de tarjeta desconocido (dashboard_list)."));
                dash->addCard(kind, QString::fromStdString(a.value("source", std::string())));
            }
            done(ToolResult::ok("Dashboard actualizado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dashboard_background";
        t.title = "Fondo del dashboard";
        t.category = "interfaz";
        t.description = "Videos de fondo del dashboard (claves de feeds_list), disposicion y oscurecido.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"feeds", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"layout", {{"type", "string"}, {"enum", {"grid", "focus", "pip", "row"}}}},
                           {"dim", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}}}};
        t.run = [dash](const json& a, Done done) {
            if (a.contains("feeds")) {
                QStringList l;
                for (const auto& k : a["feeds"])
                    l << QString::fromStdString(k.get<std::string>());
                dash->setBackground(l);
            }
            if (a.contains("layout")) dash->setBackgroundLayout(QString::fromStdString(a["layout"]));
            if (a.contains("dim")) dash->setBackgroundDim(a["dim"]);
            done(ToolResult::ok("Fondo actualizado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "output_window";
        t.title = "Ventanas de salida (multiview)";
        t.category = "interfaz";
        t.description =
            "Abre una ventana con videos (claves de feeds_list: program, guest:1...) en una "
            "disposicion, o modifica/cierra una existente: monitor, pantalla completa, disposicion.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"open",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Videos a mostrar."}}},
              {"id", {{"type", "integer"}, {"description", "Ventana existente."}}},
              {"close", {{"type", "boolean"}}},
              {"layout", {{"type", "string"}, {"enum", {"grid", "focus", "pip", "row"}}}},
              {"screen", {{"type", "string"}, {"description", "Nombre del monitor (dashboard_list)."}}},
              {"fullscreen", {{"type", "boolean"}}}}}};
        t.examples = {
            R"({"open": ["program", "guest:1", "guest:2"], "layout": "pip", "screen": "HDMI-1", "fullscreen": true})"};
        t.run = [windows](const json& a, Done done) {
            int id = a.value("id", 0);
            if (a.contains("open")) {
                QStringList keys;
                for (const auto& k : a["open"])
                    keys << QString::fromStdString(k.get<std::string>());
                id = windows->open(keys, QString::fromStdString(a.value("layout", std::string("grid"))));
            } else if (id == 0) {
                return done(
                    ToolResult::error("Indica `open` (videos) o el `id` de una ventana (dashboard_list)."));
            }
            if (a.value("close", false)) {
                windows->close(id);
                return done(ToolResult::ok("Ventana " + std::to_string(id) + " cerrada."));
            }
            if (a.contains("layout") && !a.contains("open"))
                windows->setLayout(id, QString::fromStdString(a["layout"]));
            if (a.contains("screen")) windows->setScreen(id, QString::fromStdString(a["screen"]));
            if (a.contains("fullscreen")) windows->setFullscreen(id, a["fullscreen"]);
            done(ToolResult::ok("Ventana de salida " + std::to_string(id) + " lista."));
        };
        core_.addTool(std::move(t));
    }

    // --- Sistema -----------------------------------------------------------------
    {
        Tool t;
        t.name = "feeds_list";
        t.title = "Videos disponibles";
        t.category = "sistema";
        t.readOnly = true;
        t.description =
            "Claves de video que la app puede mostrar o capturar (program, guest:N, share:N, lobby:K, "
            "backstage:K, operator) con su etiqueta y si ahora tienen imagen.";
        t.run = [&vm](const json&, Done done) {
            auto* frames = static_cast<FrameService*>(vm.frames());
            QStringList keys = frames->keys();
            for (int k = 1; k <= 8; ++k) {
                keys << QString("lobby:%1").arg(k) << QString("backstage:%1").arg(k);
            }
            keys << "operator";
            json out = json::array();
            for (const QString& k : keys) {
                QImage img;
                std::uint64_t serial = 0;
                const bool live = frames->latest(k, img, serial);
                if (!live && (k.startsWith("lobby:") || k.startsWith("backstage:")))
                    continue; // los vacios no suman
                out.push_back(json{
                    {"key", k.toStdString()}, {"label", frames->label(k).toStdString()}, {"hasImage", live}});
            }
            done(ToolResult().data(json{{"feeds", out}}));
        };
        core_.addTool(std::move(t));
    }
    // --- La barra de navegacion ---------------------------------------------
    // Tiene vista (Configuracion -> Barra de navegacion), asi que se puede
    // publicar: la regla es que no se expone un setter que no se vea en
    // ningun lado (ver docs/EXTENSIONES.md).
    {
        Tool t;
        t.name = "ui_barra";
        t.title = "La barra de navegacion";
        t.category = "interfaz";
        t.description = "Donde va la barra lateral, de que tamaño y que paginas muestra.";
        t.details =
            "Sin argumentos, informa como esta. Con ellos, cambia lo que se le diga y deja el resto.\n\n"
            "`posicion`: izquierda (por defecto), derecha, arriba o abajo. Arriba y abajo convienen en "
            "pantallas anchas y bajas, donde lo que falta es altura.\n\n"
            "`tamano`: el lado del boton en pixeles, de 30 a 64. 34 es chico, 42 normal, 54 grande.\n\n"
            "`mostrar` y `esconder`: listas de paginas por nombre, los mismos que toma ui_show_page. "
            "Esconder una NO la desactiva: la pagina sigue existiendo, el asistente la sigue usando y "
            "se puede abrir en su propia ventana; lo unico que se esconde es su boton. Configuracion "
            "no se puede esconder, porque es por donde se vuelve.\n\n"
            "El cambio se ve al toque y se guarda solo en Config/riel.json.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"posicion", {{"type", "string"}, {"enum", {"izquierda", "derecha", "arriba", "abajo"}}}},
              {"tamano", {{"type", "integer"}, {"minimum", 30}, {"maximum", 64}}},
              {"separadores", {{"type", "boolean"}, {"description", "Lineas entre los grupos."}}},
              {"mostrar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"esconder", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"restaurar", {{"type", "boolean"}, {"description", "Todo como viene de fabrica."}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* riel = static_cast<RielConfig*>(vm.riel());
            if (riel == nullptr) return done(ToolResult::error("La interfaz grafica no esta abierta."));
            static const std::map<std::string, int> kPages{
                {"dashboard", 0}, {"escenas", 1},       {"sala", 2},      {"youtube", 3},  {"twitch", 4},
                {"facebook", 5},  {"configuracion", 6}, {"asistente", 7}, {"entradas", 8}, {"audio", 9},
                {"guion", 10}, {"graficos", 11}, {"rotulos", 12}, {"registro", 13}};

            if (a.value("restaurar", false)) riel->restaurar();
            if (a.contains("posicion"))
                riel->setPosicion(QString::fromStdString(a["posicion"].get<std::string>()));
            if (a.contains("tamano")) riel->setTamano(a["tamano"].get<int>());
            if (a.contains("separadores")) riel->setSeparadores(a["separadores"].get<bool>());
            for (const char* cual : {"mostrar", "esconder"}) {
                if (!a.contains(cual) || !a[cual].is_array()) continue;
                const bool ver = std::string(cual) == "mostrar";
                for (const json& p : a[cual]) {
                    if (!p.is_string()) continue;
                    const auto it = kPages.find(p.get<std::string>());
                    if (it == kPages.end())
                        return done(ToolResult::error("No conozco la pagina \"" +
                                                      p.get<std::string>() + "\"."));
                    riel->setVisible(it->second, ver);
                }
            }

            json paginas = json::array();
            for (const QVariant& v : riel->paginas()) {
                const QVariantMap m = v.toMap();
                paginas.push_back(json{{"titulo", m.value("titulo").toString().toStdString()},
                                       {"visible", m.value("visible").toBool()},
                                       {"grupo", m.value("grupo").toString().toStdString()}});
            }
            done(ToolResult().data(json{{"posicion", riel->posicion().toStdString()},
                                        {"tamano", riel->tamano()},
                                        {"separadores", riel->separadores()},
                                        {"escondidas", riel->escondidas()},
                                        {"paginas", paginas}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "logs_tail";
        t.title = "Registros";
        t.category = "sistema";
        t.readOnly = true;
        t.description =
            "Ultimas lineas CRUDAS de un archivo de Logs/, tal como esta escrito.";
        t.details =
            "Para diagnosticar, usa `log_leer`: lee los mismos archivos pero ya partidos en hora, "
            "nivel, modulo y descripcion, entrelazando los tres procesos y con filtros. Esto sirve "
            "cuando hace falta ver el archivo TAL CUAL, sin interpretar: por ejemplo "
            "cloudflared.log, que lo escribe un programa de terceros con su propio formato.\n\n"
            "Los archivos: app.log (Proceso A), gateway.log (Proceso B), graficos.log, "
            "launcher.log (supervisor) y cloudflared.log (tunel).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"log", {{"type", "string"}, {"default", "app.log"}}},
              {"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}, {"default", 80}}},
              {"grep", {{"type", "string"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            const QString dir = QString::fromStdString((rt.dataRoot() / "Logs").string());
            const QString name = QString::fromStdString(a.value("log", std::string("ba.log")));
            if (name.contains('/') || name.contains('\\') || name.contains(".."))
                return done(ToolResult::error("Solo nombres de archivo de Logs/."));
            QFile f(dir + "/" + name);
            if (!f.open(QIODevice::ReadOnly)) {
                QStringList logs = QDir(dir).entryList(QStringList{"*.log"}, QDir::Files);
                return done(ToolResult::error("No existe " + name.toStdString() +
                                              ". Hay: " + logs.join(", ").toStdString()));
            }
            // Solo el final: los registros pueden ser grandes.
            if (f.size() > 4 * 1024 * 1024) f.seek(f.size() - 4 * 1024 * 1024);
            const QString grep = QString::fromStdString(a.value("grep", std::string()));
            std::deque<QString> keep;
            const size_t n = a.value("lines", 80);
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.startsWith("debug:")) continue;
                if (!grep.isEmpty() && !line.contains(grep, Qt::CaseInsensitive)) continue;
                keep.push_back(line);
                if (keep.size() > n) keep.pop_front();
            }
            QString out;
            for (const QString& l : keep)
                out += l + "\n";
            done(ToolResult::ok(out.isEmpty() ? "(vacio)" : out.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "system_info";
        t.title = "Carpeta del sistema, credenciales y respaldos";
        t.category = "sistema";
        t.readOnly = true;
        t.description = "Donde vive el sistema (todo en una carpeta portable), que credenciales hay "
                        "guardadas (solo nombres; "
                        "estan cifradas en Credenciales/), los respaldos de datos y si hay una version "
                        "anterior para volver.";
        t.run = [&vm](const json&, Done done) {
            auto* sys = static_cast<SystemViewModel*>(vm.system());
            json creds = json::array();
            for (const QString& n : sys->credentials())
                creds.push_back(n.toStdString());
            done(ToolResult().data(json{{"dataRoot", sys->dataRoot().toStdString()},
                                        {"programDate", sys->version().toStdString()},
                                        {"credentials", creds},
                                        {"backups", qvToJson(sys->backups())},
                                        {"canRollback", sys->canRollback()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "system_update";
        t.title = "Actualizar el sistema";
        t.category = "sistema";
        t.destructive = true;
        t.description =
            "Actualiza el programa desde una carpeta con la version nueva (descomprimida, FUERA de la "
            "carpeta del "
            "sistema). La app se cierra, el launcher reemplaza SOLO el programa (los datos no se tocan: "
            "Config/, Credenciales/, Logs/, Recordings/, Shares/, Runtime/, Cache/, Respaldos/, Agentes/ y "
            "Media/; Credenciales/ y Config/ ademas se respaldan) y vuelve a abrir sola: esta sesion MCP "
            "se corta y hay que reconectar en ~20 s. Los agentes ya instalados siguen andando; si esta "
            "version fija otra, la pestaña Asistente ofrece actualizarlos. Con check=true solo valida la "
            "carpeta. rollback=true vuelve a la version anterior.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"path", {{"type", "string"}}},
                           {"check", {{"type", "boolean"}, {"default", false}}},
                           {"rollback", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* sys = static_cast<SystemViewModel*>(vm.system());
            if (a.value("rollback", false)) {
                const QString e = sys->startRollback();
                return done(e.isEmpty()
                                ? ToolResult::ok("Volviendo a la version anterior: la app se reinicia.")
                                : ToolResult::error(e.toStdString()));
            }
            const QString path = QString::fromStdString(a.value("path", std::string()));
            const QString err = sys->checkUpdate(path);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            if (a.value("check", false)) return done(ToolResult::ok("La carpeta es una version valida."));
            done(ToolResult::ok("Actualizando: la app se cierra y vuelve a abrir sola en unos segundos."));
            QTimer::singleShot(500, sys, [sys, path]() { sys->startUpdate(path); });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "app_quit";
        t.title = "Cerrar la aplicacion";
        t.category = "sistema";
        t.destructive = true;
        t.description =
            "Cierra la aplicacion (corta transmision y grabacion; el servidor MCP tambien se cierra).";
        t.run = [&vm](const json&, Done done) {
            done(ToolResult::ok("Cerrando la aplicacion."));
            QTimer::singleShot(300, &vm, [&vm]() { emit vm.quitRequested(); });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
