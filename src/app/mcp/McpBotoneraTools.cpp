// ============================================================================
// src/app/mcp/McpBotoneraTools.cpp — bus de eventos y botonera por MCP
//
// eventos_*: lo que pasa en la app por temas (control/BusEventos), para que el
// asistente escriba plugins y botones que reaccionan a algo.
// botonera_*: la superficie de control estilo Companion (control/Botonera):
// perfiles, páginas, celdas, aparatos, emparejamiento y auditoría.
//
// Lo que el asistente NO puede: aprobar un emparejamiento (solo una persona,
// en la página Botonera), sumar herramientas a un perfil ni habilitar la
// edición desde el aparato. Lo que dispara una celda de aire, al aire, lo
// confirma una persona.
// ============================================================================

#include "i18n/I18n.hpp"
#include "mcp/McpBotoneraTools.hpp"

#include <QPointer>
#include <QJsonDocument>
#include <QTimer>

#include <memory>

#include "control/Botonera.hpp"
#include "control/BotoneraGateway.hpp"
#include "control/BusEventos.hpp"
#include "control/CarasBotonera.hpp"
#include "control/Dsk.hpp"
#include "control/Acciones.hpp"
#include "control/SatelliteServidor.hpp"
#include "mcp/McpCore.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

json objetoCelda() {
    return {{"type", "object"},
            {"description", ba::i18n::traducir("mcp", "{tipo: herramienta, herramienta, args} | {tipo: pagina, ir_a} | {tipo: secuencia, pasos, "
                            "espera_ms} | {tipo: receta, id}. En args, \"{{valor}}\" (fader/perilla), \"{{delta}}\" "
                            "(perilla relativa), \"{{no_activo}}\" (alternar: mudo, por ejemplo).")}};
}

} // namespace

