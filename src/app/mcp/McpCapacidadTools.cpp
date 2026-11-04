// ============================================================================
// src/app/mcp/McpCapacidadTools.cpp — ¿cuántos invitados aguanta? (capacidad_*)
//
// Las mismas operaciones que la pestaña Configuración → Sistema → Capacidad
// (ui/CapacidadViewModel): arrancar una prueba (este equipo, otro equipo o la conexión a
// internet), recibir la de otro equipo, el estado en vivo, cancelar y el último resultado.
// Ver docs/funciones/CAPACIDAD.md.
// ============================================================================

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "i18n/I18nTexto.hpp"
#include "ipc/LugaresSala.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/CapacidadViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {
// Literales (el inventario del MCP lee el esquema del código) atados a los de la app.
constexpr auto kTopeInvitados = 512;
static_assert(kTopeInvitados == ba::ipc::kMaxInvitados, "el tope de invitados de la sala");
constexpr auto kRecibirMinutosMax = 60;
static_assert(kRecibirMinutosMax == CapacidadViewModel::kRecibirMaxMin, "lo más que se recibe una prueba");
} // namespace

void McpService::registerCapacidadTools() {
    CapacidadViewModel& cv = vm_.capacidad();
    {
        Tool t;
        t.name = "capacidad_arrancar";
        t.title = ba::i18n::traducir("mcp", "Arrancar una prueba de capacidad (cuántos invitados aguanta)");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Prueba cuántos invitados aguanta un sistema. `prueba`: «este_equipo» (invitados de prueba generados "
                   "acá contra esta misma conferencia, por escalones 10, 20, 40, 60, 80… hasta `tope` o hasta que se "
                   "degrade), «remoto» (lo mismo contra OTRO BroadcastAppliance que está recibiendo: `codigo` es el que "
                   "muestra el otro, empieza con BACAP1.) o «internet» (la conexión: el túnel WireGuard si está y el "
                   "camino de los invitados por pinggy; con `prender_pinggy` lo prende si está apagado y lo apaga al "
                   "terminar). Seguila con capacidad_estado.");
        t.details = QT_TRANSLATE_NOOP(
            "mcp", "Nunca corre sola: confirmalo con el operador (`confirmar: true`). No arranca con algo al aire, con "
                   "invitados de verdad conectados ni con otra prueba corriendo (`riesgo` en capacidad_estado). Cada "
                   "escalón dura 2 minutos como mucho; un escalón está «degradado» si la CPU sostenida pasa del 85 % "
                   "(sin la parte de hinchada si corre acá), queda poca memoria disponible, la grilla baja de 25 cuadros "
                   "por segundo, el audio de los invitados llega con más de 50 ms, no entran con video o la interfaz "
                   "se traba; la guardia corta antes de que la máquina se ahogue. Los de prueba quedan fuera del "
                   "padrón, la votación y el quórum y el evento abierto no se toca (si el cupo no alcanza se sube y "
                   "vuelve). El resultado va al historial de la máquina (capacidad_ultimo). Prender pinggy reinicia la "
                   "conferencia: solo sin nadie conectado.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"prueba", "confirmar"}},
                         {"properties",
                          {{"prueba", {{"type", "string"}, {"enum", {"este_equipo", "remoto", "internet"}}}},
                           {"tope", {{"type", "integer"}, {"minimum", 1}, {"maximum", kTopeInvitados}, {"default", 80},
                                     {"description", QT_TRANSLATE_NOOP("mcp", "hasta cuántos invitados sube la rampa")}}},
                           {"codigo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "remoto: el código que muestra el otro equipo (BACAP1.…)")}}},
                           {"prender_pinggy", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "internet: prender pinggy para medir si está apagado")}}},
                           {"confirmar", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "el operador confirmó que se puede cargar la máquina ahora")}}}}}};
        t.examples = {R"({"prueba": "este_equipo", "tope": 40, "confirmar": true})", R"({"prueba": "internet", "confirmar": true})",
                      R"({"prueba": "remoto", "codigo": "BACAP1.eyJ1Ijoi…", "tope": 80, "confirmar": true})"};
        t.run = [&cv](const json& a, Done done) {
            if (!a.value("confirmar", false))
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Confirmalo con el operador antes (confirmar: true): la prueba carga la máquina.")));
            const std::string prueba = a.value("prueba", std::string());
            QString e;
            if (prueba == "este_equipo") e = cv.probarEsteEquipo(a.value("tope", 80));
            else if (prueba == "remoto") e = cv.probarRemoto(QString::fromStdString(a.value("codigo", std::string())), a.value("tope", 80));
            else if (prueba == "internet") e = cv.medirInternet(a.value("prender_pinggy", false));
            else e = QString::fromStdString(ba::i18n::traducir("mcp", "`prueba` es este_equipo, remoto o internet"));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Prueba de capacidad arrancada. Seguila con capacidad_estado.")).data(cv.estadoJson(false)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capacidad_recibir";
        t.title = ba::i18n::traducir("mcp", "Dejar que otro equipo pruebe la capacidad de este");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Con `on`, este equipo se deja probar desde otro BroadcastAppliance: abre una credencial de prueba que "
                   "vence sola, sube el cupo a `cupo` y muestra el CÓDIGO para pegar en el otro (capacidad_estado → "
                   "recibir.codigo; trae la dirección, la credencial y la huella del certificado: pasalo solo al "
                   "operador). Mientras dura, le informa al otro su CPU, memoria, grilla y audio, y la guardia corta si "
                   "la máquina se ahoga. Con `on: false` termina (echa a los de prueba y devuelve el cupo). Confirmalo "
                   "con el operador.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"on"}},
                         {"properties",
                          {{"on", {{"type", "boolean"}}},
                           {"cupo", {{"type", "integer"}, {"minimum", 1}, {"maximum", kTopeInvitados}, {"default", 80}}},
                           {"minutos", {{"type", "integer"}, {"minimum", 1}, {"maximum", kRecibirMinutosMax}, {"default", 20}}}}}};
        t.run = [&cv](const json& a, Done done) {
            if (!a.value("on", false)) {
                cv.dejarDeRecibir();
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "Este equipo dejó de recibir la prueba.")));
            }
            const QString e = cv.empezarARecibir(a.value("cupo", 80), a.value("minutos", 20));
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Recibiendo: el código aparece en unos segundos en capacidad_estado (recibir.codigo).")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capacidad_estado";
        t.title = ba::i18n::traducir("mcp", "Cómo va la prueba de capacidad");
        t.category = "sistema";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true; // los nombres de las máquinas y lo medido por la red
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El estado de la prueba de capacidad: `estado` (libre, local, remoto, recibiendo, internet), `riesgo` "
                   "(por qué no se puede arrancar ahora), `progreso` (el escalón y la fase), `vivo` (CPU, memoria, "
                   "grilla, audio, cuántos entraron), `pasos` (cada escalón juzgado), `recibir` (con el código, si "
                   "este equipo está recibiendo), pinggy, y el resumen del último resultado y de la última conexión.");
        t.run = [&cv](const json&, Done done) {
            ToolResult r;
            r.data(cv.estadoJson(true));
            r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capacidad_cancelar";
        t.title = ba::i18n::traducir("mcp", "Cancelar la prueba de capacidad");
        t.category = "sistema";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "Corta lo que esté corriendo de la prueba de capacidad y limpia: corta hinchada, revoca la credencial "
                   "(el gateway echa a los de prueba), devuelve el cupo y, si la medición de internet prendió pinggy, lo "
                   "apaga. Lo medido hasta ahí queda en el historial.");
        t.run = [&cv](const json&, Done done) {
            if (!cv.ocupado()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay una prueba de capacidad en curso.")));
            cv.cancelar();
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Prueba de capacidad cancelada.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "capacidad_ultimo";
        t.title = ba::i18n::traducir("mcp", "El último resultado de capacidad");
        t.category = "sistema";
        t.clase = Clase::Lectura;
        t.untrustedOutput = true;
        t.description = QT_TRANSLATE_NOOP(
            "mcp", "El último resultado guardado, entero: `que` «capacidad» (de fábrica: el de ESTA máquina con cada "
                   "escalón, lo que limitó y la frase para el operador), «remoto» (el último de otro equipo) o "
                   "«internet» (la conexión: cada camino con lo medido y cuántos invitados entran). `historial`: la "
                   "lista de las pruebas guardadas (fecha, máquina, resultado).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"que", {{"type", "string"}, {"enum", {"capacidad", "remoto", "internet", "historial"}}}}}}};
        t.run = [&cv](const json& a, Done done) {
            const std::string que = a.value("que", std::string("capacidad"));
            json r;
            if (que == "internet") {
                r = cv.ultimoInternet();
            } else if (que == "historial") {
                r = json::parse(QJsonDocument(QJsonArray::fromVariantList(cv.historial())).toJson(QJsonDocument::Compact).toStdString(),
                                nullptr, false);
            } else if (que == "remoto") {
                const QVariantMap m = cv.resultado();
                if (m.value("tipo").toString() == QLatin1String("remoto"))
                    r = json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            } else {
                r = cv.ultimoDeEstaMaquina();
            }
            if (r.is_null() || r.empty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Todavía no hay un resultado de ese tipo (capacidad_arrancar).")));
            ToolResult res;
            res.data(r);
            res.untrusted = true;
            done(res);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
