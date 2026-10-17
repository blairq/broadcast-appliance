// ============================================================================
// src/app/mcp/McpProcedimientoTools.cpp — los procedimientos estándar de un evento
//
// Dos herramientas que el asistente usa al armar un evento (help: procedimiento_salon y
// procedimiento_asamblea):
//
//   evento_preparar   deja listo el principio y el final: las escenas «Señal de ajuste»
//                     (barras con la hora + tono de 1 kHz a −20 dBFS + la banda con el
//                     nombre del evento), «Antes de empezar» (la placa con la hora de
//                     inicio) y «Fin de transmisión», y los bloques «Previa» y «Cierre» del
//                     guion. Con `al_aire`, la señal de ajuste queda en el programa.
//   evento_verificar  la lista de control según el tipo de evento (salón o asamblea):
//                     revisa sola todo lo que se puede revisar, dice qué falta y cómo se
//                     arregla, y anota en la lista de chequeo del evento lo que solo puede
//                     confirmar una persona (para que el asistente se lo pida).
//
// Las dos trabajan LLAMANDO A LAS HERRAMIENTAS QUE YA HAY, por el mismo camino que el
// asistente (core.callTool): las mismas validaciones, la misma guarda de aire y la misma
// aprobación humana. No hay una puerta de atrás.
// ============================================================================
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"

