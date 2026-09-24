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
#include "mcp/McpPreguntaTools.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "mcp/McpCore.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

using json = nlohmann::json;

/// Tope de la imagen de fondo. La bajan TODOS los telefonos del salon: dos
/// megas por doscientos aparatos son cuatrocientos megas de la red del lugar,
/// que suele ser el wifi que tambien usa la transmision.
constexpr qint64 kMaxFondo = 2 * 1024 * 1024;

json aJson(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr,
                       false);
}

} // namespace

void registerPreguntaTools(McpCore& core, StudioViewModel& vm) {
    core.addCategory("preguntas", "Preguntas del publico",
                     "La cola de lo que pregunta la gente, del salon y de las plataformas, para "
                     "moderarla y sacarla al aire.");

    const auto salaDe = [&vm]() { return static_cast<ConferenceViewModel*>(vm.conference()); };

    /// Todo pasa por B. Si B no esta, se dice y no se inventa una cola vacia:
    /// «no hay preguntas» y «no puedo ver las preguntas» son cosas muy
    /// distintas para el que esta por salir al aire.
    const auto pedir = [salaDe](const QString& metodo, const QString& ruta,
                                const QJsonObject& cuerpo, Done done,
                                const std::string& textoOk) {
        ConferenceViewModel* conf = salaDe();
        if (conf == nullptr)
            return done(ToolResult::error("La sala no esta disponible en esta sesion."));
        const auto contestar = [done, textoOk](bool ok, const QJsonObject& r) {
            if (!ok)
                return done(ToolResult::error(
                    "No pude hablar con la sala (gateway). Fijate `room_status`: si esta caida, las "
                    "preguntas del publico no llegan a ningun lado."));
            ToolResult res;
            if (!textoOk.empty()) res.text(textoOk);
            res.structured = aJson(r);
            done(res);
        };
        if (metodo == "GET") conf->client().get(ruta, contestar);
        else conf->client().post(ruta, cuerpo, contestar);
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
        t.run = [pedir](const json&, Done done) {
            pedir("GET", "/api/v1/preguntas", {}, std::move(done), {});
        };
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
                {"enum", {"elegir", "des_elegir", "al_aire", "responder", "descartar", "limpiar",
                          "vaciar"}}}},
              {"id", {{"type", "string"}, {"description", "el id que da preguntas_ver"}}}}}};
        t.run = [pedir](const json& a, Done done) {
            const std::string accion = a.value("accion", std::string());
            const std::string id = a.value("id", std::string());
            const bool necesitaId = accion != "limpiar" && accion != "vaciar";
            if (necesitaId && id.empty())
                return done(ToolResult::error(
                    "Falta `id`: decime cual. Los ids los da `preguntas_ver`."));
            pedir("POST", "/api/v1/preguntas",
                  QJsonObject{{"accion", QString::fromStdString(accion)},
                              {"id", QString::fromStdString(id)}},
                  std::move(done), "Hecho.");
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
        t.inputSchema = {
            {"type", "object"},
            {"required", {"texto"}},
            {"properties",
             {{"texto", {{"type", "string"}}},
              {"nombre", {{"type", "string"}}},
              {"deDonde",
               {{"type", "string"},
                {"enum", {"sala", "publico", "youtube", "twitch", "facebook"}},
                {"description", "por donde llego"}}}}}};
        t.run = [pedir](const json& a, Done done) {
            pedir("POST", "/api/v1/preguntas",
                  QJsonObject{
                      {"accion", "preguntar"},
                      {"texto", QString::fromStdString(a.value("texto", std::string()))},
                      {"nombre", QString::fromStdString(a.value("nombre", std::string()))},
                      {"deDonde", QString::fromStdString(a.value("deDonde", std::string("sala")))}},
                  std::move(done), "Sumada a la cola.");
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
              {"fondo",
               {{"type", "string"},
                {"description", "ruta de una imagen del disco; vacio la saca"}}},
              {"remoto",
               {{"type", "boolean"},
                {"description", "el enlace por internet en vez del de la red local"}}}}}};
        t.run = [pedir, salaDe](const json& a, Done done) {
            ConferenceViewModel* conf = salaDe();
            if (conf == nullptr)
                return done(ToolResult::error("La sala no esta disponible en esta sesion."));
            const bool cambia = a.contains("titulo") || a.contains("cuando") || a.contains("fondo");
            const QString ruta = QString("/api/v1/publico-link?target=") +
                                 (a.value("remoto", false) ? "remote" : "local") + "&conClave=1";
            if (!cambia) return pedir("GET", ruta, {}, std::move(done), {});

            QJsonObject info;
            if (a.contains("titulo"))
                info["evento"] = QString::fromStdString(a["titulo"].get<std::string>());
            if (a.contains("cuando"))
                info["cuando"] = QString::fromStdString(a["cuando"].get<std::string>());
            QJsonObject cuerpo{{"info", info}};
            if (a.contains("fondo")) {
                const std::string ruta = a["fondo"].get<std::string>();
                if (ruta.empty()) {
                    cuerpo["fondo"] = QString();
                } else {
                    QFile f(QString::fromStdString(ruta));
                    if (!f.open(QIODevice::ReadOnly))
                        return done(ToolResult::error("No pude leer la imagen: " + ruta));
                    const QByteArray bytes = f.read(kMaxFondo + 1);
                    if (bytes.size() > kMaxFondo)
                        return done(ToolResult::error(
                            "Esa imagen pesa mas de 2 MB. La bajan todos los telefonos del salon: "
                            "achicala antes."));
                    cuerpo["fondo"] = QString::fromLatin1(bytes.toBase64());
                    cuerpo["fondoTipo"] =
                        QString::fromStdString(ruta).endsWith(".png", Qt::CaseInsensitive)
                            ? "image/png"
                            : "image/jpeg";
                }
            }
            pedir("POST", "/api/v1/publico-info", cuerpo,
                  [pedir, ruta, done](const ToolResult&) mutable {
                      // Despues de cambiarla, se devuelve el enlace: quien la
                      // configura casi siempre quiere el QR a continuacion.
                      pedir("GET", ruta, {}, std::move(done), {});
                  },
                  {});
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
