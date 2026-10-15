// ============================================================================
// src/app/mcp/McpPtzTools.cpp — el PTZ de las cámaras y Mejorar imagen, por MCP
//
// Lo mismo que la página «Cámaras PTZ»: operan sobre el mismo objeto
// (ptz/ControlCamaras), así que lo que se mueve por acá se ve allá en el acto.
// Un frontend para todas las cámaras: el backend (digital, VISCA, ONVIF,
// Panasonic) lo elige cada entrada y estas herramientas no cambian.
//
// En su propia categoría («camaras»): no está en el perfil del modelo
// reactivo, así que agregarlas no le cambia el prompt.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ptz/ControlCamaras.hpp"
#include "ptz/Interlock.hpp"
#include "ui/Seguidor.hpp"
#include "ui/StudioViewModel.hpp"

#include "arranque/AppRuntime.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

namespace ba::app::mcp {

namespace {

json aJson(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}

QString qarg(const json& a, const char* k) { return QString::fromStdString(a.value(k, std::string())); }

} // namespace

void McpService::registerPtzTools() {
    auto* cc = qobject_cast<ptz::ControlCamaras*>(vm_.camaras());
    if (cc == nullptr) return;
    core_.addCategory("camaras", "Cámaras PTZ",
                      "Mover las cámaras (pan, tilt, zoom, foco, presets): cada entrada de video tiene PTZ "
                      "digital (recorte y "
                      "zoom en la GPU) y, si la cámara es motorizada, su backend (VISCA, ONVIF, Panasonic). "
                      "Y «Mejorar imagen».");
    const QPointer<ptz::ControlCamaras> c = cc;
    // Interlock: la cámara AL AIRE no se mueve por pedido de algo automático.
    // La regla es la del modelo (ControlCamaras::chequearMovimiento), la misma
    // que consultan las páginas antes de preguntarle al operador. Con `forzar`
    // igual pasa por la confirmación de aire (McpAire). El `ptz_seguir` en modo
    // suave puede seguir si el interlock lo permite.
    const auto conInterlock = [c](Tool& t) {
        if (t.inputSchema.contains("properties"))
            t.inputSchema["properties"]["forzar"] = {
                {"type", "boolean"},
                {"description", "mover aunque la camara este al aire (igual pide confirmacion en vivo)"}};
        auto run = t.run;
        const std::string nombre = t.name;
        t.run = [c, run, nombre](const json& a, Done done) {
            if (c) {
                const QString e = qarg(a, "entrada");
                const QString modo = nombre == "ptz_seguir" && a.value("modo", std::string()) == "suave"
                                         ? QString::fromLatin1(ptz::kMovimientoSeguirSuave)
                                         : QString();
                const QString motivo = c->chequearMovimiento(e, a.value("forzar", false), modo);
                if (!motivo.isEmpty()) {
                    ToolResult r = ToolResult::error(motivo.toStdString() +
                                                     " Usá otra camara, o `forzar`: true (pide confirmacion al "
                                                     "operador).");
                    r.structured = json{{"camara_al_aire", true}};
                    return done(r);
                }
            }
            run(a, done);
        };
    };
    AppRuntime& rt = rt_;
    const auto resultado = [](const QString& error, const std::string& ok) {
        return error.isEmpty() ? ToolResult::ok(ok) : ToolResult::error(error.toStdString());
    };
    const json entrada = {{"type", "string"},
                          {"description", "Nombre de la entrada de video (como en ptz_camaras)."}};

    {
        Tool t;
        t.name = "ptz_camaras";
        t.title = "Las cámaras y su PTZ";
        t.category = "camaras";
        t.readOnly = true;
        t.description =
            "Cada entrada de video con su PTZ: backend (ninguno = solo digital, visca, onvif, panasonic), si "
            "contesta, qué puede (capacidades: panTilt, zoom, foco, presets…), qué mueve el joystick (modo "
            "optico|digital), el encuadre digital (x, y, zoom) y si tiene Mejorar imagen.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"entrada", entrada}}}};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            c->sincronizar();
            json d = aJson(c->estadoJson(qarg(a, "entrada")));
            // Cuáles están al aire (el interlock no las deja mover).
            const auto marcar = [&c](json& cam) {
                if (cam.is_object() && cam.contains("entrada"))
                    cam["al_aire"] = c->alAire(QString::fromStdString(cam.value("entrada", std::string())));
            };
            if (d.is_array()) for (auto& cam : d) marcar(cam);
            else if (d.is_object() && d.contains("camaras") && d["camaras"].is_array()) for (auto& cam : d["camaras"]) marcar(cam);
            else marcar(d);
            done(ToolResult().data(d));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_interlock";
        t.title = "Interlock de PTZ (no mover la camara al aire)";
        t.category = "camaras";
        t.description = "Con `activo` (de fabrica true), lo automatico (asistente, plugins, disparadores) no mueve una "
                        "camara que esta en la escena al aire: error camara_al_aire, salvo `forzar` (que pide "
                        "confirmacion). `seguir_suave_permitido`: el seguimiento en modo suave sigue igual. Sin "
                        "argumentos, muestra el ajuste.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}}, {"seguir_suave_permitido", {{"type", "boolean"}}}}}};
        t.run = [c, this](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            if (a.contains("activo") && !a.value("activo", true) && core_.hayAire())
                return done(ToolResult::error("Al aire no se apaga el interlock de PTZ."));
            QVariantMap m;
            if (a.contains("activo")) m["activo"] = a.value("activo", true);
            if (a.contains("seguir_suave_permitido")) m["seguir_suave_permitido"] = a.value("seguir_suave_permitido", true);
            if (!m.isEmpty()) c->setInterlock(m);
            done(ToolResult().data(aJson(QJsonObject::fromVariantMap(c->interlock()))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_mover";
        t.title = "Mover una cámara";
        t.category = "camaras";
        t.description = "Mueve la cámara `entrada` durante `duracion_ms` y la para: `pan` (+ derecha), "
                        "`tilt` (+ arriba), "
                        "`zoom` (+ acercar), velocidades de -1 a 1 (0,2 es lento, 1 a fondo). Mueve lo que "
                        "diga su modo: la "
                        "cabeza (óptico) o el recorte (digital).";
        t.details =
            "Para un encuadre digital exacto usá ptz_encuadre; para volver a un lugar guardado, ptz_preset. "
            "Devuelve el encuadre digital después de moverse (si el modo es digital).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"pan", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"tilt", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"zoom", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"duracion_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 10000}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "pan": 0.3, "duracion_ms": 1000})",
                      R"({"entrada": "Cámara 1", "zoom": 0.5, "duracion_ms": 1500})"};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString e = qarg(a, "entrada");
            const QString err = c->mover(e, a.value("pan", 0.0), a.value("tilt", 0.0), a.value("zoom", 0.0));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            const int ms = std::clamp(a.value("duracion_ms", 800), 100, 10000);
            QTimer::singleShot(ms, c.data(), [c, e, done]() {
                if (!c) return done(ToolResult::error("el PTZ no está disponible"));
                c->parar(e);
                // Un momento para que el digital frene con su rampa.
                QTimer::singleShot(400, c.data(), [c, e, done]() {
                    if (!c) return done(ToolResult::ok("movida"));
                    json r = {{"ok", true}};
                    const QVariantMap p = c->posicion(e);
                    if (!p.isEmpty())
                        r["encuadre_digital"] = {{"x", p["x"].toDouble()},
                                                 {"y", p["y"].toDouble()},
                                                 {"zoom", p["zoom"].toDouble()}};
                    done(ToolResult().data(r));
                });
            });
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_foco";
        t.title = "Foco de una cámara";
        t.category = "camaras";
        t.description = "Foco de la cámara `entrada`: `auto` true/false prende o apaga el autofoco; `velocidad` (-1 cerca … "
                        "1 lejos) lo mueve a mano durante `duracion_ms` y lo para.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"auto", {{"type", "boolean"}}},
                           {"velocidad", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                           {"duracion_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 5000}}}}}};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString e = qarg(a, "entrada");
            if (a.contains("auto")) {
                const QString err = c->autoFoco(e, a.value("auto", true));
                if (!err.isEmpty() || !a.contains("velocidad")) return done(err.isEmpty() ? ToolResult::ok("listo") : ToolResult::error(err.toStdString()));
            }
            if (!a.contains("velocidad")) return done(ToolResult::error("falta `auto` o `velocidad`"));
            const QString err = c->enfocar(e, a.value("velocidad", 0.0));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            QTimer::singleShot(std::clamp(a.value("duracion_ms", 600), 100, 5000), c.data(), [c, e, done]() {
                if (c) c->enfocar(e, 0.0);
                done(ToolResult::ok("enfocada"));
            });
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_home";
        t.title = "Llevar una cámara a su posición de inicio";
        t.category = "camaras";
        t.description = "La cámara `entrada` vuelve a su posición de inicio (home): centrada, sin zoom.";
        t.inputSchema = {{"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString err = c->home(qarg(a, "entrada"));
            done(err.isEmpty() ? ToolResult::ok("en home") : ToolResult::error(err.toStdString()));
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    if (auto* sg = vm_.seguidorPtz()) {
        const QPointer<Seguidor> seg = sg;
        {
            Tool t;
            t.name = "ptz_rostros";
            t.title = "Las caras que ve una cámara";
            t.category = "camaras";
            t.readOnly = true;
            t.description = "Mira la cámara `entrada` y devuelve las caras que hay: dónde (x, y, ancho, alto "
                            "en 0..1 del cuadro) y, "
                            "si la reconoce (Personas), su `id` y `nombre`. Para seguir a una: ptz_seguir.";
            t.inputSchema = {
                {"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
            t.run = [seg](const json& a, Done done) {
                if (!seg) return done(ToolResult::error("el seguimiento no está disponible"));
                const QString e = qarg(a, "entrada");
                auto conexion = std::make_shared<QMetaObject::Connection>();
                *conexion = QObject::connect(
                    seg, &Seguidor::analizado, seg,
                    [seg, e, done, conexion](const QString& cual, const QString& error) {
                        if (cual != e) return;
                        QObject::disconnect(*conexion);
                        if (!error.isEmpty()) return done(ToolResult::error(error.toStdString()));
                        const QJsonDocument d = QJsonDocument::fromVariant(seg->caras().value(e));
                        done(ToolResult().data(
                            {{"entrada", e.toStdString()},
                             {"caras",
                              json::parse(d.toJson(QJsonDocument::Compact).toStdString(), nullptr, false)}}));
                    });
                seg->analizar(e);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "ptz_seguir";
            t.title = "Seguir a una persona con el PTZ";
            t.category = "camaras";
            t.description =
                "La cámara `entrada` sigue a una cara y la mantiene encuadrada. A quién: `persona` (el id de alguien "
                "reconocido, de ptz_rostros o Personas: la vuelve a encontrar si se cruza con otro), `cara` (el número "
                "que le puso la tarjeta Rostros), `regla` (\"mayor\": la más grande; \"centro\": la más cercana al "
                "centro) o `x`, `y` (0..1 de la toma entera: la cara más cercana a ese punto). `parar: true` deja de seguir.";
            t.details =
                "Corre en el pipeline de la entrada, con tarjetas que se pueden ver y mover en Video › Racks: Rostros "
                "(barostros) ve las caras, Seguimiento (baseguir) elige y decide el encuadre, y el PTZ digital (baptz) va "
                "hacia ahí. Se hablan con eventos de GStreamer (ba/rostros, ba/encuadre, ba/ptz) en las dos direcciones, "
                "así que anda en cualquier orden. Si faltan, las tarjetas Rostros y Seguimiento se ponen solas. Con una "
                "cámara motorizada (VISCA, ONVIF) la app la mueve con pulsos según el mensaje «ba-seguir» de la tarjeta. "
                "Ajustes del encuadre (zoom, tamaño de la cara, aire, suavidad, zona muerta): video_tarjeta_ajustar sobre "
                "la tarjeta Seguimiento. El estado (siguiendo, buscando, perdida) sale en ptz_camaras y en la tarjeta.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada"}},
                             {"properties",
                              {{"entrada", entrada},
                               {"persona", {{"type", "string"}}},
                               {"nombre", {{"type", "string"}}},
                               {"cara", {{"type", "integer"}, {"description", "el número de la cara (tarjeta Rostros)"}}},
                               {"regla", {{"type", "string"}, {"enum", {"mayor", "centro"}}}},
                               {"x", {{"type", "number"}}},
                               {"y", {{"type", "number"}}},
                               {"parar", {{"type", "boolean"}}}}}};
            t.run = [seg](const json& a, Done done) {
                if (!seg) return done(ToolResult::error("el seguimiento no está disponible"));
                const QString e = qarg(a, "entrada");
                if (a.value("parar", false)) {
                    seg->dejar(e);
                    return done(ToolResult::ok("Dejó de seguir en «" + e.toStdString() + "»."));
                }
                QVariantMap obj{{"id", qarg(a, "persona")}, {"nombre", qarg(a, "nombre")}};
                if (a.contains("cara")) obj["cara"] = a.value("cara", 0);
                if (a.contains("regla")) obj["regla"] = qarg(a, "regla");
                if (a.contains("x")) obj["x"] = a.value("x", 0.5);
                if (a.contains("y")) obj["y"] = a.value("y", 0.5);
                const QString err = seg->seguir(e, obj);
                if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                done(ToolResult::ok("Siguiendo en «" + e.toStdString() +
                                    "». Para dejar: ptz_seguir {\"entrada\", \"parar\": true}."));
            };
            conInterlock(t);
        core_.addTool(std::move(t));
        }
    }
    {
        Tool t;
        t.name = "ptz_parar";
        t.title = "Parar una cámara";
        t.category = "camaras";
        t.idempotent = true;
        t.description = "Para todo movimiento de la cámara `entrada` (cabeza y recorte).";
        t.inputSchema = {
            {"type", "object"}, {"required", {"entrada"}}, {"properties", {{"entrada", entrada}}}};
        t.run = [c, resultado](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            done(resultado(c->parar(qarg(a, "entrada")), "parada"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_preset";
        t.title = "Ir a un preset o guardarlo";
        t.category = "camaras";
        t.description = "`accion` ir (de fábrica) o guardar el preset `numero` (desde 1) de la cámara "
                        "`entrada`. El preset N es la POSICIÓN número N (ptz_posiciones): guardar la regraba "
                        "donde está ahora (o crea «Preset N»), en la cámara y/o en el recorte, igual en una "
                        "digital que en una ONVIF/VISCA. Para pedirlas por nombre, ptz_ir_posicion.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "numero"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"numero", {{"type", "integer"}, {"minimum", 1}, {"maximum", 256}}},
                           {"accion", {{"type", "string"}, {"enum", {"ir", "guardar"}}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "numero": 2})",
                      R"({"entrada": "Cámara 1", "numero": 3, "accion": "guardar"})"};
        t.run = [c, resultado](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString e = qarg(a, "entrada");
            const int n = a.value("numero", 1);
            const bool guardar = a.value("accion", std::string("ir")) == "guardar";
            done(resultado(guardar ? c->guardarPreset(e, n) : c->irPreset(e, n),
                           (guardar ? "preset guardado: " : "yendo al preset ") + std::to_string(n)));
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_encuadre";
        t.title = "Encuadre digital exacto";
        t.category = "camaras";
        t.idempotent = true;
        t.description = "Recorte digital de la entrada: centro `x`, `y` (0 a 1 del cuadro; 0,5 = centro) y "
                        "`zoom` (1 = todo el "
                        "cuadro, hasta 8), llegando en `segundos` (0 = corte). Sirve en cualquier cámara, "
                        "aunque sea fija.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"x", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"y", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"zoom", {{"type", "number"}, {"minimum", 1}, {"maximum", 8}}},
                           {"segundos", {{"type", "number"}, {"minimum", 0}, {"maximum", 10}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "x": 0.3, "y": 0.4, "zoom": 2, "segundos": 1})",
                      R"({"entrada": "Cámara 1", "zoom": 1})"};
        t.run = [c, resultado](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            done(resultado(c->encuadre(qarg(a, "entrada"), a.value("x", 0.5), a.value("y", 0.5),
                                       a.value("zoom", 1.0), a.value("segundos", 1.0)),
                           "encuadre aplicado"));
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_apuntar";
        t.title = "Apuntar a un punto o cuadro de la imagen";
        t.category = "camaras";
        t.description =
            "Lo de la tarjeta PTZ con el mouse, sobre la imagen que se ve (ya recortada): `u`, `v` (0 a 1) "
            "es el "
            "punto que va al centro; con `ancho` y `alto` (0 a 1 de la imagen) se acerca hasta que ese "
            "cuadro la "
            "llene. Lo hace lo que mueve el joystick: el digital, exacto; una cámara motorizada, aproximado.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "u", "v"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"u", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"v", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"ancho", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"alto", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "u": 0.7, "v": 0.4})",
                      R"({"entrada": "Cámara 1", "u": 0.3, "v": 0.5, "ancho": 0.25, "alto": 0.3})"};
        t.run = [c, resultado](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            done(resultado(c->apuntar(qarg(a, "entrada"), a.value("u", 0.5), a.value("v", 0.5),
                                      a.value("ancho", 0.0), a.value("alto", 0.0)),
                           "apuntada"));
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_configurar";
        t.title = "Configurar el PTZ de una entrada";
        t.category = "camaras";
        t.description = "Elige el `backend` de la cámara `entrada` (ninguno = solo digital, visca, onvif, "
                        "panasonic) con su "
                        "`host` y lo que haga falta; y/o `digital` (prender el PTZ digital), `modo` (qué "
                        "mueve el joystick: "
                        "optico|digital) y `mejora` (Mejorar imagen en esta entrada: general|si|no).";
        t.details =
            "VISCA: `transporte` sony (UDP 52381, con encabezado), tcp (5678) o udp (1259); `direccion` 1-7. "
            "ONVIF: `usuario`, `clave`, `perfil` (vacío = el que tenga PTZ); ptz_buscar las encuentra. "
            "Panasonic: "
            "`usuario` y `clave` si la cámara los pide. La clave se guarda cifrada (Credenciales/), nunca en "
            "el json.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"entrada"}},
            {"properties",
             {{"entrada", entrada},
              {"backend", {{"type", "string"}, {"enum", {"ninguno", "visca", "onvif", "panasonic"}}}},
              {"host", {{"type", "string"}}},
              {"puerto", {{"type", "integer"}}},
              {"transporte", {{"type", "string"}, {"enum", {"sony", "tcp", "udp"}}}},
              {"direccion", {{"type", "integer"}, {"minimum", 1}, {"maximum", 7}}},
              {"usuario", {{"type", "string"}}},
              {"clave", {{"type", "string"}}},
              {"perfil", {{"type", "string"}}},
              {"digital", {{"type", "boolean"}}},
              {"modo", {{"type", "string"}, {"enum", {"optico", "digital"}}}},
              {"mejora", {{"type", "string"}, {"enum", {"general", "si", "no"}}}}}}};
        t.examples = {
            R"({"entrada": "Cámara 1", "backend": "visca", "host": "192.168.1.50", "transporte": "sony"})",
            R"({"entrada": "Cámara 2", "backend": "onvif", "host": "192.168.1.64", "usuario": "admin", "clave": "..."})",
            R"({"entrada": "Webcam", "mejora": "si"})"};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString e = qarg(a, "entrada");
            QString err;
            if (a.contains("backend")) {
                QVariantMap conf;
                for (const char* k : {"host", "transporte", "usuario", "clave", "perfil"})
                    if (a.contains(k)) conf[k] = qarg(a, k);
                for (const char* k : {"puerto", "direccion"})
                    if (a.contains(k)) conf[k] = a.value(k, 0);
                err = c->configurar(e, qarg(a, "backend"), conf);
            }
            if (err.isEmpty() && a.contains("digital")) err = c->setDigital(e, a.value("digital", true));
            if (err.isEmpty() && a.contains("mejora")) err = c->setMejora(e, qarg(a, "mejora"));
            if (err.isEmpty() && a.contains("modo")) err = c->setModo(e, qarg(a, "modo"));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(aJson(c->estadoJson(e))));
        };
        core_.addTool(std::move(t));
    }
    // --- Posiciones con nombre -------------------------------------------------
    const json posicion = {
        {"type", "string"},
        {"description", "La posición: su id o su nombre (sin importar mayúsculas ni tildes)."}};
    const auto mapaAJson = [](const QVariantMap& m) {
        return json::parse(
            QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(),
            nullptr, false);
    };
    {
        Tool t;
        t.name = "ptz_posiciones";
        t.title = "Las posiciones con nombre";
        t.category = "camaras";
        t.readOnly = true;
        t.description =
            "Las posiciones guardadas de una cámara (o de todas): id, `numero` (su número de preset: "
            "ptz_preset y los controles por número van a la posición de ese número), nombre, comentario "
            "(sirven para pedirlas de palabra: «la cámara 2 al atril»), la definición en que se registró, el encuadre digital (x, y, zoom), la memoria de la cámara donde quedó grabada, la "
            "velocidad y los segundos con que se va, y `miniatura_ruta` (la foto JPEG; se puede mirar con "
            "image_view o mandarla a quien la pida).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"entrada", entrada}}}};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            c->sincronizar();
            json out = json::object();
            const QString solo = qarg(a, "entrada");
            for (const QVariant& v : c->camaras()) {
                const QString e = v.toMap()["entrada"].toString();
                if (!solo.isEmpty() && e != solo) continue;
                out[e.toStdString()] =
                    aJson(QJsonObject{{"p", QJsonArray::fromVariantList(c->posiciones(e))}})["p"];
            }
            if (!solo.isEmpty() && out.empty())
                return done(ToolResult::error("No hay una entrada de video «" + solo.toStdString() + "»"));
            done(ToolResult().data({{"posiciones", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_posicion_crear";
        t.title = "Guardar la posición actual con un nombre";
        t.category = "camaras";
        t.description =
            "Registra DONDE ESTÁ AHORA la cámara `entrada` como una posición con `nombre` (único en la "
            "cámara) y un "
            "`comentario`: el encuadre digital y, si la cámara es motorizada, la graba en una memoria de la "
            "cámara. "
            "Guarda también la definición de la entrada y una FOTO (miniatura de 320 px, sale sola un "
            "instante después). `velocidad` (0..1, 0 = la de la cámara) es la de la cabeza al volver; "
            "`segundos` los del recorte digital. Primero encuadrá (ptz_mover, ptz_encuadre).";
        const json velocidad = {{"type", "number"},
                                {"minimum", 0},
                                {"maximum", 1},
                                {"description", "Velocidad de la cabeza al ir (0 = la de la cámara)."}};
        const json segundos = {{"type", "number"},
                               {"minimum", -1},
                               {"maximum", 10},
                               {"description", "Segundos del recorte digital al ir (-1 = sin fijar)."}};
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "nombre"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"nombre", {{"type", "string"}}},
                           {"comentario", {{"type", "string"}}},
                           {"numero", {{"type", "integer"}, {"minimum", 1}, {"description", "su número de preset (si no, el primero libre)"}}},
                           {"velocidad", velocidad},
                           {"segundos", segundos}}}};
        t.examples = {
            R"({"entrada": "Cámara 1", "nombre": "Atril", "comentario": "Plano medio del orador, con el logo"})"};
        t.run = [c, mapaAJson](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QVariantMap r =
                c->crearPosicion(qarg(a, "entrada"), qarg(a, "nombre"), qarg(a, "comentario"),
                                 a.value("velocidad", 0.0), a.value("segundos", -1.0), a.value("numero", 0));
            if (!r["ok"].toBool()) return done(ToolResult::error(r["error"].toString().toStdString()));
            done(ToolResult().data(mapaAJson(r)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_posicion_editar";
        t.title = "Cambiar una posición";
        t.category = "camaras";
        t.idempotent = true;
        t.description = "Cambia el `nombre`, el `comentario`, el `numero` de preset, la `velocidad` (0..1) o los `segundos` de una "
                        "posición; con `regrabar: true` la vuelve a registrar donde está ahora la cámara "
                        "(encuadre, memoria, definición y foto); con `miniatura: true` solo saca la foto de nuevo.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "posicion"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"posicion", posicion},
                           {"nombre", {{"type", "string"}}},
                           {"comentario", {{"type", "string"}}},
                           {"numero", {{"type", "integer"}, {"minimum", 1}}},
                           {"velocidad", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                           {"segundos", {{"type", "number"}, {"minimum", -1}, {"maximum", 10}}},
                           {"regrabar", {{"type", "boolean"}}},
                           {"miniatura", {{"type", "boolean"}}}}}};
        t.run = [c, mapaAJson](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            QVariantMap cambios;
            if (a.contains("nombre")) cambios["nombre"] = qarg(a, "nombre");
            if (a.contains("comentario")) cambios["comentario"] = qarg(a, "comentario");
            if (a.contains("numero")) cambios["numero"] = a.value("numero", 0);
            if (a.contains("regrabar")) cambios["regrabar"] = a.value("regrabar", false);
            if (a.contains("miniatura")) cambios["miniatura"] = a.value("miniatura", false);
            if (a.contains("velocidad")) cambios["velocidad"] = a.value("velocidad", 0.0);
            if (a.contains("segundos")) cambios["segundos"] = a.value("segundos", -1.0);
            const QVariantMap r = c->editarPosicion(qarg(a, "entrada"), qarg(a, "posicion"), cambios);
            if (!r["ok"].toBool()) return done(ToolResult::error(r["error"].toString().toStdString()));
            done(ToolResult().data(mapaAJson(r)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_posicion_borrar";
        t.title = "Borrar una posición";
        t.category = "camaras";
        t.destructive = true;
        t.description = "Borra una posición de la cámara `entrada` (y libera su memoria en la cámara).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "posicion"}},
                         {"properties", {{"entrada", entrada}, {"posicion", posicion}}}};
        t.run = [c, resultado](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            done(resultado(c->borrarPosicion(qarg(a, "entrada"), qarg(a, "posicion")), "posición borrada"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_ir_posicion";
        t.title = "Llevar la cámara a una posición";
        t.category = "camaras";
        t.description =
            "Lleva la cámara `entrada` a la posición (id o nombre): la cabeza a su memoria y el recorte a su "
            "encuadre, en `segundos` (0 = corte; si no se dice, los de la posición o 1). La cabeza va con la "
            "velocidad de la posición. Avisa si la definición cambió desde que se "
            "registró.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "posicion"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"posicion", posicion},
                           {"segundos", {{"type", "number"}, {"minimum", 0}, {"maximum", 10}}}}}};
        t.examples = {R"({"entrada": "Cámara 1", "posicion": "Atril"})",
                      R"({"entrada": "Cámara 1", "posicion": "3", "segundos": 0})"};
        t.run = [c, mapaAJson](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QVariantMap r =
                c->irAPosicion(qarg(a, "entrada"), qarg(a, "posicion"), a.value("segundos", -1.0));
            if (!r["ok"].toBool()) return done(ToolResult::error(r["error"].toString().toStdString()));
            done(ToolResult().data(mapaAJson(r)));
        };
        conInterlock(t);
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_posicion_foto";
        t.title = "La foto de una posición";
        t.category = "camaras";
        t.readOnly = true;
        t.description = "La miniatura de una posición (cómo se veía cuando se registró), como imagen. Si no "
                        "tiene, sacala con ptz_posicion_editar {miniatura: true} (la cámara tiene que estar ahí).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "posicion"}},
                         {"properties", {{"entrada", entrada}, {"posicion", posicion}}}};
        t.run = [c](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QString clave = qarg(a, "posicion");
            for (const QVariant& v : c->posiciones(qarg(a, "entrada"))) {
                const QVariantMap m = v.toMap();
                if (m["id"].toString() != clave && m["nombre"].toString().compare(clave, Qt::CaseInsensitive) != 0) continue;
                QFile f(m.value("miniatura_ruta").toString());
                if (!f.open(QIODevice::ReadOnly))
                    return done(ToolResult::error("la posición «" + m["nombre"].toString().toStdString() + "» no tiene foto"));
                ToolResult r;
                r.text("«" + m["nombre"].toString().toStdString() + "» (" + m.value("miniatura_ruta").toString().toStdString() + ")");
                r.image(f.readAll().toBase64().toStdString(), "image/jpeg");
                return done(r);
            }
            done(ToolResult::error("No hay una posición «" + clave.toStdString() + "» en esa entrada"));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "ptz_buscar";
        t.title = "Buscar cámaras ONVIF en la red";
        t.category = "camaras";
        t.readOnly = true;
        t.openWorld = true;
        t.description = "Busca cámaras ONVIF en la red local (3 s) y devuelve su dirección, nombre y modelo.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [c](const json&, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            c->buscarOnvif();
            QTimer::singleShot(3300, c.data(), [c, done]() {
                if (!c) return done(ToolResult::error("el PTZ no está disponible"));
                json lista = json::array();
                for (const QVariant& v : c->encontradas()) {
                    const QVariantMap m = v.toMap();
                    lista.push_back({{"direccion", m["direccion"].toString().toStdString()},
                                     {"host", m["host"].toString().toStdString()},
                                     {"nombre", m["nombre"].toString().toStdString()},
                                     {"modelo", m["modelo"].toString().toStdString()}});
                }
                done(ToolResult().data({{"encontradas", lista}}));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "video_mejora";
        t.title = "Mejorar imagen (general)";
        t.category = "camaras";
        t.idempotent = true;
        t.description =
            "Mejorar imagen en las cámaras que siguen lo general (Configuración → Cámaras y entradas): `activa`, y la "
            "intensidad "
            "de `ruido`, `color` (color pegado al brillo: el LED, la corbata) y `nitidez`, de 0 a 100. Sin "
            "argumentos, dice cómo está. Por entrada: ptz_configurar `mejora`.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activa", {{"type", "boolean"}}},
                           {"ruido", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                           {"color", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}},
                           {"nitidez", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100}}}}}};
        t.examples = {R"({"activa": true})", R"({"nitidez": 70})"};
        t.run = [c, &rt](const json& a, Done done) {
            MejoraConfig& m = rt.config().mejora;
            const bool cambia =
                a.contains("activa") || a.contains("ruido") || a.contains("color") || a.contains("nitidez");
            if (a.contains("activa")) m.activa = a.value("activa", m.activa);
            if (a.contains("ruido")) m.ruido = std::clamp(a.value("ruido", m.ruido), 0, 100);
            if (a.contains("color")) m.color = std::clamp(a.value("color", m.color), 0, 100);
            if (a.contains("nitidez")) m.nitidez = std::clamp(a.value("nitidez", m.nitidez), 0, 100);
            if (cambia) {
                rt.save();
                if (c) c->sincronizar();
            }
            done(ToolResult().data(
                {{"activa", m.activa}, {"ruido", m.ruido}, {"color", m.color}, {"nitidez", m.nitidez}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
