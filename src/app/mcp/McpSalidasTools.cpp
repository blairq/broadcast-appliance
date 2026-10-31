// ============================================================================
// src/app/mcp/McpSalidasTools.cpp — las salidas del aparato, por MCP
//
// docs/planes/PLAN-SALIDAS.md, etapa 3. Cada salida es un operador completo (su
// aire y su armada, su mezcla, sus destinos, su grabación…). Acá están:
//
//   - salida_crear / salida_editar / salida_borrar, por el mismo modelo que la
//     GUI (AppRuntime::crearSalida…): lo que hace el asistente lo ve el operador
//     en el acto (salidas_listar está en McpFoto.cpp: contesta con la interfaz
//     trabada);
//   - ui_salida_activa: cuál maneja la interfaz del operador;
//   - el resumen por salida que usan `status` y `salidas_listar` (uno solo; lo
//     copia la interfaz para ellas, ver McpService::fuentesDeFoto);
//   - lo que el núcleo necesita para las herramientas de salida (Tool::porSalida):
//     qué salidas hay y si una sale (McpCore::setSalidas);
//   - conmutadorDe: el conmutador de la salida de la llamada en curso, para las
//     herramientas de escenas (cada una lo pide acá, ninguna lo resuelve sola).
// ============================================================================

#include "arranque/AppRuntime.hpp"
#include "emision/VentanillaMotor.hpp"
#include "diag/Recursos.hpp"
#include "escenas/Conmutador.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/SalidasViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

escenas::Conmutador& conmutadorDe(StudioViewModel& vm, const std::string& salida) {
    // callTool ya validó la salida; si justo la borraron, la por omisión (no se cae).
    escenas::Conmutador* c = vm.conmutadores().de(salida);
    return c != nullptr ? *c : vm.conmutadores().porOmision();
}

escenas::Conmutador& conmutadorDe(const McpCore& core, StudioViewModel& vm, const json& args) {
    return conmutadorDe(vm, core.salidaDe(args));
}

json resumenSalidas(const FotoMotor& f) {
    // Lo del motor (parte Equipo) y, de cada salida, lo guardado, la armada y la placa (Salidas).
    json out = json::array();
    for (const json& s : f.equipo.motor.value("salidas", json::array())) {
        const bool porOmision = s.value("por_omision", false);
        const FotoMotor::Salida* x = f.salida(porOmision ? std::string() : s.value("id", std::string()));
        json o = s;
        o["color"] = x != nullptr ? x->color : std::string();
        o["armada"] = x != nullptr ? x->armada : std::string();
        o["codificador"] = x != nullptr ? x->codificador : std::string();
        o["placa"] = x != nullptr ? x->placa : json::object();
        out.push_back(std::move(o));
    }
    return out;
}

