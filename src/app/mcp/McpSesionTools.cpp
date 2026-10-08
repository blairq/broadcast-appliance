// ============================================================================
// src/app/mcp/McpSesionTools.cpp — la banca virtual (sesión remota), por MCP
//
// Los integrantes entran desde el navegador con su link personal y su PIN, en
// la sala o afuera (docs/funciones/SESION-REMOTA.md). Estas herramientas hablan de
// BANCAS (o del id o el nombre del integrante, como en el Cuerpo).
// ============================================================================

#include "arranque/AppRuntime.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/Quorum.hpp"
#include "asamblea/SesionRemota.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#include <algorithm>

namespace ba::app::mcp {

void McpService::registerSesionTools() {
    asamblea::SesionRemota* s = vm_.sesionRemota();
    if (s == nullptr) return;
    auto& cuerpo = vm_.cuerpo().cuerpo();
    core_.addCategory("sesion", "Banca virtual (sesión remota)",
                      "Integrantes que sesionan desde el navegador (en la sala o afuera): link personal y PIN, "
                      "habilitarlos viéndolos por la cámara, presentes por la banca virtual, registro para el acta.");

    const json quien = {{"type", "string"}, {"description", "La banca, el id o el nombre del integrante (como en el Cuerpo)."}};
    // El id del integrante a partir de banca, id o nombre.
    const auto idDe = [&cuerpo](const json& a, std::string& error) -> std::string {
        const std::string q = a.value("integrante", a.value("banca", std::string()));
        if (q.empty()) {
            error = "falta la banca (o el integrante)";
            return {};
        }
        const json i = cuerpo.buscar(q);
        if (!i.is_object() || i.empty()) {
            error = "no hay nadie con la banca o el nombre «" + q + "» en el Cuerpo";
            return {};
        }
        return i.value("id", std::string());
    };

    {
        Tool t;
        t.name = "sesion_estado";
        t.title = "Quién está en la banca virtual";
        t.category = "sesion";
        t.readOnly = true;
        t.description = "Los integrantes con link de la banca virtual: si están conectados, si esperan que los "
                        "habiliten o ya están habilitados (cuentan como presentes), desde dónde (sala o afuera) y "
                        "quién los habilitó. `presentes` es cuántos cuentan para el quórum por acá.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [s](const json&, Done done) {
            done(ToolResult().data({{"participantes", s->participantes()},
                                    {"presentes", s->presentes()},
                                    {"conectados", s->conectados()},
                                    {"origen", s->origen()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_credencial";
        t.title = "Link y PIN de un integrante";
        t.category = "sesion";
        t.destructive = true;
        t.description = "Genera el link personal y el PIN de la banca virtual de un integrante (si ya tenía, el "
                        "anterior deja de andar). El link y el PIN se muestran UNA sola vez: se le pasan al "
                        "integrante por un canal seguro (no por el chat de la transmisión).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"banca", quien}}}, {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const json r = s->generar(id);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_clave";
        t.title = "Usuario y contraseña de un integrante";
        t.category = "sesion";
        t.destructive = true;
        t.description = "Le da usuario y contraseña para entrar tipeando en …/sesion/ (sin el link). La contraseña "
                        "(12 caracteres fáciles de dictar) reemplaza a la anterior y se muestra UNA sola vez. Necesita "
                        "que ya tenga link (sesion_credencial). `usuario` opcional (de fábrica, banca<número>).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"banca", quien}, {"usuario", {{"type", "string"}}}}},
                         {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const json r = s->generarClave(id, a.value("usuario", std::string()));
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_datos";
        t.title = "Datos de contacto de un conferencista";
        t.category = "sesion";
        t.description = "Guarda su `email`, `telefono` y una `nota` (para la secretaría; no se publican).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"banca", quien}, {"email", {{"type", "string"}}}, {"telefono", {{"type", "string"}}},
                           {"nota", {{"type", "string"}}}}},
                         {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const std::string r = s->setDatos(id, a);
            if (!r.empty()) return done(ToolResult::error(r));
            done(ToolResult().data({{"id", id}, {"ok", true}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_revocar";
        t.title = "Revocar el link de un integrante";
        t.category = "sesion";
        t.destructive = true;
        t.description = "Su link deja de andar (y si está conectado, se lo corta). Para volver a entrar necesita uno nuevo.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"banca", quien}}}, {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            if (!s->revocar(id)) return done(ToolResult::error("no tenía link"));
            done(ToolResult().data({{"revocado", id}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_habilitar";
        t.title = "Habilitar a un integrante de la banca virtual";
        t.category = "sesion";
        t.description = "Con `habilitado: true` (de fábrica) cuenta como presente y, en las fases siguientes, puede "
                        "pedir la palabra y votar; con false vuelve a esperar. Solo después de VERLO por la cámara "
                        "(la interfaz muestra su foto): nunca habilites a alguien sin que una persona lo haya visto.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"banca", quien}, {"habilitado", {{"type", "boolean"}}}}},
                         {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const std::string r = s->habilitar(id, a.value("habilitado", true), a.value("quien", std::string("mcp")));
            if (!r.empty()) return done(ToolResult::error(r));
            done(ToolResult().data({{"id", id}, {"habilitado", a.value("habilitado", true)}, {"presentes", s->presentes()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_expulsar";
        t.title = "Sacar a alguien de la banca virtual";
        t.category = "sesion";
        t.destructive = true;
        t.description = "Corta su conexión (su link sigue andando: si vuelve a entrar, hay que habilitarlo de nuevo). "
                        "`motivo` es lo que ve en su pantalla.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"banca", quien}, {"motivo", {{"type", "string"}}}}},
                         {"required", {"banca"}}};
        t.run = [s, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const std::string r = s->expulsar(id, a.value("quien", std::string("mcp")), a.value("motivo", std::string()));
            if (!r.empty()) return done(ToolResult::error(r));
            done(ToolResult().data({{"expulsado", id}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_votacion";
        t.title = "Votación en los celulares";
        t.category = "sesion";
        t.description = "Los integrantes habilitados votan desde el celular (cartel rojo con el tema, las opciones y la "
                        "cuenta regresiva). Si la central abre una votación, los celulares la siguen solos; esto es para "
                        "votar SIN la central (o ver cómo va): `accion` abrir {tema, menu: si_no | si_no_abstencion | "
                        "si_no_abstencion_nppv, segundos} | pausar | reanudar | cerrar | estado | a_quorum (carga el "
                        "resultado oficial en Quórum: un voto por integrante, con el origen de cada uno; nunca sin que "
                        "una persona lo pida). `voto_s` cambia el tiempo para votar de fábrica.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"abrir", "pausar", "reanudar", "cerrar", "estado", "a_quorum"}}}},
                           {"tema", {{"type", "string"}}},
                           {"menu", {{"type", "string"}, {"enum", {"si_no", "si_no_abstencion", "si_no_abstencion_nppv"}}}},
                           {"segundos", {{"type", "integer"}, {"minimum", 0}}},
                           {"voto_s", {{"type", "integer"}, {"minimum", 0}}}}}};
        auto& vmRef = vm_;
        t.run = [s, &vmRef](const json& a, Done done) {
            if (a.contains("voto_s")) s->setVotoSegundos(a.value("voto_s", 60));
            const std::string acc = a.value("accion", std::string("estado"));
            if (acc == "abrir") {
                const QString e = s->abrirVotacionUi(QString::fromStdString(a.value("tema", std::string())),
                                                     QString::fromStdString(a.value("menu", std::string("si_no_abstencion"))),
                                                     a.value("segundos", s->votoSegundos()));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            } else if (acc == "pausar" || acc == "reanudar") {
                s->pausarVotacion(acc == "pausar");
            } else if (acc == "cerrar") {
                s->cerrarVotacionUi();
            } else if (acc == "a_quorum") {
                const json v = s->votacion();
                if (v.value("estado", std::string()) != "cerrada") return done(ToolResult::error("cerrala primero"));
                const json c = s->resultadoCombinado(json::array());
                const json t2 = c.value("totales", json::object());
                json r{{"asunto", a.value("tema", v.value("tema", std::string()))},
                       {"afirmativos", t2.value("afirmativo", 0)},
                       {"negativos", t2.value("negativo", 0)},
                       {"abstenciones", t2.value("abstencion", 0)},
                       {"presentes", s->presentes()},
                       {"nominal", c.value("nominal", json::array())},
                       {"origen", "celulares"}};
                const json res = vmRef.quorum().cargarOficial(r);
                if (res.contains("error")) return done(ToolResult::error(res.value("error", std::string())));
                return done(ToolResult().data({{"resultado", res}}));
            }
            done(ToolResult().data(s->votacion()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_consola";
        t.title = "Acceso a la consola web de la presidencia";
        t.category = "sesion";
        t.destructive = true;
        t.description = "La consola web de la presidencia (…/presidencia/): el recinto con las caras, habilitar, la fila de "
                        "oradores y su tiempo, la votación, el programa en vivo y el integrante que habla, y «apretar para "
                        "hablar» con los que pidieron la palabra. `accion` crear {nombre} (devuelve link y PIN UNA vez) | "
                        "revocar {id} | listar.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"crear", "revocar", "listar"}}}},
                           {"nombre", {{"type", "string"}}},
                           {"id", {{"type", "string"}}}}}};
        t.run = [s](const json& a, Done done) {
            const std::string acc = a.value("accion", std::string("listar"));
            if (acc == "crear") return done(ToolResult().data(s->generarConsola(a.value("nombre", std::string("Presidencia")))));
            if (acc == "revocar" && !s->revocarConsola(a.value("id", std::string())))
                return done(ToolResult::error("no hay una consola con ese id"));
            done(ToolResult().data({{"consolas", s->consolas()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_vistas";
        t.title = "Lo que ven los remotos en su celular";
        t.category = "sesion";
        t.description = "Además de su propia cámara (siempre, en un recuadro), el integrante remoto puede mirar en "
                        "su celular hasta dos canales por WebRTC y cambiar entre ellos: `canales` = [{nombre, fuente}] "
                        "con fuente «programa» (la salida principal) o el nombre de una escena. Sin argumentos dice "
                        "cuáles hay. Las escenas usan los canales de las pantallas de la sala (4 entre todos) y se "
                        "componen solo mientras haya algún remoto conectado.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"canales",
                            {{"type", "array"},
                             {"maxItems", 2},
                             {"items",
                              {{"type", "object"},
                               {"required", {"fuente"}},
                               {"properties", {{"nombre", {{"type", "string"}}}, {"fuente", {{"type", "string"}}}}}}}}}}}};
        t.examples = {R"({"canales": [{"nombre": "Salida principal", "fuente": "programa"}, {"nombre": "Plano general", "fuente": "Cámara del frente"}]})"};
        AppRuntime* rt = &rt_;
        t.run = [s, rt](const json& a, Done done) {
            if (a.contains("canales")) {
                for (const auto& c : a["canales"]) {
                    const std::string f = c.is_object() ? c.value("fuente", std::string()) : std::string();
                    if (f.empty() || f == "programa") continue;
                    const auto n = rt->engine().sceneNames();
                    if (std::find(n.begin(), n.end(), f) == n.end())
                        return done(ToolResult::error("no hay ninguna escena «" + f + "» (scene_list)"));
                }
                const std::string e = s->setVistas(a["canales"]);
                if (!e.empty()) return done(ToolResult::error(e));
            }
            done(ToolResult().data({{"canales", s->vistas()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_hablar";
        t.title = "Hablarle a un remoto (apretar para hablar)";
        t.category = "sesion";
        t.description = "Con `activo: true` el integrante remoto (`banca`) oye tu voz (tu micrófono de la sala, el "
                        "mismo con el que le hablás a los invitados) mezclada en el sonido de su celular y ve «Te están "
                        "hablando»; con false se corta. Es para hablarle un momento: no lo dejes prendido.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"banca", quien}, {"activo", {{"type", "boolean"}}}}},
                         {"required", {"banca", "activo"}}};
        t.run = [vm = &vm_, idDe](const json& a, Done done) {
            std::string e;
            const std::string id = idDe(a, e);
            if (id.empty()) return done(ToolResult::error(e));
            const QString r = vm->hablarARemoto(QString::fromStdString(id), a.value("activo", false));
            if (!r.isEmpty()) return done(ToolResult::error(r.toStdString()));
            done(ToolResult::ok(a.value("activo", false) ? "Le estás hablando." : "Listo, ya no le hablás."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "sesion_registro";
        t.title = "Registro de la banca virtual";
        t.category = "sesion";
        t.readOnly = true;
        t.description = "Quién entró, desde dónde, quién lo habilitó, intentos fallidos y bloqueos: lo que va "
                        "anexado al acta. `texto: true` lo da como en el acta.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"texto", {{"type", "boolean"}}}}}};
        t.run = [s](const json& a, Done done) {
            if (a.value("texto", false)) return done(ToolResult().data({{"texto", s->anexoActa()}}));
            done(ToolResult().data({{"registro", s->registro()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
