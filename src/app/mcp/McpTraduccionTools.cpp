// ============================================================================
// src/app/mcp/McpTraduccionTools.cpp — traducción en vivo y su glosario
//
// De una salida de texto (lo que transcribe una «Voz a texto») a otra en otro
// idioma, sin red (agent/Traductor). El glosario fija cómo se traducen los
// términos del evento: lo maneja el asistente con glosario_* o editando el
// archivo que dice glosario_ver (Datos/Config/glosario-traduccion.json).
// ============================================================================

#include "i18n/I18n.hpp"
#include "agent/Subtitulos.hpp"
#include "agent/Traductor.hpp"
#include "agent/Voz.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

#include <QPointer>

#include <fstream>
#include <string>

namespace ba::app::mcp {

void McpService::registerTraduccionTools() {
    agent::Traductor* tr = vm_.traductorAgent();
    if (tr == nullptr) return;
    const QPointer<agent::Traductor> t = tr;
    core_.addCategory("traduccion", ba::i18n::traducir("mcp", "Traducción en vivo"),
                      ba::i18n::traducir("mcp", "Traducir lo que se dice (una salida de texto de «Voz a texto») a inglés, portugués, "
                      "francés, alemán, ruso "
                      "o chino, sin red, para subtítulos y para que una voz lo diga en otro idioma; y el "
                      "glosario de términos "
                      "del evento."));
    const json idioma = {{"type", "string"}, {"enum", {"es", "en", "pt", "fr", "de", "ru", "zh"}}};
    {
        Tool x;
        x.name = "traducciones";
        x.title = ba::i18n::traducir("mcp", "Traducciones en vivo: cuáles hay y cómo andan");
        x.category = "traduccion";
        x.clase = Clase::Lectura;
        x.description = ba::i18n::traducir("mcp", "Las traducciones configuradas (de qué salida de texto, a qué idioma, en qué salida "
                        "publican, la última frase y su "
                        "demora), si falta bajar algún paquete, el estado del motor y los idiomas: `origen` "
                        "(el del evento, de donde se traduce si no se dice otro) y `destinos` (a cuáles hay "
                        "paquete desde ahí). Receta "
                        "completa: help {\"topic\": "
                        "\"traduccion\"}.");
        x.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        x.run = [t](const json&, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            json idiomas = json::object();
            for (const auto& [k, v] : agent::Traductor::nombresIdiomas())
                idiomas[k] = ba::i18n::traducir("idiomas", v.c_str()); // vienen con QT_TRANSLATE_NOOP
            const std::string origen = agent::Traductor::origen();
            done(ToolResult().data({{"traducciones", t->lista()},
                                    {"motor", t->motor().toStdString()},
                                    {"idiomas", idiomas},
                                    {"origen", origen},
                                    {"destinos", t->destinos(origen)}}));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "traducir";
        x.title = ba::i18n::traducir("mcp", "Traducir una frase ahora");
        x.category = "traduccion";
        x.clase = Clase::Lectura;
        x.description = ba::i18n::traducir("mcp", "Traduce `texto` (en `de`, el idioma del evento si no se dice) a `a`, con el glosario. Para "
                        "probar cómo sale "
                        "un término antes del evento.");
        x.inputSchema = {{"type", "object"},
                         {"required", {"texto", "a"}},
                         {"properties", {{"texto", {{"type", "string"}}}, {"a", idioma}, {"de", idioma}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            t->traducir(QString::fromStdString(a.value("texto", std::string())),
                        a.value("de", std::string()), a.value("a", std::string()), [done](json r) {
                            if (r.contains("error"))
                                return done(ToolResult::error(r["error"].get<std::string>()));
                            done(ToolResult().data(r));
                        });
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "traduccion_agregar";
        x.title = ba::i18n::traducir("mcp", "Traducir en vivo una salida de texto");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description =
            ba::i18n::traducir("mcp", "Cada frase de la salida `desde` (p. ej. «Orador», la de una tarjeta «Voz a texto»: "
            "texto_flujos) se traduce a `a` y "
            "se publica en la salida `nombre` («<desde> · <a>» si no se dice). Esa salida se usa como "
            "cualquier otra: "
            "texto_a_voz {lee} con una voz del idioma, subtítulos en un `rotulo` (fuente de texto de la "
            "escena), `archivo` "
            "(.txt y .srt). Queda como una tarjeta «Traducir» en el rack de audio de `fuente` (si no se "
            "dice, el de la voz a "
            "texto que publica `desde`); su `id` es «<fuente>#<nodo>». `espera_ms`: cuánto espera el fin de "
            "la oración (600); `maximo_ms`: lo más que junta sin punto antes de traducir (8000). "
            "Si falta bajar un paquete, traduccion_bajar.");
        x.inputSchema = {
            {"type", "object"},
            {"required", {"desde", "a"}},
            {"properties",
             {{"desde", {{"type", "string"}}},
              {"a", idioma},
              {"idioma_desde", idioma},
              {"nombre", {{"type", "string"}}},
              {"fuente",
               {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "la fuente de audio en cuyo rack va la tarjeta")}}},
              {"rotulo", {{"type", "string"}}},
              {"archivo", {{"type", "boolean"}}},
              {"espera_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 10000}}},
              {"maximo_ms", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 60000}}}}}};
        x.examples = {R"({"desde": "Orador", "a": "en", "rotulo": "Subtítulos EN"})"};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            json hecha;
            const std::string e =
                t->agregar(a.value("desde", std::string()), a.value("idioma_desde", std::string()),
                           a.value("a", std::string()), a.value("nombre", std::string()), a, &hecha);
            if (!e.empty()) return done(ToolResult::error(e));
            hecha["faltan"] =
                t->faltan(hecha.value("idioma_desde", std::string()), hecha.value("a", std::string()));
            done(ToolResult().data(hecha));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "traduccion_ajustar";
        x.title = ba::i18n::traducir("mcp", "Pausar o cambiar una traducción");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description = ba::i18n::traducir("mcp", "Cambia una traducción (`id` de `traducciones`, «<fuente>#<nodo>» de su tarjeta): "
                        "`activa` (pausarla), "
                        "`desde`, `idioma_desde`, `a`, `nombre`, `rotulo`, `archivo`, `espera_ms`, "
                        "`maximo_ms`. Con `quitar: true` saca la tarjeta.");
        x.inputSchema = {{"type", "object"},
                         {"required", {"id"}},
                         {"properties",
                          {{"id", {{"type", "string"}}},
                           {"activa", {{"type", "boolean"}}},
                           {"desde", {{"type", "string"}}},
                           {"idioma_desde", idioma},
                           {"a", idioma},
                           {"rotulo", {{"type", "string"}}},
                           {"archivo", {{"type", "boolean"}}},
                           {"nombre", {{"type", "string"}}},
                           {"espera_ms", {{"type", "integer"}, {"minimum", 100}, {"maximum", 10000}}},
                           {"maximo_ms", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 60000}}},
                           {"quitar", {{"type", "boolean"}}}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            const std::string id = a.value("id", std::string());
            const std::string e = a.value("quitar", false) ? t->quitar(id) : t->ajustar(id, a);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"traducciones", t->lista()}}));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "traduccion_bajar";
        x.title = ba::i18n::traducir("mcp", "Bajar los paquetes de un idioma");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description = ba::i18n::traducir("mcp", "Baja los paquetes que hacen falta para traducir de `de` (el idioma del evento si no se "
                        "dice) a `a` (uno o dos: "
                        "sin paquete directo, pasa por otro idioma, casi siempre el inglés). "
                        "Entre 70 y 300 MB "
                        "cada uno; se ve el avance en modelo_local_estado.");
        x.inputSchema = {
            {"type", "object"}, {"required", {"a"}}, {"properties", {{"a", idioma}, {"de", idioma}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            const std::string de = a.value("de", std::string()), para = a.value("a", std::string()); // "" = el del evento
            const json faltaban = t->faltan(de, para);
            if (const std::string e = t->bajar(de, para); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data({{"bajando", faltaban},
                                    {"nota", faltaban.empty() ? ba::i18n::traducir("mcp", "ya estaba todo") : ba::i18n::traducir("mcp", "se bajan de a uno")}}));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "voz_cortes";
        x.title = ba::i18n::traducir("mcp", "Cuándo corta una frase la transcripción continua");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description = ba::i18n::traducir("mcp", "Sin argumentos: los cortes de la transcripción continua (la que alimenta subtítulos "
                        "y traducción) y lo que oye cada "
                        "una ahora. Con `pausa` (silencio que corta, 0,1-3 s; 0,35 típico), `maximo` (largo "
                        "máximo de frase, 2-30 s; al "
                        "pasarlo corta en la próxima pausa corta; 6 típico) y/o `umbral` (0,1-0,95; más alto "
                        "ignora el ruido de la sala; "
                        "0,5 típico): los cambia y se guardan. Si el operador dice que «no corta nunca», "
                        "bajá `maximo` y subí `umbral`.");
        x.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"pausa", {{"type", "number"}}},
                           {"maximo", {{"type", "number"}}},
                           {"umbral", {{"type", "number"}}}}}};
        x.run = [vmp = &vm_](const json& a, Done done) {
            auto* vz = qobject_cast<agent::Voz*>(vmp->voz());
            if (vz == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "la voz no está")));
            QVariantMap c = vz->cortes();
            if (a.contains("pausa") || a.contains("maximo") || a.contains("umbral")) {
                const QString e = vz->setCortes(a.value("pausa", c.value("pausa").toDouble()),
                                                a.value("maximo", c.value("maximo").toDouble()),
                                                a.value("umbral", c.value("umbral").toDouble()));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                c = vz->cortes();
            }
            json oido = json::array();
            for (const QVariant& v : vz->oido()) {
                const QVariantMap m = v.toMap();
                oido.push_back({{"flujo", m.value("flujo").toString().toStdString()},
                                {"hablando", m.value("hablando").toBool()},
                                {"segundos", m.value("segundos").toDouble()}});
            }
            done(ToolResult().data({{"pausa", c.value("pausa").toDouble()},
                                    {"maximo", c.value("maximo").toDouble()},
                                    {"umbral", c.value("umbral").toDouble()},
                                    {"oido", oido}}));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "subtitulos_limpiar";
        x.title = ba::i18n::traducir("mcp", "Borrar ya los subtítulos de pantalla");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description =
            ba::i18n::traducir("mcp", "Vacía los cuadros «Subtítulos» (ba_subtitulos) de las escenas: el de nombre `nombre`, o todos "
            "los que muestran la salida `flujo`, o todos. Lo que llegue después se muestra normal.");
        x.inputSchema = {{"type", "object"},
                         {"properties", {{"nombre", {{"type", "string"}}}, {"flujo", {{"type", "string"}}}}}};
        x.run = [](const json& a, Done done) {
            agent::Subtitulos* sub = agent::Subtitulos::activo();
            const int cuantos =
                sub != nullptr ? sub->limpiar(a.value("nombre", std::string()), a.value("flujo", std::string())) : 0;
            if (cuantos == 0) return done(ToolResult::error(ba::i18n::traducir("mcp", "no hay cuadros «Subtítulos» que coincidan")));
            done(ToolResult().text(ba::i18n::traducir("mcp", "Borrado %n cuadro de subtítulos.", nullptr, cuantos)));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "glosario_ver";
        x.title = ba::i18n::traducir("mcp", "El glosario de la traducción");
        x.category = "traduccion";
        x.clase = Clase::Lectura;
        x.description = ba::i18n::traducir("mcp", "Los términos que la traducción respeta (castellano -> en, pt, fr, de, ru, zh), por "
                        "grupo («general», "
                        "«asamblea», «operador»), cuáles están activos, y `archivo`: el JSON del "
                        "operador, que también se puede "
                        "editar directamente (se suma al de fábrica y lo pisa). Con `buscar` filtra.");
        x.inputSchema = {{"type", "object"},
                         {"properties", {{"buscar", {{"type", "string"}}}, {"grupo", {{"type", "string"}}}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            const QString b = QString::fromStdString(a.value("buscar", std::string())).toLower();
            const std::string g = a.value("grupo", std::string());
            json out = json::array();
            for (const auto& e : t->glosario()) {
                if (!g.empty() && e.value("grupo", "") != g) continue;
                if (!b.isEmpty() && !QString::fromStdString(e.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)).toLower().contains(b)) continue;
                out.push_back(e);
            }
            done(ToolResult().data(
                {{"terminos", out}, {"cuantos", out.size()}, {"archivo", t->archivoGlosario().string()}}));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "glosario_exportar";
        x.title = ba::i18n::traducir("mcp", "Exportar el glosario para revisión");
        x.category = "traduccion";
        x.clase = Clase::Lectura;
        x.description = ba::i18n::traducir("mcp", "El glosario en CSV («es;en;pt;fr;de;ru;zh;grupo;revisado_por», las formas en castellano "
                        "separadas por «|») para que lo revise la institución. Con `archivo`, lo escribe ahí.");
        x.inputSchema = {{"type", "object"}, {"properties", {{"archivo", {{"type", "string"}}}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            const std::string csv = t->glosarioCsv();
            const std::string ruta = a.value("archivo", std::string());
            if (!ruta.empty()) {
                std::ofstream o(ruta);
                if (!o) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "no pude escribir %1"), {ruta})));
                o << csv;
                return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Escrito en %1"), {ruta})));
            }
            done(ToolResult().text(csv));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "glosario_importar";
        x.title = ba::i18n::traducir("mcp", "Importar el glosario revisado");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description = ba::i18n::traducir("mcp", "Vuelve el CSV revisado (`csv`, o `archivo`): cada fila suma o pisa un término con la marca "
                        "`revisado_por` (quién lo revisó: obligatorio).");
        x.inputSchema = {{"type", "object"},
                         {"required", {"revisado_por"}},
                         {"properties",
                          {{"csv", {{"type", "string"}}}, {"archivo", {{"type", "string"}}}, {"revisado_por", {{"type", "string"}}}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            std::string csv = a.value("csv", std::string());
            if (csv.empty() && a.contains("archivo")) {
                std::ifstream f(a.value("archivo", std::string()));
                if (!f) return done(ToolResult::error(ba::i18n::traducir("mcp", "no pude leer el archivo")));
                csv.assign(std::istreambuf_iterator<char>(f), {});
            }
            int n = 0;
            const std::string e = t->glosarioImportarCsv(csv, a.value("revisado_por", std::string()), &n);
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Importado %n término revisado.", nullptr, n)));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "traduccion_rotulo";
        x.title = ba::i18n::traducir("mcp", "Rotular la traducción automática");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description = ba::i18n::traducir("mcp", "Mientras salga una traducción en un cuadro de subtítulos o en los subtítulos ocultos, una "
                        "capa DSK muestra «Traducción automática» (prendido de fábrica; se va 15 s despues de la ultima "
                        "frase). `activo` lo prende o apaga; sin argumentos, el estado.");
        x.inputSchema = {{"type", "object"}, {"properties", {{"activo", {{"type", "boolean"}}}}}};
        x.run = [this](const json& a, Done done) {
            if (a.contains("activo")) vm_.asistente().setRotuloTraduccion(a.value("activo", true));
            done(ToolResult().data(vm_.asistente().rotuloTraduccionEstado()));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "glosario_agregar";
        x.title = ba::i18n::traducir("mcp", "Sumar o corregir un término del glosario");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description =
            ba::i18n::traducir("mcp", "Un término del evento y cómo se dice en cada idioma: `es` (la forma en castellano, o una lista "
            "de formas: "
            "singular, plural, femenino), y al menos una de en, pt, fr, de, ru, zh. Si ya existía (aunque "
            "sea de fábrica), lo "
            "pisa. Nombres propios que no se traducen: poné el mismo nombre. Probalo con traducir.");
        x.inputSchema = {
            {"type", "object"},
            {"required", {"es"}},
            {"properties",
             {{"es",
               {{"anyOf", {{{"type", "string"}}, {{"type", "array"}, {"items", {{"type", "string"}}}}}}}},
              {"en", {{"type", "string"}}},
              {"pt", {{"type", "string"}}},
              {"fr", {{"type", "string"}}},
              {"de", {{"type", "string"}}},
              {"ru", {{"type", "string"}}},
              {"zh", {{"type", "string"}}},
              {"grupo", {{"type", "string"}}}}}};
        x.examples = {R"({"es": ["Banca del Vecino"], "en": "Citizens' Seat", "pt": "Cadeira do Cidadão"})"};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            if (const std::string e = t->glosarioAgregar(a); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::llenar(ba::i18n::traducir("mcp", "Agregado al glosario (%1)."), {t->archivoGlosario().string()})));
        };
        core_.addTool(std::move(x));
    }
    {
        Tool x;
        x.name = "glosario_quitar";
        x.destructive = true;
        x.title = ba::i18n::traducir("mcp", "Sacar un término del glosario (o apagar un grupo)");
        x.category = "traduccion";
        x.clase = Clase::Escritura;
        x.description =
            ba::i18n::traducir("mcp", "Saca el término `es` (uno de fábrica queda apagado, no se borra). Con `grupo` y `activo`, "
            "prende o "
            "apaga un grupo entero (p. ej. «asamblea» fuera de una asamblea).");
        x.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"es", {{"type", "string"}}},
                           {"grupo", {{"type", "string"}}},
                           {"activo", {{"type", "boolean"}}}}}};
        x.run = [t](const json& a, Done done) {
            if (!t) return done(ToolResult::error(ba::i18n::traducir("mcp", "el traductor no está")));
            std::string e;
            if (a.contains("grupo"))
                e = t->glosarioGrupo(a.value("grupo", std::string()), a.value("activo", false));
            else if (a.contains("es"))
                e = t->glosarioQuitar(a.value("es", std::string()));
            else
                e = ba::i18n::traducir("mcp", "falta `es` (un término) o `grupo`");
            if (!e.empty()) return done(ToolResult::error(e));
            done(ToolResult().text(ba::i18n::traducir("mcp", "Hecho.")));
        };
        core_.addTool(std::move(x));
    }
}

} // namespace ba::app::mcp
