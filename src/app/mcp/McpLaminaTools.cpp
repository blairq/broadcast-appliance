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

#include "medios/Medios.hpp"
#include "i18n/I18n.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/BroadcastEngine.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerLaminaTools() {
    core_.addCategory("laminas", ba::i18n::traducir("mcp", "Carruseles de láminas"),
                      ba::i18n::traducir("mcp", "Tiras de imágenes para una escena: la presentación de un orador, las placas "
                      "institucionales. Se arman una vez y se aplican donde haga falta; saltar a una "
                      "lámina cualquiera es instantaneo. Un PDF o un PowerPoint se desarma solo."));

    auto* cc = qobject_cast<CarruselViewModel*>(vm_.carruseles());
    AppRuntime& rt = rt_;
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());

    {
        Tool t;
        t.name = "carruseles_listar";
        t.title = ba::i18n::traducir("mcp", "Que carruseles de laminas hay");
        t.category = "laminas";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Los carruseles de la biblioteca (no sus láminas), con cuántas láminas tiene cada uno y cuál está "
            "abierto. Es por donde se empieza: el resto pide el nombre.");
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
                                        // Desarmar tarda y no espera: sin esto
                                        // el que pregunta no tiene como saber
                                        // si todavia esta trabajando.
                                        {"desarmando", cc->convirtiendo()},
                                        {"etapa", cc->etapa().toStdString()},
                                        {"hechas", cc->hecho()},
                                        {"deCuantas", cc->total()},
                                        // El de la escena al aire: es el que el
                                        // operador esta manejando ahora mismo.
                                        {"enLaEscenaAlAire", cc->enEscena().toStdString()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "carrusel_laminas_ver";
        t.title = ba::i18n::traducir("mcp", "Las laminas de un carrusel");
        t.category = "laminas";
        t.readOnly = true;
        t.description = ba::i18n::traducir("mcp", "Las láminas DE UN carrusel, en orden, con su número, su título y cuál está puesta. El numero es lo "
            "que pide `laminas_ir`.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"carrusel", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "su nombre")}}}}},
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
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay ningun carrusel llamado «%1». carruseles_listar los da."), {n.toStdString()})));
                return;
            }
            done(ToolResult().data(json{{"carrusel", n.toStdString()}, {"laminas", out}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_ir";
        t.title = ba::i18n::traducir("mcp", "Saltar a una lamina");
        t.category = "laminas";
        t.description = ba::i18n::traducir("mcp", "Pone la lamina numero N del carrusel. Es la razon de que el carrusel exista: "
                        "«volve a la del grafico» no son veinte clics de siguiente. Sale al aire si "
                        "la escena que la muestra esta al aire.\n\n"
                        "OJO: si el carrusel todavia no esta en ninguna escena (`laminas_aplicar`), "
                        "esto mueve el puntero pero no se ve NADA.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"carrusel", {{"type", "string"}}},
                           {"numero",
                            {{"type", "integer"},
                             {"minimum", 1},
                             {"description", ba::i18n::traducir("mcp", "empezando en 1, como los da carrusel_laminas_ver")}}},
                           {"hacia",
                            {{"type", "string"},
                             {"enum", {"siguiente", "anterior"}},
                             {"description", ba::i18n::traducir("mcp", "en vez de `numero`: la que sigue o la de antes. "
                                             "Da la vuelta al llegar al final.")}}}}},
                         {"required", {"carrusel"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            cc->setActual(n);
            if (cc->actual() != n) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay ningun carrusel llamado «%1»."), {n.toStdString()})));
                return;
            }
            // «La que sigue» es lo que mas se pide en vivo, y obligaba a
            // preguntar en cual estaba para poder sumarle uno.
            const std::string hacia = a.value("hacia", std::string());
            int numero = a.value("numero", 0);
            if (!hacia.empty()) {
                // SIN el filtro puesto. `laminas()` devuelve lo que pasa el
                // cuadro de busqueda de la pestaña, y si el operador dejo algo
                // escrito ahi, «la siguiente» contaria sobre tres laminas en
                // vez de cincuenta.
                const QString filtro = cc->consulta();
                cc->setConsulta("");
                const int n = cc->laminas().size();
                cc->setConsulta(filtro);
                if (n == 0) {
                    done(ToolResult::error(ba::i18n::traducir("mcp", "Ese carrusel esta vacio.")));
                    return;
                }
                const int ahora = cc->posicion();
                numero = ((hacia == "anterior" ? ahora - 1 : ahora + 1) + n) % n + 1;
            } else if (numero <= 0) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "Deci `numero` o `hacia` (siguiente/anterior).")));
                return;
            }
            if (!cc->ir(numero - 1)) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Ese carrusel no tiene una lamina %1. carrusel_laminas_ver dice cuantas hay."),
                                         {std::to_string(numero)})));
                return;
            }
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Puesta la lamina %1 de «%2»."), {std::to_string(numero), n.toStdString()})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_buscar";
        t.title = ba::i18n::traducir("mcp", "Buscar una lamina por lo que dice");
        t.category = "laminas";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Las laminas de un carrusel que coinciden con un texto, por su titulo y por las "
            "etiquetas que se le sacaron mirandolas. Es lo que hace el cuadro de busqueda de la "
            "pestaña Medios, con el MISMO criterio: sin tildes y todas las palabras tienen que "
            "estar.\n\n"
            "PARA QUE SIRVE DE VERDAD: «volve a la del grafico de barras» con una presentacion de "
            "cincuenta laminas. Sin esto habria que listarlas todas y leerlas; con esto vuelven "
            "tres y se salta con `laminas_ir`.\n\n"
            "Si no encuentra nada puede ser que a esas laminas todavia nadie las miro: "
            "`laminas_sin_analizar` lo dice.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"carrusel", {{"type", "string"}}},
              {"texto", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "lo que dijo el operador: «grafico barras»")}}}}},
            {"required", {"carrusel", "texto"}}};
        t.examples = {R"({"carrusel": "Apertura", "texto": "grafico de barras"})"};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            if (!cc->existe(n)) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay ningun carrusel llamado «%1». carruseles_listar los da."), {n.toStdString()})));
                return;
            }
            const QString antes = cc->actual();
            const QString consulta = cc->consulta();
            cc->setActual(n);
            cc->setConsulta(QString::fromStdString(a.value("texto", std::string())));
            json out = json::array();
            for (const QVariant& v : cc->laminas()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"numero", m["indice"].toInt() + 1},
                                   {"titulo", m["titulo"].toString().toStdString()},
                                   {"etiquetas", m["etiquetas"].toString().toStdString()},
                                   {"puesta", m["actual"].toBool()}});
            }
            cc->setActual(antes);
            cc->setConsulta(consulta);
            done(ToolResult().data(json{{"carrusel", n.toStdString()}, {"encontradas", out}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_desarmar";
        t.title = ba::i18n::traducir("mcp", "Desarmar un PDF o un PowerPoint");
        t.category = "laminas";
        t.description = ba::i18n::traducir("mcp", "Convierte cada pagina de un PDF o de una presentacion en una lamina y arma "
                        "un carrusel con ellas. Tarda (segundos por pagina) y avisa cuando termina; "
                        "el operador ve la barra de trabajo. Un .pptx pasa por DeckCraft (viaja en el "
                        "programa); el .ppt viejo y el .odp no se leen: exportalos a .pptx o a PDF.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"archivo",
               {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "ruta del PDF o .pptx, como la da media_list")}}},
              {"nombre",
               {{"type", "string"},
                {"description", ba::i18n::traducir("mcp", "como llamar al carrusel. Vacio = el nombre del archivo")}}}}},
            {"required", {"archivo"}}};
        t.run = [cc](const json& a, Done done) {
            const QString archivo = QString::fromStdString(a.value("archivo", std::string()));
            if (!QFileInfo::exists(archivo)) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe ese archivo: %1"), {archivo.toStdString()})));
                return;
            }
            if (!cc->sePuedeDesarmar(archivo)) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se puede desarmar %1. Se puede un PDF o un .pptx (este, con DeckCraft); el .ppt viejo "
                                           "y el .odp, exportados a .pptx o a PDF."),
                                         {archivo.toStdString()})));
                return;
            }
            cc->desarmar(archivo, QString::fromStdString(a.value("nombre", std::string())));
            // No se espera: convertir cincuenta laminas puede tardar mas que
            // cualquier limite razonable de una herramienta. Se avisa y el que
            // pregunte despues lo ve con carruseles_listar.
            done(ToolResult().text(ba::i18n::traducir("mcp", "Empece a desarmarlo. Cuando termine aparece en carruseles_listar.")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_armar";
        t.title = ba::i18n::traducir("mcp", "Armar un carrusel a mano");
        t.category = "laminas";
        t.description =
            ba::i18n::traducir("mcp", "Crea un carrusel y le pone las imagenes que se le digan, en ese orden. Para armar uno "
            "con placas sueltas —logos, fondos, placas institucionales— en vez de desarmar una "
            "presentacion (para eso esta `laminas_desarmar`).\n\n"
            "Si el carrusel ya existe, le AGREGA las imagenes al final en vez de pisarlo. Una imagen "
            "que ya estaba no entra dos veces. Las rutas son las que da `media_list`.\n\n"
            "Queda armado y nada mas: para que se vea hay que ponerlo en una escena con "
            "`laminas_aplicar`.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "como se llama el carrusel")}}},
                           {"imagenes",
                            {{"type", "array"},
                             {"items", {{"type", "string"}}},
                             {"description", ba::i18n::traducir("mcp", "rutas, como las da media_list")}}}}}};
        t.examples = {R"({"nombre": "Sponsors", "imagenes": ["Media/logos/a.png", "Media/logos/b.png"]})"};
        t.run = [cc, root](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("nombre", std::string())).trimmed();
            if (n.isEmpty()) {
                done(
                    ToolResult::error(ba::i18n::traducir("mcp", "Hace falta un nombre: sin nombre no se lo puede volver a encontrar.")));
                return;
            }
            // Las imágenes, revisadas antes de crear nada (una que no es texto dejaba el carrusel vacío creado).
            for (const auto& v : a.value("imagenes", json::array()))
                if (!v.is_string()) return done(ToolResult::error(ba::i18n::traducir("mcp", "`imagenes` es una lista de rutas (texto).")));
            bool nuevo = false;
            if (cc->fuenteDe(n).isEmpty() && !cc->existe(n)) {
                if (!cc->crear(n)) {
                    done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo crear «%1»."), {n.toStdString()})));
                    return;
                }
                nuevo = true;
            }
            cc->setActual(n);
            int puestas = 0;
            std::string rechazadas;
            for (const auto& v : a.value("imagenes", json::array())) {
                const QString rel = QString::fromStdString(v.get<std::string>());
                // Adentro de la carpeta del sistema y nada mas: una ruta de
                // afuera en una escena se rompe al mover el aparato de maquina.
                const QString abs = medios::resolverAdentro(root, rel);
                if (abs.isEmpty() || !QFileInfo::exists(abs)) {
                    rechazadas += (rechazadas.empty() ? "" : ", ") + rel.toStdString();
                    continue;
                }
                if (cc->agregar(abs)) ++puestas;
            }
            std::string txt = ba::i18n::llenar(nuevo ? ba::i18n::traducir("mcp", "Carrusel «%1»: %2 laminas agregadas.")
                                      : ba::i18n::traducir("mcp", "Ya existia «%1»: %2 laminas agregadas."),
                                {n.toStdString(), std::to_string(puestas)});
            if (!rechazadas.empty())
                txt += " " + ba::i18n::llenar(ba::i18n::traducir("mcp", "No encontre (o se salian de la carpeta del sistema): %1."), {rechazadas});
            txt += " " + ba::i18n::traducir("mcp", "Para que se vea: laminas_aplicar.");
            done(ToolResult::ok(txt));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_editar";
        t.title = ba::i18n::traducir("mcp", "Cambiar un carrusel ya armado");
        t.category = "laminas";
        t.description =
            ba::i18n::traducir("mcp", "Quitar una lamina, moverla de lugar, renombrar el carrusel, borrarlo, o soltarlo de la "
            "escena.\n\n"
            "`borrar` se lleva el carrusel, NO las imagenes: pueden estar en una escena o ser del "
            "operador. `soltar` lo desvincula de la fuente y la imagen queda como estaba; la fuente "
            "se saca desde Escenas si ademas hay que quitarla.\n\n"
            "Los numeros son los de `carrusel_laminas_ver`, empezando en 1.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"carrusel", "accion"}},
            {"properties",
             {{"carrusel", {{"type", "string"}}},
              {"accion",
               {{"type", "string"}, {"enum", {"quitar", "mover", "renombrar", "borrar", "soltar"}}}},
              {"numero", {{"type", "integer"}, {"minimum", 1}, {"description", ba::i18n::traducir("mcp", "con quitar y mover")}}},
              {"hasta", {{"type", "integer"}, {"minimum", 1}, {"description", ba::i18n::traducir("mcp", "con mover: a que puesto va")}}},
              {"nombre", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "con renombrar: el nombre nuevo")}}}}}};
        t.examples = {R"({"carrusel": "Sponsors", "accion": "mover", "numero": 5, "hasta": 1})"};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            if (!cc->existe(n)) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay ningun carrusel llamado «%1». carruseles_listar los da."), {n.toStdString()})));
                return;
            }
            const std::string accion = a.value("accion", std::string());
            bool ok = false;
            if (accion == "borrar") {
                ok = cc->borrar(n);
            } else if (accion == "soltar") {
                ok = cc->soltarDeEscena(n);
            } else if (accion == "renombrar") {
                ok = cc->renombrar(n, QString::fromStdString(a.value("nombre", std::string())));
            } else {
                cc->setActual(n);
                const int numero = a.value("numero", 0);
                if (accion == "quitar")
                    ok = cc->quitar(numero - 1);
                else if (accion == "mover")
                    ok = cc->mover(numero - 1, a.value("hasta", 0) - 1);
            }
            if (!ok) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo: revisa los numeros con carrusel_laminas_ver (empiezan en 1) "
                                       "y que el nombre nuevo no este usado.")));
                return;
            }
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_aplicar";
        t.title = ba::i18n::traducir("mcp", "Poner un carrusel en una escena");
        t.category = "laminas";
        t.description =
            ba::i18n::traducir("mcp", "Pone la tira en una ESCENA: crea ahi una fuente de imagen llamada «Carrusel · <nombre>» "
            "y deja anotado que ese carrusel la maneja. Recien a partir de aca `laminas_ir` cambia "
            "algo que se VE: le cambia el archivo a esa fuente, y si la escena esta al aire, sale al "
            "aire.\n\n"
            "Nace a pantalla completa; se acomoda como cualquier otra fuente (source_rect). El mismo "
            "carrusel puede estar en varias escenas: es la misma fuente reusada.\n\n"
            "La tarjeta de control del operador sigue a la escena: cuando esta escena esta al aire, "
            "maneja este carrusel.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"carrusel", {{"type", "string"}}},
              {"escena", {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "por defecto, la que esta al aire")}}}}},
            {"required", {"carrusel"}}};
        t.examples = {R"({"carrusel": "Apertura"})"};
        t.run = [cc, &rt](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const std::string escena = a.value("escena", rt.engine().currentScene());
            if (escena.empty()) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "No hay ninguna escena al aire; deci en cual.")));
                return;
            }
            if (!cc->aplicarAEscena(n, QString::fromStdString(escena))) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo poner «%1» en «%2». carruseles_listar dice cuales hay."),
                                         {n.toStdString(), escena})));
                return;
            }
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» quedo en «%2». Ahora laminas_ir cambia lo que se ve. Verifica con "
                                  "foto_fuente."),
                                {n.toStdString(), escena})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "laminas_sin_analizar";
        t.title = ba::i18n::traducir("mcp", "La proxima lamina sin etiquetas");
        t.category = "laminas";
        t.readOnly = true;
        t.description =
            ba::i18n::traducir("mcp", "Devuelve la ruta de la primera lamina a la que todavia no se le sacaron "
            "etiquetas. Es por donde entra el que mira imagenes: pedir esta, mirarla con "
            "imagen_preguntar —que es quien sabe mirar; este asistente NO recibe imagenes—, y guardar "
            "lo que vio con laminas_etiquetar. Vacio = no queda "
            "ninguna.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"carrusel", {{"type", "string"}}}}},
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
        t.title = ba::i18n::traducir("mcp", "Guardar lo que se ve en una lamina");
        t.category = "laminas";
        t.description = ba::i18n::traducir("mcp", "Guarda las palabras con las que despues se va a buscar esa lamina: lo que "
                        "se ve, lo que dice, de que trata. El operador escribe «barras» en el cuadro "
                        "y le queda esa. Se guarda UNA vez por lamina; si salio vacio igual queda "
                        "marcada, para no volver a mirarla para siempre.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"carrusel", {{"type", "string"}}},
              {"numero", {{"type", "integer"}, {"minimum", 1}}},
              {"etiquetas",
               {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "palabras sueltas separadas por espacios o comas")}}}}},
            {"required", {"carrusel", "numero", "etiquetas"}}};
        t.run = [cc](const json& a, Done done) {
            const QString n = QString::fromStdString(a.value("carrusel", std::string()));
            const int numero = a.value("numero", 0);
            if (!cc->etiquetar(n, numero - 1, QString::fromStdString(a.value("etiquetas", std::string())))) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No encontre la lamina %1 de «%2»."), {std::to_string(numero), n.toStdString()})));
                return;
            }
            done(ToolResult().text(ba::i18n::traducir("mcp", "Etiquetada.")));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
