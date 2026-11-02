// ============================================================================
// src/app/mcp/McpEnlaceTools.cpp — la velocidad del enlace por el túnel
//
// Las mismas operaciones que la tarjeta «Enlace» de la página Sala (ui/EnlaceViewModel):
// medir (con la misma confirmación si hay invitados o algo al aire), cancelar, el estado
// (en vivo y la última) y la cuenta de cuánto necesita el evento para N invitados.
// Ver docs/funciones/ENLACE.md.
// ============================================================================

#include "i18n/I18n.hpp"
#include "ipc/LugaresSala.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/EnlaceViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerEnlaceTools() {
    EnlaceViewModel& en = vm_.enlace();
    {
        Tool t;
        t.name = "enlace_medir";
        t.title = ba::i18n::traducir("mcp", "Medir el enlace por el túnel");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Mide el enlace por el túnel WireGuard hasta el eco del servidor (el camino real de los invitados): "
                   "RTT en reposo, subida y bajada UDP en escalones (2, 5, 10, 20… Mbps hasta `tope_kbps`, de fábrica "
                   "100 Mbps; para cuando el enlace se llena) con el RTT, el jitter y la pérdida con carga. Con "
                   "`tasa_kbps`, una sola tasa fija. Tarda ~15-30 s; seguila con enlace_estado.");
        t.details = QT_TRANSLATE_NOOP(
            "mcp", "Necesita el túnel WireGuard y el eco instalado en el servidor (si no contesta, enlace_estado lo "
                   "dice; el instalador está en la tarjeta «Enlace» de la página Sala). La medición CARGA el enlace: "
                   "con invitados conectados o algo al aire no arranca sin `confirmar` (preguntale al operador) y "
                   "entonces mide suave (hasta 5 Mbps). Con una prueba de sala corriendo, no. Al terminar, el "
                   "resultado va a la lista de chequeo del evento abierto (ítem «enlace»).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"confirmar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "el operador aceptó medir con invitados o al aire")}}},
                           {"tope_kbps", {{"type", "integer"}, {"minimum", 500}, {"maximum", 1000000}}},
                           {"tasa_kbps", {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}}}}};
        t.run = [&en](const json& a, Done done) {
            const QString e = en.medir(a.value("confirmar", false), a.value("tope_kbps", 0), a.value("tasa_kbps", 0));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Midiendo el enlace. Seguilo con enlace_estado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "enlace_cancelar";
        t.title = ba::i18n::traducir("mcp", "Cancelar la medición del enlace");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Corta la medición del enlace que esté corriendo.");
        t.run = [&en](const json&, Done done) {
            if (!en.midiendo()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay una medición del enlace en curso.")));
            en.cancelar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Cancelando la medición del enlace.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "enlace_estado";
        t.title = ba::i18n::traducir("mcp", "Cómo está el enlace");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El estado de la medición del enlace (sin túnel, libre o midiendo, con la fase y el avance), lo "
                   "parcial mientras mide, la última medición (reposo, subida y bajada con sus escalones; si fue cancelada, también la última buena, con la que se hace la cuenta), el riesgo "
                   "de medir ahora y la cuenta para el cupo de la sala (enlace_necesita).");
        t.run = [&en](const json&, Done done) { done(ToolResult().data(en.estadoJson())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "enlace_necesita";
        t.title = ba::i18n::traducir("mcp", "Cuánto enlace necesita el evento");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Cuánta subida y bajada hacen falta para `invitados` (de fábrica, el cupo de la sala) con la "
                   "grilla como está («alta definición hasta N»), adentro del túnel y con los encabezados de "
                   "WireGuard, y contra la última medición: veredicto (entra | justo | no_entra | sin_medir), por qué "
                   "y hasta cuántos invitados entran con margen.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"invitados", {{"type", "integer"}, {"minimum", 1}, {"maximum", ba::ipc::kMaxInvitados}}}}}};
        t.run = [&en](const json& a, Done done) { done(ToolResult().data(en.necesidadPara(a.value("invitados", en.invitados())))); };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
