// ============================================================================
// src/app/mcp/McpAudioTools.cpp — rack de audio por fuente (modulos LSP)
//
// Cada fuente con audio tiene (si se quiere) un rack tipo JACK: modulos LSP
// conectados con cables, con medidores en cada modulo y analizadores. El
// asistente lo arma, lo MIDE y lo calibra:
//   entender   audio_sources, audio_rack, audio_analyze, audio_measure
//   armar      audio_module_add, audio_patch, audio_module_remove, audio_bypass
//   calibrar   audio_gate, audio_compressor, audio_deesser, audio_eq,
//              audio_multiband, audio_limiter (parametros con nombre, en
//              unidades reales) y audio_module_get/_set (cualquier parametro)
//   probar     audio_generator (señal de prueba), audio_editor (interfaz LSP)
// ============================================================================

#include "audio/PluginTerminos.hpp"
#include <set>

#include "mcp/McpIndice.hpp"
#include "mcp/McpService.hpp"

#if defined(BA_HAVE_CLAP)

#include <QCoreApplication>
#include <QTimer>

#include <algorithm>

#include <QQuickWindow>

#include "audio/AudioViewModel.hpp"
#include "audio/Modules.hpp"
#include "audio/RackManager.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

using audio::AudioRack;
using audio::RackManager;

namespace {

const json kSource{{"type", "string"}, {"description", "Nombre de la fuente (audio_sources)"}};
const json kNode{{"type", "string"}, {"description", "Id del nodo del rack (audio_rack)"}};

// Nodo por id, o el primero del tipo del catalogo si no se dio.
std::string resolveNode(AudioRack& rack, const std::string& node, const audio::ModuleType* type) {
    if (!node.empty()) return rack.has(node) ? node : std::string();
    if (type == nullptr) return {};
    const json d = rack.describe();
    for (const auto& n : d["nodos"])
        if (n.value("modulo", "") == type->pluginId) return n["id"];
    return {};
}

json moduleView(AudioRack& rack, const std::string& id) {
    audio::ClapInstance* c = rack.plugin(id);
    json out{{"nodo", id}, {"medidores", rack.meters(id)}};
    if (c == nullptr) return out;
    out["modulo"] = c->desc().id;
    if (const audio::ModuleType* t = audio::findModule(c->desc().id)) {
        out["tipo"] = t->key;
        out["parametros"] = audio::readModule(*c, *t);
    }
    return out;
}

} // namespace

