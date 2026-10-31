// ============================================================================
// src/app/mcp/McpConfiguracionesTools.cpp — configuraciones con nombre, por MCP
//
// Cada configuración es una carpeta con todo lo suyo (escenas, medios, plugins,
// Cuerpo, botonera, eventos…); cambiar de una a otra no copia nada (reinicia con
// la otra carpeta). Ver arranque/Configuraciones.hpp.
// ============================================================================

#include "arranque/Configuraciones.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerConfiguracionesTools() {
    Configuraciones* cf = vm_.configuraciones();
    if (cf == nullptr) return;
    core_.addCategory("configuraciones", ba::i18n::traducir("mcp", "Configuraciones (cada una con lo suyo)"),
                      ba::i18n::traducir("mcp", "Configuraciones con nombre: cada una con sus escenas, medios, plugins, Cuerpo, botonera y "
                      "eventos, sin compartir nada; cambiar de una a otra no copia nada (reinicia la app)."));
    const json nombre = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "El nombre de la configuración.")}};
    // --- Eventos: cada carpeta es un evento con todo lo suyo (PLAN-EVENTO-TODO) ----------
    core_.addCategory("eventos", ba::i18n::traducir("mcp", "Eventos (cada uno con todo lo suyo)"),
                      ba::i18n::traducir("mcp", "Cada evento tiene sus escenas, mezclador, medios, gráficos, asamblea, orden del día, web y "
                      "guion. Crear uno nuevo nunca toca el abierto; abrir otro reinicia la app."));
    {
        Tool t;
        t.name = "eventos_listar";
        t.title = ba::i18n::traducir("mcp", "Los eventos");
        t.category = "eventos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los eventos que hay (cuál está abierto, de dónde salió cada uno, cuánto ocupa, cuáles son "
                        "plantilla) y las plantillas para crear uno nuevo.");
        // Carpetas y archivos (Configuraciones: los tamaños, con su candado): en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
        t.run = [cf](const json&, Done done) {
            done(ToolResult().data({{"eventos", cf->listar()}, {"abierto", cf->activa()}, {"plantillas", cf->plantillas()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_nuevo";
        t.title = ba::i18n::traducir("mcp", "Crear un evento nuevo (arrancar de cero o desde otro)");
        t.category = "eventos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea un evento APARTE con todo lo suyo, sin tocar el abierto (antes de prepararlo: la guía de su "
                        "tipo, evento_escenarios / evento_escenario). Es lo que corresponde cuando "
                        "piden «arrancar de cero», «un evento nuevo» o «algo nuevo». `desde`: «cero» (de fábrica, "
                        "vacío), «actual» (COPIA TOTAL del abierto: escenas, mezclador, medios, asamblea, página web, "
                        "guion, padrón, orden del día, votaciones, historial y grabaciones), el nombre de otro evento "
                        "(copia total de ese), «plantilla:<nombre>» (copia total de una plantilla; eventos_listar dice "
                        "cuáles) o «evento:<carpeta>/<inicio|cierre|ultima>» (solo los ajustes con que se hizo un "
                        "evento cerrado). Una copia puede tardar (grabaciones): la respuesta llega al terminar. Si no "
                        "dijeron desde dónde, preguntá. `abrir: true` pasa a él (reinicia la app: no con algo al aire). "
                        "`cps`: los cuadros por segundo de su lienzo («25», «50», «30000/1001»; sin esto, los que "
                        "traiga): con una red eléctrica de 50 Hz el lienzo de fábrica (30) puede parpadear con luces "
                        "LED; video_estado (lienzo) dice la red y sus cadencias. Ofrecelo, no lo elijas solo.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre", "desde"}},
                         {"properties",
                          {{"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "El nombre del evento.")}}},
                           {"desde", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"abrir", {{"type", "boolean"}}},
                           {"cps", {{"type", "string"}}}}}};
        StudioViewModel* vm = &vm_;
        t.run = [cf, vm](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            const bool alAire = vm->alAire();
            const bool abrir = a.value("abrir", false) && !alAire;
            // En otro hilo (una copia total puede ser de varios GB): contesta al terminar.
            const QString e = cf->nuevoEventoAparte(
                QString::fromStdString(n), QString::fromStdString(a.value("desde", std::string("cero"))), abrir,
                [done, n, abrir, alAire, pidio = a.value("abrir", false)](const QString& err) {
                    if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                    if (abrir) return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creado «%1»: la app se reinicia con él en unos segundos. Seguís en esta conversación: al volver te aviso que ya está abierto. No hagas nada más hasta entonces."), {n})));
                    if (pidio && alAire)
                        return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creado «%1», pero no lo abro: hay una transmisión o una "
                                                   "grabación en curso (abrirlo reinicia la app). Abrilo con "
                                                   "evento_cambiar cuando termine."), {n})));
                    done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creado «%1». Para abrirlo: evento_cambiar (reinicia la app)."), {n})));
                },
                QString::fromStdString(a.value("cps", std::string())));
            if (!e.isEmpty()) done(ToolResult::error(e.toStdString()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_cambiar";
        t.title = ba::i18n::traducir("mcp", "Abrir otro evento");
        t.category = "eventos";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "abre otro evento (reinicia el programa: corta todo lo que sale)");
        t.description = QT_TRANSLATE_NOOP("mcp", "Pasa al evento `nombre` (sus escenas, mezclador, medios, asamblea, web…): la app se REINICIA y "
                        "se corta todo lo que sale (el programa, la sala, las pantallas, la conferencia). No con una "
                        "transmisión o una grabación en curso; pedile confirmación al operador antes.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties", {{"nombre", {{"type", "string"}}}, {"aunque_este_al_aire", {{"type", "boolean"}}}}}};
        StudioViewModel* vm = &vm_;
        t.run = [cf, vm](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            if (n == cf->activa()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ya es el evento abierto")));
            if (vm->alAire() && !a.value("aunque_este_al_aire", false))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "hay una transmisión o una grabación en curso: abrir otro evento reinicia "
                                              "la app y la corta. Esperá a que termine (o que el operador lo pida).")));
            if (const std::string e = cf->activar(n); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Abriendo «%1»: la app se reinicia en unos segundos. Seguís en esta conversación: al volver te aviso que ya está abierto. No hagas nada más hasta entonces."), {n})));
            cf->reiniciar();
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_borrar";
        t.title = ba::i18n::traducir("mcp", "Borrar un evento (a la papelera)");
        t.category = "eventos";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Manda el evento `nombre` a la papelera con TODO lo suyo (escenas, medios, grabaciones, "
                        "historial…). El abierto no se borra, ni uno que se esté copiando. Se puede recuperar con "
                        "evento_recuperar hasta que se vacíe la papelera (sola, al mes). Confirmá con el operador cuál "
                        "es antes: el nombre exacto sale de eventos_listar (si es plantilla, decíselo).");
        t.inputSchema = {{"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", {{"type", "string"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            bool eraPlantilla = false;
            for (const json& e : cf->listar())
                if (e.value("nombre", std::string()) == n) eraPlantilla = e.value("plantilla", false);
            if (const std::string e = cf->borrar(n); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(eraPlantilla ? ba::i18n::traducir("mcp", "«%1» quedó en la papelera (evento_recuperar lo devuelve; a los %2 días se "
                                                    "borra solo). Era plantilla: ya no aparece en «Evento nuevo › desde "
                                                    "una plantilla».")
                                               : ba::i18n::traducir("mcp", "«%1» quedó en la papelera (evento_recuperar lo devuelve; a los %2 días se "
                                                    "borra solo)."),
                                  {n, std::to_string(Configuraciones::kDiasPapelera)}))
                     .data({{"eventos", cf->listar()}, {"papelera", cf->papelera()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_recuperar";
        t.title = ba::i18n::traducir("mcp", "Recuperar un evento borrado");
        t.category = "eventos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Sin `id`: lo que hay en la papelera (lo último primero). Con `id` (de esa lista): lo vuelve a "
                        "la lista de eventos con su nombre de antes, o con `como` si ese nombre ya está usado.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"id", {{"type", "string"}}}, {"como", {{"type", "string"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string id = a.value("id", std::string());
            if (id.empty()) return done(ToolResult().data({{"papelera", cf->papelera()}}));
            if (const std::string e = cf->recuperar(id, a.value("como", std::string())); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Recuperado.")).data({{"eventos", cf->listar()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_papelera_vaciar";
        t.title = ba::i18n::traducir("mcp", "Vaciar la papelera de eventos (borrar de verdad)");
        t.category = "eventos";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra DEFINITIVAMENTE de la papelera el `id` (de evento_recuperar), o todo si no viene: libera "
                        "el disco y ya no se puede recuperar. Pedile confirmación al operador (con cuánto ocupa).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}};
        t.run = [cf](const json& a, Done done) {
            if (const std::string e = cf->vaciarPapelera(a.value("id", std::string())); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Borrando (en segundo plano).")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuraciones_listar";
        t.title = ba::i18n::traducir("mcp", "Las configuraciones");
        t.category = "configuraciones";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las configuraciones que hay (cuál es la activa, su descripción, de dónde salió y cuánto ocupa) "
                        "y las demos que trae el programa.");
        // Carpetas y archivos (Configuraciones: los tamaños, con su candado): en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
        t.run = [cf](const json&, Done done) {
            done(ToolResult().data({{"configuraciones", cf->listar()}, {"activa", cf->activa()}, {"demos", cf->demos()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_crear";
        t.title = ba::i18n::traducir("mcp", "Crear una configuración");
        t.category = "configuraciones";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea una configuración nueva (no la activa). `desde`: «vacia» (la de fábrica, de fábrica), "
                        "«actual» (una copia de la activa), el nombre de otra, «demo:<nombre>» (una demo del programa: "
                        "configuraciones_listar las dice) o «evento:<carpeta>/<inicio|cierre|ultima>» (la configuración "
                        "con la que se hizo un evento, con los medios de la actual). Las grabaciones no se copian salvo "
                        "`con_grabaciones`.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", nombre},
                           {"desde", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"con_grabaciones", {{"type", "boolean"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string e = cf->crear(a.value("nombre", std::string()), a.value("desde", std::string("vacia")),
                                            a.value("descripcion", std::string()), a.value("con_grabaciones", false));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Creada «%1». Para usarla: configuracion_cambiar (reinicia la app)."),
                                  {a.value("nombre", std::string())})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_cambiar";
        t.title = ba::i18n::traducir("mcp", "Cambiar de configuración");
        t.category = "configuraciones";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "cambia de configuración (reinicia el programa: corta todo lo que sale)");
        t.description = QT_TRANSLATE_NOOP("mcp", "Pasa a la configuración `nombre`: queda anotada y la app se REINICIA con ella (no copia nada). "
                        "Se corta todo lo que esté andando (transmisión, grabación, conferencia): pedile confirmación al "
                        "operador antes.");
        t.inputSchema = {{"type", "object"}, {"required", {"nombre"}}, {"properties", {{"nombre", nombre}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            if (n == cf->activa()) return done(ToolResult::error(ba::i18n::traducir("mcp", "ya es la activa")));
            if (const std::string e = cf->activar(n); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Cambiando a «%1»: la app se reinicia en unos segundos. Seguís en esta conversación: al volver te aviso que ya está abierto. No hagas nada más hasta entonces."), {n})));
            cf->reiniciar();
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "configuracion_editar";
        t.title = ba::i18n::traducir("mcp", "Renombrar, describir o borrar una configuración");
        t.category = "configuraciones";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`renombrar_a`: le cambia el nombre; `descripcion`: la anota; `borrar: true`: la manda a la "
                        "papelera (Configuraciones/.papelera). La activa no se renombra ni se borra.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"nombre"}},
                         {"properties",
                          {{"nombre", nombre},
                           {"renombrar_a", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"borrar", {{"type", "boolean"}}}}}};
        t.run = [cf](const json& a, Done done) {
            const std::string n = a.value("nombre", std::string());
            std::string e;
            if (a.contains("descripcion")) e = cf->describir(n, a.value("descripcion", std::string()));
            if (e.empty() && a.value("borrar", false)) e = cf->borrar(n);
            else if (e.empty() && a.contains("renombrar_a")) e = cf->renombrar(n, a.value("renombrar_a", std::string()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"configuraciones", cf->listar()}}));
        };
        core_.addTool(std::move(t));
    }
    // --- Las plantillas de evento (el menú «Plantillas…» de la página del evento) -------
    {
        Tool t;
        t.name = "plantillas_listar";
        t.title = ba::i18n::traducir("mcp", "Las plantillas de evento (para administrarlas)");
        t.category = "eventos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Todas las plantillas en su orden: id (la carpeta: «plantilla:<id>» en evento_nuevo), el nombre que "
                        "se ve, descripción, escenario (su guía: evento_escenario), si es visible, si viene con el programa, "
                        "si es propia o una del programa editada, y si está abierta.");
        // Carpetas y archivos: en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
        t.run = [cf](const json&, Done done) { done(ToolResult().data({{"plantillas", cf->plantillasAdmin()}})); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "plantilla_ajustar";
        t.title = ba::i18n::traducir("mcp", "Ordenar, renombrar u ocultar una plantilla");
        t.category = "eventos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia de la plantilla `id` lo que venga: `nombre` (el que se ve; \"\" = el de la carpeta), "
                        "`descripcion`, `visible` (si aparece en «Evento nuevo» y en «Prepararlo con el asistente») y "
                        "`mover` (-1 un lugar arriba, +1 abajo). Para EDITAR su contenido hay que abrirla (desde el menú "
                        "Plantillas…, reinicia la app); para una nueva desde el evento abierto, `desde_actual` con su nombre.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"descripcion", {{"type", "string"}}},
                           {"visible", {{"type", "boolean"}}},
                           {"mover", {{"type", "integer"}}},
                           {"desde_actual", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "nombre de una plantilla nueva copiando el evento abierto")}}},
                           {"escenario", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con desde_actual: su tipo (evento_escenarios)")}}}}}};
        t.run = [cf](const json& a, Done done) {
            if (a.contains("desde_actual")) {
                const std::string e = cf->plantillaDesdeActual(a.value("desde_actual", std::string()), a.value("escenario", std::string()));
                if (!e.empty()) return done(ToolResult::error(e));
                return done(ToolResult().data({{"plantillas", cf->plantillasAdmin()}}));
            }
            const std::string id = a.value("id", std::string());
            if (id.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta `id` (plantillas_listar)")));
            json c = a;
            for (const char* k : {"id", "mover", "desde_actual", "escenario"}) c.erase(k);
            if (!c.empty())
                if (const std::string e = cf->plantillaAjustar(id, c); !e.empty()) return done(ToolResult::error(e));
            if (a.contains("mover"))
                if (const std::string e = cf->plantillaMover(id, a.value("mover", 0)); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"plantillas", cf->plantillasAdmin()}}));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
