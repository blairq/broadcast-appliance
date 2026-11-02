// ============================================================================
// src/app/mcp/McpPruebaSalaTools.cpp — la prueba de sala (simulacro antes del evento)
//
// Las mismas operaciones que la tarjeta «Prueba de sala» de la página Sala
// (ui/PruebaSalaViewModel): arrancar (con la guardia), terminar, la entrada directa
// para todos, el estado en vivo, revisar la máquina y el informe de la última.
// Ver docs/funciones/PRUEBA-SALA.md.
// ============================================================================

#include "i18n/I18n.hpp"
#include "ipc/LugaresSala.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PruebaSalaViewModel.hpp"
#include "ui/StudioViewModel.hpp"

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
                   "URL y la credencial de prueba para un generador externo.");
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
                                       {"description", QT_TRANSLATE_NOOP("mcp", "cuántos hablan a la vez (0: cada uno a su ritmo)")}}}}}};
        t.run = [&ps](const json& a, Done done) {
            const bool hinchada = a.value("hinchada", ps.hinchadaDisponible());
            const QString e = ps.arrancar(a.value("participantes", 10), a.value("minutos", 5), hinchada, a.value("hablan", 2));
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
                   "fallidos, cortes, la HD, el ancho de banda, la CPU de la app y del gateway, la memoria) y el "
                   "resumen de la última.");
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
                           {"hinchada", {{"type", "boolean"}}}}}};
        t.run = [&ps](const json& a, Done done) {
            ps.revisarMaquina(a.value("participantes", 10), a.value("hinchada", ps.hinchadaDisponible()));
            done(ToolResult().data(ps.estadoJson(false).value("maquina", json::object())));
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
}

} // namespace ba::app::mcp
