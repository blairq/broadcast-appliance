// ============================================================================
// src/app/mcp/McpFoto.cpp — ver McpFoto.hpp
// ============================================================================
#include "mcp/McpFoto.hpp"

#include "diag/LectorDeLogs.hpp"
#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpCore.hpp"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <utility>

namespace ba::app::mcp {

// --- La copia de la interfaz ----------------------------------------------------------

struct CopiaDeLaInterfaz::Estado {
    std::mutex mu;
    std::condition_variable cv;
    json valor;
    bool pedida = false;         ///< hay una en la cola de la interfaz (una sola, aunque pregunten diez)
    std::uint64_t vueltas = 0;   ///< cuántas copió
    std::chrono::steady_clock::time_point cuando{};
};

CopiaDeLaInterfaz::CopiaDeLaInterfaz(std::function<json()> leer)
    : leer_(std::move(leer)), e_(std::make_shared<Estado>()) {}

CopiaDeLaInterfaz::Lectura CopiaDeLaInterfaz::leer(McpCore& core, int esperaMs) const {
    if (!leer_) return {};
    const std::shared_ptr<Estado> e = e_;
    std::uint64_t antes = 0;
    bool pedir = false;
    {
        const std::lock_guard<std::mutex> l(e->mu);
        antes = e->vueltas;
        pedir = !e->pedida;
        e->pedida = true;
    }
    // Sin el candado: sin ejecutores (o desde la interfaz) corre en el acto, acá mismo.
    if (pedir)
        core.encolarEnInterfaz([e, leer = leer_]() {
            json v;
            bool ok = true;
            try {
                v = leer();
            } catch (const std::exception&) {
                ok = false; // queda la anterior; el que espera se va por el tope
            }
            {
                const std::lock_guard<std::mutex> l(e->mu);
                e->pedida = false;
                if (ok) {
                    e->valor = std::move(v);
                    e->cuando = std::chrono::steady_clock::now();
                    ++e->vueltas;
                }
            }
            e->cv.notify_all();
        });
    std::unique_lock<std::mutex> l(e->mu);
    e->cv.wait_for(l, std::chrono::milliseconds(std::max(0, esperaMs)), [&]() { return e->vueltas != antes; });
    Lectura r;
    r.alDia = e->vueltas != antes;
    if (e->vueltas > 0) {
        r.valor = e->valor;
        r.edadMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - e->cuando).count();
    }
    return r;
}

