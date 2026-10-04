// ============================================================================
// src/app/mcp/McpEquiposTools.cpp — los equipos, para el asistente
//
// Cuatro herramientas genéricas sobre dispositivos::Dispositivos: buscar,
// listar, ver y correr una acción. Las acciones NO se escriben acá: se
// registran con sus parámetros en dispositivos/Registro.cpp y aparecen solas
// (en la descripción de equipo_accion y en lo que devuelve cada equipo).
// ============================================================================

#include "dispositivos/Dispositivos.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#include <QJsonDocument>
#include <QPointer>

namespace ba::app::mcp {

namespace {
json aJ(const QVariant& v) {
    return json::parse(
        QJsonDocument::fromVariant(QVariantMap{{"v", v}}).toJson(QJsonDocument::Compact).toStdString(),
        nullptr, false)["v"];
}
QVariant aV(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump()))
        .toVariant()
        .toMap()
        .value("v");
}
/// Un equipo en corto: sin sus datos crudos, con sus acciones por id.
json corto(const QVariant& v) {
    json e = aJ(v);
    json acc = json::array();
    for (const auto& a : e.value("acciones", json::array()))
        acc.push_back(a.value("id", ""));
    return {{"id", e.value("id", "")},
            {"tipo", e.value("tipo", "")},
            {"nombre", e.value("nombre", "")},
            {"detalle", e.value("detalle", "")},
            {"acciones", acc}};
}
} // namespace

void McpService::registerEquiposTools() {
    dispositivos::Dispositivos* dd = vm_.equipos();
    if (dd == nullptr) return;
    const QPointer<dispositivos::Dispositivos> d = dd;
    core_.addCategory(
        "equipos", "Equipos",
        "Detectar equipos (red local, cámaras y micrófonos de la máquina, USB/HID/serie) y hacerles cosas: "
        "ficha, "
        "funciones ONVIF, operaciones ONVIF crudas, claves, agregarlos como entrada, PTZ, explorar UVC…");
    const json id = {
        {"type", "string"},
        {"description", "El id del equipo (de equipos_listar: «red:192.168.1.90», «local:video:0»…)."}};
    {
        Tool t;
        t.name = "equipos_escanear";
        t.title = "Buscar equipos";
        t.category = "equipos";
        t.readOnly = true;
        t.openWorld = true;
        t.description = "Busca con un `detector` (" + d->resumenDetectores().toStdString() +
                        ") o con todos. Devuelve cada equipo con su id, tipo y las acciones que se le pueden "
                        "hacer (equipo_accion).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"detector", {{"type", "string"}}}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            d->escanear(QString::fromStdString(a.value("detector", std::string())), [done](QVariantMap r) {
                if (r.contains("error"))
                    return done(ToolResult::error(r.value("error").toString().toStdString()));
                json l = json::array();
                for (const QVariant& e : r.value("equipos").toList())
                    l.push_back(corto(e));
                done(ToolResult().data({{"detectores", aJ(r.value("detectores"))}, {"equipos", l}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipos_listar";
        t.title = "Los equipos encontrados";
        t.category = "equipos";
        t.readOnly = true;
        t.description = "Lo que ya encontraron los detectores (sin volver a buscar), filtrable por `tipo` "
                        "(camara_red, equipo_red, "
                        "camara_local, microfono, usb, hid, serie) o `buscar` (texto). Para el detalle de "
                        "uno: equipo_ver.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}}}, {"buscar", {{"type", "string"}}}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            json l = json::array();
            for (const QVariant& e : d->equipos(QString::fromStdString(a.value("tipo", std::string())),
                                                QString::fromStdString(a.value("buscar", std::string()))))
                l.push_back(corto(e));
            done(ToolResult().data({{"equipos", l}, {"detectores", aJ(d->detectoresQml())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_ver";
        t.title = "Todo sobre un equipo";
        t.category = "equipos";
        t.readOnly = true;
        t.description = "Un equipo con todos sus datos y sus acciones (con los parámetros de cada una).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            const QVariantMap e = d->equipo(QString::fromStdString(a.value("id", std::string())));
            if (e.isEmpty()) return done(ToolResult::error("no hay un equipo con ese id (equipos_listar)"));
            done(ToolResult().data(aJ(e)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_accion";
        t.title = "Hacerle algo a un equipo";
        t.category = "equipos";
        t.description = "Corre la `accion` del equipo `id` con sus `args`. Las que hay, por tipo: " +
                        d->resumenAcciones().toStdString() +
                        " (? = opcional; equipo_ver da la descripción de cada una). Nunca pases una clave: "
                        "usá «pedir_clave».";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"id", "accion"}},
            {"properties", {{"id", id}, {"accion", {{"type", "string"}}}, {"args", {{"type", "object"}}}}}};
        t.examples = {
            R"({"id": "red:192.168.1.90", "accion": "funciones_onvif"})",
            R"({"id": "red:192.168.1.90", "accion": "onvif", "args": {"servicio": "ptz", "cuerpo": "<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>"}})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            d->correr(QString::fromStdString(a.value("id", std::string())),
                      QString::fromStdString(a.value("accion", std::string())),
                      aV(a.value("args", json::object())).toMap(), [done](QVariantMap r) {
                          if (r.contains("error")) {
                              json e{{"error", r.value("error").toString().toStdString()}};
                              if (r.contains("datos")) e["datos"] = aJ(r.value("datos"));
                              return done(ToolResult::error(e.dump()));
                          }
                          done(ToolResult().data(aJ(r)));
                      });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
