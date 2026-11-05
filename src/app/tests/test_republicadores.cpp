// test_republicadores.cpp — A le vuelve a decir a B lo de estado cuando B no lo sabe (G-7).
//
// Un servidor local hace de B: atiende /api/v1/events (SSE) con el primer `room` de cada
// conexión ({epoch, reanudado}) y corta cuando se le dice. ConferenceClient:
//   1. con la primera conexión (B nueva para A) corre los republicadores;
//   2. una reconexión a la MISMA B que pudo reenviar lo perdido (reanudado) no republica,
//      y A manda Last-Event-ID con el id del último evento;
//   3. la misma B sin poder reenviar (reanudado false) republica;
//   4. otra B (epoch nuevo) republica;
//   5. registrarse con B ya en línea corre el republicador (una vez, en la vuelta siguiente);
//   6. si el dueño muere, deja de figurar.
//   7. el flujo lo lee un hilo propio (web 5): la última foto se lee desde cualquier hilo
//      sin I/O (fotoSala) y se vacía cuando B no está.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "platform/Paths.hpp"
#include "ui/ConferenceClient.hpp"

using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)

/// B de mentira: a cada conexión de eventos le manda el `room` que diga `proximo`.
struct FalsoB {
    QTcpServer srv;
    json proximo{{"epoch", "E1"}, {"reanudado", false}};
    std::string idProximo = "E1:3";
    QPointer<QTcpSocket> sse;  ///< la conexión de eventos de ahora
    std::string ultimoLastId;  ///< el Last-Event-ID que mandó A en la última
    int conexiones = 0;

    FalsoB() {
        srv.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&srv, &QTcpServer::newConnection, [this]() {
            while (QTcpSocket* s = srv.nextPendingConnection()) {
                auto buf = std::make_shared<std::string>();
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, buf]() {
                    *buf += s->readAll().toStdString();
                    const auto fin = buf->find("\r\n\r\n");
                    if (fin == std::string::npos) return;
                    const std::string cab = buf->substr(0, fin);
                    if (cab.rfind("GET /api/v1/events", 0) == 0) {
                        ++conexiones;
                        ultimoLastId.clear();
                        // Los nombres de cabecera no distinguen mayúsculas (RFC 9110), y desde
                        // Qt 6.7 QNetworkRequest los manda en minúsculas («last-event-id»).
                        std::string min = cab;
                        for (auto& ch : min) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        if (const auto p = min.find("last-event-id: "); p != std::string::npos)
                            ultimoLastId = cab.substr(p + 15, cab.find("\r\n", p) - p - 15);
                        sse = s;
                        const std::string r = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\nid: " + idProximo +
                                              "\nevent: room\ndata: " + proximo.dump() + "\n\n";
                        s->write(r.data(), static_cast<qint64>(r.size()));
                    } else {
                        const std::string r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n"
                                              "Connection: close\r\n\r\n{\"ok\":true}";
                        s->write(r.data(), static_cast<qint64>(r.size()));
                        s->disconnectFromHost();
                    }
                });
            }
        });
    }
    void cortar() {
        if (sse) sse->abort();
    }
};

/// Corre el bucle hasta que `listo` o `ms`.
bool esperar(const std::function<bool()>& listo, int ms) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return listo();
}
} // namespace

int main(int argc, char** argv) {
    QTemporaryDir dir;
    qputenv(ba::platform::kDataRootEnv, dir.path().toUtf8());
    QCoreApplication app(argc, argv);
    FalsoB b;
    std::filesystem::create_directories(ba::platform::stateDir());
    std::ofstream(ba::platform::stateDir() / "gateway-admin.json") << json{{"port", b.srv.serverPort()}, {"token", "tok"}}.dump();

    ba::app::ConferenceClient c;
    int veces = 0;
    auto dueno = std::make_unique<QObject>();
    c.registrarRepublicador("prueba", dueno.get(), [&veces]() { ++veces; });

    // 1) La primera conexión: una B que A no conoce.
    CHECK(esperar([&]() { return c.online(); }, 5000), "se conecta");
    esperar([&]() { return veces >= 1; }, 1000);
    CHECK(veces == 1 && c.epoch() == "E1", "primera conexión: republica");
    QString deOtroHilo;
    std::thread([&deOtroHilo]() { deOtroHilo = ba::app::ConferenceClient::fotoSala().value("epoch").toString(); }).join();
    CHECK(deOtroHilo == "E1", "la última foto se lee desde otro hilo (sin I/O)");

    // 2) La misma B, que pudo reenviar lo perdido.
    b.proximo = {{"epoch", "E1"}, {"reanudado", true}};
    b.idProximo = "E1:9";
    b.cortar();
    CHECK(esperar([&]() { return b.conexiones >= 2 && c.online(); }, 8000), "reconecta");
    esperar([]() { return false; }, 300);
    CHECK(b.ultimoLastId == "E1:3", "manda Last-Event-ID con el último id");
    CHECK(veces == 1, "la misma B que reanudó: no republica");

    // 3) La misma B, sin poder reenviar.
    b.proximo = {{"epoch", "E1"}, {"reanudado", false}};
    b.cortar();
    CHECK(esperar([&]() { return b.conexiones >= 3 && veces == 2; }, 8000), "la misma B sin reanudar: republica");

    // 4) Otra B.
    b.proximo = {{"epoch", "E2"}, {"reanudado", true}};
    b.cortar();
    CHECK(esperar([&]() { return b.conexiones >= 4 && veces == 3; }, 8000), "otra B (epoch nuevo): republica");
    CHECK(c.epoch() == "E2", "y se acuerda de cuál es");

    // 5) Registrarse con B en línea.
    int tarde = 0;
    QObject otro;
    c.registrarRepublicador("tarde", &otro, [&tarde]() { ++tarde; });
    CHECK(tarde == 0, "no corre adentro de registrarse");
    CHECK(esperar([&]() { return tarde == 1; }, 1000), "corre enseguida si B ya está");

    // 6) El dueño se va.
    CHECK(c.republicadores().contains("prueba"), "figura");
    dueno.reset();
    CHECK(!c.republicadores().contains("prueba"), "muerto el dueño, deja de figurar");

    // 7) Sin B, la foto no vale.
    b.srv.close();
    b.cortar();
    CHECK(esperar([&]() { return !c.online(); }, 5000) && ba::app::ConferenceClient::fotoSala().isEmpty(),
          "sin B, la foto queda vacía («no se sabe»)");

    std::printf(g_fallas == 0 ? "test_republicadores: OK\n" : "test_republicadores: %d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
