// ============================================================================
// src/app/mcp/McpRecintoTools.cpp — Herramientas MCP del motor: el recinto: la grilla de bancas, apuntar y enfocar, la toma de cada banca (asamblea/Cuerpo).
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include "ptz/ControlCamaras.hpp"
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
#include "asamblea/Bancas.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerRecintoTools() {
    InterfazMcp& vm = vm_;

    // --- El recinto: la grilla de bancas (asamblea/Cuerpo) ------------------------
    {
        Tool t;
        t.name = "recinto_ver";
        t.title = ba::i18n::traducir("mcp", "La grilla del recinto");
        t.category = "evento";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las bancas como se ven desde la cabina: {filas, columnas, bancas: [{banca, fila, columna, "
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia la grilla: `filas` y `columnas`; `auto: true` acomoda todas las bancas del cuerpo en "
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Lleva la PTZ a la toma de la banca (por banca, id o nombre) para tenerla lista, sin armar ni "
                        "abrir micrófonos. No mueve la cámara que está al aire. Para ponerla al aire: banca_al_aire; "
                        "para pedirlo de palabra (banca, persona o posición): enfocar.");
        t.inputSchema = {{"type", "object"}, {"required", {"banca"}}, {"properties", {{"banca", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = errorDe([&] { vm.bancas().apuntar(QString::fromStdString(a.value("banca", std::string()))); });
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
            QT_TRANSLATE_NOOP("mcp", "Lleva la cámara a lo que se pida DE PALABRA: «banca 10», «10», el nombre de un integrante "
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
                          {{"que", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "«banca 10», un nombre o el de una posición")}}},
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
            const QString e = errorDe([&] { vm.bancas().enfocar(QString::fromStdString(que), a.value("forzar", false)); });
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
            QT_TRANSLATE_NOOP("mcp", "Asocia la banca `banca` a una cámara (`camara`, la entrada) y a una de sus posiciones "
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
}

} // namespace ba::app::mcp
