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
    core_.addCategory("registro", "Registro",
                      "Lo que pasó en los tres procesos: leerlo, filtrarlo y decidir cuánto se "
                      "registra.");

    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;

    {
        Tool t;
        t.name = "log_leer";
        t.title = "Leer el registro";
        t.category = "registro";
        t.readOnly = true;
        t.description = "Las últimas líneas del registro, de los tres procesos, con filtros.";
        t.details = "Cada línea trae hora (UTC, en milisegundos desde la época), nivel, módulo, código y "
                    "descripción.\n\n"
                    "Los NIVELES, de menos a más grave: traza, depuracion, info, aviso, error, fatal. "
                    "`minimo` filtra de ese para arriba; el que trae por defecto es `aviso`, que es lo que "
                    "conviene mirar primero cuando algo anda mal.\n\n"
                    "Los MÓDULOS que suele haber: `app` (lo que no dice de dónde viene: printf y salida "
                    "cruda), `qml` (lo que se rompe en la interfaz), `qt`, `obs` (el motor de emisión, muy "
                    "hablador), `gateway` (el Proceso B: sala, WebRTC, túnel), `launcher`, `graficos`, "
                    "`registro` y `marca`. `log_leer` sin filtros te devuelve cuáles hay ahora.\n\n"
                    "Cómo usarlo bien cuando algo falla:\n"
                    "  1. Primero sin filtros y con `minimo: \"aviso\"`: si hay un error, está ahí.\n"
                    "  2. Si no aparece nada, `minimo: \"info\"` y el módulo que sospeches.\n"
                    "  3. Si sigue sin aparecer, `log_nivel` a depuracion o traza, hacé que el problema "
                    "pase otra vez, y volvé a leer. Acordate de dejar el nivel como estaba.\n\n"
                    "Ojo: lo que está por DEBAJO del nivel al que el proceso está registrando no se escribe "
                    "en ningún lado, así que no alcanza con pedirlo acá.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"minimo",
               {{"type", "string"},
                {"enum", {"traza", "depuracion", "info", "aviso", "error", "fatal"}},
                {"description", "Desde qué nivel. Por defecto, aviso."}}},
              {"modulo", {{"type", "string"}, {"description", "Uno solo. Vacío = todos."}}},
              {"busca", {{"type", "string"}, {"description", "Texto a buscar (sin distinguir mayúsculas)."}}},
              {"ultimosMin",
               {{"type", "integer"}, {"minimum", 1}, {"description", "Solo lo de los últimos N minutos."}}},
              {"cuantas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}}}};
        t.run = [&rt](const json& a, Done done) {
            ba::diag::Filtro f;
            f.minimo = ba::diag::Nivel::Aviso;
            if (a.contains("minimo")) {
                ba::diag::Nivel n{};
                if (ba::diag::nivelDeNombre(a["minimo"].get<std::string>(), n)) f.minimo = n;
            }
            if (a.contains("modulo") && a["modulo"].is_string() && !a["modulo"].get<std::string>().empty())
                f.componentes.push_back(a["modulo"].get<std::string>());
            f.busca = a.value("busca", std::string());
            f.cuantas = static_cast<std::size_t>(std::clamp(a.value("cuantas", 120), 1, 500));
            if (a.contains("ultimosMin")) {
                const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
                f.desdeMs = ahora - static_cast<std::int64_t>(a["ultimosMin"].get<int>()) * 60000LL;
            }

            const auto dir = ba::platform::dataRoot() / "Logs";
            json lineas = json::array();
            for (const auto& r : ba::diag::leerLogs(dir, f))
                lineas.push_back(json{{"ms", r.ms},
                                      {"nivel", ba::diag::nombreDeNivel(r.nivel)},
                                      {"modulo", r.componente},
                                      {"codigo", r.codigo},
                                      {"texto", r.texto}});
            json modulos = json::array();
            for (const std::string& m : ba::diag::componentesEnLogs(dir))
                modulos.push_back(m);

            done(ToolResult().data(
                json{{"lineas", lineas},
                     {"modulos", modulos},
                     {"nivelDelProceso", ba::diag::nombreDeNivel(ba::diag::registro().nivel())},
                     {"descartadasPorNivel", ba::diag::registro().descartadas()},
                     {"carpeta", dir.string()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "log_nivel";
        t.title = "Cuánto se registra";
        t.category = "registro";
        t.description = "Lee o cambia, en vivo, el nivel desde el que este proceso registra.";
        t.details = "Bajarlo a `depuracion` o `traza` hace que se escriba MUCHO más (GStreamer solo ya es muy "
                    "hablador). Sirve para reproducir un problema con detalle, pero no para dejarlo así: "
                    "llena el disco y hace que lo importante se pierda entre miles de líneas.\n\n"
                    "El cambio queda anotado en el propio registro, con quién lo hizo, para que mañana "
                    "nadie se pregunte por qué el log está lleno de trazas.\n\n"
                    "`donde` elige el proceso: `app` (por defecto) o `gateway`. El del gateway tambien se "
                    "puede fijar al arrancar con la variable de entorno BA_LOG_NIVEL.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nivel",
               {{"type", "string"},
                {"enum", {"traza", "depuracion", "info", "aviso", "error", "fatal"}},
                {"description", "Ausente = solo informa el que hay."}}},
              {"donde", {{"type", "string"}, {"enum", {"app", "gateway"}}, {"default", "app"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            // El gateway es otro proceso: se le pide por su API de control.
            if (a.value("donde", std::string("app")) == "gateway") {
                auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
                if (conf == nullptr) return done(ToolResult::error("La sala no esta corriendo."));
                if (!a.contains("nivel")) {
                    conf->control().nivelLog([done](const std::string& err, const json& r) {
                        if (!err.empty()) return done(ToolResult::error("La sala no responde."));
                        done(ToolResult().data(json{{"nivel", r.value("nivel", std::string())}, {"donde", "gateway"}}));
                    });
                    return;
                }
                const std::string nivel = a["nivel"].get<std::string>();
                conf->control().setNivelLog(nivel, [done, nivel](const std::string& err, const json&) {
                    if (!err.empty()) return done(ToolResult::error("No se pudo cambiar el nivel del gateway: " + err));
                    done(ToolResult::ok("El gateway ahora registra desde " + nivel + "."));
                });
                return;
            }
            auto& reg = ba::diag::registro();
            if (!a.contains("nivel")) {
                return done(ToolResult().data(json{{"nivel", ba::diag::nombreDeNivel(reg.nivel())}}));
            }
            ba::diag::Nivel n{};
            if (!ba::diag::nivelDeNombre(a["nivel"].get<std::string>(), n))
                return done(ToolResult::error("No conozco ese nivel."));
            reg.setNivel(n);
            reg.linea(ba::diag::Nivel::Info, "registro", 0,
                      std::string("nivel de registro cambiado a ") + ba::diag::nombreDeNivel(n) +
                          " desde el asistente");
            done(ToolResult::ok(std::string("Ahora se registra desde ") + ba::diag::nombreDeNivel(n) + "."));
        };
        core_.addTool(std::move(t));
    }

    // --- Extensiones: UNA familia, no una herramienta por extension ---------
    // Cien extensiones instaladas cuestan lo mismo que una en el contexto del
    // modelo. Ver docs/funciones/EXTENSIONES.md: reflexion, no inventario.
    {
        Tool t;
        t.name = "ext_listar";
        t.title = "Tarjetas de extension";
        t.category = "registro";
        t.readOnly = true;
        t.description = "Las tarjetas que viven en Extensiones/, con su estado.";
        t.details = "Una extension es una carpeta con `extension.json` (nombre, descripcion) y un QML que se "
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
                    "arreglar el QML y prenderla con `ext_prender`.";
        t.run = [&vm](const json&, Done done) {
            auto* ex = static_cast<ExtensionesModel*>(vm.extensiones());
            if (ex == nullptr) return done(ToolResult::error("No hay extensiones en esta corrida."));
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
        t.title = "Prender una extension apagada";
        t.category = "registro";
        t.description = "Vuelve a cargar una tarjeta que el arranque habia apagado.";
        t.details = "Arregla el QML PRIMERO. Si sigue colgando el arranque, se vuelve a apagar sola "
                    "y vas a haber gastado dos reinicios del programa.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [&vm](const json& a, Done done) {
            auto* ex = static_cast<ExtensionesModel*>(vm.extensiones());
            if (ex == nullptr) return done(ToolResult::error("No hay extensiones en esta corrida."));
            ex->prender(QString::fromStdString(a["id"].get<std::string>()));
            done(ToolResult::ok("Prendida. Se carga de nuevo al reabrir la pagina o el programa."));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "log_marcar";
        t.title = "Dejar una marca";
        t.category = "registro";
        t.description = "Escribe una línea propia en el registro.";
        t.details = "Se pone ANTES de probar algo («voy a probar la camara 2»): después se busca esa marca "
                    "y todo lo que sigue es lo que pasó por eso. Es la forma barata de separar lo que "
                    "provocaste de lo que ya estaba pasando.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"texto", {{"type", "string"}}}}}, {"required", {"texto"}}};
        t.run = [](const json& a, Done done) {
            ba::diag::registro().linea(ba::diag::Nivel::Info, "marca", 0, a.value("texto", std::string()));
            done(ToolResult::ok("Marca puesta."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
