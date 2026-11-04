// ============================================================================
// src/app/mcp/McpVivosTools.cpp — varias cuentas por plataforma y los vivos de cada salida
//
// docs/funciones/STREAMING-CUENTAS.md. Todo pasa por el modelo de la interfaz
// (ui/VivosViewModel → vivos::ServicioVivos): lo que hace el asistente lo ve el operador
// en el acto (la sección Cuentas de cada página, la tarjeta de cada salida).
//
//   cuentas_listar          las cuentas de cada plataforma con su estado
//   cuenta_agregar          una cuenta más (y, con `conectar`, abre la autorización)
//   cuenta_renombrar        el nombre que ve el operador
//   cuenta_quitar           revoca su token, lo borra y la saca (la principal no)
//   cuenta_conectar         conectar o reautorizar (vencida, sin permiso)
//   vivos_listar            los vivos programados o activos de una cuenta
//   vivo_crear              uno nuevo (y, con `salida`, asignado a esa salida)
//   salida_destino_asignar  (plataforma, cuenta, vivo) o URL + clave -> destino de una salida
//   salida_destinos_estado  cada destino: conectando, en vivo, reintentando, salud, bitrate
// Quitar un destino es stream_destino_borrar; arrancarlo, stream_start con `destino`.
// ============================================================================

#include <QJsonObject>
#include <QPointer>

