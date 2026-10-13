// ============================================================================
// src/app/mcp/McpQrTools.cpp — los codigos QR, por MCP
//
// Lo mismo que la pestaña Códigos QR y por el mismo ViewModel: lo que genera
// o guarda el asistente aparece en la pantalla del operador en el acto.
//
// Para que sirve de verdad: "hace el QR del WiFi de la sala y mandalo al
// televisor del hall", "generame el QR de este formulario para el folleto".
// Son cosas de armar una vez y usar todo el evento.
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "qr/Iconos.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// Los campos de estilo que se aceptan, con su forma. Se declaran una vez y
/// los usan las dos herramientas que dibujan.
json esquemaEstilo() {
    return json{
        {"primario", {{"type", "string"}, {"description", "color del codigo (#000000)"}}},
        {"secundario", {{"type", "string"}, {"description", "si esta, degradado de primario a este"}}},
        {"fondo", {{"type", "string"}, {"description", "#FFFFFF o \"transparent\""}}},
        {"puntos",
         {{"type", "string"},
          {"enum", {"cuadrados", "redondeados", "circulos", "separados", "muy-redondeados"}}}},
        {"ojos", {{"type", "string"}, {"enum", {"cuadrados", "redondeados"}}}},
        {"marco", {{"type", "string"}, {"enum", {"ninguno", "simple", "redondeado"}}}},
        {"correccion", {{"type", "string"}, {"enum", {"baja", "media", "alta", "maxima"}}}},
        {"icono", {{"type", "string"}, {"description", "id de un icono para el centro (qr_listar los da)"}}},
        {"iconoColor", {{"type", "string"}}}};
}

QVariantMap aMapa(const json& o) {
    QVariantMap m;
    if (!o.is_object()) return m;
    for (const auto& [k, v] : o.items()) {
        if (v.is_string())
            m[QString::fromStdString(k)] = QString::fromStdString(v);
        else if (v.is_number_float())
            m[QString::fromStdString(k)] = v.get<double>();
        else if (v.is_number_integer())
            m[QString::fromStdString(k)] = v.get<int>();
        else if (v.is_boolean())
            m[QString::fromStdString(k)] = v.get<bool>();
    }
    return m;
}

} // namespace

