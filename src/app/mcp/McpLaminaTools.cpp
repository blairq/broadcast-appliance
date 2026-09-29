// ============================================================================
// src/app/mcp/McpLaminaTools.cpp — carruseles de láminas
//
// «Volvé a la del gráfico de barras», dice el orador en medio de la charla. Con
// veinte diapositivas y un botón de siguiente, eso son veinte clics y diez
// segundos de aire perdido. `laminas_ir` es un salto.
//
// POR QUE NO SE LLAMAN carrusel_*. Ese nombre ya estaba, y significa otra cosa:
// la rotación de mensajes, QR y relojes en una PANTALLA SECUNDARIA (ver
// McpPantallaTools.cpp). Dos cosas distintas con el mismo nombre se eligen mal
// justo cuando hay apuro. Acá se usa la palabra del oficio —lámina— y el
// buscador lleva igual a estas herramientas cuando se escribe «carrusel»,
// «diapositiva» o «presentacion» (ver McpEtiquetas.cpp).
//
// EL QUE MIRA IMAGENES entra por `laminas_sin_analizar` y `laminas_etiquetar`:
// pide la próxima sin etiquetas, la mira, y guarda lo que vio. Una imagen que
// no dio nada NO se reintenta para siempre: se marca analizada igual.
// ============================================================================

#include <QFileInfo>

