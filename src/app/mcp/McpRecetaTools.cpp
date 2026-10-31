// ============================================================================
// src/app/mcp/McpRecetaTools.cpp — recetas: hacer de una lo que son diez pasos
//
// Una receta es una LISTA DE LLAMADAS A HERRAMIENTAS, guardada con un nombre.
// Nada mas. Y ese «nada mas» es la decision de diseño, no una limitacion que
// falte completar:
//
//   * SIN condiciones ni bucles. Eso ya es un programa, y un programa que corre
//     en vivo es algo que a las ocho de la noche no puede depurar nadie. Si
//     hace falta decidir, decide el asistente, que para eso esta.
//   * SIN recetas que llamen a recetas. Sin eso no hay forma de armar un ciclo
//     infinito por accidente.
//   * Todo lo que una receta puede hacer es lo que YA puede hacer el asistente:
//     pasa por el mismo despachador, con la misma validacion de argumentos, la
//     misma aprobacion humana cuando corresponde y el mismo refresco de la
//     interfaz. Una receta no es una puerta de atras.
//
// SI UN PASO FALLA, SE FRENA. En vivo, seguir dejaria el sistema a mitad de
// camino y sin que nadie sepa donde: es peor que no haber hecho nada. Se
// informa en que paso se corto y por que.
//
// Se guardan en Config/recetas.json. Las escribe el asistente cuando el
// operador le dice «esto que acabamos de hacer, dejalo listo para la proxima».
// ============================================================================
#include <algorithm>
#include <memory>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "i18n/I18n.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"

namespace ba::app::mcp {
namespace {

QString archivo(AppRuntime& rt) {
    return QString::fromStdString((rt.dataRoot() / "Config" / "recetas.json").string());
}

json leer(AppRuntime& rt) {
    const json j = config::leerObjeto(archivo(rt).toStdString()).value("recetas", json::array());
    return j.is_array() ? j : json::array();
}

bool escribir(AppRuntime& rt, const json& recetas) { return config::guardar(archivo(rt).toStdString(), {{"recetas", recetas}}); }

/// Reemplaza `{clave}` por su valor en todos los textos del objeto, a
/// cualquier profundidad. Es lo mismo que hacen los graficos y los vinculos:
/// una receta sirve para muchos casos si se le puede pasar el nombre de turno.
json conParametros(const json& v, const json& par) {
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        for (auto it = par.begin(); it != par.end(); ++it) {
            const std::string marca = "{" + it.key() + "}";
            const std::string valor =
                it.value().is_string() ? it.value().get<std::string>() : it.value().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            for (std::size_t p = s.find(marca); p != std::string::npos; p = s.find(marca, p))
                s.replace(p, marca.size(), valor);
        }
        return s;
    }
    if (v.is_object()) {
        json o = json::object();
        for (auto it = v.begin(); it != v.end(); ++it)
            o[it.key()] = conParametros(it.value(), par);
        return o;
    }
    if (v.is_array()) {
        json a = json::array();
        for (const json& e : v)
            a.push_back(conParametros(e, par));
        return a;
    }
    return v;
}

} // namespace

