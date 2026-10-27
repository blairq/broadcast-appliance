// ============================================================================
// src/app/mcp/McpMatrizTools.cpp — la matriz de mezcla, por MCP
//
// Lo mismo que Audio -> Matriz: fuentes (a la salida de sus filtros) × pistas
// de OBS y buses virtuales. Operan sobre el mismo objeto (audio/Matriz): lo
// que se cambia por acá se ve en la matriz en el acto, y al revés.
//
// En «audio_rack» y no en «audio»: el modelo reactivo se entrena con las
// fichas de su perfil (que incluye «audio») y una herramienta nueva ahí le
// cambiaría el prompt.
// ============================================================================

#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/AudioViewModel.hpp"
#include "audio/Matriz.hpp"
#include "audio/Mezclador.hpp"
#endif

namespace ba::app::mcp {

void McpService::registerMatrizTools() {
#if defined(BA_HAVE_CLAP)
    auto* m = qobject_cast<audio::Matriz*>(vm_.matriz());
    if (m == nullptr) return;
    const auto err = [](const QString& e) { return ToolResult::error(e.toStdString()); };

    if (auto* mz = qobject_cast<audio::Mezclador*>(vm_.mezcladorQml())) {
        Tool t;
        t.name = "mezclador_estado";
        t.title = ba::i18n::traducir("mcp", "El Mezclador: niveles de cada entrada y salida, y adónde va cada una");
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Lo mismo que muestra la ventana del Mezclador: por entrada (lo que SALE de ella, después de su "
            "rack, vaya a donde vaya) su pico, RMS y retención (2 s) por canal en dB, el fader, mute, escucha "
            "y a qué destinos va; por salida (Programa, pistas, buses, placa) su nivel; y el loudness del "
            "programa (EBU R128).");
        t.details = ba::i18n::traducir("mcp", "Niveles en dBFS, [L, R]; -100 = nada. Una entrada «sin salida» (envios vacío) suena pero no "
                    "va a ningún lado: suele ser un error de la matriz (audio_matriz_cruce la conecta). La "
                    "ventana se abre con ui_panel_window {panel: mezclador}.");
        t.run = [mz](const json&, Done done) {
            // Sin la ventana abierta no corre su temporizador: se mide una vez acá.
            if (!mz->activo()) mz->medirYa();
            done(ToolResult().data(mz->estado()));
        };
        core_.addTool(std::move(t));
    }
    if (auto* mz = qobject_cast<audio::Mezclador*>(vm_.mezcladorQml())) {
        Tool t;
        t.name = "mezclador_analisis_lsp";
        t.title = ba::i18n::traducir("mcp", "Abrir un analizador de LSP sobre una entrada");
        t.category = "audio_rack";
        t.description =
            ba::i18n::traducir("mcp", "Abre, sobre lo que sale de la `entrada`, el analizador de LSP `cual` con su interfaz (una ventana "
            "aparte para el operador): espectro (Spectrum Analyzer), fase (Phase Detector: L contra R), "
            "osciloscopio, latencia (Latency Meter) o perfil (Profiler: barrido, respuesta impulsiva y RT60; "
            "SUENA). Si ya está en su rack lo reusa; si no, lo agrega al final (deja pasar el audio sin tocarlo).");
        t.details = ba::i18n::traducir("mcp", "La interfaz de LSP es para el operador: sus gráficos no vuelven por acá (LSP no publica sus "
                    "medidores por CLAP). Para leer números usá mezclador_estado o audio_analyze.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "cual"}},
                         {"properties",
                          {{"entrada", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "una de mezclador_estado.entradas")}}},
                           {"cual", {{"type", "string"}, {"enum", {"espectro", "fase", "osciloscopio", "latencia", "perfil"}}}}}}};
        t.run = [mz](const json& a, Done done) {
            const QString e = mz->abrirAnalisisLsp(QString::fromStdString(a["entrada"].get<std::string>()),
                                                   QString::fromStdString(a["cual"].get<std::string>()));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Abierto: la interfaz de LSP está en una ventana aparte.")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "audio_matriz";
        t.title = ba::i18n::traducir("mcp", "La matriz de mezcla");
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Fuentes con audio (a la salida de sus filtros) y a qué buses van: las 6 pistas del motor "
            "(la 1 es el programa; nativas, sin retardo) y los buses virtuales (salidas a un "
            "dispositivo, entradas que vuelven como fuente). Con los envíos y sus ajustes.");
        t.run = [m](const json&, Done done) {
            m->refrescar();
            json d = m->estado();
            // Dónde está el audio: lo que entrega cada fila, lo que pasa por cada bus y si
            // los dispositivos del equipo se abrieron (y si no, por qué).
            d["diagnostico"] = m->diagnostico();
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_matriz_cruce";
        t.title = ba::i18n::traducir("mcp", "Prender, apagar o ajustar un cruce");
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "`fuente` → `bus` (pista1..pista6, `escucha` = los auriculares del operador, o el id de un bus "
            "virtual): `activo` lo prende o apaga. "
            "En buses virtuales, además, `nivel_db` (-60 a +12), `pan` (-1 izq a 1 der) y `mute`; "
            "`ajustes: false` lo vuelve a 0 dB, centro.");
        t.details =
            ba::i18n::traducir("mcp", "Las pistas son la asignacion nativa de la fuente: solo prender o apagar, sin retardo. "
            "La pista 1 es lo que sale al AIRE: apagarla saca esa fuente del programa. Los buses virtuales "
            "pasan por una mezcla propia (~25 ms). Una entrada virtual no puede mandarse a su propio bus.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente", "bus"}},
                         {"properties",
                          {{"fuente", {{"type", "string"}}},
                           {"bus", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}},
                           {"nivel_db", {{"type", "number"}, {"minimum", -60}, {"maximum", 12}}},
                           {"pan", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"mute", {{"type", "boolean"}}},
                           {"ajustes", {{"type", "boolean"}}}}}};
        t.examples = {R"({"fuente": "Mic 1", "bus": "pista1", "activo": true})",
                      R"({"fuente": "Mic 1", "bus": "salida1", "activo": true, "nivel_db": -6})"};
        t.run = [m, err](const json& a, Done done) {
            const QString f = QString::fromStdString(a.value("fuente", std::string()));
            const QString b = QString::fromStdString(a.value("bus", std::string()));
            if (a.contains("activo")) {
                const QString e = m->setCruce(f, b, a.value("activo", true));
                if (!e.isEmpty()) return done(err(e));
            }
            if (a.contains("ajustes") && !a.value("ajustes", true)) m->setAjustes(f, b, false);
            if (a.contains("nivel_db") || a.contains("pan") || a.contains("mute")) {
                const QVariantMap c = m->cruces().value(f + "|" + b).toMap();
                if (!c.value("activo").toBool())
                    return done(
                        ToolResult::error(ba::i18n::traducir("mcp", "Ese cruce no está activo (o es una pista nativa, sin ajustes).")));
                m->ajustar(f, b, a.value("nivel_db", c.value("nivelDb").toDouble()),
                           a.value("pan", c.value("pan").toDouble()),
                           a.value("mute", c.value("mute").toBool()));
            }
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")).data(m->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar una entrada o salida virtual");
        t.category = "audio_rack";
        t.description = ba::i18n::traducir("mcp", "`tipo` \"salida\": un bus que sale por un dispositivo (`dispositivo`: id de "
                        "audio_dispositivos). `tipo` \"entrada\": un bus cuya mezcla vuelve como una fuente "
                        "nueva (aparece en Audio con su menú de filtros). `nombre` opcional.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"tipo"}},
                         {"properties",
                          {{"tipo", {{"type", "string"}, {"enum", {"entrada", "salida"}}}},
                           {"nombre", {{"type", "string"}}},
                           {"dispositivo", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            const QString e =
                a.value("tipo", std::string()) == "entrada"
                    ? m->agregarEntradaVirtual(nombre)
                    : m->agregarSalidaVirtual(nombre,
                                              QString::fromStdString(a.value("dispositivo", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Agregado.")).data(m->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_quitar";
        t.title = ba::i18n::traducir("mcp", "Quitar un bus virtual");
        t.category = "audio_rack";
        t.destructive = true;
        t.description = ba::i18n::traducir("mcp", "Quita una entrada o salida virtual (y sus envíos). Las pistas no se quitan.");
        t.inputSchema = {
            {"type", "object"}, {"required", {"bus"}}, {"properties", {{"bus", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e = m->quitarBus(QString::fromStdString(a.value("bus", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_renombrar";
        t.title = ba::i18n::traducir("mcp", "Renombrar un bus o una pista");
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Cambia el nombre de una pista (pista1..pista6) o de un bus virtual. Una entrada virtual "
            "cambia también el nombre de su fuente.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"bus", "nombre"}},
                         {"properties", {{"bus", {{"type", "string"}}}, {"nombre", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e = m->renombrar(QString::fromStdString(a.value("bus", std::string())),
                                           QString::fromStdString(a.value("nombre", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Renombrado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bus_dispositivo";
        t.title = ba::i18n::traducir("mcp", "A qué dispositivo sale una salida virtual");
        t.category = "audio_rack";
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Cambia el dispositivo de una salida virtual (`dispositivo`: id de audio_dispositivos).");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"bus", "dispositivo"}},
            {"properties", {{"bus", {{"type", "string"}}}, {"dispositivo", {{"type", "string"}}}}}};
        t.run = [m, err](const json& a, Done done) {
            const QString e =
                m->setDispositivo(QString::fromStdString(a.value("bus", std::string())),
                                  QString::fromStdString(a.value("dispositivo", std::string())));
            if (!e.isEmpty()) return done(err(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_dispositivos";
        t.title = ba::i18n::traducir("mcp", "Dispositivos de salida del equipo");
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Las salidas de audio del equipo (placas, auriculares, cables virtuales): id y nombre, "
            "para las salidas virtuales de la matriz.");
        t.run = [m](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : m->dispositivos())
                out.push_back({{"id", v.toMap().value("id").toString().toStdString()},
                               {"nombre", v.toMap().value("nombre").toString().toStdString()}});
            done(ToolResult().data(json{{"dispositivos", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_matriz_equipo";
        t.title = ba::i18n::traducir("mcp", "Entradas y salidas del equipo en la matriz");
        t.category = "audio_rack";
        t.description =
            ba::i18n::traducir("mcp", "Lo enchufado (bocinas, monitores con audio, placas USB) en la matriz: las salidas, una columna "
            "por par de canales (id fis:...); las entradas, una fila por canal. Sin argumentos lo lista. "
            "`mostrar` (true/false) muestra u oculta todo el equipo; `dispositivo` (id) con `visible` "
            "muestra u oculta uno (lo oculto no se abre).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"mostrar", {{"type", "boolean"}}},
                           {"dispositivo", {{"type", "string"}}},
                           {"visible", {{"type", "boolean"}}}}}};
        t.run = [m](const json& a, Done done) {
            if (a.contains("mostrar")) m->setMostrarEquipo(a.value("mostrar", true));
            if (a.contains("dispositivo"))
                m->setVisibleDispositivo(QString::fromStdString(a.value("dispositivo", std::string())),
                                         a.value("visible", true));
            json devs = json::array();
            for (const QVariant& v : m->equipo()) {
                const QVariantMap d = v.toMap();
                devs.push_back({{"id", d.value("id").toString().toStdString()},
                                {"nombre", d.value("nombre").toString().toStdString()},
                                {"tipo", d.value("tipo").toString().toStdString()},
                                {"canales", d.value("canales").toInt()},
                                {"visible", d.value("visible").toBool()},
                                {"abierto", d.value("abierto").toBool()},
                                {"ya_en_audio", d.value("enApp").toBool()}});
            }
            done(ToolResult().data(json{{"mostrar", m->mostrarEquipo()},
                                        {"dispositivos", devs},
                                        {"matriz", m->estado().value("equipo", json::object())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_filtros_catalogo";
        t.title = ba::i18n::traducir("mcp", "Catalogo de filtros de audio (LSP)");
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Las familias de filtros LSP (compresor, compuerta, EQ...) en castellano: que hacen, para "
            "que sirven en vivo, si se recomiendan para vivo y sus variantes (id del plugin, mono/"
            "estereo, sidechain). `buscar` filtra por palabras (castellano o ingles), `categoria` y "
            "`solo_vivo`. El id de una variante es el que se agrega al rack de una fuente.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"buscar", {{"type", "string"}}},
                           {"categoria", {{"type", "string"}}},
                           {"solo_vivo", {{"type", "boolean"}}}}}};
        t.examples = {R"({"buscar": "ruido"})", R"({"categoria": "Dinámica", "solo_vivo": true})"};
        auto* au = static_cast<audio::AudioViewModel*>(vm_.audio());
        t.run = [au](const json& a, Done done) {
            if (au == nullptr)
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Esta compilacion no tiene los modulos de audio.")));
            const QString q = QString::fromStdString(a.value("buscar", std::string())).toLower();
            const QString c = QString::fromStdString(a.value("categoria", std::string()));
            const bool vivo = a.value("solo_vivo", false);
            json out = json::array();
            for (const QVariant& v : au->catalogoLsp()) {
                const QVariantMap f = v.toMap();
                if (!f.value("esFiltro").toBool()) continue;
                if (!c.isEmpty() && f.value("categoria").toString() != c) continue;
                if (vivo && !f.value("vivo").toBool()) continue;
                const QString h =
                    (f.value("nombre").toString() + " " + f.value("clave").toString() + " " +
                     f.value("etiquetas").toStringList().join(" ") + " " + f.value("descripcion").toString() +
                     " " + f.value("descripcionOriginal").toString())
                        .toLower();
                bool ok = true;
                for (const QString& p : q.split(' ', Qt::SkipEmptyParts))
                    if (!h.contains(p)) ok = false;
                if (!ok) continue;
                json variantes = json::array();
                for (const QVariant& x : f.value("variantes").toList())
                    variantes.push_back({{"id", x.toMap().value("id").toString().toStdString()},
                                         {"etiqueta", x.toMap().value("etiqueta").toString().toStdString()}});
                out.push_back({{"familia", f.value("clave").toString().toStdString()},
                               {"nombre", f.value("nombre").toString().toStdString()},
                               {"categoria", f.value("categoria").toString().toStdString()},
                               {"vivo", f.value("vivo").toBool()},
                               {"descripcion", f.value("descripcion").toString().toStdString()},
                               {"uso_en_vivo", f.value("usoVivo").toString().toStdString()},
                               {"variantes", variantes}});
            }
            done(ToolResult().data(json{{"familias", out}}));
        };
        core_.addTool(std::move(t));
    }
#endif
}

} // namespace ba::app::mcp
