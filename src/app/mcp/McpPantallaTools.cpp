// ============================================================================
// src/app/mcp/McpPantallaTools.cpp — las pantallas externas, por MCP
//
// Lo mismo que la pagina Pantallas y por el mismo ViewModel: lo que cambia el
// asistente se ve en la pantalla en el acto.
//
// Para que sirve: "poné el QR de la sala en el televisor del hall", "mostrá
// «Volvemos en 10 minutos» mientras cortamos", "el televisor del fondo que
// muestre el teleprompter". Son cosas que el operador pediria hablando
// mientras tiene las manos en otra cosa.
//
// Y los MONITORES de la máquina (Salidas → Pantallas → Pantallas conectadas):
// monitor_listar / monitor_asignar / monitor_quitar, por el mismo modelo que
// la página (MonitoresModel). Lo que se puede poner sale del catálogo único
// (catalogo_listar), el mismo que usan la página, las ventanas y el tablero.
//
// Y las DISTRIBUCIONES de pantallas (pantallas/Distribuciones.hpp): cómo quedan
// las ventanas en los monitores, guardado con nombre en el evento. Es lo que más
// se usa por acá: el asistente arma la lista de ventanas en JSON, la guarda y la
// aplica (pantallas_distribucion_*). Los monitores con su geometría los da
// monitor_listar.
// ============================================================================

#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "pantallas/BuscadorCatalogo.hpp"
#include "pantallas/Catalogo.hpp"
#include "pantallas/Distribuciones.hpp"
#include "ui/EscritorioViewModel.hpp"
#include "pantallas/Monitores.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app::mcp {
namespace {

/// El id de una pantalla por su id o por su NOMBRE, que es como la piensa una
/// persona ("el televisor del hall").
QString idDePantalla(PantallasViewModel& p, const QString& quien) {
    for (const QVariant& v : p.pantallas()) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() == quien ||
            m.value("nombre").toString().compare(quien, Qt::CaseInsensitive) == 0)
            return m.value("id").toString();
    }
    return {};
}

} // namespace

