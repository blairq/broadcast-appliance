// ============================================================================
// src/app/mcp/McpPruebaAsambleaTools.cpp — la prueba de asamblea (integrantes simulados)
//
// Las mismas operaciones que la tarjeta «Prueba de asamblea» de Asamblea → Conexiones
// remotas (ui/PruebaAsambleaViewModel): arrancar, el estado en vivo, terminar y el informe
// de la última. Ver docs/funciones/PRUEBA-ASAMBLEA.md.
// ============================================================================

#include "asamblea/PruebaAsambleaReglas.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PruebaAsambleaViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerPruebaAsambleaTools() {
    PruebaAsambleaViewModel& pa = vm_.pruebaAsamblea();
    {
        Tool t;
        t.name = "prueba_asamblea_arrancar";
        t.title = ba::i18n::traducir("mcp", "Arrancar una prueba de asamblea");
        t.category = "sesion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Simula a `integrantes` integrantes de la asamblea entrando por la sesión remota, como desde el "
                   "celular: crea integrantes de PRUEBA en el Cuerpo, les da acceso (link + PIN y usuario + "
                   "contraseña) y los hace entrar con hinchada (de fábrica, si viene en el programa) durante "
                   "`minutos`. Esperan que los habiliten en el Recinto (sesion_habilitar, o `habilitar_solos_s`), "
                   "piden la palabra, hablan con cámara y micrófono, votan cuando hay una votación abierta, se "
                   "les corta la red y vuelven, y algunos erran el PIN hasta quedar bloqueados.");
        t.details = QT_TRANSLATE_NOOP(
            "mcp", "SOLO en un evento sin integrantes reales: si el padrón del evento abierto tiene alguno que no "
                   "es de prueba, no arranca (para no tocar el padrón, el quórum ni el acta): creá un evento vacío "
                   "con evento_nuevo y abrilo. Necesita la conferencia andando. Sin `hinchada`, devuelve el archivo "
                   "de credenciales y la línea para un generador externo. `comportamiento` (todo opcional, en %): "
                   "afuera_pct (20), piden_palabra_pct (30), votan_pct (90), voto («azar» o "
                   "«afirmativo:60,negativo:30,abstencion:10»), reconectan_pct (10), pin_errado_pct (10), "
                   "pin_errado_intentos (5: el gateway lo bloquea), con_usuario_pct (20), entrada_cada_ms (1000), "
                   "habilitar_solos_s (0: los habilita el operador), programa (true). Seguila con "
                   "prueba_asamblea_estado; al terminar (prueba_asamblea_terminar o por tiempo) se borran los "
                   "integrantes de prueba y su acceso, el recinto vuelve a como estaba y el informe queda en "
                   "Pruebas/ del evento.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"integrantes"}},
            {"properties",
             {{"integrantes", {{"type", "integer"}, {"minimum", 1}, {"maximum", asamblea::prueba::kMaxIntegrantes}}},
              {"minutos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}, {"default", 5}}},
              {"hinchada", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "los genera esta máquina (de fábrica, si está)")}}},
              {"comportamiento",
               {{"type", "object"},
                {"properties",
                 {{"afuera_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"piden_palabra_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"votan_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"voto", {{"type", "string"}}},
                  {"reconectan_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"pin_errado_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"pin_errado_intentos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}}},
                  {"con_usuario_pct", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                  {"entrada_cada_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 60000}}},
                  {"habilitar_solos_s", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}}},
                  {"programa", {{"type", "boolean"}}}}}}}}}};
        t.run = [&pa](const json& a, Done done) {
            const bool hinchada = a.value("hinchada", pa.hinchadaDisponible());
            const std::string e = pa.arrancarJson(a.value("integrantes", 10), a.value("minutos", 5),
                                                  a.value("comportamiento", json::object()), hinchada);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Prueba de asamblea arrancada. Seguila con prueba_asamblea_estado."))
                     .data(pa.estadoJson(true)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_asamblea_estado";
        t.title = ba::i18n::traducir("mcp", "Cómo va la prueba de asamblea");
        t.category = "sesion";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El estado de la prueba de asamblea: si corre, el generador, cuántos integrantes reales y de "
                   "prueba hay en el padrón (y si se puede arrancar acá), cómo van los simulados según la app "
                   "(conectados, esperando que los habiliten, habilitados, afuera, bloqueados, en la fila, con la "
                   "palabra, votaron y qué), el archivo de credenciales y la línea para un generador externo, y el "
                   "resumen de la última.");
        t.run = [&pa](const json&, Done done) { done(ToolResult().data(pa.estadoJson(true))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_asamblea_terminar";
        t.title = ba::i18n::traducir("mcp", "Terminar la prueba de asamblea");
        t.category = "sesion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Termina la prueba de asamblea (o descarta una preparada): corta el probador, saca a los "
                   "integrantes de prueba de la sesión, revoca su acceso, los borra del Cuerpo, deja el recinto "
                   "como estaba, borra el archivo de credenciales y guarda el informe en el evento. Sin una prueba "
                   "en curso, con `limpiar`, borra los integrantes de prueba que hayan quedado. El informe queda "
                   "en prueba_asamblea_informe en unos segundos.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"limpiar", {{"type", "boolean"}}}}}};
        t.destructivaSi = "limpiar"; // borra integrantes (solo los de prueba)
        t.run = [&pa](const json& a, Done done) {
            if (!pa.activa()) {
                if (!a.value("limpiar", false))
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay una prueba de asamblea en curso.")));
                const QString e = pa.limpiar();
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Integrantes de prueba borrados (si había).")));
            }
            pa.parar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Terminando la prueba de asamblea (limpieza e informe).")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_asamblea_informe";
        t.title = ba::i18n::traducir("mcp", "El informe de la última prueba de asamblea");
        t.category = "sesion";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El informe completo de la última prueba de asamblea de esta sesión: lo que vio la app, el "
                   "resumen del probador (entraron, esperaron que los habiliten, pidieron y tuvieron la palabra, "
                   "votaron, se reconectaron, quedaron bloqueados; latencias), qué le pasó a cada integrante y "
                   "dónde quedó en el evento.");
        t.run = [&pa](const json&, Done done) {
            const json inf = pa.ultimoInformeJson();
            if (inf.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Todavía no terminó ninguna prueba de asamblea en esta sesión.")));
            done(ToolResult().data(inf));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