void McpService::registerAudioTools() {
    core_.addCategory("audio_rack", "Rack de audio (LSP)",
                      "Procesamiento por fuente tipo JACK: modulos LSP (gate, compresor, de-esser, EQ, "
                      "multibanda, limitador...) "
                      "conectados con cables, con medidores por modulo, analizador (LUFS, espectro, piso de "
                      "ruido) y medicion "
                      "fuera de linea. Guia: help {\"topic\": \"audio_rack\"}.");
    StudioViewModel* vm = &vm_;
    auto mgr = [vm]() { return vm->racks(); };

    {
        Tool t;
        t.name = "audio_sources";
        t.title = "Fuentes con audio";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "El inventario de fuentes con SONIDO. "
            "Todas las fuentes con audio (microfonos, placas, videos con sonido, invitados...), si tienen "
            "rack, cuantos modulos y el nivel de salida.";
        t.run = [mgr](const json&, Done done) {
            std::string e;
            mgr()->library(&e);
            json r{{"fuentes", mgr()->audioSources()}};
            if (!e.empty()) r["aviso"] = e;
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_plantillas";
        t.title = "Plantillas de rack";
        t.category = "audio_rack";
        t.description =
            "Guarda el rack de una fuente con nombre, se lo aplica a otra, lo borra, o lo marca como "
            "predeterminado de un tipo de fuente. Sin argumentos, lista las que hay.";
        t.details =
            "Un rack armado a mano (compuerta, compresor, de-esser, sus parametros y sus cables) es media "
            "hora de trabajo. Guardarlo con nombre permite ponerselo a otra fuente en un clic.\n\n"
            "Lo que mas sirve es `porDefecto`: marcada para un tipo de fuente (`ba_guest_audio` son los "
            "invitados), las fuentes NUEVAS de ese tipo la toman solas al crearse. O sea que un invitado "
            "que se conecta ya suena bien sin que nadie le arme nada mientras el evento empieza. Una sola "
            "por tipo: si marcas otra, la anterior se desmarca.\n\n"
            "`aplicar` REEMPLAZA el rack que la fuente tenga: lo que habia se pierde. Si el operador "
            "armo algo a mano ahi, guardalo antes con `guardar`.\n\n"
            "Viven en Config/audio-plantillas.json, asi que viajan con la carpeta portable.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"guardar",
               {{"type", "string"}, {"description", "Nombre con el que guardar el rack de `source`."}}},
              {"aplicar", {{"type", "string"}, {"description", "Plantilla a ponerle a `source`."}}},
              {"borrar", {{"type", "string"}}},
              {"porDefecto", {{"type", "string"}, {"description", "Plantilla a marcar."}}},
              {"paraTipo",
               {{"type", "string"},
                {"description", "Tipo de fuente de libobs para `porDefecto` (vacio = desmarcar)."}}},
              {"source", {{"type", "string"}, {"description", "La fuente, para guardar o aplicar."}}}}}};
        t.run = [mgr](const json& a, Done done) {
            const std::string src = a.value("source", std::string());
            std::string err;
            if (a.contains("guardar")) {
                if (src.empty())
                    return done(ToolResult::error("Falta `source`: de que fuente guardar el rack."));
                err = mgr()->guardarPlantilla(src, a["guardar"]);
            } else if (a.contains("aplicar")) {
                if (src.empty()) return done(ToolResult::error("Falta `source`: a que fuente aplicarsela."));
                err = mgr()->aplicarPlantilla(a["aplicar"], src);
            } else if (a.contains("borrar")) {
                err = mgr()->borrarPlantilla(a["borrar"]);
            } else if (a.contains("porDefecto")) {
                err = mgr()->plantillaPorDefecto(a["porDefecto"], a.value("paraTipo", std::string()));
            }
            if (!err.empty()) return done(ToolResult::error(err));
            done(ToolResult().data(json{{"plantillas", mgr()->plantillas()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_rack";
        t.title = "Rack de una fuente";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "El grafo del rack: nodos (in, out, modulos, analizadores, generadores) con sus puertos y "
            "medidores (entradaDb, salidaDb, cambioDb = lo que el modulo sube o baja) y los cables.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"source", kSource}}}, {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult().data(json{{"rack", false}, {"detalle", e}}));
            done(ToolResult().data(r->describe()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_modules";
        t.title = "Modulos con interfaz propia";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "Los seis modulos que el aparato maneja con nombre propio —compuerta, compresor, "
            "deesser, ecualizador, multibanda, limitador— con sus parametros explicados, sus rangos "
            "y sus presets. Son los que conviene usar cuando alcanzan.";
        t.details = "Para cualquier otro de los ~200 instalados, `audio_plugin_buscar`: busca en castellano "
                    "y dice para que sirve cada uno. Aca estaba antes ese buscador, haciendo una busqueda "
                    "literal peor y devolviendo el catalogo entero encima —19 KB para leer diez lineas—. "
                    "Dos buscadores para lo mismo es uno de mas.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [](const json&, Done done) {
            json cat = json::array();
            for (const auto& m : audio::moduleCatalog()) {
                json params = json::array();
                for (const auto& p : m.params)
                    params.push_back({{"clave", p.key}, {"ayuda", p.help}});
                json bands = json::array();
                for (const auto& p : m.bandParams)
                    bands.push_back({{"clave", p.key}, {"ayuda", p.help}});
                json presets = json::array();
                for (auto it = m.presets.begin(); it != m.presets.end(); ++it)
                    presets.push_back(it.key());
                cat.push_back(
                    {{"clave", m.key},
                     {"titulo", m.title},
                     {"ayuda", m.help},
                     {"modulo", m.pluginId},
                     {"parametros", params},
                     {"parametrosPorBanda", bands},
                     {"bandas", m.bandCount > 0 ? json{m.bandFirst, m.bandFirst + m.bandCount - 1} : json()},
                     {"presets", presets}});
            }
            done(ToolResult().data(json{{"catalogo", cat}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "audio_plugin_buscar";
        t.title = "Buscar entre los ~200 plugins de audio instalados";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "Busca EN CASTELLANO entre todos los plugins de audio instalados (~200) y devuelve los "
            "que sirven para lo que pediste, con para que sirve cada uno y cuantos canales maneja. "
            "El rack no se limita a los seis modulos con interfaz propia.";
        t.details = "POR QUE HACE FALTA BUSCAR. Los plugins se describen en ingles («performs gating of "
                    "input signal») y son doscientos: ni entran en una lista ni se encuentran pidiendo lo "
                    "que uno quiere. Aca hay un indice hecho a mano con las palabras de una cabina —«que la "
                    "musica baje cuando hablan», «sacale el siseo», «que no pase de cierto volumen»— sobre "
                    "el nombre, la descripcion y las caracteristicas de cada uno.\n\n"
                    "EL `id` QUE DEVUELVE SE USA TAL CUAL en `audio_module_add`. Los seis del catalogo "
                    "(gate, compresor, deesser, eq, multibanda, limitador) tienen ademas parametros con "
                    "nombre y presets: si uno de esos alcanza, es mejor, y `audio_modules` los detalla.\n\n"
                    "`canales` dice si la version es mono, estereo o por bandas: LSP publica el mismo "
                    "aparato varias veces y hay que elegir el que corresponde a la fuente.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"buscar"}},
            {"properties",
             {{"buscar", {{"type", "string"}, {"description", "que necesitas, con tus palabras"}}},
              {"limite", {{"type", "integer"}, {"default", 10}}}}}};
        t.examples = {R"({"buscar": "que la musica baje cuando hablan"})",
                      R"({"buscar": "sacarle el siseo a la voz"})",
                      R"({"buscar": "sincronizar el audio con la imagen"})"};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            auto lib = mgr()->library(&e);
            if (!lib) return done(ToolResult::error("No se pudo abrir la biblioteca de plugins: " + e));

            // El MISMO motor que busca herramientas (McpIndice): pesos por
            // campo, IDF y prefijos. No hay razon para tener dos buscadores.
            Indice idx;
            for (const auto& p : lib->plugins()) {
                Documento d;
                d.id = p.id;
                d.clase = "plugin";
                d.nombre = audio::familiaDe(p.id);
                d.titulo = p.name;
                d.etiquetas = audio::terminosDePlugin(p.id);
                // El para-que en castellano pesa como cuerpo; la descripcion en
                // ingles tambien entra, que a veces alguien busca por el nombre
                // tecnico.
                d.cuerpo = audio::paraQueSirve(p.id) + " " + p.description;
                for (const std::string& f : p.features)
                    d.cuerpo += " " + f;
                idx.agregar(std::move(d));
            }
            idx.armar();

            const int limite = std::max(1, std::min(40, a.value("limite", 10)));
            // CUANTO DE LO QUE PREGUNTASTE MATCHEO DE VERDAD.
            //
            // El puntaje NO alcanza para saber si la respuesta sirve: «auto
            // tune corregir la afinacion» sacaba 393 —mas que el deesser
            // correcto, 281— porque «auto» es prefijo de `autogain`,
            // `automatico` y `automaticamente`. Tres coincidencias de UNA sola
            // palabra, y las otras tres («tune», «corregir», «afinacion») no
            // pegaban nada. Contestar eso como si sirviera es peor que decir
            // que no hay: manda a poner un control de volumen donde pedian un
            // corrector de tono.
            //
            // Entonces se cuenta cuantas PALABRAS DISTINTAS de la pregunta
            // aparecen entre lo que matcheo. Una sola, habiendo pedido varias,
            // es un parecido de casualidad.
            const auto palabras = Indice::terminos(a["buscar"].get<std::string>());
            // Una palabra CORTA que solo aparece como principio de otra mas
            // larga no cuenta: «auto» dentro de `autogain` no es haber
            // encontrado un auto-tune. Una palabra larga que matcheo, si:
            // «comprimir» trayendo `compresor` es un acierto, y castigarlo por
            // las palabras de relleno que la acompañan («necesito», «del») era
            // esconder la respuesta correcta.
            constexpr std::size_t kPalabraFuerte = 6;
            const auto cubiertas = [&palabras](const std::vector<std::string>& porque) {
                std::set<std::string> vistas;
                for (const std::string& w : palabras)
                    for (const std::string& t : porque) {
                        const bool exacta = t == w;
                        const bool porPrefijo = t.rfind(w, 0) == 0 || w.rfind(t, 0) == 0;
                        if (exacta || (porPrefijo && w.size() >= kPalabraFuerte)) vistas.insert(w);
                    }
                return vistas.size();
            };
            json out = json::array();
            std::size_t mejorCobertura = 0;
            for (const Resultado& r :
                 idx.buscar(a["buscar"].get<std::string>(), static_cast<std::size_t>(limite))) {
                const std::string id = idx.doc(r.doc).id;
                const audio::PluginDesc* d = nullptr;
                for (const auto& p : lib->plugins())
                    if (p.id == id) d = &p;
                if (d == nullptr) continue;
                const std::size_t cub = cubiertas(r.porque);
                mejorCobertura = std::max(mejorCobertura, cub);
                out.push_back(
                    json{{"deLoQuePediste", std::to_string(cub) + "/" + std::to_string(palabras.size())},
                         {"id", d->id},
                         {"nombre", d->name},
                         {"paraQue", audio::paraQueSirve(d->id)},
                         {"canales", audio::canalesDe(d->id)},
                         {"porque", r.porque},
                         {"descripcion", d->description}});
            }
            // Con una sola palabra pegada habiendo pedido varias, se dice que
            // no hay: ofrecer «lo mas parecido» como si sirviera es lo que hace
            // que alguien termine poniendo un control de volumen donde pidio un
            // corrector de tono.
            const bool flojo = out.empty() || mejorCobertura == 0;
            ToolResult res;
            if (out.empty())
                res.text("No hay ningun plugin instalado para eso.");
            else if (flojo)
                res.text("NO hay ninguno para eso. Lo unico que se le parece de nombre es " +
                         out[0]["id"].get<std::string>() +
                         ", que hace otra cosa: no lo pongas sin leer `paraQue`.");
            else
                res.text(std::to_string(out.size()) +
                         " plugin(s). El `id` va tal cual en `audio_module_add`.");
            res.structured = json{{"plugins", out}, {"hayAlguno", !flojo}};
            done(res);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_module_add";
        t.title = "Agregar un modulo";
        t.category = "audio_rack";
        t.description = "Agrega un modulo al rack de la fuente (lo crea si no tiene): del catalogo (gate, "
                        "compresor, deesser, "
                        "eq, multibanda, limitador) o cualquier id LSP. Se inserta en la cadena despues de "
                        "`after` (vacio = "
                        "justo antes de la salida). `preset` aplica valores de partida (p. ej. \"voz\"). "
                        "Tambien \"analizador\", y \"entrada\" con `fuente`: trae el audio de OTRA fuente "
                        "(el microfono, un invitado, un canal de placa) como una tarjeta del grafo; despues se "
                        "cablea con audio_patch (sale por \"out\"). \"traducir\" pone una tarjeta Traducir (sin "
                        "cables) que traduce la salida de texto `desde` al idioma `a`; se ajusta con traduccion_ajustar.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", kSource},
                           {"module", {{"type", "string"}}},
                           {"after", {{"type", "string"}}},
                           {"label", {{"type", "string"}}},
                           {"preset", {{"type", "string"}}},
                           {"fuente", {{"type", "string"}}},
                           {"desde", {{"type", "string"}}},
                           {"a", {{"type", "string"}, {"enum", {"es", "en", "pt", "fr", "de", "ru", "zh"}}}}}},
                         {"required", {"source", "module"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            const std::string mod = a.value("module", "");
            if (mod == "entrada") {
                const std::string id = mgr()->addEntrada(a.value("source", ""), a.value("fuente", ""), &e);
                if (id.empty()) return done(ToolResult::error(e));
                AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
                return done(ToolResult().data(json{{"nodo", id}, {"rack", r != nullptr ? r->describe() : json()}}));
            }
            if (mod == "traducir") {
                const std::string id = mgr()->addTraduccion(
                    {{"fuente", a.value("source", "")}, {"desde", a.value("desde", "")}, {"a", a.value("a", "en")}}, &e);
                if (id.empty()) return done(ToolResult::error(e));
                return done(ToolResult().data(json{{"id", id}, {"aviso", "ajustala con traduccion_ajustar {id}; traducciones muestra si falta bajar el idioma"}}));
            }
            AudioRack* r = mgr()->rack(a.value("source", ""), true, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            if (mod == "analizador" || mod == "analyzer") {
                const std::string id = r->addAnalyzer(a.value("label", ""), a.value("after", ""));
                if (id.empty()) return done(ToolResult::error("no existe el nodo " + a.value("after", "")));
                mgr()->markDirty();
                return done(ToolResult().data(json{{"nodo", id}, {"rack", r->describe()}}));
            }
            if (!mgr()->library(&e)) return done(ToolResult::error(e));
            const audio::ModuleType* type = audio::findModule(mod);
            const std::string pluginId = type != nullptr ? type->pluginId : mod;
            const std::string id =
                r->addPlugin(pluginId, a.value("label", type != nullptr ? type->title : ""),
                             a.value("after", ""), &e, type != nullptr ? type->key : "");
            if (id.empty()) return done(ToolResult::error(e));
            json res{{"nodo", id}};
            const std::string preset = a.value("preset", "");
            if (!preset.empty()) {
                if (type == nullptr || !type->presets.contains(preset))
                    res["aviso"] = "no hay preset '" + preset + "' para ese modulo";
                else
                    res["preset"] = audio::applyModule(*r->plugin(id), type, type->presets[preset]);
            }
            mgr()->markDirty();
            res["modulo"] = moduleView(*r, id);
            done(ToolResult().data(res));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_module_remove";
        t.title = "Quitar un modulo";
        t.category = "audio_rack";
        t.destructive = true;
        t.description = "Quita un nodo del rack; lo que entraba se conecta a donde salia (la cadena sigue). "
                        "`node: \"*\"` quita el rack entero (el audio vuelve a pasar directo).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"source", kSource}, {"node", kNode}}},
                         {"required", {"source", "node"}}};
        t.run = [mgr](const json& a, Done done) {
            const std::string src = a.value("source", "");
            if (a.value("node", "") == "*") {
                return done(mgr()->removeRack(src) ? ToolResult::ok("Rack quitado.")
                                                   : ToolResult::error("esa fuente no tiene rack"));
            }
            std::string e;
            AudioRack* r = mgr()->rack(src, false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            if (!r->remove(a.value("node", ""), &e)) return done(ToolResult::error(e));
            mgr()->markDirty();
            done(ToolResult().data(r->describe()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_patch";
        t.title = "Conectar cables";
        t.category = "audio_rack";
        t.description =
            "Conecta y/o desconecta cables como en JACK: {desde, salida (\"out\"), hacia, entrada "
            "(\"in\" o \"sc\" para sidechain)}. Varios cables a una entrada se suman (ramas en "
            "paralelo). No se permiten ciclos. Por CANAL: agregar `canalSalida` y `canalEntrada` con "
            "\"L\" o \"R\" (p. ej. la derecha de un modulo a la izquierda de otro, o usar un solo "
            "canal); sin ellos el cable lleva el puerto entero (L->L y R->R). Un cable de puerto entero "
            "y uno de canal entre los mismos puertos no conviven: el ultimo reemplaza al anterior. "
            "Ojo: si la salida \"out\" queda sin nada conectado, la fuente se calla.";
        const json canal{{"type", "string"}, {"enum", {"L", "R", "LR"}}};
        const json cable{{"type", "object"},
                         {"properties",
                          {{"desde", {{"type", "string"}}},
                           {"salida", {{"type", "string"}}},
                           {"hacia", {{"type", "string"}}},
                           {"entrada", {{"type", "string"}}},
                           {"canalSalida", canal},
                           {"canalEntrada", canal}}},
                         {"required", {"desde", "hacia"}}};
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", kSource},
                           {"conectar", {{"type", "array"}, {"items", cable}}},
                           {"desconectar", {{"type", "array"}, {"items", cable}}}}},
                         {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), true, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            json errors = json::array();
            // "L" = izquierdo, "R" = derecho, ausente o "LR" = el puerto entero.
            const auto canal = [](const json& c, const char* key) {
                const std::string v = c.value(key, std::string());
                if (v == "L" || v == "l") return 0;
                if (v == "R" || v == "r") return 1;
                return -1;
            };
            for (const auto& c : a.value("desconectar", json::array()))
                if (!r->disconnect(c.value("desde", ""), c.value("salida", ""), c.value("hacia", ""),
                                   c.value("entrada", ""), canal(c, "canalSalida"), canal(c, "canalEntrada")))
                    errors.push_back("no habia cable " + c.value("desde", "") + " -> " +
                                     c.value("hacia", ""));
            for (const auto& c : a.value("conectar", json::array())) {
                std::string ce;
                if (!r->connect(c.value("desde", ""), c.value("salida", "out"), c.value("hacia", ""),
                                c.value("entrada", "in"), &ce, canal(c, "canalSalida"),
                                canal(c, "canalEntrada")))
                    errors.push_back(ce);
            }
            mgr()->markDirty();
            json res = r->describe();
            if (!errors.empty()) res["errores"] = errors;
            done(ToolResult().data(res));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_ordenar";
        t.title = "Acomodar las tarjetas del rack";
        t.category = "audio_rack";
        t.description = "Acomoda las tarjetas del rack de `source` en la grilla siguiendo el orden de la cadena (el botón "
                        "«Ordenar» de la página Audio). No cambia ningún cable.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"source", kSource}}}, {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e.empty() ? "esa fuente no tiene rack" : e));
            r->layoutGrid();
            mgr()->markDirty();
            done(ToolResult().data(r->describe()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_bypass";
        t.title = "Anular / activar un modulo";
        t.category = "audio_rack";
        t.description =
            "bypass=true: el modulo deja pasar el audio sin procesarlo (para comparar antes/despues).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"source", kSource}, {"node", kNode}, {"bypass", {{"type", "boolean"}}}}},
            {"required", {"source", "node", "bypass"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            if (!r->setBypass(a.value("node", ""), a.value("bypass", false)))
                return done(ToolResult::error("no es un modulo del rack"));
            mgr()->markDirty();
            done(ToolResult::ok(a.value("bypass", false) ? "Modulo anulado." : "Modulo activo."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_module_get";
        t.title = "Parametros de un modulo";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "Parametros del modulo con sus valores (en unidades) y medidores. Los del catalogo con sus "
            "claves; `todos: true` lista todos los parametros LSP (con opciones o rango).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"source", kSource}, {"node", kNode}, {"todos", {{"type", "boolean"}}}}},
            {"required", {"source", "node"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            const std::string id = a.value("node", "");
            audio::ClapInstance* c = r->plugin(id);
            if (c == nullptr) return done(ToolResult::error("no es un modulo del rack"));
            json res = moduleView(*r, id);
            if (a.value("todos", false)) {
                json all = json::array();
                for (const auto& p : c->params())
                    if (!p.hidden()) all.push_back(audio::paramJson(*c, p));
                res["todosLosParametros"] = all;
            }
            done(ToolResult().data(res));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_module_set";
        t.title = "Cambiar parametros de un modulo";
        t.category = "audio_rack";
        t.description =
            "Cambia parametros: claves del catalogo (umbral_db, ratio...) o nombres LSP (audio_module_get "
            "todos). Valores en unidades reales: numeros (-30, 20, 4000) o texto (\"20 ms\", \"Bell\", "
            "\"on\"). `preset` aplica uno del catalogo antes.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", kSource},
                           {"node", kNode},
                           {"params", {{"type", "object"}}},
                           {"preset", {{"type", "string"}}}}},
                         {"required", {"source", "node"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            const std::string id = a.value("node", "");
            audio::ClapInstance* c = r->plugin(id);
            if (c == nullptr) return done(ToolResult::error("no es un modulo del rack"));
            const audio::ModuleType* type = audio::findModule(c->desc().id);
            json res;
            const std::string preset = a.value("preset", "");
            if (!preset.empty() && type != nullptr && type->presets.contains(preset))
                res["preset"] = audio::applyModule(*c, type, type->presets[preset]);
            res["resultado"] = audio::applyModule(*c, type, a.value("params", json::object()));
            mgr()->markDirty();
            res["modulo"] = moduleView(*r, id);
            done(ToolResult().data(res));
        };
        core_.addTool(std::move(t));
    }
    // Interfaces con nombre para los modulos mas usados (se crean si faltan).
    struct Friendly {
        const char* tool;
        const char* module;
        const char* title;
    };
    for (const Friendly& f :
         {Friendly{"audio_gate", "gate", "Compuerta (gate)"},
          Friendly{"audio_compressor", "compresor", "Compresor"},
          Friendly{"audio_deesser", "deesser", "De-esser"}, Friendly{"audio_eq", "eq", "Ecualizador"},
          Friendly{"audio_multiband", "multibanda", "Compresor multibanda"},
          Friendly{"audio_limiter", "limitador", "Limitador"}}) {
        const audio::ModuleType* type = audio::findModule(f.module);
        if (type == nullptr) continue;
        Tool t;
        t.name = f.tool;
        t.title = f.title;
        t.category = "audio_rack";
        json props{{"source", kSource},
                   {"node", {{"type", "string"}, {"description", "Nodo (vacio = el primero de este tipo)"}}},
                   {"crear", {{"type", "boolean"}, {"default", true}, {"description", "Crearlo si no esta"}}},
                   {"after", {{"type", "string"}, {"description", "Al crearlo: despues de que nodo"}}},
                   {"preset", {{"type", "string"}}}};
        for (const auto& p : type->params)
            props[p.key] = {{"description", p.help}};
        if (type->bandCount > 0) {
            json bandProps{{"banda", {{"type", "integer"}}}};
            for (const auto& p : type->bandParams)
                bandProps[p.key] = {{"description", p.help}};
            props["bandas"] = {
                {"type", "array"},
                {"items", {{"type", "object"}, {"properties", bandProps}, {"required", {"banda"}}}}};
        }
        std::string presets;
        for (auto it = type->presets.begin(); it != type->presets.end(); ++it)
            presets += (presets.empty() ? "" : ", ") + it.key();
        // Con las palabras del que se queja, no las del manual. `type->help`
        // describe el modulo; esto dice CUANDO se usa, que es lo que el modelo
        // tiene que emparejar con «los graves suenan mal».
        const std::string cuando =
            std::string(f.module) == "eq"          ? "Ecualiza: cuando los graves o los agudos suenan mal, "
                                                     "retumba, suena metalico o apagado. "
            : std::string(f.module) == "gate"      ? "Compuerta: cierra el microfono cuando nadie habla, "
                                                     "para que no se escuche todo lo de alrededor. "
            : std::string(f.module) == "limitador" ? "Limitador: para que no sature ni pase de cierto nivel. "
            : std::string(f.module) == "compresor" ? "Compresor: empareja lo muy fuerte con lo muy bajo. "
                                                   : std::string();
        t.description = cuando + type->help +
                        " Sin parametros solo lee. Devuelve valores y medidores (cambioDb = lo que "
                        "el modulo baja/sube ahora)." +
                        (presets.empty() ? std::string() : " Presets: " + presets + ".");
        t.inputSchema = {{"type", "object"}, {"properties", props}, {"required", {"source"}}};
        t.run = [mgr, type](const json& a, Done done) {
            std::string e;
            if (!mgr()->library(&e)) return done(ToolResult::error(e));
            AudioRack* r = mgr()->rack(a.value("source", ""), true, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            std::string id = resolveNode(*r, a.value("node", ""), type);
            json res;
            if (id.empty()) {
                if (!a.value("node", "").empty() || !a.value("crear", true))
                    return done(ToolResult::error("no hay " + type->title + " en el rack de esa fuente"));
                id = r->addPlugin(type->pluginId, type->title, a.value("after", ""), &e, type->key);
                if (id.empty()) return done(ToolResult::error(e));
                res["creado"] = id;
            }
            audio::ClapInstance* c = r->plugin(id);
            const std::string preset = a.value("preset", "");
            if (!preset.empty() && type->presets.contains(preset))
                res["preset"] = audio::applyModule(*c, type, type->presets[preset]);
            json values = json::object();
            for (auto it = a.begin(); it != a.end(); ++it)
                if (it.key() != "source" && it.key() != "node" && it.key() != "crear" &&
                    it.key() != "after" && it.key() != "preset")
                    values[it.key()] = it.value();
            if (!values.empty()) res["resultado"] = audio::applyModule(*c, type, values);
            if (!values.empty() || res.contains("creado") || res.contains("preset")) mgr()->markDirty();
            res["modulo"] = moduleView(*r, id);
            done(ToolResult().data(res));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_analyze";
        t.title = "Analizar el audio";
        t.category = "audio";
        t.readOnly = true;
        t.description =
            "Numeros crudos del espectro en un punto del rack, para verificar un ajuste. "
            "Analiza el audio en un punto del rack (\"in\" = la fuente cruda, \"out\" = lo que sale, o un "
            "nodo "
            "analizador) durante `segundos` (0-30; 0 = lo ya medido): pico, RMS, LUFS (momentaneo, corto, "
            "integrado), piso de ruido, nivel activo, rango dinamico, espectro por tercios y por zonas "
            "(graves, "
            "cuerpo, medios, presencia, sibilancia, aire), correlacion, recortes y pistas de mejora. Crea el "
            "rack "
            "(vacio: no cambia el sonido) si hace falta. Para calibrar un gate: medir con la persona "
            "hablando Y "
            "callada.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", kSource},
                           {"punto", {{"type", "string"}, {"default", "in"}}},
                           {"segundos", {{"type", "number"}, {"default", 5}, {"maximum", 30}}},
                           {"reiniciar", {{"type", "boolean"}, {"default", true}}}}},
                         {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            const std::string src = a.value("source", "");
            AudioRack* r = mgr()->rack(src, true, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            const std::string point = a.value("punto", "in");
            audio::Analyzer* an = r->analyzer(point);
            if (an == nullptr)
                return done(ToolResult::error("'" + point + "' no es in, out ni un analizador"));
            const double secs = std::clamp(a.value("segundos", 5.0), 0.0, 30.0);
            if (secs > 0 && a.value("reiniciar", true)) an->requestReset();
            QTimer::singleShot(static_cast<int>(secs * 1000), QCoreApplication::instance(),
                               [mgr, src, point, done]() {
                                   std::string e2;
                                   AudioRack* r2 = mgr()->rack(src, false, &e2);
                                   audio::Analyzer* an2 = r2 != nullptr ? r2->analyzer(point) : nullptr;
                                   if (an2 == nullptr)
                                       return done(ToolResult::error("el rack cambio mientras se medía"));
                                   json rep = audio::Analyzer::report(an2->snapshot());
                                   rep["punto"] = point;
                                   json mods = json::object();
                                   for (const auto& id : r2->nodeIds())
                                       if (r2->kindOf(id) == "plugin") mods[id] = r2->meters(id);
                                   if (!mods.empty()) rep["medidoresModulos"] = mods;
                                   done(ToolResult().data(rep));
                               });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        // MEDIR NO ES EL RACK. `audio_measure` y `audio_analyze` solo miran: no
        // tocan una ganancia ni mueven un modulo. Estaban catalogadas en
        // `audio_rack` porque viven en este archivo, y eso las dejaba fuera del
        // juego del asistente: ante un acople no podia ni medir, que es lo
        // primero que hay que hacer. La categoria dice QUE ES la herramienta,
        // no en que archivo quedo.
        t.name = "audio_measure";
        t.title = "Medir un modulo o la cadena (fuera de linea)";
        t.category = "audio";
        t.readOnly = true;
        t.description =
            "Que esta pasando con el audio AHORA: es lo primero ante un acople, un pitido, saturacion, un "
            "zumbido, o cuando alguien no se escucha o no se escucha nada al aire. Mide copias de los "
            "modulos con señales de prueba SIN tocar lo que sale al aire: `tipo: "
            "respuesta` "
            "(ganancia por tercio de octava con un seno a `nivel_db`: para ver un EQ o un filtro) o `curva` "
            "(salida vs entrada de 1 kHz de -80 a 0 dBFS: para ver donde y cuanto actua un gate/compresor). "
            "Sin "
            "`node`, toda la cadena principal.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"source", kSource},
                           {"node", kNode},
                           {"tipo", {{"type", "string"}, {"enum", {"respuesta", "curva"}}}},
                           {"nivel_db", {{"type", "number"}, {"default", -20}}}}},
                         {"required", {"source", "tipo"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            const json m =
                r->measure(a.value("tipo", "respuesta"), a.value("node", ""), a.value("nivel_db", -20.0), &e);
            if (m.is_null()) return done(ToolResult::error(e));
            done(ToolResult().data(m));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_generator";
        t.title = "Generador de señal de prueba";
        t.category = "audio_rack";
        t.description = "Crea o ajusta un generador en el rack: `senal` seno | rosa | blanco | barrido | "
                        "silencio, `frecuencia`, "
                        "`nivel_db` (dBFS; ruidos en RMS). Queda SUELTO: para oirlo o medirlo, `conectar_a` "
                        "un nodo (p. ej. un "
                        "analizador). Conectarlo a la cadena que sale al aire exige `al_aire: true` (se "
                        "escucharia en vivo).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"source", kSource},
              {"node", kNode},
              {"senal", {{"type", "string"}, {"enum", {"seno", "rosa", "blanco", "barrido", "silencio"}}}},
              {"frecuencia", {{"type", "number"}}},
              {"frecuencia_fin", {{"type", "number"}}},
              {"segundos", {{"type", "number"}}},
              {"nivel_db", {{"type", "number"}}},
              {"conectar_a", {{"type", "string"}}},
              {"al_aire", {{"type", "boolean"}}}}},
            {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), true, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            std::string id = a.value("node", "");
            if (id.empty() || r->generator(id) == nullptr) id = r->addGenerator("");
            audio::Generator* g = r->generator(id);
            audio::Generator::Config c = g->config();
            if (a.contains("senal") && !audio::Generator::parseKind(a.value("senal", ""), c.kind))
                return done(ToolResult::error("senal: seno, rosa, blanco, barrido o silencio"));
            c.freq = a.value("frecuencia", c.freq);
            c.freqEnd = a.value("frecuencia_fin", c.freqEnd);
            c.seconds = a.value("segundos", c.seconds);
            c.levelDb = std::min(0.0, a.value("nivel_db", c.levelDb));
            g->configure(c);
            const std::string to = a.value("conectar_a", "");
            if (!to.empty()) {
                // ¿Llega a la salida? Entonces sale al aire.
                const bool live = to == "out" || r->kindOf(to) == "plugin";
                if (live && !a.value("al_aire", false))
                    return done(
                        ToolResult::error("conectarlo a '" + to +
                                          "' lo manda al aire: repetir con al_aire=true si es a proposito"));
                if (!r->connect(id, "out", to, "in", &e)) return done(ToolResult::error(e));
            }
            mgr()->markDirty();
            done(ToolResult().data(json{{"nodo", id}, {"rack", r->describe()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_outputs";
        t.title = "Salidas de audio del equipo";
        t.category = "audio_rack";
        t.readOnly = true;
        t.description =
            "QUE ES: las salidas fisicas de esta maquina (auriculares, parlantes, HDMI), agrupadas por "
            "placa. PARA QUE: escuchar aparte lo que pasa por cualquier punto del rack de una fuente, sin "
            "tocar lo que sale al aire ni a la mezcla del programa. Con `audio_output_set` se agrega al "
            "grafo una tarjeta con esa salida y despues se le conecta lo que se quiera oir (audio_patch). "
            "OJO: el motor reproduce por UNA salida a la vez; elegir otra cambia la de todos.";
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        t.run = [mgr](const json&, Done done) {
            done(ToolResult().data(json{{"dispositivos", mgr()->outputDevices()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_output_set";
        t.title = "Mandar el rack a una salida del equipo";
        t.category = "audio_rack";
        t.description =
            "COMO SE USA: `salida` es el id que informa `audio_outputs`. Agrega (o reapunta) en el grafo "
            "de esa fuente una tarjeta con el nombre de la salida; el audio recien se oye cuando se le "
            "conecta algo con `audio_patch` (p. ej. {desde: \"in\", hacia: \"salida-audio\"}). No "
            "cambia lo que sale al aire. `grabar: true` graba todo lo que le llega a la tarjeta en un .wav "
            "(Recordings/Audio/); `grabar: false` lo corta (sin `salida`, solo cambia eso).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"source", kSource}, {"salida", {{"type", "string"}}}, {"grabar", {{"type", "boolean"}}}}},
                         {"required", {"source"}}};
        t.run = [mgr](const json& a, Done done) {
            const std::string src = a.value("source", "");
            if (a.contains("salida")) {
                const std::string e = mgr()->setOutput(src, a.value("salida", ""));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            if (a.contains("grabar")) {
                const std::string e = mgr()->setGrabar(src, a.value("grabar", false));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            std::string re;
            AudioRack* r = mgr()->rack(src, false, &re);
            done(ToolResult().data(json{{"rack", r != nullptr ? r->describe() : json::object()}, {"grabacion", mgr()->grabacion(src)}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_show";
        t.title = "Mostrar el rack en la pantalla";
        t.category = "audio_rack";
        t.description =
            "Abre la pestaña Audio en la fuente y (opcional) el nodo: para mostrarle al operador lo que "
            "estas ajustando (parametros, medidores, espectro).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"source", kSource}, {"node", kNode}}},
                         {"required", {"source"}}};
        McpService* self = this;
        t.run = [self, vm](const json& a, Done done) {
            auto* au = static_cast<audio::AudioViewModel*>(vm->audio());
            QQuickWindow* w = self->mainWindow_ ? self->mainWindow_() : nullptr;
            if (au == nullptr || w == nullptr)
                return done(ToolResult::error("La interfaz grafica no esta abierta."));
            au->selectSource(QString::fromStdString(a.value("source", "")));
            if (!a.value("node", "").empty()) au->select(QString::fromStdString(a.value("node", "")));
            w->setProperty("page", 9);
            done(ToolResult::ok("Pestaña Audio: " + a.value("source", "")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_lsp_restablecer_interfaz";
        t.title = "Restablecer la interfaz LSP";
        t.category = "audio_rack";
        t.description = "Cierra las ventanas de los modulos LSP y vuelve la configuracion de pantalla de LSP "
                        "(zoom, escala "
                        "del host, letra, zoom por plugin) a la de fabrica, con copia del archivo. Para "
                        "cuando la interfaz "
                        "de un modulo no aparece o no entra en la pantalla.";
        t.run = [mgr](const json&, Done done) {
            bool ok = false;
            const std::string r = mgr()->restablecerInterfazLsp(&ok);
            done(ok ? ToolResult::ok(r) : ToolResult::error(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "audio_editor";
        t.title = "Abrir la interfaz del modulo";
        t.category = "audio_rack";
        t.openWorld = true;
        t.description =
            "Abre (o cierra con abrir=false) la interfaz grafica completa de LSP del modulo en una "
            "ventana (para que la persona ajuste con los graficos del plugin).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"source", kSource}, {"node", kNode}, {"abrir", {{"type", "boolean"}}}}},
            {"required", {"source", "node"}}};
        t.run = [mgr](const json& a, Done done) {
            std::string e;
            AudioRack* r = mgr()->rack(a.value("source", ""), false, &e);
            if (r == nullptr) return done(ToolResult::error(e));
            audio::ClapInstance* c = r->plugin(a.value("node", ""));
            if (c == nullptr) return done(ToolResult::error("no es un modulo del rack"));
            if (!a.value("abrir", true)) {
                c->closeEditor();
                return done(ToolResult::ok("Interfaz cerrada."));
            }
            if (!c->openEditor(c->desc().name + " — " + a.value("source", ""), &e))
                return done(ToolResult::error(e));
            done(ToolResult::ok("Interfaz abierta."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp

#else

namespace ba::app::mcp {
void McpService::registerAudioTools() {}
} // namespace ba::app::mcp

#endif