#include "AppRuntime.hpp"
#include "BroadcastEngine.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerLaminaTools() {
    core_.addCategory("laminas", "Carruseles de laminas",
                      "Tiras de imagenes para una escena: la presentacion de un orador, las placas "
                      "institucionales. Se arman una vez y se aplican donde haga falta; saltar a una "
                      "lamina cualquiera es instantaneo. Un PDF o un PowerPoint se desarma solo.");

    auto* cc = qobject_cast<CarruselViewModel*>(vm_.carruseles());
    AppRuntime& rt = rt_;

    {
        Tool t;
        t.name = "laminas_listar";
        t.title = "Que carruseles de laminas hay";
        t.category = "laminas";
        t.readOnly = true;
        t.description = "Los carruseles de la biblioteca, con cuantas laminas tiene cada uno y cual "
                        "esta abierto. Es por donde se empieza: el resto pide el nombre.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [cc](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : cc->carruseles()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"nombre", m["nombre"].toString().toStdString()},
                                   {"laminas", m["laminas"].toInt()}});
            }
            done(ToolResult().data(json{{"carruseles", out},
                                        {"abierto", cc->actual().toStdString()},
                                        // El de la escena al aire: es el que el
                                        // operador esta manejando ahora mismo.
                                        {"enLaEscenaAlAire", cc->enEscena().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_ver";
        t.title = "Las laminas de un carrusel";
        t.category = "laminas";
        t.readOnly = true;
        t.description = "Las laminas de un carrusel, en orden, con su numero, su titulo, sus "
                        "etiquetas y cual esta puesta ahora. El numero es lo que pide `laminas_ir`.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"carrusel", {{"type", "string"}, {"description", "su nombre"}}}}},
                         {"required", {"carrusel"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const QString antes = cc->actual();
            const QString consulta = cc->consulta();
            // Se mira SIN dejarlo abierto ni pisar el filtro del operador: una
            // consulta del asistente no puede cambiarle la pantalla de abajo.
            cc->setConsulta("");
            cc->setActual(n);
            json out = json::array();
            for (const QVariant& v : cc->laminas()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"numero", m["indice"].toInt() + 1},
                                   {"titulo", m["titulo"].toString().toStdString()},
                                   {"etiquetas", m["etiquetas"].toString().toStdString()},
                                   {"analizada", m["analizada"].toBool()},
                                   {"puesta", m["actual"].toBool()},
                                   {"archivo", m["archivo"].toString().toStdString()}});
            }
            const bool existe = !out.empty() || cc->actual() == n;
            cc->setActual(antes);
            cc->setConsulta(consulta);
            if (!existe) {
                done(ToolResult::error("No hay ningun carrusel llamado «" + n.toStdString() +
                                       "». laminas_listar los da."));
                return;
            }
            done(ToolResult().data(json{{"carrusel", n.toStdString()}, {"laminas", out}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_ir";
        t.title = "Saltar a una lamina";
        t.category = "laminas";
        t.description = "Pone la lamina numero N del carrusel. Es la razon de que el carrusel exista: "
                        "«volve a la del grafico» no son veinte clics de siguiente. Sale al aire si "
                        "la escena que la muestra esta al aire.\n\n"
                        "OJO: si el carrusel todavia no esta en ninguna escena (`laminas_aplicar`), "
                        "esto mueve el puntero pero no se ve NADA.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"carrusel", {{"type", "string"}}},
                           {"numero", {{"type", "integer"}, {"minimum", 1},
                                       {"description", "empezando en 1, como los da laminas_ver"}}}}},
                         {"required", {"carrusel", "numero"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            cc->setActual(n);
            if (cc->actual() != n) {
                done(ToolResult::error("No hay ningun carrusel llamado «" + n.toStdString() + "»."));
                return;
            }
            const int numero = a.value("numero", 0);
            if (!cc->ir(numero - 1)) {
                done(ToolResult::error("Ese carrusel no tiene una lamina " + std::to_string(numero) +
                                       ". laminas_ver dice cuantas hay."));
                return;
            }
            done(ToolResult().text("Puesta la lamina " + std::to_string(numero) + " de «" +
                                   n.toStdString() + "»."));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_desarmar";
        t.title = "Desarmar un PDF o un PowerPoint";
        t.category = "laminas";
        t.description = "Convierte cada pagina de un PDF o de una presentacion en una lamina y arma "
                        "un carrusel con ellas. Tarda (segundos por pagina) y avisa cuando termina; "
                        "el operador ve la barra de trabajo. Un .pptx necesita que el equipo traiga "
                        "LibreOffice; un PDF no.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"archivo", {{"type", "string"}, {"description", "ruta del PDF o .pptx, como la da media_list"}}},
                           {"nombre", {{"type", "string"}, {"description", "como llamar al carrusel. Vacio = el nombre del archivo"}}}}},
                         {"required", {"archivo"}}};
        t.run = [cc](const json& a, Done done) {
            const QString archivo = QString::fromStdString(a.value("archivo", std::string()));
            if (!QFileInfo::exists(archivo)) {
                done(ToolResult::error("No existe ese archivo: " + archivo.toStdString()));
                return;
            }
            if (!cc->sePuedeDesarmar(archivo)) {
                done(ToolResult::error("No se puede desarmar " + archivo.toStdString() +
                                       ". Un PDF siempre se puede; un .pptx solo si este equipo trae "
                                       "LibreOffice."));
                return;
            }
            cc->desarmar(archivo, QString::fromStdString(a.value("nombre", std::string())));
            // No se espera: convertir cincuenta laminas puede tardar mas que
            // cualquier limite razonable de una herramienta. Se avisa y el que
            // pregunte despues lo ve con laminas_listar.
            done(ToolResult().text("Empece a desarmarlo. Cuando termine aparece en laminas_listar."));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_aplicar";
        t.title = "Poner un carrusel en una escena";
        t.category = "laminas";
        t.description =
            "Pone la tira en una ESCENA: crea ahi una fuente de imagen llamada «Carrusel · <nombre>» "
            "y deja anotado que ese carrusel la maneja. Recien a partir de aca `laminas_ir` cambia "
            "algo que se VE: le cambia el archivo a esa fuente, y si la escena esta al aire, sale al "
            "aire.\n\n"
            "Nace a pantalla completa; se acomoda como cualquier otra fuente (source_rect). El mismo "
            "carrusel puede estar en varias escenas: es la misma fuente reusada.\n\n"
            "La tarjeta de control del operador sigue a la escena: cuando esta escena esta al aire, "
            "maneja este carrusel.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"carrusel", {{"type", "string"}}},
                           {"escena", {{"type", "string"},
                                       {"description", "por defecto, la que esta al aire"}}}}},
                         {"required", {"carrusel"}}};
        t.examples = {R"({"carrusel": "Apertura"})"};
        t.run = [cc, &rt](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const std::string escena = a.value("escena", rt.engine().currentScene());
            if (escena.empty()) {
                done(ToolResult::error("No hay ninguna escena al aire; deci en cual."));
                return;
            }
            if (!cc->aplicarAEscena(n, QString::fromStdString(escena))) {
                done(ToolResult::error("No se pudo poner «" + n.toStdString() + "» en «" + escena +
                                       "». laminas_listar dice cuales hay."));
                return;
            }
            done(ToolResult::ok("«" + n.toStdString() + "» quedo en «" + escena +
                                "». Ahora laminas_ir cambia lo que se ve. Verifica con "
                                "capture_source."));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_sin_analizar";
        t.title = "La proxima lamina sin etiquetas";
        t.category = "laminas";
        t.readOnly = true;
        t.description = "Devuelve la ruta de la primera lamina a la que todavia no se le sacaron "
                        "etiquetas. Es por donde entra el que mira imagenes: pedir esta, mirarla con "
                        "image_view, y guardar lo que vio con laminas_etiquetar. Vacio = no queda "
                        "ninguna.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"carrusel", {{"type", "string"}}}}},
                         {"required", {"carrusel"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const QString antes = cc->actual();
            const QString consulta = cc->consulta();
            cc->setConsulta("");
            cc->setActual(n);
            json r = json::object();
            for (const QVariant& v : cc->laminas()) {
                const QVariantMap m = v.toMap();
                if (m["analizada"].toBool()) continue;
                r = json{{"numero", m["indice"].toInt() + 1},
                         {"archivo", m["archivo"].toString().toStdString()},
                         {"titulo", m["titulo"].toString().toStdString()}};
                break;
            }
            cc->setActual(antes);
            cc->setConsulta(consulta);
            done(ToolResult().data(r.empty() ? json{{"quedan", false}}
                                             : json{{"quedan", true}, {"lamina", r}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_etiquetar";
        t.title = "Guardar lo que se ve en una lamina";
        t.category = "laminas";
        t.description = "Guarda las palabras con las que despues se va a buscar esa lamina: lo que "
                        "se ve, lo que dice, de que trata. El operador escribe «barras» en el cuadro "
                        "y le queda esa. Se guarda UNA vez por lamina; si salio vacio igual queda "
                        "marcada, para no volver a mirarla para siempre.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"carrusel", {{"type", "string"}}},
                           {"numero", {{"type", "integer"}, {"minimum", 1}}},
                           {"etiquetas", {{"type", "string"},
                                          {"description", "palabras sueltas separadas por espacios o comas"}}}}},
                         {"required", {"carrusel", "numero", "etiquetas"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const int numero = a.value("numero", 0);
            if (!cc->etiquetar(n, numero - 1,
                               QString::fromStdString(a.value("etiquetas", std::string())))) {
                done(ToolResult::error("No encontre la lamina " + std::to_string(numero) + " de «" +
                                       n.toStdString() + "»."));
                return;
            }
            done(ToolResult().text("Etiquetada."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
