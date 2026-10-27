// ============================================================================
// src/app/mcp/McpTotemTools.cpp — el registro de personas por tótem, por MCP
//
// Lo mismo que Personas → Registro (ui/TotemsViewModel, el mismo modelo): armar
// los tótems del evento abierto (qué se pregunta y cómo), dar su url y su QR,
// proyectarlos en una TV, ver quién se está registrando ahora y lo que
// contestaron los registrados (va al padrón, en `datos`).
//
// (La categoría es «totems» y no «registro»: «registro» ya son los logs.)
//
// Y la categoría «identidad»: leer un documento de identidad por su código de
// barras (documento_leer, documento_tipos), con el lector del tótem.
// ============================================================================
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QPointer>

#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"
#include "ui/FrameService.hpp"
#include "ui/LectorDocumentos.hpp"
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
          {"tipo", {{"type", "string"}, {"enum", {"texto", "nombre", "correo", "telefono", "numero", "sino", "opcion", "documento"}}}},
          {"obligatorio", {{"type", "boolean"}, {"description", "si no, a los dos reintentos se saltea"}}},
          {"opciones", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "solo tipo opcion (al menos dos)"}}},
          {"ejemplo", {{"type", "string"}, {"description", "se dice si no se entiende"}}},
          {"documentos", {{"type", "array"}, {"items", {{"type", "string"}}},
                          {"description", "solo tipo documento: los tipos aceptados (documento_tipos; vacío = cualquiera)"}}}}},
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
            {"ficha", {{"type", "string"}, {"description", "la ficha del evento: lo que el asistente puede contestar si le preguntan (horarios, lugar, salas), en texto libre, hasta 4000 letras"}}},
            {"conModelo", {{"type", "boolean"}, {"description", "entender con el modelo del rol «registro» (respuestas de corrido, correcciones, preguntas); apagado, el guion solo"}}},
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
                    "com»), `telefono` (dígitos o palabras), `numero`, `sino`, `opcion` (con `opciones`), `texto` y "
                    "`documento` (muestra su DNI a la cámara y se lee el código de barras —si no sale y hay un modelo con visión "
                    "(el local con su complemento de visión, o el propio), le pide el dorso y se leen los renglones de atrás—; `documentos`: los tipos "
                    "aceptados, de `documento_tipos`; completa el nombre y las claves apellido/nombres/dni/"
                    "nacimiento/cuil).\n\n"
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
                out["aviso"] = "El servidor todavía no dio su dirección: la url no tiene el comienzo.";
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
                        "asistente y qué lleva anotado; si falta algo para hablar (voz a texto, voz); con qué entiende "
                        "(`agente`; y por conexión `modo`: agente = el modelo del rol «registro», guion = el guion solo, "
                        "con `modoPorQue`). La cámara de una conexión con `canal` k se ve con capture_feed {key: \"registro:k\"}.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [tv](const json&, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            done(ToolResult().data(tv()->estadoJson()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "totem_cortar";
        t.title = "Cortar una conexión de un tótem";
        t.category = "totems";
        t.description = "Corta la conexión `conn` (de totem_estado): lo que se estaba conversando se pierde sin "
                        "guardar y su canal queda libre. La página del tótem vuelve a conectarse sola (es lo mismo "
                        "que la ✕ de la pestaña Registro).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"conn", {{"type", "string"}, {"description", "la conexión, p. ej. \"c3\""}}}}},
                         {"required", {"conn"}}};
        t.run = [tv](const json& a, Done done) {
            if (tv() == nullptr) return done(ToolResult::error("No hay registro por tótem en este proceso."));
            const std::string conn = a.value("conn", std::string());
            const json est = tv()->estadoJson();
            bool hay = false;
            for (const auto& c : est.value("conexiones", json::array())) hay = hay || c.value("conn", std::string()) == conn;
            if (!hay) return done(ToolResult::error("No hay ninguna conexión «" + conn + "» (totem_estado)."));
            tv()->cortar(QString::fromStdString(conn));
            done(ToolResult::ok("Cortada."));
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

    // --- Documentos de identidad (docs/funciones/DOCUMENTOS.md) ----------------------
    // El mismo lector que el paso «documento» del tótem (ui/LectorDocumentos). Lo que
    // devuelven NO trae los datos completos: el asistente puede ser un modelo en la
    // nube, y un DNI no sale de la máquina (queda en el padrón del evento).
    core_.addCategory("identidad", "Documentos de identidad",
                      "Leer un DNI (u otro documento con código de barras: PDF417, QR…, o por el dorso: los renglones "
                      "de la zona de lectura mecánica) de una cámara o una imagen, y qué tipos de documento se "
                      "entienden (de fábrica y de plugins). No son los PDF del evento.");
    {
        Tool t;
        t.name = "documento_tipos";
        t.title = "Qué documentos de identidad entiende";
        t.category = "identidad";
        t.readOnly = true;
        t.description = "Los tipos de documento de identidad que se pueden leer por su código de barras: los de fábrica "
                        "(el DNI argentino, `dni-ar`, y cualquier cédula con zona MRZ, `mrz-td1`) y los de los plugins "
                        "que corren (`plugin:<plugin>/<tipo>`). Son los que se eligen en un campo `documento` de un "
                        "tótem. `dorso`: si se puede leer la parte de atrás y con qué (el modelo local con visión o el modelo "
                        "propio), o qué falta.";
        t.details = "Para sumar otro tipo (una licencia de conducir, el documento de otro país): un plugin con "
                    "`@plugin.documento` (help {\"topic\": \"plugins\"}, ejemplo `documento-aamva`).\n\n"
                    "EL DORSO: los tipos con `dorso: true` (el DNI y `mrz-td1`) se leen también por la parte de "
                    "atrás, los tres renglones de la zona de lectura mecánica. Los transcribe un modelo de visión: "
                    "primero el MODELO LOCAL (llama-server del programa con el proyector de visión: hay que bajar el "
                    "complemento «Visión para los modelos de 4B» con `modelo_local_bajar`; la foto no sale de la "
                    "máquina) y, si no, el MODELO PROPIO del operador (Config/modelo-propio.json; la foto va solo a "
                    "ese servidor). Se aceptan si cierran los verificadores del número, el nacimiento y el "
                    "vencimiento y, además, el compuesto u otra foto que coincida. Sin ninguno de los dos, "
                    "`dorso.disponible` es false (y `dorso.falta` dice qué falta) y el tótem no pide el dorso: se "
                    "tipea el número o se saltea.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [tv](const json&, Done done) {
            if (tv() == nullptr || tv()->lector() == nullptr)
                return done(ToolResult::error("No hay lector de documentos en este proceso."));
            QString falta;
            const QString con = tv()->lector()->dorsoCon(&falta);
            json dorso{{"disponible", !con.isEmpty()}};
            if (!con.isEmpty()) dorso["con"] = con.toStdString();
            if (!falta.isEmpty()) dorso[con.isEmpty() ? "falta" : "faltaLocal"] = falta.toStdString();
            done(ToolResult().data({{"tipos", tv()->lector()->tipos()},
                                    {"lectorDisponible", LectorDocumentos::disponible()},
                                    {"dorso", dorso}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "documento_leer";
        t.title = "Leer un documento de identidad";
        t.category = "identidad";
        t.readOnly = true;
        t.description = "Busca códigos de barras (PDF417, QR, DataMatrix…) en el cuadro de una cámara (`video`: una "
                        "clave de video, p. ej. «registro:1», la cámara del tótem del canal 1), en una imagen "
                        "(`archivo` o `imagen_base64`) o en un texto ya leído (`texto`), y dice si es un documento "
                        "de los tipos aceptados. Con `lado` lee también el DORSO (los renglones de la zona de lectura "
                        "mecánica, con el modelo propio). Devuelve un RESUMEN (tipo, nombre, últimos tres dígitos): los "
                        "datos completos no salen de la máquina.";
        t.details = "Sirve para probar la cámara de un tótem («¿lee el DNI?») o un tipo de documento de un plugin. "
                    "`tipos`: los aceptados en orden (vacío = todos; `documento_tipos`). Si no lee: más cerca, más "
                    "quieto, sin reflejo, del lado del código de barras.\n\n"
                    "`lado`: «frente» (solo el código de barras), «dorso» (solo los renglones de atrás: los "
                    "transcribe el modelo de visión —el local primero— y se validan con los dígitos verificadores; "
                    "unos segundos por foto) o «auto» (por defecto: el código y, si no hay un documento, el dorso si se "
                    "puede). Del dorso hace falta una foto ENTERA y nítida (`archivo`/`imagen_base64`: la de la cámara "
                    "de 1920x1080; el video `registro:<k>` llega comprimido a 960x540 y el modelo no ve los renglones). "
                    "Si cierran todos los verificadores menos el compuesto, contesta `dudosa`: llamala de nuevo con "
                    "otra foto de la misma fuente y, si coinciden, se acepta. Con `texto` y `formato` «MRZ» se "
                    "validan unos renglones ya transcriptos (ahí se exige el compuesto).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"video", {{"type", "string"}, {"description", "clave de video de la app (registro:<k>, …)"}}},
              {"archivo", {{"type", "string"}, {"description", "ruta de una imagen (PNG, JPG)"}}},
              {"imagen_base64", {{"type", "string"}, {"description", "una imagen en base64"}}},
              {"texto", {{"type", "string"}, {"description", "el texto de un código ya leído (sin imagen)"}}},
              {"formato", {{"type", "string"}, {"description", "con `texto`: PDF417, QRCode…, MRZ (vacío = cualquiera)"}}},
              {"lado", {{"type", "string"}, {"enum", {"auto", "frente", "dorso"}}, {"description", "qué leer de la imagen (por defecto auto)"}}},
              {"tipos", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "los tipos aceptados"}}}}}};
        t.examples = {R"({"video": "registro:1"})", R"({"archivo": "/ruta/dni.jpg", "tipos": ["dni-ar"]})",
                      R"({"archivo": "/ruta/dorso.jpg", "lado": "dorso"})"};
        auto confirmadores =
            std::make_shared<std::map<std::string, std::pair<std::shared_ptr<documentos::ConfirmadorMrz>, qint64>>>();
        t.run = [tv, &vm, confirmadores](const json& a, Done done) {
            if (tv() == nullptr || tv()->lector() == nullptr)
                return done(ToolResult::error("No hay lector de documentos en este proceso."));
            std::vector<std::string> tipos;
            for (const json& x : a.value("tipos", json::array()))
                if (x.is_string()) tipos.push_back(x.get<std::string>());
            const auto responder = [done](LectorDocumentos::Resultado r) {
                if (!r.error.isEmpty()) return done(ToolResult::error(r.error.toStdString()));
                json cs = json::array();
                for (const auto& c : r.codigos) {
                    json x{{"formato", c.formato}};
                    // El texto de un documento no sale; el de otro código (una url) sí, con
                    // los números largos tapados por las dudas.
                    // Los renglones de un dorso son los datos de la persona: tampoco.
                    if (!r.leido && c.formato != documentos::kFormatoMrz && !c.texto.empty())
                        x["texto"] = documentos::taparNumeros(c.texto);
                    cs.push_back(std::move(x));
                }
                json out{{"codigos", cs}, {"leido", r.leido}, {"lado", r.dorso ? "dorso" : "frente"}};
                if (!r.con.empty()) out["con"] = r.con;
                if (!r.detalle.empty()) out["detalle"] = r.detalle;
                if (r.leido) {
                    out["documento"] = documentos::resumenParaAfuera(r.datos);
                    out["aviso"] = "Los datos completos del documento quedan en esta máquina (en el padrón, si lo "
                                   "leyó un tótem); acá va solo un resumen.";
                } else if (r.dorso) {
                    if (r.dudosa) out["dudosa"] = true;
                    out["aviso"] = r.noSeVe ? "El modelo no ve los renglones del dorso: más cerca, derecho, sin reflejo."
                                   : r.dudosa
                                       ? "Leído sin el verificador compuesto: llamá de nuevo con otra foto de la misma "
                                         "fuente y, si coincide, se acepta."
                                       : "No se aceptó el dorso (" + r.detalle +
                                             "): lo que se transcribió no cierra con los dígitos verificadores.";
                } else if (r.codigos.empty()) {
                    out["aviso"] = "No se vio ningún código. Más cerca, más quieto, sin reflejo, del lado del código.";
                }
                done(ToolResult().data(out));
            };
            if (a.contains("texto") && a["texto"].is_string()) {
                documentos::Codigo c{a.value("formato", std::string()), a["texto"].get<std::string>(), {}};
                // Unos renglones ya transcriptos: con un solo texto no hay otra foto que
                // confirme, así que acá se exige el compuesto (los tipos no lo exigen).
                if (c.formato == documentos::kFormatoMrz) {
                    const documentos::LecturaMrz l = documentos::interpretarRespuestaMrz(c.texto);
                    if (l.estado != documentos::LecturaMrz::Estado::Leida)
                        return done(ToolResult().data(
                            {{"leido", false}, {"lado", "dorso"}, {"detalle", l.motivo},
                             {"aviso", "Esos renglones no se aceptan: no cierran los dígitos verificadores."}}));
                    c.texto = l.texto;
                }
                return tv()->lector()->traducir({c}, tipos, responder);
            }
            QImage img;
            if (const std::string v = a.value("video", std::string()); !v.empty()) {
                auto* frames = static_cast<FrameService*>(vm.frames());
                std::uint64_t serie = 0;
                if (frames == nullptr || !frames->latest(QString::fromStdString(v), img, serie) || img.isNull())
                    return done(ToolResult::error("No hay imagen en «" + v + "» (¿está conectada esa cámara?)."));
            } else if (const std::string f = a.value("archivo", std::string()); !f.empty()) {
                if (!img.load(QString::fromStdString(f)))
                    return done(ToolResult::error("No pude abrir la imagen «" + f + "»."));
            } else if (const std::string b = a.value("imagen_base64", std::string()); !b.empty()) {
                if (!img.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(b))))
                    return done(ToolResult::error("La imagen en base64 no se pudo leer."));
            } else {
                return done(ToolResult::error("Pasá `video`, `archivo`, `imagen_base64` o `texto`."));
            }
            const std::string lado = a.value("lado", std::string("auto"));
            LectorDocumentos* lector = tv()->lector();
            // El criterio de dos fotos, entre llamadas: uno por fuente, que se olvida al
            // minuto (una lectura dudosa vieja no confirma la de otra persona).
            const std::string fuente = a.value("video", a.value("archivo", std::string("imagen")));
            auto& [conf, cuando] = (*confirmadores)[fuente];
            const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
            if (!conf || ahora - cuando > 60000) conf = std::make_shared<documentos::ConfirmadorMrz>();
            cuando = ahora;
            const auto confirmador = conf;
            if (lado == "dorso") return lector->leerDorso(img, tipos, confirmador, responder);
            if (lado != "auto" || !lector->dorsoDisponible()) return lector->leer(img, tipos, false, responder);
            // Auto: el código del frente y, si no hay un documento, el dorso.
            QPointer<LectorDocumentos> lp(lector);
            lector->leer(img, tipos, false, [lp, img, tipos, responder, confirmador](LectorDocumentos::Resultado r) {
                if (r.leido || !r.error.isEmpty() || lp.isNull()) return responder(std::move(r));
                lp->leerDorso(img, tipos, confirmador, [responder, frente = std::move(r)](LectorDocumentos::Resultado d) mutable {
                    if (!d.error.isEmpty()) {
                        frente.detalle = "dorso: " + d.error.toStdString();
                        return responder(std::move(frente));
                    }
                    if (!d.leido) d.codigos = frente.codigos; // lo que se vio del frente (otro código)
                    responder(std::move(d));
                });
            });
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
                out["aviso"] = "El servidor todavía no dio su dirección: el link no tiene el comienzo.";
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
