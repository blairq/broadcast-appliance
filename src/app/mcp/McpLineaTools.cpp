// ============================================================================
// src/app/mcp/McpLineaTools.cpp — la línea de tiempo del evento, para el asistente
//
// El paso L3 del plan (docs/planes/PLAN-LINEA-DE-TIEMPO.md, 3.7). Lo que hace la
// persona en la conducción, pero para el asistente: ver el pronóstico compacto (sin
// leer el JSON entero), editar la línea en lote probando antes, revisarla, ajustarla
// a una hora, correrla (GO, HOLD, saltar, ir a, insertar, adlibs) y, tras un corte de
// luz, recibir lo que la persona cuenta y proponer cómo seguir.
//
// Las `guion_*` y `evento_correr` siguen andando igual: son la misma línea leída
// como guion de cues (EventoViewModel). Esto es la puerta directa a la línea.
//
// Las de vivo que cambian lo que sale (linea_go, linea_ir, linea_repetir,
// linea_insertar, linea_adlib, linea_soltar) están en la lista de aire (McpAire):
// pedidas por algo automático con el aire andando, las confirma una persona.
// ============================================================================
#include "i18n/I18n.hpp"
#include "mcp/McpEventoTools.hpp"

#include "mcp/McpCore.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app::mcp {
namespace {

using ba::i18n::llenar;
using ba::i18n::traducir;

json salidaEsquema() {
    return {{"type", "string"},
            {"description", traducir("mcp", "la salida (principal, sala, web-en…); sin esto, la por omisión")}};
}

/// Una hora para `desde`/`hasta`: "HH:MM" (hoy, en el huso del evento) o ms de época. 0 = sin límite; -1 = mal escrita.
int64_t horaDe(EventoViewModel& ev, const json& a, const char* clave) {
    if (!a.contains(clave) || a[clave].is_null()) return 0;
    if (a[clave].is_number_integer()) return a[clave].get<int64_t>();
    if (a[clave].is_string()) return ev.horaHoyMs(a[clave].get<std::string>());
    return -1;
}

/// Lo que devuelven las de vivo: el error, o el estado de la línea después.
void responderVivo(EventoViewModel& ev, const std::string& error, Done& done) {
    if (!error.empty()) return done(ToolResult::error(error));
    done(ToolResult().data(ev.lineaEstado()));
}

} // namespace

