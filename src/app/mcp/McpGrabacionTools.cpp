// ============================================================================
// src/app/mcp/McpGrabacionTools.cpp — Herramientas MCP del motor: lo que oye cada invitado, el disco, las pruebas con --test (diag_*), la grabación (remux, ajustes, por entrada), los destinos extra, la repetición y la señal limpia.
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // diag_probar_caida: HeapCreate
#endif
#include "fault/ModoSeguro.hpp"
#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include "Piezas.hpp"
#include <QCoreApplication>
#include "fault/FallasProgramadas.hpp"
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
#include "emision/RetornosInvitados.hpp"
#include "emision/VentanillaMotor.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "mcp/InterfazMcp.hpp"
#include "platform/Hilo.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/EscenasViewModel.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerGrabacionTools() {
    AppRuntime& rt = rt_;
    // Si corre con --test: de la configuración que publica el dueño (la foto), sin entrar.
    const bool modoPrueba = vm_.ventanilla().foto().configuracion.app->modoPrueba;
    InterfazMcp& vm = vm_;

    {
        Tool t;
        t.name = "invitado_retorno";
        t.title = ba::i18n::traducir("mcp", "Lo que oye cada invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Mix-minus: cada invitado recibe el programa SIN su propia voz (de fabrica), asi no se escucha "
                        "con retardo ni hace eco. `retorno`: mix_minus o programa (el programa entero). Por `slot` "
                        "(1..invitados). Sin `retorno`, dice lo de cada slot.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"slot", {{"type", "integer"}, {"minimum", 1}}},
                           {"retorno", {{"type", "string"}, {"enum", {"mix_minus", "programa"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& r = vm.retornos();
            if (a.contains("retorno"))
                if (const std::string e = r.poner(a.value("slot", 0), a.value("retorno", std::string())); !e.empty())
                    return done(ToolResult::error(e));
            done(ToolResult().data(json{{"retornos", r.todos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_disco";
        t.title = ba::i18n::traducir("mcp", "Disco de la grabación");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cuanto disco queda y para cuantas horas alcanza al bitrate de todo lo que se graba "
                        "(grabación, copia, señal limpia e ISO de todas las salidas, repartido por disco: manda el que antes "
                        "se llena; sin grabar, lo que sumaría arrancar la grabación); si alguna copia va al mismo disco que su "
                        "grabación (`copia_mismo_volumen`, `mismo_volumen`); y el estado del ultimo pase a MP4 (remux).");
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"disco", rt.discoGrabacion()}, {"remux", rt.engine().remuxEstado()},
                                        {"apuros", rt.engine().discoEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "disco_prioridad";
        t.title = ba::i18n::traducir("mcp", "El disco, para la grabación del programa");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cuando el disco no da abasto (lo grabado espera en memoria para entrar, o el registro no puede "
                        "escribir): `priorizar` true le da el disco a la grabacion del programa (pausa las ISO, la "
                        "repeticion, la copia si va al mismo disco y la senal limpia a disco; el registro se junta en "
                        "memoria); false sigue con todo. Cuando el disco anda bien 30 s, todo sigue solo. `modo`: "
                        "preguntar (avisa y ofrece, de fabrica), auto (lo hace solo) o nunca (solo avisa). Sin "
                        "argumentos: el estado {apuros, atraso_s, registro_trabado_ms, prioridad, automatica, modo, pausadas}.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"priorizar", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"preguntar", "auto", "nunca"}}}}}}};
        if (modoPrueba)
            t.inputSchema["properties"]["probar_trabado_ms"] = {
                {"type", "integer"}, {"description", QT_TRANSLATE_NOOP("mcp", "Solo con --test: la grabación del programa, como con el disco trabado tantos ms.")}};
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json& a, Done done) {
            if (a.contains("probar_trabado_ms") && rt.config().modoPrueba)
                rt.engine().simularDiscoTrabado(std::clamp(a.value("probar_trabado_ms", 0), 0, 120000));
            if (a.contains("modo")) {
                const std::string e = rt.engine().setModoDisco(a.value("modo", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            json out{{"disco", rt.engine().discoEstado()}};
            if (a.contains("priorizar")) {
                rt.engine().priorizarPrograma(a.value("priorizar", false));
                // Se aplica en el hilo de salidas (sin esperar): el estado nuevo, en la próxima consulta.
                out["pedido"] = a.value("priorizar", false) ? ba::i18n::traducir("mcp", "darle el disco al programa")
                                                         : ba::i18n::traducir("mcp", "seguir con todo");
            }
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    if (modoPrueba) {
        // Solo con --test: para probar que una caída deja su volcado bien hecho
        // (diag/Caidas.cpp + launcher/Volcados.cpp). Tira la app abajo.
        Tool t;
        t.name = "diag_probar_caida";
        t.title = ba::i18n::traducir("mcp", "Probar una caida (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "SOLO EN MODO PRUEBA. Tira la app abajo a proposito para probar los volcados: "
                        "`tipo` acceso (violacion de acceso), heap (corrupcion del heap) o abort. "
                        "El launcher la relanza; el volcado y su .json quedan en Logs/dumps.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}, {"enum", {"acceso", "heap", "abort"}}}}}},
                         {"required", {"tipo"}}};
        t.run = [](const json& a, Done done) {
            const std::string tipo = a.value("tipo", std::string());
            done(ToolResult().data(json{{"cayendo", tipo}}));
            // En otro hilo y un momento después: que la respuesta llegue.
            std::thread([tipo]() {
                ba::platform::nombrarHilo("caida-prueba");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (tipo == "acceso") {
                    volatile int* p = nullptr;
                    *p = 1;
                } else if (tipo == "heap") {
#if defined(_WIN32)
                    // Un heap propio de Windows: el doble HeapFree lo detecta siempre
                    // (0xC0000374, como la caída real); el del CRT a veces no se entera.
                    HANDLE h = HeapCreate(0, 0, 0);
                    void* p = HeapAlloc(h, 0, 64);
                    HeapFree(h, 0, p);
                    HeapFree(h, 0, p);
#else
                    char* p = static_cast<char*>(std::malloc(32));
                    std::free(p);
                    std::free(p);
#endif
                } else {
                    std::abort();
                }
            }).detach();
        };
        core_.addTool(std::move(t));
    }
    if (modoPrueba) {
        // Solo con --test: la interfaz trabada a propósito (un bucle ocupado en su
        // hilo), para probar lo que tiene que seguir andando sin ella: el latido, el
        // aire y la alarma al control web y a los notificadores (F1-13, banco de caos).
        Tool t;
        t.name = "diag_trabar_ui";
        t.title = ba::i18n::traducir("mcp", "Trabar la interfaz (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "SOLO EN MODO PRUEBA. Traba el hilo de la interfaz `segundos` (1 a 60) con un "
                        "bucle ocupado, medio segundo después de contestar. Mientras dure, nada de la interfaz "
                        "(ni este MCP) contesta.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 60}}}}},
                         {"required", {"segundos"}}};
        t.run = [](const json& a, Done done) {
            const int s = std::clamp(a.value("segundos", 5), 1, 60);
            done(ToolResult().data(json{{"trabando_s", s}}));
            QTimer::singleShot(500, qApp, [s]() {
                const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(s);
                while (std::chrono::steady_clock::now() < fin) {
                }
            });
        };
        core_.addTool(std::move(t));
    }
#ifdef BA_DIAG_ACOPLES
    if (modoPrueba) {
        // Solo con --test y en los builds con el detector (BA_DIAG_ACOPLES; lo que se publica
        // no lo trae): el detector de acoples (diag/Acoples.hpp,
        // docs/arquitectura/detector-de-acoples.md). Lo usa tools/diag/recorrer-ui.py.
        Tool t;
        t.name = "diag_acoples";
        t.title = ba::i18n::traducir("mcp", "Detector de acoples de la interfaz (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "SOLO EN MODO PRUEBA. El detector de acoples: con `demora_ms` mayor que 0 lo "
                        "enciende (cada camino de la interfaz que no pasa por el modelo, como entrar directo al motor, "
                        "la configuración, el disco o la red, o un binding que llama a una función que devuelve datos, "
                        "espera esos ms de más y queda anotado); con 0 lo apaga. `reiniciar` borra lo anotado. Siempre "
                        "devuelve el informe: cada lugar con su clase, archivo y línea, veces, lo que costó, lo que "
                        "corría y la pila de la primera vez, ordenado por tiempo total.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"demora_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10000}}},
                           {"reiniciar", {{"type", "boolean"}}}}}};
        // Atómicos y su candado: nada de la interfaz.
        t.hilo = Hilo::SinInterfaz;
        t.run = [](const json& a, Done done) {
            if (a.value("reiniciar", false)) ba::diag::acoples::reiniciar();
            if (a.contains("demora_ms")) ba::diag::acoples::activar(std::clamp(a.value("demora_ms", 0), 0, 10000));
            done(ToolResult().data(ba::diag::acoples::informe()));
        };
        core_.addTool(std::move(t));
    }
#endif // BA_DIAG_ACOPLES
    if (modoPrueba) {
        // Solo con --test: el banco de caos (tools/caos/) rompe a propósito el codificador
        // o el sumidero de una salida a los N s (FallasProgramadas → motor/Fallas.hpp).
        Tool t;
        t.name = "diag_falla";
        t.title = ba::i18n::traducir("mcp", "Inyectar una falla en una salida (solo --test)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "SOLO EN MODO PRUEBA. Programa una falla: `pieza` codificador (deja de codificar, "
                        "como un codificador de placa que se cae) o sumidero (no puede escribir: disco o conexión) de "
                        "`salida` grabacion o transmision, dentro de `en_s` segundos (0 = ya). La primera de ese tipo "
                        "que pase se la lleva, una vez. `salida` video con `pieza` gpu: la GPU del video se pierde (el mezclador "
                        "avisa el error de una GPU perdida y la app rearma el motor). `limpiar`: borra lo programado. Siempre "
                        "devuelve lo programado y lo ya disparado.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"pieza", {{"type", "string"}, {"enum", {"codificador", "sumidero", "gpu"}}}},
                           {"salida", {{"type", "string"}, {"enum", {"grabacion", "transmision", "video"}}}},
                           {"en_s", {{"type", "number"}, {"minimum", 0}, {"maximum", 3600}}},
                           {"limpiar", {{"type", "boolean"}}}}}};
        t.run = [](const json& a, Done done) {
            auto& f = ba::fault::FallasProgramadas::instancia();
            if (a.value("limpiar", false)) f.limpiar();
            if (a.contains("pieza") || a.contains("salida")) {
                const std::string e = f.programar(a.value("salida", std::string()), a.value("pieza", std::string()),
                                                  static_cast<std::int64_t>(a.value("en_s", 0.0) * 1000));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data(f.estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_remux";
        t.title = ba::i18n::traducir("mcp", "Pasar una grabación a MP4/MOV");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.destructivaSi = "borrar_mkv"; // borra la grabación original
        t.description = QT_TRANSLATE_NOOP("mcp", "Pasa un `archivo` .mkv a `formato` (mp4, de fabrica, o mov) sin recodificar, en otro hilo "
                        "(record_disco dice como va). `borrar_mkv`: borrar el original si salio bien (de fabrica false).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"archivo"}},
                         {"properties",
                          {{"archivo", {{"type", "string"}}},
                           {"formato", {{"type", "string"}, {"enum", {"mp4", "mov"}}}},
                           {"borrar_mkv", {{"type", "boolean"}}}}}};
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json& a, Done done) {
            const std::string f = a.value("archivo", std::string());
            std::error_code ec;
            if (!std::filesystem::exists(f, ec)) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no existe %1"), {f})));
            if (!rt.engine().remux(f, a.value("formato", std::string("mp4")), a.value("borrar_mkv", false)))
                return done(ToolResult::error(rt.engine().lastError()));
            done(ToolResult().text(ba::i18n::traducir("mcp", "Pasando; record_disco dice como va.")).data(rt.engine().remuxEstado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "record_ajustes";
        t.title = ba::i18n::traducir("mcp", "Ajustes de la grabación");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`contenedor`: mkv (de fabrica, sobrevive a un corte), mp4, mov o flv (mp4/mov se graban en MKV "
                        "y se pasan al terminar); `remux_al_terminar`. `calidad`: como_stream (comparte el encode de "
                        "la transmisión) o archivo (codificador propio a calidad constante `crf`, 14-28; un encode "
                        "más). `pistas`: las pistas EXTRA de la Matriz (2-6) que se graban aparte, cada una con lo "
                        "que se le cruce ahí (la 1, el programa, va siempre). `copia_dir`: segunda grabación igual en "
                        "otra carpeta/disco (\"\" = sin copia). Rige en la próxima grabación. Sin argumentos, los "
                        "muestra junto con la grabación en curso.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"contenedor", {{"type", "string"}, {"enum", {"mkv", "mp4", "mov", "flv"}}}},
                           {"remux_al_terminar", {{"type", "boolean"}}},
                           {"calidad", {{"type", "string"}, {"enum", {"como_stream", "archivo"}}}},
                           {"crf", {{"type", "integer"}, {"minimum", 10}, {"maximum", 35}}},
                           {"pistas", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 2}, {"maximum", 6}}}}},
                           {"copia_dir", {{"type", "string"}}}}}};
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json& a, Done done) {
            RecordConfig& r = rt.config().rec;
            if (a.contains("contenedor")) r.container = a.value("contenedor", std::string("mkv"));
            if (a.contains("remux_al_terminar")) r.remuxAlTerminar = a.value("remux_al_terminar", true);
            if (a.contains("calidad")) r.calidad = a.value("calidad", std::string("como_stream"));
            if (a.contains("crf")) r.calidadCrf = std::clamp(a.value("crf", 18), 10, 35);
            if (a.contains("pistas") && a["pistas"].is_array()) {
                r.pistas.clear();
                for (const json& p : a["pistas"])
                    if (p.is_number_integer() && p.get<int>() >= 2 && p.get<int>() <= 6) r.pistas.push_back(p.get<int>());
            }
            if (a.contains("copia_dir")) {
                const std::filesystem::path c = a.value("copia_dir", std::string());
                r.copiaDir = c.empty() || c.is_absolute() ? c.string() : (rt.dataRoot() / c).string();
            }
            done(ToolResult().data(json{{"contenedor", r.container},
                                        {"remux_al_terminar", r.remuxAlTerminar},
                                        {"calidad", r.calidad},
                                        {"crf", r.calidadCrf},
                                        {"pistas", r.pistas},
                                        {"copia_dir", r.copiaDir},
                                        {"en_curso", rt.engine().grabacionEstado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destinos";
        t.title = ba::i18n::traducir("mcp", "Destinos de la transmisión");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los destinos adicionales (respaldo, otras plataformas), que comparten los codificadores del "
                        "principal: cada uno con su estado (activo, reconectando, cuadros perdidos) y si tiene clave "
                        "guardada. El principal es el de stream_start sin `destino`.");
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json&, Done done) {
            done(ToolResult().data(json{{"principal", rt.engine().status().streaming ? rt.engine().destinoActivo() : ""},
                                        {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_agregar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Agregar o cambiar un destino de transmisión");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Un destino adicional: `nombre`, `url` (rtmp://, rtmps://, srt:// o rist://), `clave` (se "
                        "guarda cifrada; mejor que la cargue el operador en Salidas > + Destino…, donde no la ves; en "
                        "SRT es la passphrase, en RTMP la stream key), `respaldo`. SRT: `modo` (caller, listener, "
                        "rendezvous) y `latencia_ms` (120 en LAN, 4× el ping por internet). `url_respaldo`: el punto "
                        "de ingest de respaldo de la plataforma (la misma clave; sale a los dos a la vez con el mismo "
                        "codificador; \"\" lo saca). Con `id` de uno existente, lo cambia (stream_destino_editar es lo mismo).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"url_respaldo", {{"type", "string"}}},
                           {"modo", {{"type", "string"}, {"enum", {"caller", "listener", "rendezvous"}}}},
                           {"latencia_ms", {{"type", "integer"}, {"minimum", 20}, {"maximum", 8000}}}}}};
        // Los destinos son del dueño del motor (la configuración y las salidas): corre allá.
        t.hilo = Hilo::Control;
        t.run = [this, &rt](const json& a, Done done) {
            // Uno nuevo sale con el programa de la salida pedida (o la de quien llama; "" = la
            // por omisión). Uno que ya existe (`id`) se queda en la suya salvo que venga
            // `salida`: «agregar» con su id no lo mudaba a propósito, lo mudaba siempre.
            json d = a;
            if (!a.contains("id") || a.contains("salida")) d["salida"] = core_.salidaDe(a);
            std::string id;
            const std::string e = rt.guardarDestino(d, &id);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"id", id}, {"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_editar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Cambiar un destino de transmisión");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia `nombre`, `url`, `url_respaldo` (el ingest de respaldo, misma clave; "
                                                  "\"\" lo saca), `clave` o `respaldo` del destino `id`; con `salida`, "
                                                  "lo pasa a tomar el programa de esa salida.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"url", {{"type", "string"}}},
                           {"clave", {{"type", "string"}}},
                           {"respaldo", {{"type", "boolean"}}},
                           {"url_respaldo", {{"type", "string"}}}}}};
        t.hilo = Hilo::Control; // como stream_destino_agregar
        t.run = [this, &rt](const json& a, Done done) {
            json d = a;
            if (a.contains("salida")) d["salida"] = core_.salidaDe(a); // por nombre o id → id
            const std::string e = rt.guardarDestino(d);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"destinos", rt.destinos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "stream_destino_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar un destino de transmisión");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra un destino (si transmite, lo corta)");
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra el destino `id` (si estaba transmitiendo, lo corta) y su clave guardada.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.hilo = Hilo::Control; // como stream_destino_agregar
        t.run = [&rt](const json& a, Done done) {
            const std::string e = rt.borrarDestino(a.value("id", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_ajustes";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Repetición: cuántos segundos guardar");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Guarda en memoria los últimos `segundos` del programa (5 a 600; 0 la apaga) para "
                        "replay_guardar. Usa el mismo encode que la transmisión. Sin argumentos informa. Cada salida tiene la "
                        "suya (`salida`; la de las demás no queda guardada para el próximo arranque).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"segundos", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}}}}}};
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (a.contains("segundos"))
                if (const std::string e = rt.ponerReplay(a.value("segundos", 0), salida); !e.empty())
                    return done(ToolResult::error(e));
            done(ToolResult().data(rt.engine().replayEstado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "replay_guardar";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Repetir: guardar los últimos segundos");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Escribe a disco los últimos segundos de la repetición (Recordings/repeticiones/) y "
                        "devuelve el archivo. Con `y_armar: true` lo pone en la escena «Repetición» y la deja "
                        "armada para el take (no la saca al aire).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"y_armar", {{"type", "boolean"}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            // La misma operación que «Repetir» de la interfaz (por la ventanilla del motor).
            vm.escenas().repetir(core_.salidaDe(a), a.value("y_armar", false),
                                 [done, armar = a.value("y_armar", false)](const QString& f, const QString& e, bool armada) {
                                     if (f.isEmpty()) return done(ToolResult::error(e.toStdString()));
                                     json r{{"archivo", f.toStdString()}};
                                     if (armar) {
                                         r["armada"] = armada;
                                         if (!e.isEmpty()) r["error_al_armar"] = e.toStdString();
                                     }
                                     done(ToolResult().data(r));
                                 });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_limpia";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Señal limpia (clean feed)");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "El programa SIN las capas globales (zócalos DSK, logo) ni la placa de seguridad: lo que "
                        "piden un canal o una cadena que ponen su propia gráfica. Sin argumentos informa. "
                        "`activar: true` la arranca: con `destino` (id de stream_destinos: rtmp/srt/rist, con su "
                        "clave guardada) la manda ahí; sin destino la graba en Recordings/limpia/. `activar: false` "
                        "la corta. Es un encode más (x264, en la CPU): mirá `status.cpu`.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"activar", {{"type", "boolean"}}}, {"destino", {{"type", "string"}}}}}};
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [this, &rt](const json& a, Done done) {
            const std::string salida = core_.salidaDe(a);
            if (a.contains("activar")) {
                if (a.value("activar", false)) {
                    const std::string e = rt.iniciarSenalLimpia(a.value("destino", std::string()), salida);
                    if (!e.empty()) return done(ToolResult::error(e));
                } else {
                    rt.detenerSenalLimpia(salida);
                }
            }
            done(ToolResult().data(rt.engine().cleanFeedEstado(salida)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salidas_profesionales";
        t.title = ba::i18n::traducir("mcp", "Qué salidas profesionales hay");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Qué salidas de contribución trae este equipo y cuáles no, para no prometer lo que no hay.");
        // Solo el motor y la configuración: en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt](const json&, Done done) {
            const bool ts = motor::hayFabrica("mpegtsmux") && motor::hayFabrica("srtsink");
            done(ToolResult().data(json{
                {"rtmp", motor::hayFabrica("rtmp2sink")},
                {"srt", ts},
                {"rist", false},
                {"senal_limpia", true},
                {"senal_limpia_estado", rt.engine().cleanFeedEstado()},
                {"ndi_salida", false},
                {"decklink_salida", false},
                {"nota", ba::i18n::traducir("mcp", "SRT y RIST salen por el muxer MPEG-TS (stream_destino_agregar con srt:// o "
                                            "rist://). NDI y DeckLink de salida no vienen en el paquete (sus SDK no "
                                            "se pueden redistribuir).")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grabacion_individual";
        t.title = ba::i18n::traducir("mcp", "Grabar cada entrada por separado");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Graba entradas sueltas (cámaras, invitados) cada una en su propio archivo, además del programa; sin "
                                                 "argumentos, dice qué se graba.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Para un evento que despues hay que EDITAR: el programa sirve para subirlo esa misma "
            "noche, y los archivos por entrada para armar la version buena sin haber perdido nada.\n\n"
            "Sin argumentos informa que se esta grabando. Con `arrancar` o `parar` (listas de nombres "
            "de fuente) lo cambia. `todas: true` con `arrancar` toma todas las fuentes de video de la "
            "escena que esta al aire.\n\n"
            "LO QUE CUESTA, y conviene decirlo antes de prender ocho: cada entrada es UNA CODIFICACION "
            "MAS, con su render propio. Ocho camaras son ocho encodes simultaneos y eso no lo hace "
            "cualquier maquina; si la CPU se satura, lo primero que se degrada es el programa, que es "
            "lo que sale al aire. Empeza por las que de verdad vas a editar y mira `status.cpu`.\n\n"
            "EL AUDIO: cada una graba el de su propia entrada (despues de su rack); una entrada sin audio "
            "graba solo el video, y la respuesta lo dice en `audioPropio`.\n\n"
            "Los archivos van a Recordings/entradas/, con el nombre de la fuente adentro.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"arrancar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"parar", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"todas",
               {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "Con `arrancar`: todas las de la escena al aire.")}}},
              {"invitado",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id o nombre de un invitado de la conferencia: graba su fuente.")}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            std::vector<std::string> arrancar;
            if (a.contains("invitado")) {
                const QString f = vm.emision().fuenteDeInvitado(QString::fromStdString(a.value("invitado", std::string())));
                if (f.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ese invitado no tiene una fuente en el programa (¿esta admitido?)")));
                arrancar.push_back(f.toStdString());
            }
            if (a.contains("arrancar") && a["arrancar"].is_array())
                for (const json& s : a["arrancar"])
                    if (s.is_string()) arrancar.push_back(s.get<std::string>());
            std::vector<std::string> parar;
            if (a.contains("parar") && a["parar"].is_array())
                for (const json& s : a["parar"])
                    if (s.is_string()) parar.push_back(s.get<std::string>());
            // El invitado se resolvió acá (es de la interfaz); lo demás es del dueño del motor:
            // se le pide por la ventanilla (desde acá eran entradas directas).
            vm.ventanilla().pedir(
                &vm, "grabacion_individual",
                [&rt, arrancar, parar, todas = a.value("todas", false)]() mutable {
                    ApiMotor& e = rt.engine();
                    if (todas) {
                        // Las de la escena al aire que NO se están grabando ya (como «grabar todas» de la
                        // interfaz): pedirlas de nuevo daba un error por cada una.
                        std::set<std::string> ya;
                        for (const auto& g : e.sourceRecordings())
                            if (g.activa) ya.insert(g.source);
                        for (const std::string& s : e.sceneSources(e.currentScene()))
                            if (ya.count(s) == 0) arrancar.push_back(s);
                    }
                    json fallaron = json::array();
                    // Con las reglas del modelo (disco, tope), las mismas que la GUI.
                    for (const std::string& s : arrancar)
                        if (const std::string err = rt.grabarAparte(s); !err.empty())
                            fallaron.push_back(json{{"fuente", s}, {"motivo", err}});
                    for (const std::string& s : parar) rt.stopSourceRecording(s);
                    json grabando = json::array();
                    for (const auto& g : e.sourceRecordings())
                        grabando.push_back(json{{"fuente", g.source},
                                                {"archivo", g.path},
                                                {"mb", [&]() {
                                                     std::error_code ec;
                                                     const auto bytes = std::filesystem::file_size(g.path, ec);
                                                     return ec ? 0.0 : static_cast<double>(bytes) / 1e6;
                                                 }()},
                                                {"audioPropio", g.audioPropio},
                                                {"activa", g.activa}});
                    return json{{"grabando", grabando}, {"noSePudo", fallaron}, {"tope", ApiMotor::kMaxIndividuales}};
                },
                [done](const json& r) { done(ToolResult().data(r)); });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
