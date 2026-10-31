// ============================================================================
// src/app/mcp/McpRegistroTools.cpp — el registro, para el asistente
//
// Es la herramienta que más cambia lo que el asistente puede hacer: sin esto,
// cuando algo falla, lo único que tiene es lo que el operador le cuente. Con
// esto puede mirar lo que pasó de verdad, en los tres procesos, filtrado.
//
// Y puede BAJAR EL NIVEL, probar, y volver a mirar, que es el bucle con el que
// se encuentra un problema que no se repite a pedido.
// ============================================================================
#include <algorithm>

#include "i18n/I18n.hpp"
#include "arranque/AppRuntime.hpp"
#include "platform/Paths.hpp"
#include "diag/LectorDeLogs.hpp"
#include "diag/Registro.hpp"
#include "mcp/McpService.hpp"
#include "sala/ControlSala.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/ExtensionesModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerRegistroTools() {
    core_.addCategory("registro", ba::i18n::traducir("mcp", "Registro"),
                      ba::i18n::traducir("mcp", "Lo que pasó en los tres procesos: leerlo, filtrarlo y decidir "
                                                "cuánto se registra."));

    StudioViewModel& vm = vm_;


    {
        Tool t;
        t.name = "log_nivel";
        t.title = ba::i18n::traducir("mcp", "Cuánto se registra");
        t.category = "registro";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Lee o cambia, en vivo, el nivel desde el que este proceso registra.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Bajarlo a `depuracion` o `traza` hace que se escriba MUCHO más (GStreamer solo ya es muy "
                    "hablador). Sirve para reproducir un problema con detalle, pero no para dejarlo así: "
                    "llena el disco y hace que lo importante se pierda entre miles de líneas.\n\n"
                    "El cambio queda anotado en el propio registro, con quién lo hizo, para que mañana "
                    "nadie se pregunte por qué el log está lleno de trazas.\n\n"
                    "`donde` elige el proceso: `app` (por defecto) o `gateway`. El del gateway tambien se "
                    "puede fijar al arrancar con la variable de entorno BA_LOG_NIVEL.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nivel",
               {{"type", "string"},
                {"enum", {"traza", "depuracion", "info", "aviso", "error", "fatal"}},
                {"description", QT_TRANSLATE_NOOP("mcp", "Ausente = solo informa el que hay.")}}},
              {"donde", {{"type", "string"}, {"enum", {"app", "gateway"}}, {"default", "app"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            // El gateway es otro proceso: se le pide por su API de control.
            if (a.value("donde", std::string("app")) == "gateway") {
                auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
                if (conf == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "El servidor no esta corriendo.")));
                if (!a.contains("nivel")) {
                    conf->control().nivelLog([done](const std::string& err, const json& r) {
                        if (!err.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "El servidor no responde.")));
                        done(ToolResult().data(json{{"nivel", r.value("nivel", std::string())}, {"donde", "gateway"}}));
                    });
                    return;
                }
                const std::string nivel = a["nivel"].get<std::string>();
                conf->control().setNivelLog(nivel, [done, nivel](const std::string& err, const json&) {
                    if (!err.empty()) return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "No se pudo cambiar el nivel del gateway: %1"), {err})));
                    done(ToolResult::ok(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", "El gateway ahora registra desde %1."), {nivel})));
                });
                return;
            }
            auto& reg = ba::diag::registro();
            if (!a.contains("nivel")) {
                return done(ToolResult().data(json{{"nivel", ba::diag::nombreDeNivel(reg.nivel())}}));
            }
            ba::diag::Nivel n{};
            if (!ba::diag::nivelDeNombre(a["nivel"].get<std::string>(), n))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "No conozco ese nivel.")));
            reg.setNivel(n);
            reg.linea(ba::diag::Nivel::Info, "registro", 0,
                      ba::i18n::llenar(ba::i18n::traducir("registro", "nivel de registro cambiado a %1 desde el asistente"),
                                       {ba::diag::nombreDeNivel(n)}));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Ahora se registra desde %1."),
                                                 {ba::diag::nombreDeNivel(n)})));
        };
        core_.addTool(std::move(t));
    }

    // --- Extensiones: UNA familia, no una herramienta por extension ---------
    // Cien extensiones instaladas cuestan lo mismo que una en el contexto del
    // modelo. Ver docs/funciones/EXTENSIONES.md: reflexion, no inventario.
    {
        Tool t;
        t.name = "ext_listar";
        t.title = ba::i18n::traducir("mcp", "Tarjetas de extension");
        t.category = "registro";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las extensiones (tarjetas QML del dashboard, en Extensiones/) con su estado: si les falta algo o si el "
                                                 "arranque las apagó.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Una extension es una carpeta con `extension.json` (nombre, descripcion) y un QML que se "
                    "dibuja como una tarjeta mas del dashboard. Las escribis vos: la carpeta esta en el "
                    "campo `carpeta` de la respuesta.\n\n"
                    "Como se escribe una: mira `Extensiones/reloj-de-aire/tarjeta.qml`, que es el ejemplo de "
                    "referencia y tiene arriba las reglas (la raiz se ajusta sola, `Studio` y `Theme` estan "
                    "disponibles, no importes lo que no esta en el bundle, y NUNCA un bucle largo: corre en "
                    "el hilo de la interfaz).\n\n"
                    "Despues de escribir una, `ext_listar` de nuevo: `problema` dice si le falta algo, y el "
                    "operador la agrega desde «+ Tarjeta» en el dashboard.\n\n"
                    "`apagada` en true significa que se llevo puesto el arranque dos veces y el programa "
                    "dejo de cargarla para poder abrir. Eso NO se arregla escribiendo de nuevo: hay que "
                    "arreglar el QML y prenderla con `ext_prender`.");
        t.run = [&vm](const json&, Done done) {
            auto* ex = static_cast<ExtensionesModel*>(vm.extensiones());
            if (ex == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay extensiones en esta corrida.")));
            json out = json::array();
            for (int i = 0; i < ex->rowCount(); ++i) {
                const QVariantMap f = ex->ficha(ex->data(ex->index(i), ExtensionesModel::IdRole).toString());
                out.push_back(json{{"id", f.value("id").toString().toStdString()},
                                   {"nombre", f.value("nombre").toString().toStdString()},
                                   {"descripcion", f.value("descripcion").toString().toStdString()},
                                   {"problema", f.value("problema").toString().toStdString()},
                                   {"apagada", f.value("apagada").toBool()}});
            }
            done(ToolResult().data(json{{"extensiones", out}, {"carpeta", ex->carpeta().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ext_prender";
        t.title = ba::i18n::traducir("mcp", "Prender una extension apagada");
        t.category = "registro";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Vuelve a cargar una extensión (tarjeta QML del dashboard) que el arranque apagó porque colgaba la app; "
                                                 "antes, arreglá el QML.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Arregla el QML PRIMERO. Si sigue colgando el arranque, se vuelve a apagar sola "
                    "y vas a haber gastado dos reinicios del programa.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [&vm](const json& a, Done done) {
            auto* ex = static_cast<ExtensionesModel*>(vm.extensiones());
            if (ex == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay extensiones en esta corrida.")));
            ex->prender(QString::fromStdString(a["id"].get<std::string>()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Prendida. Se carga de nuevo al reabrir la pagina o el programa.")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "log_marcar";
        t.title = ba::i18n::traducir("mcp", "Dejar una marca");
        t.category = "registro";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Escribe una línea propia (`texto`) en el registro de la app, para marcar desde dónde mirar después lo que "
                                                 "pasó.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Se pone ANTES de probar algo («voy a probar la camara 2»): después se busca esa marca "
                    "y todo lo que sigue es lo que pasó por eso. Es la forma barata de separar lo que "
                    "provocaste de lo que ya estaba pasando.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"texto", {{"type", "string"}}}}}, {"required", {"texto"}}};
        t.run = [](const json& a, Done done) {
            ba::diag::registro().linea(ba::diag::Nivel::Info, "marca", 0, a.value("texto", std::string()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Marca puesta.")));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
