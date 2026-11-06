// ============================================================================
// src/app/mcp/McpGrillaTools.cpp — la grilla de personas, por MCP
//
// Lo mismo que la ventana Propiedades de una fuente «Grilla de personas»
// (ba_grilla) y por el mismo modelo (escenas::ControlGrillas): lo que ajusta el
// asistente lo ve el operador en el acto, y al revés. Crear una es crear_fuente
// {type: "ba_grilla"} como cualquier fuente.
// ============================================================================

#include "escenas/ControlGrillas.hpp"
#include "escenas/GrillaPersonas.hpp"
#include "i18n/I18nTexto.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "mcp/InterfazMcp.hpp"
#include "ui/Fluidez.hpp"

#include <functional>

namespace ba::app::mcp {

void McpService::registerGrillaTools() {
    InterfazMcp* vmp = &vm_;
    const std::string ajustesAyuda = ba::i18n::traducir("mcp", 
        "origen (sala|asamblea|ambas), orden (llegada|habla|prioridad), tope (celdas con el «+N», 0 = sin tope), "
        "solo_con_camara, sin_al_aire (sin los que ya están al aire en otra fuente o en otra grilla que se ve, contando "
        "las escenas de las capas DSK), resaltar (a quien habla), grupo "
        "(las grillas con el mismo grupo se reparten la gente sin repetir; \"\" = sola), orden_en_grupo (la primera "
        "manda el orden y el modo), reparto (en_orden: llena la primera hasta su tope; parejo: la misma cantidad en "
        "cada una), ancho, alto, proporcion (16:9|4:3|1:1|3:4|9:16|llenar), separacion, margen, transicion_ms, fondo "
        "y color_resaltar (\"#AARRGGBB\"). Cuando alguien habla: al_hablar (sin_cambios: la grilla no se toca, solo la "
        "cámara del orador en alta | resaltar: nadie se mueve, el orador con borde | destacar: el orador grande y los demás en una franja, en el mismo orden | superponer: el orador "
        "encima en un hueco que le deja la grilla | escena: la grilla no cambia y otra grilla con muestra=orador lo "
        "muestra solo | nada: sin orador), agrandar (resaltar y escena: no | encima: el orador crece | achicando: el orador crece y los demás se achican; animado en transicion_ms y el borde del orador aparece con un fundido), pasar_a_escena "
        "(escena: a_mano | automatico | asistente: siempre avisa al operador), salida_orador (\"\" = la principal), "
        "escena_orador (la pone grilla_personas_escena_orador), disparo (voz: voz sostenida voz_ms | palabra: la palabra de la asamblea | orador: el "
        "orador al aire de la conferencia | manual: grilla_personas_destacar), voz_ms, minimo_ms (el orador no "
        "cambia antes: histéresis), precarga (el siguiente ya pide la capa alta), destacar_lugar (centro|izquierda), "
        "destacar_tamano (% del lienzo), franja (abajo|costado), superponer_tamano, superponer_completa, fundido_ms (de "
        "la baja a la HD). Nombres: nombres (no|nombre|nombre_cargo), nombre_tamano (% del alto de la celda), "
        "nombre_color, nombre_fondo, nombre_lugar (abajo|arriba). muestra (todos|orador) y sigue (la grilla cuyo "
        "orador muestra; \"\" = la primera que tenga): la escena del orador.") + " " +
        ba::i18n::traducir("mcp", "alta_hasta (de fábrica 10; 0 = sin límite): con más personas que esto en la grilla, "
        "todas las miniaturas piden la cámara en baja definición, salvo el orador y el siguiente (precarga); con esto o "
        "menos, cada una según el tamaño de su celda. Vuelve a la regla por tamaño recién con una persona menos "
        "(histéresis). grilla_personas_estado dice en cada grilla alta_hasta y en_baja, y en cada persona su capa (alta|baja).") + " " +
        ba::i18n::traducir("mcp", "alta_max (de fábrica 4; 1 a 64): tope duro, nunca más personas que esto en alta a la vez, "
        "sean cuantos sean los invitados. Si piden más, por prioridad: el orador (o los oradores), el presentador, el "
        "siguiente (precarga), los fijados y el resto por tamaño; el que ya estaba en alta no lo pierde por uno nuevo del "
        "mismo grupo. Con varias grillas manda el alta_max más chico. grilla_personas_estado lo dice en cada grilla.") + " " +
        ba::i18n::traducir("mcp", "La forma de las celdas: la proporción del video (16:9) siempre cuenta. Con proporcion=llenar "
        "la celda puede recortar el video hasta recorte_max % por lado (de fábrica 25: celdas entre 4:3 y 21:9; 0 a 40); "
        "con una proporción fija, la celda exacta con bandas. celda_min_ancho y celda_min_alto (de fábrica 96×54, px en "
        "un lienzo de 1080 de lado corto, se escalan al lienzo): ninguna celda más chica; si no entran todos, el resto "
        "va al «+N». grilla_personas_estado dice en cada grilla «disposicion»: columnas, filas, la celda en px, su "
        "proporción efectiva, la del video, la mínima y la máxima, el mínimo escalado y cuántas celdas caben.");
    {
        Tool t;
        t.name = "grilla_personas_estado";
        t.title = ba::i18n::traducir("mcp", "Qué muestra cada grilla de personas");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true; // los nombres los escribieron los invitados
        t.description = QT_TRANSLATE_NOOP("mcp", "Las fuentes «Grilla de personas» (ba_grilla): a quién muestra cada una ahora, cuántos quedaron en el "
                        "«+N», su canal y sus ajustes principales; y la gente que puede aparecer (conferencia y asamblea) con "
                        "quién habla, quién ya está al aire (en otra fuente o en una grilla que se ve), quién tiene la "
                        "cámara prendida, en qué grillas está cada uno y lo decidido a mano (fijo, fuera).");
        t.details = ba::i18n::llenar(QT_TRANSLATE_NOOP("mcp", "Una grilla crece y se acomoda sola con la gente conectada: los admitidos de la Conferencia (al aire o en la "
                    "fila de oradores) y los integrantes habilitados de la Asamblea. La misma fuente en dos escenas es la "
                    "misma imagen: para repartir gente entre escenas se crean varias con el mismo `grupo`. Crear una: "
                    "crear_fuente {type: \"ba_grilla\", name, settings: {…}}. Ajustes: %1"), {ajustesAyuda});
        t.examples = {R"({})"};
        t.run = [vmp](const json&, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            g->recalcular();
            ToolResult r;
            r.data(g->estado());
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grilla_personas_ajustar";
        t.title = ba::i18n::traducir("mcp", "Cambiar los ajustes de una grilla de personas");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = ba::i18n::llenar(QT_TRANSLATE_NOOP("mcp", "Cambia los ajustes de una fuente «Grilla de personas» (los mismos de la ventana Propiedades). Solo "
                        "los que vienen; se validan todos antes de tocar nada. %1"), {ajustesAyuda});
        t.inputSchema = {{"type", "object"},
                         {"required", {"fuente", "ajustes"}},
                         {"properties",
                          {{"fuente", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el nombre de la fuente ba_grilla")}}},
                           {"ajustes", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "los ajustes a cambiar")}}}}}};
        t.examples = {R"({"fuente": "Grilla", "ajustes": {"origen": "asamblea", "orden": "prioridad", "tope": 12}})",
                      R"({"fuente": "Grilla 2", "ajustes": {"grupo": "recinto", "orden_en_grupo": 2, "reparto": "parejo"}})"};
        t.run = [vmp](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            const std::string e = g->ajustar(a.value("fuente", std::string()), a.value("ajustes", json::object()));
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» quedó con los ajustes nuevos."), {a.value("fuente", std::string())})));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grilla_personas_medir";
        t.title = ba::i18n::traducir("mcp", "Medir los cuadros por segundo de cada cara de la grilla");
        t.category = "fuentes";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true; // las claves de las personas
        t.description = QT_TRANSLATE_NOOP("mcp", "Mide `segundos` (de fábrica 10, de 2 a 120) cuántos cuadros NUEVOS muestra cada celda de cada grilla de "
                        "personas, desde los píxeles: `entrada` es el mosaico apenas lo lee la app (lo que pintó el gateway y "
                        "llegó) y `programa`, la misma celda en el programa (si la grilla está al aire; `programa_rect` [x, y, w, h] "
                        "en fracciones del lienzo dice dónde está, de fábrica el lienzo entero). Por celda: cps, intervalo_ms "
                        "(p50/p95/max entre un cuadro nuevo y el anterior), huecos (intervalos de más de 1,5 períodos de su capa) "
                        "y por_debajo (menos del 90 % de lo que manda su capa: baja 15 cps, alta 30). Del mosaico: cps, llegada_ms "
                        "y perdidos (cuadros que el gateway publicó y la app no leyó). `resumen`: cps_p50, cps_p5, por_debajo_pct.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"segundos", {{"type", "integer"}, {"minimum", 2}, {"maximum", 120}}},
                           {"programa_rect", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4},
                                              {"description", QT_TRANSLATE_NOOP("mcp", "dónde está la grilla en el programa, en fracciones del lienzo")}}},
                           {"zonas", {{"type", "array"},
                                      {"items", {{"type", "object"}}},
                                      {"description", QT_TRANSLATE_NOOP("mcp", "otras zonas del programa a medir igual (un invitado en su lugar, una cámara): "
                                                                               "[{id, x, y, w, h en fracciones del lienzo, cps que manda}]; el resultado va en `zonas`")}}}}}};
        t.examples = {R"({})", R"({"segundos": 20})"};
        t.run = [vmp](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            g->recalcular();
            const std::string e = g->medir(a.value("segundos", 10), a, [done](const json& r) {
                ToolResult res;
                res.data(r);
                res.untrusted = true;
                done(res);
            });
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "video_fluidez";
        t.title = ba::i18n::traducir("mcp", "Si los videos se ven parejos o a saltos");
        t.category = "video";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true; // las claves y los nombres de las personas
        t.description = QT_TRANSLATE_NOOP("mcp", "La app se mide sola, siempre: por cada video que dibuja, cada cuánto llega un cuadro nuevo en cada "
                        "tramo (intervalo p50/p95/max en ms de los últimos 128, huecos = intervalos de más de 1,5 veces el período "
                        "que debería tener, cps contra esperado_cps) y si viene «a_saltos» (sostenido 5 s; se avisa al operador). "
                        "`grilla`: cada celda de la grilla de personas con lo del gateway (red: lo que llegó; deco: lo que salió "
                        "del decodificador; guardado; pintado: lo que se ve en el mosaico, con salteados y repetidos; tirados) y "
                        "el ritmo del mosaico publicado. `fuentes`: lo que llega a la interfaz por clave (program, guest:N…). "
                        "`vistas`: cada video en pantalla (tomados, dibujados, salteados, tope_cps). `ventanas`: los cuadros "
                        "que pone cada ventana en la pantalla. `peor`: el video con más huecos.");
        t.examples = {R"({})"};
        t.run = [vmp](const json&, Done done) {
            auto* f = vmp->fluidez();
            if (f == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la medición de fluidez todavía no arrancó")));
            ToolResult r;
            r.data(f->estado());
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }

    // --- A mano: lo mismo que el menú de cada cara en la vista de las grillas ------------
    // (Propiedades de la fuente, Sala, Asamblea). Las claves salen de grilla_personas_estado.
    const json esquemaClave = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la persona: sala:<id> o asamblea:<id> (ver grilla_personas_estado)")}};
    const json esquemaFuente = {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "el nombre de la fuente ba_grilla")}};
    const auto aMano = [this, vmp](const char* nombre, const std::string& titulo, const std::string& descripcion, json props,
                                   std::vector<std::string> requeridos, std::vector<std::string> ejemplos,
                                   std::function<std::string(escenas::ControlGrillas&, const json&)> hacer,
                                   std::function<std::string(const json&)> listo) {
        Tool t;
        t.name = nombre;
        t.title = titulo;
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = descripcion;
        t.details = QT_TRANSLATE_NOOP("mcp", "Lo decidido a mano vive mientras la app está abierta (las claves son de la conexión de cada uno) y "
                    "se ve en la vista de las grillas: en las Propiedades de la fuente, en la Conferencia y en la Asamblea.");
        t.inputSchema = {{"type", "object"}, {"required", requeridos}, {"properties", props}};
        t.examples = std::move(ejemplos);
        t.run = [vmp, hacer, listo](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            if (const std::string e = hacer(*g, a); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(listo(a)));
        };
        core_.addTool(std::move(t));
    };
    const auto txt = [](const json& a, const char* k) { return a.value(k, std::string()); };
    aMano("grilla_personas_fijar", ba::i18n::traducir("mcp", "Fijar a alguien en una grilla de personas"),
          ba::i18n::traducir("mcp", "Fija a una persona en una grilla: va siempre en esa (antes que nadie), aunque los filtros digan otra cosa (al "
          "aire en otra fuente, sin cámara, otro origen). En su grupo no aparece en ninguna otra. Fijarla en otra grilla "
          "suelta la anterior."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"}, {R"({"fuente": "Grilla", "clave": "sala:3f2a"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.fijar(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» quedó fijo en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
    aMano("grilla_personas_sacar", ba::i18n::traducir("mcp", "Sacar a alguien de una grilla de personas"),
          ba::i18n::traducir("mcp", "Saca a una persona de una grilla (y de las de su grupo): no aparece ahí aunque los filtros la acepten. Si "
          "estaba fija ahí, deja de estarlo. Para que vuelva: grilla_personas_devolver."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"},
          {R"({"fuente": "Grilla", "clave": "asamblea:12"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.sacar(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» ya no aparece en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
    aMano("grilla_personas_mover", ba::i18n::traducir("mcp", "Mover a alguien de una grilla de personas a otra"),
          ba::i18n::traducir("mcp", "Pasa a una persona de una grilla a otra: la fija en `hacia` y, si las dos son de grupos distintos, la saca de "
          "`desde`."),
          {{"clave", esquemaClave},
           {"desde", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la grilla donde está")}}},
           {"hacia", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la grilla adonde va")}}}},
          {"clave", "desde", "hacia"}, {R"({"clave": "sala:3f2a", "desde": "Grilla", "hacia": "Grilla 2"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.mover(txt(a, "clave"), txt(a, "desde"), txt(a, "hacia")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» pasó de «%2» a «%3»."), {txt(a, "clave"), txt(a, "desde"), txt(a, "hacia")}); });
    aMano("grilla_personas_devolver", ba::i18n::traducir("mcp", "Devolver a alguien a lo automático en una grilla de personas"),
          ba::i18n::traducir("mcp", "Deshace lo hecho a mano con una persona en una grilla (fijarla o sacarla): vuelve a entrar o salir según los "
          "ajustes de la grilla."),
          {{"fuente", esquemaFuente}, {"clave", esquemaClave}}, {"fuente", "clave"},
          {R"({"fuente": "Grilla", "clave": "asamblea:12"})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.devolver(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) { return ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» vuelve a lo automático en «%2»."), {txt(a, "clave"), txt(a, "fuente")}); });
    {
        Tool t;
        t.name = "grilla_personas_escena_orador";
        t.title = ba::i18n::traducir("mcp", "Armar la escena del orador de una grilla de personas");
        t.category = "fuentes";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Crea (si no está) la escena «Orador · <grilla>» con una grilla de personas que muestra solo "
                        "al orador de esa grilla (muestra = orador), grande y con su nombre y cargo, en alta definición. Lo "
                        "que ya existe no se toca. NO la pone al aire: eso lo hace el operador o la línea de tiempo, en la "
                        "salida que quiera, mientras la grilla completa sigue en otra (al_hablar = escena).");
        t.inputSchema = {{"type", "object"}, {"required", {"fuente"}}, {"properties", {{"fuente", esquemaFuente}}}};
        t.examples = {R"({"fuente": "Grilla"})"};
        t.run = [vmp](const json& a, Done done) {
            auto* g = vmp->grillasPersonas();
            if (g == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la grilla de personas todavía no arrancó")));
            std::string escena, grilla;
            if (const std::string e = g->armarEscenaOrador(a.value("fuente", std::string()), &escena, &grilla); !e.empty())
                return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: la escena «%1» con la grilla «%2» (solo el orador)."),
                                                 {escena, grilla})));
        };
        core_.addTool(std::move(t));
    }
    aMano("grilla_personas_destacar", ba::i18n::traducir("mcp", "Elegir a mano el orador de una grilla de personas"),
          ba::i18n::traducir("mcp", "Hace orador de una grilla a una persona, en el acto y aunque el disparo diga otra cosa: se la muestra "
          "como diga «al_hablar» (destacada, resaltada, superpuesta o en la escena del orador) y se le pide su cámara en "
          "alta (entra con un fundido desde la baja). Con clave vacía, el orador lo vuelve a decidir el disparo."),
          {{"fuente", esquemaFuente},
           {"clave", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la persona (sala:<id> o asamblea:<id>); vacía = que lo decida el disparo")}}}},
          {"fuente"}, {R"({"fuente": "Grilla", "clave": "asamblea:12"})", R"({"fuente": "Grilla", "clave": ""})"},
          [txt](escenas::ControlGrillas& g, const json& a) { return g.destacar(txt(a, "fuente"), txt(a, "clave")); },
          [txt](const json& a) {
              return txt(a, "clave").empty()
                         ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: el orador de «%1» lo vuelve a decidir el disparo."), {txt(a, "fuente")})
                         : ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: «%1» es el orador de «%2»."), {txt(a, "clave"), txt(a, "fuente")});
          });
}

} // namespace ba::app::mcp
