// ============================================================================
// src/app/mcp/McpPruebaSalaTools.cpp — la prueba de sala (simulacro antes del evento)
//
// Las mismas operaciones que la tarjeta «Prueba de sala» de la página Sala
// (ui/PruebaSalaViewModel): arrancar (con la guardia), terminar, la entrada directa
// para todos, el estado en vivo, revisar la máquina y el informe de la última.
// Ver docs/funciones/PRUEBA-SALA.md.
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "ipc/LugaresSala.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PruebaSalaViewModel.hpp"
#include "mcp/InterfazMcp.hpp"

namespace ba::app::mcp {

void McpService::registerPruebaSalaTools() {
    PruebaSalaViewModel& ps = vm_.pruebaSala();
    {
        Tool t;
        t.name = "prueba_sala_arrancar";
        t.title = ba::i18n::traducir("mcp", "Arrancar una prueba de sala");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Simulacro antes del evento: entran `participantes` de prueba por el camino real de los invitados "
                   "(video con su número y voz sintética) durante `minutos`, para ver si la máquina y la red aguantan. "
                   "Con `hinchada` (de fábrica, si viene en el programa) los genera esta máquina; si no, devuelve la "
                   "URL y la credencial de prueba para un generador externo. `decodificar`: cuánto del video que vuelve "
                   "decodifica el probador («muestra», de fábrica: unos pocos, lo justo para ver que el retorno llega "
                   "bien sin cargar la máquina que genera; «nada»: solo envía; «todo»: cada uno decodifica lo suyo). "
                   "`otras_maquinas` > 0 reparte los N entre esta y otras PC: devuelve en `reparto` el código "
                   "(BAGEN1.…) que se pega en la otra app (prueba_sala_generar) o la línea de hinchada para correr allá.");
        t.details = QT_TRANSLATE_NOOP(
            "mcp", "Necesita un evento abierto (el informe va a su lista de chequeo, ítem «prueba_sala», y a "
                   "Pruebas/ de su carpeta) y la conferencia andando. La GUARDIA decide antes: con poca memoria "
                   "disponible, la máquina ocupada o si los N no entrarían, NO arranca y dice por qué; durante la "
                   "prueba la corta sola si la memoria baja de 3 GB, la app pasa de 1200 hilos o la carga de 40. "
                   "Los de prueba entran sin lobby, se marcan «prueba» y quedan fuera del padrón, la votación y el "
                   "quórum. Si el cupo no alcanza se sube y al terminar vuelve. Seguí la prueba con "
                   "prueba_sala_estado y terminala con prueba_sala_parar.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"participantes"}},
                         {"properties",
                          {{"participantes", {{"type", "integer"}, {"minimum", 1}, {"maximum", ba::ipc::kMaxInvitados}}},
                           {"minutos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}, {"default", 5}}},
                           {"hinchada", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "los genera esta máquina (de fábrica, si está)")}}},
                           {"hablan", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10}, {"default", 2},
                                       {"description", QT_TRANSLATE_NOOP("mcp", "cuántos hablan a la vez (0: cada uno a su ritmo)")}}},
                           {"decodificar", {{"type", "string"}, {"enum", {"nada", "muestra", "todo"}}, {"default", "muestra"},
                                            {"description", QT_TRANSLATE_NOOP("mcp", "cuánto video que vuelve decodifica el probador")}}},
                           {"muestra_pct", {{"type", "integer"}, {"minimum", sala::prueba::kMuestraMinPct}, {"maximum", sala::prueba::kMuestraMaxPct},
                                            {"default", 5},
                                            {"description", QT_TRANSLATE_NOOP("mcp", "con «muestra»: el % que se decodifica (al menos 3 participantes)")}}},
                           {"otras_maquinas", {{"type", "integer"}, {"minimum", 0}, {"maximum", sala::prueba::kMaxOtrasMaquinas}, {"default", 0},
                                               {"description", QT_TRANSLATE_NOOP("mcp", "otras PC que generan parte de los N (se suman con el código de `reparto`)")}}},
                           {"esta_genera", {{"type", "boolean"}, {"default", true},
                                            {"description", QT_TRANSLATE_NOOP("mcp", "con otras máquinas: esta también genera (false: solo coordina, así la app medida no comparte la CPU)")}}}}}};
        t.examples = {R"({"participantes": 40, "minutos": 3})", R"({"participantes": 200, "minutos": 2, "decodificar": "nada"})",
                      R"({"participantes": 200, "minutos": 2, "otras_maquinas": 1, "esta_genera": false})"};
        t.run = [&ps](const json& a, Done done) {
            const bool hinchada = a.value("hinchada", ps.hinchadaDisponible());
            const QString e = ps.arrancar(a.value("participantes", 10), a.value("minutos", 5), hinchada, a.value("hablan", 2),
                                          QString::fromStdString(a.value("decodificar", std::string("muestra"))), a.value("muestra_pct", 5),
                                          a.value("otras_maquinas", 0), a.value("esta_genera", true));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Prueba de sala arrancada. Seguila con prueba_sala_estado."))
                     .data(ps.estadoJson(true)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_parar";
        t.title = ba::i18n::traducir("mcp", "Terminar la prueba de sala");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Termina la prueba de sala: corta el probador, revoca la credencial de prueba, echa a los "
                   "participantes de prueba, apaga la entrada directa, devuelve el cupo y guarda el informe en el "
                   "evento. El informe queda en prueba_sala_informe en unos segundos.");
        t.run = [&ps](const json&, Done done) {
            if (!ps.corriendo()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay una prueba de sala en curso.")));
            ps.parar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Terminando la prueba de sala (limpieza e informe).")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_entrada_directa";
        t.title = ba::i18n::traducir("mcp", "Entrada directa para todos (prueba de sala)");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Prende o apaga «Entrada directa para todos»: con `on`, CUALQUIERA con el enlace y el PIN entra a "
                   "la conferencia sin pasar por el lobby (no solo los de prueba). Solo existe durante una prueba de "
                   "sala y se apaga sola al terminarla o al cerrar el programa. Confirmalo con el operador antes de "
                   "prenderla.");
        t.inputSchema = {{"type", "object"}, {"required", {"on"}}, {"properties", {{"on", {{"type", "boolean"}}}}}};
        t.run = [&ps](const json& a, Done done) {
            const QString e = ps.setEntradaDirecta(a.value("on", false));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(a.value("on", false) ? ba::i18n::traducir("mcp", "Pedida la entrada directa para todos.")
                                                      : ba::i18n::traducir("mcp", "Pedido apagar la entrada directa.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_estado";
        t.title = ba::i18n::traducir("mcp", "Cómo va la prueba de sala");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El estado de la prueba de sala: si corre, el generador, la entrada directa, la URL y la "
                   "credencial (para un generador externo), el informe en vivo (cuántos entraron, con video, "
                   "fallidos, cortes, la HD, el ancho de banda, la CPU de la app, del gateway y del probador, la memoria), "
                   "cuánto decodifica el probador, el reparto entre máquinas (cuántas se sumaron, su CPU y el código para "
                   "sumar otra), si esta máquina está generando para otra (`generando`) y el resumen de la última.");
        t.run = [&ps](const json&, Done done) { done(ToolResult().data(ps.estadoJson(true))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_revisar";
        t.title = ba::i18n::traducir("mcp", "¿Aguanta la máquina una prueba de sala?");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Mide la máquina ahora (memoria disponible, carga, hilos de la app) y dice si la guardia dejaría "
                   "arrancar una prueba con `participantes` (`puede`: vacío = sí; si no, por qué).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participantes", {{"type", "integer"}, {"minimum", 1}, {"maximum", ba::ipc::kMaxInvitados}}},
                           {"hinchada", {{"type", "boolean"}}},
                           {"otras_maquinas", {{"type", "integer"}, {"minimum", 0}, {"maximum", sala::prueba::kMaxOtrasMaquinas}}},
                           {"esta_genera", {{"type", "boolean"}}}}}};
        t.run = [&ps](const json& a, Done done) {
            ps.revisarMaquina(a.value("participantes", 10), a.value("hinchada", ps.hinchadaDisponible()), a.value("otras_maquinas", 0),
                              a.value("esta_genera", true));
            done(ToolResult().data(ps.estadoJson(false).value("maquina", json::object())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_generar";
        t.title = ba::i18n::traducir("mcp", "Generar para la prueba de sala de otro equipo");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Esta máquina se suma como GENERADORA a la prueba de sala de OTRO BroadcastAppliance: con `on` y el "
                   "`codigo` que muestra el otro en su prueba de sala con varias máquinas (BAGEN1.…), lanza hinchada acá "
                   "y le manda parte de los participantes de prueba; termina sola cuando termina la prueba del otro. "
                   "`on: false` deja de generar. Carga esta máquina: confirmalo con el operador.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"on"}},
                         {"properties",
                          {{"on", {{"type", "boolean"}}},
                           {"codigo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el código del otro equipo (BAGEN1.…)")}}}}}};
        t.run = [&ps](const json& a, Done done) {
            if (!a.value("on", false)) {
                ps.dejarDeGenerar();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Dejando de generar.")).data(ps.estadoJson(false).value("generando", json::object())));
            }
            const QString e = ps.generarPara(QString::fromStdString(a.value("codigo", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Generando para el otro equipo. Seguilo con prueba_sala_estado (`generando`)."))
                     .data(ps.estadoJson(false).value("generando", json::object())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "prueba_sala_informe";
        t.title = ba::i18n::traducir("mcp", "El informe de la última prueba de sala");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El informe completo de la última prueba de sala de esta sesión: el resumen, la serie (de a 5 s), "
                   "cada participante (cuánto tardó en tener video, sus cortes, la HD) y dónde quedó en el evento.");
        t.run = [&ps](const json&, Done done) {
            const json inf = ps.ultimoInformeJson();
            if (inf.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "Todavía no terminó ninguna prueba de sala en esta sesión.")));
            done(ToolResult().data(inf));
        };
        core_.addTool(std::move(t));
    }
    // --- El historial de pruebas de la máquina (Pruebas → Historial, sala/HistorialPruebas) ---
    {
        Tool t;
        t.name = "pruebas_historial";
        t.title = ba::i18n::traducir("mcp", "El historial de pruebas de esta máquina");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Todas las pruebas de esta máquina, lo más nuevo primero: prueba de sala, capacidad (este equipo, "
                   "otro equipo, la recibida desde otro, la conexión) y prueba de asamblea. Cada una con la fecha, el tipo, "
                   "la máquina y la remota o las generadoras, cuántos participantes, la capacidad con margen y la máxima, lo "
                   "que limitó, qué se decodificó y la versión del programa. Para una entera: pruebas_ver; para dos lado a "
                   "lado: `comparar` con dos archivos. `csv`: la lista en CSV.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"limite", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 50}}},
                           {"comparar", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 2}, {"maxItems", 2},
                                         {"description", QT_TRANSLATE_NOOP("mcp", "dos `archivo` de la lista: sus números lado a lado")}}},
                           {"csv", {{"type", "boolean"}}}}}};
        t.untrustedOutput = true; // los nombres de las otras máquinas llegan por la red
        t.run = [&ps](const json& a, Done done) {
            if (a.contains("comparar") && a["comparar"].is_array() && a["comparar"].size() == 2) {
                const json c = ps.compararPruebas(a["comparar"][0].get<std::string>(), a["comparar"][1].get<std::string>());
                if (!c.is_array())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "No está esa prueba en el historial (pruebas_historial).")));
                ToolResult r;
                r.data(json{{"comparacion", c}});
                r.untrusted = true;
                return done(r);
            }
            ToolResult r;
            r.data(ps.historialPruebas(a.value("limite", 50), a.value("csv", false)));
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pruebas_ver";
        t.title = ba::i18n::traducir("mcp", "Una prueba del historial, entera");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Una prueba del historial de esta máquina, entera: los parámetros, la ficha de cada máquina (la local y "
                   "la remota o las generadoras: sistema, CPU, núcleos, memoria, GPU, la red y si es Wi-Fi o cable), los "
                   "resultados, las estadísticas y las series (CPU, memoria, red, grilla, audio, de los dos lados). Sin "
                   "credenciales ni códigos. `archivo` sale de pruebas_historial.");
        t.inputSchema = {{"type", "object"}, {"required", {"archivo"}}, {"properties", {{"archivo", {{"type", "string"}}}}}};
        t.untrustedOutput = true;
        t.run = [&ps](const json& a, Done done) {
            const json j = ps.verPrueba(a.value("archivo", std::string()));
            if (!j.is_object()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No está esa prueba en el historial (pruebas_historial).")));
            ToolResult r;
            r.data(j);
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
