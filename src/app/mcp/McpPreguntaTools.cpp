// ============================================================================
// src/app/mcp/McpPreguntaTools.cpp — las preguntas del publico
//
// Hay chat de sala y hay chat de YouTube, Twitch y Facebook. CHAT NO ES COLA DE
// PREGUNTAS: no se modera, no se elige, no se ordena, no hay «la que sigue» y
// no sale al aire con el nombre de quien pregunto. Esto es la cola.
//
// UNA SOLA COLA para el que esta en la butaca y para el que mira por YouTube.
// Dos colas convierten al publico remoto en publico de segunda, que es lo que
// hace que un evento hibrido se sienta como dos eventos mal pegados.
//
// Las preguntas viven en B (el gateway), porque llegan de telefonos y B es lo
// que los telefonos alcanzan. Estas herramientas son el lado de A: mirar la
// cola, elegir, sacar al aire y dar por respondida.
// ============================================================================
#include "sala/VotacionSala.hpp"
#include "asamblea/Votaciones.hpp"
#include "sala/PreguntasSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/PaginaPublico.hpp"
#include "mcp/McpPreguntaTools.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "arranque/AppRuntime.hpp"
#include "mcp/McpCore.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/PersonasViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Quorum.hpp"
#include "control/BusEventos.hpp"

