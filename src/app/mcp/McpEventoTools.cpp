// ============================================================================
// src/app/mcp/McpEventoTools.cpp — el guion del evento, para el asistente
//
// Lo mismo que hace el operador en la pantalla del guion, pero para que lo
// pueda hacer la IA: listar y abrir eventos, armar bloques y cues, revisar que
// no falte nada y correr el guion.
//
// Por que existe: armar un guion de 40 cues a mano es el trabajo aburrido de
// un evento, y es justo lo que un asistente hace bien a partir de una
// descripcion ("acto por el 25 de mayo, 40 minutos, himno, dos discursos,
// video institucional, cierre"). Correrlo es otra cosa: eso lo decide el
// operador, y por eso `evento_correr` NO es de solo lectura.
//
// Cada herramienta trae en `details` para que sirve y como se usa, porque el
// asistente las descubre sin nadie que se las explique.
// ============================================================================
#include "mcp/McpEventoTools.hpp"
#include "arranque/Configuraciones.hpp"
#include "mcp/McpAsamblea.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/Evento.hpp"
#include "evento/Historico.hpp"
#include "evento/FotosConfig.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "net/AccesoRemoto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/HistoricoViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// El guion completo tal como lo ve la interfaz, para que el asistente vea lo
/// mismo que el operador (incluido lo que le falta a cada cue).
json guionDe(EventoViewModel& ev) {
    const QVariantMap g = ev.guion();
    return json::parse(QString(QJsonDocument(QJsonObject::fromVariantMap(g)).toJson()).toStdString(), nullptr,
                       false);
}

QVariantMap aVariantMap(const json& j) {
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
    return d.object().toVariantMap();
}

} // namespace

void registrarPadron(McpCore& core, EventoViewModel& ev, FichaDe fichaDe);