void McpService::registerQrTools() {
    core_.addCategory("qr", "Códigos QR",
                      "Generar códigos QR (enlace, WiFi, contacto, reunión, y los del propio aparato: "
                      "sala, teleprompter, rótulos), guardarlos en la biblioteca y exportarlos.");
    auto* q = static_cast<QrViewModel*>(vm_.qr());
    if (q == nullptr) return;
    // Los tipos que existen (los de qr_listar): un tipo mal escrito se rechaza en la
    // validación en vez de guardar en silencio el que hubiera quedado en el editor.
    json tipos = json::array();
    for (const QVariant& v : q->tipos())
        tipos.push_back(v.toMap().value("id").toString().toStdString());
    const json esquemaTipo = {{"type", "string"}, {"enum", tipos}, {"description", "uno de qr_listar (enlace para una url)"}};

    {
        Tool t;
        t.name = "qr_listar";
        t.title = "Los codigos guardados, y que se puede generar";
        t.category = "qr";
        t.readOnly = true;
        t.description = "Los codigos de la biblioteca (del aparato y del evento), los TIPOS que se pueden "
                        "generar con sus campos, y los iconos que pueden ir al centro.";
        t.details = "Los tipos con `delAparato: true` (sala, prompter, rotulos) NO llevan datos: el "
                    "contenido lo arma el sistema con la clave que corresponda en ese momento. Por eso se "
                    "guardan sin texto y salen siempre al dia.\n\n`para_elegir`: lo que acepta `qr` en "
                    "pantalla_mostrar y en una fuente «Código QR» (source_add {type: ba_qr, settings: {qr}}): los del "
                    "aparato SIEMPRE (\"aparato:publico\", \"aparato:sala\", \"aparato:prompter\", "
                    "\"aparato:rotulos\"), aunque no esten guardados, y los guardados por su id.";
        t.run = [q](const json&, Done done) {
            json guardados = json::array();
            for (const QVariant& v : q->todos()) {
                const QVariantMap m = v.toMap();
                guardados.push_back(json{{"id", m.value("id").toString().toStdString()},
                                         {"nombre", m.value("nombre").toString().toStdString()},
                                         {"tipo", m.value("tipo").toString().toStdString()},
                                         {"ambito", m.value("ambito").toString().toStdString()},
                                         {"contenido", m.value("contenido").toString().toStdString()}});
            }
            json tipos = json::array();
            for (const QVariant& v : q->tipos()) {
                const QVariantMap m = v.toMap();
                json campos = json::array();
                for (const QVariant& c : m.value("campos").toList())
                    campos.push_back(c.toMap().value("clave").toString().toStdString());
                tipos.push_back(json{{"id", m.value("id").toString().toStdString()},
                                     {"que_es", m.value("ayuda").toString().toStdString()},
                                     {"campos", campos},
                                     {"delAparato", m.value("delAparato").toBool()}});
            }
            json ic = json::array();
            for (const qr::Icono& i : qr::iconos())
                ic.push_back(i.id);
            // Para elegir en una pantalla (pantalla_mostrar {qr}) o en una escena (fuente ba_qr):
            // los del aparato siempre («aparato:publico»…) y los guardados.
            json elegir = json::array();
            for (const QVariant& v : q->paraElegir()) {
                const QVariantMap m = v.toMap();
                elegir.push_back(json{{"qr", m.value("value").toString().toStdString()}, {"que_es", m.value("label").toString().toStdString()}});
            }
            done(ToolResult().data(json{{"guardados", guardados}, {"para_elegir", elegir}, {"tipos", tipos}, {"iconos", ic}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "qr_mostrar";
        t.title = "Mostrarle al operador un QR (y para copiar)";
        t.category = "qr";
        t.readOnly = true;
        t.description =
            "Abre en la pantalla del operador una ventana con el código QR de `texto` (una URL, un enlace de la sala, "
            "un PIN…), una `descripcion` grande, el texto seleccionable y los botones «Copiar al portapapeles» y "
            "«Abrir». USALO SIEMPRE que le pases una URL o un dato largo al operador: desde el chat no lo puede copiar "
            "cómodo; así lo escanea con el celular o lo copia con un clic. `color` (verde, azul, rojo, amarillo, "
            "naranja, violeta, rosa, celeste, gris o #rrggbb) pinta el marco: con varios QR a la vez, poné uno de "
            "cada color y decile «escaneá el QR verde». Cada llamada abre una ventana nueva.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"texto"}},
            {"properties",
             {{"texto", {{"type", "string"}, {"description", "lo que va en el QR (p. ej. la URL)"}}},
              {"descripcion", {{"type", "string"}, {"description", "qué es, en grande (p. ej. «Enlace para los invitados»)"}}},
              {"mensaje", {{"type", "string"}, {"description", "una línea de explicación (opcional)"}}},
              {"color",
               {{"type", "string"},
                {"description", "verde (de fábrica), azul, rojo, amarillo, naranja, violeta, rosa, celeste, gris o #rrggbb"}}}}}};
        t.examples = {R"({"texto": "https://sala.ejemplo.com/invitados/#room=sala1", "descripcion": "Enlace para los invitados", "color": "verde"})",
                      R"({"texto": "https://sala.ejemplo.com/sesion/", "descripcion": "Sesión remota", "color": "azul"})"};
        StudioViewModel* vmp = &vm_;
        t.run = [vmp](const json& a, Done done) {
            const std::string texto = a.value("texto", std::string());
            if (texto.empty()) return done(ToolResult::error("falta `texto`"));
            if (texto.size() > 2000) return done(ToolResult::error("demasiado largo para un QR legible (más de 2000 caracteres)"));
            std::string color = a.value("color", std::string("verde"));
            for (auto& c : color) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            emit vmp->mostrarQr(QString::fromStdString(texto),
                                QString::fromStdString(a.value("descripcion", a.value("titulo", std::string()))),
                                QString::fromStdString(a.value("mensaje", std::string())), QString::fromStdString(color));
            done(ToolResult::ok("Listo: el operador ve el QR " + color + " con el botón para copiar."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "qr_generar";
        t.title = "Generar un codigo (sin guardarlo)";
        t.category = "qr";
        t.readOnly = true;
        t.description = "Arma el codigo y devuelve QUE DICE y su SVG, sin tocar la biblioteca ni lo que el "
                        "operador este diseñando. Para guardarlo: qr_guardar. Para dejarlo en un archivo: "
                        "qr_exportar.";
        t.details = "El estilo es opcional: sin el sale negro, cuadrado y sin adornos, que es lo que "
                    "siempre se lee.\n\n"
                    "OJO CON EL ESTILO: un icono al centro tapa modulos, asi que sube la correccion de "
                    "errores solo. Y no hay ojos circulares porque NO SE LEEN (se midio: un ojo mas "
                    "redondo que `redondeados` deja de escanear).";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"tipo"}},
            {"properties",
             {{"tipo", esquemaTipo},
              {"datos", {{"type", "object"}, {"description", "los campos del tipo (qr_listar)"}}},
              {"estilo", {{"type", "object"}, {"properties", esquemaEstilo()}}}}}};
        t.examples = {
            R"({"tipo": "wifi", "datos": {"red": "Estudio", "clave": "…"}, "estilo": {"icono": "wifi", "primario": "#1d4ed8"}})"};
        t.run = [q](const json& a, Done done) {
            const QString tipo = QString::fromStdString(a["tipo"].get<std::string>());
            const QVariantMap datos = aMapa(a.value("datos", json::object()));
            const QString texto = q->contenidoDe(tipo, datos);
            if (texto.isEmpty()) {
                done(ToolResult::error(
                    "No pude armar el contenido: ¿es un tipo que existe y estan sus campos? "
                    "(qr_listar los da). Los del aparato necesitan la sala corriendo."));
                return;
            }
            const QString s = q->svgSuelto(tipo, datos, aMapa(a.value("estilo", json::object())));
            if (s.isEmpty()) {
                done(ToolResult::error("El texto no entra en un codigo QR: acortalo o baja la correccion."));
                return;
            }
            done(ToolResult().data(
                json{{"dice", texto.toStdString()}, {"svg", s.toStdString()}, {"bytes", s.size()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "qr_guardar";
        t.title = "Guardar un codigo en la biblioteca";
        t.category = "qr";
        t.description = "Lo deja guardado para usarlo despues. `ambito`: \"global\" (del aparato, sirve "
                        "para todos los eventos) o \"evento\" (viaja con este). Con `id` de uno que ya "
                        "existe, lo reemplaza.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"nombre", "tipo"}},
            {"properties",
             {{"nombre", {{"type", "string"}, {"minLength", 1}}},
              {"tipo", esquemaTipo},
              {"ambito", {{"type", "string"}, {"enum", {"global", "evento"}}, {"default", "global"}}},
              {"datos", {{"type", "object"}}},
              {"estilo", {{"type", "object"}, {"properties", esquemaEstilo()}}},
              {"id", {{"type", "string"}, {"description", "para reemplazar uno que ya existe"}}}}}};
        t.run = [q](const json& a, Done done) {
            // Se arma en el editor y se guarda: el operador ve aparecer lo que
            // el asistente hizo, en la pantalla, sin refrescar nada.
            q->setAmbito(QString::fromStdString(a.value("ambito", "global")));
            if (a.contains("id"))
                q->abrir(QString::fromStdString(a["id"].get<std::string>()));
            else
                q->nuevo(QString::fromStdString(a["tipo"].get<std::string>()));
            q->setTipo(QString::fromStdString(a["tipo"].get<std::string>()));
            // Con nombre: el temporal de `value()` muere antes del bucle.
            const json jd = a.value("datos", json::object());
            for (const auto& [k, v] : jd.items())
                if (v.is_string()) q->setDato(QString::fromStdString(k), QString::fromStdString(v));
            const QVariantMap est = aMapa(a.value("estilo", json::object()));
            for (auto it = est.begin(); it != est.end(); ++it)
                q->setEstilo(it.key(), it.value());
            const QString err = q->guardar(QString::fromStdString(a["nombre"].get<std::string>()));
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            done(ToolResult::ok("Guardado como «" + a["nombre"].get<std::string>() + "».")
                     .data(json{{"id", q->abierto().toStdString()}, {"dice", q->contenido().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "qr_borrar";
        t.title = "Borrar un codigo guardado";
        t.category = "qr";
        t.destructive = true;
        t.description = "Lo saca de la biblioteca. No toca lo que ya se haya exportado ni lo que este en "
                        "una pantalla.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"ambito", {{"type", "string"}, {"enum", {"global", "evento"}}}}}}};
        t.run = [q](const json& a, Done done) {
            if (a.contains("ambito")) q->setAmbito(QString::fromStdString(a["ambito"].get<std::string>()));
            const QString err = q->borrar(QString::fromStdString(a["id"].get<std::string>()));
            done(err.isEmpty() ? ToolResult::ok("Borrado.") : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "qr_exportar";
        t.title = "Dejar un codigo en un archivo";
        t.category = "qr";
        t.description = "Escribe el codigo en Exportado/, como PNG (para pegar en algo) o SVG (para "
                        "imprimir a cualquier tamaño sin que se vea borroso). Con `id` exporta uno "
                        "guardado; si no, se arma con `tipo`/`datos`/`estilo` como en qr_generar.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}},
              {"tipo", esquemaTipo},
              {"datos", {{"type", "object"}}},
              {"estilo", {{"type", "object"}, {"properties", esquemaEstilo()}}},
              {"nombre", {{"type", "string"}, {"description", "el del archivo"}}},
              {"formato", {{"type", "string"}, {"enum", {"png", "svg"}}, {"default", "png"}}},
              {"px", {{"type", "integer"}, {"minimum", 64}, {"maximum", 4096}, {"default", 1024}}}}}};
        t.run = [q](const json& a, Done done) {
            QString s;
            QString nombre = QString::fromStdString(a.value("nombre", ""));
            if (a.contains("id")) {
                s = q->svgDe(QString::fromStdString(a["id"].get<std::string>()));
                if (nombre.isEmpty())
                    for (const QVariant& v : q->todos())
                        if (v.toMap().value("id").toString() == QString::fromStdString(a["id"]))
                            nombre = v.toMap().value("nombre").toString();
            } else if (a.contains("tipo")) {
                s = q->svgSuelto(QString::fromStdString(a["tipo"].get<std::string>()),
                                 aMapa(a.value("datos", json::object())),
                                 aMapa(a.value("estilo", json::object())));
            } else {
                done(ToolResult::error("Falta `id` (uno guardado) o `tipo` (para armar uno nuevo)."));
                return;
            }
            if (s.isEmpty()) {
                done(ToolResult::error("No pude generar ese codigo."));
                return;
            }
            const QString ruta = q->escribir(s, nombre, QString::fromStdString(a.value("formato", "png")),
                                             a.value("px", 1024));
            if (ruta.isEmpty()) {
                done(ToolResult::error("No pude escribir el archivo."));
                return;
            }
            done(
                ToolResult::ok("Guardado en " + ruta.toStdString()).data(json{{"ruta", ruta.toStdString()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
