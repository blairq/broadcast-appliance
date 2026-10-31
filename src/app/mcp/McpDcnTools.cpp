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

#include "i18n/I18n.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerDcnTools() {
    auto& dcn = vm_.dcn();
    core_.addCategory("conferencia", ba::i18n::traducir("mcp", "Central de conferencia (micrófonos de banca)"),
                      ba::i18n::traducir("mcp", "La central Bosch DCN del recinto: abrir y cerrar micrófonos de banca, pedidos de palabra, "
                      "siguiente orador, modo de operación, unidades de cada banca."));

    const json banca = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "La banca (como en el Cuerpo).")}};
    struct Def {
        const char* nombre;
        const char* control;
        const char* titulo;
        const char* descripcion;
        json props;
        Clase clase; ///< la declarada (registro de F3-02)
        bool destructiva;
        const char* porQueAire = nullptr; ///< las de Clase::Aire: por qué cambian lo que sale
    };
    const std::vector<Def> defs = {
        {"dcn_estado", "estado", QT_TRANSLATE_NOOP("mcp", "Estado de la central"),
         QT_TRANSLATE_NOOP("mcp", "Todo lo de la central: enlace, modo, máximo de abiertos, unidades con su banca, micrófonos abiertos, "
         "pedidos de palabra en orden (`siguiente` = el primero), oradores, bancas sin unidad."),
         json::object(), Clase::Lectura, false},
        {"dcn_puertos", "puertos", QT_TRANSLATE_NOOP("mcp", "Puertos serie del equipo"),
         QT_TRANSLATE_NOOP("mcp", "Los puertos serie (adaptadores USB-serie) para conectar la central."), json::object(), Clase::Lectura, false},
        {"dcn_desconectar", "desconectar", QT_TRANSLATE_NOOP("mcp", "Soltar la central"),
         QT_TRANSLATE_NOOP("mcp", "Deja de controlar la central de conferencias: la sala sigue andando sola con sus micrófonos; dcn_conectar "
                                  "la retoma."), json::object(), Clase::Escritura, true},
        {"dcn_microfono", "microfono", QT_TRANSLATE_NOOP("mcp", "Abrir o cerrar el micrófono de una banca"),
         QT_TRANSLATE_NOOP("mcp", "Abre (abierto=true) o cierra el micrófono de la banca en la central. Si ya está el máximo de abiertos, "
         "la central lo rechaza y se dice. El del presidente: cerrarlo pide `confirmar: true`."),
         {{"banca", banca}, {"abierto", {{"type", "boolean"}}}, {"confirmar", {{"type", "boolean"}}}}, Clase::Aire, false, QT_TRANSLATE_NOOP("mcp", "abre o cierra el micrófono de una banca en la central de conferencia")},
        {"dcn_siguiente", "siguiente", QT_TRANSLATE_NOOP("mcp", "Dar la palabra al siguiente"),
         QT_TRANSLATE_NOOP("mcp", "Le da la palabra al primero de la lista de pedidos (abre su micrófono; según el modo cierra el más viejo)."),
         json::object(), Clase::Aire, false, QT_TRANSLATE_NOOP("mcp", "le da la palabra (y el micrófono) al primero de la lista")},
        {"dcn_cerrar_todos", "cerrar_todos", QT_TRANSLATE_NOOP("mcp", "Cerrar todos los micrófonos"),
         QT_TRANSLATE_NOOP("mcp", "Cierra todos los micrófonos abiertos de la sala, menos el del presidente (salvo `incluir_presidente`)."),
         {{"incluir_presidente", {{"type", "boolean"}}}}, Clase::Aire, true, QT_TRANSLATE_NOOP("mcp", "cierra los micrófonos de la sala")},
        {"dcn_pedidos", "pedidos", QT_TRANSLATE_NOOP("mcp", "Quién pidió la palabra"),
         QT_TRANSLATE_NOOP("mcp", "La lista de pedidos de palabra en orden, con banca, nombre y turno."), json::object(), Clase::Lectura, false},
        {"dcn_pedido", "pedido", QT_TRANSLATE_NOOP("mcp", "Editar la lista de pedidos"),
         QT_TRANSLATE_NOOP("mcp", "`accion`: agregar (al final), quitar, primero (lo pasa adelante) o antes_de (`referencia`: otra banca)."),
         {{"banca", banca},
          {"accion", {{"type", "string"}, {"enum", {"agregar", "quitar", "primero", "antes_de"}}}},
          {"referencia", {{"type", "string"}}}},
         Clase::Escritura, false},
        {"dcn_pedidos_limpiar", "pedidos_limpiar", QT_TRANSLATE_NOOP("mcp", "Vaciar la lista de pedidos"),
         QT_TRANSLATE_NOOP("mcp", "Borra todos los pedidos de palabra de la central (los de dcn_pedidos): nadie queda esperando para hablar."), json::object(), Clase::Escritura, true},
        {"dcn_oradores", "oradores", QT_TRANSLATE_NOOP("mcp", "Quién tiene la palabra"),
         QT_TRANSLATE_NOOP("mcp", "La lista de oradores de la central, con el micrófono prendido o no."), json::object(), Clase::Lectura, false},
        {"dcn_orador_quitar", "orador_quitar", QT_TRANSLATE_NOOP("mcp", "Sacar a un orador"),
         QT_TRANSLATE_NOOP("mcp", "Saca a la banca de la lista de oradores de la central (su micrófono se cierra)."), {{"banca", banca}}, Clase::Aire,
         false, QT_TRANSLATE_NOOP("mcp", "saca a un orador de la lista (su micrófono se cierra)")},
        {"dcn_modo", "modo", QT_TRANSLATE_NOOP("mcp", "Modo de los micrófonos"),
         QT_TRANSLATE_NOOP("mcp", "Cambia el modo de operación de la central (operador = la app y la botonera dan la palabra; voz, "
         "delegado_lista, reemplazo, pulsar…) y los máximos de micrófonos abiertos y de pedidos."),
         {{"modo",
           {{"type", "string"},
            {"enum", {"operador", "delegado_lista", "reemplazo", "voz", "operador_replicas", "pulsar",
                      "operador_sin_ac", "voz_sin_ac"}}}},
          {"max_abiertos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
          {"max_pedidos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
         Clase::Aire, false, QT_TRANSLATE_NOOP("mcp", "cambia cómo se abren los micrófonos de la sala")},
        {"dcn_ajustes", "ajustes", QT_TRANSLATE_NOOP("mcp", "Ajustes de la central"),
         QT_TRANSLATE_NOOP("mcp", "Lee de la central todos sus ajustes globales (modo, max_abiertos, max_pedidos, cancelar_pedidos, "
         "apagar_micro, tono, ambiente y prio_cancela_todo: si el botón de prioridad del presidente también borra los "
         "pedidos). Con alguno de esos campos, lo cambia; los demás quedan como estaban."),
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
         Clase::Escritura, false},
        {"dcn_simular_prioridad", "simular_prioridad", QT_TRANSLATE_NOOP("mcp", "Simular la prioridad del presidente"),
         QT_TRANSLATE_NOOP("mcp", "Solo con el simulador: el presidente aprieta (`activa: true`) o suelta su botón de prioridad."),
         {{"activa", {{"type", "boolean"}}}}, Clase::Escritura, false},
        {"dcn_tiempo", "tiempo", QT_TRANSLATE_NOOP("mcp", "Tiempo de uso de la palabra"),
         QT_TRANSLATE_NOOP("mcp", "`{limite_min, reloj}` configura el reloj de las unidades (cuenta para atrás); `{banca, aviso}` muestra en la "
         "unidad de esa banca «queda un minuto» (ultimo_minuto) o «se terminó el tiempo» (terminado). La fila de "
         "oradores lo hace sola con el tiempo asignado al orador."),
         {{"banca", banca},
          {"aviso", {{"type", "string"}, {"enum", {"ultimo_minuto", "terminado"}}}},
          {"limite_min", {{"type", "integer"}, {"minimum", 0}, {"maximum", 120}}},
          {"reloj", {{"type", "boolean"}}}},
         Clase::Escritura, false},
        {"dcn_votacion", "votacion", QT_TRANSLATE_NOOP("mcp", "Votación en la central"),
         QT_TRANSLATE_NOOP("mcp", "`accion`: preparar (`tema`, `menu` si_no_abstencion de fábrica, `abierta` para el voto nominal; `numero` "
         "opcional: sin él, la que sigue a la última de la central), "
         "abrir, pausar, reanudar o cerrar (devuelve el resultado final). Los delegados votan en sus unidades; los "
         "parciales salen en dcn_votacion_resultado. El resultado OFICIAL se lleva a Quórum con dcn_votacion_a_quorum."),
         {{"accion", {{"type", "string"}, {"enum", {"preparar", "abrir", "pausar", "reanudar", "cerrar"}}}},
          {"tema", {{"type", "string"}}},
          {"numero", {{"type", "integer"}}},
          {"menu", {{"type", "string"}, {"enum", {"si_no", "si_no_abstencion", "a_favor_en_contra", "si_no_abstencion_nppv"}}}},
          {"abierta", {{"type", "boolean"}}},
          {"mostrar", {{"type", "boolean"}}}},
         Clase::Escritura, false},
        {"dcn_votacion_resultado", "votacion_resultado", QT_TRANSLATE_NOOP("mcp", "Resultado de la votación de la central"),
         QT_TRANSLATE_NOOP("mcp", "Totales por opción, presentes, los que no votaron y el voto de cada banca (parcial si sigue abierta)."),
         json::object(), Clase::Lectura, false},
        {"dcn_asistencia", "asistencia", QT_TRANSLATE_NOOP("mcp", "Presentes según la central"),
         QT_TRANSLATE_NOOP("mcp", "Presentes y ausentes según la asistencia de la central, por banca."), json::object(), Clase::Lectura, false},
        {"dcn_simular_voto", "simular_voto", QT_TRANSLATE_NOOP("mcp", "Simular un voto"),
         QT_TRANSLATE_NOOP("mcp", "Solo con el simulador: la banca vota (0 afirmativo, 1 negativo, 2 abstención)."),
         {{"banca", banca}, {"voto", {{"type", "integer"}}}}, Clase::Escritura, false},
        {"dcn_simular_presencia", "simular_presencia", QT_TRANSLATE_NOOP("mcp", "Simular presencia"),
         QT_TRANSLATE_NOOP("mcp", "Solo con el simulador: la banca se registra presente o ausente."),
         {{"banca", banca}, {"presente", {{"type", "boolean"}}}}, Clase::Escritura, false},
        {"dcn_unidades", "unidades", QT_TRANSLATE_NOOP("mcp", "Unidades y bancas"),
         QT_TRANSLATE_NOOP("mcp", "Las unidades de la central con su tipo (delegado, presidente; `tipo_nombre` dice qué equipo es) y la banca "
         "asignada; bancas sin unidad y unidades sin banca. Una DCN-DDI en modo doble son DOS micrófonos: la mitad "
         "izquierda es la unidad y la derecha la unidad + 256 (`mitad`, `ddi`); cada una va en su banca."),
         json::object(), Clase::Lectura, false},
        {"dcn_unidad_asignar", "unidad_asignar", QT_TRANSLATE_NOOP("mcp", "Asignar la unidad de una banca"),
         QT_TRANSLATE_NOOP("mcp", "`{banca, unidad}` la asigna (queda en el Cuerpo como dcn_unidad). `{aprender: banca}` espera el próximo "
         "botón de micrófono que se apriete en la sala; `{aprender_todas: true}` recorre las bancas sin unidad una "
         "por una; `{cancelar: true}` deja de aprender."),
         {{"banca", banca},
          {"unidad", {{"type", "integer"}}},
          {"aprender", {{"type", "string"}}},
          {"aprender_todas", {{"type", "boolean"}}},
          {"cancelar", {{"type", "boolean"}}}},
         Clase::Escritura, false},
        {"dcn_traza", "traza", QT_TRANSLATE_NOOP("mcp", "Tramas crudas"),
         QT_TRANSLATE_NOOP("mcp", "Para diagnóstico: `{activa: true}` registra las tramas en hexadecimal; `{ultimas: n}` las devuelve con los "
         "contadores de reenvíos, NAK y checksum mal."),
         {{"activa", {{"type", "boolean"}}}, {"ultimas", {{"type", "integer"}}}}, Clase::Escritura, false},
        {"dcn_funciones", "funciones", QT_TRANSLATE_NOOP("mcp", "Funciones sin documentar de la central"),
         QT_TRANSLATE_NOOP("mcp", "Las 56 funciones del Open Interface que no están en los manuales (salen del software de Bosch del DVD; "
         "las 10 de descarga de firmware a la CCU no se ofrecen): nombre, código, parámetros, respuesta, si el "
         "formato está verificado y si es peligrosa. Filtros: `texto`, `clase` (pedido | aviso). Ninguna está "
         "probada contra una CCU real."),
         {{"texto", {{"type", "string"}}}, {"clase", {{"type", "string"}, {"enum", {"pedido", "aviso"}}}}}, Clase::Lectura, false},
        {"dcn_funcion", "funcion", QT_TRANSLATE_NOOP("mcp", "Llamar una función sin documentar"),
         QT_TRANSLATE_NOOP("mcp", "`nombre` (de dcn_funciones, el nombre de Bosch o el código) y `args` con sus parámetros. Las peligrosas "
         "(licencias, visor de las unidades) piden `confirmar: true`. `args.hex` manda los bytes tal cual. Si la "
         "central no la conoce, contesta un error y se dice."),
         {{"nombre", {{"type", "string"}}}, {"args", {{"type", "object"}}}, {"confirmar", {{"type", "boolean"}}}},
         Clase::Escritura, false},
        {"dcn_proximo", "proximo", QT_TRANSLATE_NOOP("mcp", "Próximo en hablar"),
         QT_TRANSLATE_NOOP("mcp", "Quién es el próximo en hablar según la central y, en una votación por llamado nominal, a quién le toca "
         "votar."),
         json::object(), Clase::Lectura, false},
        {"dcn_cerrar_tras_siguiente", "cerrar_tras_siguiente", QT_TRANSLATE_NOOP("mcp", "Cerrar micrófonos al pasar al siguiente"),
         QT_TRANSLATE_NOOP("mcp", "Con `activo: true`, cada «siguiente» cierra los micrófonos de los que estaban hablando (lo hace la central)."),
         {{"activo", {{"type", "boolean"}}}}, Clase::Escritura, false},
        {"dcn_llamado_nominal", "llamado_nominal", QT_TRANSLATE_NOOP("mcp", "Orden del llamado nominal"),
         QT_TRANSLATE_NOOP("mcp", "El orden en que votan las bancas en una votación por llamado nominal (`bancas`, o `delegados` de la central)."),
         {{"bancas", {{"type", "array"}, {"items", {{"type", "string"}}}}},
          {"delegados", {{"type", "array"}, {"items", {{"type", "integer"}}}}}},
         Clase::Escritura, false},
        {"dcn_simular_aviso", "simular_aviso", QT_TRANSLATE_NOOP("mcp", "Simular un aviso de la central"),
         QT_TRANSLATE_NOOP("mcp", "Solo con el simulador: la central manda un aviso sin documentar (por ejemplo votacion_pedido_iniciar "
         "{numero}, como si el presidente abriera la votación desde su unidad)."),
         {{"nombre", {{"type", "string"}}}, {"datos", {{"type", "object"}}}}, Clase::Escritura, false},
        {"dcn_simular_boton", "simular_boton", QT_TRANSLATE_NOOP("mcp", "Simular un botón de la sala"),
         QT_TRANSLATE_NOOP("mcp", "Solo con el simulador (transporte simulador): aprieta el botón de micrófono de una banca (o unidad), como "
         "si un delegado pidiera la palabra."),
         {{"banca", banca}, {"unidad", {{"type", "integer"}}}}, Clase::Escritura, false},
    };
    for (const Def& d : defs) {
        Tool t;
        t.name = d.nombre;
        t.title = ba::i18n::traducir("mcp", d.titulo);
        t.category = "conferencia";
        t.clase = d.clase;
        t.porQueAire = d.porQueAire;
        t.destructive = d.destructiva;
        t.description = d.descripcion; // el original: lo traduce la vista (ver traducirParaVer)
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
        t.title = ba::i18n::traducir("mcp", "Llevar la votación de la central a Quórum");
        t.category = "conferencia";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Carga la última votación CERRADA de la central como resultado oficial en Quórum (totales, "
                        "presentes y el voto de cada integrante), para el acta, el CSV nominal y el gráfico de votación. "
                        "`asunto` reemplaza el tema que tenía en la central.");
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
        t.title = ba::i18n::traducir("mcp", "Presentes de Quórum desde la central");
        t.category = "conferencia";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Con `activo: true`, Quórum cuenta los presentes que dice la asistencia de la central (en vez "
                        "del padrón del evento). Sin argumentos, dice cómo está y los dos números.");
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
        t.title = ba::i18n::traducir("mcp", "Conectar la central de conferencia");
        t.category = "conferencia";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Conecta con la central Bosch DCN (instala y prende el plugin bosch-dcn si hace falta). Lo que se pasa "
            "queda guardado: `transporte` serie (RS-232 por adaptador USB; `puerto` de dcn_puertos; de fábrica el "
            "puerto 1 de la CCU es `protocolo` full a `velocidad` 115200 con `rtscts`; «simple» es el otro protocolo "
            "serie), tcp (CCU2: `host`, `puerto_tcp` 9451) o simulador (para probar sin el equipo). `votacion` y "
            "`asistencia` (de fábrica sí) enganchan esas aplicaciones de la central. "
            "`solo_escuchar` (de fábrica true) recibe todo pero no le manda órdenes: apagarlo cuando la sala responde "
            "bien. Requiere la licencia del Open Interface habilitada en la central.");
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
                           {"escape", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Avanzado: el byte de escape (0xC8 o 0x1B).")}}},
                           {"traza", {{"type", "boolean"}}},
                           {"simulador_actividad_s", {{"type", "number"}}},
                           {"simulador_unidades",
                            {{"type", "integer"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "Simulador: cuántas unidades (de fábrica, una por banca del Cuerpo más la "
                                             "presidencia; crece solo si el Cuerpo crece).")}}}}}};
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
