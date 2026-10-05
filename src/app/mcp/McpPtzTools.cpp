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
#include "ui/Seguidor.hpp"
#include "ui/StudioViewModel.hpp"

#include "AppRuntime.hpp"

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
    // Con `forzar` igual pasa por la confirmación de aire (McpAire). El
    // `ptz_seguir` en modo suave puede seguir si el interlock lo permite.
    const auto conInterlock = [c](Tool& t) {
        if (t.inputSchema.contains("properties"))
            t.inputSchema["properties"]["forzar"] = {
                {"type", "boolean"},
                {"description", "mover aunque la camara este al aire (igual pide confirmacion en vivo)"}};
        auto run = t.run;
        const std::string nombre = t.name;
        t.run = [c, run, nombre](const json& a, Done done) {
            if (c) {
                const QString e = QString::fromStdString(a.value("entrada", std::string()));
                const QVariantMap il = c->interlock();
                const bool suave = nombre == "ptz_seguir" && a.value("modo", std::string()) == "suave" &&
                                   il.value("seguir_suave_permitido").toBool();
                if (!e.isEmpty() && il.value("activo").toBool() && c->alAire(e) && !a.value("forzar", false) && !suave) {
                    ToolResult r = ToolResult::error("La camara «" + e.toStdString() +
                                                     "» esta AL AIRE: moverla se ve. Usá otra camara, o `forzar`: true "
                                                     "(pide confirmacion al operador).");
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
            t.description = "La cámara `entrada` sigue a una cara y la mantiene en el centro: con el PTZ "
                            "real si tiene motor (pulsos de "
                            "pan/tilt), si no con el recorte digital. `persona`: el id de alguien reconocido "
                            "(de ptz_rostros o Personas; "
                            "la sigue aunque se cruce con otro); o `x`, `y` (0..1): la cara sin nombre que "
                            "está ahí. `parar: true` deja "
                            "de seguir. El estado (siguiendo, centrada, perdida) sale en ptz_camaras… y en "
                            "la ventana del PTZ.";
            t.inputSchema = {{"type", "object"},
                             {"required", {"entrada"}},
                             {"properties",
                              {{"entrada", entrada},
                               {"persona", {{"type", "string"}}},
                               {"nombre", {{"type", "string"}}},
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
                        "`entrada`. Los presets "
                        "son de lo que mueve el joystick: en la cámara (óptico) o en el recorte (digital).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "numero"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"numero", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
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
            "Las posiciones guardadas de una cámara (o de todas): id, nombre, comentario, la definición en "
            "que se "
            "registró, el encuadre digital (x, y, zoom) y la memoria de la cámara donde quedó grabada.";
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
            "Guarda también la definición de la entrada. Primero encuadrá (ptz_mover, ptz_encuadre).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"entrada", "nombre"}},
            {"properties",
             {{"entrada", entrada}, {"nombre", {{"type", "string"}}}, {"comentario", {{"type", "string"}}}}}};
        t.examples = {
            R"({"entrada": "Cámara 1", "nombre": "Atril", "comentario": "Plano medio del orador, con el logo"})"};
        t.run = [c, mapaAJson](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            const QVariantMap r =
                c->crearPosicion(qarg(a, "entrada"), qarg(a, "nombre"), qarg(a, "comentario"));
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
        t.description = "Cambia el `nombre` o el `comentario` de una posición; con `regrabar: true` la "
                        "vuelve a registrar donde "
                        "está ahora la cámara (encuadre, memoria y definición).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"entrada", "posicion"}},
                         {"properties",
                          {{"entrada", entrada},
                           {"posicion", posicion},
                           {"nombre", {{"type", "string"}}},
                           {"comentario", {{"type", "string"}}},
                           {"regrabar", {{"type", "boolean"}}}}}};
        t.run = [c, mapaAJson](const json& a, Done done) {
            if (!c) return done(ToolResult::error("el PTZ no está disponible"));
            QVariantMap cambios;
            if (a.contains("nombre")) cambios["nombre"] = qarg(a, "nombre");
            if (a.contains("comentario")) cambios["comentario"] = qarg(a, "comentario");
            if (a.contains("regrabar")) cambios["regrabar"] = a.value("regrabar", false);
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
            "encuadre, en `segundos` (0 = corte; de fábrica 1). Avisa si la definición cambió desde que se "
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
                c->irAPosicion(qarg(a, "entrada"), qarg(a, "posicion"), a.value("segundos", 1.0));
            if (!r["ok"].toBool()) return done(ToolResult::error(r["error"].toString().toStdString()));
            done(ToolResult().data(mapaAJson(r)));
        };
        conInterlock(t);
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
            "Mejorar imagen en las cámaras que siguen lo general (Configuración → Video): `activa`, y la "
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
