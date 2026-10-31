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
// Y los DRIVERS de la máquina (dispositivos/Drivers): cuáles andan, cuáles están
// en cuarentena y por qué (drivers_listar), probar uno de nuevo (driver_probar)
// y volver a revisar todo (drivers_revisar): lo mismo que la sección «Drivers».
// ============================================================================

#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Drivers.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "i18n/I18n.hpp"
#include "ui/StudioViewModel.hpp"

#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

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
        "equipos", ba::i18n::traducir("mcp", "Dispositivos y equipos"),
        ba::i18n::traducir("mcp", "Detectar equipos (red local, cámaras, micrófonos y placas de sonido de la máquina, NDI, USB/HID/serie) y "
        "hacerles cosas: ficha, funciones ONVIF, operaciones ONVIF crudas, claves, agregarlos como entrada, PTZ, "
        "explorar UVC… Los de audio y video tienen el nombre que les puso el operador (de la máquina, no del "
        "evento) y si están conectados; los que ya se vieron y hoy no están salen como ausentes."));
    const json id = {
        {"type", "string"},
        {"description",
         ba::i18n::traducir("mcp", "El id del equipo (de equipos_listar: «red:192.168.1.90», «local:video:<id de la cámara>», "
         "«local:audio:<id del micrófono>», «local:salida:<id>», «ndi:<emisor>»…).")}};
    {
        Tool t;
        t.name = "equipos_escanear";
        t.title = ba::i18n::traducir("mcp", "Buscar equipos");
        t.category = "equipos";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description = ba::i18n::llenar(
            ba::i18n::traducir("mcp", "Busca con un `detector` (%1) o con todos. Devuelve cada equipo con su id, tipo y "
                                      "las acciones que se le pueden hacer (equipo_accion)."),
            {d->resumenDetectores().toStdString()});
        t.inputSchema = {{"type", "object"}, {"properties", {{"detector", {{"type", "string"}}}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
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
        t.title = ba::i18n::traducir("mcp", "Los equipos encontrados");
        t.category = "equipos";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Lo que ya encontraron los detectores (sin volver a buscar) y los dispositivos de audio y "
                        "video que la máquina ya vio aunque hoy no estén (`conectado` false, `estado` «ausente»; "
                        "«sin_buscar» si su detector todavía no buscó). Cada uno con el `nombre` que le puso el "
                        "operador (y el del sistema en `tecnico`) y su `valor` (lo que guarda una entrada). "
                        "Filtrable por `tipo` (camara_local, camara_red, ndi, microfono, salida_audio, equipo_red, "
                        "usb, hid, serie) o `buscar` (texto, también el nombre técnico). Para el detalle de uno: "
                        "equipo_ver; para ponerle nombre: equipo_renombrar.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}}}, {"buscar", {{"type", "string"}}}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
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
        t.title = ba::i18n::traducir("mcp", "Todo sobre un equipo");
        t.category = "equipos";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Un equipo con todos sus datos y sus acciones (con los parámetros de cada una).");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
            const QVariantMap e = d->equipo(QString::fromStdString(a.value("id", std::string())));
            if (e.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay un equipo con ese id (equipos_listar)")));
            done(ToolResult().data(aJ(e)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "equipo_accion";
        t.title = ba::i18n::traducir("mcp", "Hacerle algo a un equipo");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::llenar(
            ba::i18n::traducir("mcp", "Corre la `accion` del equipo `id` con sus `args`. Las que hay, por tipo: %1 "
                                      "(? = opcional; equipo_ver da la descripción de cada una). Nunca pases una "
                                      "clave: usá «pedir_clave»."),
            {d->resumenAcciones().toStdString()});
        t.inputSchema = {
            {"type", "object"},
            {"required", {"id", "accion"}},
            {"properties", {{"id", id}, {"accion", {{"type", "string"}}}, {"args", {{"type", "object"}}}}}};
        t.examples = {
            R"({"id": "red:192.168.1.90", "accion": "funciones_onvif"})",
            R"({"id": "red:192.168.1.90", "accion": "onvif", "args": {"servicio": "ptz", "cuerpo": "<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>"}})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
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
        t.title = ba::i18n::traducir("mcp", "Ponerle nombre a un dispositivo");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Le pone a un dispositivo de audio o video (cámara, capturadora, micrófono, placa de sonido, NDI, cámara "
            "de red) el nombre con que lo conoce el operador («Cámara del fondo», «Mic del atril»). Es de la "
            "MÁQUINA (vale para todos los eventos) y se guarda con el id estable del equipo, así que sigue aunque "
            "se lo reenchufe. Las entradas, el mezclador y las salidas lo muestran con ese nombre; una entrada "
            "nueva se llama así. `nombre` vacío: vuelve al del sistema. Dos dispositivos no pueden llamarse igual. "
            "No renombra entradas ya creadas (para eso, el nombre de la fuente).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "nombre"}},
                         {"properties", {{"id", id}, {"nombre", {{"type", "string"}}}}}};
        t.examples = {R"({"id": "local:audio:BuiltInMicrophoneDevice", "nombre": "Mic del atril"})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
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
        t.title = ba::i18n::traducir("mcp", "Configurar un dispositivo");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "La configuración de un dispositivo de audio o video de la máquina, que se mezcla con la que tiene "
            "(una clave en null o \"\" se borra): `nota` (texto libre: dónde está, para qué es) y, en una "
            "cámara (camara_local), `modo` = el formato preferido (un `valor` de video_devices) que usa una "
            "entrada nueva de esa cámara si no se le pide otro.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id", "config"}},
                         {"properties", {{"id", id}, {"config", {{"type", "object"}}}}}};
        t.examples = {R"({"id": "local:video:0x1420000046d0825", "config": {"nota": "trípode del fondo"}})"};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
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
        t.title = ba::i18n::traducir("mcp", "Olvidar un dispositivo que ya no está");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Borra de la máquina un dispositivo AUSENTE (su nombre y su configuración): deja de "
                        "aparecer en la lista. Uno conectado no se olvida (se lo volvería a ver al toque).");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", id}}}};
        t.run = [d](const json& a, Done done) {
            if (!d) return done(ToolResult::error(ba::i18n::traducir("mcp", "los equipos no están disponibles")));
            const QString err = d->olvidar(QString::fromStdString(a.value("id", std::string())));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Olvidado.")));
        };
        core_.addTool(std::move(t));
    }
    registerDriversTools();
}

void McpService::registerDriversTools() {
    dispositivos::Dispositivos* dd = vm_.equipos();
    auto* drv = dd != nullptr ? qobject_cast<dispositivos::Drivers*>(dd->drivers()) : nullptr;
    if (drv == nullptr) return;
    const QPointer<dispositivos::Drivers> dr = drv;
    const auto estado = [](dispositivos::Drivers& x) {
        return json{{"resumen", x.resumen().toStdString()},
                    {"maquina", x.maquina().toStdString()},
                    {"maquina_conocida", x.maquinaConocida()},
                    {"revisando", x.revisando()},
                    {"fallas_para_cuarentena", x.umbral()},
                    {"drivers", aJ(x.lista())}};
    };
    {
        Tool t;
        t.name = "drivers_listar";
        t.title = ba::i18n::traducir("mcp", "Los drivers y su cuarentena");
        t.category = "equipos";
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Los drivers y proveedores de dispositivos de ESTA máquina (GStreamer: WASAPI, Media Foundation, Core "
            "Audio, AVFoundation, Pulse, V4L2, DeckLink, los de plugins; cada driver ASIO en Windows) con su "
            "`estado`: anda, falla (falló menos veces que el tope), cuarentena (falló `fallas_para_cuarentena` "
            "veces seguidas: no se vuelve a cargar solo), sin_probar, probando o ausente (no está instalado). Con "
            "`motivo` (el último, legible), `ultima_falla` (error | tiempo | caida), `fallas` seguidas y `detalle` "
            "(canales, dispositivos) del último éxito. `resumen`: «máquina conocida: N drivers listos, revisando "
            "novedades…» o «máquina nueva: revisando todo (k de n)…». Para sacar uno de la cuarentena: "
            "driver_probar.");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [dr, estado](const json&, Done done) {
            if (!dr) return done(ToolResult::error(ba::i18n::traducir("mcp", "los drivers no están disponibles")));
            done(ToolResult().data(estado(*dr)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "driver_probar";
        t.title = ba::i18n::traducir("mcp", "Probar un driver de nuevo");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "«Probar de nuevo»: saca de la cuarentena al driver `id` (de drivers_listar) para UNA prueba, en un "
            "proceso aparte con tiempo límite (si se cuelga o se cae, la app sigue). Si anda, sale de la "
            "cuarentena y se vuelve a usar; si no, se queda. Espera el resultado (hasta ~1 minuto). Sirve después "
            "de arreglar la causa (p. ej. arrancar el servidor de JACK, actualizar el driver).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties", {{"id", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "«asio:{CLSID}», «gst:<proveedor>»")}}}}}};
        t.examples = {R"({"id": "gst:decklinkdeviceprovider"})"};
        t.run = [dr](const json& a, Done done) {
            if (!dr) return done(ToolResult::error(ba::i18n::traducir("mcp", "los drivers no están disponibles")));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            const QString err = dr->reintentar(id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            // Se espera a que deje de estar «probando» (lo dice el modelo), con un tope.
            auto* reloj = new QTimer(dr.data());
            auto vueltas = std::make_shared<int>(0);
            QObject::connect(reloj, &QTimer::timeout, reloj, [dr, id, reloj, vueltas, done]() {
                if (!dr) return reloj->deleteLater();
                const QVariantMap u = dr->uno(id);
                if (u.value("estado").toString() == "probando" && ++*vueltas < 300) return;
                reloj->stop();
                reloj->deleteLater();
                done(ToolResult().data(aJ(u)));
            });
            reloj->start(200);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "drivers_revisar";
        t.title = ba::i18n::traducir("mcp", "Revisar todos los drivers");
        t.category = "equipos";
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Vuelve a probar TODOS los drivers instalados (los que andan también; los de cuarentena no: "
                        "esos, de a uno con driver_probar), en segundo plano y fuera de la app. El avance, en "
                        "drivers_listar (`revisando`, `resumen`).");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [dr, estado](const json&, Done done) {
            if (!dr) return done(ToolResult::error(ba::i18n::traducir("mcp", "los drivers no están disponibles")));
            dr->revisarTodo();
            done(ToolResult().data(estado(*dr)));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
