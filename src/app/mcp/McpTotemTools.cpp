// ============================================================================
// src/app/mcp/McpTotemTools.cpp — el registro de personas por tótem, por MCP
//
// Lo mismo que Personas → Registro (ui/TotemsViewModel, el mismo modelo): armar
// los tótems del evento abierto (qué se pregunta y cómo), dar su url y su QR,
// proyectarlos en una TV, ver quién se está registrando ahora y lo que
// contestaron los registrados (va al padrón, en `datos`).
//
// (La categoría es «totems» y no «registro»: «registro» ya son los logs.)
// ============================================================================
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/TotemsViewModel.hpp"

namespace ba::app::mcp {

namespace {

/// Las propiedades que se pueden poner en un tótem (crear y cambiar).
json propiedadesDeTotem() {
    const json campo{
        {"type", "object"},
        {"properties",
         {{"clave", {{"type", "string"}, {"description", "[a-z0-9_], única: la clave en `datos` del padrón"}}},
          {"etiqueta", {{"type", "string"}, {"description", "cómo se muestra en la pantalla"}}},
          {"pregunta", {{"type", "string"}, {"description", "cómo se pregunta en voz alta (vacía = la de fábrica del tipo)"}}},
          {"tipo", {{"type", "string"}, {"enum", {"texto", "nombre", "correo", "telefono", "numero", "sino", "opcion"}}}},
          {"obligatorio", {{"type", "boolean"}, {"description", "si no, a los dos reintentos se saltea"}}},
          {"opciones", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "solo tipo opcion (al menos dos)"}}},
          {"ejemplo", {{"type", "string"}, {"description", "se dice si no se entiende"}}}}},
        {"required", {"clave", "tipo"}}};
    return {{"nombre", {{"type", "string"}, {"description", "para el operador («Entrada principal»)"}}},
            {"activo", {{"type", "boolean"}, {"description", "apagado: su url no entra"}}},
            {"titulo", {{"type", "string"}, {"description", "encabezado de la pantalla; admite {evento}"}}},
            {"subtitulo", {{"type", "string"}}},
            {"saludo", {{"type", "string"}, {"description", "lo que dice al empezar; admite {hola}, {nombre}, {evento}"}}},
            {"despedida", {{"type", "string"}}},
            {"yaRegistrado", {{"type", "string"}, {"description", "lo que le dice a quien ya se registró en este evento"}}},
            {"confirmar", {{"type", "boolean"}, {"description", "repetir cada dato y preguntar «¿está bien?»"}}},
            {"reconocer", {{"type", "boolean"}, {"description", "saludar por el nombre a quien reconoce la cara"}}},
            {"foto", {{"type", "boolean"}, {"description", "al final, pedirle que mire a la cámara y esperar una foto de frente que sirva"}}},
            {"campos", {{"type", "array"}, {"items", campo}, {"description", "reemplaza la lista entera, en orden"}}}};
}

} // namespace

