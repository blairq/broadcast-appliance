// ============================================================================
// src/app/mcp/McpDcnTools.cpp — la central de conferencia Bosch DCN, por MCP
//
// Herramientas con nombre propio (dcn_*) sobre los controles del plugin
// bosch-dcn (asamblea/ConferenciaDcn). Hablan de BANCAS, no de unidades de
// la central: el mapa lo tiene el Cuerpo (dcn_unidad). Las que cambian lo que
// se escucha (abrir/cerrar micrófonos, siguiente, cerrar todos, modo) están en
// McpAire: si las dispara algo automático con aire, se piden confirmar.
//
// Guía: docs/bosch/open-interface.md §9.
// ============================================================================

#include "asamblea/ConferenciaDcn.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerDcnTools() {
    auto& dcn = vm_.dcn();
    core_.addCategory("conferencia", "Central de conferencia (micrófonos de banca)",
                      "La central Bosch DCN del recinto: abrir y cerrar micrófonos de banca, pedidos de palabra, "
                      "siguiente orador, modo de operación, unidades de cada banca.");

    const json banca = {{"type", "string"}, {"description", "La banca (como en el Cuerpo)."}};
    struct Def {
        const char* nombre;
        const char* control;
        const char* titulo;
        const char* descripcion;
        json props;
        bool soloLectura;
        bool destructiva;
    };
    const std::vector<Def> defs = {
        {"dcn_estado", "estado", "Estado de la central",
         "Todo lo de la central: enlace, modo, máximo de abiertos, unidades con su banca, micrófonos abiertos, "
         "pedidos de palabra en orden (`siguiente` = el primero), oradores, bancas sin unidad.",
         json::object(), true, false},
        {"dcn_puertos", "puertos", "Puertos serie del equipo",
         "Los puertos serie (adaptadores USB-serie) para conectar la central.", json::object(), true, false},
        {"dcn_desconectar", "desconectar", "Soltar la central",
         "Deja de controlar la central. La sala sigue andando sola.", json::object(), false, true},
        {"dcn_microfono", "microfono", "Abrir o cerrar el micrófono de una banca",
         "Abre (abierto=true) o cierra el micrófono de la banca en la central. Si ya está el máximo de abiertos, "
         "la central lo rechaza y se dice. El del presidente: cerrarlo pide `confirmar: true`.",
         {{"banca", banca}, {"abierto", {{"type", "boolean"}}}, {"confirmar", {{"type", "boolean"}}}}, false, false},
        {"dcn_siguiente", "siguiente", "Dar la palabra al siguiente",
         "Le da la palabra al primero de la lista de pedidos (abre su micrófono; según el modo cierra el más viejo).",
         json::object(), false, false},
        {"dcn_cerrar_todos", "cerrar_todos", "Cerrar todos los micrófonos",
         "Cierra todos los micrófonos abiertos de la sala, menos el del presidente (salvo `incluir_presidente`).",
         {{"incluir_presidente", {{"type", "boolean"}}}}, false, true},
        {"dcn_pedidos", "pedidos", "Quién pidió la palabra",
         "La lista de pedidos de palabra en orden, con banca, nombre y turno.", json::object(), true, false},
        {"dcn_pedido", "pedido", "Editar la lista de pedidos",
         "`accion`: agregar (al final), quitar, primero (lo pasa adelante) o antes_de (`referencia`: otra banca).",
         {{"banca", banca},
          {"accion", {{"type", "string"}, {"enum", {"agregar", "quitar", "primero", "antes_de"}}}},
          {"referencia", {{"type", "string"}}}},
         false, false},
        {"dcn_pedidos_limpiar", "pedidos_limpiar", "Vaciar la lista de pedidos",
         "Borra todos los pedidos de palabra de la central.", json::object(), false, true},
        {"dcn_oradores", "oradores", "Quién tiene la palabra",
         "La lista de oradores de la central, con el micrófono prendido o no.", json::object(), true, false},
        {"dcn_orador_quitar", "orador_quitar", "Sacar a un orador",
         "Saca a la banca de la lista de oradores de la central (su micrófono se cierra).", {{"banca", banca}}, false,
         false},
        {"dcn_modo", "modo", "Modo de los micrófonos",
         "Cambia el modo de operación de la central (operador = la app y la botonera dan la palabra; voz, "
         "delegado_lista, reemplazo, pulsar…) y los máximos de micrófonos abiertos y de pedidos.",
         {{"modo",
           {{"type", "string"},
            {"enum", {"operador", "delegado_lista", "reemplazo", "voz", "operador_replicas", "pulsar",
                      "operador_sin_ac", "voz_sin_ac"}}}},
          {"max_abiertos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
          {"max_pedidos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
         false, false},
        {"dcn_ajustes", "ajustes", "Ajustes de la central",
         "Lee de la central todos sus ajustes globales (modo, max_abiertos, max_pedidos, cancelar_pedidos, "
         "apagar_micro, tono, ambiente y prio_cancela_todo: si el botón de prioridad del presidente también borra los "
         "pedidos). Con alguno de esos campos, lo cambia; los demás quedan como estaban.",
         {{"modo",
           {{"type", "string"},
            {"enum", {"operador", "delegado_lista", "reemplazo", "voz", "operador_replicas", "pulsar",
                      "operador_sin_ac", "voz_sin_ac"}}}},
          {"max_abiertos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
          {"max_pedidos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
          {"cancelar_pedidos", {{"type", "boolean"}}},
          {"apagar_micro", {{"type", "boolean"}}},
          {"tono", {{"type", "integer"}}},
          {"ambiente", {{"type", "boolean"}}},
          {"prio_cancela_todo", {{"type", "boolean"}}}},
         false, false},
        {"dcn_simular_prioridad", "simular_prioridad", "Simular la prioridad del presidente",
         "Solo con el simulador: el presidente aprieta (`activa: true`) o suelta su botón de prioridad.",
         {{"activa", {{"type", "boolean"}}}}, false, false},
        {"dcn_tiempo", "tiempo", "Tiempo de uso de la palabra",
         "`{limite_min, reloj}` configura el reloj de las unidades (cuenta para atrás); `{banca, aviso}` muestra en la "
         "unidad de esa banca «queda un minuto» (ultimo_minuto) o «se terminó el tiempo» (terminado). La fila de "
         "oradores lo hace sola con el tiempo asignado al orador.",
         {{"banca", banca},
          {"aviso", {{"type", "string"}, {"enum", {"ultimo_minuto", "terminado"}}}},
          {"limite_min", {{"type", "integer"}, {"minimum", 0}, {"maximum", 120}}},
          {"reloj", {{"type", "boolean"}}}},
         false, false},
        {"dcn_votacion", "votacion", "Votación en la central",
         "`accion`: preparar (`tema`, `menu` si_no_abstencion de fábrica, `abierta` para el voto nominal; `numero` "
         "opcional: sin él, la que sigue a la última de la central), "
         "abrir, pausar, reanudar o cerrar (devuelve el resultado final). Los delegados votan en sus unidades; los "
         "parciales salen en dcn_votacion_resultado. El resultado OFICIAL se lleva a Quórum con dcn_votacion_a_quorum.",
         {{"accion", {{"type", "string"}, {"enum", {"preparar", "abrir", "pausar", "reanudar", "cerrar"}}}},
          {"tema", {{"type", "string"}}},
          {"numero", {{"type", "integer"}}},
          {"menu", {{"type", "string"}, {"enum", {"si_no", "si_no_abstencion", "a_favor_en_contra", "si_no_abstencion_nppv"}}}},
          {"abierta", {{"type", "boolean"}}},
          {"mostrar", {{"type", "boolean"}}}},
         false, false},
        {"dcn_votacion_resultado", "votacion_resultado", "Resultado de la votación de la central",
         "Totales por opción, presentes, los que no votaron y el voto de cada banca (parcial si sigue abierta).",
         json::object(), true, false},
        {"dcn_asistencia", "asistencia", "Presentes según la central",
         "Presentes y ausentes según la asistencia de la central, por banca.", json::object(), true, false},
        {"dcn_simular_voto", "simular_voto", "Simular un voto",
         "Solo con el simulador: la banca vota (0 afirmativo, 1 negativo, 2 abstención).",
         {{"banca", banca}, {"voto", {{"type", "integer"}}}}, false, false},
        {"dcn_simular_presencia", "simular_presencia", "Simular presencia",
         "Solo con el simulador: la banca se registra presente o ausente.",
         {{"banca", banca}, {"presente", {{"type", "boolean"}}}}, false, false},
        {"dcn_unidades", "unidades", "Unidades y bancas",
         "Las unidades de la central con su tipo (delegado, presidente; `tipo_nombre` dice qué equipo es) y la banca "
         "asignada; bancas sin unidad y unidades sin banca. Una DCN-DDI en modo doble son DOS micrófonos: la mitad "
         "izquierda es la unidad y la derecha la unidad + 256 (`mitad`, `ddi`); cada una va en su banca.",
         json::object(), true, false},
        {"dcn_unidad_asignar", "unidad_asignar", "Asignar la unidad de una banca",
         "`{banca, unidad}` la asigna (queda en el Cuerpo como dcn_unidad). `{aprender: banca}` espera el próximo "
         "botón de micrófono que se apriete en la sala; `{aprender_todas: true}` recorre las bancas sin unidad una "
         "por una; `{cancelar: true}` deja de aprender.",
         {{"banca", banca},
          {"unidad", {{"type", "integer"}}},
          {"aprender", {{"type", "string"}}},
          {"aprender_todas", {{"type", "boolean"}}},
          {"cancelar", {{"type", "boolean"}}}},
         false, false},
        {"dcn_traza", "traza", "Tramas crudas",
         "Para diagnóstico: `{activa: true}` registra las tramas en hexadecimal; `{ultimas: n}` las devuelve con los "
         "contadores de reenvíos, NAK y checksum mal.",
         {{"activa", {{"type", "boolean"}}}, {"ultimas", {{"type", "integer"}}}}, false, false},
        {"dcn_funciones", "funciones", "Funciones sin documentar de la central",
         "Las 56 funciones del Open Interface que no están en los manuales (salen del software de Bosch del DVD; "
         "las 10 de descarga de firmware a la CCU no se ofrecen): nombre, código, parámetros, respuesta, si el "
         "formato está verificado y si es peligrosa. Filtros: `texto`, `clase` (pedido | aviso). Ninguna está "
         "probada contra una CCU real.",
         {{"texto", {{"type", "string"}}}, {"clase", {{"type", "string"}, {"enum", {"pedido", "aviso"}}}}}, true, false},
        {"dcn_funcion", "funcion", "Llamar una función sin documentar",
         "`nombre` (de dcn_funciones, el nombre de Bosch o el código) y `args` con sus parámetros. Las peligrosas "
         "(licencias, visor de las unidades) piden `confirmar: true`. `args.hex` manda los bytes tal cual. Si la "
         "central no la conoce, contesta un error y se dice.",
         {{"nombre", {{"type", "string"}}}, {"args", {{"type", "object"}}}, {"confirmar", {{"type", "boolean"}}}},
         false, false},
        {"dcn_proximo", "proximo", "Próximo en hablar",
         "Quién es el próximo en hablar según la central y, en una votación por llamado nominal, a quién le toca "
         "votar.",
         json::object(), true, false},
        {"dcn_cerrar_tras_siguiente", "cerrar_tras_siguiente", "Cerrar micrófonos al pasar al siguiente",
         "Con `activo: true`, cada «siguiente» cierra los micrófonos de los que estaban hablando (lo hace la central).",
         {{"activo", {{"type", "boolean"}}}}, false, false},
        {"dcn_llamado_nominal", "llamado_nominal", "Orden del llamado nominal",
         "El orden en que votan las bancas en una votación por llamado nominal (`bancas`, o `delegados` de la central).",
         {{"bancas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"delegados", {{"type", "array"}, {"items", {{"type", "integer"}}}}}},
         false, false},
        {"dcn_simular_aviso", "simular_aviso", "Simular un aviso de la central",
         "Solo con el simulador: la central manda un aviso sin documentar (por ejemplo votacion_pedido_iniciar "
         "{numero}, como si el presidente abriera la votación desde su unidad).",
         {{"nombre", {{"type", "string"}}}, {"datos", {{"type", "object"}}}}, false, false},
        {"dcn_simular_boton", "simular_boton", "Simular un botón de la sala",
         "Solo con el simulador (transporte simulador): aprieta el botón de micrófono de una banca (o unidad), como "
         "si un delegado pidiera la palabra.",
         {{"banca", banca}, {"unidad", {{"type", "integer"}}}}, false, false},
    };
    for (const Def& d : defs) {
        Tool t;
        t.name = d.nombre;
        t.title = d.titulo;
        t.category = "conferencia";
        t.readOnly = d.soloLectura;
        t.destructive = d.destructiva;
        t.description = d.descripcion;
        t.inputSchema = {{"type", "object"}, {"properties", d.props.is_object() ? d.props : json::object()}};
        const std::string control = d.control;
        t.run = [&dcn, control](const json& a, Done done) {
            json args = a.is_object() ? a : json::object();
            if (!args.contains("quien")) args["quien"] = "mcp";
            dcn.control(control, args, [done](bool ok, const json& r, const std::string& e) {
                if (!ok) return done(ToolResult::error(e));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dcn_votacion_a_quorum";
        t.title = "Llevar la votación de la central a Quórum";
        t.category = "conferencia";
        t.description = "Carga la última votación CERRADA de la central como resultado oficial en Quórum (totales, "
                        "presentes y el voto de cada integrante), para el acta, el CSV nominal y el gráfico de votación. "
                        "`asunto` reemplaza el tema que tenía en la central.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"asunto", {{"type", "string"}}}}}};
        t.run = [&dcn](const json& a, Done done) {
            const json r = dcn.pasarAQuorum(a.value("asunto", std::string()));
            if (r.contains("error")) return done(ToolResult::error(r.value("error", std::string())));
            done(ToolResult().data(r));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dcn_presentes_quorum";
        t.title = "Presentes de Quórum desde la central";
        t.category = "conferencia";
        t.description = "Con `activo: true`, Quórum cuenta los presentes que dice la asistencia de la central (en vez "
                        "del padrón del evento). Sin argumentos, dice cómo está y los dos números.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        t.run = [&dcn](const json& a, Done done) {
            if (a.contains("activo")) dcn.setPresentesDeLaCentral(a.value("activo", false));
            done(ToolResult().data({{"activo", dcn.presentesDeLaCentral()}, {"presentes_central", dcn.presentesCentral()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "dcn_conectar";
        t.title = "Conectar la central de conferencia";
        t.category = "conferencia";
        t.description =
            "Conecta con la central Bosch DCN (instala y prende el plugin bosch-dcn si hace falta). Lo que se pasa "
            "queda guardado: `transporte` serie (RS-232 por adaptador USB; `puerto` de dcn_puertos; de fábrica el "
            "puerto 1 de la CCU es `protocolo` full a `velocidad` 115200 con `rtscts`; «simple» es el otro protocolo "
            "serie), tcp (CCU2: `host`, `puerto_tcp` 9451) o simulador (para probar sin el equipo). `votacion` y "
            "`asistencia` (de fábrica sí) enganchan esas aplicaciones de la central. "
            "`solo_escuchar` (de fábrica true) recibe todo pero no le manda órdenes: apagarlo cuando la sala responde "
            "bien. Requiere la licencia del Open Interface habilitada en la central.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"transporte", {{"type", "string"}, {"enum", {"serie", "tcp", "simulador"}}}},
                           {"puerto", {{"type", "string"}}},
                           {"velocidad", {{"type", "integer"}, {"enum", {9600, 19200, 57600, 115200}}}},
                           {"protocolo", {{"type", "string"}, {"enum", {"full", "simple"}}}},
                           {"rtscts", {{"type", "boolean"}}},
                           {"votacion", {{"type", "boolean"}}},
                           {"asistencia", {{"type", "boolean"}}},
                           {"host", {{"type", "string"}}},
                           {"puerto_tcp", {{"type", "integer"}}},
                           {"solo_escuchar", {{"type", "boolean"}}},
                           {"escape", {{"type", "string"}, {"description", "Avanzado: el byte de escape (0xC8 o 0x1B)."}}},
                           {"traza", {{"type", "boolean"}}},
                           {"simulador_actividad_s", {{"type", "number"}}},
                           {"simulador_unidades",
                            {{"type", "integer"},
                             {"description", "Simulador: cuántas unidades (de fábrica, una por banca del Cuerpo más la "
                                             "presidencia; crece solo si el Cuerpo crece)."}}}}}};
        t.examples = {R"({"transporte": "simulador", "solo_escuchar": false})",
                      R"({"transporte": "serie", "puerto": "/dev/cu.usbserial-1410", "velocidad": 115200, "protocolo": "full"})",
                      R"({"transporte": "tcp", "host": "192.168.0.20"})"};
        t.run = [&dcn](const json& a, Done done) {
            dcn.conectar(a.is_object() ? a : json::object(), [done](bool ok, const json& r, const std::string& e) {
                if (!ok) return done(ToolResult::error(e));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