void registerEventoTools(McpCore& core, EventoViewModel& ev, StudioViewModel& vm, FichaDe fichaDe) {
    core.addCategory("evento", "Guion del evento",
                     "El storyboard: bloques, cues, tiempos previstos y el recorrido en vivo.");

    // --- Ver ------------------------------------------------------------------
    {
        Tool t;
        t.name = "guion_listar";
        t.antes = {"evento_listar"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Los guiones del evento abierto";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Los guiones guardados DENTRO del evento abierto, con su fecha, su duracion prevista y cuantos cues "
            "tiene cada uno. Los eventos (cada uno con sus escenas, medios, asamblea…) son eventos_listar.";
        t.details =
            "Un evento es el guion de lo que va a pasar: bloques (apertura, discurso, cierre) y dentro "
            "de cada uno los cues concretos (poner una escena, arrancar un tema, dar la palabra).\n\n"
            "Cada evento vive en su propia carpeta dentro de Eventos/, con su musica, sus videos y sus "
            "discursos adentro, para poder llevarselo entero a otra maquina.\n\n"
            "Usalo para saber sobre que evento trabajar antes de llamar a guion_abrir.\n\n"
            "Los eventos se ordenan en carpetas (`grupo`, p. ej. \"2026/Octubre\"); `carpeta` es la ruta "
            "completa y es lo que piden las otras herramientas. `estado` es el del historico: "
            "planificado, en_curso o terminado; `en_curso` marca el que recibe todo lo que pasa "
            "(evento_en_curso). Con `grupo` filtra por carpeta (incluye las de adentro).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"grupo", {{"type", "string"}, {"description", "Solo los de esta carpeta."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            const std::string grupo = a.value("grupo", std::string());
            json out = json::array();
            for (const QVariant& v : ev.eventos()) {
                const QVariantMap m = v.toMap();
                const std::string g = m["grupo"].toString().toStdString();
                if (!grupo.empty() && g != grupo && g.rfind(grupo + "/", 0) != 0) continue;
                const QVariantMap h = m["historico"].toMap();
                out.push_back(json{{"carpeta", m["carpeta"].toString().toStdString()},
                                   {"grupo", g},
                                   {"estado", h["estado"].toString().toStdString()},
                                   {"en_curso", m["enCurso"].toBool()},
                                   {"automatico", h["automatico"].toBool()},
                                   {"nombre", m["nombre"].toString().toStdString()},
                                   {"fecha", m["fecha"].toString().toStdString()},
                                   {"inicio", m["inicio"].toString().toStdString()},
                                   {"huso", m["huso"].toString().toStdString()},
                                   {"duracionSeg", m["duracionSeg"].toInt()},
                                   {"bloques", m["bloques"].toInt()},
                                   {"cues", m["cues"].toInt()}});
            }
            json carpetas = json::array();
            for (const QString& c : ev.carpetas()) carpetas.push_back(c.toStdString());
            done(ToolResult().data(json{{"eventos", out},
                                        {"carpetas", carpetas},
                                        {"abierto", ev.abierto().toStdString()},
                                        {"en_curso", ev.enCurso().value("carpeta").toString().toStdString()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_web";
        t.title = "La página web del evento en vivo";
        t.category = "evento";
        t.openWorld = true;
        t.description =
            "El sitio del evento abierto: una página por sección (bloque) del guion, una portada (antes de empezar) "
            "y un cierre, en la carpeta Web/ del evento (queda en el historial). La raíz de la dirección pública "
            "muestra la página de la sección que se está tratando y cambia sola cuando el guion avanza; los "
            "invitados entran por /invitados/. `accion`: ver | generar {sobrescribir} | leer {pagina} | escribir "
            "{pagina, html} | sitio {titulo, bajada, color, fondo, subdominio, barra, direccion_al_iniciar, vivos, participacion} | "
            "aviso {texto, nivel: info|urgente} (el cartel de arriba de la página; texto vacío lo saca) | "
            "direccion {subdominio?} (pasa la dirección pública a <subdominio>.a.pinggy.link con la API key; reinicia "
            "la sala).";
        t.details =
            "`pagina`: «portada», «cierre», el id, el nombre o el número de una sección, o un .html/.css propio. "
            "LA PÁGINA SE ARMA SOLA CON LOS DATOS DEL EVENTO: las plantillas del sistema no traen datos escritos; "
            "/evento/_/vivo.js pone el nombre, la fecha, las secciones, el programa, anterior/siguiente, quién habla "
            "y la gente conectada con lo que da /api/v1/evento, así que si cambia el guion la página se actualiza sola. "
            "Para personalizar, sumá contenido propio (de qué va cada sección, fotos, links) con `escribir` y dejá los "
            "atributos data-vivo (la lista completa está al principio de vivo.js; `leer` una página muestra cómo). "
            "`generar` no pisa lo editado salvo `sobrescribir`. "
            "Mantené `<script src=\"/evento/_/vivo.js\" defer></script>` y usá data-vivo=\"estado|seccion|siguiente|"
            "cuenta|transcurrido|agenda\" para lo que cambia en vivo (ver el comentario al final de cada página). "
            "Nunca pongas en la página instrucciones del operador ni datos privados: es pública. "
            "DÓNDE VERLO EN VIVO: el estado trae `vivos` [{plataforma, nombre, url, embed, enVivo}] (YouTube, Twitch, "
            "Facebook: salen solos de las cuentas conectadas; `sitio {vivos: {youtube: link o id, twitch: canal, facebook: "
            "link}}` los fija para este evento, \"\" los saca). En la página: data-vivo=\"reproductor\" (el vivo embebido) y "
            "data-vivo=\"vivos\" (botones); en JSON, GET /api/v1/evento/vivos. "
            "ASAMBLEA (si el guion tiene `asamblea: true`): el estado trae `asamblea` {cuerpo, presentes, total, "
            "integrantes [{nombre, banca, bloque, presente, lugar, habla}], bloques [{bloque, presentes, total}], "
            "recinto {distribucion, filas, columnas, bancas [{banca, fila, columna, nombre, bloque, presente, habla}]}, "
            "quorum, punto (el que se trata), votaciones (las oficiales)}. La plantilla dibuja el plano de bancas "
            "(data-vivo=\"recinto\") con el color de cada bloque: a los partidos les importa dónde se sienta cada uno. "
            "El subdominio por defecto sale del nombre del evento («Entrevista Pepito» → entrevista-pepito); con "
            "`direccion_al_iniciar` la dirección cambia sola al poner el evento en curso. Pasale al operador la "
            "dirección con qr_mostrar.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"accion"}},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"ver", "generar", "leer", "escribir", "sitio", "direccion", "aviso"}}}},
                           {"sobrescribir", {{"type", "boolean"}}},
                           {"pagina", {{"type", "string"}}},
                           {"html", {{"type", "string"}}},
                           {"titulo", {{"type", "string"}}},
                           {"bajada", {{"type", "string"}}},
                           {"color", {{"type", "string"}}},
                           {"fondo", {{"type", "string"}}},
                           {"subdominio", {{"type", "string"}}},
                           {"barra", {{"type", "boolean"}}},
                           {"direccion_al_iniciar", {{"type", "boolean"}}},
                           {"participacion", {{"type", "boolean"}, {"description", "la página ofrece preguntar y votar (/publico/)"}}},
                           {"texto", {{"type", "string"}}},
                           {"nivel", {{"type", "string"}, {"enum", {"info", "urgente"}}}},
                           {"vivos", {{"type", "object"},
                                      {"properties", {{"youtube", {{"type", "string"}}}, {"twitch", {{"type", "string"}}},
                                                      {"facebook", {{"type", "string"}}}}}}}}}};
        t.run = [&ev, &vm](const json& a, Done done) {
            const std::string acc = a.value("accion", std::string("ver"));
            if (ev.abierto().isEmpty()) return done(ToolResult::error("No hay ningún evento abierto: guion_abrir."));
            if (acc == "ver") return done(ToolResult().data(qvToJson(ev.web())));
            if (acc == "generar") {
                const QVariantMap r = ev.generarWeb(a.value("sobrescribir", false));
                if (r.contains("error")) return done(ToolResult::error(r.value("error").toString().toStdString()));
                return done(ToolResult().data(qvToJson(r)));
            }
            if (acc == "leer") {
                const QString h = ev.leerPaginaWeb(QString::fromStdString(a.value("pagina", std::string())));
                if (h.isEmpty()) return done(ToolResult::error("No existe esa página (¿generaste el sitio?)."));
                return done(ToolResult::ok(h.toStdString()));
            }
            if (acc == "escribir") {
                const QString e = ev.escribirPaginaWeb(QString::fromStdString(a.value("pagina", std::string())),
                                                       QString::fromStdString(a.value("html", std::string())));
                return done(e.isEmpty() ? ToolResult::ok("Página guardada: se ve en vivo.") : ToolResult::error(e.toStdString()));
            }
            if (acc == "sitio") {
                QVariantMap c;
                for (const char* k : {"titulo", "bajada", "color", "fondo", "subdominio"})
                    if (a.contains(k) && a[k].is_string()) c[k] = QString::fromStdString(a[k].get<std::string>());
                for (const char* k : {"barra", "direccion_al_iniciar", "participacion"})
                    if (a.contains(k) && a[k].is_boolean()) c[k] = a[k].get<bool>();
                if (a.contains("vivos") && a["vivos"].is_object()) {
                    QVariantMap v;
                    for (auto it = a["vivos"].begin(); it != a["vivos"].end(); ++it)
                        if (it.value().is_string()) v[QString::fromStdString(it.key())] = QString::fromStdString(it.value().get<std::string>());
                    c["vivos"] = v;
                }
                const QString e = ev.setWebSitio(c);
                return done(e.isEmpty() ? ToolResult().data(qvToJson(ev.web().value("sitio"))) : ToolResult::error(e.toStdString()));
            }
            if (acc == "aviso") {
                ev.setAvisoWeb(QString::fromStdString(a.value("texto", std::string())),
                               QString::fromStdString(a.value("nivel", std::string("info"))));
                return done(ToolResult::ok(a.value("texto", std::string()).empty() ? "Aviso sacado." : "Aviso publicado: se ve ya en la página."));
            }
            if (acc == "direccion") {
                AccesoRemoto* ar = vm.accesoRemoto();
                if (!ar || !ar->apiKeyGuardada())
                    return done(ToolResult::error("Falta la API key de pinggy (acceso_remoto_clave tipo api_key)."));
                if (vm.alAire())
                    return done(ToolResult::error("Se está transmitiendo o grabando: cambiar la dirección reinicia la sala. Esperá a que termine."));
                QString sub = QString::fromStdString(a.value("subdominio", std::string()));
                if (sub.isEmpty()) sub = ev.web().value("sitio").toMap().value("subdominio").toString();
                if (sub.isEmpty()) return done(ToolResult::error("El evento no tiene subdominio: evento_web sitio {subdominio}."));
                ar->configurarConApiKey(sub);
                return done(ToolResult::ok(("Pidiendo " + sub + ".a.pinggy.link a pinggy (la sala se reinicia). Mirá el "
                                            "resultado con acceso_remoto_estado.").toStdString()));
            }
            done(ToolResult::error("accion: ver, generar, leer, escribir, sitio o direccion"));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guion_ver";
        t.antes = {"evento_ver"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "El guion abierto";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "El guion completo del evento abierto: bloques con su hora prevista y sus cues, cada uno con "
            "tipo, duracion, si encadena solo y que le falta para poder ejecutarse.";
        t.details =
            "`falta` es lo importante: dice por que ese cue no se podria ejecutar (una escena sin nombre, "
            "un orador sin tiempo, un video sin archivo). Un guion sano tiene todos los `falta` vacios.\n\n"
            "`offsetSeg` de cada cue son los segundos desde el arranque del evento y `horaPrevista` la "
            "hora («HH:MM») a la que le toca, si el evento tiene `inicio`.\n\n"
            "`cuando` es el arranque como instante absoluto con desfasaje "
            "(\"2026-09-24T19:00:00-03:00\"): es lo unico que le sirve a alguien que esta en otro huso. "
            "Viene vacio si falta la fecha o la hora.\n\n"
            "Si no hay ningun evento abierto, `bloques` viene vacio: abri uno con guion_abrir.";
        t.run = [&ev](const json&, Done done) {
            done(ToolResult().data(json{{"abierto", ev.abierto().toStdString()},
                                        {"sinGuardar", ev.sucio()},
                                        {"husoDelAparato", EventoViewModel::husoDelAparato().toStdString()},
                                        {"guion", guionDe(ev)}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guion_abrir";
        t.antes = {"evento_abrir"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Abrir o crear un guion (dentro del evento abierto)";
        t.category = "evento";
        t.description = "Abre un guion guardado DENTRO del evento abierto (o crea otro guion ahí con `nombre`). NO "
                        "cambia de evento: para pasar a otro evento (sus escenas, mezclador, medios, asamblea) es "
                        "evento_cambiar, y para uno nuevo, evento_nuevo.";
        t.details =
            "Con `carpeta` abre uno que ya existe (el nombre exacto que devuelve guion_listar).\n"
            "Con `nombre` crea uno nuevo con ese titulo y lo deja abierto; si ya hay uno con ese nombre "
            "NO lo pisa, crea otro al lado.\n\n"
            "Lo que se este editando sin guardar se guarda antes de cambiar de evento.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"carpeta", {{"type", "string"}, {"description", "Un evento que ya existe."}}},
              {"nombre", {{"type", "string"}, {"description", "Crear uno nuevo con este titulo."}}},
              {"en_carpeta",
               {{"type", "string"}, {"description", "Al crear: en qué carpeta (\"2026/Octubre\"); se crea si no está."}}}}}};
        t.run = [&ev, &vm](const json& a, Done done) {
            if (a.contains("nombre")) {
                // Con el nombre de otro EVENTO casi seguro querían pasar a él, no un guion con ese título acá.
                const std::string n = a["nombre"].get<std::string>();
                if (const Configuraciones* cf = vm.configuraciones())
                    for (const json& e : cf->listar())
                        if (e.value("nombre", std::string()) == n)
                            return done(ToolResult::error(
                                "«" + n + "» es un evento, no un guion: para pasar a él, evento_cambiar (reinicia la "
                                "app). Para un guion con ese título acá adentro, ponele otro nombre."));
                const QString c = ev.crear(QString::fromStdString(n), QString::fromStdString(a.value("en_carpeta", std::string())));
                if (c.isEmpty()) return done(ToolResult::error("No se pudo crear el guion."));
                return done(ToolResult::ok("Guion creado y abierto (en este mismo evento): " + c.toStdString()));
            }
            const std::string c = a.value("carpeta", std::string());
            if (c.empty())
                return done(ToolResult::error("Pasa `carpeta` (uno que exista) o `nombre` (uno nuevo)."));
            if (!ev.abrir(QString::fromStdString(c)))
                return done(
                    ToolResult::error("No se pudo abrir '" + c + "' (guion_listar muestra los que hay)."));
            done(ToolResult::ok("Guion abierto: " + c));
        };
        core.addTool(std::move(t));
    }

    // --- El histórico: el evento en curso y las carpetas ----------------------
    {
        Tool t;
        t.name = "evento_en_curso";
        t.title = "El evento en curso";
        t.category = "evento";
        t.readOnly = true;
        t.description = "Qué evento está en curso: el que recibe todo lo que pasa (grabaciones, vivos, registro).";
        t.details =
            "No es lo mismo que el evento ABIERTO (el que se edita): se puede preparar el de la semana que viene "
            "con el de hoy en curso. Hay uno solo a la vez y sobrevive a un corte de luz.\n\n"
            "Si nadie eligió uno y el automático está prendido (`evento_automatico`), está en curso la jornada "
            "del día («Jornada del 07/10/2026», en la carpeta 2026/Octubre): nada queda sin evento. "
            "Correr un guion de verdad (evento_correr, no en ensayo) lo pone en curso solo.";
        t.run = [&ev](const json&, Done done) {
            const QVariantMap m = ev.enCurso();
            json j{{"evento_automatico", ev.eventoAutomatico()}};
            if (m.isEmpty()) {
                j["en_curso"] = nullptr;
            } else {
                j["en_curso"] = json{{"carpeta", m["carpeta"].toString().toStdString()},
                                     {"nombre", m["nombre"].toString().toStdString()},
                                     {"grupo", m["grupo"].toString().toStdString()},
                                     {"desde_ms", m["desdeMs"].toLongLong()},
                                     {"automatico", m["automatico"].toBool()}};
            }
            done(ToolResult().data(j));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_bitacora";
        t.title = "La bitácora de un evento";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Lo que pasó durante un evento, en orden: escenas al aire, grabación y transmisión, oradores, "
            "votaciones, cada herramienta del MCP que cambió algo, cada botón de la botonera, cada cue del guion, "
            "avisos y errores. Sin `evento`: el en curso.";
        t.details =
            "Sale de `bitacora.jsonl` en la carpeta del evento (una línea por hecho, se escribe mientras el evento "
            "está en curso y sobrevive a un corte de luz). Cada línea: `t` (ms, reloj de pared), `tipo` (bus | mcp "
            "| botonera | guion | aviso | evento), `origen` (el tema del bus, el componente) y `texto` o `valor`. "
            "Filtros: `tipos`, `texto` (busca en toda la línea), `desde`/`hasta` (ms o «HH:MM» del día del "
            "evento), `limite` (las últimas N, 200 de fábrica). Las claves y tokens van tapados.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"evento", {{"type", "string"}, {"description", "La carpeta del evento (guion_listar)."}}},
              {"tipos", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", {"bus", "mcp", "botonera", "guion", "aviso", "evento"}}}}}},
              {"texto", {{"type", "string"}}},
              {"desde", {{"type", {"integer", "string"}}}},
              {"hasta", {{"type", {"integer", "string"}}}},
              {"limite", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000}}}}}};
        t.examples = {R"({"tipos": ["bus"], "limite": 50})", R"({"texto": "votacion"})",
                      R"({"evento": "2026/Octubre/Sesion-ordinaria", "desde": "18:00", "hasta": "19:30"})"};
        t.run = [&ev](const json& a, Done done) {
            const std::string carpeta = a.value("evento", std::string());
            const auto archivo = ev.archivoBitacoraDe(carpeta);
            if (archivo.empty())
                return done(ToolResult::error(carpeta.empty() ? "no hay un evento en curso" : "no hay un evento «" + carpeta + "»"));
            // «HH:MM» es del día de la bitácora: se toma la fecha de la primera línea.
            const json primera = evento::Bitacora::leer(archivo, 0, 0, {}, {}, 0);
            const auto ms = [&](const char* k) -> std::int64_t {
                if (!a.contains(k)) return 0;
                if (a[k].is_number()) return a[k].get<std::int64_t>();
                const QTime h = QTime::fromString(QString::fromStdString(a.value(k, std::string())), "HH:mm");
                if (!h.isValid() || primera.empty()) return 0;
                QDateTime d = QDateTime::fromMSecsSinceEpoch(primera.front().value("t", std::int64_t{0}));
                d.setTime(h);
                return d.toMSecsSinceEpoch();
            };
            std::set<std::string> tipos;
            for (const auto& x : a.value("tipos", json::array()))
                if (x.is_string()) tipos.insert(x.get<std::string>());
            const json l = evento::Bitacora::leer(archivo, ms("desde"), ms("hasta"), tipos, a.value("texto", std::string()),
                                                  static_cast<std::size_t>(a.value("limite", 200)));
            json out = json::array();
            for (json x : l) {
                x["hora"] = QDateTime::fromMSecsSinceEpoch(x.value("t", std::int64_t{0})).toString("HH:mm:ss").toStdString();
                out.push_back(std::move(x));
            }
            done(ToolResult().data(json{{"evento", carpeta.empty() ? ev.enCurso().value("carpeta").toString().toStdString() : carpeta},
                                        {"lineas", out},
                                        {"total_en_archivo", primera.size()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_config";
        t.title = "La configuración de un evento";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Con qué configuración se hizo un evento: la foto de Config/ al entrar en curso (`inicio`), al terminar "
            "(`cierre`) o la última vista (`ultima`, de fábrica). Sin `archivo`: la lista de archivos; con `archivo`: "
            "su contenido. Con `comparar_con`: qué cambió (contra «actual» o contra otro evento).";
        t.details =
            "Las fotos están en <evento>/config/{inicio,cierre,ultima}.json y nombran cada contenido por su sha256 "
            "(Eventos/.objetos/<sha>.json, guardado una sola vez). Los secretos van tapados («tapado»). Los cambios "
            "durante el evento también quedan en la bitácora (evento_bitacora, tipo `config`), con el JSON Patch. "
            "`comparar_con`: \"actual\" (la configuración de ahora) o {evento, foto}. Volver a una configuración "
            "es otra herramienta (con aprobación).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"evento", {{"type", "string"}, {"description", "La carpeta (vacío = el en curso)."}}},
              {"foto", {{"type", "string"}, {"enum", {"inicio", "cierre", "ultima"}}}},
              {"archivo", {{"type", "string"}, {"description", "p. ej. escenas.json"}}},
              {"comparar_con", {{"description", "\"actual\" o {evento, foto}"}}}}}};
        t.examples = {R"({})", R"({"archivo": "escenas.json"})", R"({"foto": "inicio", "comparar_con": "actual"})",
                      R"({"evento": "2026/Octubre/Sesion-03", "comparar_con": {"evento": "2026/Octubre/Sesion-10", "foto": "inicio"}})"};
        t.run = [&ev](const json& a, Done done) {
            evento::FotosConfig fotos(ev.carpetaConfig(), ev.raizEventos() / ".objetos");
            const auto cargarFoto = [&](const std::string& carpeta, const std::string& cual, std::string& err) -> json {
                const auto dir = ev.carpetaEvento(carpeta);
                if (dir.empty()) {
                    err = carpeta.empty() ? "no hay un evento en curso" : "no hay un evento «" + carpeta + "»";
                    return nullptr;
                }
                json f = evento::FotosConfig::cargar(dir / "config" / ((cual.empty() ? "ultima" : cual) + ".json"));
                if (f.is_null()) err = "ese evento no tiene foto «" + (cual.empty() ? "ultima" : cual) + "»";
                return f;
            };
            std::string err;
            const std::string carpeta = a.value("evento", std::string());
            const json f = cargarFoto(carpeta, a.value("foto", std::string()), err);
            if (f.is_null()) return done(ToolResult::error(err));
            if (a.contains("comparar_con")) {
                json otra;
                if (a["comparar_con"].is_string() && a["comparar_con"].get<std::string>() == "actual") {
                    otra = fotos.foto(QDateTime::currentMSecsSinceEpoch());
                } else if (a["comparar_con"].is_object()) {
                    otra = cargarFoto(a["comparar_con"].value("evento", std::string()),
                                      a["comparar_con"].value("foto", std::string()), err);
                    if (otra.is_null()) return done(ToolResult::error(err));
                } else {
                    return done(ToolResult::error("`comparar_con`: \"actual\" o {evento, foto}"));
                }
                const json c = fotos.comparar(f, otra);
                return done(ToolResult().data(json{{"cambios", c}, {"archivos_distintos", c.size()}}));
            }
            const std::string archivo = a.value("archivo", std::string());
            const json archivos = f.value("archivos", json::object());
            if (!archivo.empty()) {
                if (!archivos.contains(archivo)) return done(ToolResult::error("la foto no tiene «" + archivo + "»"));
                return done(ToolResult().data(
                    json{{"archivo", archivo}, {"contenido", fotos.leer(archivos[archivo].value("sha", std::string()))}}));
            }
            json lista = json::array();
            for (auto it = archivos.begin(); it != archivos.end(); ++it)
                lista.push_back(json{{"archivo", it.key()}, {"bytes", it.value().value("bytes", 0)}});
            done(ToolResult().data(json{
                {"hora", QDateTime::fromMSecsSinceEpoch(f.value("t", 0LL)).toString("dd/MM/yyyy HH:mm:ss").toStdString()},
                {"archivos", lista}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_grabaciones";
        t.title = "Grabaciones, vivos y transcripciones de un evento";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Lo que produjo un evento: cada grabación (archivo, desde/hasta, tamaño y sus capítulos: qué escena "
            "estaba al aire en cada momento), cada vivo (plataforma, URL pública, estudio, caídas) y las "
            "transcripciones. Sin `evento`: el en curso.";
        t.details =
            "Con un evento en curso, la grabación va a <evento>/Grabaciones/ (y las de entradas y la señal limpia, "
            "a sus subcarpetas) y las transcripciones a <evento>/Transcripciones/. Los índices son "
            "grabaciones.json y transmisiones.json en la carpeta del evento. Los capítulos salen de la bitácora "
            "(`aire/escena`), en segundos desde el inicio del archivo. Las claves de transmisión nunca se guardan.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"evento", {{"type", "string"}, {"description", "La carpeta (vacío = el en curso)."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            const std::string carpeta = a.value("evento", std::string());
            const auto dir = ev.carpetaEvento(carpeta);
            if (dir.empty())
                return done(ToolResult::error(carpeta.empty() ? "no hay un evento en curso" : "no hay un evento «" + carpeta + "»"));
            const auto lista = [](const std::filesystem::path& p) {
                std::ifstream f(p);
                json j = f ? json::parse(f, nullptr, false) : json();
                return j.is_array() ? j : json::array();
            };
            const auto hora = [](std::int64_t ms) {
                return QDateTime::fromMSecsSinceEpoch(ms).toString("dd/MM HH:mm:ss").toStdString();
            };
            const json escenas = evento::Bitacora::leer(dir / "bitacora.jsonl", 0, 0, {"bus"}, "aire/escena", 0);
            json grab = json::array();
            for (json g : lista(dir / "grabaciones.json")) {
                const std::int64_t d = g.value("desde_ms", std::int64_t{0});
                const std::int64_t h = g.value("hasta_ms", std::int64_t{0});
                g["desde"] = hora(d);
                if (h != 0) g["hasta"] = hora(h), g["duracion_seg"] = (h - d) / 1000;
                else g["en_curso"] = true;
                json caps = json::array();
                std::string escenaAntes;
                for (const json& e : escenas) {
                    if (e.value("origen", std::string()) != "aire/escena") continue;
                    const std::int64_t t = e.value("t", std::int64_t{0});
                    const std::string esc = e.value("escena", std::string());
                    if (t <= d) { escenaAntes = esc; continue; }
                    if (h != 0 && t > h) break;
                    if (caps.empty() && !escenaAntes.empty()) caps.push_back(json{{"seg", 0}, {"escena", escenaAntes}});
                    caps.push_back(json{{"seg", (t - d) / 1000}, {"escena", esc}});
                }
                if (caps.empty() && !escenaAntes.empty()) caps.push_back(json{{"seg", 0}, {"escena", escenaAntes}});
                g["capitulos"] = caps;
                std::error_code ec;
                g["existe"] = std::filesystem::exists(g.value("archivo", std::string()), ec);
                grab.push_back(std::move(g));
            }
            json vivos = json::array();
            for (json v : lista(dir / "transmisiones.json")) {
                v["desde"] = hora(v.value("desde_ms", std::int64_t{0}));
                if (v.contains("hasta_ms")) v["hasta"] = hora(v.value("hasta_ms", std::int64_t{0}));
                vivos.push_back(std::move(v));
            }
            json textos = json::array();
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(dir / "Transcripciones", ec))
                if (e.is_regular_file()) textos.push_back(json{{"archivo", e.path().filename().string()}, {"bytes", e.file_size()}});
            done(ToolResult().data(json{{"carpeta", dir.string()}, {"grabaciones", grab}, {"transmisiones", vivos},
                                        {"transcripciones", textos}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_sellos";
        t.title = "Verificar que un evento no se tocó";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Compara la carpeta de un evento cerrado con su sellos.json (sha256 de cada archivo al cerrarlo): dice "
            "si está intacto, qué archivos cambiaron, cuáles faltan y cuáles se agregaron después.";
        t.details =
            "Al dejar de estar en curso, cada evento recibe asamblea/ (copia sin secretos de orden del día, quórum, "
            "votaciones, cuerpo, sesión remota…), registro/ (el pedazo de cada log entre su inicio y su fin) y, en "
            "segundo plano, sellos.json. Verificar relee todo, también las grabaciones: con gigas tarda.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"evento"}},
                         {"properties", {{"evento", {{"type", "string"}, {"description", "La carpeta (guion_listar)."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            const auto dir = ev.carpetaEvento(a.value("evento", std::string()));
            if (dir.empty()) return done(ToolResult::error("no hay un evento «" + a.value("evento", std::string()) + "»"));
            // Fuera del hilo de la interfaz: puede leer gigas.
            const bool enCurso = dir == ev.carpetaEvento();
            QThreadPool::globalInstance()->start([dir, done, enCurso]() {
                json r = evento::CierreEvento::verificar(dir);
                if (enCurso)
                    r["aviso"] = "está en curso: se sigue escribiendo (bitácora, ficha…), así que lo que cambió desde "
                                 "su último cierre es normal; los sellos valen cuando deje de estar en curso.";
                QMetaObject::invokeMethod(qApp, [r, done]() { done(ToolResult().data(r)); }, Qt::QueuedConnection);
            });
        };
        core.addTool(std::move(t));
    }
    // --- historico_*: el archivo de todos los eventos (página Histórico) -----------
    // (Sin registrar, sus herramientas caían en «Otras»: fuera del mapa del
    // asistente, de su juego y de las secciones de la Botonera.)
    core.addCategory("historico", "Histórico",
                     "El archivo de todos los eventos: buscar en sus bitácoras y volver a su configuración.");
    {
        Tool t;
        t.name = "historico_buscar";
        t.title = "Buscar en todos los eventos";
        t.category = "historico";
        t.readOnly = true;
        t.description =
            "Busca un texto en todo el histórico: la bitácora de cada evento (escenas, MCP, botonera, avisos…), las "
            "transcripciones, la asamblea (orden del día, votaciones), las actas, los discursos y los guiones. Lo "
            "más nuevo primero.";
        t.details = "Cada resultado: evento (carpeta), archivo, línea, el texto alrededor y, en la bitácora, la hora. "
                    "Para ver el contexto de una línea de la bitácora: evento_bitacora con `texto` o `desde`/`hasta`.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"texto"}},
                         {"properties",
                          {{"texto", {{"type", "string"}, {"minLength", 2}}},
                           {"limite", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}}};
        t.examples = {R"({"texto": "votación"})", R"({"texto": "sin señal", "limite": 50})"};
        t.run = [&ev](const json& a, Done done) {
            const auto raiz = ev.raizEventos();
            const std::string q = a.value("texto", std::string());
            const std::size_t lim = static_cast<std::size_t>(a.value("limite", 100));
            QThreadPool::globalInstance()->start([raiz, q, lim, done]() {
                json r = evento::Historico::buscar(raiz, q, lim);
                for (auto& x : r)
                    if (x.contains("t"))
                        x["hora"] = QDateTime::fromMSecsSinceEpoch(x["t"].get<std::int64_t>()).toString("dd/MM/yyyy HH:mm:ss").toStdString();
                QMetaObject::invokeMethod(qApp, [r, done]() { done(ToolResult().data(json{{"resultados", r}, {"cantidad", r.size()}})); },
                                          Qt::QueuedConnection);
            });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "historico_restaurar";
        t.title = "Volver a la configuración de un evento";
        t.category = "historico";
        t.description =
            "Prepara volver a la configuración que tenía un evento (toda, o solo algunos archivos de Config/), "
            "para aplicarla al reiniciar la app. Con `cancelar`: descarta lo preparado. Sin argumentos: dice si hay "
            "algo preparado.";
        t.details =
            "No escribe Config/ con la app andando (cada módulo guardaría encima lo que tiene en memoria): deja todo "
            "en Config/.restaurar/ y se aplica al arrancar, antes de que se lea nada; lo de antes queda copiado en "
            "Config/.restaurar/antes-<ms>/. Las claves y tokens, que en las fotos van tapados, toman el valor de "
            "ahora. launcher.json no se toca. Mirá antes qué cambia con evento_config `comparar_con: \"actual\"`.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"evento", {{"type", "string"}}},
                           {"foto", {{"type", "string"}, {"enum", {"inicio", "cierre", "ultima"}}}},
                           {"archivos", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"cancelar", {{"type", "boolean"}}}}}};
        t.examples = {R"({"evento": "2026/Octubre/Sesion-03", "foto": "inicio"})",
                      R"({"evento": "2026/Octubre/Sesion-03", "archivos": ["escenas.json", "audio-racks.json"]})",
                      R"({"cancelar": true})", R"({})"};
        t.run = [&vm](const json& a, Done done) {
            auto* h = vm.historico();
            if (h == nullptr) return done(ToolResult::error("el histórico no está disponible"));
            if (a.value("cancelar", false)) {
                h->cancelarRestauracion();
                return done(ToolResult::ok("Descartado: no se va a cambiar nada al reiniciar."));
            }
            if (!a.contains("evento")) {
                const QVariantMap r = h->restauracion();
                if (r.isEmpty()) return done(ToolResult::ok("No hay nada preparado para restaurar."));
                return done(ToolResult().data(json::parse(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson().toStdString())));
            }
            QStringList l;
            for (const auto& x : a.value("archivos", json::array()))
                if (x.is_string()) l << QString::fromStdString(x.get<std::string>());
            const QString e = h->prepararRestauracion(QString::fromStdString(a.value("evento", std::string())),
                                                      QString::fromStdString(a.value("foto", std::string("inicio"))), l);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            const QVariantMap r = h->restauracion();
            done(ToolResult()
                     .text("Preparado: se aplica la próxima vez que arranque la app (cerrarla con app_quit y abrirla).")
                     .data(json::parse(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson().toStdString())));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "historico_traer_del_dia";
        t.title = "Traer las grabaciones viejas de un día a un evento";
        t.category = "historico";
        t.description =
            "Le asigna a un evento las grabaciones (Recordings/, con entradas/ y limpia/) y las transcripciones que "
            "terminaron en su día (el de su inicio, su fecha o el de la jornada), o entre `desde` y `hasta`. De "
            "fábrica quedan donde están y el evento las nombra; con `mover`, pasan a su carpeta.";
        t.details = "Para lo grabado antes del histórico (o con la jornada automática apagada). Lo que el evento ya "
                    "tiene no se repite. Las transcripciones se copian (son chicas), o se mueven con `mover`. "
                    "`desde`/`hasta`: ms de reloj de pared.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"evento"}},
                         {"properties",
                          {{"evento", {{"type", "string"}}},
                           {"mover", {{"type", "boolean"}}},
                           {"desde", {{"type", "integer"}}},
                           {"hasta", {{"type", "integer"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* h = vm.historico();
            if (h == nullptr) return done(ToolResult::error("el histórico no está disponible"));
            const json r = h->traerDelDiaJson(a.value("evento", std::string()), a.value("mover", false),
                                              a.value("desde", std::int64_t{0}), a.value("hasta", std::int64_t{0}));
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "historico_retencion";
        t.title = "Retención de grabaciones y disco";
        t.category = "historico";
        t.description =
            "Cuánto disco queda para los eventos y qué grabaciones borra la retención (las de eventos terminados "
            "hace más de `grabaciones_dias`; la bitácora, la configuración, la asamblea, las transcripciones y los "
            "índices quedan). De fábrica SIMULA: dice qué borraría. Con `simular: false`, borra.";
        t.details = "Se configura con historico_ajustes {retencion: {grabaciones_dias, aviso_disco_pct}}; 0 días = "
                    "nunca borrar (de fábrica). La app la aplica sola cada hora y avisa cuando el disco pasa del "
                    "porcentaje. Lo borrado queda anotado en grabaciones.json (`borrada_ms`) y en la bitácora del "
                    "evento; sus sellos van a decir que faltan esos archivos.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"simular", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto* h = vm.historico();
            if (h == nullptr) return done(ToolResult::error("el histórico no está disponible"));
            json r = h->retencion(a.value("simular", true));
            r["disco"] = h->disco();
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "historico_exportar";
        t.title = "Exportar un evento";
        t.category = "historico";
        t.description =
            "Copia un evento entero a otra carpeta (de fábrica, Exportados/ al lado de Eventos/), con o sin las "
            "grabaciones, junto con los contenidos de sus fotos de configuración: la copia se entiende sola en "
            "otra máquina.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"evento"}},
                         {"properties",
                          {{"evento", {{"type", "string"}}},
                           {"destino", {{"type", "string"}, {"description", "Carpeta absoluta (vacío = Exportados/)."}}},
                           {"grabaciones", {{"type", "boolean"}, {"description", "Incluir Grabaciones/ (de fábrica, sí)."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            const auto dir = ev.carpetaEvento(a.value("evento", std::string()));
            if (dir.empty()) return done(ToolResult::error("no hay un evento «" + a.value("evento", std::string()) + "»"));
            const std::string d = a.value("destino", std::string());
            const std::filesystem::path destino = d.empty() ? ev.raizEventos().parent_path() / "Exportados" : std::filesystem::path(d);
            const auto objetos = ev.raizEventos() / ".objetos";
            const bool con = a.value("grabaciones", true);
            QThreadPool::globalInstance()->start([dir, destino, objetos, con, done]() {
                std::filesystem::path copia;
                const std::string e = evento::Historico::exportar(dir, objetos, destino, con, &copia);
                QMetaObject::invokeMethod(qApp, [e, copia, done]() {
                    if (!e.empty()) return done(ToolResult::error(e));
                    done(ToolResult().text("Copiado en " + copia.string()).data(json{{"copia", copia.string()}}));
                }, Qt::QueuedConnection);
            });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "historico_ajustes";
        t.title = "Qué se registra en el histórico";
        t.category = "historico";
        t.description =
            "Qué entra en la bitácora de cada evento y la jornada automática. Sin argumentos: cómo está. "
            "`registrar`: {bus, mcp, botonera, guion, aviso, evento, config} en true/false; `bus_excluir`: "
            "patrones del bus que no se anotan (lo que se repite muchas veces por segundo); `evento_automatico`; "
            "`retencion`: {grabaciones_dias (0 = nunca borrar), aviso_disco_pct} (ver historico_retencion).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"registrar", {{"type", "object"}}},
                           {"bus_excluir", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"evento_automatico", {{"type", "boolean"}}},
                           {"retencion", {{"type", "object"}}}}}};
        t.examples = {R"({})", R"({"registrar": {"aviso": false}})", R"({"retencion": {"grabaciones_dias": 90, "aviso_disco_pct": 85}})", R"({"bus_excluir": ["audio/nivel/#", "audio/loudness"]})"};
        t.run = [&vm](const json& a, Done done) {
            auto* h = vm.historico();
            if (h == nullptr) return done(ToolResult::error("el histórico no está disponible"));
            if (!a.empty()) {
                const std::string e = h->guardarAjustesJson(a);
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data(h->ajustesJson()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_iniciar";
        t.title = "Poner un evento en curso";
        t.category = "evento";
        t.description = "Pone un evento en curso (el que estaba se termina): desde ahora todo lo que pasa es de ese evento.";
        t.details =
            "`carpeta` es la ruta que da guion_listar. Si el evento ya había empezado antes, conserva cuándo "
            "empezó. Para dejar de registrar en uno, evento_terminar.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"carpeta", {{"type", "string"}, {"description", "El evento."}}}}},
                         {"required", {"carpeta"}}};
        t.run = [&ev](const json& a, Done done) {
            const std::string c = a.value("carpeta", std::string());
            if (!ev.iniciarEvento(QString::fromStdString(c)))
                return done(ToolResult::error("No se pudo poner en curso '" + c + "' (guion_listar muestra los que hay)."));
            done(ToolResult::ok("En curso: " + c));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_terminar";
        t.destructive = true;
        t.title = "Terminar el evento en curso";
        t.category = "evento";
        t.description = "Termina el evento en curso (queda en el histórico como terminado).";
        t.details =
            "Con el automático prendido, enseguida queda en curso la jornada del día: lo que pase después no se "
            "pierde, va ahí. Con `automatico` (true/false) además se prende o apaga eso.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"automatico", {{"type", "boolean"}, {"description", "Prender o apagar el evento automático del día."}}},
              {"solo_ajuste", {{"type", "boolean"}, {"description", "Solo cambiar `automatico`, sin terminar nada."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("automatico") && a["automatico"].is_boolean()) ev.setEventoAutomatico(a["automatico"].get<bool>());
            if (a.value("solo_ajuste", false))
                return done(ToolResult::ok(std::string("Evento automático del día: ") +
                                           (ev.eventoAutomatico() ? "prendido" : "apagado")));
            const std::string antes = ev.enCurso().value("carpeta").toString().toStdString();
            if (antes.empty() || !ev.terminarEvento()) return done(ToolResult::error("No había ningún evento en curso."));
            const std::string ahora = ev.enCurso().value("carpeta").toString().toStdString();
            if (ahora == antes)
                return done(ToolResult::ok("Se cerró " + antes + " y el evento automático lo volvió a abrir (es la "
                                           "jornada de hoy). Para cerrarla del todo: evento_terminar con "
                                           "`automatico`: false."));
            done(ToolResult::ok("Terminado: " + antes + (ahora.empty() ? std::string() : ". En curso ahora: " + ahora)));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guion_carpeta";
        t.antes = {"evento_carpeta"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Ordenar los guiones en carpetas";
        t.category = "evento";
        t.description = "Crea una carpeta para agrupar guiones (dentro del evento abierto), o mueve un guion a otra carpeta.";
        t.details =
            "Las carpetas son carpetas de verdad, anidadas con «/» (\"2026/Octubre\", "
            "\"Consejo/Ordinarias\"). Con `crear` crea una; con `guion` y `a` lleva ese guion a la carpeta "
            "`a` (se crea si no está; \"\" es la raíz). Mover no cambia nada del guion, ni si está en curso.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"crear", {{"type", "string"}, {"description", "Carpeta a crear."}}},
              {"guion", {{"type", "string"}, {"description", "Guion a mover (ruta de guion_listar)."}}},
              {"a", {{"type", "string"}, {"description", "Carpeta destino."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("crear")) {
                const QString c = ev.crearCarpeta(QString::fromStdString(a.value("crear", std::string())));
                if (c.isEmpty()) return done(ToolResult::error("No se pudo crear la carpeta."));
                return done(ToolResult::ok("Carpeta: " + c.toStdString()));
            }
            // `evento` es el nombre de antes del parametro (evento_carpeta): sigue andando
            const std::string e = a.value("guion", a.value("evento", std::string()));
            if (e.empty() || !a.contains("a")) return done(ToolResult::error("Pasá `crear`, o `guion` y `a`."));
            const QString n = ev.moverEvento(QString::fromStdString(e), QString::fromStdString(a.value("a", std::string())));
            if (n.isEmpty()) return done(ToolResult::error("No se pudo mover '" + e + "'."));
            done(ToolResult::ok("Movido: " + n.toStdString()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_ficha";
        t.title = "La ficha de un evento";
        t.category = "evento";
        t.description = "Lee o cambia la ficha del histórico de un evento: estado, cuándo empezó y terminó, lugar, etiquetas y notas.";
        t.details =
            "Sin campos, la lee. Con `lugar`, `notas` o `etiquetas` (lista) los cambia; el estado y las horas "
            "los pone el sistema (evento_iniciar / evento_terminar). Las etiquetas sirven para cruzar eventos "
            "de distintas carpetas («con votación», «con invitados remotos»).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"carpeta", {{"type", "string"}, {"description", "El evento (vacío = el en curso)."}}},
              {"lugar", {{"type", "string"}}},
              {"notas", {{"type", "string"}}},
              {"etiquetas", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}};
        t.run = [&ev](const json& a, Done done) {
            std::string c = a.value("carpeta", std::string());
            if (c.empty()) c = ev.enCurso().value("carpeta").toString().toStdString();
            if (c.empty()) return done(ToolResult::error("No hay evento en curso: pasá `carpeta`."));
            QVariantMap campos;
            if (a.contains("lugar")) campos["lugar"] = QString::fromStdString(a.value("lugar", std::string()));
            if (a.contains("notas")) campos["notas"] = QString::fromStdString(a.value("notas", std::string()));
            if (a.contains("etiquetas") && a["etiquetas"].is_array()) {
                QStringList l;
                for (const auto& e : a["etiquetas"])
                    if (e.is_string()) l << QString::fromStdString(e.get<std::string>());
                campos["etiquetas"] = l;
            }
            if (!campos.isEmpty() && !ev.cambiarDatos(QString::fromStdString(c), campos))
                return done(ToolResult::error("No se pudo guardar la ficha de '" + c + "'."));
            const QVariantMap m = ev.datosDe(QString::fromStdString(c));
            json et = json::array();
            for (const QString& e : m["etiquetas"].toStringList()) et.push_back(e.toStdString());
            done(ToolResult().data(json{{"carpeta", c},
                                        {"estado", m["estado"].toString().toStdString()},
                                        {"iniciado_ms", m["iniciadoMs"].toLongLong()},
                                        {"terminado_ms", m["terminadoMs"].toLongLong()},
                                        {"lugar", m["lugar"].toString().toStdString()},
                                        {"notas", m["notas"].toString().toStdString()},
                                        {"etiquetas", et},
                                        {"automatico", m["automatico"].toBool()}}));
        };
        core.addTool(std::move(t));
    }

    // --- Armar ----------------------------------------------------------------
    {
        Tool t;
        t.name = "guion_bloque";
        t.antes = {"evento_bloque"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Agregar o cambiar un bloque";
        t.category = "evento";
        t.description = "Agrega un bloque al final del guion, lo renombra, lo mueve o lo saca.";
        t.details =
            "Un bloque es un tramo con nombre propio: «Apertura», «Palabras del intendente», «Cierre». "
            "Sirve para leer el guion de un vistazo y para que los tiempos se sumen por tramo.\n\n"
            "Sin `indice` agrega uno nuevo al final. Con `indice` (base 0) actua sobre ese: `nombre` lo "
            "renombra, `mover` lo cambia de lugar y `quitar` lo saca con todos sus cues.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"indice", {{"type", "integer"}, {"minimum", 0}, {"description", "Cual bloque (base 0)."}}},
              {"nombre", {{"type", "string"}}},
              {"mover", {{"type", "integer"}, {"minimum", 0}, {"description", "Nueva posicion."}}},
              {"quitar", {{"type", "boolean"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (!a.contains("indice")) {
                ev.agregarBloque(QString::fromStdString(a.value("nombre", std::string("Bloque nuevo"))));
                return done(ToolResult::ok("Bloque agregado al final."));
            }
            const int i = a["indice"].get<int>();
            if (a.value("quitar", false)) {
                ev.quitarBloque(i);
                return done(ToolResult::ok("Bloque quitado."));
            }
            if (a.contains("nombre")) ev.renombrarBloque(i, QString::fromStdString(a["nombre"]));
            if (a.contains("mover")) ev.moverBloque(i, a["mover"].get<int>());
            done(ToolResult::ok("Bloque actualizado."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guion_cue";
        t.antes = {"evento_cue"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Agregar o cambiar un cue";
        t.category = "evento";
        t.description = "Agrega un cue a un bloque, le cambia los datos, lo mueve de lugar o lo saca.";
        t.details =
            "Un cue es una cosa concreta que pasa. Los tipos y lo que necesita cada uno en `datos`:\n\n"
            "  escena        {escena}              pone esa escena al aire\n"
            "  musica        {archivo, accion, repetir}  accion: arrancar (por defecto) o parar;\n"
            "                                        el archivo: uno de los medios del evento (media_list)\n"
            "  video         {archivo, accion, repetir, alAire}\n"
            "                                        el video va en SU escena («Video del evento»),\n"
            "                                        que se crea sola la primera vez y queda\n"
            "                                        protegida. El operador puede componerla; el\n"
            "                                        guion solo le cambia el archivo. `alAire` en\n"
            "                                        false lo deja cargado sin cortar a esa escena.\n"
            "  teleprompter  {discurso}            un archivo de Discursos/\n"
            "  orador        {participante}        ademas necesita duracionSeg > 0\n"
            "  grafico       {fuente, grafico, valores, seVa, duracionMs, repetir, accion}\n"
            "                                        `fuente` es el nombre de la fuente «Grafico\n"
            "                                        animado» de la escena (DONDE se ve); `grafico` es\n"
            "                                        cual de los guardados; `valores` son sus parametros\n"
            "                                        {clave: texto} (los que no pongas quedan en el valor\n"
            "                                        por defecto). OJO: `valores` y `grafico`, NO\n"
            "                                        `parametros`/`nombre` como en grafico_mostrar.\n"
            "                                        CUÁNDO SE VA: `seVa` = al-avanzar (sale con su\n"
            "                                        animación cuando el guion pasa a otra cue: títulos\n"
            "                                        y placas), sola (a los `salidaEn` ms, DENTRO de\n"
            "                                        `valores`) o queda (hasta un cue `ocultar`: un\n"
            "                                        marcador, un zócalo fijo). Sin `seVa`: sola si trae\n"
            "                                        salidaEn > 0; si no, lo que diga la ficha del\n"
            "                                        gráfico (`seVa`; placas y títulos: al-avanzar).\n"
            "                                        `duracionMs` no hace falta. Y `duracionSeg` del cue\n"
            "                                        es solo para los tiempos del guion, no del gráfico.\n"
            "                                        accion: mostrar (por defecto), ocultar, o\n"
            "                                        prender/apagar un VINCULO (un rotulo que se\n"
            "                                        mantiene solo): en ese caso lleva {vinculo} con\n"
            "                                        su id y no hace falta fuente ni grafico.\n"
            "                                        Sirve para «durante las entrevistas, zocalo\n"
            "                                        automatico; en el cierre, apagado».\n"
            "  pantalla      {pantalla, contenido}\n"
            "  audio         {accion, fuente, db}    accion: silenciar | activar | volumen\n"
            "  pausa         -                     espera lo que diga duracionSeg\n"
            "  espera        -                     frena hasta que el operador confirme\n"
            "  nota          -                     no hace nada: es una indicacion\n\n"
            "`texto` es lo que el operador lee cuando toca ese cue: escribilo siempre, aunque el tipo no "
            "lo exija.\n\n"
            "`automatico` encadena con el siguiente sin esperar el clic. Usalo con cuidado: en vivo, que "
            "algo salga solo cuando no lo esperabas es peor que llegar tarde. La revision avisa si hay "
            "seis automaticos seguidos.\n\n"
            "Sin `cue` agrega uno nuevo al final del bloque. Con `cue` actua sobre ese.\n\n"
            "Ojo con que se EJECUTA hoy: escena, orador, musica, teleprompter, grafico, video y audio si. "
            "Pantalla todavia no: se puede dejar escrito en el guion (sirve para los tiempos y para "
            "que el operador sepa que hacer), pero cuando toquen se anotan como «todavia no se "
            "ejecutan» en vez de fallar callados.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"bloque", {{"type", "integer"}, {"minimum", 0}}},
              {"cue",
               {{"type", "integer"}, {"minimum", 0}, {"description", "Cual cue del bloque (base 0)."}}},
              {"tipo",
               {{"type", "string"},
                {"enum",
                 {"escena", "musica", "video", "teleprompter", "orador", "grafico", "pantalla", "audio",
                  "pausa", "espera", "nota"}}}},
              {"texto", {{"type", "string"}, {"description", "Lo que lee el operador."}}},
              {"datos", {{"type", "object"}, {"description", "Lo propio del tipo (ver la ayuda)."}}},
              {"duracionSeg", {{"type", "integer"}, {"minimum", 0}}},
              {"automatico", {{"type", "boolean"}}},
              {"moverABloque", {{"type", "integer"}, {"minimum", 0}}},
              {"moverACue", {{"type", "integer"}, {"minimum", 0}}},
              {"quitar", {{"type", "boolean"}}}}},
            {"required", {"bloque"}}};
        t.run = [&ev](const json& a, Done done) {
            const int b = a["bloque"].get<int>();
            // LO QUE EL TIPO NO USA, SE DICE (antes se guardaba callado): el asistente mandó
            // los textos de los gráficos como `params`/`parametros` (así se llaman en
            // grafico_mostrar), nadie los leía y todos salieron con el texto por defecto.
            const auto revisarDatos = [&ev, &a, b]() -> std::string {
                if (!a.contains("datos") || !a["datos"].is_object()) return {};
                std::string tipo = a.value("tipo", std::string());
                if (tipo.empty() && a.contains("cue")) {
                    const QVariantList bs = ev.guion()["bloques"].toList();
                    const int c = a["cue"].get<int>();
                    if (b >= 0 && b < bs.size()) {
                        const QVariantList cs = bs[b].toMap()["cues"].toList();
                        if (c >= 0 && c < cs.size()) tipo = cs[c].toMap()["tipo"].toString().toStdString();
                    }
                }
                static const std::map<std::string, std::set<std::string>> kCampos{
                    {"escena", {"escena"}},
                    {"musica", {"archivo", "accion", "repetir"}},
                    {"video", {"archivo", "accion", "repetir", "alAire"}},
                    {"teleprompter", {"discurso", "arrancar"}},
                    {"orador", {"participante"}},
                    {"grafico", {"fuente", "grafico", "valores", "seVa", "duracionMs", "repetir", "accion", "vinculo"}},
                    {"pantalla", {"pantalla", "contenido", "tipo", "pie"}},
                    {"audio", {"accion", "fuente", "db"}},
                    {"pausa", {}}, {"espera", {}}, {"nota", {}}};
                const auto it = kCampos.find(tipo);
                if (it == kCampos.end()) return {};
                std::string corregidos, fuera;
                for (const auto& [k, v] : a["datos"].items()) {
                    if (it->second.count(k)) continue;
                    if (tipo == "grafico" && (k == "params" || k == "parametros")) corregidos += " «" + k + "»→«valores»";
                    else if (tipo == "grafico" && k == "nombre") corregidos += " «nombre»→«grafico»";
                    else fuera += (fuera.empty() ? "" : ", ") + std::string("«") + k + "»";
                }
                std::string aviso;
                if (!corregidos.empty())
                    aviso += " Ojo: en un cue de " + tipo + " se tomó" + corregidos +
                             " (los campos del gráfico van en `valores`, no como en grafico_mostrar).";
                if (!fuera.empty()) {
                    std::string acepta;
                    for (const auto& k : it->second) acepta += (acepta.empty() ? "" : ", ") + k;
                    aviso += " Ojo: un cue de " + tipo + " no usa " + fuera + " (se guardó igual, pero no hace nada). " +
                             "Usa: " + (acepta.empty() ? std::string("nada en datos") : acepta) + ".";
                }
                return aviso;
            };
            const std::string aviso = revisarDatos();
            if (!a.contains("cue")) {
                const QString id =
                    ev.agregarCue(b, QString::fromStdString(a.value("tipo", std::string("nota"))));
                if (id.isEmpty())
                    return done(ToolResult::error("No existe el bloque " + std::to_string(b) + "."));
                // Recien creado: es el ultimo del bloque.
                const QVariantMap g = ev.guion();
                const QVariantList bs = g["bloques"].toList();
                const int ultimo = bs[b].toMap()["cues"].toList().size() - 1;
                QVariantMap campos;
                if (a.contains("texto")) campos["texto"] = QString::fromStdString(a["texto"]);
                if (a.contains("datos")) campos["datos"] = aVariantMap(a["datos"]);
                if (a.contains("duracionSeg")) campos["duracionSeg"] = a["duracionSeg"].get<int>();
                if (a.contains("automatico")) campos["automatico"] = a["automatico"].get<bool>();
                if (!campos.isEmpty()) ev.editarCue(b, ultimo, campos);
                return done(ToolResult::ok("Cue agregado en el bloque " + std::to_string(b) + "." + aviso));
            }
            const int c = a["cue"].get<int>();
            if (a.value("quitar", false)) {
                ev.quitarCue(b, c);
                return done(ToolResult::ok("Cue quitado."));
            }
            if (a.contains("moverABloque") || a.contains("moverACue")) {
                ev.moverCue(b, c, a.value("moverABloque", b), a.value("moverACue", c));
                return done(ToolResult::ok("Cue movido."));
            }
            QVariantMap campos;
            if (a.contains("tipo")) campos["tipo"] = QString::fromStdString(a["tipo"]);
            if (a.contains("texto")) campos["texto"] = QString::fromStdString(a["texto"]);
            if (a.contains("datos")) campos["datos"] = aVariantMap(a["datos"]);
            if (a.contains("duracionSeg")) campos["duracionSeg"] = a["duracionSeg"].get<int>();
            if (a.contains("automatico")) campos["automatico"] = a["automatico"].get<bool>();
            ev.editarCue(b, c, campos);
            done(ToolResult::ok("Cue actualizado." + aviso));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "guion_guardar";
        t.antes = {"evento_guardar"}; // trabaja sobre el guion, no sobre el evento (eventos_listar, evento_cambiar)
        t.title = "Guardar el guion";
        t.category = "evento";
        t.description = "Escribe el guion en la carpeta del evento, respaldando antes el que estaba.";
        t.details =
            "Guarda en Eventos/<carpeta>/evento.json. El guion anterior queda en Respaldos/ con la fecha, "
            "asi que guardar nunca pierde lo que habia.\n\n"
            "Tambien se puede cambiar el titulo del evento y CUANDO es: `fecha` (YYYY-MM-DD), `inicio` "
            "(HH:MM) y `huso` (nombre IANA, por ejemplo America/Argentina/Buenos_Aires).\n\n"
            "Con la hora puesta, el guion calcula la hora prevista de cada bloque y cada cue. Con la "
            "fecha, el evento se puede convocar y archivar. Y con el huso, el que se conecta desde otro "
            "lado puede saber que hora es para el: sin eso, \"empieza 19:00\" no lo ubica.\n\n"
            "El huso se guarda POR NOMBRE y no por desfasaje (-03:00): el desfasaje cambia con el "
            "horario de verano, asi que uno escrito hoy miente para un evento de dentro de tres meses. "
            "Si no sabes cual poner, el del aparato es un buen valor: viene en guion_ver.\n\n"
            "Un formato mal escrito se RECHAZA en vez de guardarse: una fecha que no se entiende no se "
            "ignora sola, se lee mal.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nombre", {{"type", "string"}}},
              {"fecha", {{"type", "string"}, {"description", "YYYY-MM-DD, el dia del evento."}}},
              {"inicio", {{"type", "string"}, {"description", "HH:MM, hora prevista de arranque."}}},
              {"huso",
               {{"type", "string"},
                {"description", "Nombre IANA del huso, p. ej. America/Argentina/Buenos_Aires."}}},
              {"invitados", {{"type", "boolean"}, {"description", "el evento usa invitados en la sala (la página ofrece entrar)"}}},
              {"preguntas", {{"type", "boolean"}, {"description", "el público puede preguntar y votar (/publico/)"}}},
              {"asamblea", {{"type", "boolean"}, {"description", "es una sesión de asamblea: la página muestra integrantes, quórum, orden del día y votaciones"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            // Se valida ANTES de tocar nada: si la fecha esta mal, el nombre
            // tampoco se cambia. Guardar a medias deja al operador sin saber
            // que quedo escrito.
            if (a.contains("fecha") && a["fecha"].is_string() && !a["fecha"].get<std::string>().empty() &&
                !evento::fechaValida(a["fecha"].get<std::string>()))
                return done(ToolResult::error(
                    "La fecha tiene que ser un dia que exista, en formato YYYY-MM-DD (llego: " +
                    a["fecha"].get<std::string>() + ")."));
            if (a.contains("inicio") && a["inicio"].is_string() && !a["inicio"].get<std::string>().empty() &&
                !evento::horaValida(a["inicio"].get<std::string>()))
                return done(ToolResult::error("La hora tiene que estar en formato HH:MM (llego: " +
                                              a["inicio"].get<std::string>() + ")."));
            if (a.contains("huso") && a["huso"].is_string() && !a["huso"].get<std::string>().empty() &&
                !QTimeZone(QByteArray::fromStdString(a["huso"].get<std::string>())).isValid())
                return done(ToolResult::error("No conozco ese huso horario: " + a["huso"].get<std::string>() +
                                              ". Va el nombre IANA, como America/Argentina/Buenos_Aires."));
            if (a.contains("nombre")) ev.setNombre(QString::fromStdString(a["nombre"]));
            if (a.contains("fecha")) ev.setFecha(QString::fromStdString(a["fecha"]));
            if (a.contains("inicio")) ev.setInicio(QString::fromStdString(a["inicio"]));
            if (a.contains("huso")) ev.setHuso(QString::fromStdString(a["huso"]));
            if (a.contains("invitados") && a["invitados"].is_boolean()) ev.setInvitados(a["invitados"].get<bool>());
            if (a.contains("preguntas") && a["preguntas"].is_boolean()) ev.setPreguntas(a["preguntas"].get<bool>());
            if (a.contains("asamblea") && a["asamblea"].is_boolean()) ev.setAsamblea(a["asamblea"].get<bool>());
            if (!ev.guardar())
                return done(ToolResult::error("No se pudo guardar (¿hay algun evento abierto?)."));
            done(ToolResult::ok("Guion guardado."));
        };
        core.addTool(std::move(t));
    }

    // --- Revisar y correr -----------------------------------------------------
    {
        Tool t;
        t.name = "evento_revisar";
        t.title = "Revision previa";
        t.category = "evento";
        t.description =
            "Busca lo que impide correr el guion: cues incompletos, escenas que no existen, bloques "
            "vacios, cadenas largas de cues automaticos.";
        t.details =
            "Es lo que evita los problemas del dia del evento. Corrila despues de armar el guion y de "
            "nuevo antes de empezar.\n\n"
            "Cada reparo dice donde esta (`bloque`, `cue`) y si es `grave` (asi no se puede correr) o "
            "solo algo para mirar.\n\n"
            "Revisa tambien los archivos que el guion nombra: que la musica y los videos esten en "
            "los medios del evento, que los discursos esten en Discursos/, que no esten vacios y que ninguno apunte "
            "fuera de la carpeta del evento.\n\n"
            "Revisa tambien el aparato: horas de disco al bitrate configurado, destino de la transmision, "
            "entradas sin señal (reparos sin bloque). Y devuelve la lista de chequeo manual (`chequeo`: lo "
            "que solo se confirma a mano, como probar el PTZ o el casteo; evento_chequeo la marca).";
        t.run = [&ev](const json&, Done done) {
            ev.revisar();
            json out = json::array();
            int graves = 0;
            for (const QVariant& v : ev.reparos()) {
                const QVariantMap m = v.toMap();
                if (m["grave"].toBool()) ++graves;
                out.push_back(json{{"bloque", m["bloque"].toInt()},
                                   {"cue", m["cue"].toInt()},
                                   {"grave", m["grave"].toBool()},
                                   {"texto", m["texto"].toString().toStdString()}});
            }
            json chequeo = json::array();
            int pendientes = 0;
            for (const QVariant& v : ev.chequeo()) {
                const QVariantMap m = v.toMap();
                if (!m["hecho"].toBool()) ++pendientes;
                chequeo.push_back(json{{"item", m["item"].toString().toStdString()},
                                       {"texto", m["texto"].toString().toStdString()},
                                       {"hecho", m["hecho"].toBool()},
                                       {"nota", m["nota"].toString().toStdString()}});
            }
            done(ToolResult().data(json{{"reparos", out},
                                        {"graves", graves},
                                        {"sePuedeCorrer", graves == 0},
                                        {"chequeo", chequeo},
                                        {"chequeo_pendientes", pendientes}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_chequeo";
        t.title = "Marcar la lista de chequeo del evento";
        t.category = "evento";
        t.description =
            "Marca un ítem de la lista de chequeo manual del evento abierto (`item`: ptz, casteo, caida, "
            "glosario, respaldo, auriculares, sonido, o uno nuevo con ese nombre), `hecho` y una `nota`. Es lo "
            "que no se puede medir solo: confirmalo con el operador antes de marcarlo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"item", "hecho"}},
                         {"properties",
                          {{"item", {{"type", "string"}}},
                           {"hecho", {{"type", "boolean"}}},
                           {"nota", {{"type", "string"}}},
                           {"texto", {{"type", "string"}, {"description", "lo que ve la persona, para un ítem nuevo"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (!ev.marcarChequeo(QString::fromStdString(a.value("item", std::string())), a.value("hecho", false),
                                  QString::fromStdString(a.value("nota", std::string())),
                                  QString::fromStdString(a.value("texto", std::string()))))
                return done(ToolResult::error("No hay un evento abierto (guion_abrir)."));
            done(ToolResult().text("Anotado."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_chequeo_ver";
        t.title = "Ver la lista de chequeo del evento";
        t.category = "evento";
        t.readOnly = true;
        t.description = "La lista de chequeo manual del evento abierto: cada ítem con si está hecho, la nota y cuándo.";
        t.run = [&ev](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : ev.chequeo()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"item", m["item"].toString().toStdString()},
                                   {"texto", m["texto"].toString().toStdString()},
                                   {"hecho", m["hecho"].toBool()},
                                   {"nota", m["nota"].toString().toStdString()},
                                   {"cuando", m["cuando"].toString().toStdString()}});
            }
            done(ToolResult().data(json{{"chequeo", out}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_ensayo_rapido";
        t.title = "Ensayo acelerado";
        t.category = "evento";
        t.description =
            "Recorre el guion ENTERO de una, sin esperar las duraciones y sin tocar nada, y dice de cada "
            "cue si se podria ejecutar.";
        t.details =
            "La diferencia con evento_revisar es que esto CAMINA el recorrido de verdad: sigue el orden "
            "real de los cues cruzando de bloque en bloque, asi que ademas de los cues incompletos "
            "muestra los bloques vacios que nadie pisa y los cues de un tipo que todavia no se ejecuta.\n\n"
            "No sale al aire nunca, ni siquiera con el ensayo apagado. Es lo que conviene correr despues "
            "de armar un guion largo, antes de probarlo en tiempo real.\n\n"
            "El resultado queda en la bitacora, que se devuelve entera.";
        t.readOnly = true;
        t.run = [&ev](const json&, Done done) {
            ev.ensayoAcelerado();
            json out = json::array();
            for (const QVariant& v : ev.bitacora()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"texto", m["texto"].toString().toStdString()},
                                   {"tipo", m["tipo"].toString().toStdString()}});
            }
            // La bitacora se lee de arriba para abajo con lo mas nuevo primero;
            // para un informe conviene el orden en que paso.
            std::reverse(out.begin(), out.end());
            done(ToolResult().data(json{{"recorrido", out}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_informe";
        t.title = "Informe del evento";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Lo que realmente paso: por bloque y por cue, cuanto estaba previsto y cuanto duro de verdad.";
        t.details =
            "Es la materia prima para el evento siguiente: con los desvios reales, las duraciones "
            "previstas de la proxima vez dejan de ser una adivinanza.\n\n"
            "Se arma del recorrido que el guion fue guardando, asi que solo tiene sentido despues de "
            "correrlo (o de ensayarlo, y en ese caso `ensayo` viene en true). Si no se corrio, viene "
            "vacio.\n\n"
            "Respeta el ORDEN EN QUE PASO, no el del guion: si el operador salto o repitio un bloque, el "
            "informe lo cuenta asi.\n\n"
            "Con `guardar: true` ademas lo escribe en la carpeta del evento con la fecha en el nombre.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"guardar", {{"type", "boolean"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            const QVariantMap inf = ev.informe();
            if (inf.isEmpty())
                return done(ToolResult::error(
                    "Todavia no hay nada que informar: el guion no se corrio en esta sesion."));
            QString archivo;
            if (a.value("guardar", false)) archivo = ev.guardarInforme();
            json out =
                json::parse(QString(QJsonDocument(QJsonObject::fromVariantMap(inf)).toJson()).toStdString(),
                            nullptr, false);
            if (!archivo.isEmpty()) out["archivo"] = archivo.toStdString();
            done(ToolResult().data(out));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_correr";
        t.title = "Correr el guion";
        t.category = "evento";
        t.description = "Arranca el guion, avanza al cue siguiente, vuelve atras, salta a uno, pausa o para. "
                        "Con `ensayo` recorre todo SIN salir al aire.";
        t.details =
            "Esto SALE AL AIRE salvo que `ensayo` este prendido: los cues de escena cambian lo que se ve "
            "y los de orador le dan la palabra a alguien de verdad. Por eso no es de solo lectura y "
            "conviene que el operador sepa lo que se va a hacer.\n\n"
            "`accion`:\n"
            "  arrancar    empieza por el primer cue\n"
            "  siguiente   pasa al que sigue\n"
            "  atras       vuelve al anterior (para repetir algo)\n"
            "  ir          salta a `bloque` y `cue`\n"
            "  pausar      frena el guion; el tiempo pausado NO cuenta como atraso\n"
            "  reanudar\n"
            "  parar       lo deja parado\n\n"
            "`ensayo` es un modo que QUEDA como se lo dejo (como el boton Ensayo de la GUI): despues de "
            "ensayar, para salir al aire de verdad hay que pasar `ensayo`: false (la respuesta dice en "
            "`ensayo` como quedo).\n\n"
            "La respuesta trae el cue actual, el que sigue y el reloj: `desvioSeg` positivo = vamos "
            "tarde.\n\n"
            "Mientras el guion corre sin ensayo, el sistema entra en MODO EVENTO: la app le avisa al "
            "operador antes de cualquier accion de riesgo, y settings_set queda bloqueado (un ajuste que "
            "reinicia la sala sacaria a todos los invitados en el peor momento posible).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"accion",
               {{"type", "string"},
                {"enum", {"arrancar", "siguiente", "atras", "ir", "pausar", "reanudar", "parar"}}}},
              {"bloque", {{"type", "integer"}, {"minimum", 0}}},
              {"cue", {{"type", "integer"}, {"minimum", 0}}},
              {"ensayo", {{"type", "boolean"}, {"description", "Recorrer sin salir al aire."}}},
              {"forzar", {{"type", "boolean"}, {"description", "arrancar aunque la revisión encuentre problemas graves"}}}}},
            {"required", {"accion"}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("ensayo")) ev.setEnsayo(a["ensayo"].get<bool>());
            const std::string accion = a["accion"];
            if (accion == "arrancar") {
                // La misma revisión previa que la GUI: con problemas graves no arranca sin `forzar`.
                if (const int graves = ev.arrancarRevisando(a.value("forzar", false)); graves > 0)
                    return done(ToolResult::error("La revisión encontró " + std::to_string(graves) +
                                                  " problema(s) grave(s): mirá evento_revisar (o evento_verificar) "
                                                  "y corregilos, o repetí con `forzar`: true."));
            }
            else if (accion == "siguiente")
                ev.avanzar();
            else if (accion == "atras")
                ev.retroceder();
            else if (accion == "ir")
                ev.irA(a.value("bloque", 0), a.value("cue", 0));
            else if (accion == "pausar")
                ev.pausar();
            else if (accion == "reanudar")
                ev.reanudar();
            else if (accion == "parar")
                ev.parar();
            else
                return done(ToolResult::error("Accion desconocida: " + accion));

            const auto mapa = [](const QVariantMap& m) {
                json o = json::object();
                for (auto it = m.begin(); it != m.end(); ++it) {
                    const QVariant& v = it.value();
                    if (v.typeId() == QMetaType::Int)
                        o[it.key().toStdString()] = v.toInt();
                    else if (v.typeId() == QMetaType::Bool)
                        o[it.key().toStdString()] = v.toBool();
                    else if (v.typeId() == QMetaType::QString)
                        o[it.key().toStdString()] = v.toString().toStdString();
                }
                return o;
            };
            done(ToolResult().data(json{{"estado", ev.estado().toStdString()},
                                        {"ensayo", ev.ensayo()},
                                        {"actual", mapa(ev.actual())},
                                        {"siguiente", mapa(ev.siguiente())},
                                        {"reloj", mapa(ev.reloj())}}));
        };
        core.addTool(std::move(t));
    }

    // --- Las guías de cada tipo de evento (datos/escenarios) -------------------------
    {
        Tool t;
        t.name = "evento_escenarios";
        t.title = "Los tipos de evento y sus guías";
        t.category = "evento";
        t.readOnly = true;
        t.description = "Los escenarios para los que hay guía de preparación (reunión chica, clase híbrida, sesión de un "
                        "cuerpo deliberativo, asamblea con votación, streaming de juegos, acto, audiencia pública…): id, "
                        "título y para qué sirve. ANTES de preparar un evento nuevo, elegí el que corresponde y leelo con "
                        "evento_escenario.";
        t.run = [](const json&, Done done) {
            QFile f(":/escenarios/indice.json");
            if (!f.open(QIODevice::ReadOnly)) return done(ToolResult::error("no están las guías en este programa"));
            done(ToolResult().data({{"escenarios", json::parse(f.readAll().toStdString(), nullptr, false)}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_escenario";
        t.title = "La guía para preparar un tipo de evento";
        t.category = "evento";
        t.readOnly = true;
        t.description = "El protocolo para preparar CUALQUIER evento (entrevistar antes de armar, el orden de armado, qué "
                        "verificar antes de invitar, el ensayo y qué entregar al final) más la guía del escenario `id` "
                        "(evento_escenarios): sus preguntas propias, qué armar, qué verificar. Seguilo paso a paso.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [](const json& a, Done done) {
            const auto leer = [](const QString& r) {
                QFile f(r);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString comun = leer(":/escenarios/comun.md");
            if (comun.isEmpty()) return done(ToolResult::error("no están las guías en este programa"));
            const std::string id = a.value("id", std::string());
            QString guia;
            if (!id.empty()) {
                // Solo letras, números y guiones: es un nombre de archivo.
                if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
                    return done(ToolResult::error("id inválido: " + id));
                guia = leer(":/escenarios/" + QString::fromStdString(id) + ".md");
                if (guia.isEmpty()) return done(ToolResult::error("no hay una guía «" + id + "» (evento_escenarios)"));
            }
            done(ToolResult::ok((comun + (guia.isEmpty() ? QString() : "\n\n---\n\n" + guia)).toStdString()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_armar";
        t.title = "Armar un evento entero de una vez";
        t.category = "evento";
        t.description =
            "Crea el evento con su fecha, sus bloques con tiempos y su lista de gente, todo en una "
            "llamada.";
        t.details =
            "ANTES: la guía del tipo de evento (evento_escenarios / evento_escenario): qué preguntar y qué "
            "verificar.\n\n"
            "ES PARA ARMAR CONVERSANDO. El que organiza cuenta lo que va a pasar —«asamblea del "
            "consorcio, el jueves 8 a las 19, dos horas, presencial y por videollamada, hay que votar "
            "tres puntos, son 40 propietarios»— y esto lo deja escrito de una.\n\n"
            "PROPONE, NO SACA AL AIRE. Deja el evento guardado para revisar. Armar un evento entero "
            "de una orden es justo el caso donde una equivocacion cuesta caro y se descubre en vivo, "
            "asi que MOSTRALE AL OPERADOR lo que vas a armar ANTES de llamar a esto, y despues "
            "corre `evento_revisar` y contale lo que salga.\n\n"
            "No reemplaza nada: si ya hay un evento con ese nombre, crea otro con un sufijo. Nunca "
            "pisa un guion existente.\n\n"
            "Los bloques van en orden, con sus cues. Cada cue necesita lo suyo segun el tipo (una "
            "escena necesita el nombre de la escena, un video el archivo): lo que falte queda "
            "marcado y `evento_revisar` lo lista. Un bloque sin cues sirve igual como marcador de "
            "tramo con su duracion.\n\n"
            "`gente` arma el padron. Para una asamblea, `vota: true` en los que tienen derecho: sin "
            "eso no hay quorum y lo que se decida no vale. Las fichas de rostro se enganchan "
            "despues con `padron_cambiar`.\n\n"
            "LA FECHA CON SU HUSO no es opcional en un evento hibrido: el que se conecta de otro lado "
            "no sabe a que hora es. Si no te la dijeron, pregunta antes de armar.\n\n"
            "UNA SESION DE ASAMBLEA (cuerpo con bancas, central, votacion nominal, remotos) necesita mucho mas que "
            "el guion: camaras por banca y ESCENAS que las muestren. Lee help {\"topic\": \"asamblea\"}; si hay "
            "un Cuerpo cargado, el resultado trae lo que falta (asamblea_revisar).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"nombre"}},
            {"properties",
             {{"nombre", {{"type", "string"}}},
              {"fecha", {{"type", "string"}, {"description", "YYYY-MM-DD"}}},
              {"inicio", {{"type", "string"}, {"description", "HH:MM"}}},
              {"huso", {{"type", "string"}, {"description", "nombre IANA"}}},
              {"nota", {{"type", "string"}}},
              {"invitados", {{"type", "boolean"}, {"description", "usa invitados en la sala de videoconferencia"}}},
              {"preguntas", {{"type", "boolean"}, {"description", "el público puede preguntar y votar"}}},
              {"asamblea", {{"type", "boolean"}, {"description", "es una sesión de asamblea"}}},
              {"bloques",
               {{"type", "array"},
                {"items",
                 {{"type", "object"},
                  {"properties",
                   {{"nombre", {{"type", "string"}}},
                    {"nota", {{"type", "string"}}},
                    {"cues",
                     {{"type", "array"},
                      {"items",
                       {{"type", "object"},
                        {"properties",
                         {{"tipo",
                           {{"type", "string"},
                            {"description", "escena, musica, video, teleprompter, orador, grafico, "
                                            "pantalla, audio, pausa, espera o nota"}}},
                          {"texto", {{"type", "string"}, {"description", "lo que lee el operador"}}},
                          {"duracionSeg", {{"type", "integer"}}},
                          {"automatico", {{"type", "boolean"}}},
                          {"datos", {{"type", "object"}, {"description", "lo propio del tipo"}}}}}}}}}}}}}}},
              {"gente",
               {{"type", "array"},
                {"items",
                 {{"type", "object"},
                  {"required", {"nombre"}},
                  {"properties",
                   {{"nombre", {{"type", "string"}}},
                    {"papel", {{"type", "string"}, {"enum", {"orador", "panelista", "publico", "prensa", "staff"}}}},
                    {"canal", {{"type", "string"}, {"enum", {"presencial", "online", "sin_definir"}}}},
                    {"contacto", {{"type", "string"}}},
                    {"nota", {{"type", "string"}}},
                    {"vota", {{"type", "boolean"}}}}}}}}}}}};
        t.run = [&ev, &vm](const json& a, Done done) {
            const std::string nombre = a.value("nombre", std::string());
            if (nombre.empty()) return done(ToolResult::error("El evento necesita un nombre."));
            // Se valida ANTES de crear la carpeta: quedarse con un evento a
            // medias porque la fecha estaba mal escrita es basura en el disco.
            if (a.contains("fecha") && !a["fecha"].get<std::string>().empty() &&
                !evento::fechaValida(a["fecha"].get<std::string>()))
                return done(ToolResult::error("La fecha tiene que ser un dia que exista, YYYY-MM-DD."));
            if (a.contains("inicio") && !a["inicio"].get<std::string>().empty() &&
                !evento::horaValida(a["inicio"].get<std::string>()))
                return done(ToolResult::error("La hora tiene que estar en HH:MM."));
            if (a.contains("huso") && !a["huso"].get<std::string>().empty() &&
                !QTimeZone(QByteArray::fromStdString(a["huso"].get<std::string>())).isValid())
                return done(ToolResult::error("No conozco ese huso: " + a["huso"].get<std::string>() + "."));

            const QString carpeta = ev.crear(QString::fromStdString(nombre));
            if (carpeta.isEmpty()) return done(ToolResult::error("No se pudo crear el evento."));

            if (a.contains("fecha")) ev.setFecha(QString::fromStdString(a["fecha"]));
            if (a.contains("inicio")) ev.setInicio(QString::fromStdString(a["inicio"]));
            if (a.contains("huso")) ev.setHuso(QString::fromStdString(a["huso"]));
            if (a.contains("invitados") && a["invitados"].is_boolean()) ev.setInvitados(a["invitados"].get<bool>());
            if (a.contains("preguntas") && a["preguntas"].is_boolean()) ev.setPreguntas(a["preguntas"].get<bool>());
            if (a.contains("asamblea") && a["asamblea"].is_boolean()) ev.setAsamblea(a["asamblea"].get<bool>());

            int nBloques = 0, nCues = 0;
            if (a.contains("bloques") && a["bloques"].is_array()) {
                for (const json& b : a["bloques"]) {
                    if (!b.is_object()) continue;
                    ev.agregarBloque(QString::fromStdString(b.value("nombre", std::string())));
                    const int bi = nBloques++;
                    if (!b.contains("cues") || !b["cues"].is_array()) continue;
                    int ci = 0;
                    for (const json& c : b["cues"]) {
                        if (!c.is_object()) continue;
                        ev.agregarCue(bi, QString::fromStdString(c.value("tipo", std::string("nota"))));
                        QVariantMap campos{{"texto", QString::fromStdString(c.value("texto", std::string()))},
                                           {"duracionSeg", c.value("duracionSeg", 0)},
                                           {"automatico", c.value("automatico", false)}};
                        // Como guion_cue: lo propio del tipo va en `datos` (antes se
                        // mezclaba con los campos del cue y la escena quedaba sin nombre).
                        if (c.contains("datos") && c["datos"].is_object()) campos["datos"] = aVariantMap(c["datos"]);
                        ev.editarCue(bi, ci++, campos);
                        ++nCues;
                    }
                }
            }
            ev.guardar();

            int nGente = 0;
            if (a.contains("gente") && a["gente"].is_array())
                for (const json& g : a["gente"]) {
                    if (!g.is_object() || g.value("nombre", std::string()).empty()) continue;
                    QVariantMap m;
                    for (const char* k : {"nombre", "papel", "canal", "contacto", "nota"})
                        if (g.contains(k) && g[k].is_string())
                            m[k] = QString::fromStdString(g[k].get<std::string>());
                    if (g.contains("vota")) m["vota"] = g["vota"].get<bool>();
                    if (!ev.anotar(m).isEmpty()) ++nGente;
                }

            ToolResult r;
            r.text("Evento «" + nombre + "» armado: " + std::to_string(nBloques) + " bloque(s), " +
                   std::to_string(nCues) + " cue(s) y " + std::to_string(nGente) +
                   " persona(s) en el padron. NO esta al aire: revisalo con `evento_revisar` y "
                   "contale al operador lo que salga.");
            r.structured = json{{"carpeta", carpeta.toStdString()},
                                {"bloques", nBloques},
                                {"cues", nCues},
                                {"gente", nGente}};
            // Con un Cuerpo cargado es una sesión de asamblea: lo que le falta (sobre todo
            // escenas) se dice ya, aunque nadie haya pedido revisarlo.
            if (!vm.cuerpo().cuerpo().integrantes().empty()) {
                const json asam = revisarAsamblea(vm, vm.runtime());
                r.structured["asamblea"] = asam;
                if (!asam.value("listo", true)) {
                    std::string t = "\n\nPara sesionar todavía falta (asamblea_revisar):";
                    for (const auto& f : asam["faltas"])
                        t += "\n- " + f.value("detalle", std::string()) + " → " + f.value("como", std::string());
                    t += "\nDecíselo al operador y ofrecé arreglarlo (asamblea_escenas_base arma las escenas).";
                    r.text(t);
                }
            }
            done(r);
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_capitulos";
        t.title = "Los capitulos de la grabacion";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "Las marcas de tiempo de cada bloque del evento, listas para pegar en la descripcion del "
            "video.";
        t.details =
            "Salen del recorrido REAL que el guion fue guardando, no de lo previsto: si el operador "
            "salto o repitio un bloque, las marcas lo reflejan.\n\n"
            "SE MIDEN DESDE QUE ARRANCO LA GRABACION, no desde el arranque del evento. Casi siempre "
            "se aprieta grabar antes de empezar, y contar desde el evento dejaria todas las marcas "
            "corridas. Si no se esta grabando, no hay contra que medir y se dice.\n\n"
            "UNO POR BLOQUE, no por cue: cuarenta capitulos de quince segundos no le sirven a nadie, "
            "y los bloques son justamente los tramos con nombre.\n\n"
            "`texto` viene listo para pegar. Para dejarlo en el video: `youtube_video_update` con la "
            "descripcion, o `youtube_video_upload` si todavia no se subio.\n\n"
            "Las reglas de YouTube estan contempladas: el primero tiene que ser 0:00 (si el guion "
            "arranco despues, se antepone uno), ninguno puede durar menos de diez segundos (los "
            "pegados se juntan) y tiene que haber al menos tres (si no, `ok` viene en false y "
            "`motivo` lo dice, en vez de dar una lista que YouTube va a ignorar en silencio).\n\n"
            "`antesDeGrabar` cuenta los bloques que pasaron antes de apretar grabar: esos no estan en "
            "el video y no se les inventa una marca.";
        t.run = [&ev, &vm](const json&, Done done) {
            const QVariantMap c = ev.capitulos(vm.recordStartMs());
            if (c.isEmpty()) return done(ToolResult::error("No hay evento abierto o el guion no se corrio."));
            json out = json{{"ok", c.value("ok").toBool()},
                            {"texto", c.value("texto").toString().toStdString()},
                            {"antesDeGrabar", c.value("antesDeGrabar").toInt()}};
            if (!c.value("motivo").toString().isEmpty())
                out["motivo"] = c.value("motivo").toString().toStdString();
            json caps = json::array();
            for (const QVariant& v : c.value("capitulos").toList()) {
                const QVariantMap m = v.toMap();
                caps.push_back(json{{"seg", m.value("seg").toInt()},
                                    {"marca", m.value("marca").toString().toStdString()},
                                    {"titulo", m.value("titulo").toString().toStdString()}});
            }
            out["capitulos"] = caps;
            ToolResult r;
            if (!c.value("ok").toBool()) r.text(c.value("motivo").toString().toStdString());
            r.structured = out;
            done(r);
        };
        core.addTool(std::move(t));
    }

    registrarPadron(core, ev, fichaDe);
}

// ============================================================================
// El padron: la gente del evento
// ============================================================================
void registrarPadron(McpCore& core, EventoViewModel& ev, FichaDe fichaDe) {
    core.addCategory("padron", "La gente del evento",
                     "Quién viene, con qué papel, por dónde asiste y si ya llegó. Vale para los "
                     "que entran por la puerta y para los que entran por la sala: es una sola lista.");

    const auto sinEvento = [](Done& done) {
        done(ToolResult::error("No hay ningun evento abierto: abri uno con guion_abrir."));
    };
    const auto aMapaQt = [](const json& j) {
        QVariantMap m;
        if (!j.is_object()) return m;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const QString k = QString::fromStdString(it.key());
            if (it->is_string())
                m[k] = QString::fromStdString(it->get<std::string>());
            else if (it->is_boolean())
                m[k] = it->get<bool>();
            else if (it->is_number_integer())
                m[k] = static_cast<qint64>(it->get<std::int64_t>());
        }
        return m;
    };
    const auto listaDe = [](EventoViewModel& vm) {
        json out = json::array();
        for (const QVariant& v : vm.padron()) {
            const QVariantMap m = v.toMap();
            json o = json::object();
            for (auto it = m.begin(); it != m.end(); ++it) {
                const QVariant& val = it.value();
                if (val.typeId() == QMetaType::Bool)
                    o[it.key().toStdString()] = val.toBool();
                else if (val.typeId() == QMetaType::LongLong)
                    o[it.key().toStdString()] = val.toLongLong();
                else
                    o[it.key().toStdString()] = val.toString().toStdString();
            }
            out.push_back(o);
        }
        return out;
    };
    // Dejar pasar solo a alguien EXIGE que su ficha exista de verdad. Una
    // entrada con un id que no existe no deja pasar a nadie y nadie se entera
    // hasta el dia del evento, cuando esa persona se queda esperando en el
    // lobby y el operador no sabe por que.
    const auto fichaFalla = [fichaDe](const json& a, std::string& motivo) {
        if (!a.value("dejarPasarSolo", false)) return false;
        const std::string id = a.value("personaId", std::string());
        if (id.empty()) {
            motivo = "Para que alguien entre solo hace falta su ficha de rostro (`personaId`): sin cara no "
                     "hay con que reconocerlo, y entraria por haber ESCRITO un nombre, que es lo que no se "
                     "quiere. `rostro_listar` da las fichas.";
            return true;
        }
        const auto [existe, nombre] = fichaDe ? fichaDe(id) : std::pair{true, std::string()};
        if (!existe) {
            motivo = "No hay ninguna ficha con id '" + id +
                     "' en la libreta de rostros. `rostro_listar` los da; si esa persona todavia no "
                     "esta, dale de alta con `rostro_guardar` primero.";
            return true;
        }
        return false;
    };
    const auto resumenDe = [](EventoViewModel& vm) {
        json r = json::object();
        const QVariantMap m = vm.resumenPadron();
        for (auto it = m.begin(); it != m.end(); ++it)
            r[it.key().toStdString()] = it.value().toInt();
        return r;
    };

    {
        Tool t;
        t.name = "padron_ver";
        t.title = "La gente del evento, y quienes estan";
        t.category = "padron";
        t.readOnly = true;
        t.description =
            "La lista de quien viene al evento abierto, con su papel y su estado, mas el recuento de "
            "cuantos llegaron.";
        t.details =
            "ESTO CONTESTA \"quienes estan\", que en un evento hibrido es LA pregunta y hasta ahora no "
            "tenia quien la contestara: los de la sala se veian con participant_* y los que entran por "
            "la puerta no se veian en ningun lado. El padron es UNO para los dos mundos.\n\n"
            "Cada persona trae:\n"
            "- `papel`: orador, panelista, publico, prensa o staff.\n"
            "- `canal`: presencial, online o sin_definir (que es lo honesto en una lista recien armada).\n"
            "- `estado`: invitado -> confirmo -> llego -> se_fue.\n"
            "- `personaId`: su ficha en la libreta de rostros, si tiene. Es lo que permite reconocerla "
            "en la puerta; sin eso solo se la puede buscar por nombre.\n"
            "- `vota`: si cuenta para el quorum. NO sale del papel: en una asamblea un propietario es "
            "publico y vota, y la prensa no.\n"
            "- `llegoMs` y `porDonde`: cuando se lo acredito y por donde (rostro, qr, mostrador, lobby).\n"
            "- `codigo`: su codigo de votante, cuatro simbolos. Es lo que se le dice al acreditarlo "
            "para que pueda votar en una votacion formal, donde el telefono no alcanza —se borra el "
            "sitio y se vota de nuevo—. No lleva O ni 0 ni I ni 1, porque se dicta en voz alta.\n\n"
            "`resumen` trae `presentes` (los que llegaron), `presenciales` y `enLinea` (de los "
            "presentes, por donde), `faltan` y `votoPresente`, que es el QUORUM: cuantos de los que "
            "votan estan. Sin quorum, lo que se decida en una asamblea no vale.\n\n"
            "Se puede filtrar por `papel`, `canal` o `estado` para no leer cuarenta renglones cuando se "
            "buscan tres.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"papel",
               {{"type", "string"}, {"enum", {"orador", "panelista", "publico", "prensa", "staff"}}}},
              {"canal", {{"type", "string"}, {"enum", {"presencial", "online", "sin_definir"}}}},
              {"estado", {{"type", "string"}, {"enum", {"invitado", "confirmo", "llego", "se_fue"}}}},
              {"solos",
               {{"type", "boolean"},
                {"description", "Solo los que entran a la sala sin que el operador apruebe."}}}}}};
        t.run = [&ev, listaDe, resumenDe, sinEvento](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return sinEvento(done);
            json gente = json::array();
            for (const json& p : listaDe(ev)) {
                for (const char* c : {"papel", "canal", "estado"})
                    if (a.contains(c) && a[c].is_string() && p.value(c, "") != a[c].get<std::string>())
                        goto siguiente;
                if (a.value("solos", false) && !p.value("dejarPasarSolo", false)) goto siguiente;
                gente.push_back(p);
            siguiente:;
            }
            done(ToolResult().data(
                json{{"evento", ev.abierto().toStdString()}, {"gente", gente}, {"resumen", resumenDe(ev)}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "padron_anotar";
        t.title = "Anotar a alguien en el evento";
        t.category = "padron";
        t.description = "Suma a una persona a la lista del evento abierto, con su papel y por donde asiste.";
        t.details =
            "Es como se arma la lista ANTES del evento, y tambien como se agrega al que aparece sin "
            "estar anotado.\n\n"
            "Lo unico obligatorio es el `nombre`. El resto se puede completar despues con "
            "padron_cambiar: una lista a medias sirve, una lista que no se pudo empezar no.\n\n"
            "`personaId` es su ficha en la libreta de rostros (ver rostro_listar). Con ella se la "
            "reconoce en la puerta y puede entrar sola a la sala; sin ella solo se la busca por nombre.\n\n"
            "`dejarPasarSolo` NECESITA `personaId`: dejar pasar a quien ESCRIBIO un nombre es dejar "
            "pasar a cualquiera que sepa escribirlo, asi que sin ficha se apaga solo.\n\n"
            "Esto NO da de alta a nadie en la libreta de rostros: son dos cosas distintas a proposito. "
            "La libreta es permanente y el padron caduca con el evento, porque la misma persona es "
            "panelista en uno y publico en el siguiente.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"nombre"}},
            {"properties",
             {{"nombre", {{"type", "string"}}},
              {"papel",
               {{"type", "string"}, {"enum", {"orador", "panelista", "publico", "prensa", "staff"}}}},
              {"canal", {{"type", "string"}, {"enum", {"presencial", "online", "sin_definir"}}}},
              {"estado", {{"type", "string"}, {"enum", {"invitado", "confirmo", "llego", "se_fue"}}}},
              {"personaId", {{"type", "string"}, {"description", "Su ficha en la libreta de rostros."}}},
              {"contacto", {{"type", "string"}, {"description", "Mail o telefono, para convocar."}}},
              {"nota", {{"type", "string"}}},
              {"vota", {{"type", "boolean"}, {"description", "Si cuenta para el quorum."}}},
              {"dejarPasarSolo",
               {{"type", "boolean"},
                {"description",
                 "Entra a la sala sin que el operador apruebe. Exige `personaId`: se lo reconoce por "
                 "la CARA, no por el nombre que escriba al entrar."}}}}}};
        t.run = [&ev, aMapaQt, resumenDe, sinEvento, fichaFalla](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return sinEvento(done);
            std::string motivo;
            if (fichaFalla(a, motivo)) return done(ToolResult::error(motivo));
            const QString id = ev.anotar(aMapaQt(a));
            if (id.isEmpty())
                return done(ToolResult::error("No se pudo anotar (hace falta al menos el nombre)."));
            ToolResult r;
            r.text("Anotado: " + a.value("nombre", std::string()) + ".");
            r.structured = json{{"id", id.toStdString()}, {"resumen", resumenDe(ev)}};
            done(r);
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "padron_cambiar";
        t.title = "Cambiarle los datos a alguien del padron";
        t.category = "padron";
        t.description = "Cambia el papel, el canal, el estado o los datos de una persona ya anotada.";
        t.details =
            "Cambia SOLO lo que se le manda y deja el resto. Mandar `nota` en vacio la borra; no "
            "mandarla la deja como estaba.\n\n"
            "Es donde se le engancha la ficha de rostro (`personaId`) al que se anoto antes de tenerla, "
            "que es lo normal: la lista se arma con nombres y las caras aparecen despues.\n\n"
            "Para marcar que alguien LLEGO no uses esto: usa padron_acreditar, que ademas guarda la "
            "hora y por donde entro.\n\n"
            "Una palabra que no se entiende en `papel`, `canal` o `estado` deja el valor que habia en "
            "vez de cambiarlo por el de fabrica: un dedazo no convierte a un orador en publico.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"id"}},
            {"properties",
             {{"id", {{"type", "string"}, {"description", "El id que devolvio padron_anotar o padron_ver."}}},
              {"nombre", {{"type", "string"}}},
              {"papel",
               {{"type", "string"}, {"enum", {"orador", "panelista", "publico", "prensa", "staff"}}}},
              {"canal", {{"type", "string"}, {"enum", {"presencial", "online", "sin_definir"}}}},
              {"estado", {{"type", "string"}, {"enum", {"invitado", "confirmo", "llego", "se_fue"}}}},
              {"personaId", {{"type", "string"}}},
              {"contacto", {{"type", "string"}}},
              {"nota", {{"type", "string"}}},
              {"vota", {{"type", "boolean"}}},
              {"dejarPasarSolo", {{"type", "boolean"}}}}}};
        t.run = [&ev, aMapaQt, sinEvento, fichaFalla](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return sinEvento(done);
            std::string motivo;
            if (fichaFalla(a, motivo)) return done(ToolResult::error(motivo));
            QVariantMap m = aMapaQt(a);
            m.remove("id");
            if (!ev.cambiarAsistente(QString::fromStdString(a.value("id", std::string())), m))
                return done(ToolResult::error("No hay nadie con ese id en el padron."));
            done(ToolResult::ok("Cambiado."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "padron_acreditar";
        t.title = "Marcar que alguien llego (o que se fue)";
        t.category = "padron";
        t.description =
            "Deja constancia de que una persona esta en el evento, con la hora y por donde entro.";
        t.details =
            "Es la acreditacion, y vale igual para el que entra por la puerta y para el que entra por "
            "la sala: una sola lista para los dos mundos.\n\n"
            "Se puede identificar de tres maneras, en este orden de confianza:\n"
            "- `id`: el del padron, cuando ya se sabe quien es.\n"
            "- `personaId`: la ficha de rostro, que es como lo dice la camara de la puerta.\n"
            "- `nombre`: como se tipea en un mostrador. No hace falta que este igual: los acentos, las "
            "mayusculas y los espacios de mas no importan.\n\n"
            "La ficha de rostro MANDA sobre el nombre: el nombre lo escribe cualquiera y en una lista "
            "de cuarenta hay dos Gonzalez.\n\n"
            "Acreditar dos veces NO es un error y NO mueve la hora: en la puerta se escanea de nuevo "
            "por las dudas todo el tiempo, y la que importa es la de la primera vez.\n\n"
            "Con `se_fue` en true se marca la salida, que es lo que saca a alguien del quorum sin "
            "borrarle que habia llegado.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}},
              {"personaId", {{"type", "string"}}},
              {"nombre", {{"type", "string"}}},
              {"porDonde",
               {{"type", "string"},
                {"enum", {"rostro", "qr", "mostrador", "lobby"}},
                {"description", "Por donde se lo acredito. Sirve para saber en que confiar."}}},
              {"se_fue", {{"type", "boolean"}, {"description", "Marca la salida en vez de la llegada."}}}}}};
        t.run = [&ev, resumenDe, sinEvento](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return sinEvento(done);
            QString id = QString::fromStdString(a.value("id", std::string()));
            if (id.isEmpty())
                id = ev.buscarEnPadron(QString::fromStdString(a.value("personaId", std::string())),
                                       QString::fromStdString(a.value("nombre", std::string())));
            if (id.isEmpty())
                return done(ToolResult::error(
                    "No encontre a esa persona en el padron. Fijate en padron_ver como esta escrita, o "
                    "anotala con padron_anotar si no estaba."));
            const bool seFue = a.value("se_fue", false);
            const bool hecho =
                seFue
                    ? ev.despedir(id)
                    : ev.acreditar(id, QString::fromStdString(a.value("porDonde", std::string("mostrador"))));
            if (!hecho) return done(ToolResult::error("No se pudo: esa persona ya no esta en el padron."));
            ToolResult r;
            r.text(seFue ? "Marcado que se fue." : "Acreditado.");
            r.structured = json{{"id", id.toStdString()}, {"resumen", resumenDe(ev)}};
            done(r);
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitacion";
        t.title = "La invitacion de una persona, con su QR";
        t.category = "padron";
        t.readOnly = true;
        t.description =
            "El texto de la invitacion de alguien del padron y el enlace con QR que le sirve para "
            "acreditarse, preguntar y votar.";
        t.details =
            "EL SISTEMA GENERA, NO MANDA. No tiene correo ni mensajeria, y es a proposito: esto corre "
            "portable desde un pendrive y no depende de los servicios de nadie. Se arma la invitacion "
            "y la manda el que convoca, por donde ya manda todo.\n\n"
            "EL QR HACE TRES COSAS CON UN SOLO ESCANEO:\n"
            "1. Lo ACREDITA. Llega, lo mira con el telefono y queda anotado que esta. Es la tercera "
            "puerta, al lado del reconocimiento de cara y del mostrador: hay gente que no va a "
            "aceptar que le miren la cara, y un evento no puede depender de que la camara ande.\n"
            "2. Le abre la pagina donde pregunta y vota.\n"
            "3. Le deja puesto su codigo de votante, asi no tiene que tipearlo.\n\n"
            "Por eso el QR de cada persona es DISTINTO y no se comparte: lleva su codigo. El que se "
            "proyecta para todos es otro (`publico_pagina`) y no acredita a nadie.\n\n"
            "Sin fecha en el evento la invitacion sale sin fecha y se avisa: una invitacion que no "
            "dice cuando es no sirve para nada.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}, {"description", "el del padron"}}},
              {"nombre", {{"type", "string"}, {"description", "o su nombre, como esta escrito"}}},
              {"remoto",
               {{"type", "boolean"},
                {"description", "el enlace por internet en vez del de la red local"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error("No hay evento abierto."));
            QString id = QString::fromStdString(a.value("id", std::string()));
            if (id.isEmpty())
                id = ev.buscarEnPadron({}, QString::fromStdString(a.value("nombre", std::string())));
            if (id.isEmpty())
                return done(
                    ToolResult::error("No encontre a esa persona en el padron. `padron_ver` las lista."));
            QVariantMap quien;
            for (const QVariant& v : ev.padron()) {
                const QVariantMap m = v.toMap();
                if (m.value("id").toString() == id) {
                    quien = m;
                    break;
                }
            }
            if (quien.isEmpty()) return done(ToolResult::error("Esa persona ya no esta en el padron."));
            ev.pedirInvitacion(quien, a.value("remoto", false), [done, quien](const QVariantMap& r) {
                if (!r.value("ok").toBool())
                    return done(ToolResult::error(r.value("motivo").toString().toStdString()));
                ToolResult res;
                res.text(r.value("texto").toString().toStdString());
                res.structured = json{
                    {"texto", r.value("texto").toString().toStdString()},
                    {"url", r.value("url").toString().toStdString()},
                    {"codigo", quien.value("codigo").toString().toStdString()},
                    {"qr", json::parse(QJsonDocument(QJsonArray::fromVariantList(r.value("modulos").toList()))
                                           .toJson(QJsonDocument::Compact)
                                           .toStdString(),
                                       nullptr, false)}};
                done(res);
            });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "padron_quitar";
        t.destructive = true;
        t.title = "Sacar a alguien del padron";
        t.category = "padron";
        t.description = "Borra a una persona de la lista del evento abierto.";
        t.details =
            "Es para el que se anoto dos veces o el que no venia a este evento. Para el que aviso que "
            "no viene, mejor dejarlo con `estado` en invitado: el que no esta en la lista no se "
            "distingue del que nunca se invito, y eso se nota al armar el evento siguiente.\n\n"
            "No toca la libreta de rostros: la ficha de la persona queda donde estaba.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [&ev, resumenDe, sinEvento](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return sinEvento(done);
            if (!ev.quitarAsistente(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error("No hay nadie con ese id en el padron."));
            ToolResult r;
            r.text("Sacado del padron.");
            r.structured = json{{"resumen", resumenDe(ev)}};
            done(r);
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