void McpService::registerSalidasTools() {
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    // Lo que el núcleo necesita para las herramientas de salida: qué salidas hay
    // (para validar `salida`, por id o nombre) y si una sale (la guarda de aire).
    // La lista, la que publica el reloj del motor (se lee desde cualquier hilo: el núcleo
    // valida `salida` en el hilo de la herramienta, F3-03).
    core_.setSalidas([&rt]() { return rt.salidasPublicadas(); },
        [&rt](const std::string& id) { return rt.engine().haySalidaEn(id); });

    const json idProp = {{"type", "string"},
                         {"description", QT_TRANSLATE_NOOP("mcp", "Id de la salida (letras, números, - y _).")}};
    {
        Tool t;
        t.name = "salida_crear";
        t.title = ba::i18n::traducir("mcp", "Crear una salida");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Una salida nueva, andando en el acto (con su programa, su preview y su mezcla), hasta el tope. Usa las "
            "mismas entradas y escenas que las demás. Cada salida es otra composición y, al transmitir o grabar, otros "
            "codificadores: la respuesta trae `recursos` (lo de sistema_recursos) y un `aviso` si la máquina ya está "
            "cerca del límite.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", idProp},
                           {"nombre", {{"type", "string"}}},
                           {"color", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "#RRGGBB (tally, multiview).")}}}}}};
        t.examples = {R"({"id": "youtube2", "nombre": "YouTube 2", "color": "#2E86DE"})"};
        RecursosViewModel* rec = vm_.recursos();
        // Crearla se le pide al dueño del motor; lo demás, cuando contesta (con la foto al día).
        t.run = [&rt, &vm, rec](const json& a, Done done) {
            const std::string id = a.value("id", std::string());
            vm.ventanilla().pedir(
                &vm, "salida_crear",
                [&rt, id, nombre = a.value("nombre", id), color = a.value("color", std::string())]() {
                    return rt.crearSalida(id, nombre, color);
                },
                [&vm, rec, done](const std::string& e) {
                    if (!e.empty()) return done(ToolResult::error(e));
                    json out{{"salidas", resumenSalidas(vm.ventanilla().foto())}};
                    // El margen de la máquina: el tope lo pone el operador; si aguanta, lo dice esto.
                    if (rec != nullptr) {
                        const json inf = rec->muestreador().informe(0, 1);
                        const json mg = inf.value("margen", json::object());
                        out["recursos"] = {{"estado", inf.value("estado", std::string("desconocido"))}, {"margen", mg}};
                        const std::string estado = inf.value("estado", std::string());
                        const double pct = mg.value("pct", json()).is_number() ? mg["pct"].get<double>() : -1.0;
                        if (estado == "aviso" || estado == "critico" || (pct >= 0 && pct < 20))
                            out["aviso"] = ba::i18n::llenar(
                                ba::i18n::traducir("mcp", "La máquina ya está cerca del límite (%1, margen %2 %): transmitir o "
                                                          "grabar con esta salida puede no dar abasto. Mirá sistema_recursos."),
                                {mg.value("limita", std::string("?")), pct >= 0 ? std::to_string(static_cast<int>(pct)) : "?"});
                    }
                    done(ToolResult().data(out));
                });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_editar";
        t.title = ba::i18n::traducir("mcp", "Cambiar el nombre, el color o el codificador de una salida (o el tope)");
        t.category = "salidas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`id` con `nombre`, `color` (#RRGGBB) y/o `codificador` (el de video de esa salida: auto, nvenc, qsv, amf, "
            "vaapi, videotoolbox o x264; vacío = el de la configuración; la por omisión usa siempre el de la "
            "configuración; rige desde lo próximo que arranque en ella). `tope` (1 a 16) cambia cuántas salidas "
            "puede tener el aparato (no baja de las que ya hay: borrá alguna antes).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", idProp},
                           {"nombre", {{"type", "string"}}},
                           {"color", {{"type", "string"}}},
                           {"codificador", {{"type", "string"}}},
                           {"tope", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.examples = {R"({"id": "youtube2", "nombre": "YouTube en inglés"})", R"({"id": "youtube2", "codificador": "x264"})",
                      R"({"tope": 6})"};
        t.run = [&rt, &vm](const json& a, Done done) {
            const bool cambia = a.contains("nombre") || a.contains("color") || a.contains("codificador");
            if (!cambia && !a.contains("tope"))
                return done(ToolResult::error(
                    ba::i18n::traducir("mcp", "Falta qué cambiar: `nombre`, `color`, `codificador` o `tope`.")));
            json cambios = json::object();
            for (const char* k : {"nombre", "color", "codificador"})
                if (a.contains(k)) cambios[k] = a[k];
            // En el dueño del motor; la respuesta, con la foto al día.
            vm.ventanilla().pedir(
                &vm, "salida_editar",
                [&rt, cambia, cambios, id = a.value("id", std::string()), tope = a.contains("tope") ? a.value("tope", 0) : -1]() {
                    if (tope >= 0)
                        if (std::string e = rt.setTopeSalidas(tope); !e.empty()) return e;
                    return cambia ? rt.editarSalida(id, cambios) : std::string();
                },
                [&vm, done](const std::string& e) {
                    if (!e.empty()) return done(ToolResult::error(e));
                    const FotoMotor& f = vm.ventanilla().foto();
                    done(ToolResult().data(json{{"tope", f.tope}, {"salidas", resumenSalidas(f)}}));
                });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "salida_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar una salida");
        t.category = "salidas";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "borra una salida (corta lo que sale de ella)");
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Para todo lo que sale de ella (transmisiones, grabación, repetición, señal limpia) y la saca. La por "
            "omisión no se borra. Si tiene destinos guardados, `destinos` dice qué pasa con ellos: el id de otra "
            "salida (pasan a ella) o «borrar» (se borran con su clave); sin decirlo no se borra nada y el error dice "
            "cuántos tiene. Preguntale al operador antes de borrarlos.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", idProp},
                           {"destinos",
                            {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "Sus destinos guardados: el id de otra salida o «borrar».")}}}}}};
        t.run = [&rt, &vm](const json& a, Done done) {
            vm.ventanilla().pedir(
                &vm, "salida_borrar",
                [&rt, id = a.value("id", std::string()), destinos = a.value("destinos", std::string())]() {
                    return rt.borrarSalida(id, destinos);
                },
                [&vm, done](const std::string& e) {
                    if (!e.empty()) return done(ToolResult::error(e));
                    done(ToolResult().data(json{{"salidas", resumenSalidas(vm.ventanilla().foto())}}));
                });
        };
        core_.addTool(std::move(t));
    }
    // La activa de ESTA interfaz (SalidasViewModel): la que manejan Dirección, Escenas y
    // el Mezclador del operador. No cambia nada de lo que sale ni la salida por omisión
    // de las herramientas (esas siguen con `salida`); se recuerda en Config/interfaz.json.
    {
        Tool t;
        t.name = "ui_salida_activa";
        t.title = ba::i18n::traducir("mcp", "La salida que maneja la interfaz");
        t.category = "interfaz";
        t.clase = Clase::Ui;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia la salida ACTIVA de la interfaz del operador (la que muestran y manejan Dirección, Escenas y el "
            "Mezclador), con `salida` (id o nombre, como en salidas_listar). Sin `salida`, dice cuál es. No toca lo que "
            "sale ni lo que hacen las demás herramientas: esas usan su `salida` (o la por omisión).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"salida",
                            {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "Id o nombre de la salida (salidas_listar).")}}}}}};
        t.run = [this, &vm](const json& a, Done done) {
            SalidasViewModel& s = vm.salidas();
            if (const std::string pedida = a.value("salida", std::string()); !pedida.empty()) {
                std::string err;
                const std::string id = core_.resolverSalida(pedida, err); // "" = la por omisión
                if (!err.empty()) return done(ToolResult::error(err));
                s.setActiva(QString::fromStdString(id.empty() ? std::string(BroadcastEngine::kSalidaPorOmision) : id));
            }
            const QVariantMap datos = s.activaDatos();
            done(ToolResult().data(json{{"activa", s.activa().toStdString()},
                                        {"nombre", datos.value("nombre").toString().toStdString()},
                                        {"varias", s.varias()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
