// ============================================================================
// src/app/mcp/McpConfigTools.cpp — Herramientas MCP del motor: configuración: listar, cambiar y guardar los ajustes.
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "mcp/McpFamilia.hpp"
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
#include "mcp/McpToolsCommon.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerConfigTools() {
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;

    // --- Configuracion -------------------------------------------------------------
    {
        Tool t;
        t.name = "settings_list";
        t.title = ba::i18n::traducir("mcp", "Ajustes");
        t.category = "config";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Todos los ajustes (o los de una seccion) con clave, valor, tipo, opciones, rango, ayuda y "
            "alcance (live = en el momento, gateway = reinicia la conferencia, restart = al proximo arranque).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"section", {{"type", "string"}}}}}};
        SettingsModel* settings = settings_.get();
        t.run = [settings](const json& a, Done done) {
            settings->revert(); // lo vigente, sin borradores viejos
            json rows = qvToJson(settings->filas());
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
            std::string(QT_TRANSLATE_NOOP("mcp", "Cambia uno o varios ajustes por clave y los aplica y guarda. Los de alcance 'gateway' "
            "reinician la conferencia (los invitados se reconectan solos); los 'motor' (el lienzo, la salida, los "
            "cuadros por segundo, el formato y el color: video.width, video.height, video.outputWidth, "
            "video.outputHeight, video.fps, video.formato, video.color, video.rango) REINICIAN EL MOTOR; los "
            "'restart', al proximo arranque.")) + "\n\n" + avisoReiniciaMotor();
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"values", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "{clave: valor} (ver settings_list).")}}},
              {"resetSection",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Volver a los valores por defecto de una seccion.")}}}}}};
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
            // La vista se recarga sola (la foto trae la configuración nueva). Se aplica en el
            // dueño del motor: se contesta cuando termina.
            settings->aplicar([&vm, motor, done](const QString& summary, bool fallo) {
                if (fallo) return done(ToolResult::error(summary.toStdString()));
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
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "app_save";
        t.title = ba::i18n::traducir("mcp", "Guardar configuración");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Guarda escenas y ajustes en disco (las herramientas ya guardan solas; esto fuerza).");
        t.hilo = Hilo::Control; // la configuración es del dueño
        t.run = [&rt](const json&, Done done) {
            done(rt.save() ? ToolResult::ok(ba::i18n::traducir("mcp", "Guardado.")) : ToolResult::error(rt.lastError()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