void McpService::registerRecetaTools() {
    core_.addCategory("recetas", ba::i18n::traducir("mcp", "Recetas"),
                      ba::i18n::traducir("mcp", "Series de pasos guardadas con un nombre, para repetir de una lo que son "
                      "diez llamadas."));

    AppRuntime& rt = rt_;
    McpCore& core = core_;

    {
        Tool t;
        t.name = "receta_listar";
        t.title = ba::i18n::traducir("mcp", "Recetas que hay");
        t.category = "recetas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las recetas guardadas (secuencias de pasos para repetir igual, receta_guardar) que se pueden correr, con "
                                                 "su id y su nombre.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Antes de armar una nueva, fijate si ya hay una que haga eso: repetidas, "
                    "despues nadie sabe cual es la buena.");
        t.run = [&rt](const json&, Done done) { done(ToolResult().data(json{{"recetas", leer(rt)}})); };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "receta_guardar";
        t.title = ba::i18n::traducir("mcp", "Guardar una receta");
        t.category = "recetas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "GUARDA una receta: una secuencia de pasos para repetirla despues igual. Guarda (o "
                        "reemplaza) una serie de pasos con un nombre.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Un paso es `{ \"tool\": \"<herramienta>\", \"args\": { ... } }`. Los pasos corren EN "
                    "ORDEN y si uno falla la receta se frena ahi.\n\n"
                    "Los textos de `args` aceptan `{parametros}` entre llaves, que se reemplazan al "
                    "correrla: asi una misma receta sirve para todos los casos en vez de una por orador.\n\n"
                    "Ejemplo, la apertura de un bloque:\n\n"
                    "    pasos: [\n"
                    "      { \"tool\": \"scene_switch\",   \"args\": { \"name\": \"Camara 1\" } },\n"
                    "      { \"tool\": \"grafico_mostrar\", \"args\": { \"fuente\": 1, \"nombre\": "
                    "\"placa-titulo\",\n"
                    "                                                \"parametros\": { \"titulo\": "
                    "\"{titulo}\" } } },\n"
                    "      { \"tool\": \"record_start\",    \"args\": {} }\n"
                    "    ]\n\n"
                    "NO hay condiciones, bucles ni recetas que llamen a recetas: eso ya es un programa, y un "
                    "programa que corre en vivo no lo depura nadie a las ocho de la noche. Si hay que "
                    "decidir algo, decidilo vos y despues llama a la receta que corresponda.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Corto y sin espacios. Si ya existe, la reemplaza.")}}},
              {"nombre", {{"type", "string"}}},
              {"descripcion", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Que hace y cuando conviene usarla.")}}},
              {"pasos", {{"type", "array"}, {"items", {{"type", "object"}}}}}}},
            {"required", {"id", "pasos"}}};
        t.run = [&rt, &core](const json& a, Done done) {
            const std::string id = a["id"].get<std::string>();
            if (!a["pasos"].is_array() || a["pasos"].empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Una receta sin pasos no hace nada.")));

            // Se valida AHORA que cada herramienta exista: descubrir que un
            // paso no existe en vivo, a mitad de la receta, es el peor momento.
            for (const json& p : a["pasos"]) {
                if (!p.is_object() || !p.contains("tool") || !p["tool"].is_string())
                    return done(
                        ToolResult::error(ba::i18n::traducir("mcp", "Cada paso necesita `tool` con el nombre de una herramienta.")));
                const std::string n = p["tool"].get<std::string>();
                if (n.rfind("receta_", 0) == 0)
                    return done(ToolResult::error(
                        ba::i18n::traducir("mcp", "Una receta no puede llamar a otra receta: asi no hay forma de armar un "
                        "ciclo infinito sin darse cuenta.")));
                if (core.findTool(n) == nullptr)
                    return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la herramienta \"%1\"."), {n})));
            }

            json recetas = leer(rt);
            json nueva{{"id", id},
                       {"nombre", a.value("nombre", id)},
                       {"descripcion", a.value("descripcion", std::string())},
                       {"pasos", a["pasos"]}};
            bool reemplazo = false;
            for (json& r : recetas)
                if (r.value("id", std::string()) == id) {
                    r = nueva;
                    reemplazo = true;
                }
            if (!reemplazo) recetas.push_back(nueva);
            if (!escribir(rt, recetas)) return done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo guardar.")));
            done(ToolResult::ok(ba::i18n::llenar(
                reemplazo ? ba::i18n::traducir("mcp", "Receta reemplazada: %1 (%n paso).", nullptr, static_cast<int>(a["pasos"].size()))
                          : ba::i18n::traducir("mcp", "Receta guardada: %1 (%n paso).", nullptr, static_cast<int>(a["pasos"].size())),
                {id})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "receta_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar una receta");
        t.category = "recetas";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra una receta guardada (`id`, de receta_listar): esa secuencia de pasos deja de estar para correrla.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string id = a["id"].get<std::string>();
            json recetas = leer(rt);
            json quedan = json::array();
            for (const json& r : recetas)
                if (r.value("id", std::string()) != id) quedan.push_back(r);
            if (quedan.size() == recetas.size())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la receta \"%1\"."), {id})));
            if (!escribir(rt, quedan)) return done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo guardar.")));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Receta borrada: %1."), {id})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "receta_correr";
        t.title = ba::i18n::traducir("mcp", "Correr una receta");
        t.category = "recetas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Ejecuta en orden los pasos de una receta guardada (`id`), con `parametros` para sus `{...}`; si un paso "
                                                 "falla, se frena ahí.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Si un paso falla, SE FRENA AHI y te dice cual y por que: en vivo, seguir dejaria el "
                    "sistema a mitad de camino y sin que nadie sepa donde.\n\n"
                    "`parametros` reemplaza los `{...}` de los pasos.\n\n"
                    "Cada paso pasa por el mismo camino que si lo llamaras vos: se validan los argumentos, "
                    "se pide aprobacion humana si la politica lo exige y la interfaz se entera. Una receta "
                    "no puede hacer nada que no puedas hacer paso a paso.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "string"}}}, {"parametros", {{"type", "object"}}}}},
                         {"required", {"id"}}};
        t.run = [&rt, &core](const json& a, Done done) {
            const std::string id = a["id"].get<std::string>();
            json receta;
            for (const json& r : leer(rt))
                if (r.value("id", std::string()) == id) receta = r;
            if (receta.is_null()) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la receta \"%1\"."), {id})));

            const json par =
                a.contains("parametros") && a["parametros"].is_object() ? a["parametros"] : json::object();
            auto pasos = std::make_shared<json>(receta["pasos"]);
            auto hechos = std::make_shared<json>(json::array());
            auto i = std::make_shared<std::size_t>(0);

            // Encadenado a mano: las herramientas contestan por callback, asi
            // que «el paso siguiente» es lo que se hace cuando contesto el
            // anterior. Se guarda en un shared_ptr porque la lambda tiene que
            // poder llamarse a si misma y sobrevivir a esta funcion.
            auto siguiente = std::make_shared<std::function<void()>>();
            *siguiente = [&core, pasos, hechos, i, par, id, done, siguiente]() {
                if (*i >= pasos->size()) {
                    done(ToolResult().data(json{{"receta", id}, {"pasos", *hechos}, {"ok", true}}));
                    return;
                }
                const json paso = (*pasos)[*i];
                const std::string tool = paso.value("tool", std::string());
                json args = paso.contains("args") ? conParametros(paso["args"], par) : json::object();
                const std::size_t nro = *i + 1;
                ++(*i);
                core.callTool(
                    json{{"name", tool}, {"arguments", args}},
                    [hechos, nro, tool, id, done, siguiente](json r) {
                        const bool malo = r.value("isError", false);
                        std::string texto;
                        if (r.contains("content") && r["content"].is_array() && !r["content"].empty())
                            texto = r["content"][0].value("text", std::string());
                        hechos->push_back(
                            json{{"paso", nro}, {"tool", tool}, {"ok", !malo}, {"dijo", texto}});
                        if (malo) {
                            done(ToolResult::error(ba::i18n::llenar(
                                ba::i18n::traducir("mcp", "La receta \"%1\" se freno en el paso %2 (%3): %4. Lo anterior YA se hizo."),
                                {id, std::to_string(nro), tool, texto})));
                            return;
                        }
                        (*siguiente)();
                    },
                    CallContext{"", "", "receta"});
            };
            (*siguiente)();
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
