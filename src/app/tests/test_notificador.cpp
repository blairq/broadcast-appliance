// test_notificador.cpp — el webhook contra un servidor HTTP local; secretos cifrados aparte.
#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <fstream>

#include "agent/Notificador.hpp"

using ba::app::agent::Notificador;
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
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    Notificador n(dir.path().toStdString());

    QTcpServer srv;
    srv.listen(QHostAddress::LocalHost, 0);
    QByteArray recibido;
    QObject::connect(&srv, &QTcpServer::newConnection, [&]() {
        QTcpSocket* s = srv.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s, &recibido]() {
            recibido += s->readAll();
            if (recibido.contains("\r\n\r\n") && recibido.contains("}")) {
                s->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                s->disconnectFromHost();
            }
        });
    });

    CHECK(n.agregar({{"tipo", "fax"}}, "").contains("error"), "tipo inválido");
    const json r = n.agregar({{"tipo", "webhook"}, {"nombre", "Guardia"}}, "");
    const std::string id = r.value("id", std::string());
    CHECK(!id.empty() && n.lista()[0]["tiene_secreto"] == false, "sin secreto se crea, marcado");
    bool ok = true;
    std::string detalle;
    n.enviar(id, "t", "x", [&](bool o, const std::string& d) { ok = o; detalle = d; });
    CHECK(!ok && detalle.find("secreto") != std::string::npos, "sin secreto no manda");

    const std::string url = "http://127.0.0.1:" + std::to_string(srv.serverPort()) + "/hook";
    n.agregar({{"id", id}, {"tipo", "webhook"}, {"nombre", "Guardia"}}, url);
    CHECK(n.lista()[0]["tiene_secreto"] == true, "secreto cargado");
    {
        std::ifstream f(dir.path().toStdString() + "/Config/notificadores.json");
        const std::string cfg((std::istreambuf_iterator<char>(f)), {});
        CHECK(cfg.find("127.0.0.1") == std::string::npos, "la URL (secreto) no queda en el JSON de configuración");
    }
    QEventLoop l;
    bool llego = false;
    n.enviar(id, "Se cayó la transmisión", "Revisar la conexión", [&](bool o, const std::string&) {
        llego = o;
        l.quit();
    });
    QTimer::singleShot(5000, &l, &QEventLoop::quit);
    l.exec();
    CHECK(llego, "el webhook contestó 200");
    CHECK(recibido.contains("POST /hook") && recibido.contains("Se cay") && recibido.contains("\"origen\":\"broadcast-appliance\""),
          "POST JSON con título, texto y origen");
    CHECK(n.borrar(id) && n.lista().empty(), "borrar");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