void McpService::registerPantallaTools() {
    core_.addCategory("pantallas", ba::i18n::traducir("mcp", "Pantallas externas"),
                      ba::i18n::traducir("mcp", "Televisores y proyectores de la sala (Chromecast): cuáles hay y qué mostrar en "
                      "cada uno (un mensaje, un QR, la hora, una página, el teleprompter)."));
    auto* p = static_cast<PantallasViewModel*>(vm_.pantallas());
    if (p == nullptr) return;

    {
        Tool t;
        t.name = "pantalla_listar";
        t.title = ba::i18n::traducir("mcp", "Qué pantallas hay");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Que televisores conoce el sistema y como esta cada uno: libre, ocupado por nosotros, "
            "tomado por otra aplicacion, o sin responder.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`estado` dice mucho: `ocupada` es que el televisor esta con otra aplicacion (Netflix, "
                    "YouTube) y mandarle algo se la va a pisar — avisale al operador antes. `apagada` es "
                    "que no contesta: puede estar apagado de verdad o en otra red.\n\n"
                    "`sabeMostrarPaginas`: 1 si, 0 no (un parlante sin pantalla contesta el protocolo "
                    "igual), -1 todavia no se pregunto. `fijo` es lo que tiene que mostrar SIEMPRE: vuelve "
                    "solo cada vez que el aparato aparece.");
        t.run = [p](const json&, Done done) {
            json ps = json::array();
            for (const QVariant& v : p->pantallas()) {
                const QVariantMap m = v.toMap();
                ps.push_back(json{{"id", m.value("id").toString().toStdString()},
                                  {"nombre", m.value("nombre").toString().toStdString()},
                                  {"modelo", m.value("modelo").toString().toStdString()},
                                  {"host", m.value("host").toString().toStdString()},
                                  {"estado", m.value("estado").toString().toStdString()},
                                  {"muestra", m.value("que").toString().toStdString()},
                                  {"fijo", m.value("fijadoTipo").toString().toStdString()},
                                  {"sabeMostrarPaginas", m.value("puedeMostrar").toInt()},
                                  {"error", m.value("error").toString().toStdString()}});
            }
            json cs = json::array();
            for (const QVariant& v : p->contenidos()) {
                const QVariantMap m = v.toMap();
                json campos = json::array();
                for (const QVariant& f : m.value("campos").toList())
                    campos.push_back(f.toMap().value("clave").toString().toStdString());
                json c{{"tipo", m.value("id").toString().toStdString()},
                       {"que_es", m.value("ayuda").toString().toStdString()},
                       {"campos", campos}};
                if (!m.value("disponible").toBool())
                    c["todavia_no"] = m.value("porque").toString().toStdString();
                cs.push_back(c);
            }
            done(ToolResult().data(json{{"pantallas", ps}, {"sePuedeMostrar", cs}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_buscar";
        t.clase = Clase::Lectura;
        t.title = ba::i18n::traducir("mcp", "Buscar televisores en la red");
        t.category = "pantallas";
        t.description = QT_TRANSLATE_NOOP("mcp", "Busca Chromecast en la red local (tarda unos segundos). Las que aparezcan quedan "
                        "en la lista; volve a mirar con pantalla_listar.");
        t.run = [p](const json&, Done done) {
            p->buscar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Buscando. En unos segundos preguntale a pantalla_listar que aparecio.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_mostrar";
        t.title = ba::i18n::traducir("mcp", "Mostrar algo en una pantalla");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "MUESTRA algo (un QR, una pagina, un cartel, la votacion) en un televisor de la red. Le manda un "
            "contenido a una pantalla. `tipo`: mensaje (texto grande), qr (un QR a pantalla "
            "completa), reloj, url (una direccion cualquiera), prompter (la vista del orador), carrusel "
            "(una rotacion ya armada), programa (lo que sale al aire, en vivo por WebRTC), escena (UNA "
            "escena cualquiera de las armadas, en vivo, este o no al aire: `escena` es su nombre) o tally (rojo "
            "si esa camara esta al aire, verde si esta armada). Los campos de cada tipo salen de "
            "pantalla_listar.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Ojo con la que este `ocupada`: mostrarle algo le pisa lo que estuviera pasando. Si no "
                    "te lo pidieron explicitamente, preguntale al operador con `operator_ask`.\n\n"
                    "El texto se lee DE LEJOS: frases cortas y pocas palabras. «Volvemos en 10 minutos» "
                    "sirve; un parrafo, no.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla", "tipo"}},
            {"properties",
             {{"pantalla", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre, como lo da pantalla_listar")}}},
              {"tipo",
               {{"type", "string"},
                {"enum",
                 {"mensaje", "qr", "reloj", "url", "prompter", "carrusel", "programa", "escena", "tally",
                  "votacion"}}}},
              {"carrusel", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "su id, con tipo=carrusel")}}},
              {"qr",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=qr: \"aparato:publico\" (la pagina del publico, con su clave), "
                                "\"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\", o el id de uno GUARDADO (qr_listar los da), que sale "
                                "con el estilo con el que se diseño. Sin esto se usa `texto` y "
                                "sale con el estilo de fabrica.")}}},
              {"texto", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "mensaje: lo que dice; qr: lo que codifica")}}},
              {"pie", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la linea de abajo, mas chica")}}},
              {"url", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "solo con tipo=url")}}},
              {"escena",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=escena: el nombre de la escena (scene_list las da). Varias "
                                "pantallas pueden mostrar escenas distintas a la vez, hasta 4 distintas")}}},
              {"fuente",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=tally: la camara o escena de esa pantalla (vacio = el estado "
                                "general, para la cabina)")}}}}}};
        t.examples = {
            R"({"pantalla": "TV del hall", "tipo": "mensaje", "texto": "Volvemos en 10 minutos"})",
            R"({"pantalla": "TV del hall", "tipo": "qr", "texto": "https://...", "pie": "Escaneá para entrar"})"};
        t.run = [p, this](const json& a, Done done) {
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            const QString id = idDePantalla(*p, quien);
            if (id.isEmpty()) {
                done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No conozco la pantalla '%1'. pantalla_listar dice cuales hay; pantalla_buscar busca de nuevo."),
                                        {quien.toStdString()})));
                return;
            }
            const QString tipo = QString::fromStdString(a["tipo"].get<std::string>());
            if (tipo == "carrusel") {
                if (!a.contains("carrusel")) {
                    done(ToolResult::error(ba::i18n::traducir("mcp", "Con tipo=carrusel hace falta `carrusel` (su id). "
                                           "carrusel_listar los da.")));
                    return;
                }
                const QString err =
                    errorDe([&] { p->mostrarCarrusel(id, QString::fromStdString(a["carrusel"].get<std::string>())); });
                done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Mandado. Tarda un par de segundos."))
                                   : ToolResult::error(err.toStdString()));
                return;
            }
            QVariantMap datos;
            for (const char* k : {"texto", "pie", "url", "fuente", "escena"})
                if (a.contains(k) && a[k].is_string())
                    datos[k] = QString::fromStdString(a[k].get<std::string>());
            // Un codigo GUARDADO se proyecta con su estilo. Antes solo se podia
            // mandar `texto`, asi que el codigo que se habia diseñado y el que
            // salia en el televisor eran distintos y nada lo decia.
            if (tipo == "qr" && a.contains("qr") && a["qr"].is_string()) {
                auto* qvm = static_cast<QrViewModel*>(vm_.qr());
                const QString qid = QString::fromStdString(a["qr"].get<std::string>());
                const QVariantMap guardado = qvm == nullptr ? QVariantMap() : qvm->paraProyectarElegido(qid);
                if (guardado.value("pendiente").toBool()) {
                    done(ToolResult::error(ba::i18n::traducir("mcp", "La conferencia todavia no dio ese enlace (¿esta andando?): proba de nuevo en un momento.")));
                    return;
                }
                if (guardado.isEmpty()) {
                    done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No tengo guardado un codigo QR con id '%1'. qr_listar los da."), {qid.toStdString()})));
                    return;
                }
                datos["texto"] = guardado.value("texto");
                datos["estilo"] = guardado.value("estilo");
                if (!datos.contains("pie")) datos["pie"] = guardado.value("nombre");
            }
            const QString err = errorDe([&] { p->mostrar(id, tipo, datos); });
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Mandado a «%1». Tarda un par de segundos: pantalla_listar dice como salio."),
                                  {id.toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    // Buscar la pantalla por id o por NOMBRE: el operador (y el asistente)
    // piensan en "el televisor del hall", no en un id de servicio mDNS.
    {
        Tool t;
        t.name = "pantalla_fijar";
        t.title = ba::i18n::traducir("mcp", "Que una pantalla muestre siempre lo mismo");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Deja fijo lo que esa pantalla tiene que mostrar: se lo manda ahora y CADA VEZ que el aparato "
            "vuelva a aparecer (se apago de noche, se reinicio, le cambio la direccion). Con `quitar` en "
            "true deja de tener algo fijo (no apaga lo que este mostrando).");
        t.details = QT_TRANSLATE_NOOP("mcp", "Es para las pantallas que viven puestas: la del hall con el QR de la conferencia, la del "
                    "fondo con el reloj. Lo fijado se guarda contra el nombre de servicio del aparato, no "
                    "contra su IP, asi que sobrevive a que el router le de otra direccion.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla"}},
            {"properties",
             {{"pantalla", {{"type", "string"}}},
              {"tipo",
               {{"type", "string"},
                {"enum", {"mensaje", "qr", "reloj", "url", "prompter", "carrusel", "programa", "escena", "tally",
                          "votacion"}}}},
              {"carrusel", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "su id, con tipo=carrusel")}}},
              {"texto", {{"type", "string"}}},
              {"pie", {{"type", "string"}}},
              {"url", {{"type", "string"}}},
              {"escena", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=escena: su nombre")}}},
              {"fuente", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=tally: la camara o escena de esa pantalla")}}},
              {"quitar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "deja de tener algo fijo")}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString id = idDePantalla(*p, QString::fromStdString(a["pantalla"].get<std::string>()));
            if (id.isEmpty()) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No conozco la pantalla '%1'. pantalla_listar dice cuales hay."),
                                         {a["pantalla"].get<std::string>()})));
                return;
            }
            if (a.value("quitar", false)) {
                const QString err = errorDe([&] { p->desfijar(id); });
                done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Ya no tiene nada fijo."))
                                   : ToolResult::error(err.toStdString()));
                return;
            }
            if (!a.contains("tipo")) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "Falta `tipo` (o `quitar`: true).")));
                return;
            }
            QString tipo = QString::fromStdString(a["tipo"].get<std::string>());
            // Un carrusel fijo es «carrusel:<id>» (PantallasViewModel::fijar).
            if (tipo == "carrusel") {
                if (!a.contains("carrusel") || !a["carrusel"].is_string())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "Con tipo=carrusel hace falta `carrusel` (su id). "
                                                                            "carrusel_listar los da.")));
                tipo += ":" + QString::fromStdString(a["carrusel"].get<std::string>());
            }
            QVariantMap datos;
            for (const char* k : {"texto", "pie", "url", "fuente", "escena"})
                if (a.contains(k) && a[k].is_string())
                    datos[k] = QString::fromStdString(a[k].get<std::string>());
            const QString err = errorDe([&] { p->fijar(id, tipo, datos); });
            done(err.isEmpty()
                     ? ToolResult::ok(ba::i18n::traducir("mcp", "Fijado, y mandado ahora. Va a volver solo cada vez que el aparato "
                                      "aparezca."))
                     : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_olvidar";
        t.title = ba::i18n::traducir("mcp", "Olvidar una pantalla");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca la pantalla de la lista de conocidas (con su nombre y lo fijado). Si vuelve a aparecer en "
                        "la red, se la ve como nueva.");
        t.inputSchema = {{"type", "object"}, {"required", {"pantalla"}}, {"properties", {{"pantalla", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString id = idDePantalla(*p, QString::fromStdString(a["pantalla"].get<std::string>()));
            if (id.isEmpty()) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No conozco la pantalla '%1'."), {a["pantalla"].get<std::string>()})));
            const QString err = errorDe([&] { p->olvidar(id); });
            done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Olvidada.")) : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_renombrar";
        t.title = ba::i18n::traducir("mcp", "Cambiarle el nombre a una pantalla");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "El nombre que pongas gana sobre el que anuncia el aparato: «TV del hall» dice mas "
                        "que «Percee TV», y es como la vas a nombrar despues.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla", "nombre"}},
            {"properties",
             {{"pantalla", {{"type", "string"}}}, {"nombre", {{"type", "string"}, {"minLength", 1}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString id = idDePantalla(*p, QString::fromStdString(a["pantalla"].get<std::string>()));
            if (id.isEmpty()) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No conozco la pantalla '%1'."), {a["pantalla"].get<std::string>()})));
                return;
            }
            const QString err = errorDe([&] { p->renombrar(id, QString::fromStdString(a["nombre"].get<std::string>())); });
            done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Listo.")) : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar una pantalla a mano");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Para cuando la busqueda en la red no llega (otra VLAN, el firewall): se agrega con "
                        "su direccion IP. Tiene que ser un aparato que hable Google Cast.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"host"}},
                         {"properties",
                          {{"host", {{"type", "string"}, {"description", "192.168.1.50"}}},
                           {"nombre", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString err = errorDe([&] { p->agregar(QString::fromStdString(a.value("nombre", "")),
                                           QString::fromStdString(a["host"].get<std::string>()), 0); });
            done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Agregada. pantalla_listar dice si contesta."))
                               : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "carrusel_listar";
        t.title = ba::i18n::traducir("mcp", "Los carruseles que hay");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Un carrusel es una lista de cosas que rotan en una pantalla, cada una con sus "
                        "segundos. Devuelve los que hay con sus pasos y cuanto dura la vuelta entera.");
        t.run = [p](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : p->carruseles()) {
                const QVariantMap m = v.toMap();
                json pasos = json::array();
                for (const QVariant& x : m.value("pasos").toList()) {
                    const QVariantMap pm = x.toMap();
                    pasos.push_back(json{{"tipo", pm.value("tipo").toString().toStdString()},
                                         {"segundos", pm.value("segundos").toInt()},
                                         {"que", pm.value("que").toString().toStdString()}});
                }
                out.push_back(json{{"id", m.value("id").toString().toStdString()},
                                   {"nombre", m.value("nombre").toString().toStdString()},
                                   {"transicion", m.value("transicion").toString().toStdString()},
                                   {"vueltaSeg", m.value("vueltaSeg").toInt()},
                                   {"pasos", pasos}});
            }
            done(ToolResult().data(json{{"carruseles", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "carrusel_armar";
        t.destructivaSi = "borrar"; // borrar=true borra
        t.title = ba::i18n::traducir("mcp", "Armar o cambiar un carrusel");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Crea un carrusel con sus pasos, o le cambia los pasos a uno que ya existe (`id`). Cada paso "
            "es un `tipo` de contenido (los de pantalla_mostrar menos lo en vivo: programa y escena van directo a la "
            "pantalla) con sus datos y sus `segundos`. "
            "Con `borrar: true` y un `id`, lo saca.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Si la pantalla ya lo esta mostrando, NO hace falta volver a mandarselo: la lista vive "
                    "en el servidor y el televisor la toma sola en la proxima vuelta. Por eso cambiar un "
                    "paso no hace parpadear nada.\n\n"
                    "Duraciones: un QR necesita al menos 15 segundos para que alguien lo saque del "
                    "bolsillo y lo escanee; un mensaje corto con 8 alcanza.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "para cambiar uno que ya existe")}}},
              {"nombre", {{"type", "string"}}},
              {"borrar", {{"type", "boolean"}}},
              {"transicion", {{"type", "string"}, {"enum", {"fundido", "corte"}}}},
              {"pasos",
               {{"type", "array"},
                {"items",
                 {{"type", "object"},
                  {"required", {"tipo"}},
                  {"properties",
                   {{"tipo",
                     {{"type", "string"},
                      // Lo en vivo (programa, escena) no entra en un carrusel (PantallasViewModel::pasoAgregar).
                      {"enum", {"mensaje", "qr", "reloj", "url", "prompter", "tally", "votacion"}}}},
                    {"texto", {{"type", "string"}}},
                    {"pie", {{"type", "string"}}},
                    {"url", {{"type", "string"}}},
                    {"fuente", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con tipo=tally: la camara o escena")}}},
                    {"segundos",
                     {{"type", "integer"}, {"minimum", 3}, {"maximum", 3600}, {"default", 15}}}}}}}}}}}};
        t.examples = {
            R"({"nombre": "Hall", "pasos": [{"tipo": "qr", "texto": "https://…", "pie": "Entrá a la conferencia", "segundos": 20}, {"tipo": "mensaje", "texto": "Empezamos 20:00", "segundos": 10}, {"tipo": "reloj", "segundos": 10}]})"};
        t.run = [p](const json& a, Done done) {
            QString id = QString::fromStdString(a.value("id", ""));
            if (a.value("borrar", false)) {
                if (id.isEmpty()) {
                    done(ToolResult::error(ba::i18n::traducir("mcp", "Para borrar hace falta el `id`.")));
                    return;
                }
                const QString err = p->carruselBorrar(id);
                done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")) : ToolResult::error(err.toStdString()));
                return;
            }
            if (id.isEmpty()) {
                id = p->carruselNuevo(QString::fromStdString(a.value("nombre", ba::i18n::traducir("mcp", "Carrusel"))));
            } else {
                // Se vacia: `pasos` describe el carrusel ENTERO, no lo que se
                // le suma. Asi la misma llamada da siempre el mismo resultado.
                bool hay = false;
                for (const QVariant& v : p->carruseles())
                    if (v.toMap().value("id").toString() == id) {
                        hay = true;
                        const int n = v.toMap().value("pasos").toList().size();
                        for (int i = n - 1; i >= 0; --i)
                            p->pasoQuitar(id, i);
                    }
                if (!hay) {
                    done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No existe el carrusel '%1'."), {id.toStdString()})));
                    return;
                }
                if (a.contains("nombre"))
                    p->carruselRenombrar(id, QString::fromStdString(a["nombre"].get<std::string>()));
            }
            if (a.contains("transicion"))
                p->carruselTransicion(id, QString::fromStdString(a["transicion"].get<std::string>()));
            std::string problemas;
            for (const json& paso : a.value("pasos", json::array())) {
                if (!paso.is_object() || !paso.contains("tipo")) continue;
                QVariantMap datos;
                for (const char* k : {"texto", "pie", "url", "fuente", "escena"})
                    if (paso.contains(k) && paso[k].is_string())
                        datos[k] = QString::fromStdString(paso[k].get<std::string>());
                const QString err =
                    errorDe([&] { p->pasoAgregar(id, QString::fromStdString(paso["tipo"].get<std::string>()), datos,
                                   paso.value("segundos", 15)); });
                if (!err.isEmpty()) problemas += err.toStdString() + " ";
            }
            json r{{"id", id.toStdString()}};
            done(problemas.empty()
                     ? ToolResult::ok(ba::i18n::traducir("mcp", "Listo. Para verlo: pantalla_mostrar con tipo `carrusel`.")).data(r)
                     : ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "Algunos pasos no entraron: %1"), {problemas})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_parar";
        t.title = ba::i18n::traducir("mcp", "Dejar de mostrar");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "La pantalla vuelve a lo que tenia antes (su fondo de pantalla).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"pantalla"}},
                         {"properties", {{"pantalla", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            const QString id = idDePantalla(*p, quien);
            if (id.isEmpty()) {
                done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No conozco la pantalla '%1'."), {quien.toStdString()})));
                return;
            }
            p->parar(id);
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Parando «%1»."), {quien.toStdString()})));
        };
        core_.addTool(std::move(t));
    }

    // ------------------------------------------------------------------------
    // El CANAL DE CONTROL. Una pantalla «mostrando» solo quiere decir que el
    // comando salio bien; que la pagina cargue de verdad es otra cosa, y paso
    // mas de una vez que el operador viera «mostrando» con el televisor en
    // blanco. Estas dos herramientas son las que cierran ese agujero: una
    // pregunta como esta cada superficie SEGUN ELLA MISMA, la otra le cambia
    // lo que muestra sin volver a castearla (o sea, sin parpadeo).
    {
        Tool t;
        t.name = "superficie_listar";
        t.title = ba::i18n::traducir("mcp", "Qué páginas del aparato están vivas");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Las superficies web que estan ABIERTAS y latiendo ahora mismo (la pantalla de un televisor, "
            "un carrusel, el teleprompter). Cada una dice que esta mostrando con sus propias palabras.");
        t.details = QT_TRANSLATE_NOOP("mcp", "No es lo mismo que pantalla_listar. Ahi `mostrando` significa «el comando salio "
                    "bien»; aca significa «la pagina contesta». Una pantalla que figura mostrando y NO "
                    "aparece en esta lista es una pantalla que no cargo: eso es lo que hay que mirar "
                    "antes de decirle al operador que ya esta.\n\n"
                    "`que` lo escribe la propia pagina: un carrusel dice «paso 2/3», el programa en vivo "
                    "dice cuantos cuadros lleva. `desdeSeg` es hace cuanto late.");
        t.run = [p](const json&, Done done) {
            p->pedirSuperficies([done](QVariantList sup) {
                json ss = json::array();
                for (const QVariant& v : sup) {
                    const QVariantMap m = v.toMap();
                    json o = json::object();
                    for (const QString& k : m.keys()) {
                        const QVariant val = m.value(k);
                        if (val.typeId() == QMetaType::Int || val.typeId() == QMetaType::LongLong)
                            o[k.toStdString()] = val.toLongLong();
                        else
                            o[k.toStdString()] = val.toString().toStdString();
                    }
                    ss.push_back(o);
                }
                done(ToolResult().data(json{{"superficies", ss}})); // el mismo texto, y además estructurado
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "superficie_mostrar";
        t.title = ba::i18n::traducir("mcp", "Cambiarle la página a una superficie viva");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Le cambia a una superficie lo que muestra SIN volver a castearla: viaja por el canal de "
            "control que la propia pagina tiene abierto, asi que el cambio es inmediato y sin parpadeo. "
            "Con `recargar` en true la hace recargarse en vez de irse a otra direccion.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Sin `a`, va a TODAS las superficies de ese `tipo` (y sin `tipo` tampoco, a todas): "
                    "util para mandar la sala entera a la misma pagina. Los ids y los tipos salen de "
                    "superficie_listar.\n\n"
                    "Si la superficie no esta viva no pasa nada y la cuenta vuelve en 0: eso quiere decir "
                    "que hay que castearla de nuevo con pantalla_mostrar, no empujarle una URL.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"a",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id de la superficie; vacio = todas las de `tipo`")}}},
              {"tipo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "pantalla, carrusel, programa, prompter...")}}},
              {"url", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "a donde ir")}}},
              {"recargar",
               {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "que se recargue sola, sin cambiar de pagina")}}}}}};
        t.examples = {R"({"a": "carrusel-ug465j2w", "url": "http://.../pantalla/?tipo=reloj"})",
                      R"({"tipo": "carrusel", "recargar": true})"};
        t.run = [p](const json& a, Done done) {
            const QString url = a.contains("url") && a["url"].is_string()
                                    ? QString::fromStdString(a["url"].get<std::string>())
                                    : QString();
            const bool recargar = a.value("recargar", false);
            if (url.isEmpty() && !recargar) {
                done(ToolResult::error(ba::i18n::traducir("mcp", "Hace falta `url` (a donde ir) o `recargar` en true.")));
                return;
            }
            p->empujar(a.contains("a") ? QString::fromStdString(a["a"].get<std::string>()) : QString(),
                       a.contains("tipo") ? QString::fromStdString(a["tipo"].get<std::string>()) : QString(),
                       url, recargar, [done](bool ok, int cuantas) {
                           if (!ok) {
                               done(ToolResult::error(ba::i18n::traducir("mcp", "El gateway no acepto la orden.")));
                               return;
                           }
                           done(ToolResult::ok(cuantas == 0
                                                   ? ba::i18n::traducir("mcp", "Ninguna superficie viva la recibio: no "
                                                                 "hay pagina abierta que escuche. Para "
                                                                 "que haya una, casteala con "
                                                                 "pantalla_mostrar.")
                                                   : ba::i18n::traducir("mcp", "Mandado a %n superficie. Ya cambio: "
                                                                               "superficie_listar lo confirma.",
                                                                        nullptr, cuantas)));
                       });
        };
        core_.addTool(std::move(t));
    }

    // --- El catálogo y los monitores de la máquina ------------------------------
    CatalogoVisible* cat = vm_.catalogoVisible();
    MonitoresModel* mon = vm_.monitoresModelo();
    if (cat == nullptr || mon == nullptr) return;
    {
        Tool t;
        t.name = "catalogo_listar";
        t.title = ba::i18n::traducir("mcp", "Qué se puede mostrar");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Todo lo que se puede poner en un monitor de la maquina, en una ventana suelta o en una TV de "
            "la red: el programa, el preview, cada escena, cada multiview, los paneles (padron, preguntas, "
            "votacion, Mezclador, PTZ de cada camara), las tarjetas del tablero, las paginas y lo de fondo "
            "del tablero. Cada cosa con su `id` estable.");
        // Las distribuciones de pantallas también son cosas (y con muchas escenas e invitados
        // la lista es larga): se busca como en el selector de la página Pantallas.
        t.details = QT_TRANSLATE_NOOP("mcp", "`monitor`: true si se puede dibujar en esta maquina (monitor_asignar, ui_panel_window "
                    "con `cosa`). `red`: el tipo de pantalla_mostrar que lo lleva a una TV (vacio si no "
                    "viaja). Los ids son estables: «escena:<nombre>», «multiview:<id>», «pagina:<id>», "
                    "«tarjeta:<tipo>», «ptz:<entrada>», «programa», «preview», «fondo», «web:<tipo>» (solo "
                    "TV: un cartel, un QR, la hora...)."
                    " «distribucion:<id>» es una distribucion de pantallas guardada "
                    "(pantallas_distribuciones_listar), entera en un monitor o una ventana; no va a una TV ni "
                    "adentro de otra distribucion. `buscar` filtra por palabras (sin tildes, todas, en "
                    "cualquier orden) en el titulo, el tipo y el id, igual que el buscador de la pagina "
                    "Pantallas; `tipo` deja un tipo solo (escena, video, distribucion, pagina, panel, "
                    "tarjeta, multiview, programa, fondo, web, registro).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"donde",
                            {{"type", "string"},
                             {"enum", {"todo", "monitor", "red"}},
                             {"description", QT_TRANSLATE_NOOP("mcp", "filtrar: lo que va a un monitor, lo que va a una TV, o todo")}}},
                           {"buscar",
                            {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "palabras a buscar en el titulo, el tipo y el id (vacio = todo)")}}},
                           {"tipo",
                            {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "un tipo solo: escena, video, distribucion, pagina, panel, tarjeta, multiview...")}}}}}};
        t.examples = {R"({"donde": "monitor", "tipo": "distribucion"})", R"({"donde": "monitor", "buscar": "remotos sala"})"};
        t.run = [cat](const json& a, Done done) {
            const std::string donde = a.value("donde", std::string("todo"));
            const QStringList palabras = BuscadorCatalogo::palabras(QString::fromStdString(a.value("buscar", std::string())));
            const QString tipo = QString::fromStdString(a.value("tipo", std::string())).trimmed();
            json cs = json::array();
            for (const CatalogoVisible::Cosa& c : cat->cosas()) {
                const bool local = !c.dibujo.isEmpty(), red = !c.redTipo.isEmpty();
                if ((donde == "monitor" && !local) || (donde == "red" && !red)) continue;
                if (!tipo.isEmpty() && c.grupo != tipo) continue;
                const QString grupoTitulo = CatalogoVisible::tituloDeGrupo(c.grupo);
                if (!BuscadorCatalogo::coincide(palabras, BuscadorCatalogo::textoDeBusqueda(c.titulo, grupoTitulo, c.id)))
                    continue;
                json j{{"id", c.id.toStdString()},
                       {"tipo", c.grupo.toStdString()},
                       {"grupo", grupoTitulo.toStdString()},
                       {"titulo", c.titulo.toStdString()},
                       {"monitor", local}};
                if (red) j["red"] = c.redTipo.toStdString();
                if (c.grupo == "distribucion") j["resumen"] = c.dibujo.value("resumen").toString().toStdString();
                cs.push_back(j);
            }
            done(ToolResult().data(json{{"cosas", cs}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitor_listar";
        t.title = ba::i18n::traducir("mcp", "Qué monitores hay y qué muestra cada uno");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Los monitores de ESTA maquina en su orden fisico (de izquierda a derecha y de arriba abajo), "
            "con su id estable, resolucion, si es el principal, y que muestra cada uno (Salidas, "
            "Pantallas, Pantallas conectadas). Tambien los recordados que no estan conectados: esos se "
            "esperan y su contenido no va a otro monitor.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`orden` es el numero que se ve en el dibujo de la pagina (y con monitor_identificar en "
                    "cada monitor). `id` es fabricante|modelo|serie: no cambia aunque cambie el cable o el "
                    "orden en que prenden. `muestra` son ids de catalogo_listar.");
        t.run = [mon](const json&, Done done) {
            json ms = json::array();
            for (int r = 0; r < mon->rowCount(); ++r) {
                const auto d = [&](int rol) { return mon->data(mon->index(r), rol); };
                const QRect g = d(MonitoresModel::GeometriaRole).toRect();
                json muestra = json::array();
                for (const QString& c : d(MonitoresModel::CosasRole).toStringList()) muestra.push_back(c.toStdString());
                json m{{"id", d(MonitoresModel::IdRole).toString().toStdString()},
                       {"nombre", d(MonitoresModel::NombreRole).toString().toStdString()},
                       {"conectado", d(MonitoresModel::ConectadoRole).toBool()},
                       {"muestra", muestra},
                       {"que_es", d(MonitoresModel::TituloRole).toString().toStdString()},
                       {"pantallaCompleta", d(MonitoresModel::PantallaCompletaRole).toBool()}};
                if (d(MonitoresModel::ConectadoRole).toBool()) {
                    m["orden"] = d(MonitoresModel::OrdenRole).toInt();
                    m["principal"] = d(MonitoresModel::PrincipalRole).toBool();
                    m["sistema"] = d(MonitoresModel::NombreSistemaRole).toString().toStdString();
                    m["geometria"] = json{{"x", g.x()}, {"y", g.y()}, {"ancho", g.width()}, {"alto", g.height()}};
                } else {
                    m["esperando"] = true;
                }
                ms.push_back(m);
            }
            done(ToolResult().data(json{{"monitores", ms}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitor_asignar";
        t.title = ba::i18n::traducir("mcp", "Poner algo en un monitor");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Deja FIJO en un monitor de esta maquina una cosa del catalogo (el programa, una escena, una "
            "multiview, un panel, una pagina...) o varias, que van en grilla. Se recuerda por monitor: "
            "vuelve sola al reiniciar, y si el monitor no esta se lo espera.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`monitor`: su id, su numero de `orden` («2») o su nombre, como los da monitor_listar. "
                    "`cosas`: ids de catalogo_listar (con `donde: monitor`); una sola = la pantalla entera. "
                    "Reemplaza lo que tenia. `pantallaCompleta` (si no se dice, queda como estaba; de "
                    "entrada true) la pone a pantalla completa; false = una ventana maximizada en ese "
                    "monitor.\n\n"
                    "No uses el monitor principal (donde esta la interfaz) salvo que lo pidan: lo tapa.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"monitor", "cosas"}},
            {"properties",
             {{"monitor", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id, numero de orden o nombre (monitor_listar)")}}},
              {"cosas",
               {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", QT_TRANSLATE_NOOP("mcp", "ids de catalogo_listar")}}},
              {"pantallaCompleta", {{"type", "boolean"}}}}}};
        t.examples = {R"({"monitor": "2", "cosas": ["programa"]})",
                      R"({"monitor": "2", "cosas": ["multiview:principal", "preguntas"], "pantallaCompleta": true})"};
        t.run = [mon](const json& a, Done done) {
            const QString id = mon->resolver(QString::fromStdString(a.value("monitor", std::string())));
            if (id.isEmpty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "No conozco ese monitor. monitor_listar dice cuales hay.")));
            QStringList cosas;
            if (a.contains("cosas") && a["cosas"].is_array())
                for (const json& c : a["cosas"])
                    if (c.is_string()) cosas << QString::fromStdString(c.get<std::string>());
            const bool completa =
                a.value("pantallaCompleta", mon->monitor(id).value("pantallaCompleta", true).toBool());
            const QString err = errorDe([&] { mon->asignar(id, cosas, completa); });
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            const QVariantMap m = mon->monitor(id);
            done(ToolResult::ok(ba::i18n::llenar(m.value("conectado").toBool() ? ba::i18n::traducir("mcp", "«%1» muestra %2.")
                                                                : ba::i18n::traducir("mcp", "«%1» muestra %2 cuando se lo conecte (ahora no esta)."),
                                  {m.value("nombre").toString().toStdString(), m.value("titulo").toString().toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "monitor_quitar";
        t.title = ba::i18n::traducir("mcp", "Sacar lo que muestra un monitor");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Deja un monitor de esta maquina sin nada (se cierra su ventana). Con un monitor "
                        "recordado que no esta conectado, ademas deja de esperarlo.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"monitor"}},
            {"properties",
             {{"monitor", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id, numero de orden o nombre (monitor_listar)")}}}}}};
        t.run = [mon](const json& a, Done done) {
            const QString id = mon->resolver(QString::fromStdString(a.value("monitor", std::string())));
            if (id.isEmpty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "No conozco ese monitor. monitor_listar dice cuales hay.")));
            const QString err = errorDe([&] { mon->quitar(id); });
            done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Listo: ese monitor ya no muestra nada."))
                               : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }

    // --- Las distribuciones de pantallas (pantallas/Distribuciones.hpp) ----------------
    DistribucionesModel* dis = &vm_.escritorio().distribuciones();
    // Lo que se le explica a un modelo de cada ventana (crear y editar lo comparten).
    static const char* kVentana = QT_TRANSLATE_NOOP("mcp",
        "Cada ventana: {\"nombre\": opcional, \"cosas\": [ids de catalogo_listar con donde=monitor] (una, o varias en "
        "grilla), \"disposicion\": grid|focus|pip|row (con varias cosas), \"monitor\": id, numero de orden o nombre "
        "(monitor_listar; vacio = el primero que no sea el principal), \"x\", \"y\", \"ancho\", \"alto\": el lugar "
        "RELATIVO al monitor, de 0 a 1 (la mitad izquierda: x 0, y 0, ancho 0.5, alto 1; si no se dice, el monitor "
        "entero), \"absoluta\": {x, y, ancho, alto} en pixeles desde la esquina del monitor, solo si hacen falta "
        "pixeles exactos (si no entran en el monitor se usa la relativa), \"pantallaCompleta\": bool, \"encima\": "
        "bool (siempre encima de las demas)}. Al aplicarla, las ventanas de cada monitor van adentro de UNA ventana "
        "madre sin marco: el operador no las mueve ni las cierra con el mouse.");
    const json ventanaSchema = {
        {"type", "object"},
        {"properties",
         {{"nombre", {{"type", "string"}}},
          {"cosas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"disposicion", {{"type", "string"}, {"enum", {"grid", "focus", "pip", "row"}}}},
          {"monitor", {{"type", "string"}}},
          {"x", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
          {"y", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
          {"ancho", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
          {"alto", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
          {"absoluta",
           {{"type", "object"},
            {"properties",
             {{"x", {{"type", "integer"}}}, {"y", {{"type", "integer"}}}, {"ancho", {{"type", "integer"}}}, {"alto", {{"type", "integer"}}}}}}},
          {"pantallaCompleta", {{"type", "boolean"}}},
          {"encima", {{"type", "boolean"}}}}}};
    const json ventanasSchema = {{"type", "array"},
                                 {"items", ventanaSchema},
                                 {"description", QT_TRANSLATE_NOOP("mcp", "las ventanas (ver los detalles de la herramienta)")}};
    // Una distribución como la devuelve el modelo, para el cliente.
    const auto comoJson = [dis](const QString& id) {
        json j = qvToJson(dis->porId().value(id));
        return j.is_null() ? json::object() : j;
    };
    const auto noHay = [](const std::string& quien) {
        return ToolResult::error(ba::i18n::llenar(
            ba::i18n::traducir("mcp", "No hay una distribucion «%1»: pantallas_distribuciones_listar dice cuales hay."), {quien}));
    };
    {
        Tool t;
        t.name = "pantallas_distribuciones_listar";
        t.title = ba::i18n::traducir("mcp", "Distribuciones de pantallas guardadas");
        t.category = "pantallas";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Las distribuciones de pantallas guardadas en el evento: cada una es una lista de ventanas (que muestra cada "
            "una, en que monitor, donde y de que tamaño) que se aplica de una vez. Dice cual esta aplicada, cual se abre "
            "sola al cargar el evento, los monitores conectados y las plantillas de un clic.");
        t.details = QT_TRANSLATE_NOOP("mcp",
            "Por ventana vuelve tambien `enMonitor` (donde iria ahora), `faltaMonitor` (su monitor no esta: va al que "
            "haya) y `titulo`. `aviso` es lo que paso la ultima vez que se aplico una. Las ventanas de una distribucion "
            "son SIN MARCO y QUIETAS: se mueven con pantallas_distribucion_editar.");
        t.run = [dis](const json&, Done done) {
            json ds = json::array();
            const QVariantMap todas = dis->porId();
            for (const Distribucion& d : dis->lista()) ds.push_back(qvToJson(todas.value(d.id)));
            done(ToolResult().data(json{{"distribuciones", ds},
                                        {"aplicada", dis->aplicada().toStdString()},
                                        {"aviso", dis->aviso().toStdString()},
                                        {"monitores", qvToJson(dis->monitores())},
                                        {"plantillas", qvToJson(DistribucionesModel::plantillas())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_crear";
        t.title = ba::i18n::traducir("mcp", "Crear una distribución de pantallas");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Guarda en el evento una distribucion de pantallas con nombre: la lista de ventanas que van en los monitores de "
            "esta maquina (el programa, el previo, una escena, una multiview, un invitado, un panel, una pagina...), "
            "cada una en su monitor, lugar y tamaño. O desde una `plantilla`, o copiando otra (`desde`). Con "
            "`aplicar: true` ademas la pone ya.");
        t.details = std::string(kVentana) +
                    QT_TRANSLATE_NOOP("mcp",
                        "\n\nAntes: monitor_listar (los monitores, su orden y cual es el principal) y catalogo_listar "
                        "{\"donde\": \"monitor\"} (que se puede mostrar). No pongas ventanas en el monitor principal (el "
                        "de la interfaz) salvo que lo pidan.\n\nPlantillas: mosaico-2x1, mosaico-2x2, una-por-monitor, "
                        "programa-previo-multiview (con `monitor` y `cosas` opcionales).\n\n`alAbrir: true` = se aplica "
                        "sola al cargar el evento (una sola puede serlo).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"ventanas", ventanasSchema},
                           {"plantilla",
                            {{"type", "string"},
                             {"enum", {"mosaico-2x1", "mosaico-2x2", "una-por-monitor", "programa-previo-multiview"}}}},
                           {"monitor", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con plantilla: en que monitor")}}},
                           {"cosas",
                            {{"type", "array"},
                             {"items", {{"type", "string"}}},
                             {"description", QT_TRANSLATE_NOOP("mcp", "con plantilla: que va en cada lugar, en orden")}}},
                           {"desde", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "copiar esta distribucion (id o nombre)")}}},
                           {"alAbrir", {{"type", "boolean"}}},
                           {"aplicar", {{"type", "boolean"}}}}}};
        t.examples = {
            R"({"nombre": "Remotos lado a lado", "aplicar": true, "ventanas": [{"nombre": "Remotos A", "cosas": ["escena:remotos-a"], "monitor": "2", "x": 0, "y": 0, "ancho": 0.5, "alto": 1}, {"nombre": "Remotos B", "cosas": ["escena:remotos-b"], "monitor": "2", "x": 0.5, "y": 0, "ancho": 0.5, "alto": 1}]})",
            R"({"nombre": "Control", "plantilla": "programa-previo-multiview", "monitor": "3"})",
            R"({"nombre": "Sala", "ventanas": [{"cosas": ["programa"], "monitor": "2", "pantallaCompleta": true}, {"cosas": ["multiview:principal"], "monitor": "3", "pantallaCompleta": true}]})"};
        t.run = [dis, comoJson, noHay](const json& a, Done done) {
            const QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            QString id;
            QString err;
            if (a.contains("desde")) {
                const std::string desde = a.value("desde", std::string());
                if (dis->buscar(QString::fromStdString(desde)) == nullptr) return done(noHay(desde));
                err = errorDe([&] { id = dis->duplicar(QString::fromStdString(desde), nombre); });
            } else if (a.contains("plantilla")) {
                QStringList cosas;
                for (const json& c : a.value("cosas", json::array()))
                    if (c.is_string()) cosas << QString::fromStdString(c.get<std::string>());
                err = errorDe([&] {
                    id = dis->crearDePlantilla(QString::fromStdString(a.value("plantilla", std::string())), nombre,
                                               QString::fromStdString(a.value("monitor", std::string())), cosas);
                });
            } else {
                const QVariantList vs = jsonToQv(a.value("ventanas", json::array())).toList();
                err = errorDe([&] { id = dis->crear(nombre, vs); });
            }
            if (id.isEmpty()) return done(ToolResult::error(err.isEmpty() ? std::string("?") : err.toStdString()));
            if (a.value("alAbrir", false)) dis->setAlAbrir(id, true);
            json r{{"distribucion", comoJson(id)}};
            if (a.value("aplicar", false)) {
                const QString e = errorDe([&] { dis->aplicar(id); });
                if (!e.isEmpty()) r["error_al_aplicar"] = e.toStdString();
                r["aplicada"] = e.isEmpty();
                if (!dis->aviso().isEmpty()) r["aviso"] = dis->aviso().toStdString();
            }
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_editar";
        t.title = ba::i18n::traducir("mcp", "Editar una distribución de pantallas");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Cambia una distribucion guardada: el nombre, si se abre sola al cargar el evento, la lista entera de "
            "ventanas, o una ventana (cambiarla, agregar una, quitar una). Si es la que esta aplicada, las ventanas se "
            "mueven en el acto: es la forma de mover las ventanas de una distribucion (sin marco y quietas).");
        t.details = std::string(kVentana) +
                    QT_TRANSLATE_NOOP("mcp",
                        "\n\n`ventanas` reemplaza la lista entera. `ventana` cambia SOLO lo que diga de la ventana numero "
                        "`indice` (desde 1, en el orden de pantallas_distribuciones_listar). `agregar` suma una ventana; "
                        "`quitar`, el numero de la que se saca.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"distribucion"}},
                         {"properties",
                          {{"distribucion", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre")}}},
                           {"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el nombre nuevo")}}},
                           {"alAbrir", {{"type", "boolean"}}},
                           {"ventanas", ventanasSchema},
                           {"indice", {{"type", "integer"}, {"minimum", 1}}},
                           {"ventana", ventanaSchema},
                           {"agregar", ventanaSchema},
                           {"quitar",
                            {{"type", "integer"}, {"minimum", 1}, {"description", QT_TRANSLATE_NOOP("mcp", "el numero de la ventana que se saca")}}}}}};
        t.examples = {R"({"distribucion": "Remotos lado a lado", "indice": 2, "ventana": {"monitor": "3", "x": 0, "y": 0, "ancho": 1, "alto": 1}})",
                      R"({"distribucion": "Control", "agregar": {"cosas": ["padron"], "monitor": "3", "x": 0.7, "y": 0, "ancho": 0.3, "alto": 1}})",
                      R"({"distribucion": "Control", "nombre": "Control de cabina", "alAbrir": true})"};
        t.run = [dis, comoJson, noHay](const json& a, Done done) {
            const std::string quien = a.value("distribucion", std::string());
            const Distribucion* d = dis->buscar(QString::fromStdString(quien));
            if (d == nullptr) return done(noHay(quien));
            const QString id = d->id;
            QVariantMap cambios;
            if (a.contains("nombre")) cambios["nombre"] = QString::fromStdString(a.value("nombre", std::string()));
            if (a.contains("alAbrir")) cambios["alAbrir"] = a.value("alAbrir", false);
            if (a.contains("ventanas")) cambios["ventanas"] = jsonToQv(a["ventanas"]).toList();
            QString err;
            if (!cambios.isEmpty()) err = errorDe([&] { dis->editar(id, cambios); });
            if (err.isEmpty() && a.contains("ventana")) {
                const int i = a.value("indice", 0) - 1;
                const QVariantMap v = jsonToQv(a["ventana"]).toMap();
                err = errorDe([&] { dis->setVentana(id, i, v); });
            }
            if (err.isEmpty() && a.contains("agregar")) {
                const QVariantMap v = jsonToQv(a["agregar"]).toMap();
                err = errorDe([&] { dis->agregarVentana(id, v); });
            }
            if (err.isEmpty() && a.contains("quitar")) {
                const int i = a.value("quitar", 0) - 1;
                err = errorDe([&] { dis->quitarVentana(id, i); });
            }
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            json r{{"distribucion", comoJson(id)}};
            if (!dis->aviso().isEmpty()) r["aviso"] = dis->aviso().toStdString();
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_aplicar";
        t.title = ba::i18n::traducir("mcp", "Aplicar una distribución de pantallas");
        t.category = "pantallas";
        t.clase = Clase::Ui;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Pone la distribucion EXACTAMENTE como dice: en cada monitor que usa, UNA ventana madre sin marco (a "
            "pantalla completa si ocupa el monitor entero) con sus ventanas adentro como rectangulos; las ventanas "
            "sueltas que hubiera se cierran. Cambiar de distribucion no abre ni cierra ventanas del sistema: cambia lo "
            "de adentro. Aplicarla dos veces no cambia nada.");
        t.details = QT_TRANSLATE_NOOP("mcp",
            "Si un monitor de la distribucion no esta, su ventana va al que haya y vuelve en `aviso` (no falla); si "
            "despues aparece, vuelve sola a su lugar. Lo que ya no existe (una escena borrada) se saltea, tambien con "
            "aviso. Lo fijo de Salidas -> Pantallas (monitor_asignar) no se toca (si comparte monitor, avisa). La "
            "aplicada se recuerda: vuelve sola al reiniciar. Para sacarla: pantallas_distribucion_cerrar.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"distribucion"}},
                         {"properties", {{"distribucion", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre")}}}}}};
        t.examples = {R"({"distribucion": "Remotos lado a lado"})"};
        t.run = [dis, noHay](const json& a, Done done) {
            const std::string quien = a.value("distribucion", std::string());
            if (dis->buscar(QString::fromStdString(quien)) == nullptr) return done(noHay(quien));
            const QString err = errorDe([&] { dis->aplicar(QString::fromStdString(quien)); });
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            json r{{"aplicada", dis->aplicada().toStdString()}};
            if (!dis->aviso().isEmpty()) r["aviso"] = dis->aviso().toStdString();
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_capturar";
        t.title = ba::i18n::traducir("mcp", "Guardar las ventanas de ahora como distribución");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Guarda las ventanas que hay abiertas AHORA (que muestran, en que monitor, donde) como una distribucion nueva "
            "con `nombre`, o reemplaza las ventanas de una que ya existe (`distribucion`).");
        t.details = QT_TRANSLATE_NOOP("mcp",
            "El lugar se guarda relativo al monitor (sobrevive a un cambio de resolucion). Lo fijo de Salidas -> "
            "Pantallas (monitor_asignar) no entra: son las ventanas sueltas.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"distribucion",
                            {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "reemplazar las ventanas de esta (id o nombre)")}}}}}};
        t.examples = {R"({"nombre": "Como estaba el martes"})", R"({"distribucion": "Control"})"};
        t.run = [dis, comoJson, noHay](const json& a, Done done) {
            const std::string quien = a.value("distribucion", std::string());
            QString destino;
            if (!quien.empty()) {
                const Distribucion* d = dis->buscar(QString::fromStdString(quien));
                if (d == nullptr) return done(noHay(quien));
                destino = d->id;
            }
            QString id;
            const QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            const QString err = errorDe([&] { id = dis->capturar(nombre, destino); });
            if (id.isEmpty()) return done(ToolResult::error(err.isEmpty() ? std::string("?") : err.toStdString()));
            done(ToolResult().data(json{{"distribucion", comoJson(id)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar una distribución de pantallas");
        t.category = "pantallas";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "Borra una distribucion guardada. Sus ventanas, si estan puestas, quedan como estan (para sacarlas: "
            "pantallas_distribucion_cerrar).");
        t.inputSchema = {{"type", "object"},
                         {"required", {"distribucion"}},
                         {"properties", {{"distribucion", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre")}}}}}};
        t.run = [dis, noHay](const json& a, Done done) {
            const std::string quien = a.value("distribucion", std::string());
            if (dis->buscar(QString::fromStdString(quien)) == nullptr) return done(noHay(quien));
            const QString err = errorDe([&] { dis->borrar(QString::fromStdString(quien)); });
            done(err.isEmpty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Borrada.")) : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantallas_distribucion_cerrar";
        t.title = ba::i18n::traducir("mcp", "Cerrar las ventanas de la distribución");
        t.category = "pantallas";
        t.clase = Clase::Ui;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp",
            "La salida de emergencia: cierra todas las ventanas puestas por una distribucion (van sin marco y quietas: el "
            "operador no las puede cerrar con el mouse). La distribucion queda guardada. El operador hace lo mismo con "
            "Ctrl+Shift+F12 o desde Pantallas.");
        t.run = [dis](const json&, Done done) {
            const int n = dis->cerrarVentanas();
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Cerradas: %1."), {std::to_string(n)})));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
