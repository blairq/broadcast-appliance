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
// ============================================================================

#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/PantallasViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/StudioViewModel.hpp"

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
    core_.addCategory("pantallas", "Pantallas externas",
                      "Televisores y proyectores de la sala (Chromecast): cuáles hay y qué mostrar en "
                      "cada uno (un mensaje, un QR, la hora, una página, el teleprompter).");
    auto* p = static_cast<PantallasViewModel*>(vm_.pantallas());
    if (p == nullptr) return;

    {
        Tool t;
        t.name = "pantalla_listar";
        t.title = "Que pantallas hay";
        t.category = "pantallas";
        t.readOnly = true;
        t.description =
            "Que televisores conoce el sistema y como esta cada uno: libre, ocupado por nosotros, "
            "tomado por otra aplicacion, o sin responder.";
        t.details = "`estado` dice mucho: `ocupada` es que el televisor esta con otra aplicacion (Netflix, "
                    "YouTube) y mandarle algo se la va a pisar — avisale al operador antes. `apagada` es "
                    "que no contesta: puede estar apagado de verdad o en otra red.\n\n"
                    "`sabeMostrarPaginas`: 1 si, 0 no (un parlante sin pantalla contesta el protocolo "
                    "igual), -1 todavia no se pregunto. `fijo` es lo que tiene que mostrar SIEMPRE: vuelve "
                    "solo cada vez que el aparato aparece.";
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
        t.title = "Buscar televisores en la red";
        t.category = "pantallas";
        t.description = "Busca Chromecast en la red local (tarda unos segundos). Las que aparezcan quedan "
                        "en la lista; volve a mirar con pantalla_listar.";
        t.run = [p](const json&, Done done) {
            p->buscar();
            done(ToolResult::ok("Buscando. En unos segundos preguntale a pantalla_listar que aparecio."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_mostrar";
        t.title = "Mostrar algo en una pantalla";
        t.category = "pantallas";
        t.description =
            "MUESTRA algo (un QR, una pagina, un cartel, la votacion) en un televisor de la red. Le manda un "
            "contenido a una pantalla. `tipo`: mensaje (texto grande), qr (un QR a pantalla "
            "completa), reloj, url (una direccion cualquiera), prompter (la vista del orador), carrusel "
            "(una rotacion ya armada), programa (lo que sale al aire, en vivo por WebRTC), escena (UNA "
            "escena cualquiera de las armadas, en vivo, este o no al aire: `escena` es su nombre) o tally (rojo "
            "si esa camara esta al aire, verde si esta armada). Los campos de cada tipo salen de "
            "pantalla_listar.";
        t.details = "Ojo con la que este `ocupada`: mostrarle algo le pisa lo que estuviera pasando. Si no "
                    "te lo pidieron explicitamente, preguntale al operador con `operator_ask`.\n\n"
                    "El texto se lee DE LEJOS: frases cortas y pocas palabras. «Volvemos en 10 minutos» "
                    "sirve; un parrafo, no.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla", "tipo"}},
            {"properties",
             {{"pantalla", {{"type", "string"}, {"description", "id o nombre, como lo da pantalla_listar"}}},
              {"tipo",
               {{"type", "string"},
                {"enum",
                 {"mensaje", "qr", "reloj", "url", "prompter", "carrusel", "programa", "escena", "tally",
                  "votacion"}}}},
              {"carrusel", {{"type", "string"}, {"description", "su id, con tipo=carrusel"}}},
              {"qr",
               {{"type", "string"},
                {"description", "con tipo=qr: \"aparato:publico\" (la pagina del publico, con su clave), "
                                "\"aparato:sala\", \"aparato:prompter\", \"aparato:rotulos\", o el id de uno GUARDADO (qr_listar los da), que sale "
                                "con el estilo con el que se diseño. Sin esto se usa `texto` y "
                                "sale con el estilo de fabrica."}}},
              {"texto", {{"type", "string"}, {"description", "mensaje: lo que dice; qr: lo que codifica"}}},
              {"pie", {{"type", "string"}, {"description", "la linea de abajo, mas chica"}}},
              {"url", {{"type", "string"}, {"description", "solo con tipo=url"}}},
              {"escena",
               {{"type", "string"},
                {"description", "con tipo=escena: el nombre de la escena (scene_list las da). Varias "
                                "pantallas pueden mostrar escenas distintas a la vez, hasta 4 distintas"}}},
              {"fuente",
               {{"type", "string"},
                {"description", "con tipo=tally: la camara o escena de esa pantalla (vacio = el estado "
                                "general, para la cabina)"}}}}}};
        t.examples = {
            R"({"pantalla": "TV del hall", "tipo": "mensaje", "texto": "Volvemos en 10 minutos"})",
            R"({"pantalla": "TV del hall", "tipo": "qr", "texto": "https://...", "pie": "Escaneá para entrar"})"};
        t.run = [p, this](const json& a, Done done) {
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            const QString id = idDePantalla(*p, quien);
            if (id.isEmpty()) {
                done(
                    ToolResult::error("No conozco la pantalla '" + quien.toStdString() +
                                      "'. pantalla_listar dice cuales hay; pantalla_buscar busca de nuevo."));
                return;
            }
            const QString tipo = QString::fromStdString(a["tipo"].get<std::string>());
            if (tipo == "carrusel") {
                if (!a.contains("carrusel")) {
                    done(ToolResult::error("Con tipo=carrusel hace falta `carrusel` (su id). "
                                           "carrusel_listar los da."));
                    return;
                }
                const QString err =
                    p->mostrarCarrusel(id, QString::fromStdString(a["carrusel"].get<std::string>()));
                done(err.isEmpty() ? ToolResult::ok("Mandado. Tarda un par de segundos.")
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
                    done(ToolResult::error("La sala todavia no dio ese enlace (¿esta andando?): proba de nuevo en un momento."));
                    return;
                }
                if (guardado.isEmpty()) {
                    done(ToolResult::error("No tengo guardado un codigo QR con id '" + qid.toStdString() +
                                           "'. qr_listar los da."));
                    return;
                }
                datos["texto"] = guardado.value("texto");
                datos["estilo"] = guardado.value("estilo");
                if (!datos.contains("pie")) datos["pie"] = guardado.value("nombre");
            }
            const QString err = p->mostrar(id, tipo, datos);
            if (!err.isEmpty()) {
                done(ToolResult::error(err.toStdString()));
                return;
            }
            done(ToolResult::ok("Mandado a «" + id.toStdString() +
                                "». Tarda un par de segundos: pantalla_listar dice como salio."));
        };
        core_.addTool(std::move(t));
    }
    // Buscar la pantalla por id o por NOMBRE: el operador (y el asistente)
    // piensan en "el televisor del hall", no en un id de servicio mDNS.
    {
        Tool t;
        t.name = "pantalla_fijar";
        t.title = "Que una pantalla muestre siempre lo mismo";
        t.category = "pantallas";
        t.description =
            "Deja fijo lo que esa pantalla tiene que mostrar: se lo manda ahora y CADA VEZ que el aparato "
            "vuelva a aparecer (se apago de noche, se reinicio, le cambio la direccion). Con `quitar` en "
            "true deja de tener algo fijo (no apaga lo que este mostrando).";
        t.details = "Es para las pantallas que viven puestas: la del hall con el QR de la sala, la del "
                    "fondo con el reloj. Lo fijado se guarda contra el nombre de servicio del aparato, no "
                    "contra su IP, asi que sobrevive a que el router le de otra direccion.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla"}},
            {"properties",
             {{"pantalla", {{"type", "string"}}},
              {"tipo",
               {{"type", "string"}, {"enum", {"mensaje", "qr", "reloj", "url", "prompter", "programa", "escena"}}}},
              {"texto", {{"type", "string"}}},
              {"pie", {{"type", "string"}}},
              {"url", {{"type", "string"}}},
              {"escena", {{"type", "string"}, {"description", "con tipo=escena: su nombre"}}},
              {"quitar", {{"type", "boolean"}, {"description", "deja de tener algo fijo"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString id = idDePantalla(*p, QString::fromStdString(a["pantalla"].get<std::string>()));
            if (id.isEmpty()) {
                done(ToolResult::error("No conozco la pantalla '" + a["pantalla"].get<std::string>() +
                                       "'. pantalla_listar dice cuales hay."));
                return;
            }
            if (a.value("quitar", false)) {
                const QString err = p->desfijar(id);
                done(err.isEmpty() ? ToolResult::ok("Ya no tiene nada fijo.")
                                   : ToolResult::error(err.toStdString()));
                return;
            }
            if (!a.contains("tipo")) {
                done(ToolResult::error("Falta `tipo` (o `quitar`: true)."));
                return;
            }
            QVariantMap datos;
            for (const char* k : {"texto", "pie", "url", "fuente", "escena"})
                if (a.contains(k) && a[k].is_string())
                    datos[k] = QString::fromStdString(a[k].get<std::string>());
            const QString err = p->fijar(id, QString::fromStdString(a["tipo"].get<std::string>()), datos);
            done(err.isEmpty()
                     ? ToolResult::ok("Fijado, y mandado ahora. Va a volver solo cada vez que el aparato "
                                      "aparezca.")
                     : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_renombrar";
        t.title = "Cambiarle el nombre a una pantalla";
        t.category = "pantallas";
        t.idempotent = true;
        t.description = "El nombre que pongas gana sobre el que anuncia el aparato: «TV del hall» dice mas "
                        "que «Percee TV», y es como la vas a nombrar despues.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pantalla", "nombre"}},
            {"properties",
             {{"pantalla", {{"type", "string"}}}, {"nombre", {{"type", "string"}, {"minLength", 1}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString id = idDePantalla(*p, QString::fromStdString(a["pantalla"].get<std::string>()));
            if (id.isEmpty()) {
                done(ToolResult::error("No conozco la pantalla '" + a["pantalla"].get<std::string>() + "'."));
                return;
            }
            const QString err = p->renombrar(id, QString::fromStdString(a["nombre"].get<std::string>()));
            done(err.isEmpty() ? ToolResult::ok("Listo.") : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_agregar";
        t.title = "Agregar una pantalla a mano";
        t.category = "pantallas";
        t.description = "Para cuando la busqueda en la red no llega (otra VLAN, el firewall): se agrega con "
                        "su direccion IP. Tiene que ser un aparato que hable Google Cast.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"host"}},
                         {"properties",
                          {{"host", {{"type", "string"}, {"description", "192.168.1.50"}}},
                           {"nombre", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString err = p->agregar(QString::fromStdString(a.value("nombre", "")),
                                           QString::fromStdString(a["host"].get<std::string>()), 0);
            done(err.isEmpty() ? ToolResult::ok("Agregada. pantalla_listar dice si contesta.")
                               : ToolResult::error(err.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "carrusel_listar";
        t.title = "Los carruseles que hay";
        t.category = "pantallas";
        t.readOnly = true;
        t.description = "Un carrusel es una lista de cosas que rotan en una pantalla, cada una con sus "
                        "segundos. Devuelve los que hay con sus pasos y cuanto dura la vuelta entera.";
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
        t.title = "Armar o cambiar un carrusel";
        t.category = "pantallas";
        t.description =
            "Crea un carrusel con sus pasos, o le cambia los pasos a uno que ya existe (`id`). Cada paso "
            "es un `tipo` de contenido (los mismos de pantalla_mostrar) con sus datos y sus `segundos`. "
            "Con `borrar: true` y un `id`, lo saca.";
        t.details = "Si la pantalla ya lo esta mostrando, NO hace falta volver a mandarselo: la lista vive "
                    "en el servidor y el televisor la toma sola en la proxima vuelta. Por eso cambiar un "
                    "paso no hace parpadear nada.\n\n"
                    "Duraciones: un QR necesita al menos 15 segundos para que alguien lo saque del "
                    "bolsillo y lo escanee; un mensaje corto con 8 alcanza.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}, {"description", "para cambiar uno que ya existe"}}},
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
                      {"enum", {"mensaje", "qr", "reloj", "url", "prompter", "programa", "escena"}}}},
                    {"texto", {{"type", "string"}}},
                    {"pie", {{"type", "string"}}},
                    {"url", {{"type", "string"}}},
                    {"segundos",
                     {{"type", "integer"}, {"minimum", 3}, {"maximum", 3600}, {"default", 15}}}}}}}}}}}};
        t.examples = {
            R"({"nombre": "Hall", "pasos": [{"tipo": "qr", "texto": "https://…", "pie": "Entrá a la sala", "segundos": 20}, {"tipo": "mensaje", "texto": "Empezamos 20:00", "segundos": 10}, {"tipo": "reloj", "segundos": 10}]})"};
        t.run = [p](const json& a, Done done) {
            QString id = QString::fromStdString(a.value("id", ""));
            if (a.value("borrar", false)) {
                if (id.isEmpty()) {
                    done(ToolResult::error("Para borrar hace falta el `id`."));
                    return;
                }
                const QString err = p->carruselBorrar(id);
                done(err.isEmpty() ? ToolResult::ok("Borrado.") : ToolResult::error(err.toStdString()));
                return;
            }
            if (id.isEmpty()) {
                id = p->carruselNuevo(QString::fromStdString(a.value("nombre", "Carrusel")));
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
                    done(ToolResult::error("No existe el carrusel '" + id.toStdString() + "'."));
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
                    p->pasoAgregar(id, QString::fromStdString(paso["tipo"].get<std::string>()), datos,
                                   paso.value("segundos", 15));
                if (!err.isEmpty()) problemas += err.toStdString() + " ";
            }
            json r{{"id", id.toStdString()}};
            done(problemas.empty()
                     ? ToolResult::ok("Listo. Para verlo: pantalla_mostrar con tipo `carrusel`.").data(r)
                     : ToolResult::error("Algunos pasos no entraron: " + problemas));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pantalla_parar";
        t.title = "Dejar de mostrar";
        t.category = "pantallas";
        t.idempotent = true;
        t.description = "La pantalla vuelve a lo que tenia antes (su fondo de pantalla).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"pantalla"}},
                         {"properties", {{"pantalla", {{"type", "string"}}}}}};
        t.run = [p](const json& a, Done done) {
            const QString quien = QString::fromStdString(a["pantalla"].get<std::string>());
            const QString id = idDePantalla(*p, quien);
            if (id.isEmpty()) {
                done(ToolResult::error("No conozco la pantalla '" + quien.toStdString() + "'."));
                return;
            }
            p->parar(id);
            done(ToolResult::ok("Parando «" + quien.toStdString() + "»."));
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
        t.title = "Que paginas del aparato estan vivas";
        t.category = "pantallas";
        t.readOnly = true;
        t.description =
            "Las superficies web que estan ABIERTAS y latiendo ahora mismo (la pantalla de un televisor, "
            "un carrusel, el teleprompter). Cada una dice que esta mostrando con sus propias palabras.";
        t.details = "No es lo mismo que pantalla_listar. Ahi `mostrando` significa «el comando salio "
                    "bien»; aca significa «la pagina contesta». Una pantalla que figura mostrando y NO "
                    "aparece en esta lista es una pantalla que no cargo: eso es lo que hay que mirar "
                    "antes de decirle al operador que ya esta.\n\n"
                    "`que` lo escribe la propia pagina: un carrusel dice «paso 2/3», el programa en vivo "
                    "dice cuantos cuadros lleva. `desdeSeg` es hace cuanto late.";
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
                done(ToolResult::ok(json{{"superficies", ss}}.dump(2)));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "superficie_mostrar";
        t.title = "Cambiarle la pagina a una superficie viva";
        t.category = "pantallas";
        t.description =
            "Le cambia a una superficie lo que muestra SIN volver a castearla: viaja por el canal de "
            "control que la propia pagina tiene abierto, asi que el cambio es inmediato y sin parpadeo. "
            "Con `recargar` en true la hace recargarse en vez de irse a otra direccion.";
        t.details = "Sin `a`, va a TODAS las superficies de ese `tipo` (y sin `tipo` tampoco, a todas): "
                    "util para mandar la sala entera a la misma pagina. Los ids y los tipos salen de "
                    "superficie_listar.\n\n"
                    "Si la superficie no esta viva no pasa nada y la cuenta vuelve en 0: eso quiere decir "
                    "que hay que castearla de nuevo con pantalla_mostrar, no empujarle una URL.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"a",
               {{"type", "string"}, {"description", "id de la superficie; vacio = todas las de `tipo`"}}},
              {"tipo", {{"type", "string"}, {"description", "pantalla, carrusel, programa, prompter..."}}},
              {"url", {{"type", "string"}, {"description", "a donde ir"}}},
              {"recargar",
               {{"type", "boolean"}, {"description", "que se recargue sola, sin cambiar de pagina"}}}}}};
        t.examples = {R"({"a": "carrusel-ug465j2w", "url": "http://.../pantalla/?tipo=reloj"})",
                      R"({"tipo": "carrusel", "recargar": true})"};
        t.run = [p](const json& a, Done done) {
            const QString url = a.contains("url") && a["url"].is_string()
                                    ? QString::fromStdString(a["url"].get<std::string>())
                                    : QString();
            const bool recargar = a.value("recargar", false);
            if (url.isEmpty() && !recargar) {
                done(ToolResult::error("Hace falta `url` (a donde ir) o `recargar` en true."));
                return;
            }
            p->empujar(a.contains("a") ? QString::fromStdString(a["a"].get<std::string>()) : QString(),
                       a.contains("tipo") ? QString::fromStdString(a["tipo"].get<std::string>()) : QString(),
                       url, recargar, [done](bool ok, int cuantas) {
                           if (!ok) {
                               done(ToolResult::error("El gateway no acepto la orden."));
                               return;
                           }
                           done(ToolResult::ok(cuantas == 0
                                                   ? std::string("Ninguna superficie viva la recibio: no "
                                                                 "hay pagina abierta que escuche. Para "
                                                                 "que haya una, casteala con "
                                                                 "pantalla_mostrar.")
                                                   : "Mandado a " + std::to_string(cuantas) +
                                                         " superficie(s). Ya cambio: superficie_listar lo "
                                                         "confirma."));
                       });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
