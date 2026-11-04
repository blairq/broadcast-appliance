// ============================================================================
// src/app/mcp/McpTransmisionTools.cpp — Herramientas MCP del motor: salidas: transmitir y grabar.
//
// Una familia de McpService::registerTools() (ver mcp/McpService.hpp: el orden en que
// se registran es el de siempre). El texto de cada herramienta es el que estaba en
// McpService.cpp.
// ============================================================================

#include "i18n/I18nTexto.hpp"
#include "sala/SeguridadSala.hpp"
#include "escenas/RespaldoEntradas.hpp"
#include "mcp/McpService.hpp"
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
#include "mcp/McpHttp.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "platform/Paths.hpp"
#include "agent/SubtitulosCC.hpp"
#include "agent/Vocabulario.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/AppViewModel.hpp"
#include "ui/AsambleaViewModel.hpp"
#include "ui/EstadoAireViewModel.hpp"
#include "ui/SalaViewModel.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {

void McpService::registerTransmisionTools() {
    AppRuntime& rt = rt_;
    InterfazMcp& vm = vm_;

    // --- Salidas -------------------------------------------------------------------
    {
        Tool t;
        t.name = "audio_loudness";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Loudness de lo que sale");
        t.category = "audio";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "El loudness del PROGRAMA (la mezcla que va a la transmision y la grabacion), EBU R128 / "
                        "BS.1770-4: `m` momentaneo (400 ms), `s` corto (3 s), `i` integrado desde el reset (arranca "
                        "solo al transmitir o grabar), `tp` true peak maximo (dBTP), `lra` rango, `gr_db` lo que "
                        "baja el limitador del master, `objetivo` y `fuera_de_objetivo` (integrado a mas de 2 LU o TP "
                        "arriba del techo). audio_measure es por fuente; esto es lo que sale. Cada salida tiene el suyo (`salida`).");
        t.run = [this, &rt](const json& a, Done done) {
            done(ToolResult().data(rt.engine().loudness(core_.salidaDe(a)).json()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_reset";
        t.hilo = Hilo::Control;
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Reiniciar el integrado del loudness");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Vuelve a cero el integrado, el LRA y el true peak maximo del programa.");
        t.run = [this, &rt](const json& a, Done done) {
            rt.engine().loudness(core_.salidaDe(a)).reiniciar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Reiniciado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_loudness_objetivo";
        t.porSalida = true;
        t.title = ba::i18n::traducir("mcp", "Objetivo de loudness");
        t.category = "audio";
        t.clase = Clase::Escritura;
        t.description = std::string(QT_TRANSLATE_NOOP("mcp", "`perfil`: r128 (-23 LUFS / -1 dBTP, de fabrica), a85 (-24 LKFS / -2), plataforma16 (-16 / -1), "
                        "plataforma14 (-14 / -1) o custom con `lufs` y `tp`.")) +
                        " " +
                        std::string(QT_TRANSLATE_NOOP("mcp", "`agc` true: el programa se lleva solo al objetivo, despacio (1 dB por segundo como mucho, "
                        "hasta 6 dB arriba o abajo, quieto en las pausas), antes del limitador; de fabrica no. Si una "
                        "transmision va con AAC por debajo de 128 kbps, el techo baja a -2 dBTP.")) +
                        " " + std::string(QT_TRANSLATE_NOOP("mcp", "Cada salida tiene el suyo (`salida`)."));
        t.inputSchema = {{"type", "object"},
                         {"required", {"perfil"}},
                         {"properties",
                          {{"perfil", {{"type", "string"}, {"enum", {"r128", "a85", "plataforma16", "plataforma14", "custom"}}}},
                           {"lufs", {{"type", "number"}}},
                           {"tp", {{"type", "number"}}},
                           {"agc", {{"type", "boolean"}}}}}};
        // El objetivo es un modelo del dueño (toca el loudness de cada salida del motor): en el control.
        t.hilo = Hilo::Control;
        t.run = [this, &vm, &rt](const json& a, Done done) {
            json o{{"perfil", a.value("perfil", std::string("r128"))}};
            if (a.contains("lufs")) o["lufs"] = a.value("lufs", -23.0);
            if (a.contains("tp")) o["tp"] = a.value("tp", -1.0);
            if (a.contains("agc")) o["agc"] = a.value("agc", false);
            const std::string salida = core_.salidaDe(a);
            if (const std::string e = vm.objetivoLoudness().poner(o, salida); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(rt.engine().loudness(salida).json()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_info_aprobar";
        t.title = ba::i18n::traducir("mcp", "Aprobar el cargo que escribió un invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Lo que un invitado escribe de si mismo al entrar (`info`) NO sale en el zocalo hasta que una "
                        "persona lo aprueba: con esto, `participant` (id o nombre) y un `texto` corregido opcional. Si "
                        "esta en el cuerpo (cuerpo_listar), sale de ahi y no hace falta. Confirmalo con el operador.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"participant"}},
                         {"properties", {{"participant", {{"type", "string"}}}, {"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = vm.sala().aprobarInfoInvitado(QString::fromStdString(a.value("participant", std::string())),
                                                      QString::fromStdString(a.value("texto", std::string())));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Aprobado: el zocalo ya lo usa.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "mcp_token_rotar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Rotar el token del MCP");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia ya el token del servidor MCP (el viejo deja de valer). El puente ba-mcp lo relee solo; un "
                        "cliente HTTP directo lo vuelve a pedir con `ba-mcp --token`.");
        t.run = [this](const json&, Done done) {
            const QString nuevo = rotarToken(QString::fromStdWString((ba::platform::credentialsDir()).wstring()));
            if (nuevo.isEmpty() || !http_) return done(ToolResult::error(ba::i18n::traducir("mcp", "no se pudo guardar el token nuevo")));
            http_->setToken(nuevo);
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Token rotado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguridad_estado";
        t.title = ba::i18n::traducir("mcp", "Seguridad: estado y ajustes");
        t.category = "config";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Lo que protege los accesos: si el token del MCP se rota en cada arranque, si la conferencia cambia de "
                        "PIN al empezar una sesion en vivo en un dia nuevo (el PIN es uno por dia: los reinicios "
                        "del mismo dia lo conservan), y el modo en vivo. Con `rotar_token_mcp` o `pin_por_sesion` "
                        "los cambia (fuera de vivo).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"rotar_token_mcp", {{"type", "boolean"}}}, {"pin_por_sesion", {{"type", "boolean"}}}}}};
        t.run = [this](const json& a, Done done) {
            auto& seg = vm_.seguridad();
            if (a.contains("rotar_token_mcp"))
                if (const std::string e = seg.setRotarTokenMcp(a.value("rotar_token_mcp", true)); !e.empty())
                    return done(ToolResult::error(e));
            if (a.contains("pin_por_sesion"))
                if (const std::string e = seg.setPinPorSesion(a.value("pin_por_sesion", true)); !e.empty())
                    return done(ToolResult::error(e));
            const json j = seg.estado();
            done(ToolResult().data({{"rotar_token_mcp", j.value("rotar_token_mcp", true)},
                                    {"pin_por_sesion", j.value("pin_por_sesion", true)},
                                    {"modo_vivo", vm_.app().modoVivoEstado()},
                                    {"passphrase", ba::i18n::traducir("mcp", "todavia no (ver docs/planes/DECISIONES-PENDIENTES.md)")}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_estado";
        t.title = ba::i18n::traducir("mcp", "Subtitulos ocultos (CC)");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los subtitulos ocultos de la transmision (el espectador los prende en su reproductor): si estan "
                        "prendidos, de que salida de texto salen (texto_flujos), el metodo (608 en el video, la API de "
                        "YouTube, o los dos), cuantas lineas se mandaron y las ultimas.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(qvToJson(vm.cc().estado()))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_activar";
        t.title = ba::i18n::traducir("mcp", "Prender los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`flujo`: la salida de texto (la transcripcion o una traduccion; texto_flujos). `metodo`: 608 (de "
                        "fabrica: CEA-608 dentro del video de la transmision y de cada destino), youtube_http (la API de "
                        "subtitulos de YouTube: `url` de ingesta del evento) o ambos.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"flujo"}},
                         {"properties",
                          {{"flujo", {{"type", "string"}}},
                           {"metodo", {{"type", "string"}, {"enum", {"608", "youtube_http", "ambos"}}}},
                           {"url", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            QVariantMap m{{"flujo", QString::fromStdString(a.value("flujo", std::string()))},
                          {"metodo", QString::fromStdString(a.value("metodo", std::string("608")))}};
            if (a.contains("url")) m["url"] = QString::fromStdString(a.value("url", std::string()));
            const QString e = errorDe([&] { vm.cc().activar(m); });
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(qvToJson(vm.cc().estado())));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_desactivar";
        t.title = ba::i18n::traducir("mcp", "Apagar los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Deja de mandar subtítulos ocultos (CEA-608 en la transmisión y la API de subtítulos de YouTube): lo "
                                                 "contrario de cc_activar.");
        t.run = [&vm](const json&, Done done) {
            vm.cc().desactivar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Apagados.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "cc_probar";
        t.title = ba::i18n::traducir("mcp", "Probar los subtitulos ocultos");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Manda un `texto` de prueba por lo configurado (608 necesita una transmision activa).");
        t.inputSchema = {{"type", "object"}, {"required", {"texto"}}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const QString e = errorDe([&] { vm.cc().probar(QString::fromStdString(a.value("texto", std::string()))); });
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Mandado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario";
        t.hilo = Hilo::Foto; // el vocabulario es de cualquier hilo (su candado)
        t.title = ba::i18n::traducir("mcp", "Vocabulario de la voz a texto");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los terminos que la transcripcion escribe bien aunque el modelo no los conozca (nombres del "
                        "cuerpo, localidades, terminos de asamblea, los del operador), por grupo (activo o no), y los "
                        "ultimos reemplazos hechos. La correccion es por sonido y solo con confianza alta.");
        t.run = [&voc = vm.vocabulario()](const json&, Done done) {
            json d = voc.todo();
            d["reemplazos"] = voc.ultimosReemplazos();
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar un termino al vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Agrega (o cambia) el `termino` bien escrito, con `variantes` (como lo escribe mal el modelo, "
                        "opcional), en el `grupo` (operador de fabrica; cuerpo, localidades, asamblea u otro nuevo). "
                        "Con `grupo` y `activo` y sin `termino`, prende o apaga el grupo. Con `del_cuerpo`: true, pone "
                        "en el grupo cuerpo los nombres y apellidos de cuerpo_listar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"termino", {{"type", "string"}}},
                           {"variantes", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                           {"grupo", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}},
                           {"del_cuerpo", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("del_cuerpo", false))
                return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Nombres del cuerpo: %1."),
                                                                {std::to_string(vm.vocabulario().cargarCuerpo(vm.cuerpo().cuerpo()))})));
            const std::string termino = a.value("termino", std::string()), grupo = a.value("grupo", std::string());
            if (termino.empty() && !grupo.empty() && a.contains("activo")) {
                vm.vocabulario().activarGrupo(grupo, a.value("activo", true));
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
            }
            std::vector<std::string> v;
            for (const auto& x : a.value("variantes", json::array()))
                if (x.is_string()) v.push_back(x.get<std::string>());
            const std::string e = vm.vocabulario().agregar(termino, v, grupo);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Agregado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_quitar";
        t.destructive = true;
        t.title = ba::i18n::traducir("mcp", "Quitar un termino del vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca un `termino` del vocabulario de la transcripción, de cualquier grupo (voz_vocabulario los muestra).");
        t.inputSchema = {{"type", "object"}, {"required", {"termino"}}, {"properties", {{"termino", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!vm.vocabulario().quitar(a.value("termino", std::string())))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "no esta en el vocabulario")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Quitado.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "voz_vocabulario_probar";
        t.hilo = Hilo::Foto; // el vocabulario es de cualquier hilo (su candado)
        t.title = ba::i18n::traducir("mcp", "Probar el vocabulario");
        t.category = "traduccion";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Pasa un `texto` (como lo escribiria el modelo) por la correccion y devuelve el resultado y los "
                        "reemplazos.");
        t.inputSchema = {{"type", "object"}, {"required", {"texto"}}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [&voc = vm.vocabulario()](const json& a, Done done) {
            json r = json::array();
            const std::string t2 = voc.corregir(a.value("texto", std::string()), &r);
            done(ToolResult().data({{"texto", t2}, {"reemplazos", r}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "entrada_respaldo";
        t.title = ba::i18n::traducir("mcp", "Qué hacer si una entrada se cae al aire");
        t.category = "video";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Si la entrada `entrada` se queda sin señal mas de `ms` (de fabrica 1000) estando en la escena al "
                        "aire: `al_perder_aire` avisar (de fabrica), armar la `escena` de respaldo en el preview o cortar "
                        "a ella. Sin `al_perder_aire`, muestra lo configurado y las entradas que ahora no tienen señal.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"entrada", {{"type", "string"}}},
                           {"escena", {{"type", "string"}}},
                           {"al_perder_aire", {{"type", "string"}, {"enum", {"avisar", "armar", "cortar"}}}},
                           {"ms", {{"type", "integer"}, {"minimum", 200}, {"maximum", 60000}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& resp = vm.respaldoEntradas(); // el modelo, el mismo que usa la GUI
            const std::string e = a.value("entrada", std::string());
            if (a.contains("al_perder_aire")) {
                if (e.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta `entrada`")));
                const std::string err = resp.setRespaldo(e, a.value("escena", std::string()),
                                                         a.value("al_perder_aire", std::string()), a.value("ms", 1000));
                if (!err.empty()) return done(ToolResult::error(err));
            }
            json sin = json::array();
            for (const QString& s : vm.sinSenal()) sin.push_back(s.toStdString()); // lo último que avisó el respaldo
            done(ToolResult().data(json{{"sin_senal", sin}, {"respaldo", e.empty() ? json() : resp.respaldo(e)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "recinto_bancas_ver";
        t.title = ba::i18n::traducir("mcp", "Bancas y fila de oradores presenciales");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los oradores PRESENCIALES del recinto: las bancas, la fila, quién habla y hace cuánto; no son los "
            "oradores remotos. remotos_fila_ver es la fila de los remotos (invitados conectados).");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.bancas().estadoJson())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_cola";
        t.title = ba::i18n::traducir("mcp", "Sumar o sacar una banca de la fila");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Suma (`agregar`) o saca (`quitar`) una banca de la fila de oradores presenciales: por número de banca, id "
                                                 "o nombre del integrante.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"agregar", {{"type", "string"}}}, {"quitar", {{"type", "string"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("agregar")) {
                const QString e = errorDe([&] { vm.bancas().agregar(QString::fromStdString(a.value("agregar", std::string()))); });
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            if (a.contains("quitar")) vm.bancas().quitar(QString::fromStdString(a.value("quitar", std::string())));
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_al_aire";
        t.title = ba::i18n::traducir("mcp", "Poner al aire a un orador presencial");
        t.category = "oradores";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "pone a un orador al aire (en directo corta la escena)");
        t.aireSi = AireSi::BancaDirecto;
        t.description = QT_TRANSLATE_NOOP("mcp", "Una banca al aire, en una sola accion: la PTZ va al preset de su banca (solo si esa camara no "
                        "esta al aire), la escena de esa camara queda ARMADA en el preview (`modo` armar, de fabrica: "
                        "sale con TAKE) o se corta (directo), el zocalo toma su nombre y cargo del cuerpo ({orador}), "
                        "arranca su reloj y se abre su microfono (y se cierra el de la anterior). `banca`: banca, id o "
                        "nombre. Sin `banca`: la primera de la fila. Si esa persona habla desde el navegador (sesion "
                        "remota, en la sala o afuera), no se mueve ninguna camara: se le da la palabra en su celular "
                        "con la cuenta regresiva. `asignado_s`: el tiempo de cada uno (queda guardado).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"banca", {{"type", "string"}}},
                           {"modo", {{"type", "string"}, {"enum", {"armar", "directo"}}}},
                           {"asignado_s", {{"type", "integer"}, {"minimum", 0}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("asignado_s")) vm.bancas().setAsignado(a.value("asignado_s", 180));
            const QString modo = QString::fromStdString(a.value("modo", std::string("armar")));
            const QString e = errorDe([&] {
                if (a.contains("banca"))
                    vm.bancas().alAire(QString::fromStdString(a.value("banca", std::string())), modo);
                else
                    vm.bancas().siguiente(modo);
            });
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            json r = vm.bancas().estadoJson();
            done(ToolResult().text(r.value("aviso", std::string()).empty()
                                       ? ba::i18n::traducir("mcp", "Listo.")
                                       : ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo, con un aviso: %1"), {r.value("aviso", std::string())})).data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "banca_tiempo";
        t.title = ba::i18n::traducir("mcp", "Tiempo del que tiene la palabra");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`sumar_s` le suma (o resta, negativo) tiempo al que habla ahora (presencial o remoto: su celular "
                        "se actualiza); `asignado_s` cambia el tiempo de cada turno de aca en adelante. A un minuto y al "
                        "terminar avisa al operador, en el celular del remoto y en la unidad de la central.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"sumar_s", {{"type", "integer"}}}, {"asignado_s", {{"type", "integer"}, {"minimum", 10}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.contains("asignado_s")) vm.bancas().setAsignado(a.value("asignado_s", 180));
            if (a.contains("sumar_s")) vm.bancas().sumarTiempo(a.value("sumar_s", 0));
            done(ToolResult().data(vm.bancas().estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguir_microfono_estado";
        t.title = ba::i18n::traducir("mcp", "Seguir al micrófono: estado");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Si esta prendido, el modo (proponer o automatico), el umbral y los tiempos, los microfonos de "
                        "banca abiertos ahora y la propuesta («Banca 12 – Perez pidio la palabra»), si hay.");
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.seguirMicrofono().estado())); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "seguir_microfono";
        t.title = ba::i18n::traducir("mcp", "Seguir al micrófono abierto");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cuando un microfono de banca (cuerpo: microfono.fuente) pasa `umbral_db` durante `sostener_ms`, "
                        "propone poner esa banca al aire. `modo` proponer (de fabrica: el operador acepta) o automatico "
                        "(la arma sola en el preview; nunca corta, y no mueve una camara al aire). `accion` aceptar "
                        "(arma la propuesta) o descartar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"modo", {{"type", "string"}, {"enum", {"proponer", "automatico"}}}},
                           {"fuente", {{"type", "string"}, {"enum", {"niveles", "conferencia"}},
                                       {"description", QT_TRANSLATE_NOOP("mcp", "niveles de los microfonos (de fabrica) o el sistema de conferencia del recinto (plugin conferencia-recinto)")}}},
                           {"umbral_db", {{"type", "number"}, {"minimum", -60}, {"maximum", 0}}},
                           {"sostener_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 10000}}},
                           {"soltar_ms", {{"type", "integer"}, {"minimum", 200}, {"maximum", 30000}}},
                           {"accion", {{"type", "string"}, {"enum", {"aceptar", "descartar"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& s = vm.seguirMicrofono();
            json c = a;
            c.erase("accion");
            if (!c.empty())
                if (const std::string e = s.configurar(c); !e.empty()) return done(ToolResult::error(e));
            const std::string acc = a.value("accion", std::string());
            if (acc == "aceptar")
                if (const std::string e = s.aceptar(); !e.empty()) return done(ToolResult::error(e));
            if (acc == "descartar") s.descartar();
            done(ToolResult().data(s.estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_cargar";
        t.title = ba::i18n::traducir("mcp", "Cargar el orden del día");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.destructive = true; // reemplaza los puntos que había (y con `limpiar`, también los tiempos)
        t.description = QT_TRANSLATE_NOOP("mcp", "Carga los puntos de la sesion desde `texto`: una linea por punto («1. Expte. 123/24 – Titulo», "
                        "«2) Titulo») o CSV («n;expediente;titulo»). Reemplaza los que habia (el uso de la palabra sigue). "
                        "Con `limpiar`: true empieza una sesion nueva (borra puntos y tiempos).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"texto", {{"type", "string"}}}, {"limpiar", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (a.value("limpiar", false)) vm.ordenDelDia().limpiar();
            if (!a.contains("texto")) return done(ToolResult::ok(ba::i18n::traducir("mcp", "Sesion nueva.")));
            int n = 0;
            const std::string e = vm.ordenDelDia().cargar(a.value("texto", std::string()), &n);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Cargados %1 puntos."), {std::to_string(n)})).data({{"puntos", vm.ordenDelDia().puntos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_ver";
        t.title = ba::i18n::traducir("mcp", "Orden del día");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los puntos con su estado (pendiente, en_tratamiento, votado, comision, retirado), cuando se "
                        "trataron y el resultado; el punto actual; el uso de la palabra por orador (y por punto); y, si "
                        "se esta grabando, los capitulos por punto para pegar en YouTube.");
        t.run = [&vm](const json&, Done done) {
            auto& o = vm.ordenDelDia();
            done(ToolResult().data({{"puntos", o.puntos()},
                                    {"actual", o.actual()},
                                    {"tiempos_palabra", o.tiemposPalabra(vm.asamblea().remotosPalabra())},
                                    {"capitulos", o.capitulosTexto(vm.aire().recordStartMs())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "orden_del_dia_punto";
        t.title = ba::i18n::traducir("mcp", "Cambiar el estado de un punto");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "El punto `n` pasa a `estado` (en_tratamiento: cierra el anterior, pone «Punto N: …» en {punto} "
                        "para los zocalos y lo marca para el acta y los capitulos), con `resultado` opcional («aprobado "
                        "por unanimidad»). Con `siguiente`: true, el proximo pendiente pasa a tratamiento.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"n", {{"type", "integer"}, {"minimum", 1}}},
                           {"estado", {{"type", "string"}, {"enum", {"pendiente", "en_tratamiento", "votado", "comision", "retirado"}}}},
                           {"resultado", {{"type", "string"}}},
                           {"siguiente", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& o = vm.ordenDelDia();
            const std::string e = a.value("siguiente", false)
                                      ? o.siguiente()
                                      : o.punto(a.value("n", 0), a.value("estado", std::string("en_tratamiento")),
                                                a.value("resultado", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"actual", o.actual()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
