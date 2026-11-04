// ============================================================================
// src/app/mcp/McpDebateTools.cpp — Herramientas MCP del motor: el debate entre dos (asamblea/Debate).
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "mcp/McpFamilia.hpp"
#include "asamblea/Debate.hpp"
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
#include "emision/VentanillaMotor.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerDebateTools() {
    InterfazMcp& vm = vm_;

    // --- El debate entre dos (asamblea/Debate) --------------------------------------
    if (vm.debate() != nullptr) {
        asamblea::Debate* deb = vm.debate();
        {
            Tool t;
            t.name = "debate_estado";
            t.title = ba::i18n::traducir("mcp", "El debate en curso");
            t.category = "oradores";
            t.clase = Clase::Lectura;
            t.description = QT_TRANSLATE_NOOP("mcp", "Si hay un debate: los dos (A y B, de la sala o remotos, con el canal de cada remoto), de qué "
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
                QT_TRANSLATE_NOOP("mcp", "Debate entre `a` y `b` (banca, id o nombre del Cuerpo): dos de la sala, dos remotos o uno y uno. Termina "
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
            t.description = QT_TRANSLATE_NOOP("mcp", "`turno`: a, b, ambos o nadie. Al que no le toca se le cierra el micrófono (en la central, o su "
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
            t.description = QT_TRANSLATE_NOOP("mcp", "Cierra los micrófonos de los dos, les saca la palabra a los remotos y devuelve la central "
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
            t.description = QT_TRANSLATE_NOOP("mcp", "`escenas`: {sala_sala, sala_remoto, remoto_remoto} = la escena que se arma para cada tipo de "
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
            t.run = [deb, &vm](const json& a, Done done) {
                if (a.contains("escenas") && a["escenas"].is_object()) {
                    const FotoMotor& foto = vm.ventanilla().foto(); // la interfaz no entra al motor
                    for (const auto& [k, v] : a["escenas"].items())
                        if (v.is_string() && !v.get<std::string>().empty() && foto.escena(v.get<std::string>()) == nullptr)
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
        t.description = QT_TRANSLATE_NOOP("mcp", "El cuerpo deliberativo: nombre e integrantes con banca, cargo oficial, bloque, distrito, "
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Pone el nombre del cuerpo (p. ej. «Asamblea 2025-2029»). No borra integrantes.");
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Importa una planilla CSV (coma o punto y coma, con encabezado: banca, nombre, cargo, bloque, "
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Escribe una planilla CSV de ejemplo (encabezado y dos filas) en Exportado/ y devuelve la ruta.");
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Con `id` lo cambia; sin `id`, lo crea (`nombre` obligatorio). Campos: nombre, cargo_oficial, "
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
            const QString e = errorDe([&] { vm.cuerpo().guardar(m); });
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
        t.description = QT_TRANSLATE_NOOP("mcp", "La ficha de un integrante del cuerpo, buscado por `clave`: su id, su número de banca o su nombre.");
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Marca que una persona revisó el nombre y el cargo de un integrante (lo que sale en el zocalo). "
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
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra un integrante del cuerpo (`id`, de cuerpo_listar): su nombre, su cargo y su banca dejan de estar en "
                                                 "el cuerpo.");
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
        t.description = QT_TRANSLATE_NOOP("mcp", "La foto de un integrante (`banca`, id o nombre), que es la cara de su botón en la botonera: "
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
}

} // namespace ba::app::mcp