namespace ba::app::mcp {
namespace {

using json = nlohmann::json;

} // namespace

void registerPreguntaTools(McpCore& core, StudioViewModel& vm, AppRuntime& rt) {
    core.addCategory("preguntas", "Preguntas del público",
                     "La cola de lo que pregunta la gente, del salón y de las plataformas, para "
                     "moderarla y sacarla al aire.");

    const auto salaDe = [&vm]() { return static_cast<ConferenceViewModel*>(vm.conference()); };

    /// Lo que contestó la sala, para el asistente (los errores dicen que mire la sala).
    const auto contestarSala = [](Done done, std::string texto) {
        return [done, texto](const std::string& err, const json& r) {
            if (!err.empty())
                return done(ToolResult::error("No pude hablar con la sala (gateway): " + err +
                                              ". Fijate `room_status`: si esta caida, las preguntas del publico no llegan a ningun lado."));
            ToolResult res;
            res.structured = r;
            res.text(texto.empty() ? r.dump(2, ' ', false, json::error_handler_t::replace) : texto);
            done(res);
        };
    };
    {
        Tool t;
        t.name = "preguntas_ver";
        t.title = "La cola de preguntas del publico";
        t.category = "preguntas";
        t.readOnly = true;
        t.description =
            "Todas las preguntas que mando el publico, en el orden en que conviene mirarlas, con el "
            "recuento y cual es la proxima.";
        t.details =
            "EL ORDEN ES EL DEL QUE MODERA: primero la que esta al aire, despues la cola en el orden "
            "en que se va a preguntar, despues las que todavia nadie miro con las mas votadas arriba, "
            "y al final las respondidas y las descartadas.\\n\\n"
            "El recorrido de una pregunta:\\n"
            "  nueva -> elegida -> al_aire -> respondida\\n"
            "    o bien: nueva -> descartada\\n\\n"
            "`elegida` es la cola de verdad: lo que el conductor va a preguntar, en orden. Tenerla "
            "aparte de `nueva` es lo que permite moderar antes en vez de leer en vivo lo primero que "
            "llego.\\n\\n"
            "`votos` sirve para ORDENAR, no para decidir: se cuentan por navegador, que alcanza para "
            "que no sume diez veces el que toca diez veces, no para frenar a alguien decidido.\\n\\n"
            "`deDonde` dice por donde entro (publico, sala, youtube, twitch, facebook). La cola es una "
            "sola para todos a proposito.\\n\\n"
            "El TEXTO LO ESCRIBE CUALQUIERA con el enlace: leelo antes de sacarlo al aire.";
        t.run = [&vm, contestarSala](const json&, Done done) { vm.preguntas().ver(contestarSala(done, {})); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pregunta_moderar";
        t.title = "Elegir, sacar al aire o descartar una pregunta";
        t.category = "preguntas";
        t.description =
            "Mueve una pregunta por la cola: elegirla para preguntarla, ponerla al aire, darla por "
            "respondida o descartarla.";
        t.details =
            "Las acciones:\\n"
            "- `elegir`: la manda AL FINAL de la cola de las que se van a preguntar. Es moderar.\\n"
            "- `des_elegir`: la saca de la cola y la devuelve a las nuevas.\\n"
            "- `al_aire`: es la que se esta preguntando AHORA. Hay una sola: poner otra da por "
            "terminada la anterior, porque si se saco otra es que con aquella ya se termino.\\n"
            "- `responder`: ya se contesto.\\n"
            "- `descartar`: no se va a preguntar. El que la mando lo ve; los demas no, porque "
            "enterarse de que a alguien se la bocharon no le sirve a nadie.\\n"
            "- `limpiar`: borra las respondidas y las descartadas, sin tocar las que esperan.\\n"
            "- `vaciar`: las borra TODAS. Es para empezar un evento nuevo, no para hacer lugar.\\n\\n"
            "`limpiar` y `vaciar` no llevan `id`; las demas si.\\n\\n"
            "Para que la pregunta se vea en pantalla hace falta ademas un grafico: `al_aire` la marca "
            "como la que se esta preguntando, no la dibuja.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"accion"}},
            {"properties",
             {{"accion",
               {{"type", "string"},
                {"enum",
                 {"elegir", "des_elegir", "al_aire", "responder", "descartar", "limpiar", "vaciar"}}}},
              {"id", {{"type", "string"}, {"description", "el id que da preguntas_ver"}}}}}};
        t.run = [&vm, contestarSala](const json& a, Done done) {
            const std::string e = vm.preguntas().moderar(a.value("accion", std::string()), a.value("id", std::string()),
                                                         contestarSala(done, "Hecho."));
            if (!e.empty()) done(ToolResult::error(e + " Los ids los da `preguntas_ver`."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "pregunta_sumar";
        t.title = "Meter en la cola una pregunta que llego por otro lado";
        t.category = "preguntas";
        t.description =
            "Agrega a la cola una pregunta que no vino de la pagina del publico: del chat de la sala, "
            "del de YouTube, o dicha a viva voz en el salon.";
        t.details =
            "Es lo que mantiene UNA SOLA COLA. El que pregunta por el chat de YouTube y el que levanta "
            "la mano en la sala tienen que terminar en la misma lista, moderada en el mismo lugar: si "
            "no, el publico remoto es publico de segunda.\\n\\n"
            "`deDonde` deja dicho por donde entro, para poder nombrarlo al aire («nos preguntan por "
            "YouTube...»).\\n\\n"
            "El nombre es opcional: mucha gente prefiere preguntar sin dar el nombre y eso esta bien.";
        t.inputSchema = {{"type", "object"},
                         {"required", {"texto"}},
                         {"properties",
                          {{"texto", {{"type", "string"}}},
                           {"nombre", {{"type", "string"}}},
                           {"deDonde",
                            {{"type", "string"},
                             {"enum", {"sala", "publico", "youtube", "twitch", "facebook"}},
                             {"description", "por donde llego"}}}}}};
        t.run = [&vm, contestarSala](const json& a, Done done) {
            const std::string e = vm.preguntas().sumar(a.value("texto", std::string()), a.value("nombre", std::string()),
                                                       a.value("deDonde", std::string("sala")),
                                                       contestarSala(done, "Sumada a la cola."));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core.addTool(std::move(t));
    }
    // --- Quorum, mayorias y resultado oficial -----------------------------------------
    {
        Tool t;
        t.name = "quorum_regla";
        t.title = "Reglas de quorum y mayoria";
        t.category = "preguntas";
        t.description = "Las reglas del cuerpo: `miembros` (cuantos lo integran), `minimo` para sesionar "
                        "(\"mitad_mas_uno\" o un numero) y la `mayoria` de fabrica para aprobar (simple, absoluta, "
                        "dos_tercios_presentes, dos_tercios_miembros). Sin argumentos, las muestra.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"miembros", {{"type", "integer"}, {"minimum", 0}}},
                           {"minimo", {{"description", "\"mitad_mas_uno\" o un numero"}}},
                           {"mayoria", {{"type", "string"}, {"enum", {"simple", "absoluta", "dos_tercios_presentes", "dos_tercios_miembros"}}}}}}};
        t.run = [&vm](const json& a, Done done) {
            if (!a.empty())
                if (const std::string e = vm.quorum().setRegla(a); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(vm.quorum().regla()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "quorum_presentes";
        t.title = "Hay quorum?";
        t.category = "preguntas";
        t.readOnly = true;
        t.description = "Si hay QUÓRUM ahora: cuántos presentes con voto hay y cuántos hacen falta; no es una votación.";
        t.run = [&vm](const json&, Done done) { done(ToolResult().data(vm.quorum().estado())); };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "votacion_resultado_oficial";
        t.title = "Cargar el resultado oficial de una votacion";
        t.category = "preguntas";
        t.description = "El resultado OFICIAL (el del tablero del recinto o el que lee Secretaria), que es el que va al "
                        "acta: `asunto`, `afirmativos`, `negativos`, `abstenciones` (o `csv` nominal «nombre;voto» por "
                        "linea), `mayoria` (si no, la de quorum_regla) y `punto` del orden del dia. Calcula si se aprobo "
                        "y lo publica en el bus (sesion/votacion). La urna del publico es otra cosa (consulta).";
        t.inputSchema = {{"type", "object"},
                         {"required", {"asunto"}},
                         {"properties",
                          {{"asunto", {{"type", "string"}}},
                           {"afirmativos", {{"type", "integer"}, {"minimum", 0}}},
                           {"negativos", {{"type", "integer"}, {"minimum", 0}}},
                           {"abstenciones", {{"type", "integer"}, {"minimum", 0}}},
                           {"presentes", {{"type", "integer"}, {"minimum", 0}}},
                           {"csv", {{"type", "string"}}},
                           {"mayoria", {{"type", "string"}, {"enum", {"simple", "absoluta", "dos_tercios_presentes", "dos_tercios_miembros"}}}},
                           {"punto", {{"type", "integer"}, {"minimum", 1}}}}}};
        t.run = [&vm](const json& a, Done done) {
            // Por el modelo de votaciones: a Quórum y al bus, como la central y los celulares.
            if (vm.votaciones() == nullptr) return done(ToolResult::error("Las votaciones de la asamblea no están disponibles."));
            const json r = vm.votaciones()->cargarOficial(a);
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().text(std::string(r.value("aprobado", false) ? "APROBADO" : "RECHAZADO") + ": " +
                                   std::to_string(r.value("afirmativos", 0)) + " a " + std::to_string(r.value("negativos", 0)))
                     .data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "chat_a_preguntas";
        t.title = "Sumar un mensaje del chat a las preguntas";
        t.category = "preguntas";
        t.description = "Pasa un mensaje del chat del público a la cola de preguntas, por su key.";
        t.inputSchema = {{"type", "object"}, {"required", {"mensaje_id"}}, {"properties", {{"mensaje_id", {{"type", "string"}}}}}};
        t.run = [&vm, contestarSala](const json& a, Done done) {
            // Por el modelo, esperando lo que diga la sala (antes contestaba «ok» antes de saberlo).
            const std::string e = vm.preguntas().sumarDesdeChat(a.value("mensaje_id", std::string()),
                                                                contestarSala(done, "Sumada a la cola de preguntas."));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core.addTool(std::move(t));
    }
    // --- La urna --------------------------------------------------------------
    {
        Tool t;
        t.name = "votacion_abrir";
        t.title = "Abrir una votacion o una encuesta";
        t.category = "preguntas";
        t.description = "Pone una pregunta a votar en la pagina del publico, con sus opciones.";
        t.details = "SON DOS COSAS DISTINTAS Y NO SE MEZCLAN:\n\n"
                    "- `modo: encuesta` — vota cualquiera que tenga el enlace, una vez por telefono. Es "
                    "enganche («de que tema quieren que hablemos») y sus numeros son INDICATIVOS. No sirve "
                    "para decidir nada.\n"
                    "- `modo: formal` — vota quien esta en el PADRON con derecho a voto y ya llego, una vez "
                    "cada uno, identificandose con su codigo. Es lo que hace que lo que se decide en una "
                    "asamblea valga.\n\n"
                    "En la formal los habilitados salen solos del padron del evento abierto: los que tienen "
                    "`vota` y estado `llego`. Si no hay ninguno, no se abre y se dice por que — abrir una "
                    "votacion formal donde nadie puede votar es peor que no abrirla, porque el resultado "
                    "parece valido.\n\n"
                    "EL DENOMINADOR IMPORTA. Se manda cuantos tenian derecho segun el padron, asi el "
                    "resultado dice «votaron 12 de 40» y no solo «12»: sin eso, dos a favor y cero en contra "
                    "parece unanime cuando en realidad no vino nadie.\n\n"
                    "`secreta` no quiere decir anonima: se sigue sabiendo QUIEN voto —hace falta para probar "
                    "el quorum— y se deja de guardar QUE voto.\n\n"
                    "`enVivo` muestra el resultado mientras esta abierta. En una formal conviene que NO: ver "
                    "que va perdiendo cambia lo que vota el que todavia no voto.\n\n"
                    "Hay UNA por vez: abrir otra reemplaza a la anterior y no arrastra sus votos.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"pregunta", "opciones"}},
            {"properties",
             {{"pregunta", {{"type", "string"}}},
              {"opciones",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "de dos a diez; con una sola no hay nada que decidir"}}},
              {"modo", {{"type", "string"}, {"enum", {"encuesta", "formal"}}}},
              {"secreta", {{"type", "boolean"}, {"description", "no guardar que voto cada uno"}}},
              {"enVivo",
               {{"type", "boolean"}, {"description", "mostrar el resultado mientras esta abierta"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            // La operación es del modelo (reglas y padrón), la misma que usa la GUI.
            std::vector<std::string> opciones;
            if (a.contains("opciones") && a["opciones"].is_array())
                for (const json& o : a["opciones"])
                    if (o.is_string()) opciones.push_back(o.get<std::string>());
            const std::string e = vm.votacion().abrir(
                a.value("pregunta", std::string()), opciones, a.value("modo", std::string("encuesta")),
                a.value("secreta", false), a.value("enVivo", false), [done](const std::string& err, const json& r) {
                    if (!err.empty()) return done(ToolResult::error("La sala respondio: " + err));
                    ToolResult res;
                    res.structured = r;
                    res.text("Votacion abierta.");
                    done(res);
                });
            if (!e.empty()) done(ToolResult::error(e));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "votacion_recuento";
        t.title = "Como va la votacion, y cerrarla";
        t.category = "preguntas";
        t.description =
             "El recuento de la VOTACIÓN o encuesta: cuántos sí y cuántos no, quiénes votaron y quiénes faltan; no "
            "es el quórum. Con `cerrar` la da por terminada.";
        t.details = "`votaron` y `podian` juntos son el QUORUM: «votaron 12 de 40». En una formal viene "
                    "ademas `quienes` (con que voto cada uno, salvo que sea secreta) y `faltanVotar`, que es "
                    "con lo que se sale a buscar a los que faltan antes de cerrar.\n\n"
                    "`cerrar` deja el resultado firme y lo muestra a todos los telefonos. Los votos NO se "
                    "borran al cerrar: el resultado se sigue mirando despues y es lo que va al acta.\n\n"
                    "`enVivo` prende o apaga que el resultado se vea mientras esta abierta, por si se abrio "
                    "con la decision equivocada.\n\n"
                    "Con `historial` vienen ademas TODAS las que se cerraron en este evento, que es lo que "
                    "va al acta: una asamblea vota varios puntos y el acta los lleva a todos, no al ultimo.";
        t.readOnly = false;
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"cerrar", {{"type", "boolean"}}},
              {"historial",
               {{"type", "boolean"}, {"description", "ademas, TODAS las que se cerraron en este evento"}}},
              {"enVivo", {{"type", "boolean"}, {"description", "mostrar el resultado en vivo"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            const auto contestar = [done](const std::string& texto) {
                return [done, texto](const std::string& err, const json& r) {
                    if (!err.empty())
                        return done(ToolResult::error("No pude hablar con la sala (gateway): " + err +
                                                      ". Fijate `room_status`."));
                    ToolResult res;
                    res.structured = r;
                    res.text(texto.empty() ? r.dump(2, ' ', false, json::error_handler_t::replace) : texto);
                    done(res);
                };
            };
            auto& v = vm.votacion();
            if (a.value("cerrar", false)) return v.cerrar(contestar("Votacion cerrada."));
            if (a.contains("enVivo")) return v.setEnVivo(a["enVivo"].get<bool>(), contestar("Hecho."));
            v.ver(a.value("historial", false), contestar({}));
        };
        core.addTool(std::move(t));
    }
    // --- La prueba tecnica previa ---------------------------------------------
    {
        Tool t;
        t.name = "invitado_chequeo";
        t.title = "Si los que esperan estan listos para salir al aire";
        t.category = "preguntas";
        t.readOnly = true;
        t.description = "Para cada persona que espera en el lobby: si su camara y su microfono sirven, y que "
                        "decirle cuando no.";
        t.details = "LA CAUSA NUMERO UNO DE QUE UN EVENTO HIBRIDO SE CAIGA es el orador que prueba el "
                    "microfono cuando le dan la palabra. Esto es para correrlo ANTES: el invitado entra al "
                    "lobby cuando quiere y se le dice que arreglar mientras hay tiempo.\n\n"
                    "Junta en una sola respuesta lo que hoy hay que ir a buscar a tres lados: la calidad de "
                    "la cara (que tambien decide si el reconocimiento va a funcionar), el nivel de audio de "
                    "su canal y si esta silenciado.\n\n"
                    "`listo` es la respuesta corta. `quedecirle` es la frase para mandarle por "
                    "`participant_talk`, escrita para que la lea alguien que no sabe de audio: «acercate un "
                    "poco» y no «estas a -42 dBFS».\n\n"
                    "NO ARREGLA NADA NI LE HABLA A NADIE: informa. Mandarle el mensaje es una decision del "
                    "que opera, porque el invitado puede estar hablando con otro o todavia no saber que lo "
                    "estan mirando.\n\n"
                    "El audio solo se puede leer si esa persona ya tiene su canal andando; si todavia no "
                    "mando nada, se dice que no se sabe en vez de darlo por bueno.";
        t.run = [&vm, &rt](const json&, Done done) {
            auto* pv = static_cast<PersonasViewModel*>(vm.personas());
            if (pv == nullptr)
                return done(ToolResult::error("El reconocimiento no esta disponible en esta sesion."));
            auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
            if (conf == nullptr) return done(ToolResult::error("La sala no esta disponible."));

            const QVariantMap lobby = pv->enLobby();
            json gente = json::array();
            for (auto it = lobby.begin(); it != lobby.end(); ++it) {
                const QVariantMap p = it.value().toMap();
                const bool caraOk = p.value("sirve").toBool();
                const QString sugerencia = p.value("sugerencia").toString();

                // El canal de audio del invitado, si ya existe. Se busca por
                // el numero de lugar, que es como se llama la fuente.
                std::string canal;
                for (const std::string& nombre : rt.engine().audioSources())
                    if (nombre.find(it.key().toStdString()) != std::string::npos) {
                        canal = nombre;
                        break;
                    }
                const bool hayAudio = !canal.empty();
                const double pico = hayAudio ? rt.engine().sourcePeakDb(canal) : -100.0;
                const bool silenciado = hayAudio && rt.engine().sourceMuted(canal);
                // -45 dBFS es el piso: por debajo de eso, en una sala con
                // ruido, la voz se pierde contra el ambiente.
                const bool audioOk = hayAudio && !silenciado && pico > -45.0;

                std::string decirle;
                if (!caraOk && !sugerencia.isEmpty()) decirle = sugerencia.toStdString();
                if (!hayAudio) {
                    if (!decirle.empty()) decirle += " ";
                    decirle += "Todavia no te escucho: fijate que el navegador tenga permiso para usar el "
                               "microfono.";
                } else if (silenciado) {
                    if (!decirle.empty()) decirle += " ";
                    decirle += "Tenes el microfono silenciado.";
                } else if (!audioOk) {
                    if (!decirle.empty()) decirle += " ";
                    decirle += "Se te escucha muy bajito: acercate un poco al microfono o subile el volumen.";
                }

                gente.push_back(json{{"uid", it.key().toStdString()},
                                     {"nombre", p.value("nombre").toString().toStdString()},
                                     {"listo", caraOk && audioOk},
                                     {"camara", caraOk ? "bien" : "no sirve todavia"},
                                     {"audio", !hayAudio    ? "no se sabe"
                                               : silenciado ? "silenciado"
                                               : audioOk    ? "bien"
                                                            : "muy bajo"},
                                     {"picoDb", hayAudio ? pico : -100.0},
                                     {"quedecirle", decirle}});
            }
            ToolResult r;
            if (gente.empty()) r.text("No hay nadie esperando en el lobby.");
            r.structured = json{{"gente", gente}};
            done(r);
        };
        core.addTool(std::move(t));
    }

    // --- El acta ---------------------------------------------------------------
    {
        Tool t;
        t.name = "evento_acta";
        t.title = "El acta del evento: quienes estuvieron y que se decidio";
        t.category = "preguntas";
        t.description = "Arma el acta con el padron de presentes, el quorum y todas las votaciones que se "
                        "cerraron, y la guarda en la carpeta del evento.";
        t.details =
            "NO ES `evento_informe`, Y NO SE PARECEN. El informe dice que el bloque 3 se fue cuatro "
            "minutos: es para el operador. El acta dice QUIENES ASISTIERON, QUE SE VOTO y COMO SALIO: "
            "es para el que organizo, y en una asamblea es el documento que vale.\n\n"
            "Sale de tres lados que ya existen: el padron (quienes llegaron, por donde y cuantos "
            "tenian derecho a voto), el historial de la urna (cada punto votado con su resultado) y "
            "la fecha del evento.\n\n"
            "EL QUORUM VA ARRIBA Y CON SUS DOS NUMEROS: «12 de 40». Un acta que dice «se aprobo por "
            "8 votos a favor» sin decir cuantos podian votar no prueba nada.\n\n"
            "Se escribe en la carpeta del evento como acta-<fecha>.md. Es texto plano a proposito: "
            "se abre en cualquier lado y se pega en un correo sin que se rompa nada.\n\n"
            "Con `soloVer` en true lo devuelve sin escribir el archivo.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"soloVer", {{"type", "boolean"}}}}}};
        t.run = [salaDe, &vm](const json& a, Done done) {
            auto* ev = static_cast<EventoViewModel*>(vm.evento());
            if (ev == nullptr || ev->abierto().isEmpty())
                return done(ToolResult::error(
                    "No hay evento abierto: el acta es de un evento. Abri uno con `guion_abrir`."));
            ConferenceViewModel* conf = salaDe();
            if (conf == nullptr) return done(ToolResult::error("La sala no esta disponible en esta sesion."));

            const bool soloVer = a.value("soloVer", false);
            // El historial de la urna vive en B: hay que ir a buscarlo antes de
            // poder escribir nada.
            // El historial de la urna, por el modelo de la votación (como la GUI).
            vm.votacion().ver(
                true, [ev, done, soloVer, &vm](const std::string& err, const json& rj) {
                    const QJsonObject r = QJsonDocument::fromJson(QByteArray::fromStdString(rj.dump(-1, ' ', false, json::error_handler_t::replace))).object();
                    const QJsonArray votaciones = err.empty() ? r.value("historial").toArray() : QJsonArray{};
                    const QString texto = ev->acta(votaciones);
                    QString archivo;
                    if (!soloVer) archivo = ev->guardarActa(texto);
                    ToolResult res;
                    res.text(texto.toStdString());
                    json out{{"hash", EventoViewModel::hashActa(texto).toStdString()},
                             {"acta", texto.toStdString()},
                             {"votaciones", votaciones.size()},
                             {"tiempos_palabra", vm.ordenDelDia().tiemposPalabra(vm.remotosPalabra())}};
                    if (!archivo.isEmpty()) out["archivo"] = archivo.toStdString();
                    // Si B no contesto, el acta sale igual PERO se dice: un acta a
                    // la que le faltan las votaciones y no lo aclara es peor que
                    // ninguna.
                    if (!err.empty())
                        out["aviso"] = "No pude hablar con la sala: el acta va SIN las votaciones. Fijate "
                                       "`room_status` y volve a pedirla.";
                    res.structured = out;
                    done(res);
                });
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "publico_pagina";
        t.title = "La pagina del publico: enlace, QR y como se ve";
        t.category = "preguntas";
        t.description =
            "El enlace con QR de la pagina donde el publico pregunta y vota, y el titulo y el fondo "
            "que esa pagina muestra.";
        t.details =
            "LA PAGINA DEL PUBLICO NO ES LA SALA. El que la abre no entra por WebRTC: no le pide "
            "camara ni microfono y no ocupa un lugar de invitado. Es a proposito — doscientas personas "
            "en el mismo salon entrando con microfono es un acople garantizado y el ancho de banda "
            "tirado—. Para que alguien entre a la sala de verdad, `room_link`.\\n\\n"
            "El QR va CON LA CLAVE ADENTRO: se proyecta en una pantalla o se imprime en el programa, y "
            "pedirle a doscientas personas que tipeen una clave es perder a la mitad. La clave es "
            "nueva en cada arranque de la sala.\\n\\n"
            "Con `titulo` y `cuando` se escribe lo que la pagina muestra arriba; con `fondo` (la ruta "
            "de una imagen del disco) se le pone el fondo del evento. La imagen se manda a la sala una "
            "vez y de ahi la toman todos los telefonos; sale atenuada detras del texto, porque es "
            "identidad visual y no decoracion: si compite con el texto, la pregunta no se lee.\\n\\n"
            "`fondo` en vacio lo saca.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"titulo", {{"type", "string"}, {"description", "lo que se lee arriba de todo"}}},
              {"cuando", {{"type", "string"}, {"description", "el renglon chico de abajo del titulo"}}},
              {"fondo", {{"type", "string"}, {"description", "ruta de una imagen del disco; vacio la saca"}}},
              {"remoto",
               {{"type", "boolean"},
                {"description", "el enlace por internet en vez del de la red local"}}}}}};
        t.run = [salaDe](const json& a, Done done) {
            ConferenceViewModel* conf = salaDe();
            if (conf == nullptr) return done(ToolResult::error("La sala no esta disponible en esta sesion."));
            // El enlace y el cambio son operaciones del modelo (sala::EnlacesSala,
            // sala::PaginaPublico), las mismas que usa la GUI (PublicoViewModel).
            sala::EnlacesSala* enlaces = &conf->enlaces();
            const bool remoto = a.value("remoto", false);
            const auto darEnlace = [enlaces, remoto](Done done) {
                enlaces->publico(remoto, [done](const sala::Enlace& e) {
                    if (!e.ok) return done(ToolResult::error(e.mensaje));
                    ToolResult res;
                    res.structured = e.respuesta;
                    // Sin texto, el asistente que solo lee `content` no veía nada (el
                    // enlace del QR). La matriz del QR no: es para dibujarlo.
                    json d = e.respuesta;
                    if (d.is_object()) d.erase("modules");
                    res.text(d.dump(2, ' ', false, json::error_handler_t::replace));
                    done(res);
                });
            };
            const auto texto = [&a](const char* k) {
                return a.contains(k) && a[k].is_string() ? std::optional<std::string>(a[k].get<std::string>())
                                                         : std::nullopt;
            };
            if (!a.contains("titulo") && !a.contains("cuando") && !a.contains("fondo")) return darEnlace(done);
            const std::string e = conf->paginaPublico().cambiar(
                texto("titulo"), texto("cuando"), texto("fondo"), [darEnlace, done](const std::string& err, const json&) {
                    if (!err.empty())
                        return done(ToolResult::error("No pude hablar con la sala (gateway): " + err +
                                                      ". Fijate `room_status`."));
                    // Despues de cambiarla, se devuelve el enlace: quien la
                    // configura casi siempre quiere el QR a continuacion.
                    darEnlace(done);
                });
            if (!e.empty()) done(ToolResult::error(e));
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
