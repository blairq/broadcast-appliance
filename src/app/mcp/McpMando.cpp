// src/app/mcp/McpMando.cpp — ver el .hpp

#include "mcp/McpMando.hpp"

#include "i18n/I18n.hpp"
#include "mcp/McpCore.hpp"

#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <memory>

namespace ba::app::mcp {

void registrarHerramientasDeAire(McpCore& core, MandoDeAire mando) {
    // Compartido por todas (vive lo que el núcleo: las herramientas lo guardan).
    const auto m = std::make_shared<const MandoDeAire>(std::move(mando));
    const auto existe = [m](const std::string& s) {
        const auto names = m->escenas();
        return std::find(names.begin(), names.end(), s) != names.end();
    };
    const auto noExiste = [m](const std::string& s) {
        std::string names;
        for (const auto& n : m->escenas()) names += (names.empty() ? "" : ", ") + n;
        return ToolResult::error(
            ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la escena '%1'. Escenas: %2."), {s, names}));
    };

    // --- El negro y la placa ------------------------------------------------------------
    {
        Tool t;
        t.name = "aire_ftb";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Fundido a negro (FTB)");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "manda la salida a negro");
        t.aireSi = AireSi::AlActivar;
        t.description = ba::i18n::traducir("mcp", "Fundido a negro de TODO lo que sale (encima del programa, sin tocar las escenas). `activar` "
                        "true/false, `ms` de fundido (de fábrica 800), `audio`: bajar (de fábrica: el audio baja con "
                        "el negro) o mantener. Activarlo al aire es una acción de aire (pide confirmación si lo pide "
                        "algo automático); sacarlo, no. Cada salida tiene su negro (`salida`).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}},
                           {"audio", {{"type", "string"}, {"enum", {"bajar", "mantener"}}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            if (const std::string e =
                    m->ftb(salida, a.value("activar", false), a.value("ms", 800), a.value("audio", std::string("bajar")));
                !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data(m->estadoPlaca(salida)));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "aire_placa";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Placa de seguridad");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "tapa la salida con una placa");
        t.aireSi = AireSi::AlActivar;
        t.description = ba::i18n::traducir("mcp", "Una placa encima de todo lo que sale (\"Cuarto intermedio\", \"Volvemos enseguida\", \"Minuto "
                        "de silencio\"…), con `texto`, una `imagen` opcional (ruta) y `ms` de fundido (600). No depende "
                        "de las escenas. Activarla al aire pide confirmación si la pide algo automático; sacarla, no. "
                        "Sobrevive a una caída de la app. Cada salida tiene su placa (`salida`).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"activar"}},
                         {"properties",
                          {{"activar", {{"type", "boolean"}}},
                           {"texto", {{"type", "string"}}},
                           {"imagen", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            if (const std::string e = m->placa(salida, a.value("activar", false), a.value("texto", std::string()),
                                               a.value("imagen", std::string()), a.value("ms", 600));
                !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data(m->estadoPlaca(salida)));
        };
        core.addTool(std::move(t));
    }

    // --- Las escenas: corte directo, el bus de preview, TAKE y T-bar -------------------
    {
        Tool t;
        t.name = "scene_switch";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Poner una escena al aire (corte directo)");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia la escena al aire");
        t.description = ba::i18n::traducir("mcp", "Pone una escena al aire YA, sin pasar por el preview: es «corta a la camara 2», "
                        "«pone la apertura». Cambia la escena del programa (con la transicion configurada). "
                        "Lo que se ve y oye en la "
                        "transmision cambia EN EL ACTO, sin pasar por el bus de preview.");
        t.details =
            ba::i18n::traducir("mcp", "Es el corte de urgencia: sale ya. Para lo demas, la forma de trabajar de una mesa es "
            "`scene_preview` (armar el proximo plano) -> mirarlo con `foto_fuente` -> `preview_al_aire`. "
            "Dos pasos parece mas lento y es al reves: lo que esta roto se ve ANTES de salir al aire, "
            "no despues.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Nombre de la escena.")}}}}},
            {"required", {"name"}}};
        t.examples = {R"({"name": "Entrevista"})"};
        t.run = [&core, m, existe, noExiste](const json& a, Done done) {
            const std::string name = a["name"];
            if (!existe(name)) return done(noExiste(name));
            if (const std::string e = m->poner(core.salidaDe(a), name); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Al aire: %1"), {name})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_preview";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Armar la proxima escena (sin sacarla al aire)");
        t.category = "escenas";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Pone una escena en el bus de PREVIEW: queda armada para el proximo `preview_al_aire`. NO cambia "
            "lo que se ve en la transmision.");
        t.details =
            ba::i18n::traducir("mcp", "Asi se opera una mesa de switcher: se arma el proximo plano, se MIRA (`foto_fuente` con "
            "el mismo nombre da la foto de como va a salir), y recien ahi `preview_al_aire`.\n\n"
            "Armar la escena que YA esta al aire no tiene sentido (el take cortaria a lo mismo) y se "
            "rechaza.\n\n"
            "`scene_list` dice cual esta armada y cual al aire.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}, {"required", {"name"}}};
        t.examples = {R"({"name": "06 Debate invitados"})"};
        t.run = [&core, m, existe, noExiste](const json& a, Done done) {
            const std::string name = a["name"];
            if (!existe(name)) return done(noExiste(name));
            if (const std::string e = m->armar(core.salidaDe(a), name); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se armo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Armada: %1. Mirala con foto_fuente y sacala con preview_al_aire."), {name})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "scene_cut";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "CUT: corte seco a la armada");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la armada al aire");
        t.description = ba::i18n::traducir("mcp", "Pone al aire la escena armada con un CORTE (sin transicion), aunque la global sea un fundido. "
                        "Deja armada la que salia.");
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            const std::string armada = m->armada(salida);
            if (armada.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay nada armado (scene_preview).")));
            if (const std::string e = m->take(salida, "cut", 0); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "AL AIRE (corte): %1"), {armada})));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "transicion_manual";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "T-bar: transicion a mano");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "lleva la transición a mano (T-bar)");
        t.description = ba::i18n::traducir("mcp", "Lleva la transicion hacia la escena armada a mano: `t` de 0 a 1 (la palanca). Con `soltar`, "
                        "termina: pasada la mitad queda al aire, si no vuelve. Para un fader de la botonera: "
                        "{\"t\": \"{{valor}}\"} con el fader de 0 a 1.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"t"}},
                         {"properties", {{"t", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}, {"soltar", {{"type", "boolean"}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            if (const std::string e = m->tbar(salida, a.value("t", 0.0), a.value("soltar", false)); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult().data({{"al_aire", m->alAire(salida)}, {"hacia", m->manualHacia(salida)}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "preview_al_aire";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "TAKE: sacar al aire la escena armada");
        t.category = "escenas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "pasa el preview al aire");
        t.description = ba::i18n::traducir("mcp", "Pasa al aire la escena que estaba ARMADA en preview, con la transición configurada. Deja armada la "
            "que salia, para poder volver de un golpe.");
        t.details = ba::i18n::traducir("mcp", "Sin nada armado no hace nada y lo dice: armar primero con `scene_preview`.\n\n"
                    "Que el plano que SALE del aire quede armado es como funciona cualquier mesa: "
                    "despues de un take, volver a lo anterior es un solo take mas.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"transicion", {{"type", "string"}, {"enum", {"cut", "fade", "swipe", "slide", "fade_to_color"}},
                                           {"description", ba::i18n::traducir("mcp", "otra transicion solo para este take (si no, la de entrada de la escena o la global)")}}},
                           {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            const std::string armada = m->armada(salida);
            if (armada.empty())
                return done(ToolResult::error(
                    ba::i18n::traducir("mcp", "No hay nada armado. Arma el proximo plano con scene_preview {\"name\": ...}.")));
            if (const std::string e = m->take(salida, a.value("transicion", std::string()), a.value("ms", -1)); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo: %1"), {e})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "AL AIRE: %1. Queda armada: %2."), {armada, m->armada(salida)})));
        };
        core.addTool(std::move(t));
    }

    // --- La transmisión -------------------------------------------------------------------
    {
        Tool t;
        t.name = "stream_start";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Salir al aire (transmitir)");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "arranca o cambia la transmisión");
        t.openWorld = true;
        t.description =
            ba::i18n::traducir("mcp", "Empieza a transmitir por RTMP. Sin argumentos usa el servidor configurado y la clave de la "
            "variable de entorno. La clave NUNCA se guarda en disco.");
        t.details = ba::i18n::traducir("mcp", "Si YA esta transmitiendo: al mismo destino no hace nada (lo dice); a OTRO destino da error "
                    "con `activo_en` (no cambia nada), salvo `cambiar_en_caliente`: true, que corta y arranca al "
                    "nuevo. `destino`: el id de un destino adicional (stream_destinos) o \"todos\": arranca esos, "
                    "ademas del principal (cada destino sale con el programa de SU salida). Con `salida` (otra que la por "
                    "omisión), su transmision principal: ahí `server` y `key` son obligatorios (la clave no se guarda).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"server", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "rtmp://... (opcional).")}}},
              {"key", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Clave de transmision (solo en memoria).")}}},
              {"cambiar_en_caliente", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "estando al aire, pasar a este destino")}}},
              {"destino", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "id de un destino adicional o \"todos\"")}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            if (a.contains("destino")) {
                const std::string e = m->iniciarDestino(a.value("destino", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().text(ba::i18n::traducir("mcp", "Destino iniciado.")).data(json{{"destinos", m->destinos()}}));
            }
            // La misma operación del modelo que el botón (el vivo queda en el bus y el histórico).
            const MandoDeAire::Inicio r = m->transmitir(salida, a.value("server", std::string()), a.value("key", std::string()),
                                                        a.value("cambiar_en_caliente", false));
            if (!r.ok) {
                if (!r.activoEn.empty()) {
                    ToolResult x = ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Ya esta transmitiendo a %1: NO se cambio nada. Para pasar a "
                                                                                       "este destino: `cambiar_en_caliente`: true "
                                                                                       "(corta un instante)."),
                                                                    {r.activoEn}));
                    x.structured = json{{"activo_en", r.activoEn}};
                    return done(x);
                }
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No arranco: %1. Si falta la clave: pasala en `key` o en la "
                                                                               "variable %2."),
                                                           {r.error, m->variableClave()})));
            }
            if (r.yaIgual) return done(ToolResult::ok(ba::i18n::traducir("mcp", "Ya estaba transmitiendo a ese destino: no cambio nada.")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Transmitiendo.")));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_stop";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Cortar la transmision");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la transmisión");
        t.destructive = true;
        t.description =
            ba::i18n::traducir("mcp", "BAJA del aire: corta la transmision que esta saliendo. Detiene la transmision en vivo. Con "
            "`destino` (id o \"todos\"), corta solo ese destino adicional y el principal sigue. Con `salida`, la "
            "transmision principal de esa salida (las demás siguen).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"destino", {{"type", "string"}}}}}};
        t.run = [&core, m](const json& a, Done done) {
            if (a.contains("destino")) {
                m->detenerDestino(a.value("destino", std::string()));
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Destino cortado; el resto sigue.")));
            }
            m->cortarTransmision(core.salidaDe(a));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Transmision detenida.")));
        };
        core.addTool(std::move(t));
    }

    // --- La grabación -----------------------------------------------------------------------
    {
        Tool t;
        t.name = "record_start";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Grabar");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "EMPEZAR a grabar el programa en disco, con la carpeta y el formato de la "
                        "configuracion. Es para cuando todavia no se esta grabando.");
        t.run = [&core, m](const json& a, Done done) {
            const std::string salida = core.salidaDe(a);
            if (m->grabacion(salida).value("activa", false))
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Ya esta grabando.")));
            const json d = m->disco();
            if (d.value("horas", -1.0) >= 0 && d.value("horas", -1.0) < m->discoMinimoH)
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Queda disco para menos de 15 minutos a este bitrate: no se arranca sola. "
                                              "Liberá espacio o que el operador lo arranque desde Salidas.")));
            if (const std::string e = m->grabar(salida); !e.empty())
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No arranco: %1"), {e})));
            // La grabación arranca en el hilo de salidas del motor (nadie espera el
            // codificador): se mira cada 200 ms desde ESTE hilo (el control, sin la
            // interfaz), sin trabar nada, hasta que tiene archivo.
            auto mirar = std::make_shared<std::function<void(int)>>();
            *mirar = [m, d, done, mirar, salida](int n) {
                const json g = m->grabacion(salida);
                const std::string archivo = g.value("archivo", std::string());
                if (g.value("activa", false) && !archivo.empty()) {
                    QTimer::singleShot(0, [mirar]() { *mirar = nullptr; });
                    return done(ToolResult()
                                    .text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Grabando en %1."), {archivo}))
                                    .data(json{{"archivo", archivo},
                                               {"contenedor_real", std::filesystem::path(archivo).extension().string()},
                                               {"contenedor_pedido", m->contenedor()},
                                               {"horas_restantes", d.value("horas", -1.0)}}));
                }
                if (n >= 25 || !m->algoSale()) {
                    QTimer::singleShot(0, [mirar]() { *mirar = nullptr; });
                    return done(m->algoSale()
                                    ? ToolResult::ok(ba::i18n::traducir("mcp", "Pedido: la grabación está arrancando (todavía sin archivo)."))
                                    : ToolResult::error(ba::i18n::traducir("mcp", "La grabación no arrancó: mirá el aviso en la interfaz.")));
                }
                QTimer::singleShot(200, [mirar, n]() {
                    if (*mirar) (*mirar)(n + 1);
                });
            };
            (*mirar)(0);
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_stop";
        t.porSalida = true;
        t.hilo = Hilo::Control;
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Detener grabacion");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "corta la grabación");
        t.description = ba::i18n::traducir("mcp", "PARA de grabar: cierra el archivo y deja de escribir en disco. Con `salida`, la grabación de esa salida.");
        t.run = [&core, m](const json& a, Done done) {
            m->pararGrabacion(core.salidaDe(a));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Grabacion detenida.")));
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