namespace {

json estadoDeLaCopia(const CopiaDeLaInterfaz::Lectura& c) {
    return json{{"al_dia", c.alDia}, {"edad_ms", c.edadMs}};
}

/// Las salidas con lo que sale de cada una sacado de lo seguro: la lista que publica el
/// reloj del motor, el documento de aire (escena, negro y placa) y la foto del hilo de
/// salidas (transmite, reconecta, graba). Sobre `copia` (resumenSalidas de la última
/// copia de la interfaz): lo que no se sabe sin ella (la armada, el color, los destinos)
/// queda como estaba, viejo.
json salidasAlDia(const json& copia, const FuentesDeFoto& f) {
    std::map<std::string, json> deLaCopia;
    if (copia.is_array())
        for (const auto& s : copia)
            if (s.is_object()) deLaCopia[s.value("id", std::string())] = s;
    json lista = f.salidasPublicadas ? f.salidasPublicadas() : json::array();
    if (!lista.is_array() || lista.empty()) lista = copia.is_array() ? copia : json::array();
    const json aire = f.aire ? f.aire().value("salidas", json::object()) : json::object();
    const json motor = f.salidasMotor ? f.salidasMotor().value("salidas", json::object()) : json::object();
    json out = json::array();
    for (const auto& p : lista) {
        if (!p.is_object()) continue;
        const std::string id = p.value("id", std::string());
        json s = deLaCopia.count(id) ? deLaCopia[id] : json::object();
        s["id"] = id;
        if (p.contains("nombre")) s["nombre"] = p["nombre"];
        if (p.contains("por_omision")) s["por_omision"] = p["por_omision"];
        if (const json a = aire.value(id, json::object()); a.is_object() && !a.empty()) {
            s["escena"] = a.value("escena", std::string());
            s["placa"] = a.value("placa", json::object());
        }
        if (const json m = motor.value(id, json::object()); m.is_object() && !m.empty()) {
            const json principal = m.value("principal", json::object());
            s["streaming"] = principal.value("andando", false);
            s["reconectando"] = principal.value("reconectando", false);
            s["recording"] = m.value("grabacion", json::object()).value("activa", false);
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

void registrarHerramientasDeFoto(McpCore& core, FuentesDeFoto f) {
    // --- Estado -------------------------------------------------------------------------
    // Lo seguro (el modo seguro, el latido, el documento de aire, la foto de las salidas)
    // se lee en el acto; lo demás (la sala, los relojes y niveles de las pastillas, la
    // energía) es de la interfaz: su copia. Con la interfaz trabada, lo que sale al aire
    // se pisa con lo seguro y `interfaz.al_dia` dice que el resto es viejo.
    {
        auto copia = std::make_shared<CopiaDeLaInterfaz>(f.statusInterfaz);
        Tool t;
        t.name = "status";
        t.title = ba::i18n::traducir("mcp", "Estado general");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Estado del appliance: escena al aire, transmision y grabacion (con tiempos), fps, cuadros "
            "perdidos, codificador, CPU, niveles del programa, gateway, conferencia y oradores al aire. "
            "`en_vivo`: si sale algo de verdad (transmisión o grabación): es LO que dice si estás en vivo; "
            "el guion del evento corriendo sin transmitir no lo es. `salidas`: cada salida del aparato con su escena, "
            "su armada, si transmite o graba y su negro/placa (los campos de arriba son los de la por omisión). "
            "`disco`: para cuántas horas alcanza el disco al bitrate de todo lo que se graba (grabación, copia e ISO de "
            "todas las salidas, por disco) y si alguna copia va al mismo disco que su grabación. "
            "`modo_seguro`: si el launcher la arrancó en modo seguro tras varias caídas (nivel 1-3, qué piezas están "
            "apagadas y si las escenas se restauraron). `energia`: si se cortó la luz (batería o UPS, % y minutos que "
            "quedan) y si ya se cerraron las grabaciones por eso. "
            "Buen primer llamado.");
        t.hilo = Hilo::Foto;
        t.run = [&core, f, copia](const json&, Done done) {
            const CopiaDeLaInterfaz::Lectura c = copia->leer(core, f.esperaInterfazMs);
            json out = c.valor.is_object() ? c.valor : json::object();
            if (f.modoSeguro) out["modo_seguro"] = f.modoSeguro();
            if (f.latido) out["latido"] = f.latido();
            const json aire = f.aire ? f.aire() : json::object();
            if (f.hayAire) out["hay_aire"] = f.hayAire();
            if (!c.alDia) {
                const json motor = f.salidasMotor ? f.salidasMotor() : json::object();
                const json principal = motor.value("principal", json::object());
                out["currentScene"] = aire.value("escena", std::string());
                out["streaming"] = principal.value("andando", false);
                out["recording"] = motor.value("grabacion", json::object()).value("activa", false);
                out["droppedFrames"] = principal.value("perdidos", 0);
                out["totalFrames"] = principal.value("cuadros", 0);
                out["salidas"] = salidasAlDia(out.value("salidas", json::array()), f);
                out["aire"] = aire; // la línea de tiempo y la placa, del documento
            }
            out["interfaz"] = estadoDeLaCopia(c);
            done(ToolResult().data(out));
        };
        core.addTool(std::move(t));
    }
    {
        auto copia = std::make_shared<CopiaDeLaInterfaz>(f.salidasInterfaz);
        Tool t;
        t.name = "salidas_listar";
        t.title = ba::i18n::traducir("mcp", "Las salidas del aparato");
        t.category = "salidas";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description = ba::i18n::traducir(
            "mcp",
            "Las salidas (cada una un operador completo: su escena al aire y su armada, su mezcla, sus destinos, su "
            "grabación, su repetición, su negro y su placa), la por omisión primera, con lo que hace cada una ahora; y "
            "el `tope` de salidas. Las herramientas que tocan el programa aceptan `salida` (id o nombre); sin ella, la "
            "por omisión.");
        t.hilo = Hilo::Foto;
        t.run = [&core, f, copia](const json&, Done done) {
            const CopiaDeLaInterfaz::Lectura c = copia->leer(core, f.esperaInterfazMs);
            json out = c.valor.is_object() ? c.valor : json{{"tope", 0}, {"salidas", json::array()}};
            if (!c.alDia) out["salidas"] = salidasAlDia(out.value("salidas", json::array()), f);
            out["interfaz"] = estadoDeLaCopia(c);
            done(ToolResult().data(out));
        };
        core.addTool(std::move(t));
    }
    // Lee el registro (con su candado, McpCore::recorrer) y el «hay aire» de la foto de
    // las salidas: nada de la interfaz.
    {
        Tool t;
        t.name = "mcp_aire_lista";
        t.title = ba::i18n::traducir("mcp", "Herramientas de aire");
        t.category = "estado";
        t.clase = Clase::Lectura;
        t.idempotent = true;
        t.description =
            ba::i18n::traducir("mcp", "Las herramientas que cambian el aire (cortar, cambiar de escena, sacar a alguien, parar la "
            "transmisión…). Con la transmisión o la grabación andando, si las pide algo automático (asistente, "
            "plugin, receta, disparador, voz) esperan SIEMPRE que una persona confirme. `clase`: aire (siempre) "
            "o aire_si (según los argumentos: settings de gateway/video/encoder, mute del máster, sacar un "
            "orador, PTZ de la cámara al aire). `hay_aire`: si ahora cuentan. En vivo, la voz y los disparadores "
            "solo leen o hacen lo reversible sin preguntar.");
        t.hilo = Hilo::Foto;
        t.run = [&core](const json&, Done done) {
            done(ToolResult().data(json{{"hay_aire", core.hayAire()}, {"herramientas", core.aire().lista()}}));
        };
        core.addTool(std::move(t));
    }

    // --- El equipo ------------------------------------------------------------------------
    // El muestreador mide en su hilo y guarda bajo su candado (diag::Recursos); el vigía
    // del bucle y las métricas, igual; la deriva de las placas son atómicos; el transporte
    // y la térmica, la foto que deja la vuelta de los recursos (fotoServicio).
    {
        Tool t;
        t.name = "sistema_recursos";
        t.title = ba::i18n::traducir("mcp", "Cuánto se usa de la máquina (CPU, GPU, RAM, red)");
        t.category = "equipo";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir(
            "mcp",
            "Lo que se está usando AHORA de la máquina, medido cada segundo: CPU de toda la máquina y de nuestros "
            "procesos (app, gateway, launcher: % y núcleos, memoria residente), RAM usada y total, GPU (% de uso) y "
            "memoria de video, red (Mbps que entran y salen por cada interfaz física; % del enlace si se sabe su "
            "velocidad), escritura de disco y el motor (cuadros por segundo, % de cuadros perdidos, kbps que salen). "
            "Cada recurso trae `pico` (de los últimos 10 minutos), `promedio_10s`, `estado` (normal, aviso desde el "
            "80 %, critico desde el 92 %, sostenido `umbrales.sostenido_s` segundos; desconocido si el sistema no lo "
            "dice: nunca se inventa) y `margen` (lo que le queda). `margen` general: el del recurso más cargado y cuál "
            "`limita`. Sirve para decidir si la máquina aguanta otra salida (otra composición con sus codificadores): "
            "medí antes y después de sumarla y compará el margen. Con `historial_s` (hasta 600), la historia "
            "(columnas + filas, agrupada de a `paso_s` segundos con el máximo de cada grupo). La alarma `recursos` "
            "(alert_create) avisa sola cuando la máquina se acerca al límite. `bucle_ui`: cuánto se demora el bucle "
            "de eventos de la interfaz (un turno cada 50 ms: histograma desde el arranque, p50/p99/máximo del último "
            "minuto, y los peores `culpables`: el evento o la herramienta MCP que corría cuando el turno llegó tarde). "
            "`transporte`: cómo anda cada anillo de "
            "audio y canal de video que lee el gateway de la app, en la última ventana de 10 s: `ocupacion_max` "
            "(bloques de 10 ms sin leer; lo normal es 1 a 3), `descartados` (lo que se perdió porque el gateway no "
            "llegaba; en video, cuadros salteados), `faltantes` (bloques de audio que no llegaron), `edad_p50_ms` y "
            "`edad_p99_ms` (cuánto hace que se selló lo leído), los totales y `anomalia`.") +
            " " +
            ba::i18n::traducir(
                "mcp",
                "`deriva_placas`: cada placa de audio abierta (micrófonos, interfaces) contra el reloj del estudio: `ppm` "
                "(cuánto se aparta: positivo, va rápida; una placa sana, menos de 100), `reanclajes_sin_corregir` (los "
                "saltos de 50 ms que habría sin corrección), `corregida` (la sigue un remuestreador variable), "
                "`enganchada`, `desfase_ms` y `reanclajes` (los cortes que llegaron al programa: tiene que ser 0). Dos "
                "placas con word clock o PTP del mismo dominio tienen que dar la misma ppm.") +
            " " +
            ba::i18n::traducir(
                "mcp",
                "`termica`: el nivel térmico (normal, templado, serio, critico; desconocido si el sistema no lo dice), la "
                "temperatura o el tope de frecuencia según el sistema, y `priorizar_programa`: los pasos que se apagaron "
                "por calor, en orden (superres, modelos, vistas, invitados; nunca el programa ni la grabación) y los que "
                "no tienen quien los atienda (`sin_gancho`). `metricas`: las métricas con nombre y tipo de la app "
                "(contadores, medidores y histogramas con p50/p90/p99: latencia por herramienta MCP, espera del candado "
                "del motor, bucle de la interfaz, colchón de grabación, reconexiones); las mismas que da GET /metrics.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"historial_s",
               {{"type", "integer"},
                {"minimum", 0},
                {"maximum", 600},
                {"description", ba::i18n::traducir("mcp", "segundos de historia a devolver (0 = sin historia; hasta 600)")}}},
              {"paso_s",
               {{"type", "integer"},
                {"minimum", 1},
                {"maximum", 60},
                {"description", ba::i18n::traducir("mcp", "agrupar la historia de a N segundos (el máximo de cada grupo; por "
                                                          "defecto 1, o 10 si se piden más de 120 s)")}}}}}};
        t.hilo = Hilo::Foto;
        t.run = [f](const json& a, Done done) {
            if (!f.recursos) return done(ToolResult::error(ba::i18n::traducir("mcp", "La interfaz no esta lista.")));
            const int hist = std::clamp(a.value("historial_s", 0), 0, 600);
            const int paso = std::clamp(a.value("paso_s", hist > 120 ? 10 : 1), 1, 60);
            json out = f.recursos(hist, paso);
            const json servicio = f.servicio ? f.servicio() : json::object();
            // Los contadores del transporte A→B los mide el gateway y viajan en la foto de la sala.
            out["transporte"] = servicio.is_object() ? servicio.value("transporte", json::array()) : json::array();
            if (out.value("muestras", 0) == 0)
                out["nota"] = ba::i18n::traducir("mcp", "Todavía no hay mediciones (se mide una vez por segundo desde el arranque).");
            out["bucle_ui"] = ba::diag::VigiaBucle::interfaz().informe();
            out["deriva_placas"] = f.derivaPlacas ? f.derivaPlacas() : json::array();
            // La política «priorizar programa» por calor (F4-05) y las métricas del proceso (F4-01).
            out["termica"]["priorizar_programa"] =
                servicio.is_object() ? servicio.value("termica", json::object()) : json::object();
            out["metricas"] = ba::diag::Metricas::proceso().json();
            done(ToolResult().data(out));
        };
        core.addTool(std::move(t));
    }
    // El registro de GStreamer tiene su candado (al arrancar ya se revisa desde otro hilo).
    {
        Tool t;
        t.name = "gst_elementos";
        t.title = ba::i18n::traducir("mcp", "Elementos de GStreamer que hacen falta");
        t.category = "equipo";
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Si están los elementos de GStreamer que usa el aparato: los imprescindibles (sin ellos algo de hoy no anda: "
            "invitados, racks, voces, NDI…) y los del motor que viene (mezcla, composición, robustez, subtítulos). "
            "Para diagnosticar silencio o negro en una plataforma: un plugin que falta aparece acá.");
        t.hilo = Hilo::Foto;
        t.run = [f](const json&, Done done) {
            const json l = f.elementosGst ? f.elementosGst() : json::array();
            json faltan = json::array();
            for (const auto& e : l)
                if (!e.value("esta", true)) faltan.push_back(e);
            done(ToolResult().data({{"faltan", faltan}, {"todos", l}}));
        };
        core.addTool(std::move(t));
    }

