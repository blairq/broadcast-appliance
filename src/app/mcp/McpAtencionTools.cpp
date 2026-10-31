// ============================================================================
// src/app/mcp/McpAtencionTools.cpp — la atención a quien se conecta, por MCP
//
// Lo mismo que los interruptores de Sala, Asamblea y Personas y la pestaña
// «Voz del atendedor» (ui/AtencionViewModel, docs/funciones/ATENCION.md):
//
//   atencion_estado       quién atiende, de qué forma, a quién, con qué voz
//   atencion_configurar   «Los atiende el asistente», «Hablando» (sala y
//                         asamblea), alta automática en Personas, la voz
//                         del atendedor
//   atencion_voz_probar   genera una frase con esa voz SIN que suene: duración,
//                         forma de onda y un .wav para escucharlo aparte
//
// La lista de quién entra solo si se lo reconoce NO está acá: es la del padrón
// del evento (padron_ver {solos: true}, padron_cambiar {id, dejarPasarSolo}),
// la misma que usa la interfaz. Dos listas para lo mismo se desincronizan.
// ============================================================================
#include "i18n/I18n.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

void McpService::registerAtencionTools() {
    StudioViewModel* vm = &vm_;
    const auto atencion = [vm]() { return vm->atencionVm(); };
    const auto sinAtencion = []() {
        return ToolResult::error(ba::i18n::traducir("mcp", "Esta compilación no tiene la atención a quien se conecta (falta el motor de voz)."));
    };
    {
        Tool t;
        t.name = "atencion_estado";
        t.title = ba::i18n::traducir("mcp", "Cómo se atiende a quien se conecta");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Si el asistente atiende a los que llegan (conferencia y asamblea), si lo hace hablando, quién "
                        "atiende (recepción o automatización), a quién está atendiendo y de qué forma, el alta "
                        "automática en Personas y la voz del atendedor.");
        t.details = QT_TRANSLATE_NOOP("mcp", "QUIÉN ATIENDE: el rol «recepcion» si está prendido; si no, el asistente de automatización; "
                    "nunca el del evento. CUÁNDO TERMINA: al admitirla (o al irse): la atención es del lobby. "
                    "HABLANDO: de a uno (hay un solo canal de voz del que atiende, para el lobby y la sesión "
                    "remota); los demás, por chat (en la asamblea, sin voz: mirándole la cara). `conLaVoz` y "
                    "`conLaVozAmbito` dicen quién la tiene. En la asamblea, al que espera se le habla con "
                    "`chat_send {to: <id del integrante>}` y se lo oye con `chat_wait {participant: <id>}`.");
        t.run = [atencion, sinAtencion](const json&, Done done) {
            AtencionViewModel* a = atencion();
            if (a == nullptr) return done(sinAtencion());
            done(ToolResult().data(a->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "atencion_configurar";
        t.title = ba::i18n::traducir("mcp", "Prender o apagar la atención a quien se conecta");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.idempotent = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Cambia lo que venga: `salaAtiende` («Los atiende el asistente» en la conferencia), "
                        "`salaHablando` (lo que dice se pasa a texto y se le contesta con voz), `asambleaAtiende` "
                        "(mira la cara de quien espera en la sesión remota y lo habilita si el padrón lo deja "
                        "entrar solo; si no, le pregunta al operador), `asambleaHablando` (además, mientras espera "
                        "su página abre el micrófono: lo que dice pasa a texto y se le contesta con voz, solo a "
                        "él; termina al habilitarlo), `altaAutomatica` (anota en Personas a quien "
                        "dio su nombre y tiene cara reconocible) y `voz` {repo, hablante, velocidad} (la voz del "
                        "atendedor).");
        t.details = QT_TRANSLATE_NOOP("mcp", "Prender la atención sin quién atienda (ni recepción ni automatización) da error. Con la de "
                    "la conferencia prendida, el reconocimiento del lobby corre siempre. Quién entra solo si se lo "
                    "reconoce se cambia en el padrón del evento: padron_cambiar {id, dejarPasarSolo}.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"salaAtiende", {{"type", "boolean"}}},
                           {"salaHablando", {{"type", "boolean"}}},
                           {"asambleaAtiende", {{"type", "boolean"}}},
                           {"asambleaHablando", {{"type", "boolean"}}},
                           {"altaAutomatica", {{"type", "boolean"}}},
                           {"voz",
                            {{"type", "object"},
                             {"properties",
                              {{"repo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "una voz bajada (tipo tts)")}}},
                               {"hablante", {{"type", "integer"}, {"minimum", 0}}},
                               {"velocidad", {{"type", "number"}, {"minimum", 0.7}, {"maximum", 1.5}}}}}}}}}};
        t.examples = {R"({"salaAtiende": true, "salaHablando": true})",
                      R"({"asambleaAtiende": true, "asambleaHablando": true})", R"({"altaAutomatica": true})",
                      R"({"voz": {"velocidad": 1.1}})"};
        t.run = [atencion, sinAtencion](const json& a, Done done) {
            AtencionViewModel* at = atencion();
            if (at == nullptr) return done(sinAtencion());
            const QString e = at->configurar(a);
            if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            done(ToolResult().data(at->estado()));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "atencion_voz_probar";
        t.title = ba::i18n::traducir("mcp", "Probar la voz del atendedor sin que suene");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Genera `texto` (o una frase de ejemplo) con la voz del atendedor SIN reproducirlo: devuelve "
                        "cuánto dura, su forma de onda (picos de 0 a 1) y dónde quedó el .wav para escucharlo "
                        "aparte.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"texto", {{"type", "string"}}}}}};
        t.run = [atencion, sinAtencion](const json& a, Done done) {
            AtencionViewModel* at = atencion();
            if (at == nullptr) return done(sinAtencion());
            at->probar(QString::fromStdString(a.value("texto", std::string())), [done](const QVariantMap& p) {
                if (!p.value("error").toString().isEmpty())
                    return done(ToolResult::error(p.value("error").toString().toStdString()));
                json forma = json::array();
                for (const QVariant& v : p.value("forma").toList())
                    forma.push_back(std::round(v.toDouble() * 100.0) / 100.0);
                done(ToolResult().data({{"segundos", p.value("segundos").toDouble()},
                                        {"archivo", p.value("archivo").toString().toStdString()},
                                        {"forma", forma}}));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