namespace ba::app::mcp {
namespace {

const char* const kEscAjuste = "Señal de ajuste";
const char* const kEscAntes = "Antes de empezar";
const char* const kEscFin = "Fin de transmisión";
const char* const kFteBarras = "Ajuste — barras y tono";
const char* const kFteTono = "Ajuste — tono 1 kHz";
const char* const kFteIdent = "Ajuste — identificación";
const char* const kFteAntes = "Placa — antes de empezar";
const char* const kFteFin = "Placa — fin de transmisión";

struct Resp {
    bool ok = false;
    json datos;
    std::string texto;
};

/// Una herramienta, por el camino del asistente.
void llamar(McpCore& core, const std::string& tool, const json& args, std::function<void(Resp)> k) {
    core.callTool(
        json{{"name", tool}, {"arguments", args}},
        [k](json r) {
            Resp x;
            x.ok = !r.value("isError", false);
            if (r.contains("structuredContent")) x.datos = r["structuredContent"];
            if (r.contains("content") && r["content"].is_array() && !r["content"].empty())
                x.texto = r["content"][0].value("text", std::string());
            k(x);
        },
        CallContext{"", "", "procedimiento"});
}

/// Varias lecturas en serie; cada respuesta queda con el nombre que se le dio.
void leerVarias(McpCore& core, std::vector<std::pair<std::string, std::pair<std::string, json>>> pedidos,
                std::function<void(std::shared_ptr<std::map<std::string, Resp>>)> fin) {
    auto ps = std::make_shared<decltype(pedidos)>(std::move(pedidos));
    auto out = std::make_shared<std::map<std::string, Resp>>();
    auto i = std::make_shared<std::size_t>(0);
    // La función se llama a sí misma por un weak_ptr (sin ciclo de shared_ptr) y la mantiene
    // viva la respuesta pendiente. Ojo: una herramienta puede contestar EN EL ACTO (sin
    // volver al bucle), así que esto se anida; nada de destruir la función mientras corre.
    auto sig = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> wsig = sig;
    *sig = [&core, ps, out, i, fin, wsig]() {
        if (*i >= ps->size()) {
            fin(out);
            return;
        }
        const auto p = (*ps)[(*i)++];
        auto self = wsig.lock();
        llamar(core, p.second.first, p.second.second, [out, p, self](Resp r) {
            (*out)[p.first] = std::move(r);
            (*self)();
        });
    };
    (*sig)();
}

struct Paso {
    std::string que;
    std::string tool;
    json args;
    bool opcional = false;
};

/// Pasos en serie. Si uno (no opcional) falla, se frena ahí y se dice cuál: seguir dejaría
/// el sistema a medio armar sin que nadie sepa dónde.
void enSerie(McpCore& core, std::vector<Paso> pasos, std::function<void(json hechos, std::string error)> fin) {
    auto ps = std::make_shared<std::vector<Paso>>(std::move(pasos));
    auto hechos = std::make_shared<json>(json::array());
    auto i = std::make_shared<std::size_t>(0);
    auto sig = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> wsig = sig; // ver leerVarias: sin ciclo, sin destruir en uso
    *sig = [&core, ps, hechos, i, fin, wsig]() {
        if (*i >= ps->size()) {
            fin(*hechos, std::string());
            return;
        }
        const Paso p = (*ps)[(*i)++];
        auto self = wsig.lock();
        llamar(core, p.tool, p.args, [hechos, p, fin, self](Resp r) {
            hechos->push_back({{"paso", p.que}, {"tool", p.tool}, {"ok", r.ok}, {"dijo", r.texto.substr(0, 240)}});
            if (!r.ok && !p.opcional) {
                fin(*hechos, p.que + " (" + p.tool + "): " + r.texto);
                return;
            }
            (*self)();
        });
    };
    (*sig)();
}

std::set<std::string> nombresDeEscenas(const json& sceneList) {
    std::set<std::string> s;
    for (const auto& e : sceneList.value("scenes", json::array())) s.insert(e.value("name", std::string()));
    return s;
}

std::set<std::string> fuentesDe(const json& sceneList, const std::string& escena) {
    std::set<std::string> s;
    for (const auto& e : sceneList.value("scenes", json::array()))
        if (e.value("name", std::string()) == escena)
            for (const auto& f : e.value("sources", json::array()))
                if (f.is_string()) s.insert(f.get<std::string>());
    return s;
}

/// «Empezamos a las 19:00» (y el huso si no es el del aparato: hay gente de otros lados).
std::string empiezaTexto(const json& g, const std::string& husoAparato, const char* prefijo) {
    const std::string inicio = g.value("inicio", std::string());
    if (inicio.empty()) return std::string(prefijo) + " en breve";
    std::string s = std::string(prefijo) + " a las " + inicio;
    const std::string huso = g.value("huso", std::string());
    if (!huso.empty() && huso != husoAparato) {
        const auto barra = huso.rfind('/');
        std::string ciudad = barra == std::string::npos ? huso : huso.substr(barra + 1);
        for (char& c : ciudad)
            if (c == '_') c = ' ';
        s += " (hora de " + ciudad + ")";
    }
    return s;
}

/// «12/10/2026» de «2026-10-12».
std::string fechaCorta(const std::string& f) {
    if (f.size() != 10) return f;
    return f.substr(8, 2) + "/" + f.substr(5, 2) + "/" + f.substr(0, 4);
}

int indiceDeBloque(const json& guion, const std::string& nombre) {
    int i = 0;
    for (const auto& b : guion.value("bloques", json::array())) {
        if (b.value("nombre", std::string()) == nombre) return i;
        ++i;
    }
    return -1;
}

} // namespace

void McpService::registerProcedimientoTools() {
    McpCore& core = core_;

    // ------------------------------------------------------------------ evento_preparar
    {
        Tool t;
        t.name = "evento_preparar";
        t.title = "Preparar el principio y el final del evento";
        t.category = "evento";
        t.description =
            "Arma la señal de ajuste (barras con la hora, tono de 1 kHz y el nombre del evento), la placa «Antes de "
            "empezar» con la hora de inicio, la de «Fin de transmisión» y los bloques «Previa» y «Cierre» del guion. "
            "Con `al_aire`, deja la señal de ajuste en el programa (lo que se transmite al prender).";
        t.details =
            "Es el paso de los procedimientos estándar (help procedimiento_salon / procedimiento_asamblea) que deja "
            "un evento listo para su inicio. Se puede correr las veces que haga falta: lo que ya existe no se duplica "
            "(las escenas y fuentes se reusan; las placas se vuelven a dibujar con los datos de ahora; los bloques "
            "«Previa» y «Cierre» solo se agregan si no están).\n\n"
            "ESCENAS: «Señal de ajuste» = «Ajuste — barras y tono» (barras HD con la hora, que muestra si la señal "
            "está viva) + «Ajuste — tono 1 kHz» (−20 dBFS, sigue a la escena: se oye SOLO con ella al aire) + la banda "
            "«Ajuste — identificación» con el nombre del evento y cuándo empieza. «Antes de empezar» = la placa con el "
            "nombre y «Empezamos a las HH:MM». «Fin de transmisión» = gracias + el nombre del evento.\n\n"
            "GUION: «Previa» al principio (señal de ajuste → espera la confirmación de que se ve en la plataforma → "
            "placa de espera con la música → espera para arrancar) y «Cierre» al final (música de cierre → placa de "
            "fin → espera para cortar la transmisión y la grabación). `musica_espera` y `musica_cierre` son archivos "
            "de Medios/ (media_list); sin ellos, los cues de música no se agregan.\n\n"
            "MOSTRALE AL OPERADOR lo que vas a hacer antes, y después corré evento_verificar. `al_aire: true` cambia lo "
            "que sale al aire (pasa por la guarda de aire): solo antes de prender la transmisión o cuando el operador "
            "lo pide; sin eso, la señal de ajuste queda armada en el preview.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"musica_espera", {{"type", "string"}, {"description", "archivo de Medios/ para la previa"}}},
              {"musica_cierre", {{"type", "string"}, {"description", "archivo de Medios/ para el cierre"}}},
              {"bajada", {{"type", "string"}, {"description", "fecha, lugar o dónde seguirlo (placa de espera)"}}},
              {"cierre", {{"type", "string"}, {"description", "bajada de la placa de fin (dónde queda la grabación)"}}},
              {"acento", {{"type", "string"}, {"description", "color del evento (#rrggbb)"}}},
              {"guion", {{"type", "boolean"}, {"description", "agregar los bloques Previa y Cierre (de fábrica, sí)"}}},
              {"al_aire", {{"type", "boolean"}, {"description", "poner la señal de ajuste en el programa"}}}}}};
        t.examples = {R"({})", R"({"musica_espera": "espera.mp3", "musica_cierre": "cierre.mp3", "al_aire": true})"};
        t.run = [&core](const json& a, Done done) {
            leerVarias(core,
                       {{"ev", {"guion_ver", json::object()}},
                        {"esc", {"scene_list", json::object()}},
                        {"web", {"evento_web", json{{"accion", "ver"}}}}},
                       [&core, a, done](std::shared_ptr<std::map<std::string, Resp>> r) {
                           const json ev = (*r)["ev"].datos;
                           if (!(*r)["ev"].ok || ev.value("abierto", std::string()).empty())
                               return done(ToolResult::error("No hay un evento abierto (guion_abrir o evento_armar)."));
                           const json g = ev.value("guion", json::object());
                           const std::string husoAparato = ev.value("husoDelAparato", std::string());
                           const std::string nombre = g.value("nombre", std::string("El evento"));
                           const json sitio = (*r)["web"].datos.value("sitio", json::object());
                           const std::string acento =
                               a.value("acento", sitio.value("color", std::string("#ff4d2e")));
                           const std::string fecha = fechaCorta(g.value("fecha", std::string()));
                           const json esc = (*r)["esc"].datos;
                           const auto escenas = nombresDeEscenas(esc);

                           std::vector<Paso> pasos;
                           // --- las escenas y sus fuentes (lo que falta) ---
                           const auto escena = [&](const char* e) {
                               if (!escenas.count(e)) pasos.push_back({std::string("crear la escena «") + e + "»", "scene_create", {{"name", e}}});
                           };
                           escena(kEscAjuste);
                           escena(kEscAntes);
                           escena(kEscFin);
                           const auto fuente = [&](const char* e, const char* f, const json& args) {
                               if (fuentesDe(esc, e).count(f)) return;
                               json x = args;
                               x["scene"] = e;
                               x["name"] = f;
                               pasos.push_back({std::string("agregar «") + f + "»", "crear_fuente", x});
                           };
                           // Las barras con la hora, por el camino de las entradas virtuales (fija
                           // el tamaño y la cadencia del lienzo: sin eso no engancha con su grafo).
                           if (!fuentesDe(esc, kEscAjuste).count(kFteBarras))
                               pasos.push_back({"crear las barras con la hora", "video_entrada_virtual",
                                                {{"nombre", kFteBarras}, {"preset", "barras-hora"}, {"escena", kEscAjuste}}});
                           // El tono, aparte y solo de audio (como un micrófono): 1 kHz a −20 dBFS
                           // (amplitud 0,1), el de alineación. Sigue a la escena: suena solo con la
                           // señal de ajuste al aire.
                           if (!fuentesDe(esc, kEscAjuste).count(kFteTono)) {
                               fuente(kEscAjuste, kFteTono,
                                      {{"type", "ba_gst_fuente"},
                                       {"settings",
                                        {{"pipeline", ""},
                                         {"pipeline_audio", "audiotestsrc wave=sine freq=1000 volume=0.1 is-live=true"},
                                         {"audio", true},
                                         {"normalizar", false},
                                         {"estudio", false},
                                         {"latencia", 0}}}});
                               pasos.push_back({"que el tono siga a la escena", "audio_afv", {{"fuente", kFteTono}, {"sigue_video", true}}, true});
                           }
                           // Las placas: cada fuente con SU gráfico y del tamaño del lienzo (si no, es
                           // un zócalo de 900×220 y lo que se dibuja ahí sale deformado).
                           const auto placa = [](const char* g) {
                               return json{{"type", "ba_grafico"}, {"layout", "full"}, {"settings", {{"grafico", g}, {"ancho", 1920}, {"alto", 1080}}}};
                           };
                           fuente(kEscAjuste, kFteIdent, placa("identificacion-ajuste"));
                           fuente(kEscAntes, kFteAntes, placa("antes-de-empezar"));
                           fuente(kEscFin, kFteFin, placa("fin-transmision"));

                           // --- los valores de las placas ---
                           const json vIdent{{"evento", nombre},
                                             {"detalle", (fecha.empty() ? std::string() : fecha + " · ") +
                                                             empiezaTexto(g, husoAparato, "Empieza")}};
                           const json vAntes{{"evento", nombre},
                                             {"empieza", empiezaTexto(g, husoAparato, "Empezamos")},
                                             {"bajada", a.value("bajada", fecha)},
                                             {"acento", acento}};
                           const json vFin{{"titulo", "Gracias por acompañarnos"},
                                           {"evento", nombre},
                                           {"bajada", a.value("cierre", std::string())},
                                           {"acento", acento}};

                           // --- el guion: Previa y Cierre si no están ---
                           const bool conGuion = a.value("guion", true);
                           const bool hayPrevia = indiceDeBloque(g, "Previa") >= 0;
                           const bool hayCierre = indiceDeBloque(g, "Cierre") >= 0;
                           const int nBloques = static_cast<int>(g.value("bloques", json::array()).size());
                           const std::string mEspera = a.value("musica_espera", std::string());
                           const std::string mCierre = a.value("musica_cierre", std::string());

                           enSerie(core, pasos, [&core, done, vIdent, vAntes, vFin, conGuion, hayPrevia, hayCierre, nBloques,
                                                 mEspera, mCierre, a](json hechos, std::string error) {
                               if (!error.empty())
                                   return done(ToolResult::error("Se frenó en: " + error + ". Lo anterior YA se hizo.").data(
                                       json{{"pasos", hechos}}));
                               // las fuentes de gráfico ya existen: sus ids, para dibujar las placas
                               llamar(core, "grafico_en_escena", json::object(), [&core, done, hechos, vIdent, vAntes, vFin, conGuion,
                                                                                  hayPrevia, hayCierre, nBloques, mEspera, mCierre,
                                                                                  a](Resp rg) mutable {
                                   std::map<std::string, long long> ids;
                                   const json lista = rg.datos.is_array() ? rg.datos : rg.datos.value("fuentes", json::array());
                                   for (const auto& f : lista)
                                       if (f.contains("id")) ids[f.value("fuente", std::string())] = f.value("id", 0LL);
                                   std::vector<Paso> p2;
                                   const auto dibujar = [&](const char* fte, const char* graf, const json& v) {
                                       if (!ids.count(fte)) return;
                                       p2.push_back({std::string("dibujar «") + fte + "»", "grafico_mostrar",
                                                     {{"fuente", ids[fte]}, {"nombre", graf}, {"parametros", v}, {"duracionMs", 1500}}});
                                   };
                                   dibujar(kFteIdent, "identificacion-ajuste", vIdent);
                                   dibujar(kFteAntes, "antes-de-empezar", vAntes);
                                   dibujar(kFteFin, "fin-transmision", vFin);
                                   if (conGuion && !hayPrevia) {
                                       // al final y después al principio (guion_bloque agrega al final)
                                       p2.push_back({"agregar el bloque «Previa»", "guion_bloque", {{"nombre", "Previa"}}});
                                       p2.push_back({"llevar «Previa» al principio", "guion_bloque", {{"indice", nBloques}, {"mover", 0}}});
                                       const auto cue = [&](json c) {
                                           c["bloque"] = 0;
                                           p2.push_back({"cue de la previa", "guion_cue", c});
                                       };
                                       cue({{"tipo", "escena"},
                                            {"datos", {{"escena", kEscAjuste}}},
                                            {"texto", "Señal de ajuste al aire. Prender la transmisión (15 a 30 minutos antes) y la grabación."}});
                                       cue({{"tipo", "espera"},
                                            {"texto", "¿Se ve y se oye la señal de ajuste en la plataforma (barras, hora que corre y tono)? Recién entonces, seguir."}});
                                       cue({{"tipo", "grafico"},
                                            {"datos", {{"fuente", kFteAntes}, {"grafico", "antes-de-empezar"}, {"valores", vAntes}}},
                                            {"texto", "Placa de espera con la hora de inicio."}});
                                       cue({{"tipo", "escena"},
                                            {"datos", {{"escena", kEscAntes}}},
                                            {"texto", "A la placa de espera (5 a 10 minutos antes)."}});
                                       if (!mEspera.empty())
                                           cue({{"tipo", "musica"}, {"datos", {{"archivo", mEspera}}}, {"texto", "Música de espera."}});
                                       cue({{"tipo", "espera"}, {"texto", "Es la hora: arranca el evento (siguiente bloque)."}});
                                   }
                                   if (conGuion && !hayCierre) {
                                       const int idx = nBloques + ((conGuion && !hayPrevia) ? 1 : 0);
                                       p2.push_back({"agregar el bloque «Cierre»", "guion_bloque", {{"nombre", "Cierre"}}});
                                       const auto cue = [&](json c) {
                                           c["bloque"] = idx;
                                           p2.push_back({"cue del cierre", "guion_cue", c});
                                       };
                                       if (!mCierre.empty())
                                           cue({{"tipo", "musica"}, {"datos", {{"archivo", mCierre}}}, {"texto", "Música de cierre."}});
                                       cue({{"tipo", "grafico"},
                                            {"datos", {{"fuente", kFteFin}, {"grafico", "fin-transmision"}, {"valores", vFin}}},
                                            {"texto", "Placa de fin."}});
                                       cue({{"tipo", "escena"}, {"datos", {{"escena", kEscFin}}}, {"texto", "A la placa de fin de transmisión."}});
                                       cue({{"tipo", "espera"},
                                            {"texto", "Cortar la transmisión y la grabación (stream_stop, record_stop) y terminar el evento (evento_terminar)."}});
                                   }
                                   if (conGuion && (!hayPrevia || !hayCierre))
                                       p2.push_back({"guardar el guion", "guion_guardar", json::object()});
                                   if (a.value("al_aire", false))
                                       p2.push_back({"señal de ajuste al aire", "scene_switch", {{"name", kEscAjuste}}});
                                   else
                                       p2.push_back({"señal de ajuste armada en el preview", "scene_preview", {{"name", kEscAjuste}}, true});
                                   enSerie(core, p2, [done, hechos](json h2, std::string error) {
                                       json todos = hechos;
                                       for (auto& x : h2) todos.push_back(x);
                                       if (!error.empty())
                                           return done(ToolResult::error("Se frenó en: " + error + ". Lo anterior YA se hizo.").data(
                                               json{{"pasos", todos}}));
                                       done(ToolResult::ok("Listo: señal de ajuste, placa de espera, placa de fin y los bloques "
                                                           "Previa y Cierre. Seguí con evento_verificar.")
                                                .data(json{{"pasos", todos}}));
                                   });
                               });
                           });
                       });
        };
        core_.addTool(std::move(t));
    }

    // ------------------------------------------------------------------ evento_verificar
    {
        Tool t;
        t.name = "evento_verificar";
        t.title = "¿Está todo para salir? (lista de control)";
        t.category = "evento";
        t.description =
            "La lista de control del evento abierto según su tipo (`tipo`: salon o asamblea; de fábrica, lo dice el "
            "guion): revisa sola lo que se puede revisar, dice qué falta y cómo se arregla, y anota en la lista de "
            "chequeo lo que solo puede confirmar una persona.";
        t.details =
            "Es el control del final de los procedimientos estándar (help procedimiento_salon / procedimiento_asamblea). "
            "Cada ítem trae `estado`: «ok», «falta» (así no se puede salir: arreglalo con lo que dice `como`), «aviso» "
            "(conviene mirarlo) o «a_mano» (solo lo confirma una persona: PEDÍSELO, y cuando te diga que sí, marcalo "
            "con evento_chequeo {item, hecho: true}).\n\n"
            "Revisa: el evento (fecha, hora, huso), el guion (evento_revisar: escenas, archivos, disco, entradas sin "
            "señal), las escenas de la previa y el cierre (evento_preparar), adónde se transmite y las cuentas, el disco "
            "para grabar, las entradas de audio, la página web del evento, la sala de invitados y las preguntas del "
            "público si el evento las usa, el padrón; y en una asamblea, además, el Cuerpo, el recinto, las cámaras y "
            "escenas de las bancas, la central (asamblea_revisar), el quórum, el orden del día y los votantes.\n\n"
            "No cambia nada del evento salvo agregar los ítems a mano que falten en su lista de chequeo (nunca marca "
            "uno como hecho). Corrélo después de armar, después de arreglar y antes de empezar.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"tipo", {{"type", "string"}, {"enum", {"salon", "asamblea"}}}}}}};
        t.run = [&core](const json& a, Done done) {
            leerVarias(core, {{"ev", {"guion_ver", json::object()}}}, [&core, a, done](std::shared_ptr<std::map<std::string, Resp>> r0) {
                const json ev = (*r0)["ev"].datos;
                if (!(*r0)["ev"].ok || ev.value("abierto", std::string()).empty())
                    return done(ToolResult::error("No hay un evento abierto (guion_abrir o evento_armar)."));
                const json g = ev.value("guion", json::object());
                const std::string tipo = a.value("tipo", std::string(g.value("asamblea", false) ? "asamblea" : "salon"));
                const bool asamblea = tipo == "asamblea";
                const bool invitados = g.value("invitados", false);
                const bool preguntas = g.value("preguntas", false);
                std::vector<std::pair<std::string, std::pair<std::string, json>>> pedidos{
                    {"rev", {"evento_revisar", json::object()}},
                    {"web", {"evento_web", json{{"accion", "ver"}}}},
                    {"dest", {"stream_destinos", json::object()}},
                    {"ctas", {"accounts_status", json::object()}},
                    {"disco", {"record_disco", json::object()}},
                    {"esc", {"scene_list", json::object()}},
                    {"aud", {"audio_sources", json::object()}},
                    {"pad", {"padron_ver", json::object()}},
                    {"chq", {"evento_chequeo_ver", json::object()}}};
                if (invitados) pedidos.push_back({"sala", {"room_status", json::object()}});
                if (preguntas) pedidos.push_back({"preg", {"preguntas_ver", json::object()}});
                if (asamblea) {
                    pedidos.push_back({"asam", {"asamblea_revisar", json::object()}});
                    pedidos.push_back({"quorum", {"quorum_presentes", json::object()}});
                    pedidos.push_back({"orden", {"orden_del_dia_ver", json::object()}});
                }
                leerVarias(core, pedidos, [&core, done, g, tipo, asamblea, invitados, preguntas](std::shared_ptr<std::map<std::string, Resp>> r) {
                    json items = json::array();
                    const auto item = [&](const char* area, const std::string& que, const char* estado, const std::string& detalle,
                                          const std::string& como) {
                        items.push_back({{"area", area}, {"item", que}, {"estado", estado}, {"detalle", detalle}, {"como", como}});
                    };
                    const auto no = [&](const char* clave) { return !(*r)[clave].ok; };

                    // --- el evento ---
                    if (g.value("fecha", std::string()).empty() || g.value("inicio", std::string()).empty())
                        item("evento", "fecha y hora", "falta", "El evento no tiene fecha u hora de inicio: la placa de espera y la página no pueden decir cuándo empieza.",
                             "guion_guardar o evento_armar con fecha, inicio y huso");
                    else
                        item("evento", "fecha y hora", "ok", g.value("fecha", std::string()) + " " + g.value("inicio", std::string()), "");
                    if (g.value("huso", std::string()).empty())
                        item("evento", "huso horario", "aviso", "Sin huso: el que se conecta de otro lado no sabe a qué hora es.", "guion_guardar {huso}");

                    // --- el guion ---
                    if (no("rev")) item("guion", "revisión", "aviso", "No se pudo revisar el guion: " + (*r)["rev"].texto, "evento_revisar");
                    else {
                        const json rev = (*r)["rev"].datos;
                        int graves = 0, leves = 0;
                        for (const auto& x : rev.value("reparos", json::array())) {
                            const bool grave = x.value("grave", false);
                            if (grave ? graves++ < 8 : leves++ < 8) {
                                // sin bloque (bloque < 0): es del aparato (disco, destino, entradas)
                                const int b = x.value("bloque", -1);
                                const bool aparato = b < 0;
                                item(aparato ? "aparato" : "guion",
                                     aparato ? std::string("revisión del aparato")
                                             : "bloque " + std::to_string(b + 1) + ", cue " + std::to_string(x.value("cue", 0) + 1),
                                     grave ? "falta" : "aviso", x.value("texto", std::string()),
                                     aparato ? "ver el detalle" : "guion_cue (arreglarlo) y evento_revisar");
                            }
                        }
                        if (rev.value("reparos", json::array()).empty()) item("guion", "revisión", "ok", "Sin reparos.", "");
                    }
                    const int nb = static_cast<int>(g.value("bloques", json::array()).size());
                    if (nb == 0) item("guion", "bloques", "falta", "El guion está vacío.", "evento_armar o guion_bloque");
                    if (indiceDeBloque(g, "Previa") < 0 || indiceDeBloque(g, "Cierre") < 0)
                        item("guion", "previa y cierre", "aviso", "Faltan los bloques «Previa» (señal de ajuste, espera) y/o «Cierre» (placa de fin).",
                             "evento_preparar");

                    // --- escenas del principio y del final ---
                    if (!no("esc")) {
                        const auto escenas = nombresDeEscenas((*r)["esc"].datos);
                        std::string faltan;
                        for (const char* e : {kEscAjuste, kEscAntes, kEscFin})
                            if (!escenas.count(e)) faltan += std::string(faltan.empty() ? "" : ", ") + "«" + e + "»";
                        if (faltan.empty()) item("escenas", "señal de ajuste, espera y fin", "ok", "Están las tres.", "");
                        else item("escenas", "señal de ajuste, espera y fin", "aviso", "Faltan " + faltan + ".", "evento_preparar");
                    }

                    // --- salidas: adónde se transmite y la grabación ---
                    if (!no("dest")) {
                        const json d = (*r)["dest"].datos.value("destinos", json::array());
                        if (d.empty())
                            item("salidas", "transmisión", "falta", "No hay ningún destino de transmisión configurado.",
                                 "stream_destino_agregar (RTMP/SRT) o conectar la cuenta de YouTube/Twitch/Facebook (account_setup_guide)");
                        else item("salidas", "transmisión", "ok", std::to_string(d.size()) + " destino(s).", "");
                    }
                    if (!no("ctas")) {
                        std::string conectadas;
                        for (const auto& [plat, c] : (*r)["ctas"].datos.items())
                            if (c.is_object() && c.value("loggedIn", false)) conectadas += (conectadas.empty() ? "" : ", ") + plat;
                        item("salidas", "cuentas", conectadas.empty() ? "aviso" : "ok",
                             conectadas.empty() ? "Ninguna cuenta de plataforma conectada (si se transmite por RTMP propio, no hace falta)." : "Conectadas: " + conectadas + ".",
                             conectadas.empty() ? "account_setup_guide" : "");
                    }
                    if (!no("disco")) {
                        const json dk = (*r)["disco"].datos.value("disco", json::object());
                        const double horas = dk.value("horas", 0.0);
                        const double dur = g.value("duracionSeg", 0) / 3600.0;
                        const double hace = std::max(1.0, dur * 1.5);
                        char det[160];
                        std::snprintf(det, sizeof det, "%.1f h libres al bitrate de grabación (el evento dura %.1f h).", horas, dur);
                        item("salidas", "disco para grabar", horas < hace ? "falta" : (horas < 2 * hace ? "aviso" : "ok"), det,
                             horas < 2 * hace ? "liberar espacio o bajar el bitrate (record_ajustes)" : "");
                    }

                    // --- audio ---
                    if (!no("aud")) {
                        const size_t n = (*r)["aud"].datos.value("fuentes", json::array()).size();
                        item("audio", "entradas de audio", n ? "ok" : "falta", n ? std::to_string(n) + " fuente(s) de audio." : "No hay ninguna entrada de audio.",
                             n ? "" : "audio_input_add (micrófonos de la sala)");
                    }

                    // --- la página web del evento ---
                    if (!no("web")) {
                        const json w = (*r)["web"].datos;
                        bool todas = w.value("hay", false);
                        for (const auto& p : w.value("paginas", json::array())) todas = todas && p.value("existe", false);
                        item("web", "página del evento", todas ? "ok" : "aviso",
                             todas ? "Generada: portada, una página por sección y cierre." : "La página del evento no está generada (o le falta alguna sección).",
                             todas ? "" : "evento_web {accion: generar}");
                        if (preguntas && !w.value("sitio", json::object()).value("participacion", false))
                            item("web", "preguntar y votar en la página", "falta",
                                 "El evento usa preguntas del público pero la página no muestra dónde participar.",
                                 "evento_web {accion: sitio, participacion: true}");
                    }

                    // --- invitados y preguntas (si el evento los usa) ---
                    if (invitados) {
                        if (no("sala")) item("sala", "sala de invitados", "falta", "La sala no responde: " + (*r)["sala"].texto, "network_status");
                        else
                            item("sala", "sala de invitados", (*r)["sala"].datos.value("pin", std::string()).empty() ? "falta" : "ok",
                                 (*r)["sala"].datos.value("pin", std::string()).empty() ? "La sala no tiene PIN." : "Lista (con PIN y enlace).",
                                 "room_pin / room_link");
                    }
                    if (preguntas)
                        item("publico", "preguntas del público", no("preg") ? "falta" : "ok",
                             no("preg") ? "No responde: " + (*r)["preg"].texto : "La página del público está andando.", no("preg") ? "network_status" : "");

                    // --- el padrón ---
                    if (!no("pad")) {
                        const json res = (*r)["pad"].datos.value("resumen", json::object());
                        if (asamblea)
                            item("padron", "votantes", res.value("conVoto", 0) > 0 ? "ok" : "falta",
                                 res.value("conVoto", 0) > 0 ? std::to_string(res.value("conVoto", 0)) + " con derecho a voto."
                                                             : "Nadie del padrón tiene derecho a voto: sin eso no hay quórum.",
                                 res.value("conVoto", 0) > 0 ? "" : "padron_anotar {vota: true} o evento_armar {gente}");
                        else
                            item("padron", "asistentes", res.value("total", 0) > 0 ? "ok" : "aviso",
                                 res.value("total", 0) > 0 ? std::to_string(res.value("total", 0)) + " anotados."
                                                           : "No hay asistentes anotados (si hay acreditación en la puerta, conviene cargarlos).",
                                 res.value("total", 0) > 0 ? "" : "padron_anotar");
                    }

                    // --- la asamblea ---
                    if (asamblea) {
                        if (!no("asam")) {
                            const json as = (*r)["asam"].datos;
                            for (const auto& f : as.value("faltas", json::array()))
                                item("asamblea", f.value("que", std::string()), "falta", f.value("detalle", std::string()), f.value("como", std::string()));
                            for (const auto& f : as.value("avisos", json::array()))
                                item("asamblea", f.value("que", std::string()), "aviso", f.value("detalle", std::string()), f.value("como", std::string()));
                            if (as.value("listo", false)) item("asamblea", "cuerpo, recinto, cámaras y escenas", "ok", "asamblea_revisar no encontró faltas.", "");
                        }
                        if (!no("quorum")) {
                            const json q = (*r)["quorum"].datos;
                            item("asamblea", "quórum", q.value("configurado", false) ? "ok" : "falta",
                                 q.value("configurado", false) ? "Configurado: necesita " + std::to_string(q.value("necesarios", 0)) + " de " +
                                                                     std::to_string(q.value("miembros", 0)) + "."
                                                               : "El quórum no está configurado (miembros y mayoría).",
                                 q.value("configurado", false) ? "" : "quorum_regla");
                        }
                        if (!no("orden")) {
                            const size_t n = (*r)["orden"].datos.value("puntos", json::array()).size();
                            item("asamblea", "orden del día", n ? "ok" : "falta", n ? std::to_string(n) + " punto(s)." : "No hay orden del día cargado.",
                                 n ? "" : "orden_del_dia_cargar");
                        }
                    }

                    // --- lo que solo confirma una persona ---
                    std::vector<std::pair<std::string, std::string>> aMano{
                        {"camaras", "Cámaras encuadradas, en foco y con buena luz (mirar cada una en el multiview)"},
                        {"sonido", "Prueba de sonido con cada micrófono de la sala"},
                        {"musica", "Escuchar por el monitor la música de espera y la de cierre (audio_escuchar), con volumen parejo"},
                        {"videos", "Ver en el preview cada video del guion con su audio (scene_preview)"},
                        {"graficos", "Zócalos y placas con los nombres y cargos bien escritos (grafico_probar)"},
                        {"web", "Abrir la página del evento en un celular (el QR de evento_web) y verla bien"},
                        {"grabacion_prueba", "Grabar un minuto de prueba y reproducirlo: imagen y sonido bien"},
                        {"transmision_prueba", "Prender la transmisión con la señal de ajuste y verla en la plataforma (barras, hora que corre, tono, que sea el evento correcto)"},
                        {"caida", "Plan si se cae internet (dónde avisar, grabación local andando)"}};
                    if (invitados) aMano.push_back({"invitados_prueba", "Probar un invitado de prueba (invitado_prueba): se ve, se oye y nos oye"});
                    if (invitados) aMano.push_back({"auriculares", "Los invitados remotos con auriculares (o mix-minus probado)"});
                    if (preguntas) aMano.push_back({"preguntas_prueba", "Mandar una pregunta de prueba desde un celular y moderarla (pregunta_moderar)"});
                    if (!asamblea) aMano.push_back({"acreditacion", "Acreditación en la puerta lista (padron_puerta): quién la toma y con qué"});
                    if (asamblea) {
                        aMano.push_back({"central", "Central de conferencia probada banca por banca: cada micrófono abre y se oye"});
                        aMano.push_back({"ptz", "Preset de cámara de cada banca revisado (recinto_apuntar): encuadra al integrante"});
                        aMano.push_back({"votacion_prueba", "Votación de prueba (central o banca virtual) y el resultado en pantalla"});
                        aMano.push_back({"remotos", "Integrantes remotos con su link y PIN entregados (sesion_credencial); uno probado"});
                        aMano.push_back({"presidencia", "Consola de la presidencia probada (sesion_consola)"});
                        aMano.push_back({"acta", "Quién toma el acta, y el registro andando (sesion_registro)"});
                    }
                    std::map<std::string, json> chequeo;
                    for (const auto& c : (*r)["chq"].datos.value("chequeo", json::array())) chequeo[c.value("item", std::string())] = c;
                    std::vector<Paso> registrar;
                    for (const auto& [id, texto] : aMano) {
                        const auto it = chequeo.find(id);
                        const bool hecho = it != chequeo.end() && it->second.value("hecho", false);
                        if (it == chequeo.end())
                            registrar.push_back({"anotar «" + id + "»", "evento_chequeo", {{"item", id}, {"hecho", false}, {"texto", texto}}, true});
                        item("a_confirmar", id, hecho ? "ok" : "a_mano",
                             hecho ? texto + " — confirmado" + (it->second.value("nota", std::string()).empty() ? "" : ": " + it->second.value("nota", std::string()))
                                   : texto,
                             hecho ? "" : "pedíselo a la persona; cuando confirme: evento_chequeo {item: \"" + id + "\", hecho: true}");
                    }
                    enSerie(core, registrar, [done, items, tipo](json, std::string) {
                        int faltas = 0, avisos = 0, aMano2 = 0;
                        for (const auto& x : items) {
                            const std::string e = x.value("estado", std::string());
                            faltas += e == "falta";
                            avisos += e == "aviso";
                            aMano2 += e == "a_mano";
                        }
                        std::string txt = std::string(tipo == "asamblea" ? "Asamblea" : "Evento en salón") + ": " +
                                          (faltas ? std::to_string(faltas) + " cosa(s) FALTAN (así no se puede salir)" : "no falta nada automático") + ", " +
                                          std::to_string(avisos) + " aviso(s) y " + std::to_string(aMano2) +
                                          " cosa(s) para que confirme una persona.";
                        done(ToolResult::ok(txt).data(json{{"tipo", tipo},
                                                           {"listo", faltas == 0 && aMano2 == 0},
                                                           {"faltan", faltas},
                                                           {"avisos", avisos},
                                                           {"a_confirmar", aMano2},
                                                           {"items", items}}));
                    });
                });
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