void McpService::registerTotemTools() {
    core_.addCategory("totems", "Registro de personas (tótems)",
                      "Páginas de registro para una TV con cámara y micrófono o el celular de quien escanea un QR: "
                      "la persona se registra hablando y el asistente le contesta con voz. Lo que contesta va al "
                      "padrón del evento abierto (`datos`) y queda acreditada.");
    StudioViewModel& vm = vm_;
    const auto tv = [&vm]() { return vm.totemsVm(); };

    {
        Tool t;
        t.name = "totem_listar";
        t.title = "Los tótems de registro";
        t.category = "totems";
        t.readOnly = true;
        t.description = "Los tótems de registro del evento abierto, con su configuración (qué pregunta y cómo), su url "
                        "y cuántas conexiones tiene ahora.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [tv](const json&, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            done(ToolResult().data(tv()->listarJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_crear";
        t.title = "Crear un tótem de registro";
        t.category = "totems";
        t.description = "Crea un tótem de registro en el evento abierto. De fábrica pide nombre (obligatorio) y correo; "
                        "se puede pasar todo lo demás de una (campos, saludo…).";
        t.details = "Tipos de campo: `nombre` (con mayúsculas), `correo` (dicho: «juan punto perez arroba gmail punto "
                    "com»), `telefono` (dígitos o palabras), `numero`, `sino`, `opcion` (con `opciones`) y `texto`.\n\n"
                    "Los textos se dicen en voz alta: frases cortas, de a una pregunta, en castellano rioplatense. "
                    "{evento} es el nombre del evento; {hola} es «Hola» o «Hola Ana» si la reconoce.\n\n"
                    "Después: `totem_enlace` (la url y el QR) o `totem_proyectar` (a una TV).";
        json props = propiedadesDeTotem();
        props["id"] = {{"type", "string"}, {"description", "[a-z0-9-]{1,32}; vacío = sale del nombre"}};
        t.inputSchema = {{"type", "object"}, {"properties", props}, {"required", {"nombre"}}};
        t.examples = {R"({"nombre": "Entrada principal"})",
                      R"({"nombre": "Hall", "campos": [{"clave": "nombre", "tipo": "nombre", "obligatorio": true}, {"clave": "empresa", "etiqueta": "Empresa", "tipo": "texto", "pregunta": "¿De qué empresa venís?"}]})"};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            std::string id;
            if (const std::string e = tv()->crearJson(a, id); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(tv()->totemJson(QString::fromStdString(id))));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_cambiar";
        t.title = "Cambiar un tótem de registro";
        t.category = "totems";
        t.idempotent = true;
        t.description = "Cambia lo que venga de un tótem (textos, opciones, campos: la lista entera). Si algo no valida "
                        "no cambia nada y dice qué.";
        json props = propiedadesDeTotem();
        props["id"] = {{"type", "string"}};
        t.inputSchema = {{"type", "object"}, {"properties", props}, {"required", {"id"}}};
        t.examples = {R"({"id": "hall", "confirmar": false})",
                      R"({"id": "hall", "saludo": "{hola}. Bienvenida a {evento}."})"};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            json cambios = a;
            cambios.erase("id");
            if (const std::string e = tv()->cambiarJson(id, cambios); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(tv()->totemJson(id)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_borrar";
        t.title = "Borrar un tótem de registro";
        t.category = "totems";
        t.destructive = true;
        t.description = "Borra un tótem: su url deja de andar y se corta a los que estaban conectados. Los registrados "
                        "siguen en el padrón.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            if (!tv()->borrar(QString::fromStdString(a.value("id", std::string()))))
                return done(ToolResult::error("No hay ningún tótem con ese id (totem_listar)."));
            done(ToolResult::ok("Borrado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_enlace";
        t.title = "La url y el QR de un tótem";
        t.category = "totems";
        t.description = "La url del tótem (se abre en la TV o la tablet, o se escanea su QR) y la de la pantalla "
                        "(?pantalla=1: solo muestra el QR, para un televisor sin cámara). Con `mostrar` le muestra el "
                        "QR al operador; con `renovar` la url vieja deja de andar.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"mostrar", {{"type", "boolean"}, {"description", "mostrarle el QR al operador"}}},
                           {"renovar", {{"type", "boolean"}, {"description", "token nuevo (la url vieja deja de andar)"}}}}},
                         {"required", {"id"}}};
        t.run = [tv, &vm](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            if (tv()->url(id).isEmpty()) return done(ToolResult::error("No hay ningún tótem con ese id (totem_listar)."));
            if (a.value("renovar", false)) tv()->renovar(id);
            const json tj = tv()->totemJson(id);
            const std::string url = tj.value("url", std::string());
            json out{{"id", id.toStdString()}, {"url", url}, {"urlPantalla", tj.value("urlPantalla", std::string())}};
            if (url.rfind("/registro/", 0) == 0)
                out["aviso"] = "La sala todavía no dio su dirección: la url no tiene el comienzo.";
            if (a.value("mostrar", false))
                emit vm.mostrarQr(QString::fromStdString(url), "Registro — " + QString::fromStdString(tj.value("nombre", std::string())),
                                  "Escaneá para registrarte", "verde");
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_proyectar";
        t.title = "Mandar un tótem a una TV";
        t.category = "totems";
        t.description = "Manda a un televisor de la red (pantalla_listar) la página del tótem con su QR grande: «Escaneá "
                        "para registrarte».";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "string"}}}, {"pantalla", {{"type", "string"}, {"description", "id de pantalla_listar"}}}}},
                         {"required", {"id", "pantalla"}}};
        t.run = [tv, &vm](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            auto* p = static_cast<PantallasViewModel*>(vm.pantallas());
            if (p == nullptr) return done(ToolResult::error("No hay pantallas en este proceso."));
            const QString url = tv()->urlPantalla(QString::fromStdString(a.value("id", std::string())));
            if (url.isEmpty()) return done(ToolResult::error("No hay ningún tótem con ese id (totem_listar)."));
            const QString e = p->mostrar(QString::fromStdString(a.value("pantalla", std::string())), "url", {{"url", url}});
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok("Mandado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_estado";
        t.title = "Quién se está registrando ahora";
        t.category = "totems";
        t.readOnly = true;
        t.description = "Las conexiones en vivo de los tótems: en qué paso va cada una, qué se le oyó, qué le dijo el "
                        "asistente y qué lleva anotado; y si falta algo para hablar (voz a texto, voz).";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [tv](const json&, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            done(ToolResult().data(tv()->estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_registrados";
        t.title = "Los registrados por tótem";
        t.category = "totems";
        t.readOnly = true;
        t.description = "Los que se registraron por un tótem en el evento abierto, con lo que contestaron (`datos`), el "
                        "tótem y la hora. Lo último primero.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [tv](const json&, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            done(ToolResult().data({{"registrados", tv()->registradosJson()}}));
        };
        core_.addTool(std::move(t));
    }

    // --- La atención remota (docs/funciones/REGISTRO-TOTEM.md, «Atención remota») ---------
    {
        Tool t;
        t.name = "totem_atencion_enlace";
        t.title = "El link de atención remota del registro";
        t.category = "totems";
        t.description = "El link de la página de atención del evento (…/registro-atiende/): quien lo abre (gente del "
                        "equipo, desde un celular o una compu) ve todas las conversaciones de los tótems, mira y escucha "
                        "una en vivo, la atiende ella (el asistente se calla y su voz le llega solo a ese tótem) y "
                        "completa o corrige los datos. Con `mostrar` le muestra el QR al operador; con `renovar` el link "
                        "viejo deja de andar (y se corta a quien lo tenga abierto). También dice quiénes lo tienen abierto.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"mostrar", {{"type", "boolean"}, {"description", "mostrarle el QR al operador"}}},
                           {"renovar", {{"type", "boolean"}, {"description", "token nuevo (el link viejo deja de andar)"}}}}}};
        t.run = [tv, &vm](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            if (!tv()->hayEvento()) return done(ToolResult::error("No hay un evento abierto: la atención es de un evento."));
            if (a.value("renovar", false)) tv()->renovarAtencion();
            json out = tv()->atencionJson();
            const std::string url = out.value("url", std::string());
            if (url.rfind("/registro-atiende/", 0) == 0)
                out["aviso"] = "La sala todavía no dio su dirección: el link no tiene el comienzo.";
            if (a.value("mostrar", false))
                emit vm.mostrarQr(QString::fromStdString(url), "Atención del registro",
                                  "Para el equipo: mirar y atender los tótems", "azul");
            done(ToolResult().data(out));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_atender";
        t.title = "Atender una conversación de registro";
        t.category = "totems";
        t.description = "Interviene en una conversación de un tótem (el `conn` de totem_estado). `atender`: el guion se "
                        "pausa (el asistente se calla y deja de preguntar; la pantalla del tótem dice «Te atiende…»). "
                        "Después: `guardar` (lo guarda ya, como si hubiera terminado: padrón y, si la cara no se "
                        "conocía, la libreta con la mejor foto), `devolver` (sigue el asistente desde el próximo dato que "
                        "falta) o `cancelar`. Una conversación la atiende una persona a la vez.";
        t.details = "Los datos se completan con `totem_dato`. `guardar` no anda si falta un obligatorio (dice cuál). "
                    "Atender una conexión que todavía no conversa empieza una conversación, callada.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"conn", {{"type", "string"}, {"description", "la conexión (totem_estado)"}}},
                           {"accion", {{"type", "string"}, {"enum", {"atender", "guardar", "devolver", "cancelar"}}}},
                           {"quien", {{"type", "string"}, {"description", "el nombre que ve la persona del tótem (vacío = «una persona del equipo»)"}}}}},
                         {"required", {"conn", "accion"}}};
        t.examples = {R"({"conn": "c12", "accion": "atender"})", R"({"conn": "c12", "accion": "guardar"})"};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            const QString conn = QString::fromStdString(a.value("conn", std::string()));
            const std::string accion = a.value("accion", std::string());
            std::string e;
            if (accion == "atender") e = tv()->atenderConn(conn, QString::fromStdString(a.value("quien", std::string())), {});
            else if (accion == "guardar") e = tv()->terminarConn(conn, "guardar", {});
            else if (accion == "devolver") e = tv()->terminarConn(conn, "asistente", {});
            else if (accion == "cancelar") e = tv()->terminarConn(conn, "cancelar", {});
            else e = "«accion» es atender, guardar, devolver o cancelar.";
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(tv()->estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_dato";
        t.title = "Completar o corregir un dato del registro";
        t.category = "totems";
        t.description = "Pone un dato en una conversación de un tótem (atendida o no), como si la persona lo hubiera "
                        "tipeado: sin confirmarlo en voz alta, y se ve en la pantalla del tótem. Vacío lo borra. Si no "
                        "está atendida y es el dato que se le estaba pidiendo, el asistente sigue con el próximo.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"conn", {{"type", "string"}, {"description", "la conexión (totem_estado)"}}},
                           {"clave", {{"type", "string"}, {"description", "la clave del campo (totem_listar)"}}},
                           {"valor", {{"type", "string"}, {"description", "como se escribiría («ana@gmail.com», «11 4567 8901», «sí»)"}}}}},
                         {"required", {"conn", "clave", "valor"}}};
        t.examples = {R"({"conn": "c12", "clave": "correo", "valor": "ana.gomez@gmail.com"})"};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            const std::string e = tv()->ponerDatoConn(QString::fromStdString(a.value("conn", std::string())),
                                                      QString::fromStdString(a.value("clave", std::string())),
                                                      QString::fromStdString(a.value("valor", std::string())), {});
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(tv()->estadoJson()));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
