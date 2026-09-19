// test_operator_messages.cpp — barra del asistente al operador
//
//   avisos que vencen solos (y critical que no), preguntas con respuesta
//   (esperada por la herramienta o, si ya no espera, al chat del asistente),
//   respuestas sugeridas a invitados (se mandan por el chat de la sala a la
//   persona o a todos), descartar, y que lo cerrado no se pueda contestar.

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <functional>

#include "ui/OperatorMessages.hpp"

using ba::app::OperatorMessages;
using json = nlohmann::json;

namespace {
int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", msg, __LINE__);                                            \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

bool waitFor(const std::function<bool()>& ok, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!ok() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return ok();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_operator_messages ==\n");
    OperatorMessages m;
    std::vector<std::pair<QString, QString>> sent;
    m.setChatSender([&](const QString& to, const QString& text) {
        sent.emplace_back(to, text);
        return QString();
    });
    std::vector<json> late;
    m.setLateAnswerSink([&](const json& it) { late.push_back(it); });

    // Avisos: vencen solos; critical queda.
    const int info = m.post(json{{"kind", "message"}, {"text", "hola"}, {"ttlSec", 1}});
    const int crit = m.post(json{{"kind", "message"}, {"text", "urgente"}, {"priority", "critical"}});
    CHECK(m.count() == 2, "dos en la barra");
    CHECK(waitFor([&] { return m.get(info).value("status", "") == "expired"; }, 3000), "el aviso vence solo");
    CHECK(m.get(crit).value("status", "") == "open", "critical queda");
    CHECK(late.empty(), "los avisos no le llegan al asistente");
    m.dismiss(crit);
    CHECK(m.count() == 0, "cerrado");

    // Pregunta esperada por la herramienta.
    const int q = m.post(json{{"kind", "question"}, {"text", "¿Salimos?"}, {"options", {"Si", "No"}}});
    CHECK(!m.get(q).value("allowText", true), "con opciones: sin texto libre por defecto");
    json got;
    bool called = false;
    m.waitAnswer(q, 5000, [&](json it) {
        got = it;
        called = true;
    });
    m.answer(q, "Si");
    CHECK(called && got.value("answer", "") == "Si", "la herramienta recibe la respuesta");
    CHECK(late.empty(), "si la herramienta esperaba, no va al chat");
    m.answer(q, "No");
    CHECK(m.get(q).value("answer", "") == "Si", "lo cerrado no se vuelve a contestar");

    // Pregunta que vence en la herramienta: la respuesta va despues al chat.
    const int q2 = m.post(json{{"kind", "question"}, {"text", "¿Nombre del programa?"}});
    CHECK(m.get(q2).value("allowText", false), "sin opciones: texto libre");
    bool timedOut = false;
    m.waitAnswer(q2, 200, [&](json it) { timedOut = it.is_null(); });
    CHECK(waitFor([&] { return timedOut; }), "la espera vence");
    m.answer(q2, "Noticias");
    CHECK(late.size() == 1 && late[0].value("answer", "") == "Noticias", "la respuesta tardia llega al chat");

    // Respuestas sugeridas a un invitado.
    const int r = m.post(json{{"kind", "reply"},
                              {"participantId", "p7"},
                              {"participantName", "Ana"},
                              {"to", "p7"},
                              {"guestText", "¿Cuando salgo?"},
                              {"suggestions", {"En 5 minutos", "Ya casi"}}});
    CHECK(m.sendReply(r, "  ") != "", "vacio no se manda");
    CHECK(m.sendReply(r, "En 5 minutos").isEmpty(), "se manda");
    CHECK(sent.size() == 1 && sent[0].first == "p7" && sent[0].second == "En 5 minutos",
          "por privado a la persona");
    CHECK(late.size() == 2 && late[1].value("status", "") == "sent",
          "el asistente se entera de lo que se mando");
    CHECK(m.sendReply(r, "otra").contains("ya no"), "no se manda dos veces");
    const int r2 = m.post(
        json{{"kind", "reply"}, {"participantName", "Beto"}, {"to", "all"}, {"suggestions", {"Hola!"}}});
    m.sendReply(r2, "Hola!");
    CHECK(sent.size() == 2 && sent[1].first == "all", "si escribio en el general, se contesta en el general");
    const int r3 =
        m.post(json{{"kind", "reply"}, {"participantName", "Caro"}, {"to", "p9"}, {"suggestions", {"x"}}});
    m.dismiss(r3);
    CHECK(late.size() == 4 && late[3].value("status", "") == "dismissed",
          "ignorar tambien le llega al asistente");
    CHECK(m.list(true).size() == 7 && m.list(false).empty(), "historial completo y barra vacia");

    // Formulario de credencial: los valores van al almacen, nunca al item.
    QString gotPlatform;
    QVariantMap gotValues;
    QString sinkError = "El ID no parece valido.";
    m.setCredentialSink([&](const QString& platform, const QVariantMap& values) {
        gotPlatform = platform;
        gotValues = values;
        return sinkError;
    });
    const int c1 = m.post(json{{"kind", "credential"},
                               {"platform", "twitch"},
                               {"fields", {{{"key", "clientId"}, {"label", "ID"}, {"secret", false}}}}});
    CHECK(m.get(c1).value("fields", json::array()).size() == 1, "el item lleva los campos del formulario");
    const QVariantMap vals{{"clientId", "abc123"}, {"clientSecret", "SECRETO-XYZ"}};
    CHECK(m.submitCredential(c1, vals) == "El ID no parece valido.", "el error vuelve al formulario");
    CHECK(m.get(c1).value("status", "") == "open", "con error el formulario sigue abierto");
    sinkError.clear();
    CHECK(m.submitCredential(c1, vals).isEmpty(), "guardado");
    CHECK(gotPlatform == "twitch" && gotValues.value("clientSecret") == "SECRETO-XYZ",
          "el sink recibe los valores");
    CHECK(m.get(c1).value("status", "") == "saved", "queda como guardado");
    CHECK(m.get(c1).dump().find("SECRETO") == std::string::npos &&
              m.get(c1).dump().find("abc123") == std::string::npos,
          "el item NO guarda los valores");
    CHECK(late.size() == 5 && late[4].value("kind", "") == "credential" &&
              late[4].dump().find("SECRETO") == std::string::npos,
          "el asistente se entera de que se guardo, sin los datos");
    CHECK(m.submitCredential(c1, vals).contains("ya no"), "no se guarda dos veces");
    const int q3 = m.post(json{{"kind", "question"}, {"text", "?"}});
    CHECK(m.submitCredential(q3, vals).contains("ya no"), "solo los formularios de credencial guardan");

    if (g_failures == 0) std::printf("test_operator_messages: OK\n");
    return g_failures == 0 ? 0 : 1;
}