    // --- El registro ----------------------------------------------------------------------
    // Archivos que se leen sin tocarlos, y los contadores del registro (atómicos).
    {
        Tool t;
        t.name = "log_leer";
        t.title = ba::i18n::traducir("mcp", "Leer el registro");
        t.category = "registro";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Las últimas líneas del registro, de los tres procesos, con filtros.");
        t.details = ba::i18n::traducir("mcp", "Cada línea trae hora (UTC, en milisegundos desde la época), nivel, módulo, código y "
                    "descripción.\n\n"
                    "Los NIVELES, de menos a más grave: traza, depuracion, info, aviso, error, fatal. "
                    "`minimo` filtra de ese para arriba; el que trae por defecto es `aviso`, que es lo que "
                    "conviene mirar primero cuando algo anda mal.\n\n"
                    "Los MÓDULOS que suele haber: `app` (lo que no dice de dónde viene: printf y salida "
                    "cruda), `qml` (lo que se rompe en la interfaz), `qt`, `obs` (el motor de emisión, muy "
                    "hablador), `gateway` (el Proceso B: conferencia, WebRTC, túnel), `launcher`, `graficos`, "
                    "`registro` y `marca`. `log_leer` sin filtros te devuelve cuáles hay ahora.\n\n"
                    "Cómo usarlo bien cuando algo falla:\n"
                    "  1. Primero sin filtros y con `minimo: \"aviso\"`: si hay un error, está ahí.\n"
                    "  2. Si no aparece nada, `minimo: \"info\"` y el módulo que sospeches.\n"
                    "  3. Si sigue sin aparecer, `log_nivel` a depuracion o traza, hacé que el problema "
                    "pase otra vez, y volvé a leer. Acordate de dejar el nivel como estaba.\n\n"
                    "Ojo: lo que está por DEBAJO del nivel al que el proceso está registrando no se escribe "
                    "en ningún lado, así que no alcanza con pedirlo acá.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"minimo",
               {{"type", "string"},
                {"enum", {"traza", "depuracion", "info", "aviso", "error", "fatal"}},
                {"description", ba::i18n::traducir("mcp", "Desde qué nivel. Por defecto, aviso.")}}},
              {"modulo", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Uno solo. Vacío = todos.")}}},
              {"busca", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "Texto a buscar (sin distinguir mayúsculas).")}}},
              {"ultimosMin",
               {{"type", "integer"}, {"minimum", 1}, {"description", ba::i18n::traducir("mcp", "Solo lo de los últimos N minutos.")}}},
              {"cuantas", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}}}};
        t.hilo = Hilo::Foto;
        t.run = [f](const json& a, Done done) {
            ba::diag::Filtro filtro;
            filtro.minimo = ba::diag::Nivel::Aviso;
            if (a.contains("minimo")) {
                ba::diag::Nivel n{};
                if (ba::diag::nivelDeNombre(a["minimo"].get<std::string>(), n)) filtro.minimo = n;
            }
            if (a.contains("modulo") && a["modulo"].is_string() && !a["modulo"].get<std::string>().empty())
                filtro.componentes.push_back(a["modulo"].get<std::string>());
            filtro.busca = a.value("busca", std::string());
            filtro.cuantas = static_cast<std::size_t>(std::clamp(a.value("cuantas", 120), 1, 500));
            if (a.contains("ultimosMin")) {
                const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
                filtro.desdeMs = ahora - static_cast<std::int64_t>(a["ultimosMin"].get<int>()) * 60000LL;
            }

            const auto& dir = f.logs;
            json lineas = json::array();
            for (const auto& r : ba::diag::leerLogs(dir, filtro))
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
                     {"perdidasPorCola", ba::diag::registro().perdidas()},
                     {"carpeta", dir.string()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "logs_tail";
        t.title = ba::i18n::traducir("mcp", "Registros");
        t.category = "sistema";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Ultimas lineas CRUDAS de un archivo de Logs/, tal como esta escrito.");
        t.details = ba::i18n::traducir("mcp", "Para diagnosticar, usa `log_leer`: lee los mismos archivos pero ya partidos en hora, "
                    "nivel, modulo y descripcion, entrelazando los tres procesos y con filtros. Esto sirve "
                    "cuando hace falta ver el archivo TAL CUAL, sin interpretar: por ejemplo "
                    "cloudflared.log, que lo escribe un programa de terceros con su propio formato.\n\n"
                    "Los archivos: app.log (Proceso A), gateway.log (Proceso B), graficos.log, "
                    "launcher.log (supervisor) y cloudflared.log (tunel).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"log", {{"type", "string"}, {"default", "app.log"}}},
              {"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}, {"default", 80}}},
              {"grep", {{"type", "string"}}}}}};
        t.hilo = Hilo::Foto;
        t.run = [f](const json& a, Done done) {
            const QString dir = QString::fromStdString(f.logs.string());
            const QString name = QString::fromStdString(a.value("log", std::string("app.log")));
            if (name.contains('/') || name.contains('\\') || name.contains(".."))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Solo nombres de archivo de Logs/.")));
            QFile arch(dir + "/" + name);
            if (!arch.open(QIODevice::ReadOnly)) {
                QStringList logs = QDir(dir).entryList(QStringList{"*.log"}, QDir::Files);
                return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe %1. Hay: %2"),
                                                               {name.toStdString(), logs.join(", ").toStdString()})));
            }
            // Solo el final: los registros pueden ser grandes.
            if (arch.size() > 4 * 1024 * 1024) arch.seek(arch.size() - 4 * 1024 * 1024);
            const QString grep = QString::fromStdString(a.value("grep", std::string()));
            std::deque<QString> keep;
            const size_t n = a.value("lines", 80);
            while (!arch.atEnd()) {
                const QString line = QString::fromUtf8(arch.readLine()).trimmed();
                if (line.startsWith("debug:")) continue;
                if (!grep.isEmpty() && !line.contains(grep, Qt::CaseInsensitive)) continue;
                keep.push_back(line);
                if (keep.size() > n) keep.pop_front();
            }
            QString out;
            for (const QString& l : keep)
                out += l + "\n";
            done(ToolResult::ok(out.isEmpty() ? ba::i18n::traducir("mcp", "(vacio)") : out.toStdString()));
        };
        core.addTool(std::move(t));
    }

    // --- El bus de eventos ---------------------------------------------------------------
    // control::BusEventos guarda los últimos valores bajo su candado.
    {
        Tool t;
        t.name = "eventos_temas";
        t.title = ba::i18n::traducir("mcp", "Temas del bus de eventos");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Los temas que publica la app con su ultimo valor: aire/escena, aire/armada, aire/tally "
                        "{programa, preview, fuentes_programa, fuentes_preview, transmitiendo, grabando}, salida/stream, "
                        "salida/grabacion, audio/nivel/<fuente> {pico_db, volumen_db, mudo}, audio/loudness {m, s, i, "
                        "tp, lra, fuera_de_objetivo}, alarma/<regla>, entrada/senal, oradores/al_aire, "
                        "sala/participantes y plugin/<id>/<canal>. Además aire/medios, aire/dsk y aire/tbar {hacia, "
                        "posicion}. Esos son de la salida por omisión; los de cada salida llevan su id: "
                        "aire/<salida>/escena|armada|tally|medios|dsk|tbar, salida/<salida>/stream|grabacion|estado, "
                        "audio/<salida>/nivel/<fuente> (su fader y su mudo) y `salidas` {lista}. Una ligadura con el "
                        "tema sin id sigue a la salida que maneja el aparato. `patron` filtra: «+» un nivel, «#» el "
                        "resto (audio/nivel/+, aire/#). Sirven para ligaduras de la botonera y @plugin.escuchar.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"patron", {{"type", "string"}}}}}};
        t.hilo = Hilo::Foto;
        t.run = [f](const json& a, Done done) {
            done(ToolResult().data({{"temas", f.temas ? f.temas(a.value("patron", std::string())) : json::array()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "eventos_ultimo";
        t.title = ba::i18n::traducir("mcp", "Ultimo valor de un tema");
        t.category = "botonera";
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "El ultimo valor publicado en `tema` (eventos_temas), con la hora.");
        t.inputSchema = {{"type", "object"}, {"required", {"tema"}}, {"properties", {{"tema", {{"type", "string"}}}}}};
        t.hilo = Hilo::Foto;
        t.run = [f](const json& a, Done done) {
            const json u = f.ultimoTema ? f.ultimoTema(a.value("tema", std::string())) : json();
            if (u.is_null()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ese tema no se publico todavia (eventos_temas)")));
            done(ToolResult().data(u));
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