void registerLineaTools(McpCore& core, EventoViewModel& ev) {
    core.addCategory("linea", traducir("mcp", "Línea de tiempo del evento"),
                     traducir("mcp", "El plan del evento como línea de tiempo: pasos que entran con GO o solos (a una hora, "
                                     "tras otro), piezas adentro, pronóstico con back time y over/under, edición en lote, "
                                     "la corrida en vivo y la retoma tras un corte."));

    // --- Ver --------------------------------------------------------------------------
    {
        Tool t;
        t.name = "linea_ver";
        t.title = traducir("mcp", "Ver el pronóstico de la línea");
        t.category = "linea";
        t.readOnly = true;
        t.description = traducir("mcp", "El pronóstico compacto de una salida: bloques, pasos (y sus piezas con `nivel` piezas) "
                                        "con su hora de entrada y salida, back time, estado y cómo entran.");
        t.details = traducir(
            "mcp",
            "Es lo que hay que mirar antes de tocar la línea: no hace falta leer el JSON entero.\n\n"
            "Cada paso trae `estado` (pasado, alAire, proximo, futuro, salteado), `entra` (go = espera el GO; auto = "
            "solo; hora; cuando; ancla), `frente`/`fin` (hora prevista de entrada y de salida, o la real si ya pasó), "
            "`back` (lo último que puede arrancar para llegar al próximo hard start o al fin), `tarde`, `estimado` (depende "
            "de algo sin hora conocida), `previstoSeg` y `durSeg`. Cada bloque, su presupuesto y su over/under; y arriba, "
            "el over/under del evento contra el fin planeado.\n\n"
            "`nivel`: bloques | pasos (por defecto) | piezas (con el detalle de cada pieza: tipo, pista, datos, vida y "
            "cuándo entra y sale). `desde`/`hasta` (\"HH:MM\" de hoy o ms) dejan solo lo que se cruza con esa ventana.\n\n"
            "Las horas van en el huso del evento. Sin correr, es el plan desde la hora prevista (o desde ahora).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"salida", salidaEsquema()},
                           {"desde", {{"description", traducir("mcp", "\"HH:MM\" o ms de época")}}},
                           {"hasta", {{"description", traducir("mcp", "\"HH:MM\" o ms de época")}}},
                           {"nivel", {{"type", "string"}, {"enum", {"bloques", "pasos", "piezas"}}}}}}};
        t.examples = {R"({})", R"({"nivel": "piezas", "desde": "19:30", "hasta": "20:15"})", R"({"nivel": "bloques"})"};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const int64_t desde = horaDe(ev, a, "desde"), hasta = horaDe(ev, a, "hasta");
            if (desde < 0 || hasta < 0) return done(ToolResult::error(traducir("mcp", "`desde` y `hasta` van como \"HH:MM\" o en ms.")));
            const json v = ev.lineaVer(a.value("salida", std::string()), desde, hasta, a.value("nivel", std::string("pasos")));
            if (v.is_null()) return done(ToolResult::error(traducir("mcp", "No hay esa salida en la línea.")));
            done(ToolResult().data(v));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_estado";
        t.title = traducir("mcp", "Estado de la línea");
        t.category = "linea";
        t.readOnly = true;
        t.description = traducir("mcp", "Por salida: qué está al aire y cuánto lleva, el próximo y si entra solo, HOLD, "
                                        "over/under, el próximo hard start; la pausa y la retoma tras un corte.");
        t.details = traducir(
            "mcp",
            "`estado`: parada, corriendo, pausada, terminada. Por salida: `alAire` {paso, nombre, bloque, desde, llevaSeg, "
            "previstoSeg, restanteSeg (negativo: se pasó)}, `proximo` {paso, entra, automatico, frente, entraSoloEnSeg}, "
            "`hold`, `holdCorte`, `overUnderSeg` (positivo: vamos pasados), `proximoHardStart` {paso, hora, enSeg, gapSeg, "
            "politica} y `activas` (las piezas vivas).\n\n"
            "`retoma`: tras un corte de luz la línea vuelve en HOLD y `retoma.activa` es true; `pideDescripcion` dice si "
            "la persona todavía no contó qué está pasando, y `resumen` es lo que necesitás para proponer: el corte, lo que "
            "estaba al aire y cuánto se pasó, lo que según el plan iría ahora (`segunElPlan`), lo repuesto y lo perdido. "
            "Ver linea_retomar y linea_soltar.");
        t.run = [&ev](const json&, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            done(ToolResult().data(ev.lineaEstado()));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_recorrido";
        t.title = traducir("mcp", "Lo que pasó en la línea");
        t.category = "linea";
        t.readOnly = true;
        t.description = traducir("mcp", "Lo emitido (Eventos/<evento>/recorrido.jsonl): qué paso entró, cuándo y cómo, las "
                                        "piezas, los HOLD, los saltos, lo insertado, los cortes, lo que se contó y lo que se decidió.");
        t.details = traducir(
            "mcp",
            "Un renglón por cosa que pasó, en orden: `que` (arranca, entra, sale, pieza, repone, saca, perdida, hold, "
            "suelta, proximo, salta, ir, repite, inserta, pausa, sigue, corte, descripcion, decision, termina, fin), `ms` y "
            "`hora`, y `salida`, `paso`, `pieza`, `como` o `texto` según el caso. Es la materia prima del informe, los "
            "capítulos y el acta. Lo del ensayo no se anota. `ultimos`: solo los últimos N.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"ultimos", {{"type", "integer"}, {"minimum", 0}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            done(ToolResult().data(ev.lineaRecorrido(a.value("ultimos", 0))));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_revisar";
        t.title = traducir("mcp", "Revisar la línea");
        t.category = "linea";
        t.readOnly = true;
        t.description = traducir("mcp", "Los reparos propios de la línea: ids repetidos, pistas o salidas que no existen, piezas "
                                        "incompletas, escenas que faltan, anclas rotas, hard starts imposibles, cadenas "
                                        "automáticas largas y conflictos entre salidas (PTZ, gráficos).");
        t.details = traducir("mcp", "Cada reparo dice dónde (`bloque`, `paso`, `pieza`) y si es `grave`. Complementa a "
                                    "evento_revisar, que mira además los archivos y el aparato.");
        t.run = [&ev](const json&, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            done(ToolResult().data(ev.lineaRevisar()));
        };
        core.addTool(std::move(t));
    }

    // --- Editar -----------------------------------------------------------------------
    {
        Tool t;
        t.name = "linea_editar";
        t.title = traducir("mcp", "Editar la línea en lote");
        t.category = "linea";
        t.description = traducir("mcp", "Un lote de operaciones (agregar, mover, quitar, cambiar pasos, piezas, bloques, pistas y "
                                        "el estante) que se aplica entero o nada. Con `probar` devuelve el diff y el "
                                        "pronóstico resultante SIN aplicar.");
        t.details = traducir(
            "mcp",
            "Probá SIEMPRE primero con `probar`: true y mostrá el diff y el pronóstico; aplicalo (sin `probar`) cuando la "
            "persona lo confirme. Aplicado, la línea de antes queda en Respaldos/linea-<n>.json y se vuelve con "
            "linea_deshacer.\n\n"
            "Operaciones (`ops`, en orden):\n"
            "  {op:\"bloque_agregar\", bloque:{id, nombre, …}, despuesDe?}\n"
            "  {op:\"paso_agregar\", bloque, paso:{id, nombre, inicio, fin, continuar, piezas…}, despuesDe?}\n"
            "  {op:\"pieza_agregar\", paso, pieza:{id, pista, tipo, texto, datos, desde, dura, vida}, despuesDe?}\n"
            "  {op:\"pista_agregar\", pista:{id, tipo, salida}}   {op:\"estante_agregar\", item:{id, tecla, boton, pieza|paso}}\n"
            "  {op:\"mover\", id, bloque?|paso?, despuesDe?}   un bloque, un paso (a otro `bloque`) o una pieza (a otro `paso`)\n"
            "  {op:\"quitar\", id}\n"
            "  {op:\"cambiar\", id, campos:{…}}   las claves de `campos` reemplazan las del objeto (el id no se cambia)\n"
            "Sin `despuesDe` va al final; con \"\" al principio; con un id, después de ese.\n\n"
            "El modelo (claves en castellano): un paso entra según `inicio` ({modo: go | tras | con | hora | cuando | "
            "ancla}; hora: {hora:\"HH:MM\", politica: avisar | esperar | cortar}), termina según `fin` ({duracion:N} | "
            "{abierto:true} | {alTerminar:\"medio\"} | {mientras:C}) y `continuar` (no | sigue | con) dice si el "
            "siguiente entra solo; `preEsperaMs`/`postEsperaMs`, `transicion` {tipo, ms}, `etiquetas` (\"recortable\", "
            "\"opcional\": las usa linea_ajustar). Una pieza va en una `pista`, con `desde` ({ms:N} | {antesDelFin:N} | "
            "{manual:true} | {cuando:C}), `dura` ({ms:N} | {}) y `vida` (paso | bloque | hastaQueSeSaque | evento | "
            "alAvanzar). Los `tipo` de pieza que se ejecutan son los de los cues (escena, musica, video, teleprompter, "
            "orador, grafico, carrusel, pantalla, audio) y `asistente` ({herramienta, args}: se aprueba al escribirla).\n\n"
            "En vivo solo se toca lo que viene: lo que ya salió o está al aire no se cambia, no se mueve ni se quita.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"ops"}},
                         {"properties",
                          {{"ops", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                           {"probar", {{"type", "boolean"}, {"description", traducir("mcp", "mostrar el resultado sin aplicar")}}}}}};
        t.examples = {
            R"({"probar": true, "ops": [{"op": "paso_agregar", "bloque": "b2", "despuesDe": "c8", "paso": {"id": "video-inst", "nombre": "Video institucional", "fin": {"alTerminar": "medio"}, "piezas": [{"id": "v1", "pista": "medios-principal", "tipo": "video", "datos": {"archivo": "institucional.mp4"}}]}}]})",
            R"({"probar": true, "ops": [{"op": "cambiar", "id": "c8", "campos": {"inicio": {"modo": "hora", "hora": "20:30", "politica": "esperar"}}}]})",
            R"({"ops": [{"op": "quitar", "id": "c5"}]})"};
        t.run = [&ev](const json& a, Done done) {
            const json r = ev.lineaEditar(a.value("ops", json::array()), a.value("probar", false));
            if (!r.value("ok", false)) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_deshacer";
        t.title = traducir("mcp", "Deshacer el último cambio de la línea");
        t.category = "linea";
        t.description = traducir("mcp", "Vuelve la línea a como estaba antes del último linea_editar aplicado (la última "
                                        "revisión de Respaldos/).");
        t.run = [&ev](const json&, Done done) {
            const json r = ev.lineaDeshacer();
            if (!r.value("ok", false)) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_ajustar";
        t.title = traducir("mcp", "Llegar a horario");
        t.category = "linea";
        t.readOnly = true;
        t.description = traducir("mcp", "Para terminar a una hora: propone recortar los pasos que vienen con la etiqueta "
                                        "«recortable» y, si no alcanza, saltear los «opcional». Nunca aplica.");
        t.details = traducir(
            "mcp",
            "Devuelve {overSeg, recortaSeg, faltaSeg, ops, saltear, texto, pronostico}: `ops` es el lote para "
            "linea_editar (probalo y, si la persona confirma, aplicalo) y `saltear` los pasos opcionales que se podrían "
            "saltar (linea_saltar en vivo, o quitarlos). Cada paso recortable se acorta hasta la mitad, en proporción a lo "
            "que dura. Si no hay nada recortable, lo dice: ahí hay que decidir con la persona.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"hasta"}},
                         {"properties",
                          {{"hasta", {{"description", traducir("mcp", "la hora a la que hay que terminar: \"HH:MM\" o ms")}}},
                           {"salida", salidaEsquema()}}}};
        t.examples = {R"({"hasta": "21:00"})"};
        t.run = [&ev](const json& a, Done done) {
            const int64_t hasta = horaDe(ev, a, "hasta");
            if (hasta <= 0) return done(ToolResult::error(traducir("mcp", "`hasta` va como \"HH:MM\" o en ms.")));
            json r = ev.lineaAjustar(a.value("salida", std::string()), hasta);
            if (r.contains("ops") && !r["ops"].empty()) r["pronostico"] = ev.lineaEditar(r["ops"], true).value("pronostico", json());
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }

    // --- Vivo -------------------------------------------------------------------------
    // Las de vivo: todas devuelven el estado de la línea después (o por qué no).
    const auto vivo = [&core, &ev](const char* nombre, const std::string& titulo, const std::string& descripcion, json props,
                                   std::function<std::string(const json&)> hacer, std::vector<std::string> requeridos = {}) {
        Tool t;
        t.name = nombre;
        t.title = titulo;
        t.category = "linea";
        t.description = descripcion;
        props["salida"] = salidaEsquema();
        t.inputSchema = {{"type", "object"}, {"properties", props}};
        if (!requeridos.empty()) t.inputSchema["required"] = requeridos;
        t.run = [&ev, hacer](const json& a, Done done) { responderVivo(ev, hacer(a), done); };
        core.addTool(std::move(t));
    };
    const auto sal = [](const json& a) { return a.value("salida", std::string()); };

    vivo("linea_go", traducir("mcp", "GO: entra el próximo paso"),
         traducir("mcp", "Entra el próximo paso de la salida (con su escena por el PVW y TAKE). Sin correr, "
                         "arranca. Sin próximo, la salida termina. Tras un corte, además suelta el HOLD tal cual. "
                         "SALE AL AIRE."),
         json::object(), [&ev, sal](const json& a) { return ev.lineaGo(sal(a)); });
    vivo("linea_hold", traducir("mcp", "HOLD del paso al aire"),
         traducir("mcp", "Congela el avance automático del paso al aire (la cuenta sigue en «+» y el pronóstico "
                         "empuja). Con `hold` true/false lo fija; sin eso, alterna. Soltarlo tras un corte es "
                         "seguir como estaba."),
         json{{"hold", {{"type", "boolean"}}}},
         [&ev, sal](const json& a) {
             return ev.lineaHold(sal(a), a.contains("hold") && a["hold"].is_boolean() ? std::optional<bool>(a["hold"].get<bool>())
                                                                                       : std::nullopt);
         });
    vivo("linea_siguiente", traducir("mcp", "Mover el próximo para adelante"),
         traducir("mcp", "Mueve el próximo un paso para adelante (F10) sin tocar el aire; los del medio quedan "
                         "salteados. `pasos`: cuántos (1 por defecto)."),
         json{{"pasos", {{"type", "integer"}, {"minimum", 1}}}},
         [&ev, sal](const json& a) { return ev.lineaMoverProximo(sal(a), a.value("pasos", 1)); });
    vivo("linea_anterior", traducir("mcp", "Mover el próximo para atrás"),
         traducir("mcp", "Mueve el próximo un paso para atrás (F9) sin tocar el aire: sirve para volver a "
                         "pasar por algo. `pasos`: cuántos (1 por defecto)."),
         json{{"pasos", {{"type", "integer"}, {"minimum", 1}}}},
         [&ev, sal](const json& a) { return ev.lineaMoverProximo(sal(a), -a.value("pasos", 1)); });
    vivo("linea_saltar", traducir("mcp", "Saltear el próximo paso"),
         traducir("mcp", "Marca el próximo como salteado: no entra y el pronóstico corre lo que sigue."),
         json::object(), [&ev, sal](const json& a) { return ev.lineaSaltar(sal(a)); });
    vivo("linea_ir", traducir("mcp", "Ir a un paso"),
         traducir("mcp", "Entra ese paso ya, con el estado que tendría el aire ahí: repone la música y los "
                         "gráficos de vida larga que corresponden y saca los que no van (lo del asistente se "
                         "pregunta). SALE AL AIRE."),
         json{{"paso", {{"type", "string"}}}}, [&ev, sal](const json& a) { return ev.lineaIr(sal(a), a.value("paso", std::string())); },
         {"paso"});
    vivo("linea_repetir", traducir("mcp", "Repetir el paso al aire"),
         traducir("mcp", "Vuelve a entrar el paso al aire, con sus piezas. SALE AL AIRE."), json::object(),
         [&ev, sal](const json& a) { return ev.lineaRepetir(sal(a)); });
    vivo("linea_insertar", traducir("mcp", "Insertar un paso del estante"),
         traducir("mcp", "Un paso del estante (`item`, su id) va de próximo; con `alAire` entra ya. Lo planeado no "
                         "se toca: va una copia, anotada como insertada."),
         json{{"item", {{"type", "string"}}}, {"alAire", {{"type", "boolean"}}}},
         [&ev, sal](const json& a) { return ev.lineaInsertar(sal(a), a.value("item", std::string()), a.value("alAire", false)); },
         {"item"});
    vivo("linea_adlib", traducir("mcp", "Disparar una pieza a mano"),
         traducir("mcp", "Dispara ya una pieza: un adlib del paso al aire (o una que todavía no llegó a su "
                         "hora), o una pieza del estante (por el id del ítem o de su pieza). SALE AL AIRE."),
         json{{"pieza", {{"type", "string"}}}}, [&ev, sal](const json& a) { return ev.lineaAdlib(sal(a), a.value("pieza", std::string())); },
         {"pieza"});

    // --- Tras un corte de luz (3.8) ------------------------------------------------------
    {
        Tool t;
        t.name = "linea_retomar";
        t.title = traducir("mcp", "Contar qué está pasando tras un corte");
        t.category = "linea";
        t.description = traducir("mcp", "Tras un corte la línea vuelve en HOLD y pregunta «¿Qué está pasando?». Esto deja la "
                                        "descripción (la que la persona dictó o escribió) en el recorrido. No suelta nada.");
        t.details = traducir(
            "mcp",
            "El flujo: (1) linea_estado → `retoma` (activa, resumen); (2) la persona cuenta qué pasa («el orador sigue "
            "hablando», «ya votaron, vamos al cierre», «vamos diez minutos atrasados»): pasalo con linea_retomar "
            "{descripcion}; (3) PROPONÉ cómo adecuar la línea con linea_editar o linea_ajustar con probar:true "
            "(dónde reenganchar, qué saltear, qué reponer, qué recortar) y mostráselo; (4) cuando la persona confirme, "
            "aplicalo y soltá con linea_soltar {decision}. Si dice que siga como estaba: linea_soltar sin cambiar nada. "
            "Sin respuesta, la línea sigue en HOLD (el aire está como estaba antes del corte).\n\n"
            "`simular`: true hace como si la luz se hubiera cortado recién (con la línea corriendo): para probar el flujo "
            "sin apagar nada. Repone la música y los gráficos de vida larga como al volver de un corte de verdad.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"descripcion", {{"type", "string"}}},
                           {"simular", {{"type", "boolean"}, {"description", traducir("mcp", "simular un corte ahora (para probar)")}}}}}};
        t.examples = {R"({"descripcion": "El orador sigue hablando, vamos 5 minutos atrasados"})", R"({"simular": true})"};
        t.run = [&ev](const json& a, Done done) {
            responderVivo(ev, ev.lineaRetomar(a.value("descripcion", std::string()), a.value("simular", false)), done);
        };
        core.addTool(std::move(t));
    }
    vivo("linea_soltar", traducir("mcp", "Soltar el HOLD del corte"),
         traducir("mcp", "Suelta el HOLD en que volvió la línea tras un corte, con lo que se decidió (`decision`, "
                         "queda en el recorrido). Lo que se pasó de su hora entra en el acto. Solo cuando la "
                         "persona lo confirmó."),
         json{{"decision", {{"type", "string"}}}},
         [&ev](const json& a) { return ev.lineaSoltar(a.value("decision", std::string())); });
}

} // namespace ba::app::mcp
