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
// linea_insertar, linea_adlib, linea_soltar) se declaran de clase aire (en la llamada
// a `vivo`): pedidas por algo automático con el aire andando, las confirma una persona.
// ============================================================================
#include "i18n/I18n.hpp"
#include "mcp/McpEventoTools.hpp"

#include "evento/LineaEdicion.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpFoto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/LineaProductor.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app::mcp {
namespace {

using ba::i18n::llenar;
using ba::i18n::traducir;

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

/// La copia sin argumentos (la que se pide casi siempre), hecha ya al registrarla (recién
/// arrancada la app, la primera pregunta con la interfaz ocupada no tiene que esperar ni
/// fallar) y puesta al día por la línea cada vez que cambia lo que corre o el guion: así,
/// con la interfaz trabada, lo que se contesta es lo último que pasó (con su edad).
void sembrarCopia(EventoViewModel& ev, const std::shared_ptr<CopiaDeLaInterfaz>& copia) {
    copia->copiarYa();
    const auto alDia = [copia]() { copia->copiarYa(); };
    QObject::connect(&ev, &EventoViewModel::correChanged, &ev, alDia);
    QObject::connect(&ev, &EventoViewModel::guionChanged, &ev, alDia);
}

} // namespace

void registerLineaTools(McpCore& core, EventoViewModel& ev, LineaFuentes fuentes) {
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
        t.clase = Clase::Lectura;
        t.porSalida = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "El pronóstico compacto de una salida: bloques, pasos (y sus piezas con `nivel` piezas) "
                                        "con su hora de entrada y salida, back time, estado y cómo entran.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Es lo que hay que mirar antes de tocar la línea: no hace falta leer el JSON entero.\n\n"
            "Cada paso trae `estado` (pasado, alAire, proximo, futuro, salteado), `entra` (go = espera el GO; auto = "
            "solo; hora; cuando; ancla), `frente`/`fin` (hora prevista de entrada y de salida, o la real si ya pasó), "
            "`back` (lo último que puede arrancar para llegar al próximo hard start o al fin), `tarde`, `estimado` (depende "
            "de algo sin hora conocida), `previstoSeg` y `durSeg`. Cada bloque, su presupuesto y su over/under; y arriba, "
            "el over/under del evento contra el fin planeado.\n\n"
            "Para las superficies, cada paso trae además `escena` (la que pone) y `enSeg` (cuánto falta para que "
            "entre), y arriba va el `estante` (los adlibs: {id, boton, tecla, color, tipo, nombre}).\n\n"
            "`nivel`: bloques | pasos (por defecto) | piezas (con el detalle de cada pieza: tipo, pista, datos, vida y "
            "cuándo entra y sale). `desde`/`hasta` (\"HH:MM\" de hoy o ms) dejan solo lo que se cruza con esa ventana.\n\n"
            "Las horas van en el huso del evento. Sin correr, es el plan desde la hora prevista (o desde ahora).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"desde", {{"description", QT_TRANSLATE_NOOP("mcp", "\"HH:MM\" o ms de época")}}},
                           {"hasta", {{"description", QT_TRANSLATE_NOOP("mcp", "\"HH:MM\" o ms de época")}}},
                           {"nivel", {{"type", "string"}, {"enum", {"bloques", "pasos", "piezas"}}}}}}};
        t.examples = {R"({})", R"({"nivel": "piezas", "desde": "19:30", "hasta": "20:15"})", R"({"nivel": "bloques"})"};
        // El pronóstico lo arma la línea en la interfaz: se lee una copia (al día con ella
        // libre; trabada, la última de estos argumentos con su edad).
        const auto copia = copiarDeLaInterfaz(core, t, [&ev, &core](const json& a) -> LeidoEnLaInterfaz {
            if (ev.abierto().isEmpty()) return {{}, traducir("mcp", "No hay un evento abierto (guion_abrir).")};
            const int64_t desde = horaDe(ev, a, "desde"), hasta = horaDe(ev, a, "hasta");
            if (desde < 0 || hasta < 0) return {{}, traducir("mcp", "`desde` y `hasta` van como \"HH:MM\" o en ms.")};
            json v = ev.lineaVer(core.salidaDe(a), desde, hasta, a.value("nivel", std::string("pasos")));
            if (v.is_null()) return {{}, traducir("mcp", "No hay esa salida en la línea.")};
            return {std::move(v), {}};
        });
        sembrarCopia(ev, copia);
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_estado";
        t.title = traducir("mcp", "Estado de la línea");
        t.category = "linea";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Por salida: qué está al aire y cuánto lleva, el próximo y si entra solo, HOLD, "
                                        "over/under, el próximo hard start; la pausa y la retoma tras un corte.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`estado`: parada, corriendo, pausada, terminada. Por salida: `alAire` {paso, nombre, bloque, desde, llevaSeg, "
            "previstoSeg, restanteSeg (negativo: se pasó)}, `proximo` {paso, entra, automatico, frente, entraSoloEnSeg}, "
            "`hold`, `holdCorte`, `overUnderSeg` (positivo: vamos pasados), `proximoHardStart` {paso, hora, enSeg, gapSeg, "
            "politica} y `activas` (las piezas vivas).\n\n"
            "`retoma`: tras un corte de luz la línea vuelve en HOLD y `retoma.activa` es true; `pideDescripcion` dice si "
            "la persona todavía no contó qué está pasando, y `resumen` es lo que necesitás para proponer: el corte, lo que "
            "estaba al aire y cuánto se pasó, lo que según el plan iría ahora (`segunElPlan`), lo repuesto y lo perdido. "
            "Ver linea_retomar y linea_soltar.");
        // El estado de la corrida es de la interfaz (EventoViewModel): una copia.
        const auto copia = copiarDeLaInterfaz(core, t, [&ev](const json&) -> LeidoEnLaInterfaz {
            if (ev.abierto().isEmpty()) return {{}, traducir("mcp", "No hay un evento abierto (guion_abrir).")};
            return {ev.lineaEstado(), {}};
        });
        sembrarCopia(ev, copia);
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_recorrido";
        t.title = traducir("mcp", "Lo que pasó en la línea");
        t.category = "linea";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Lo emitido (Eventos/<evento>/recorrido.jsonl): qué paso entró, cuándo y cómo, las "
                                        "piezas, los HOLD, los saltos, lo insertado, los cortes, lo que se contó y lo que se decidió.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Un renglón por cosa que pasó, en orden: `que` (arranca, entra, sale, pieza, repone, saca, perdida, hold, "
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
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los reparos propios de la línea: ids repetidos, pistas o salidas que no existen, piezas "
                                        "incompletas, escenas que faltan, anclas rotas, hard starts imposibles, cadenas "
                                        "automáticas largas y conflictos entre salidas (PTZ, gráficos).");
        t.details = QT_TRANSLATE_NOOP("mcp", "Cada reparo dice dónde (`bloque`, `paso`, `pieza`) y si es `grave`. Complementa a "
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
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Un lote de operaciones (agregar, mover, quitar, cambiar pasos, piezas, bloques, pistas y "
                                        "el estante) que se aplica entero o nada. Con `probar` devuelve el diff y el "
                                        "pronóstico resultante SIN aplicar.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Probá SIEMPRE primero con `probar`: true y mostrá el diff y el pronóstico; aplicalo (sin `probar`) cuando la "
            "persona lo confirme. Aplicado, la línea de antes queda en Respaldos/linea-<n>.json y se vuelve con "
            "linea_deshacer.\n\n"
            "Operaciones (`ops`, en orden):\n"
            "  {op:\"bloque_agregar\", bloque:{id, nombre, …}, despuesDe?}\n"
            "  {op:\"paso_agregar\", bloque, paso:{id, nombre, inicio, fin, continuar, piezas…}, despuesDe?}\n"
            "  {op:\"pieza_agregar\", paso, pieza:{id, pista, tipo, texto, datos, desde, dura, vida}, despuesDe?}\n"
            "  {op:\"pista_agregar\", pista:{id, tipo, salida}}   {op:\"estante_agregar\", item:{id, tecla, boton, pieza|paso}}\n"
            "  {op:\"disparador_agregar\", disparador:{id, cuando, hacer, activo?, en?}}\n"
            "  {op:\"plantilla_agregar\", plantilla:{id, nombre, pasos}}   pasos-modelo con {campo} (linea_desde_plantilla)\n"
            "  {op:\"mover\", id, bloque?|paso?, despuesDe?}   un bloque, un paso (a otro `bloque`) o una pieza (a otro `paso`)\n"
            "  {op:\"quitar\", id}   también un disparador, un ítem del estante o una plantilla (si no la usa un bloque)\n"
            "  {op:\"cambiar\", id, campos:{…}}   las claves de `campos` reemplazan las del objeto (el id no se cambia)\n"
            "Sin `despuesDe` va al final; con \"\" al principio; con un id, después de ese.\n\n"
            "El modelo (claves en castellano): un paso entra según `inicio` ({modo: go | tras | con | hora | cuando | "
            "ancla}; hora: {hora:\"HH:MM\", politica: avisar | esperar | cortar}), termina según `fin` ({duracion:N} | "
            "{abierto:true} | {alTerminar:\"medio\"} | {mientras:C}) y `continuar` (no | sigue | con) dice si el "
            "siguiente entra solo; `preEsperaMs`/`postEsperaMs`, `transicion` {tipo, ms}, `etiquetas` (\"recortable\", "
            "\"opcional\": las usa linea_ajustar). Una pieza va en una `pista`, con `desde` ({ms:N} | {antesDelFin:N} | "
            "{manual:true} | {cuando:C}), `dura` ({ms:N} | {}) y `vida` (paso | bloque | hastaQueSeSaque | evento | "
            "alAvanzar). Los `tipo` de pieza que se ejecutan son los de los cues (escena, musica, video, teleprompter, "
            "orador, grafico, carrusel, pantalla, audio), `asistente` ({herramienta, args}: se aprueba al escribirla) y:\n"
            "  ptz       {camara, posicion, segundos?, siOcupada: esperar|mover|saltar}  si la cámara está al aire en "
            "alguna salida, de fábrica espera a que se libere (linea_ptz decide)\n"
            "  dsk       {capa: 1..4, accion: subir|bajar, confirmada?}  en las capas de la salida del paso; baja al "
            "terminar su vida\n"
            "  asamblea  {accion: tratar, punto} | {accion: siguiente} | {accion: punto, punto, estado, resultado?} | "
            "{accion: votacion, que: abrir|pausar|reanudar|cerrar|a_quorum, tema?, menu?, segundos?, resultado?}\n"
            "  playlist  {accion: play|next|prev|stop, indice?, autoNext?, loop?}  la playlist sigue siendo dueña del "
            "in/out y del auto-next; un paso con fin {alTerminar:\"medio\"} dura lo que ella\n\n"
            "Condiciones (`cuando`, `mientras`, `desde:{cuando}`): bus:<tema>[.campo] [== 'x'], medio:<pieza>.termino, "
            "hora>=19:30, pieza:<id>.fin + 2s y los pulsos tecla:F5, botonera:<id>, web:<botón> (ver linea_pulso), con "
            "y/o/no y paréntesis.\n\n"
            "Disparadores: {id, cuando, hacer:{accion, salida?, id?, alAire?}, activo?, en?}. `hacer.accion`: go | hold | "
            "soltar | saltar (de la salida; sin salida, la por omisión) | adlib (id: una pieza del paso al aire o del "
            "estante) | insertar (id: un paso del estante; alAire: true para que entre ya) | ir (id: un paso). Se hace "
            "cuando la condición PASA a cumplirse, con la línea corriendo. `activo` + `en` dicen cuándo vale: sin `en`, "
            "todo el evento; activo:\"paso\" + en:<paso>, mientras ese paso esté al aire; activo:\"bloque\" + "
            "en:<bloque>, mientras esté al aire un paso de ese bloque; activo:\"evento\" + en:<paso o bloque>, desde que "
            "entró hasta el final.\n\n"
            "En vivo solo se toca lo que viene: lo que ya salió o está al aire no se cambia, no se mueve ni se quita.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"ops"}},
                         {"properties",
                          {{"ops", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                           {"probar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "mostrar el resultado sin aplicar")}}}}}};
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
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Vuelve la línea a como estaba antes del último linea_editar aplicado (la última "
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
        t.clase = Clase::Lectura;
        t.porSalida = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "El solucionador del back-timing: para terminar a una hora (o al fin planeado) y "
                                        "llegar a cada hard start, propone opciones (recortar por prioridad, que un paso "
                                        "absorba, saltear lo opcional, mover una hora, relleno) ya probadas. Nunca aplica.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Devuelve {overSeg, recortaSeg, faltaSeg, ops, saltear, texto, opciones, hardStarts}. `opciones`: "
            "[{id, tipo, texto, ganaSeg, ops, resultado}] contra el fin; `hardStarts`: [{paso, gapSeg (negativo: no se "
            "llega; positivo: sobra), texto, opciones}] para cada hard start que viene desfasado. `tipo`: recortar | "
            "absorber | saltear | mover_hora | relleno | estirar. Cada opción viene PROBADA: `resultado` {fin, "
            "overUnderSeg, reparos} es cómo quedaría (linea_editar con probar). `ops` (arriba) es la opción "
            "«recortar», la primera que conviene mirar.\n\n"
            "Qué se recorta lo dicen las etiquetas de cada paso: «recortable» (hasta la mitad) o «recortable:N» (hasta "
            "el N %), «prioridad:N» (1 se recorta primero, 9 último; sin etiqueta, 5), «opcional» (se puede saltear), "
            "«fijo» (no se toca). Un paso abierto (un orador) se recorta en lo previsto: hay que avisarle.\n\n"
            "PROPONÉ, NO APLIQUES: mostrale a la persona las opciones con su texto y aplicá (linea_editar con las `ops`, "
            "sin probar) solo la que ella elija. En vivo, saltear es linea_saltar cuando el paso sea el próximo.\n\n"
            "`hasta`: la hora a la que hay que terminar (\"HH:MM\" o ms); sin eso, el fin planeado del evento.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"hasta", {{"description", QT_TRANSLATE_NOOP("mcp", "la hora a la que hay que terminar: \"HH:MM\" o ms "
                                                                      "(sin esto, el fin planeado)")}}}}}};
        t.examples = {R"({"hasta": "21:00"})", R"({})"};
        t.run = [&ev, &core](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const int64_t hasta = horaDe(ev, a, "hasta");
            if (hasta < 0) return done(ToolResult::error(traducir("mcp", "`hasta` va como \"HH:MM\" o en ms.")));
            done(ToolResult().data(LineaProductor::ajusteProbado(ev, core.salidaDe(a), hasta)));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_desde_plantilla";
        t.title = traducir("mcp", "Armar pasos desde una plantilla o una guía");
        t.category = "linea";
        t.clase = Clase::Escritura;
        t.porSalida = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Arma pasos de la línea desde una plantilla (orador, punto, votación o las del "
                                        "evento) repetida con datos (una lista de oradores, el orden del día), o la línea "
                                        "entera desde la guía de un escenario. Por defecto SOLO prueba.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Sin `plantilla` ni `escenario`: la lista de plantillas (las del evento y las de fábrica) con sus pasos y "
            "los campos que usan.\n\n"
            "`plantilla` + `datos`: una copia de los pasos de la plantilla por fila de `datos`, con cada {campo} "
            "reemplazado ({campo|defecto} si puede faltar). La de fábrica «orador» usa nombre, cargo, escena, "
            "participante, tiempoSeg; «punto» usa n, titulo, expediente, escena; «votacion», titulo. `tiempoSeg` de "
            "cada fila es lo que dura ese paso. Una pieza a la que le falta un dato no va (y se avisa en `avisos`). "
            "`origen`: \"orden-del-dia\" arma un bloque NUEVO con un paso por punto (orden_del_dia_ver) con la "
            "plantilla (por defecto «punto»); el bloque queda atado al orden del día y, si este cambia, se re-expande "
            "sin tocar lo que ya salió.\n\n"
            "`escenario` (el id de evento_escenarios): suma la línea de la plantilla de esa guía al final (bloques, "
            "pasos y piezas; los ids que choquen se renombran). Es para armar el evento desde la guía: leela antes con "
            "evento_escenario y preguntá lo que la guía dice que hay que preguntar.\n\n"
            "Dónde: `bloque` (uno que existe; sin esto, un bloque nuevo al final, con `nombreBloque`) y `despuesDe` "
            "(un paso del bloque; \"\" = al principio). `prefijo`: el de los ids nuevos.\n\n"
            "Con `probar` (por defecto true) devuelve el lote, el diff y el pronóstico SIN aplicar: mostráselo a la "
            "persona y, cuando confirme, llamá de nuevo con probar:false (o pasá `ops` a linea_editar).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"plantilla", {{"type", "string"}}},
              {"datos", {{"type", "array"}, {"items", {{"type", "object"}}}}},
              {"origen", {{"type", "string"}, {"enum", {"orden-del-dia"}}}},
              {"escenario", {{"type", "string"}}},
              {"bloque", {{"type", "string"}}},
              {"nombreBloque", {{"type", "string"}}},
              {"despuesDe", {{"type", "string"}}},
              {"prefijo", {{"type", "string"}}},
              {"probar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "mostrar el resultado sin aplicar (por defecto)")}}}}}};
        t.examples = {
            R"({})",
            R"({"plantilla": "orador", "nombreBloque": "Oradores", "datos": [{"nombre": "Ana Pérez", "cargo": "Presidenta", "escena": "Atril", "participante": "ana", "tiempoSeg": 300}, {"nombre": "Luis Gómez", "escena": "Atril", "tiempoSeg": 180}]})",
            R"({"origen": "orden-del-dia", "nombreBloque": "Orden del día"})",
            R"({"escenario": "panel"})"};
        t.run = [&ev, &core, fuentes](const json& a, Done done) {
            namespace L = ba::evento::linea;
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const std::string plantilla = a.value("plantilla", std::string()), escenario = a.value("escenario", std::string());
            if (plantilla.empty() && escenario.empty() && a.value("origen", std::string()).empty()) {
                json lista = json::array();
                const auto sumar = [&lista](const L::Plantilla& p, const char* de) {
                    json pasos = json::array();
                    for (const L::Paso& x : p.pasos) pasos.push_back({{"id", x.id}, {"nombre", x.nombre}, {"piezas", x.piezas.size()}});
                    lista.push_back({{"id", p.id}, {"nombre", p.nombre}, {"de", de}, {"pasos", pasos}});
                };
                for (const L::Plantilla& p : ev.linea().plantillas) sumar(p, "evento");
                for (const L::Plantilla& p : L::plantillasDeFabrica()) sumar(p, "fabrica");
                return done(ToolResult().data({{"plantillas", lista}}));
            }
            json armado;
            if (!escenario.empty()) {
                const json g = fuentes.eventoDeEscenario ? fuentes.eventoDeEscenario(escenario) : json();
                if (!g.is_object())
                    return done(ToolResult::error(llenar(traducir("mcp", "no hay una plantilla del escenario «%1» (evento_escenarios)"), {escenario})));
                armado = g.contains("linea") && g["linea"].is_object()
                             ? L::lineaDeGuia(ev.linea(), L::Linea::deJson(g["linea"]))
                             : L::lineaDeGuia(ev.linea(), ba::evento::Evento::deJson(g));
            } else if (a.value("origen", std::string()) == "orden-del-dia") {
                if (!fuentes.ordenDelDia) return done(ToolResult::error(traducir("mcp", "el orden del día no está disponible")));
                armado = L::bloqueDeOrdenDelDia(ev.linea(), plantilla.empty() ? "punto" : plantilla, fuentes.ordenDelDia(),
                                                a.value("nombreBloque", std::string()), core.salidaDe(a));
                if (!armado.value("ok", false)) return done(ToolResult::error(armado.value("error", std::string())));
            } else {
                L::DesdePlantilla d;
                d.plantilla = plantilla;
                d.datos = a.value("datos", json::array());
                d.bloque = a.value("bloque", std::string());
                d.nombreBloque = a.value("nombreBloque", std::string());
                d.alPrincipio = a.contains("despuesDe") && a["despuesDe"] == "";
                d.despuesDe = a.value("despuesDe", std::string());
                d.prefijo = a.value("prefijo", std::string());
                d.salida = core.salidaDe(a);
                armado = L::desdePlantilla(ev.linea(), d);
                if (!armado.value("ok", false)) return done(ToolResult::error(armado.value("error", std::string())));
            }
            json r = ev.lineaEditar(armado["ops"], a.value("probar", true));
            if (!r.value("ok", false)) return done(ToolResult::error(r.value("error", std::string())));
            r["ops"] = armado["ops"];
            r["avisos"] = armado["avisos"];
            if (armado.contains("pasos")) r["pasos"] = armado["pasos"];
            if (armado.contains("bloques")) r["bloques"] = armado["bloques"];
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_camaras";
        t.title = traducir("mcp", "Los próximos planos de cada cámara");
        t.category = "linea";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Para cada cámara (cada fuente que se ve en las escenas de la línea), los planos "
                                        "que la usan: el que está al aire y los que vienen, con cuánto falta, en todas las "
                                        "salidas. Es la pantalla del camarógrafo y del que maneja el PTZ.");
        t.details = QT_TRANSLATE_NOOP("mcp", "{camaras: {<fuente>: [{salida, paso, nombre, estado, frente, enSeg, escena, ptz?}]}}: "
                                    "lo que está al aire primero y después por lo que falta. `ptz` es la pieza del paso que "
                                    "mueve esa cámara. Con `camara`, solo esa.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"camara", {{"type", "string"}}}}}};
        t.run = [&ev, fuentes](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            json vistas = json::array();
            for (const std::string& s : ev.linea().salidasEfectivas()) {
                json v = ev.lineaVer(s, 0, 0, "pasos");
                if (!v.is_null()) vistas.push_back(std::move(v));
            }
            json c = ba::evento::linea::proximosPorCamara(ev.linea(), vistas, fuentes.camarasDeEscena);
            const std::string una = a.value("camara", std::string());
            if (!una.empty()) c = json{{una, c.value(una, json::array())}};
            done(ToolResult().data({{"camaras", c}}));
        };
        core.addTool(std::move(t));
    }

    // --- Vivo -------------------------------------------------------------------------
    // Las de vivo: todas devuelven el estado de la línea después (o por qué no).
    // La clase va declarada en cada llamada (F3-02): las que entran algo al aire, con su porqué.
    const auto vivo = [&core, &ev](const char* nombre, Clase clase, const char* porQueAire, const std::string& titulo,
                                   const std::string& descripcion, json props, std::function<std::string(const json&)> hacer,
                                   std::vector<std::string> requeridos = {}) {
        Tool t;
        t.name = nombre;
        t.title = titulo;
        t.category = "linea";
        t.clase = clase;
        t.porQueAire = porQueAire;
        t.porSalida = true;
        t.description = descripcion;
        t.inputSchema = {{"type", "object"}, {"properties", props}};
        if (!requeridos.empty()) t.inputSchema["required"] = requeridos;
        t.run = [&ev, hacer](const json& a, Done done) { responderVivo(ev, hacer(a), done); };
        core.addTool(std::move(t));
    };
    // La salida pedida o la de quien llama (el aparato, la botonera), ya validada por callTool.
    const auto sal = [&core](const json& a) { return core.salidaDe(a); };

    vivo("linea_go", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "entra el próximo paso de la línea de tiempo"),
         traducir("mcp", "GO: entra el próximo paso"),
         traducir("mcp", "Entra el próximo paso de la salida (con su escena por el PVW y TAKE). Sin correr, "
                         "arranca. Sin próximo, la salida termina. Tras un corte, además suelta el HOLD tal cual. "
                         "SALE AL AIRE."),
         json::object(), [&ev, sal](const json& a) { return ev.lineaGo(sal(a)); });
    vivo("linea_hold", Clase::Escritura, nullptr, traducir("mcp", "HOLD del paso al aire"),
         traducir("mcp", "Congela el avance automático del paso al aire (la cuenta sigue en «+» y el pronóstico "
                         "empuja). Con `hold` true/false lo fija; sin eso, alterna. Soltarlo tras un corte es "
                         "seguir como estaba."),
         json{{"hold", {{"type", "boolean"}}}},
         [&ev, sal](const json& a) {
             return ev.lineaHold(sal(a), a.contains("hold") && a["hold"].is_boolean() ? std::optional<bool>(a["hold"].get<bool>())
                                                                                       : std::nullopt);
         });
    vivo("linea_siguiente", Clase::Escritura, nullptr, traducir("mcp", "Mover el próximo para adelante"),
         traducir("mcp", "Mueve el próximo un paso para adelante (F10) sin tocar el aire; los del medio quedan "
                         "salteados. `pasos`: cuántos (1 por defecto)."),
         json{{"pasos", {{"type", "integer"}, {"minimum", 1}}}},
         [&ev, sal](const json& a) { return ev.lineaMoverProximo(sal(a), a.value("pasos", 1)); });
    vivo("linea_anterior", Clase::Escritura, nullptr, traducir("mcp", "Mover el próximo para atrás"),
         traducir("mcp", "Mueve el próximo un paso para atrás (F9) sin tocar el aire: sirve para volver a "
                         "pasar por algo. `pasos`: cuántos (1 por defecto)."),
         json{{"pasos", {{"type", "integer"}, {"minimum", 1}}}},
         [&ev, sal](const json& a) { return ev.lineaMoverProximo(sal(a), -a.value("pasos", 1)); });
    vivo("linea_saltar", Clase::Escritura, nullptr, traducir("mcp", "Saltear el próximo paso"),
         traducir("mcp", "Marca el próximo como salteado: no entra y el pronóstico corre lo que sigue."),
         json::object(), [&ev, sal](const json& a) { return ev.lineaSaltar(sal(a)); });
    vivo("linea_ir", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "entra otro paso de la línea de tiempo"),
         traducir("mcp", "Ir a un paso"),
         traducir("mcp", "Entra ese paso ya, con el estado que tendría el aire ahí: repone la música y los "
                         "gráficos de vida larga que corresponden y saca los que no van (lo del asistente se "
                         "pregunta). SALE AL AIRE."),
         json{{"paso", {{"type", "string"}}}}, [&ev, sal](const json& a) { return ev.lineaIr(sal(a), a.value("paso", std::string())); },
         {"paso"});
    vivo("linea_repetir", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "vuelve a entrar el paso al aire"),
         traducir("mcp", "Repetir el paso al aire"),
         traducir("mcp", "Vuelve a entrar el paso al aire, con sus piezas. SALE AL AIRE."), json::object(),
         [&ev, sal](const json& a) { return ev.lineaRepetir(sal(a)); });
    vivo("linea_insertar", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "mete un paso del estante en la línea (al aire, si se pide)"),
         traducir("mcp", "Insertar un paso del estante"),
         traducir("mcp", "Un paso del estante (`item`, su id) va de próximo; con `alAire` entra ya. Lo planeado no "
                         "se toca: va una copia, anotada como insertada."),
         json{{"item", {{"type", "string"}}}, {"alAire", {{"type", "boolean"}}}},
         [&ev, sal](const json& a) { return ev.lineaInsertar(sal(a), a.value("item", std::string()), a.value("alAire", false)); },
         {"item"});
    vivo("linea_adlib", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "dispara una pieza de la línea (escena, gráfico, música)"),
         traducir("mcp", "Disparar una pieza a mano"),
         traducir("mcp", "Dispara ya una pieza: un adlib del paso al aire (o una que todavía no llegó a su "
                         "hora), o una pieza del estante (por el id del ítem o de su pieza). SALE AL AIRE."),
         json{{"pieza", {{"type", "string"}}}}, [&ev, sal](const json& a) { return ev.lineaAdlib(sal(a), a.value("pieza", std::string())); },
         {"pieza"});

    vivo("linea_pulso", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "un pulso puede hacer entrar un paso (un disparador o un paso «cuando»)"),
         traducir("mcp", "Mandar un pulso a la línea"),
         traducir("mcp", "Un pulso para las condiciones de la línea: «tecla:F5», «botonera:<id>» o «web:<botón>». Vale solo en "
                         "ese instante: dispara los disparadores y los pasos o piezas «cuando» que lo esperan. Los que "
                         "espera la línea están en linea_ver (`pulsos`). Las teclas, la botonera y la web lo mandan solas."),
         json{{"clave", {{"type", "string"}}}}, [&ev](const json& a) { return ev.lineaPulso(a.value("clave", std::string())); },
         {"clave"});
    vivo("linea_reenganchar", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "vuelve a poner al aire la escena del paso"),
         traducir("mcp", "Reenganchar la salida a la línea"),
         traducir("mcp", "Alguien cambió la escena a mano fuera de la línea (la salida aparece `aMano` en linea_estado): "
                         "vuelve a la escena del paso al aire y la salida sigue con la línea. SALE AL AIRE."),
         json::object(), [&ev, sal](const json& a) { return ev.lineaReenganchar(sal(a)); });
    vivo("linea_ptz", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "puede mover una cámara que está al aire"),
         traducir("mcp", "Decidir una PTZ que espera"),
         traducir("mcp", "Una pieza PTZ espera porque su cámara está al aire en alguna salida (`ptzPendientes` en "
                         "linea_estado): «mover» la mueve igual, ya (SE VE AL AIRE); «esperar» la deja para cuando se "
                         "libere (lo de siempre); «saltar» no la mueve."),
         json{{"pieza", {{"type", "string"}}}, {"decision", {{"type", "string"}, {"enum", {"mover", "esperar", "saltar"}}}}},
         [&ev](const json& a) { return ev.lineaPtz(a.value("pieza", std::string()), a.value("decision", std::string())); },
         {"pieza", "decision"});

    // --- L10: la escaleta para afuera y desde afuera, y grabar la línea -------------------
    {
        Tool t;
        t.name = "linea_exportar";
        t.title = traducir("mcp", "Exportar la escaleta");
        t.category = "linea";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "La escaleta en CSV (un renglón por paso, para una planilla o Rundown Studio) o en MOS "
                                        "(roCreate, para un sistema de redacción). Con `archivo`, además la escribe ahí.");
        t.details = QT_TRANSLATE_NOOP("mcp", "CSV: bloque, id, nombre, entra (go | tras | con | hora 19:30 [esperar|cortar] | cuando <condición> | ancla "
            "<salida>/<paso>), hora (la prevista), duracion (m:ss), fin (vacío | abierto | medio | mientras <condición>), "
            "continuar, escena, guion, nota.\n\n"
            "MOS: un story por paso y un item por pieza; lo propio de la línea va en mosExternalMetadata, así vuelve igual "
            "con linea_importar.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"formato", {{"type", "string"}, {"enum", {"csv", "mos"}}}},
                           {"archivo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "ruta donde escribirla (opcional)")}}}}}};
        t.examples = {R"({"formato": "csv"})", R"({"formato": "mos", "archivo": "/tmp/escaleta.xml"})"};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const std::string formato = a.value("formato", std::string("csv"));
            const std::string archivo = a.value("archivo", std::string());
            if (!archivo.empty()) {
                const QString e = ev.exportarEscaleta(QString::fromStdString(formato), QString::fromStdString(archivo));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            done(ToolResult().data({{"formato", formato}, {"texto", ev.lineaExportar(formato)}, {"archivo", archivo}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_importar";
        t.title = traducir("mcp", "Importar una escaleta");
        t.category = "linea";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Trae una escaleta de afuera (CSV de una planilla o de Rundown Studio, o MOS de un "
                                        "sistema de redacción) como bloques nuevos al final de la línea. Por defecto SOLO prueba.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`texto` (el CSV o el XML) o `archivo` (.csv; .xml o .mos es MOS). `formato` se adivina si no se dice. Del CSV "
            "se leen las columnas de linea_exportar (también en inglés: segment, title, duration, script, notes); de MOS, "
            "roCreate o roStoryAppend/Insert/Replace (lo de otro sistema entra como pasos con notas).\n\n"
            "Devuelve el lote (`ops`), el diff, el pronóstico y los `avisos` (renglones que no se entendieron, ids "
            "renombrados). Mostráselo a la persona y, cuando confirme, llamá con probar:false (o pasá `ops` a "
            "linea_editar).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"texto", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}}},
                           {"formato", {{"type", "string"}, {"enum", {"csv", "mos"}}}},
                           {"nombreBloque", {{"type", "string"}}},
                           {"probar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "mostrar el resultado sin aplicar (por defecto)")}}}}}};
        t.examples = {R"({"texto": "bloque,nombre,duracion,escena\nApertura,Bienvenida,1:30,Cam 1\n"})",
                      R"({"archivo": "/Users/ana/Escaleta.csv", "probar": false})"};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const bool probar = a.value("probar", true);
            const std::string archivo = a.value("archivo", std::string());
            if (!archivo.empty()) {
                const QVariantMap r = ev.importarEscaleta(QString::fromStdString(archivo), probar);
                if (!r.value("ok").toBool()) return done(ToolResult::error(r.value("error").toString().toStdString()));
                return done(ToolResult().data(json::parse(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson().toStdString())));
            }
            const json r = ev.lineaImportar(a.value("formato", std::string()), a.value("texto", std::string()), probar,
                                            a.value("nombreBloque", std::string()));
            if (!r.value("ok", false)) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "linea_grabar";
        t.title = traducir("mcp", "Grabar los tiempos en un ensayo");
        t.category = "linea";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Grabar la línea (como ProPresenter): se corre un ensayo dando GO a mano y lo que dura "
                                        "cada paso queda como su duración prevista, cuando una persona lo confirma.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`accion`: grabar (prepara el próximo ensayo para grabar: todo entra con GO y nada se va solo; arrancalo con "
            "evento_correr) | no_grabar | ver (lo grabado: {pasos: [{paso, nombre, antesSeg, grabadoSeg}], totalAntesSeg, "
            "totalGrabadoSeg, ops}) | aplicar (lo grabado pasa a la línea, con su revisión para deshacer) | descartar.\n\n"
            "PROPONÉ, NO APLIQUES: mostrale a la persona lo que cambia y aplicá solo si confirma.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"accion"}},
                         {"properties", {{"accion", {{"type", "string"}, {"enum", {"grabar", "no_grabar", "ver", "aplicar", "descartar"}}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (ev.abierto().isEmpty()) return done(ToolResult::error(traducir("mcp", "No hay un evento abierto (guion_abrir).")));
            const std::string accion = a.value("accion", std::string("ver"));
            if (accion == "grabar" || accion == "no_grabar") {
                const std::string e = ev.lineaEstado().value("estado", std::string());
                if (e == "corriendo" || e == "pausada")
                    return done(ToolResult::error(traducir("mcp", "Grabar se elige antes de arrancar el ensayo.")));
                ev.setGrabarTiempos(accion == "grabar");
            } else if (accion == "aplicar") {
                if (const QString e = ev.aplicarTiemposGrabados(); !e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            } else if (accion == "descartar") {
                ev.descartarTiemposGrabados();
            }
            json r = ev.lineaTiemposGrabados();
            r["grabando"] = ev.grabarTiempos();
            r["ensayo"] = ev.ensayo();
            done(ToolResult().data(r));
        };
        core.addTool(std::move(t));
    }

    // --- Tras un corte de luz (3.8) ------------------------------------------------------
    {
        Tool t;
        t.name = "linea_retomar";
        t.title = traducir("mcp", "Contar qué está pasando tras un corte");
        t.category = "linea";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Tras un corte la línea vuelve en HOLD y pregunta «¿Qué está pasando?». Esto deja la "
                                        "descripción (la que la persona dictó o escribió) en el recorrido. No suelta nada.");
        t.details = QT_TRANSLATE_NOOP("mcp", "El flujo: (1) linea_estado → `retoma` (activa, resumen); (2) la persona cuenta qué pasa («el orador sigue "
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
                           {"simular", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "simular un corte ahora (para probar)")}}}}}};
        t.examples = {R"({"descripcion": "El orador sigue hablando, vamos 5 minutos atrasados"})", R"({"simular": true})"};
        t.run = [&ev](const json& a, Done done) {
            responderVivo(ev, ev.lineaRetomar(a.value("descripcion", std::string()), a.value("simular", false)), done);
        };
        core.addTool(std::move(t));
    }
    vivo("linea_soltar", Clase::Aire, QT_TRANSLATE_NOOP("mcp", "suelta el HOLD tras un corte: lo que se pasó de hora entra solo"),
         traducir("mcp", "Soltar el HOLD del corte"),
         traducir("mcp", "Suelta el HOLD en que volvió la línea tras un corte, con lo que se decidió (`decision`, "
                         "queda en el recorrido). Lo que se pasó de su hora entra en el acto. Solo cuando la "
                         "persona lo confirmó."),
         json{{"decision", {{"type", "string"}}}},
         [&ev](const json& a) { return ev.lineaSoltar(a.value("decision", std::string())); });
}

} // namespace ba::app::mcp
