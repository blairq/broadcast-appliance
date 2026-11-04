// ============================================================================
// src/app/mcp/McpMezclaTools.cpp — Herramientas MCP del motor: audio de la mezcla: volumen, silencio, niveles, grupos.
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "mcp/McpFamilia.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <fstream>
#include "arranque/RuntimeMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "audio/Matriz.hpp"
#include "ui/MezclaViewModel.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerMezclaTools() {
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;

    // --- Audio ---------------------------------------------------------------------
    {
        Tool t;
        t.name = "mixer_list";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Mezclador");
        t.category = "audio";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Foto de los niveles del mezclador, para leer. "
                        "Fuentes de audio con volumen (dB), silencio, si se esta escuchando por el monitor "
                        "(`escuchando`, ver audio_escuchar) y nivel pico actual (dBFS), mas el nivel del "
                        "programa. -100 = silencio; cerca de 0 = saturando. Cada salida tiene su mezcla: con `salida`, la de esa.");
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            json out = json::array();
            for (const std::string& n : rt.engine().audioSources()) {
                const float mul = rt.engine().sourceVolume(n, salida);
                out.push_back(json{{"name", n},
                                   {"volumeDb", mul > 0 ? 20.0 * std::log10(mul) : -100.0},
                                   {"muted", rt.engine().sourceMuted(n, salida)},
                                   // Cual se esta escuchando por el monitor
                                   // (audio_escuchar). Sin esto el PFL es
                                   // invisible: queda una fuente sonando en
                                   // los auriculares y nadie sabe cual.
                                   {"escuchando", rt.engine().sourceMonitoring(n)},
                                   {"pfl", rt.engine().pfl() == n && rt.engine().pflModo() == "pfl"},
                                   {"afl", rt.engine().pfl() == n && rt.engine().pflModo() == "afl"},
                                   {"peakDb", rt.niveles().picoEntrada(n)}});
            }
            // El del programa, de la última vuelta del reloj del motor (el canal de niveles):
            // sin robárselo al vúmetro de la vista.
            const auto [l, r] = rt.niveles().programa(salida);
            done(ToolResult().data(json{{"sources", out}, {"programPeakDb", {l, r}}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_set";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Volumen / silencio");
        t.category = "audio";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia el audio (mute del máster corta el sonido)");
        t.aireSi = AireSi::MasterOPrograma;
        t.reversible = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Silencia, sube o baja el audio de una fuente. Es con lo que se CORTA un acople, se baja lo que "
            "satura, se sube lo que no se escucha o se cierra un microfono que quedo abierto. Cambia el "
            "volumen (dB, 0 = sin cambio, negativo baja) y/o silencia una fuente de audio. En la mezcla de la "
            "`salida` (cada una tiene la suya; sin ella, la por omisión).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"volumeDb", {{"type", "number"}, {"minimum", -100}, {"maximum", 26}}},
                           {"muted", {{"type", "boolean"}}}}},
                         {"required", {"name"}}};
        t.run = [this, &rt](const json& a, Done done) {
            const std::string n = a["name"];
            const std::string salida = core_.salidaDe(a);
            bool ok = true;
            if (a.contains("volumeDb"))
                ok = rt.setVolume(n, static_cast<float>(std::pow(10.0, a["volumeDb"].get<double>() / 20.0)), salida) &&
                     ok;
            if (a.contains("muted")) ok = rt.setMuted(n, a["muted"], salida) && ok;
            if (!ok)
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente de audio '%1' (mixer_list las muestra)."), {n})));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Audio de '%1' actualizado."), {n})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_escuchar";
        t.title = ba::i18n::traducir("mcp", "Escuchar una fuente por el monitor (PFL), sin tocar el aire");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Manda una fuente de audio al monitor del operador para escucharla. NO cambia lo que sale "
            "al aire: lo que estaba saliendo sigue saliendo igual.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Es el PFL de una consola: antes de abrir un micrófono se lo escucha para ver si está ahí, "
            "si zumba, si el invitado ya volvió. Sin esto había que ponerlo al aire para enterarse.\n\n"
            "Hay UN solo a la vez: escuchar una reemplaza a la anterior. Sin argumentos se apaga. "
            "`modo` \"pfl\" (de fábrica) es lo que entrega la entrada, antes del fader; \"afl\", con el "
            "fader y el mute de `salida` (la por omisión si no se dice). Mientras dura suena SOLO esa: "
            "la escucha fija (lo que la matriz manda a los auriculares a propósito, como la voz del "
            "asistente) no se toca y vuelve al soltarlo. No se guarda.\n\n"
            "`salida_escucha` elige de qué salida son el fader y el mute de la escucha fija (vacío = la "
            "por omisión); se puede mandar solo, sin `name`.\n\n"
            "OJO: una fuente SILENCIADA (`audio_set muted`) no suena ni por el monitor —el silencio es "
            "antes de todo—. Para probar un micrófono cerrado hay que quitarle el silencio "
            "y bajarle el volumen, no silenciarlo.\n\n"
            "Que se oiga depende de que el equipo tenga un monitor de audio configurado; sin monitor "
            "esto no falla pero tampoco suena. `audio_sources` dice cuál se está escuchando.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la fuente; vacio o sin esto = apagar")}}},
              {"escuchar",
               {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "false para apagar esta (por defecto true)")}}},
              {"modo",
               {{"type", "string"}, {"enum", {"pfl", "afl"}},
                {"description", QT_TRANSLATE_NOOP("mcp", "pfl (antes del fader, por defecto) o afl (con el fader de `salida`)")}}},
              {"salida", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "afl: de qué salida es el fader (vacío = la por omisión)")}}},
              {"salida_escucha",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "de qué salida son el fader y el mute de la escucha fija (vacío = la por omisión)")}}}}}};
        t.examples = {R"({"name": "Microfono del atril"})", R"({"name": "Microfono del atril", "modo": "afl", "salida": "b"})",
                      R"({"salida_escucha": "b"})", R"({})"};
        t.run = [&vm](const json& a, Done done) {
            const std::string n = a.value("name", std::string());
            const bool on = a.value("escuchar", true) && !n.empty();
            auto* matriz = qobject_cast<audio::Matriz*>(vm.matriz());
            if (matriz == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "La matriz de audio no está disponible.")));
            if (a.contains("salida_escucha")) {
                const std::string s = a.value("salida_escucha", std::string());
                if (const QString e = matriz->ponerSalidaEscucha(QString::fromStdString(s)); !e.isEmpty())
                    return done(ToolResult::error(e.toStdString()));
                if (!a.contains("name"))
                    return done(ToolResult::ok(
                        s.empty() ? ba::i18n::traducir("mcp", "La escucha fija sigue el fader de la salida por omisión.")
                                  : ba::i18n::llenar(ba::i18n::traducir("mcp", "La escucha fija sigue el fader de la salida «%1»."), {s})));
            }
            // El PFL es uno solo y no toca la escucha fija: la regla es de la Matriz (la
            // misma del botón PFL de la interfaz); acá solo se arma la respuesta.
            // Lo del motor, de la foto (la interfaz no entra al motor).
            const FotoMotor& foto = vm.ventanilla().foto();
            const std::string antes = foto.mezcla.pfl;
            if (!on) {
                if (const QString e = matriz->ponerPfl({}); !e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                return done(ToolResult::ok(antes.empty() ? ba::i18n::traducir("mcp", "No había ninguna en PFL.")
                                                         : ba::i18n::llenar(ba::i18n::traducir("mcp", "Dejo de escucharse: %1."), {antes})));
            }
            if (!foto.hayFuente(n))
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe la fuente de audio '%1' (audio_sources las lista)."), {n})));
            const std::string modo = a.value("modo", std::string("pfl"));
            if (const QString e = matriz->ponerPfl(QString::fromStdString(n), QString::fromStdString(modo),
                                                   QString::fromStdString(a.value("salida", std::string())));
                !e.isEmpty())
                return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(
                antes.empty() || antes == n
                    ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Escuchando «%1» por el monitor. Lo que sale al aire no cambio."), {n})
                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "Escuchando «%1» por el monitor. Lo que sale al aire no cambio. "
                                                                 "(se dejo de escuchar %2)"),
                                       {n, antes})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "audio_sincro_medir";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Medir la sincronía labios/voz con aplausos");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            std::string(QT_TRANSLATE_NOOP("mcp", "Mide cuánto hay que atrasar el audio para que coincida con el video, con aplausos frente a la "
            "cámara: durante `segundos` (de 4 a 30, 10 de fábrica) mira lo que SALE (cuadros del programa y "
            "mezcla de audio) y empareja cada aplauso (pico de audio, cambio brusco en la imagen). La cámara "
            "tiene que estar en el programa y su micrófono sonando. Avisá a la persona que aplauda 4 o 5 veces, "
            "separadas. Con `aplicar_a` (una fuente de audio) y una medición confiable, le suma el retardo "
            "medido al que ya tenía (audio_sincro). Devuelve retardo_ms, dispersión y si es confiable.")) +
            " " + std::string(QT_TRANSLATE_NOOP("mcp", "Mira el programa de la salida (`salida`)."));
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"segundos", {{"type", "integer"}, {"minimum", 4}, {"maximum", 30}}},
                           {"aplicar_a", {{"type", "string"}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            // Empezar, terminar y aplicar lo medido los hace el dueño del motor (la mezcla se
            // lo pide por la ventanilla); acá se espera la respuesta.
            const QString e = vm.mezcla().medirSincro(
                a.value("segundos", 10), [done](const json& r) { done(ToolResult().data(r)); },
                [done](const QString& err) { done(ToolResult::error(err.toStdString())); }, core_.salidaDe(a),
                a.value("aplicar_a", std::string()));
            if (!e.isEmpty()) done(ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_medidores";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Balística de los medidores");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cómo se mueven todos los medidores de la interfaz: `balistica` = pico (instantáneo), ppm "
                        "(EBU tipo IIb: sube al instante y cae 24 dB en 2,8 s) o vu (integra ~300 ms: el volumen "
                        "percibido). Sin argumentos dice cuál está.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"balistica", {{"type", "string"}, {"enum", {"pico", "ppm", "vu"}}}}}}};
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("balistica")) {
                const std::string v = a.value("balistica", std::string("pico"));
                if ((v == "pico" || v == "ppm" || v == "vu") && v != rt.config().balistica) {
                    rt.config().balistica = v;
                    rt.fotoSucia(FotoMotor::Mezcla); // los medidores de la vista, con la foto
                }
            }
            done(ToolResult().data(json{{"balistica", rt.config().balistica}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_sincro";
        t.hilo = Hilo::Control;
        t.title = ba::i18n::traducir("mcp", "Sincronizar labios y voz");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Atrasa el audio de una fuente para que coincida con su video. Sin `ms`, dice el retardo que "
            "tiene puesto; '+40' / '-40' lo cambia sobre el que tiene.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Para que sirve: una camara IP llega con 200 ms a 2 s de retardo (codificacion + red + "
            "buffer), mientras que un microfono por la placa de audio llega casi al instante. Se escucha "
            "antes de ver los labios, y eso se nota muchisimo.\n\n"
            "`ms` positivo ATRASA el audio (lo normal con una camara IP); negativo lo adelanta, que sirve "
            "cuando el audio llega despues que el video. Se acota a +-5 s: mas que eso no es una camara "
            "con retardo, es un error.\n\n"
            "Como encontrar el numero: poner un valor, mirar con `foto_programa` a alguien hablando y "
            "corregir. El ojo humano es el juez. Un metodo mas fino -medirlo con un aplauso- esta "
            "escrito pero todavia no conectado a las fuentes.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"name", {{"type", "string"}}},
                           {"ms", {{"type", "integer"}, {"minimum", -5000}, {"maximum", 5000}}}}},
                         {"required", {"name"}}};
        t.run = [&rt](const json& a, Done done) {
            const std::string n = a["name"];
            if (!a.contains("ms")) {
                if (rt.engine().sourceNames().empty())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "El motor todavia no tiene fuentes.")));
                return done(ToolResult().data(json{{"name", n}, {"ms", rt.engine().sourceSyncOffset(n)}}));
            }
            // Un numero lo deja en eso; «+40» / «-40» lo cambia sobre el actual; «=200», en 200.
            int ms = 0;
            if (a["ms"].is_number()) {
                ms = static_cast<int>(std::lround(a["ms"].get<double>()));
            } else if (a["ms"].is_string()) {
                std::string v = a["ms"].get<std::string>();
                while (!v.empty() && v.front() == ' ') v.erase(0, 1);
                const bool cambio = !v.empty() && (v.front() == '+' || v.front() == '-');
                if (!v.empty() && v.front() == '=') v.erase(0, 1);
                try {
                    ms = static_cast<int>(std::lround(std::stod(v)));
                } catch (const std::exception&) {
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "`ms` tiene que ser un numero (o '+40' / '-40').")));
                }
                if (cambio) ms += rt.engine().sourceSyncOffset(n);
            } else {
                return done(ToolResult::error(ba::i18n::traducir("mcp", "`ms` tiene que ser un numero (o '+40' / '-40').")));
            }
            ms = std::clamp(ms, -5000, 5000);
            if (!rt.engine().setSourceSyncOffset(n, ms))
                return done(ToolResult::error(rt.engine().lastError()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "El audio de '%1' va %2 ms respecto del video."), {n, std::to_string(ms)})));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
