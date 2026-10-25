// ============================================================================
// src/app/mcp/McpEquiposTools.cpp — los equipos, para el asistente
//
// Herramientas genéricas sobre dispositivos::Dispositivos: buscar, listar, ver
// y correr una acción. Las acciones NO se escriben acá: se registran con sus
// parámetros en dispositivos/Registro.cpp y aparecen solas (en la descripción
// de equipo_accion y en lo que devuelve cada equipo).
// Y lo mismo que hace el operador en «Dispositivos y equipos» con los de audio
// y video de la máquina: ponerles nombre (equipo_renombrar), configurarlos
// (equipo_configurar) y olvidar uno que ya no está (equipo_olvidar).
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
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
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
    json c{{"id", e.value("id", "")},
           {"tipo", e.value("tipo", "")},
           {"nombre", e.value("nombre", "")},
           {"detalle", e.value("detalle", "")},
           {"conectado", e.value("conectado", true)},
           {"estado", e.value("estado", "conectado")},
           {"acciones", acc}};
    // El del sistema, solo si el operador le puso otro (si no, es el mismo `nombre`).
    if (e.value("nombre_propio", false)) c["tecnico"] = e.value("tecnico", "");
    if (!e.value("valor", "").empty()) c["valor"] = e.value("valor", "");
    if (e.contains("config") && e["config"].is_object() && !e["config"].empty()) c["config"] = e["config"];
    return c;
}
} // namespace

void McpService::registerEquiposTools() {
    dispositivos::Dispositivos* dd = vm_.equipos();
    if (dd == nullptr) return;
    const QPointer<dispositivos::Dispositivos> d = dd;
    core_.addCategory(
        "equipos", "Dispositivos y equipos",
        "Detectar equipos (red local, cámaras, micrófonos y placas de sonido de la máquina, NDI, USB/HID/serie) y "
        "hacerles cosas: ficha, funciones ONVIF, operaciones ONVIF crudas, claves, agregarlos como entrada, PTZ, "
        "explorar UVC… Los de audio y video tienen el nombre que les puso el operador (de la máquina, no del "
        "evento) y si están conectados; los que ya se vieron y hoy no están salen como ausentes.");
    const json id = {
        {"type", "string"},
        {"description",
         "El id del equipo (de equipos_listar: «red:192.168.1.90», «local:video:<id de la cámara>», "
         "«local:audio:<id del micrófono>», «local:salida:<id>», «ndi:<emisor>»…)."}};
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
        t.description = "Lo que ya encontraron los detectores (sin volver a buscar) y los dispositivos de audio y "
                        "video que la máquina ya vio aunque hoy no estén (`conectado` false, `estado` «ausente»; "
                        "«sin_buscar» si su detector todavía no buscó). Cada uno con el `nombre` que le puso el "
                        "operador (y el del sistema en `tecnico`) y su `valor` (lo que guarda una entrada). "
                        "Filtrable por `tipo` (camara_local, camara_red, ndi, microfono, salida_audio, equipo_red, "
                        "usb, hid, serie) o `buscar` (texto, también el nombre técnico). Para el detalle de uno: "
                        "equipo_ver; para ponerle nombre: equipo_renombrar.";
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
                              return done(ToolResult::error(e.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
                          }
                          done(ToolResult().data(aJ(r)));
                      });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_renombrar";
        t.title = "Ponerle nombre a un dispositivo";
        t.category = "equipos";
        t.description =
            "Le pone a un dispositivo de audio o video (cámara, capturadora, micrófono, placa de sonido, NDI, cámara "
            "de red) el nombre con que lo conoce el operador («Cámara del fondo», «Mic del atril»). Es de la "
            "MÁQUINA (vale para todos los eventos) y se guarda con el id estable del equipo, así que sigue aunque "
            "se lo reenchufe. Las entradas, el mezclador y las salidas lo muestran con ese nombre; una entrada "
            "nueva se llama así. `nombre` vacío: vuelve al del sistema. Dos dispositivos no pueden llamarse igual. "
            "No renombra entradas ya creadas (para eso, el nombre de la fuente).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "nombre"}},
                         {"properties", {{"id", id}, {"nombre", {{"type", "string"}}}}}};
        t.examples = {R"({"id": "local:audio:BuiltInMicrophoneDevice", "nombre": "Mic del atril"})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            const QString err = d->renombrar(id, QString::fromStdString(a.value("nombre", std::string())));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(corto(d->equipo(id))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_configurar";
        t.title = "Configurar un dispositivo";
        t.category = "equipos";
        t.description =
            "La configuración de un dispositivo de audio o video de la máquina, que se mezcla con la que tiene "
            "(una clave en null o \"\" se borra): `nota` (texto libre: dónde está, para qué es) y, en una "
            "cámara (camara_local), `modo` = el formato preferido (un `valor` de video_devices) que usa una "
            "entrada nueva de esa cámara si no se le pide otro.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "config"}},
                         {"properties", {{"id", id}, {"config", {{"type", "object"}}}}}};
        t.examples = {R"({"id": "local:video:0x1420000046d0825", "config": {"nota": "trípode del fondo"}})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            const QString err = d->configurar(id, aV(a.value("config", json::object())).toMap());
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(corto(d->equipo(id))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_olvidar";
        t.title = "Olvidar un dispositivo que ya no está";
        t.category = "equipos";
        t.destructive = true;
        t.description = "Borra de la máquina un dispositivo AUSENTE (su nombre y su configuración): deja de "
                        "aparecer en la lista. Uno conectado no se olvida (se lo volvería a ver al toque).";
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error("los equipos no están disponibles"));
            const QString err = d->olvidar(QString::fromStdString(a.value("id", std::string())));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok("Olvidado."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