#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/CuentasViewModel.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/VivosViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerVivosTools() {
    VivosViewModel* v = &vm_.cuentas().vivos();
    const json platProp = {{"type", "string"},
                           {"enum", {"youtube", "twitch", "facebook"}},
                           {"description", QT_TRANSLATE_NOOP("mcp", "La plataforma.")}};
    const json cuentaProp = {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "El id de la cuenta (cuentas_listar); sin ella, la principal.")}};
    // La respuesta de la plataforma -> resultado MCP.
    const auto responder = [](Done done) {
        return [done](const vivos::Resultado& r) {
            if (!r.ok) return done(ToolResult::error(r.error));
            done(ToolResult().data(r.datos));
        };
    };

    {
        Tool t;
        t.name = "cuentas_listar";
        t.title = ba::i18n::traducir("mcp", "Las cuentas de cada plataforma");
        t.category = "cuentas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las cuentas de YouTube, Twitch y Facebook del aparato (puede haber varias por plataforma; "
            "la «principal» es la de la página de la plataforma), con su `estado`: conectada, sin_conectar, conectando, "
            "vencida (hay que reautorizarla con cuenta_conectar), sin_permiso (reautorizar) o sin_app (falta la app de la "
            "plataforma: account_setup_guide). Nunca devuelve secretos. `plataforma` filtra.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"plataforma", platProp}}}};
        t.run = [v](const json& a, Done done) {
            done(ToolResult().data(json{{"cuentas", v->servicio().cuentas(a.value("plataforma", std::string()))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuenta_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar una cuenta de una plataforma");
        t.category = "cuentas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Suma otra cuenta de YouTube, Twitch o Facebook (otro canal, otra Página) con el `nombre` "
            "que verá el operador. Usa la misma app de la plataforma que la principal. Con `conectar: true` abre ya el "
            "navegador para que la PERSONA elija esa cuenta y autorice (después, cuentas_listar dice si quedó conectada). "
            "Devuelve su `id`.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma"}},
                         {"properties",
                          {{"plataforma", platProp},
                           {"nombre", {{"type", "string"}}},
                           {"conectar", {{"type", "boolean"}, {"default", false}}}}}};
        t.examples = {R"({"plataforma": "youtube", "nombre": "Canal en inglés", "conectar": true})"};
        t.run = [v](const json& a, Done done) {
            std::string id;
            const std::string p = a.value("plataforma", std::string());
            const std::string e = v->agregar(p, a.value("nombre", std::string()), &id);
            if (!e.empty()) return done(ToolResult::error(e));
            if (a.value("conectar", false)) v->conectar(p, id, [](const vivos::Resultado&) {});
            done(ToolResult().data(json{{"id", id}, {"cuentas", v->servicio().cuentas(p)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuenta_renombrar";
        t.title = ba::i18n::traducir("mcp", "Cambiar el nombre de una cuenta");
        t.category = "cuentas";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia el nombre con el que el operador ve una cuenta (no toca nada en la plataforma).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma", "cuenta", "nombre"}},
                         {"properties", {{"plataforma", platProp}, {"cuenta", cuentaProp}, {"nombre", {{"type", "string"}}}}}};
        t.run = [v](const json& a, Done done) {
            const std::string p = a.value("plataforma", std::string());
            const std::string e = v->renombrar(p, a.value("cuenta", std::string()), a.value("nombre", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"cuentas", v->servicio().cuentas(p)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuenta_quitar";
        t.title = ba::i18n::traducir("mcp", "Quitar una cuenta");
        t.category = "cuentas";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Revoca el acceso de esa cuenta, borra su token y la saca de la lista. La principal no se "
            "quita (se desconecta con <plataforma>_logout). Si algún destino de este evento la usa, no se quita: quitá o "
            "reasigná ese destino antes. Preguntale al operador.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma", "cuenta"}},
                         {"properties", {{"plataforma", platProp}, {"cuenta", cuentaProp}}}};
        t.run = [v](const json& a, Done done) {
            const std::string p = a.value("plataforma", std::string());
            const std::string e = v->quitar(p, a.value("cuenta", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"cuentas", v->servicio().cuentas(p)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cuenta_conectar";
        t.title = ba::i18n::traducir("mcp", "Conectar o reautorizar una cuenta");
        t.category = "cuentas";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Abre el navegador de esta máquina para que la PERSONA elija esa cuenta y la autorice "
            "(también para reautorizar una vencida o sin permiso). En Twitch se abre twitch.tv/activate con el código ya "
            "cargado. Con `wait: true` espera a que termine (máx. 5 min); si no, vuelve enseguida y cuentas_listar dice "
            "cuándo quedó conectada.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma"}},
                         {"properties",
                          {{"plataforma", platProp}, {"cuenta", cuentaProp}, {"wait", {{"type", "boolean"}, {"default", false}}}}}};
        t.run = [v](const json& a, Done done) {
            const std::string p = a.value("plataforma", std::string());
            const std::string c = a.value("cuenta", std::string());
            if (a.value("wait", false)) {
                v->conectar(p, c, [v, p, done](const vivos::Resultado& r) {
                    if (!r.ok) return done(ToolResult::error(r.error));
                    done(ToolResult().data(json{{"conectada", r.datos}, {"cuentas", v->servicio().cuentas(p)}}));
                });
                return;
            }
            // Sin esperar: el error que llega enseguida (sin app, sin cuenta) sí se contesta.
            auto contestado = std::make_shared<bool>(false);
            v->conectar(p, c, [contestado, done](const vivos::Resultado& r) {
                if (*contestado) return;
                *contestado = true;
                if (!r.ok) return done(ToolResult::error(r.error));
                done(ToolResult().data(json{{"conectada", r.datos}}));
            });
            if (*contestado) return;
            *contestado = true;
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Se abrió el navegador: la persona elige la cuenta y acepta. Después, cuentas_listar.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "vivos_listar";
        t.title = ba::i18n::traducir("mcp", "Los vivos de una cuenta");
        t.category = "cuentas";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los vivos programados o activos de una cuenta (YouTube: liveBroadcasts; Facebook: los "
            "live_videos de la Página que todavía reciben señal): id, titulo, estado, privacidad, inicio, url. En Twitch el "
            "vivo es el canal (uno solo, id vacío). El `id` es el que pide salida_destino_asignar.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma"}},
                         {"properties", {{"plataforma", platProp}, {"cuenta", cuentaProp}}}};
        t.run = [v, responder](const json& a, Done done) {
            v->listarVivos(a.value("plataforma", std::string()), a.value("cuenta", std::string()), responder(done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "vivo_crear";
        t.title = ba::i18n::traducir("mcp", "Crear un vivo en una cuenta");
        t.category = "cuentas";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea un vivo en la cuenta (YouTube: liveBroadcast + su stream, privado si no se dice; "
            "Facebook: un live_video en prueba, o público con privacidad «public»; Twitch: le pone el título al canal). "
            "No transmite nada. Con `salida` además lo asigna como destino de esa salida (como salida_destino_asignar).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"plataforma", "titulo"}},
                         {"properties",
                          {{"plataforma", platProp},
                           {"cuenta", cuentaProp},
                           {"titulo", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"privacidad", {{"type", "string"}, {"enum", {"private", "unlisted", "public"}}, {"default", "private"}}},
                           {"inicio", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "ISO 8601 (por omisión, ahora).")}}},
                           {"latencia", {{"type", "string"}, {"enum", {"normal", "low", "ultraLow"}}}},
                           {"salida", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id o nombre de la salida a la que asignarlo.")}}}}}};
        t.examples = {R"({"plataforma": "youtube", "cuenta": "cuenta-2", "titulo": "Sesión en inglés", "privacidad": "unlisted", "salida": "ingles"})"};
        t.run = [this, v](const json& a, Done done) {
            const std::string p = a.value("plataforma", std::string());
            const std::string c = a.value("cuenta", std::string());
            std::string salida;
            const bool asignar = a.contains("salida") && !a.value("salida", std::string()).empty();
            if (asignar) {
                std::string err;
                salida = core_.resolverSalida(a.value("salida", std::string()), err);
                if (!err.empty()) return done(ToolResult::error(err));
            }
            json datos = json::object();
            for (const char* k : {"titulo", "descripcion", "privacidad", "inicio", "latencia"})
                if (a.contains(k)) datos[k] = a[k];
            v->crearVivo(p, c, datos, [v, p, c, asignar, salida, done](const vivos::Resultado& r) {
                if (!r.ok) return done(ToolResult::error(r.error));
                const json vivo = r.datos.value("vivo", json::object());
                if (!asignar) return done(ToolResult().data(json{{"vivo", vivo}}));
                vivos::Asignacion x;
                x.salida = salida;
                x.plataforma = p;
                x.cuenta = c;
                x.vivo = vivo.value("id", std::string());
                x.vivoTitulo = vivo.value("titulo", std::string());
                v->asignar(std::move(x), [v, vivo, salida, done](const std::string& e, const std::string& id) {
                    if (!e.empty()) {
                        ToolResult res;
                        res.data(json{{"vivo", vivo}});
                        res.text(ba::i18n::llenar(ba::i18n::traducir("mcp", "El vivo se creó pero no se pudo asignar: %1"), {e}));
                        res.isError = true;
                        return done(res);
                    }
                    done(ToolResult().data(json{{"vivo", vivo}, {"destino", id}, {"destinos", v->estado(salida)}}));
                });
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_destino_asignar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Asignar un vivo o un destino a una salida");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.openWorld = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Un destino más de la salida (`salida`; sin ella, la de quien llama o la por omisión): el "
            "vivo `vivo` (vivos_listar) de la cuenta `cuenta` de `plataforma` (en Twitch, sin `vivo`: el canal), o uno "
            "genérico con `url` (rtmp://, rtmps://, srt://, rist://) y `clave` (va cifrada). Pide la ingesta a la "
            "plataforma (en YouTube enlaza un stream si el vivo no tiene) y queda guardado como los demás destinos (vuelve "
            "tras un corte de luz). No lo arranca: stream_start con `destino` (su id). Si otro destino (de esta u otra salida) ya manda "
            "a ese vivo, no se asigna y el error dice cuál. Con `id` reemplaza a ese destino.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"plataforma", {{"type", "string"}, {"enum", {"youtube", "twitch", "facebook", "rtmp", "srt", "rist"}}}},
                           {"cuenta", cuentaProp},
                           {"vivo", {{"type", "string"}}},
                           {"vivo_titulo", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"id", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "El destino a reemplazar (stream_destinos).")}}}}}};
        t.examples = {R"({"salida": "ingles", "plataforma": "youtube", "cuenta": "cuenta-2", "vivo": "AbCdEf12345"})",
                      R"({"salida": "principal", "plataforma": "twitch"})",
                      R"({"salida": "tv", "url": "srt://10.0.0.5:9000?mode=caller", "clave": "frase-secreta", "nombre": "Canal de TV"})"};
        t.run = [this, v](const json& a, Done done) {
            vivos::Asignacion x;
            x.salida = core_.salidaDe(a);
            x.id = a.value("id", std::string());
            x.plataforma = a.value("plataforma", std::string());
            x.cuenta = a.value("cuenta", std::string());
            x.vivo = a.value("vivo", std::string());
            x.vivoTitulo = a.value("vivo_titulo", std::string());
            x.nombre = a.value("nombre", std::string());
            x.url = a.value("url", std::string());
            x.clave = a.value("clave", std::string());
            x.respaldo = a.value("respaldo", false);
            const std::string salida = x.salida;
            v->asignar(std::move(x), [v, salida, done](const std::string& e, const std::string& id) {
                if (!e.empty()) return done(ToolResult::error(e));
                done(ToolResult().data(json{{"id", id}, {"destinos", v->estado(salida.empty() ? std::string("principal") : salida)}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_destinos_estado";
        t.title = ba::i18n::traducir("mcp", "Estado de cada destino");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cada destino (de todas las salidas, o de `salida`) con su plataforma, cuenta y vivo, su "
            "`estado` (detenido, conectando, en_vivo, reintentando), el bitrate que sale (`kbps`), la salud que informa la "
            "plataforma (`salud`: en YouTube estado_stream y salud good/ok/bad; espectadores en Twitch y Facebook) y "
            "`choque` si manda al mismo vivo que otro.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"salida", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id o nombre de la salida (salidas_listar).")}}}}}};
        t.run = [this, v](const json& a, Done done) {
            std::string salida;
            if (const std::string pedida = a.value("salida", std::string()); !pedida.empty()) {
                std::string err;
                salida = core_.resolverSalida(pedida, err);
                if (!err.empty()) return done(ToolResult::error(err));
                if (salida.empty()) salida = "principal";
            }
            done(ToolResult().data(json{{"destinos", v->estado(salida)}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