void registerBotoneraTools(McpCore& core, StudioViewModel& vm) {
    core.addCategory("botonera", ba::i18n::traducir("mcp", "Botonera y eventos"),
                     ba::i18n::traducir("mcp", "La superficie de control (tablet por QR, Stream Deck, MIDI por plugin) y "
                                               "el bus de eventos de la app (lo que está al aire, salidas, niveles, "
                                               "alarmas) para botones, plugins y disparadores."));
    control::BusEventos& bus = vm.bus();
    control::Botonera& bot = vm.botonera();

    // --- Bus de eventos ---------------------------------------------------------------
    {
        Tool t;
        t.name = "eventos_temas";
        t.title = ba::i18n::traducir("mcp", "Temas del bus de eventos");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los temas que publica la app con su ultimo valor: aire/escena, aire/armada, aire/tally "
                        "{programa, preview, fuentes_programa, fuentes_preview, transmitiendo, grabando}, salida/stream, "
                        "salida/grabacion, audio/nivel/<fuente> {pico_db, volumen_db, mudo}, audio/loudness {m, s, i, "
                        "tp, lra, fuera_de_objetivo}, alarma/<regla>, entrada/senal, oradores/al_aire, "
                        "sala/participantes y plugin/<id>/<canal>. Además aire/medios, aire/dsk y aire/tbar {hacia, "
                        "posicion}. Esos son de la salida por omisión; los de cada salida llevan su id: "
                        "aire/<salida>/escena|armada|tally|medios|dsk|tbar, salida/<salida>/stream|grabacion|estado, "
                        "audio/<salida>/nivel/<fuente> (su fader y su mudo) y `salidas` {lista}. Una ligadura con el "
                        "tema sin id sigue a la salida que maneja el aparato. `patron` filtra: «+» un nivel, «#» el "
                        "resto (audio/nivel/+, aire/#). Sirven para ligaduras de la botonera y @plugin.escuchar.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"patron", {{"type", "string"}}}}}};
        t.run = [&bus](const json& a, Done done) {
            done(ToolResult().data({{"temas", bus.temas(a.value("patron", std::string()))}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "eventos_ultimo";
        t.title = ba::i18n::traducir("mcp", "Ultimo valor de un tema");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El ultimo valor publicado en `tema` (eventos_temas), con la hora.");
        t.inputSchema = {{"type", "object"}, {"required", {"tema"}}, {"properties", {{"tema", {{"type", "string"}}}}}};
        t.run = [&bus](const json& a, Done done) {
            const json u = bus.ultimo(a.value("tema", std::string()));
            if (u.is_null()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ese tema no se publico todavia (eventos_temas)")));
            done(ToolResult().data(u));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "eventos_esperar";
        t.title = ba::i18n::traducir("mcp", "Esperar un evento");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Espera hasta `timeout_s` (de fabrica 20, como mucho 60) a que se publique algo en alguno de "
                        "los `temas` (patrones) y lo devuelve. Para reaccionar a «salio al aire», «se cayo la entrada», "
                        "una alarma. Los niveles de audio cambian todo el tiempo: no los esperes.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"temas"}},
                         {"properties",
                          {{"temas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"timeout_s", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}}}}}};
        t.run = [&bus](const json& a, Done done) {
            struct Estado {
                bool listo = false;
                std::vector<int> subs;
                Done done;
            };
            auto st = std::make_shared<Estado>();
            st->done = std::move(done);
            auto* busPtr = &bus;
            const auto terminar = [st, busPtr](ToolResult r) {
                if (st->listo) return;
                st->listo = true;
                for (int id : st->subs) busPtr->desuscribir(id);
                st->done(std::move(r));
            };
            for (const auto& p : a.value("temas", json::array())) {
                if (!p.is_string()) continue;
                st->subs.push_back(bus.suscribir(p.get<std::string>(), [terminar](const std::string& tema, const json& v) {
                    // Desde el oyente no se desuscribe en el acto (estamos dentro del bus).
                    QTimer::singleShot(0, [terminar, tema, v]() { terminar(ToolResult().data({{"tema", tema}, {"valor", v}})); });
                }));
            }
            if (st->subs.empty()) return terminar(ToolResult::error(ba::i18n::traducir("mcp", "faltan temas")));
            QTimer::singleShot(a.value("timeout_s", 20) * 1000, [terminar]() {
                terminar(ToolResult().text(ba::i18n::traducir("mcp", "No paso nada en ese tiempo.")).data({{"timeout", true}}));
            });
        };
        core.addTool(std::move(t));
    }

    // --- Perfiles -----------------------------------------------------------------------
    {
        Tool t;
        t.name = "botonera_perfiles";
        t.title = ba::i18n::traducir("mcp", "Perfiles de la botonera");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los perfiles (cada uno es un PUESTO: director, audio, sala…) con sus paginas y las "
                        "herramientas que puede usar. Un aparato emparejado muestra el perfil que tiene asignado.");
        t.run = [&bot](const json&, Done done) { done(ToolResult().data({{"perfiles", bot.perfiles()}})); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_plantillas";
        t.title = ba::i18n::traducir("mcp", "Plantillas de la botonera");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las plantillas de fabrica (director, audio, asamblea, prompter): se arman con las escenas, "
                        "fuentes y bancas que hay ahora.");
        t.run = [&bot](const json&, Done done) { done(ToolResult().data({{"plantillas", bot.plantillas()}})); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_perfil_crear";
        t.title = ba::i18n::traducir("mcp", "Crear un perfil de botonera");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Un perfil nuevo, vacio o `desde_plantilla` (botonera_plantillas). Desde una plantilla trae "
                        "las herramientas que usan sus botones; vacio, sin herramientas: los permisos los da una "
                        "persona en la pagina Botonera.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"nombre", {{"type", "string"}}}, {"desde_plantilla", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            std::string id;
            json p = json::object();
            if (a.contains("nombre")) p["nombre"] = a["nombre"];
            const std::string e = bot.crearPerfil(p, a.value("desde_plantilla", std::string()), &id);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creado: %1"), {id})).data(bot.perfil(id)));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_perfil_editar";
        t.title = ba::i18n::traducir("mcp", "Editar un perfil de botonera");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cambia `nombre`, `herramientas_permitidas` (solo QUITAR: sumar lo hace una persona) o "
                        "`puede_editar` (solo apagarlo).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"herramientas_permitidas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"puede_editar", {{"type", "boolean"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            json c = a;
            c.erase("perfil");
            const std::string e = bot.editarPerfil(a.value("perfil", std::string()), c, false);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_perfil_borrar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Borrar un perfil de botonera");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Borra el `perfil` (los aparatos que lo tenian quedan sin perfil). Confirmalo con el operador.");
        t.inputSchema = {{"type", "object"}, {"required", {"perfil"}}, {"properties", {{"perfil", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            if (!bot.borrarPerfil(a.value("perfil", std::string()))) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese perfil")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_exportar";
        t.title = ba::i18n::traducir("mcp", "Exportar un perfil");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El `perfil` entero en JSON (para guardarlo o pasarlo a otro equipo con botonera_importar).");
        t.inputSchema = {{"type", "object"}, {"required", {"perfil"}}, {"properties", {{"perfil", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            const json p = bot.exportar(a.value("perfil", std::string()));
            if (p.is_null()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese perfil")));
            done(ToolResult().data(p));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_importar";
        t.title = ba::i18n::traducir("mcp", "Importar un perfil");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Crea un perfil desde el `json` de botonera_exportar. Entra SIN herramientas permitidas: las da "
                        "una persona.");
        t.inputSchema = {{"type", "object"}, {"required", {"json"}}, {"properties", {{"json", {{"type", "object"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            std::string id;
            const std::string e = bot.importar(a.value("json", json::object()), &id);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Importado como %1 (sin permisos todavia)."), {id})));
        };
        core.addTool(std::move(t));
    }

    // --- Paginas y celdas -------------------------------------------------------------------
    {
        Tool t;
        t.name = "botonera_paginas";
        t.title = ba::i18n::traducir("mcp", "Paginas de un perfil");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las paginas del `perfil` con su grilla (columnas x filas) y cuantas celdas tienen.");
        t.inputSchema = {{"type", "object"}, {"required", {"perfil"}}, {"properties", {{"perfil", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            for (const auto& p : bot.perfiles())
                if (p.value("id", std::string()) == a.value("perfil", std::string()))
                    return done(ToolResult().data({{"paginas", p["paginas"]}}));
            done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese perfil")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_pagina_crear";
        t.title = ba::i18n::traducir("mcp", "Crear una pagina");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Una pagina nueva en el `perfil`, con `columnas` y `filas` (1 a 16).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "nombre", "columnas", "filas"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"columnas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}},
                           {"filas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.run = [&bot](const json& a, Done done) {
            std::string id;
            const std::string e = bot.crearPagina(a.value("perfil", std::string()), a.value("nombre", std::string()),
                                                  a.value("columnas", 4), a.value("filas", 3), &id);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creada: %1"), {id})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_pagina_editar";
        t.title = ba::i18n::traducir("mcp", "Editar una pagina");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cambia `nombre`, `columnas` o `filas` de la `pagina`.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"pagina", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"columnas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}},
                           {"filas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.run = [&bot](const json& a, Done done) {
            json c = a;
            c.erase("perfil");
            c.erase("pagina");
            const std::string e = bot.editarPagina(a.value("perfil", std::string()), a.value("pagina", std::string()), c);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_pagina_borrar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Borrar una pagina");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Borra la `pagina` (un perfil siempre conserva al menos una).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina"}},
                         {"properties", {{"perfil", {{"type", "string"}}}, {"pagina", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            if (!bot.borrarPagina(a.value("perfil", std::string()), a.value("pagina", std::string())))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no se pudo (¿es la unica?)")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrada.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_celdas";
        t.title = ba::i18n::traducir("mcp", "Celdas de una pagina");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las celdas de la `pagina` con su definicion y lo que muestran ahora (render: texto, fondo, "
                        "titilar, valor…).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina"}},
                         {"properties", {{"perfil", {{"type", "string"}}}, {"pagina", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            const json c = bot.celdas(a.value("perfil", std::string()), a.value("pagina", std::string()));
            if (c.is_null()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay esa pagina")));
            done(ToolResult().data({{"celdas", c}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_celda_set";
        t.title = ba::i18n::traducir("mcp", "Poner una celda");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Pone (o reemplaza) la celda en (`x`, `y`) de la `pagina`. `tipo`: boton, fader, perilla, medidor, texto o "
            "reloj. `estilo`: {texto, icono, color, fondo, tamano_texto}. `accion` al apretar (o al soltar si hay "
            "`accion_mantener`, que corre al mantener 0,7 s), `accion_soltar`. `valor` para fader/perilla: {min, max, "
            "paso, inicial, modo: absoluto|relativo}. `estado`: ligaduras al bus [{tema, ruta, op (== != > < >= <= en "
            "contiene existe), valor, efecto (fondo color texto titilar valor icono activo nivel), dato}]; en texto, "
            "«{{v}}» es el valor. Ej. tally: {tema: aire/tally, ruta: programa, op: ==, valor: Camara 2, efecto: fondo, "
            "dato: #c62828}. La herramienta de la accion tiene que estar en herramientas_permitidas del perfil.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina", "x", "y"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"pagina", {{"type", "string"}}},
                           {"x", {{"type", "integer"}, {"minimum", 0}}},
                           {"y", {{"type", "integer"}, {"minimum", 0}}},
                           {"ancho", {{"type", "integer"}, {"minimum", 1}}},
                           {"alto", {{"type", "integer"}, {"minimum", 1}}},
                           {"tipo", {{"type", "string"}, {"enum", {"boton", "fader", "perilla", "medidor", "texto", "reloj"}}}},
                           {"estilo", {{"type", "object"}}},
                           {"accion", objetoCelda()},
                           {"accion_soltar", objetoCelda()},
                           {"accion_mantener", objetoCelda()},
                           {"valor", {{"type", "object"}}},
                           {"estado", {{"type", "array"}, {"items", {{"type", "object"}}}}}}}};
        t.run = [&bot](const json& a, Done done) {
            json c = a;
            c.erase("perfil");
            c.erase("pagina");
            const std::string e = bot.setCelda(a.value("perfil", std::string()), a.value("pagina", std::string()), c);
            if (!e.empty()) return done(ToolResult::error(e));
            const std::string aviso = bot.avisoPermisos(a.value("perfil", std::string()), c);
            done(ToolResult::ok(aviso.empty() ? ba::i18n::traducir("mcp", "Listo.") : ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo. Ojo: %1"), {aviso})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_cara";
        t.title = ba::i18n::traducir("mcp", "Poner la cara de un botón (foto o video corto)");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Le pone de cara al botón en (`x`, `y`) una foto o un video de hasta 5 s, que se ve en la tablet, en el "
            "Stream Deck y en la grilla de la app (el video se repite a 8 cuadros/s). De `archivo` (imagen, GIF o video: "
            "los primeros 5 s) o de la cámara `entrada` (lo que muestra ahora: apuntala antes con ptz_apuntar o "
            "ptz_encuadre); con `segundos` (1-5) graba un video, si no saca una foto. `al_tocar`: además, al tocar el "
            "botón pone ese medio en la escena al aire (medio_poner; el perfil tiene que permitirla). Si no hay celda en "
            "(`x`, `y`), crea un botón (como la pagina Botonera). Tarda lo que dura la toma; devuelve el id (estilo.imagen = «medio:<id>»).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina", "x", "y"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"pagina", {{"type", "string"}}},
                           {"x", {{"type", "integer"}, {"minimum", 0}}},
                           {"y", {{"type", "integer"}, {"minimum", 0}}},
                           {"archivo", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Ruta de una imagen, GIF o video.")}}},
                           {"entrada", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "La cámara (ptz_camaras).")}}},
                           {"segundos", {{"type", "number"}, {"minimum", 1}, {"maximum", 5}}},
                           {"al_tocar", {{"type", "boolean"}}}}}};
        t.examples = {R"({"perfil": "operador", "pagina": "principal", "x": 0, "y": 0, "entrada": "Cámara 1", "segundos": 3})",
                      R"({"perfil": "operador", "pagina": "principal", "x": 1, "y": 0, "archivo": "/Users/yo/logo.png"})"};
        t.run = [&bot, &vm](const json& a, Done done) {
            const std::string perfil = a.value("perfil", std::string()), pagina = a.value("pagina", std::string());
            const int x = a.value("x", 0), y = a.value("y", 0);
            if (const std::string e = bot.validarLugar(perfil, pagina, x, y); !e.empty()) return done(ToolResult::error(e));
            control::CarasBotonera& caras = vm.carasBotonera();
            const QString archivo = QString::fromStdString(a.value("archivo", std::string()));
            const QString entrada = QString::fromStdString(a.value("entrada", std::string()));
            if (archivo.isEmpty() == entrada.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Pasá `archivo` o `entrada` (uno)")));
            QVariantMap r;
            if (!archivo.isEmpty())
                r = caras.importar(archivo);
            else if (a.contains("segundos"))
                r = caras.grabar(entrada, a.value("segundos", 5.0));
            else
                r = caras.capturarFoto(entrada);
            if (r.contains("error")) return done(ToolResult::error(r.value("error").toString().toStdString()));
            // Cuando termina ESTA toma: la cara en la celda (y la acción, si se pidió).
            auto con = std::make_shared<QMetaObject::Connection>();
            *con = QObject::connect(
                &caras, &control::CarasBotonera::lista, &caras,
                [&bot, &caras, con, mia = r.value("id").toString(), perfil, pagina, x, y, al = a.value("al_tocar", false),
                 done](const QString& id, const QString& error) {
                    if (id != mia) return;
                    QObject::disconnect(*con);
                    if (!error.isEmpty()) return done(ToolResult::error(error.toStdString()));
                    std::string aviso;
                    const std::string e = bot.ponerCara(perfil, pagina, x, y, id.toStdString(), al, &aviso);
                    if (!e.empty()) return done(ToolResult::error(e));
                    const int nCuadros = caras.info(id).value("cuadros").toInt();
                    const std::string cuadros = ba::i18n::traducir("mcp", "%n cuadro", nullptr, nCuadros);
                    done(ToolResult::ok(
                        aviso.empty()
                            ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: cara «medio:%1» (%2)."), {id.toStdString(), cuadros})
                            : ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: cara «medio:%1» (%2; %3)."), {id.toStdString(), cuadros, aviso})));
                });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_celda_borrar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Borrar una celda");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Saca la celda en (`x`, `y`).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina", "x", "y"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"pagina", {{"type", "string"}}},
                           {"x", {{"type", "integer"}}},
                           {"y", {{"type", "integer"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            if (!bot.borrarCelda(a.value("perfil", std::string()), a.value("pagina", std::string()), a.value("x", -1),
                                 a.value("y", -1)))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay una celda ahi")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrada.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_presionar";
        t.title = ba::i18n::traducir("mcp", "Probar una celda");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Prueba la celda (`x`, `y`) por el camino completo, como si la apretaran en un aparato: "
                        "`accion` presionar (de fabrica), soltar, valor (`v`) o girar (`v` = delta). Pasa por los "
                        "permisos del perfil; lo de aire, al aire, lo aprueba una persona. El resultado queda en "
                        "botonera_auditoria.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil", "pagina", "x", "y"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"pagina", {{"type", "string"}}},
                           {"x", {{"type", "integer"}}},
                           {"y", {{"type", "integer"}}},
                           {"accion", {{"type", "string"}, {"enum", {"presionar", "soltar", "valor", "girar"}}}},
                           {"v", {{"type", "number"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            const std::string que = a.value("accion", std::string("presionar"));
            json msg{{"type", que}, {"x", a.value("x", 0)}, {"y", a.value("y", 0)}};
            if (que == "valor") msg["v"] = a.value("v", 0.0);
            if (que == "girar") msg["delta"] = a.value("v", 1.0);
            const std::string e = bot.simular(a.value("perfil", std::string()), a.value("pagina", std::string()), msg);
            if (!e.empty()) return done(ToolResult::error(e));
            if (que == "presionar") bot.simular(a.value("perfil", std::string()), a.value("pagina", std::string()),
                                                {{"type", "soltar"}, {"x", a.value("x", 0)}, {"y", a.value("y", 0)}});
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Enviado (mira botonera_auditoria para el resultado).")));
        };
        core.addTool(std::move(t));
    }

    // --- Aparatos ---------------------------------------------------------------------------
    {
        Tool t;
        t.name = "botonera_dispositivos";
        t.title = ba::i18n::traducir("mcp", "Aparatos de la botonera");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las tablets emparejadas y los aparatos de hardware de los plugins (origen plugin), con su "
                        "perfil, pagina, IP, ultimo uso y si estan conectados ahora. Tambien las solicitudes de "
                        "emparejamiento que esperan a una persona.");
        t.run = [&bot](const json&, Done done) {
            json sol = json::array();
            for (const QVariant& v : bot.solicitudesUi()) {
                const QVariantMap m = v.toMap();
                sol.push_back({{"nombre", m.value("nombre").toString().toStdString()},
                               {"ip", m.value("ip").toString().toStdString()},
                               {"perfil", m.value("perfil").toString().toStdString()}});
            }
            done(ToolResult().data({{"dispositivos", bot.dispositivos()}, {"solicitudes_pendientes", sol}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_dispositivo_asignar";
        t.title = ba::i18n::traducir("mcp", "Asignar perfil a un aparato");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "El `dispositivo` pasa a mostrar el `perfil` (y la `pagina`, opcional).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"dispositivo", "perfil"}},
                         {"properties",
                          {{"dispositivo", {{"type", "string"}}}, {"perfil", {{"type", "string"}}}, {"pagina", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            const std::string e = bot.asignar(a.value("dispositivo", std::string()), a.value("perfil", std::string()),
                                              a.value("pagina", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_emparejar";
        t.title = ba::i18n::traducir("mcp", "Emparejar una tablet");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Genera un codigo de un uso (vence en 2 minutos) atado al `perfil` y la URL para el QR (el "
                        "codigo va en el fragmento #p=, que no sale en registros). Cuando la tablet la abre, una "
                        "PERSONA aprueba el emparejamiento en la pagina Botonera: el asistente no puede. `vence_dias`: "
                        "dias sin uso hasta que el token deja de valer (30 de fabrica, 0 = no vence).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"perfil", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"vence_dias", {{"type", "integer"}, {"minimum", 0}, {"maximum", 365}}}}}};
        t.run = [&vm](const json& a, Done done) {
            vm.botoneraGateway().emparejar(
                a.value("perfil", std::string()), a.value("nombre", std::string()), a.value("vence_dias", 30),
                [done](const json& r) {
                    if (r.contains("error") && !r.contains("url"))
                        return done(ToolResult::error(r.value("error", std::string())));
                    json d{{"url", r.value("url", std::string())},
                           {"vence_ms", r.value("vence_ms", std::int64_t{0})},
                           {"huella_tls", r.value("huella", std::string())},
                           {"aviso", r.value("aviso", std::string())}};
                    done(ToolResult()
                             .text(ba::i18n::traducir("mcp", "Abri la URL en la tablet (o mostrá el QR de la pagina Botonera). "
                                   "Despues el operador aprueba en la pagina Botonera."))
                             .data(d));
                });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_revocar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Revocar un aparato");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "El token del `dispositivo` (o de todas las tablets con \"todos\") deja de valer y se corta su "
                        "conexion. Confirmalo con el operador.");
        t.inputSchema = {{"type", "object"}, {"required", {"dispositivo"}}, {"properties", {{"dispositivo", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            const int n = bot.revocar(a.value("dispositivo", std::string()));
            if (n == 0) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese aparato")));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Revocados: %1"), {std::to_string(n)})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_auditoria";
        t.title = ba::i18n::traducir("mcp", "Auditoria de la botonera");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Lo que paso: emparejamientos, revocaciones, conexiones, cada accion hecha o rechazada (y por "
                        "que), confirmaciones de aire y ediciones. Filtra por `desde_ms` y `dispositivo`. Tambien queda "
                        "en Logs/botonera.log.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"desde_ms", {{"type", "integer"}}}, {"dispositivo", {{"type", "string"}}}}}};
        t.run = [&bot](const json& a, Done done) {
            done(ToolResult().data({{"auditoria", bot.auditoria(a.value("desde_ms", 0LL), a.value("dispositivo", std::string()))}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_estado";
        t.title = ba::i18n::traducir("mcp", "Estado de la botonera");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El servidor de la botonera en el gateway: conexiones vivas, credenciales cargadas, "
                        "solicitudes; y los aparatos de la app.");
        t.run = [&vm, &bot](const json&, Done done) {
            vm.botoneraGateway().estado([done, base = json{{"dispositivos", bot.dispositivos()}}](const json& gw) mutable {
                base["gateway"] = gw;
                done(ToolResult().data(base));
            });
        };
        core.addTool(std::move(t));
    }
    // --- DSK: capas encima del programa ---------------------------------------------------
    core.addCategory("dsk", ba::i18n::traducir("mcp", "Capas (DSK)"),
                     ba::i18n::traducir("mcp", "Cuatro capas globales encima del programa que no cambian con la escena: "
                                               "logo o mosca, el recuadro del intérprete de LSA, un zócalo fijo, un reloj."));
    {
        Tool t;
        t.name = "dsk_capas_ver";
        t.title = ba::i18n::traducir("mcp", "Capas DSK");
        t.category = "dsk";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las capas del DSK, por encima de todas las escenas (logo, intérprete, reloj): qué tienen y si están "
            "arriba en la `salida` (cada una sube y baja las suyas; sin ella, la por omisión).");
        t.porSalida = true;
        t.run = [&vm, &core](const json& a, Done done) { done(ToolResult().data({{"capas", vm.dsk().capas(core.salidaDe(a))}})); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dsk_definir";
        t.title = ba::i18n::traducir("mcp", "Definir una capa DSK");
        t.category = "dsk";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Arma la `capa` (1-4) con `contenido` (nombres de escenas o fuentes, apiladas en ese orden), "
                        "`recuadro` opcional {posicion: abajo_derecha|abajo_izquierda|arriba_derecha|arriba_izquierda, "
                        "tamano: 0..1 del ancho, margen}, `fijo` y `transicion_ms`. Con `lsa_entrada`, la plantilla "
                        "«Interprete LSA»: esa entrada en un recuadro abajo a la derecha, fija. No la sube: dsk_mostrar.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"capa"}},
                         {"properties",
                          {{"capa", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4}}},
                           {"nombre", {{"type", "string"}}},
                           {"contenido", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"recuadro", {{"type", "object"}}},
                           {"fijo", {{"type", "boolean"}}},
                           {"transicion_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}}},
                           {"lsa_entrada", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const int capa = a.value("capa", 1);
            std::string e;
            if (a.contains("lsa_entrada")) {
                e = vm.dsk().plantillaLsa(capa, a.value("lsa_entrada", std::string()), "abajo_derecha", 0.28);
            } else {
                json d = a;
                d.erase("capa");
                e = vm.dsk().definir(capa, d);
            }
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::traducir("mcp", "Listo (todavia abajo: dsk_mostrar para subirla).")).data({{"capas", vm.dsk().capas()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dsk_mostrar";
        t.title = ba::i18n::traducir("mcp", "Subir o bajar una capa DSK");
        t.category = "dsk";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "sube o baja una capa (DSK) encima del programa");
        t.description = ba::i18n::traducir("mcp", "Sube (`visible` true) o baja la `capa` con su fundido en la `salida` (sin ella, la por "
                        "omisión). Es de aire. Bajar una capa FIJA (el interprete de LSA) necesita `confirmar`: true, y "
                        "solo si el operador lo pidio.");
        t.porSalida = true;
        t.inputSchema = {{"type", "object"},
                         {"required", {"capa", "visible"}},
                         {"properties",
                          {{"capa", {{"type", "integer"}, {"minimum", 1}, {"maximum", 4}}},
                           {"visible", {{"type", "boolean"}}},
                           {"confirmar", {{"type", "boolean"}}}}}};
        t.run = [&vm, &core](const json& a, Done done) {
            const std::string e =
                vm.dsk().mostrar(a.value("capa", 1), a.value("visible", true), a.value("confirmar", false), core.salidaDe(a));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(a.value("visible", true) ? ba::i18n::traducir("mcp", "Arriba.") : ba::i18n::traducir("mcp", "Abajo.")));
        };
        core.addTool(std::move(t));
    }

    // --- Acciones y atajos de teclado ----------------------------------------------------
    {
        Tool t;
        t.name = "acciones_listar";
        t.title = ba::i18n::traducir("mcp", "Acciones de un golpe");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El catalogo de acciones (TAKE, FTB, placa, siguiente banca, siguiente punto, prompter, armar o "
                        "cortar a cada escena, mutear cada fuente, subir/bajar cada capa DSK…) con su id: lo usan los "
                        "atajos de teclado (atajo_asignar) y accion_ejecutar.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data({{"acciones", vm.acciones().catalogo()}})); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "atajos_listar";
        t.title = ba::i18n::traducir("mcp", "Atajos de teclado");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los atajos: tecla, accion y si es global (funciona aunque la ventana no tenga el foco).");
        t.run = [&vm](const json&, Done done) {
            done(ToolResult().data({{"atajos", vm.acciones().atajos()}, {"globales_disponibles", vm.acciones().globalesDisponibles()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "atajo_asignar";
        t.title = ba::i18n::traducir("mcp", "Asignar un atajo de teclado");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Asigna `tecla` («Ctrl+Shift+T», «F5», «Alt+1»; en la Mac «Ctrl» es Comando) a `accion` (id de "
                        "acciones_listar). `global`: funciona aunque la ventana no tenga el foco (Mac y Windows). Una tecla "
                        "que ya tiene otra accion da error: sacala primero. Un pedal de prompter que manda una tecla se "
                        "asigna igual.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"tecla", "accion"}},
                         {"properties", {{"tecla", {{"type", "string"}}}, {"accion", {{"type", "string"}}}, {"global", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.acciones().asignar(a.value("tecla", std::string()), a.value("accion", std::string()),
                                                        a.value("global", false));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "atajo_quitar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Quitar un atajo");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Saca el atajo de `tecla` (o de la accion con ese id).");
        t.inputSchema = {{"type", "object"}, {"required", {"tecla"}}, {"properties", {{"tecla", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.acciones().quitar(a.value("tecla", std::string()))) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay ese atajo")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "accion_ejecutar";
        t.title = ba::i18n::traducir("mcp", "Ejecutar una accion");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Ejecuta la `accion` (id de acciones_listar) como si se apretara su atajo, pero con los permisos "
                        "y la guarda de aire del que lo pide.");
        t.inputSchema = {{"type", "object"}, {"required", {"accion"}}, {"properties", {{"accion", {{"type", "string"}}}}}};
        t.run = [&vm, &core](const json& a, Done done) {
            const std::string accion = a.value("accion", std::string());
            const json r = vm.acciones().resolver(accion);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            // «salida:…» cambia la salida que manejan los atajos (no corre ninguna herramienta).
            if (r.contains("elegir_salida")) {
                const std::string e = vm.acciones().ejecutar(accion);
                return done(e.empty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")) : ToolResult::error(e));
            }
            // Por el mismo camino y con el contexto de quien la pidió (la guarda de aire aplica);
            // una acción «…@<salida>» va a esa salida.
            CallContext ctx = core.contextoEnCurso();
            if (r.value("salida_pedida", false)) ctx.salida = r.value("salida", std::string());
            core.callTool({{"name", r.value("herramienta", std::string())}, {"arguments", r.value("args", json::object())}},
                          [done](json res) {
                              ToolResult tr;
                              tr.isError = res.value("isError", false);
                              for (const auto& c : res.value("content", json::array()))
                                  if (c.value("type", "") == "text") tr.text(c.value("text", std::string()));
                              done(tr);
                          },
                          ctx);
        };
        core.addTool(std::move(t));
    }

    // --- Modo en vivo (protege la interfaz) -------------------------------------------------
    {
        Tool t;
        t.name = "modo_vivo_estado";
        t.title = ba::i18n::traducir("mcp", "Modo en vivo");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Si el modo en vivo esta activo (transmitiendo o con el evento en vivo; opcionalmente grabando), "
                        "que paginas protege (piden escribir CONFIRMAR con un motivo) y cuales se desbloquearon.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.app().modoVivoEstado())); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modo_vivo_ajustes";
        t.title = ba::i18n::traducir("mcp", "Ajustes del modo en vivo");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Fuera del vivo: `activo`, `incluir_grabando` y `paginas` protegidas (ids del riel: configuracion, "
                        "plugins, agentes, asistente…).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"incluir_grabando", {{"type", "boolean"}}},
                           {"paginas", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const std::string e = vm.app().setModoVivoAjustes(a);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(vm.app().modoVivoEstado()));
        };
        core.addTool(std::move(t));
    }

    // --- Recuperación: caídas y generaciones de la configuración -----------------------------
    {
        Tool t;
        t.name = "supervisor_caidas";
        t.title = ba::i18n::traducir("mcp", "Las ultimas caidas");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las ultimas `n` (10 de fabrica) caidas que anoto el supervisor: que se cayo (app o gateway), la "
                        "causa (error de memoria, abortado, colgado…), el codigo y la hora. La ultima la muestra un cartel "
                        "al volver, con cuanto tardo el aire en volver.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"n", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}}}};
        t.run = [&vm](const json& a, Done done) {
            json d{{"caidas", vm.app().caidas(a.value("n", 10))}};
            const QVariantMap c = vm.app().caidaReciente();
            if (!c.isEmpty()) {
                d["recien"] = {{"causa", c.value("causa").toString().toStdString()}, {"habia_aire", c.value("habia_aire").toBool()}};
                if (c.contains("aire_vuelta_s")) d["recien"]["aire_vuelta_s"] = c.value("aire_vuelta_s").toDouble();
            }
            done(ToolResult().data(d));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "config_generaciones";
        t.title = ba::i18n::traducir("mcp", "Generaciones de la configuracion");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Cada archivo de Config/ guarda la version anterior (.anterior) al guardarse; si el actual esta "
                        "roto, se carga solo el anterior. Sin argumentos, lista los que tienen anterior. Con `restaurar` "
                        "(nombre del archivo, p. ej. app.json) y fuera de vivo, vuelve a la version anterior (rige al "
                        "reiniciar).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"restaurar", {{"type", "string"}}}}}};
        t.run = [&vm, &core](const json& a, Done done) {
            namespace fs = std::filesystem;
            const fs::path dir = vm.datosRaiz() / "Config";
            if (a.contains("restaurar")) {
                if (vm.modoVivo() || core.hayAire()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no se restaura en vivo")));
                const std::string n = fs::path(a.value("restaurar", std::string())).filename().string();
                const fs::path prev = dir / (n + ".anterior");
                std::error_code ec;
                if (!fs::exists(prev, ec)) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay version anterior de %1"), {n})));
                fs::copy_file(dir / n, dir / (n + ".reemplazado"), fs::copy_options::overwrite_existing, ec);
                fs::copy_file(prev, dir / n, fs::copy_options::overwrite_existing, ec);
                if (ec) return done(ToolResult::error(ec.message()));
                return done(ToolResult::ok(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "Restaurado %1 (el que estaba quedo como .reemplazado). Rige al reiniciar."), {n})));
            }
            json lista = json::array();
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                const std::string n = e.path().filename().string();
                if (n.size() > 9 && n.compare(n.size() - 9, 9, ".anterior") == 0)
                    lista.push_back({{"archivo", n.substr(0, n.size() - 9)},
                                     {"anterior_bytes", fs::file_size(e.path(), ec)}});
            }
            done(ToolResult().data({{"con_anterior", lista}}));
        };
        core.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "media_restante";
        t.title = ba::i18n::traducir("mcp", "Cuanto le queda al medio al aire");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los videos o audios que se estan reproduciendo al aire y cuantos segundos les quedan (tambien en "
                        "el bus: aire/medios). Para avisar antes de que termine y armar lo que sigue.");
        t.run = [&vm](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : vm.salidas().mediosAlAire()) {
                const QVariantMap m = v.toMap();
                out.push_back({{"fuente", m.value("fuente").toString().toStdString()},
                               {"restante_s", m.value("restante_s").toInt()},
                               {"duracion_s", m.value("duracion_s").toInt()}});
            }
            done(ToolResult().data({{"medios", out}}));
        };
        core.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "stream_salud";
        t.title = ba::i18n::traducir("mcp", "Salud de la transmision");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Por destino (principal y los adicionales): kbps reales, cuadros perdidos por la red y por el "
                        "codificador, congestion (0 a 1), si esta reconectando, cuantas reconexiones hubo y cuanto tardo la "
                        "ultima. Con `destino` y `minutos` (hasta 10), la serie de a un punto por segundo.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"destino", {{"type", "string"}}}, {"minutos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}}};
        t.run = [&vm](const json& a, Done done) {
            json d{{"destinos", vm.emision().saludActual()}};
            if (a.contains("destino")) d["serie"] = vm.emision().saludSerie(a.value("destino", std::string()), a.value("minutos", 5));
            done(ToolResult().data(d));
        };
        core.addTool(std::move(t));
    }

    // --- Companion Satellite ---------------------------------------------------------------
    {
        Tool t;
        t.name = "botonera_satellite_estado";
        t.title = ba::i18n::traducir("mcp", "Companion Satellite: estado");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El servidor de la Satellite API de Bitfocus Companion (TCP 16622): si esta prendido, las IP "
                        "permitidas, las que pidieron y esperan aprobacion, y los Satellite conectados con sus "
                        "aparatos (Stream Deck, Loupedeck, X-keys… que maneja Companion Satellite en otra maquina).");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.satellite().estado())); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_satellite";
        t.title = ba::i18n::traducir("mcp", "Companion Satellite: prender, apagar, puerto");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Prende (`activo`) o apaga el servidor de la Satellite API en `puerto` (16622 de fabrica). "
                        "Cada aparato que agregue un Companion Satellite es un aparato mas de la botonera "
                        "(botonera_dispositivo_asignar). El protocolo no tiene clave: solo red local y solo las IP "
                        "que una PERSONA permite en la pagina Botonera (aca se pueden QUITAR con `quitar_ip`).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"puerto", {{"type", "integer"}, {"minimum", 1024}, {"maximum", 65535}}},
                           {"quitar_ip", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& s = vm.satellite();
            if (a.contains("quitar_ip")) s.permitir(QString::fromStdString(a.value("quitar_ip", std::string())), false);
            if (a.contains("activo") || a.contains("puerto")) {
                const QString e = s.configurar(a.value("activo", s.activo()), a.value("puerto", s.puerto()));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            done(ToolResult().data(s.estado()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "botonera_api_remota";
        t.title = ba::i18n::traducir("mcp", "API remota HTTP de la botonera (Companion «Generic HTTP»)");
        t.category = "botonera";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Prende o apaga (`activo`) la API remota: POST /api/botonera/<perfil>/pagina/<pagina>/celda/<x>/<y>/"
            "<presionar|soltar|tocar> en el puerto de la conferencia, con «Authorization: Bearer <token>». Es lo que "
            "llama el módulo «Generic HTTP» de Bitfocus Companion sin escribir código. Cada acceso tiene su "
            "perfil y sus permisos, como una tablet. El TOKEN lo crea y lo ve una PERSONA en Botonera → "
            "Integraciones («Nuevo acceso HTTP…»): acá no se entrega. Sin argumentos informa y lista los "
            "accesos (se revocan con botonera_dispositivo_revocar).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& b = vm.botonera();
            if (a.contains("activo")) b.setApiRemotaActiva(a.value("activo", false));
            done(ToolResult().data(json{{"activo", b.apiRemotaActiva()},
                                        {"accesos", b.accesosHttp()},
                                        {"url", vm.control().urlApiRemota().toStdString()}}));
        };
        core.addTool(std::move(t));
    }
}


} // namespace ba::app::mcp
