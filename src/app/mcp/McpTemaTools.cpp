// ============================================================================
// src/app/mcp/McpTemaTools.cpp — el tema del evento, por MCP (evento_tema)
//
// Lo mismo que el panel «Tema del evento» (Servicios → Aspecto de las páginas web) y
// por el mismo ViewModel: lo que cambia el asistente se ve en el panel y en todas las
// páginas web en el acto (el gateway arma /comun/tema.css; las páginas lo miran cada 15 s).
// Ver docs/planes/PLAN-TEMA-EVENTO.md.
// ============================================================================
#include "mcp/McpTemaTools.hpp"

#include "i18n/I18n.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/TemaEventoViewModel.hpp"

namespace ba::app::mcp {

void registerTemaTools(McpCore& core, TemaEventoViewModel& tema) {
    Tool t;
    t.name = "evento_tema";
    t.title = ba::i18n::traducir("mcp", "El tema del evento en todas las páginas web");
    t.category = "evento";
    t.description = ba::i18n::traducir(
        "mcp",
        "La cara del evento abierto en TODAS las páginas web: la del evento, invitados, la sesión remota (votar), "
        "las consultas del público, el tótem de registro, las pantallas de la sala y, con el alcance que se elija, "
        "las consolas de los operadores. `accion`: ver | cambiar {modo, colores, tipografia, radio, marca, alcance} | "
        "logo {archivo, cual: logo|logoClaro|favicon} | fuente {archivo, familia, peso, estilo} | quitar {que: "
        "logo|logoClaro|favicon|fuentes} | restablecer | migrar_sitio.");
    t.details = ba::i18n::traducir(
        "mcp",
        "Se guarda en Eventos/<evento>/tema.json; el logo y las fuentes, en Eventos/<evento>/Tema/ (los copia "
        "`logo` y `fuente`: SVG, PNG o WebP hasta 2 MB; WOFF2, WOFF, TTF u OTF hasta 4 MB; un SVG con scripts no "
        "entra). Sin tema.json, el acento y el fondo salen de los del sitio (evento_web sitio {color, fondo}).\n\n"
        "`colores`: {acento, acentoTexto, fondo, superficie, texto, textoSuave} en #rrggbb; vacío = se calcula "
        "(el texto encima del acento sale del contraste WCAG con blanco y negro; la superficie y el texto suave, "
        "de mezclar el fondo y el texto). `modo`: claro | oscuro | \"\" (según el fondo). `tipografia`: {texto, "
        "titulos} (familias: las subidas con `fuente`; sin internet, nada de Google Fonts). `radio`: 0-32 px o -1 "
        "(el de cada página). `marca`: {nombre, logo, logoClaro, favicon} (los archivos, de los ya subidos). "
        "`alcance`: {publico, control} con nada | acento | todo; por omisión el público ve todo y las consolas solo "
        "el acento y el logo (el neumorfismo depende de su fondo). Nunca cambian los colores con significado: tally, "
        "al aire/armado, vúmetros, sí/no de la votación.\n\n"
        "`ver` devuelve el tema, lo `resuelto` (los colores finales y el contraste) y `avisos` de contraste: si "
        "hay alguno, decíselo al operador y proponé otro color. `migrar_sitio` pasa un sitio generado antes del "
        "tema (Web/) a /comun/tema.css sin perder lo que se cambió a mano.");
    t.inputSchema = {
        {"type", "object"},
        {"required", {"accion"}},
        {"properties",
         {{"accion",
           {{"type", "string"}, {"enum", {"ver", "cambiar", "logo", "fuente", "quitar", "restablecer", "migrar_sitio"}}}},
          {"modo", {{"type", "string"}, {"enum", {"", "claro", "oscuro"}}}},
          {"colores",
           {{"type", "object"},
            {"properties",
             {{"acento", {{"type", "string"}}},
              {"acentoTexto", {{"type", "string"}}},
              {"fondo", {{"type", "string"}}},
              {"superficie", {{"type", "string"}}},
              {"texto", {{"type", "string"}}},
              {"textoSuave", {{"type", "string"}}}}}}},
          {"tipografia", {{"type", "object"}, {"properties", {{"texto", {{"type", "string"}}}, {"titulos", {{"type", "string"}}}}}}},
          {"radio", {{"type", "integer"}, {"minimum", -1}, {"maximum", 32}}},
          {"marca",
           {{"type", "object"},
            {"properties",
             {{"nombre", {{"type", "string"}}},
              {"logo", {{"type", "string"}}},
              {"logoClaro", {{"type", "string"}}},
              {"favicon", {{"type", "string"}}}}}}},
          {"alcance",
           {{"type", "object"},
            {"properties",
             {{"publico", {{"type", "string"}, {"enum", {"nada", "acento", "todo"}}}},
              {"control", {{"type", "string"}, {"enum", {"nada", "acento", "todo"}}}}}}}},
          {"archivo", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "ruta del archivo en esta máquina")}}},
          {"cual", {{"type", "string"}, {"enum", {"logo", "logoClaro", "favicon"}}}},
          {"familia", {{"type", "string"}}},
          {"peso", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "400, 700 o un rango variable: «100 900»")}}},
          {"estilo", {{"type", "string"}, {"enum", {"normal", "italic"}}}},
          {"que", {{"type", "string"}, {"enum", {"logo", "logoClaro", "favicon", "fuentes"}}}}}}};
    t.examples = {R"({"accion": "cambiar", "colores": {"acento": "#00a86b"}, "marca": {"nombre": "Foro 2026"}})",
                  R"({"accion": "logo", "archivo": "/Users/operador/Escritorio/logo.svg", "cual": "logo"})"};
    t.run = [&tema](const json& a, Done done) {
        const std::string acc = a.value("accion", std::string("ver"));
        if (!tema.hayEvento()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay ningún evento abierto: guion_abrir.")));
        const auto listo = [&tema, &done](const QString& err) {
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(tema.estado()));
        };
        if (acc == "ver") return done(ToolResult().data(tema.estado()));
        if (acc == "cambiar") {
            json c = json::object();
            for (const char* k : {"modo", "colores", "tipografia", "radio", "marca", "alcance"})
                if (a.contains(k)) c[k] = a[k];
            return listo(tema.cambiarJson(c));
        }
        if (acc == "logo")
            return listo(tema.subirLogo(QString::fromStdString(a.value("archivo", std::string())),
                                        QString::fromStdString(a.value("cual", std::string("logo")))));
        if (acc == "fuente")
            return listo(tema.subirFuente(QString::fromStdString(a.value("archivo", std::string())),
                                          QString::fromStdString(a.value("familia", std::string())),
                                          QString::fromStdString(a.value("peso", std::string("400"))),
                                          QString::fromStdString(a.value("estilo", std::string("normal")))));
        if (acc == "quitar") return listo(tema.quitar(QString::fromStdString(a.value("que", std::string()))));
        if (acc == "restablecer") return listo(tema.restablecer());
        if (acc == "migrar_sitio") {
            const QVariantMap r = tema.migrarSitio();
            if (r.contains("error")) return done(ToolResult::error(r.value("error").toString().toStdString()));
            return done(ToolResult().data(qvToJson(r)));
        }
        done(ToolResult::error(ba::i18n::traducir("mcp", "accion: ver, cambiar, logo, fuente, quitar, restablecer o migrar_sitio")));
    };
    core.addTool(std::move(t));
}

} // namespace ba::app::mcp
