// ============================================================================
// src/app/mcp/McpCalibracionTools.cpp — la calibración acústica de la sala, por MCP
//
// El mismo objeto que la pestaña Calibración del Mezclador (audio::Calibracion):
// lo que hace el asistente se ve en la ventana, y al revés. El procedimiento
// (docs/planes/PLAN-CALIBRACION-SALA.md): crear la sesión con las medidas, cargar
// parlantes, micrófonos (con su calibración) y puntos, medir cada parlante en
// cada punto de escucha, mirar el análisis y la propuesta, aplicar, verificar.
// ============================================================================

#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/Calibracion.hpp"
#include "audio/CalibracionAnalisis.hpp"
#include "audio/Mezclador.hpp"

#include <QTimer>

#include <memory>
#endif

namespace ba::app::mcp {

#if defined(BA_HAVE_CLAP)
namespace {

using audio::Calibracion;

// Una curva de la grilla (240 puntos de 1/24 de octava) a tercios de octava: lo
// que el asistente necesita leer, sin 240 números por curva.
json aTercios(const json& curva) {
    if (!curva.is_array() || curva.size() != audio::calib::grilla().size()) return curva;
    json r = json::array();
    for (double f : {31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0,
                     800.0, 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0,
                     12500.0, 16000.0}) {
        const double v = curva[audio::calib::indiceDe(f)].get<double>();
        r.push_back({f, std::round(v * 10) / 10});
    }
    return r;
}

// Todas las curvas de un JSON (claves conocidas) pasadas a tercios.
void compactar(json& j) {
    if (j.is_object()) {
        for (auto& [k, v] : j.items()) {
            if ((k == "curva" || k == "promedio" || k == "objetivo" || k == "previsto" || k == "despues") && v.is_array())
                v = aTercios(v);
            else if (k == "etc" || k == "ir_vista")
                v = ba::i18n::traducir("mcp", "(%n punto: se ven en la ventana)", nullptr, static_cast<int>(v.size()));
            else
                compactar(v);
        }
    } else if (j.is_array()) {
        for (auto& v : j) compactar(v);
    }
}

QString qs(const json& a, const char* k, const char* d = "") { return QString::fromStdString(a.value(k, std::string(d))); }

ToolResult resultado(const QString& e, Calibracion* c, const std::string& ok) {
    if (!e.isEmpty()) return ToolResult::error(e.toStdString());
    json d = {{"ok", ok}};
    if (!c->ultimoId().empty()) d["id"] = c->ultimoId();
    return ToolResult().data(d);
}

} // namespace
#endif

void McpService::registerCalibracionTools() {
#if defined(BA_HAVE_CLAP)
    auto* mz = qobject_cast<audio::Mezclador*>(vm_.mezcladorQml());
    if (mz == nullptr || mz->calibracion() == nullptr) return;
    Calibracion* c = mz->calibracion();
    const char* kCat = "audio_rack";

    {
        Tool t;
        t.name = "mezclador_pestana";
        t.title = ba::i18n::traducir("mcp", "Mostrar la consola o la calibración en la ventana del Mezclador");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = ba::i18n::traducir("mcp", "Cambia la pestaña de la ventana del Mezclador: consola (tiras, salidas, matriz) o calibracion "
                        "(croquis, mediciones, curvas, propuesta). Para que el operador vea lo que se le explica.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"pestana"}},
                         {"properties",
                          {{"pestana", {{"type", "string"}, {"enum", {"consola", "calibracion"}}}},
                           {"matriz", {{"type", "boolean"}, {"description", ba::i18n::traducir("mcp", "en consola: la matriz desplegada abajo")}}},
                           {"vista", {{"type", "string"},
                                      {"enum", {"respuesta", "impulso", "rt", "propuesta"}},
                                      {"description", ba::i18n::traducir("mcp", "en calibracion: curvas, impulso y ETC, RT60 por banda o la propuesta")}}}}}};
        t.run = [mz](const json& a, Done done) {
            mz->setPestana(QString::fromStdString(a.value("pestana", "consola")));
            if (a.contains("matriz")) mz->setMatrizAbierta(a.value("matriz", false));
            if (a.contains("vista")) mz->calibracion()->setVista(QString::fromStdString(a.value("vista", "respuesta")));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Pestaña: %1 (la ventana: ui_panel_window {panel: mezclador})."), {mz->pestana().toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_ver";
        t.title = ba::i18n::traducir("mcp", "La calibración de sala: sesión, análisis, propuesta y verificación");
        t.category = kCat;
        t.clase = Clase::Lectura;
        t.description =
            ba::i18n::traducir("mcp", "Lo mismo que la pestaña Calibración del Mezclador: la sesión abierta (sala, parlantes, micrófonos, "
            "puntos y mediciones), el análisis (RT60 por banda, frecuencia de Schroeder, por parlante el promedio "
            "espacial, los modos confirmados con su modo teórico, los pozos, la llegada y el nivel), la propuesta "
            "(cada filtro con su motivo, lo que NO se corrige y por qué, retardos, niveles, polaridad) y la "
            "verificación (antes contra después). También las sesiones guardadas, los dispositivos y los modelos "
            "de micrófono conocidos.");
        t.details =
            ba::i18n::traducir("mcp", "Curvas en tercios de octava [[Hz, dB]…] (dB relativos: valen las diferencias). Sin sesión: "
            "calibracion_crear. Procedimiento: crear → agregar parlantes, micrófono(s) y 5-9 puntos de escucha "
            "(a más de 1 m de las paredes, no simétricos) → medir cada parlante en cada punto → ver → aplicar → "
            "medir de nuevo con etapa verificacion. La ventana se abre con ui_panel_window {panel: mezclador}.");
        t.run = [c](const json&, Done done) {
            json d = {{"estado", c->estado().toStdString()},
                      {"mensaje", c->mensaje().toStdString()},
                      {"disponible", c->disponible()},
                      {"sesion", c->sesionJson(false)},
                      {"analisis", c->analisisJson()},
                      {"propuesta", c->propuestaJson()},
                      {"verificacion", c->verificacionJson()}};
            json ses = json::array(), sal = json::array(), ent = json::array();
            for (const auto& v : c->sesiones()) {
                const auto m = v.toMap();
                ses.push_back({{"id", m["id"].toString().toStdString()}, {"nombre", m["nombre"].toString().toStdString()},
                               {"mediciones", m["mediciones"].toInt()}});
            }
            for (const auto& v : c->salidas()) {
                const auto m = v.toMap();
                sal.push_back({{"id", m["id"].toString().toStdString()}, {"nombre", m["nombre"].toString().toStdString()},
                               {"canales", m["canales"].toInt()}});
            }
            for (const auto& v : c->entradas()) {
                const auto m = v.toMap();
                ent.push_back({{"id", m["id"].toString().toStdString()}, {"nombre", m["nombre"].toString().toStdString()},
                               {"canales", m["canales"].toInt()}});
            }
            // Las mediciones, en una línea cada una (el detalle: calibracion_medicion).
            json lista = json::array();
            for (const auto& m : d["sesion"].value("mediciones", json::array()))
                lista.push_back({{"id", m.value("id", "")}, {"parlante", m.value("parlante", "")}, {"punto", m.value("punto", "")},
                                 {"microfono", m.value("microfono", "")}, {"etapa", m.value("etapa", "")},
                                 {"directo_ms", m.value("directo_ms", 0.0)}, {"snr_db", m.value("snr_db", 0.0)},
                                 {"rt60_s", m.value("rt60_s", 0.0)}});
            if (d["sesion"].contains("mediciones")) d["sesion"]["mediciones"] = lista;
            // Los modos teóricos: los 12 más bajos (los que importan).
            if (d["analisis"].contains("modos_teoricos") && d["analisis"]["modos_teoricos"].size() > 12) {
                json t = json::array();
                for (size_t i = 0; i < 12; ++i) t.push_back(d["analisis"]["modos_teoricos"][i]);
                d["analisis"]["modos_teoricos"] = t;
            }
            // Las curvas de la propuesta (objetivo, previsto) ya están en el análisis o se ven en la ventana.
            for (auto& [pid, p] : d["propuesta"]["parlantes"].items()) {
                p.erase("objetivo");
                p.erase("previsto");
            }
            d["sesiones"] = ses;
            d["salidas"] = sal;
            d["entradas"] = ent;
            d["modelos_microfono"] = audio::calib::modelosMicrofono();
            compactar(d);
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_crear";
        t.title = ba::i18n::traducir("mcp", "Empezar (o abrir) una sesión de calibración");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Una sesión nueva para la sala de `largo` × `ancho` × `alto` metros, o con `abrir` una guardada "
                        "(su id, de calibracion_ver.sesiones).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"largo", {{"type", "number"}, {"description", ba::i18n::traducir("mcp", "m")}}},
                           {"ancho", {{"type", "number"}, {"description", ba::i18n::traducir("mcp", "m")}}},
                           {"alto", {{"type", "number"}, {"description", ba::i18n::traducir("mcp", "m")}}},
                           {"abrir", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "id de una sesión guardada")}}}}}};
        t.run = [c](const json& a, Done done) {
            if (a.contains("abrir")) return done(resultado(c->abrir(qs(a, "abrir")), c, "abierta"));
            done(resultado(c->nueva(qs(a, "nombre"), a.value("largo", 0.0), a.value("ancho", 0.0), a.value("alto", 0.0)), c,
                           "creada"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_sala";
        t.title = ba::i18n::traducir("mcp", "Medidas de la sala y curva objetivo");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = ba::i18n::traducir("mcp", "Cambia las medidas (m) y/o la pendiente de la curva objetivo (dB/octava: 0 plana, -0,8 de "
                        "fábrica para palabra en sala, como propone Toole).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"largo", {{"type", "number"}}},
                           {"ancho", {{"type", "number"}}},
                           {"alto", {{"type", "number"}}},
                           {"pendiente_db_oct", {{"type", "number"}}}}}};
        t.run = [c](const json& a, Done done) {
            QString e;
            if (a.contains("largo") || a.contains("ancho") || a.contains("alto")) {
                const json s = c->sesionJson(false).value("sala", json::object());
                e = c->setSala(a.value("largo", s.value("largo", 0.0)), a.value("ancho", s.value("ancho", 0.0)),
                               a.value("alto", s.value("alto", 0.0)));
            }
            if (e.isEmpty() && a.contains("pendiente_db_oct")) e = c->setObjetivo(a["pendiente_db_oct"].get<double>());
            done(resultado(e, c, "listo"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar un parlante, un micrófono o un punto");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "`que`: parlante (`dispositivo` = id de una salida de calibracion_ver.salidas y `canal` 0 = izquierdo, "
            "1 = derecho, -1 = todos), microfono (`dispositivo` = id de una entrada y `canal`; `calibracion`: "
            "plano, modelo con `modelo` (sm58, sm57…) o archivo con `archivo` = ruta al .txt del fabricante) o "
            "punto (`tipo`: escucha, cerca —a ~1 m de un parlante—, pared o microfono —donde está uno de la "
            "mesa—). `x`, `y` en metros desde la esquina del frente izquierdo (x a lo largo).");
        t.details = ba::i18n::traducir("mcp", "Devuelve el id nuevo. La sala simulada (no suena; con --test) es el dispositivo «sala-simulada» "
                    "en la salida Y en la entrada.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"que"}},
                         {"properties",
                          {{"que", {{"type", "string"}, {"enum", {"parlante", "microfono", "punto"}}}},
                           {"nombre", {{"type", "string"}}},
                           {"dispositivo", {{"type", "string"}}},
                           {"canal", {{"type", "integer"}}},
                           {"calibracion", {{"type", "string"}, {"enum", {"plano", "modelo", "archivo"}}}},
                           {"modelo", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}},
                           {"tipo", {{"type", "string"}, {"enum", {"escucha", "cerca", "pared", "microfono"}}}},
                           {"x", {{"type", "number"}}},
                           {"y", {{"type", "number"}}}}}};
        t.run = [c](const json& a, Done done) {
            const std::string que = a.value("que", "");
            QString e;
            if (que == "parlante")
                e = c->agregarParlante(qs(a, "nombre"), qs(a, "dispositivo"), a.value("canal", 0), a.value("x", 0.5), a.value("y", 1.0));
            else if (que == "microfono")
                e = c->agregarMicrofono(qs(a, "nombre"), qs(a, "dispositivo"), a.value("canal", 0), qs(a, "calibracion", "modelo"),
                                        qs(a, "modelo"), qs(a, "archivo"));
            else if (que == "punto")
                e = c->agregarPunto(qs(a, "nombre"), qs(a, "tipo", "escucha"), a.value("x", 1.0), a.value("y", 1.0));
            else
                e = QString::fromStdString(ba::i18n::traducir("mcp", "que: parlante, microfono o punto"));
            done(resultado(e, c, "agregado"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_editar";
        t.title = ba::i18n::traducir("mcp", "Mover o quitar algo del croquis; cambiar la calibración de un micrófono");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "Con `x`, `y`: mueve el parlante, micrófono o punto `id`. Con `quitar: true`: lo saca (sus "
                        "mediciones quedan pero no entran al análisis). Con `calibracion` (y `modelo`/`archivo`): "
                        "cambia la del micrófono. Con `borrar_medicion: true`, `id` es una medición y se borra.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"x", {{"type", "number"}}},
                           {"y", {{"type", "number"}}},
                           {"quitar", {{"type", "boolean"}}},
                           {"borrar_medicion", {{"type", "boolean"}}},
                           {"calibracion", {{"type", "string"}, {"enum", {"plano", "modelo", "archivo"}}}},
                           {"modelo", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}}}}};
        t.run = [c](const json& a, Done done) {
            const QString id = qs(a, "id");
            QString e;
            if (a.value("borrar_medicion", false)) e = c->borrarMedicion(id);
            else if (a.value("quitar", false)) e = c->quitar(id);
            else if (a.contains("calibracion")) e = c->setCalibracionMicrofono(id, qs(a, "calibracion"), qs(a, "modelo"), qs(a, "archivo"));
            else if (a.contains("x") && a.contains("y")) e = c->mover(id, a["x"].get<double>(), a["y"].get<double>());
            else e = QString::fromStdString(ba::i18n::traducir("mcp", "falta qué cambiar (x/y, quitar, calibracion o borrar_medicion)"));
            done(resultado(e, c, "listo"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_medir";
        t.title = ba::i18n::traducir("mcp", "Medir un parlante en un punto (barrido: SUENA)");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Saca un barrido exponencial (20 Hz-20 kHz, `duracion_s` 1-15, de fábrica 4) por la salida del "
            "`parlante` a `nivel_db` dBFS (de fábrica -20, nunca más de -6) y graba el `microfono` puesto en el "
            "`punto`. Espera y devuelve la medición: llegada del directo (ms), relación señal/ruido de la "
            "respuesta impulsiva (dB, hace falta 40 o más), RT60 (LSP), EDT/T20/T30 por octava, C50, polaridad "
            "y la respuesta en tercios. `etapa`: antes (de fábrica), verificacion (después de aplicar) o "
            "sustitucion (para calibrar un micrófono contra otro).");
        t.details =
            ba::i18n::traducir("mcp", "SUENA FUERTE: confirmá con el operador que no hay público, que los micrófonos en vivo están "
            "cerrados y el volumen del equipo bajo; subí de a poco. No mide con la emisión o la grabación en "
            "curso. Con el micrófono cardioide (SM58): apuntado al parlante, a más de 1 m.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"parlante", "punto", "microfono"}},
                         {"properties",
                          {{"parlante", {{"type", "string"}}},
                           {"punto", {{"type", "string"}}},
                           {"microfono", {{"type", "string"}}},
                           {"etapa", {{"type", "string"}, {"enum", {"antes", "verificacion", "sustitucion"}}}},
                           {"duracion_s", {{"type", "number"}}},
                           {"nivel_db", {{"type", "number"}}}}}};
        t.run = [c](const json& a, Done done) {
            // Espera el final una sola vez: la conexión se corta al llegar (o por el tope).
            auto con = std::make_shared<QMetaObject::Connection>();
            auto hecho = std::make_shared<bool>(false);
            *con = QObject::connect(c, &Calibracion::medicionTerminada, c, [c, con, hecho, done](const QString& id, const QString& err) {
                QObject::disconnect(*con);
                if (*hecho) return;
                *hecho = true;
                if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                json m = c->medicionJson(id.toStdString());
                compactar(m);
                done(ToolResult().data(m));
            });
            const QString e = c->medir(qs(a, "parlante"), qs(a, "punto"), qs(a, "microfono"), qs(a, "etapa", "antes"),
                                       a.value("duracion_s", 4.0), a.value("nivel_db", -20.0));
            if (!e.isEmpty()) {
                QObject::disconnect(*con);
                *hecho = true;
                return done(ToolResult::error(e.toStdString()));
            }
            QTimer::singleShot(60000, c, [c, con, hecho, done] {
                if (*hecho) return;
                QObject::disconnect(*con);
                *hecho = true;
                c->cancelar();
                done(ToolResult::error(ba::i18n::traducir("mcp", "la medición no terminó en 60 s (cancelada)")));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_medicion";
        t.title = ba::i18n::traducir("mcp", "Una medición entera");
        t.category = kCat;
        t.clase = Clase::Lectura;
        t.description = ba::i18n::traducir("mcp", "Todo lo de la medición `id`: llegada, SNR, RT por banda, C50, polaridad, respuesta en tercios.");
        t.inputSchema = {{"type", "object"}, {"required", {"id"}}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [c](const json& a, Done done) {
            json m = c->medicionJson(a.value("id", ""));
            if (m.is_null()) return done(ToolResult::error(ba::i18n::traducir("mcp", "no está la medición")));
            compactar(m);
            done(ToolResult().data(m));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_sustitucion";
        t.title = ba::i18n::traducir("mcp", "Calibrar un micrófono contra uno de referencia");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description = ba::i18n::traducir("mcp", "La corrección del `microfono` (un SM58, por ejemplo) por sustitución: el mismo parlante y el "
                        "mismo punto medidos con un micrófono de medición (`con_referencia`, id de esa medición) y con "
                        "él (`con_microfono`). Queda guardada en el micrófono y vale en esa orientación.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"microfono", "con_referencia", "con_microfono"}},
                         {"properties",
                          {{"microfono", {{"type", "string"}}},
                           {"con_referencia", {{"type", "string"}}},
                           {"con_microfono", {{"type", "string"}}}}}};
        t.run = [c](const json& a, Done done) {
            done(resultado(c->calibrarPorSustitucion(qs(a, "microfono"), qs(a, "con_referencia"), qs(a, "con_microfono")), c,
                           "calibrado"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "calibracion_aplicar";
        t.title = ba::i18n::traducir("mcp", "Aplicar (o sacar) el EQ de sala");
        t.category = kCat;
        t.clase = Clase::Escritura;
        t.description =
            ba::i18n::traducir("mcp", "Pone los filtros de la propuesta del `parlante` en un ecualizador paramétrico de LSP («eq-sala») en "
            "el rack de `fuente` (la entrada que alimenta ese parlante, de mezclador_estado.entradas). Con "
            "`revertir: true` saca el EQ de sala de esa fuente. Los retardos y niveles propuestos no se aplican "
            "solos: se informan.");
        t.details = ba::i18n::traducir("mcp", "Después de aplicar: medir de nuevo los mismos puntos con etapa verificacion y mirar "
                    "calibracion_ver.verificacion (si empeoró, revertir).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente"}},
                         {"properties",
                          {{"parlante", {{"type", "string"}}},
                           {"fuente", {{"type", "string"}}},
                           {"revertir", {{"type", "boolean"}}}}}};
        t.run = [c](const json& a, Done done) {
            if (a.value("revertir", false)) return done(resultado(c->revertir(qs(a, "fuente")), c, "sacado"));
            done(resultado(c->aplicar(qs(a, "parlante"), qs(a, "fuente")), c, "aplicado"));
        };
        core_.addTool(std::move(t));
    }
#endif
}

} // namespace ba::app::mcp
