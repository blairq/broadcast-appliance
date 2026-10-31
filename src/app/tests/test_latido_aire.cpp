// test_latido_aire.cpp — la alarma «interfaz trabada con aire» sale SIN la interfaz (F1-13).
//
// El hilo principal hace de interfaz: late (uiTick) y después se traba con un bucle
// ocupado, sin atender eventos. Un servidor HTTP local en su propio hilo hace de
// gateway (POST /api/v1/control/enviar) y de webhook de un notificador. Se mira:
//   - con aire, la alarma llega al gateway (a todas las conexiones) y al webhook en
//     ≤ 3 s desde la última señal de la interfaz, mientras sigue trabada;
//   - el latido no se corta (el motor avanza): el supervisor no mata la app;
//   - al volver la interfaz, el aviso de que terminó;
//   - sin aire, una traba no es alarma;
//   - con `notificar: []`, nada a los notificadores (al control web, igual).
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "agent/Notificador.hpp"
#include "emision/AvisoFueraDeUi.hpp"
#include "emision/LatidoAire.hpp"
#include "platform/Paths.hpp"

using ba::app::AvisoFueraDeUi;
using ba::app::LatidoAire;
using json = nlohmann::json;
using reloj = std::chrono::steady_clock;

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

/// Lo que llegó al servidor: {ruta, cuerpo, cuándo}.
struct Pedido {
    std::string ruta, cuerpo;
    reloj::time_point cuando;
};
std::mutex g_mu;
std::vector<Pedido> g_pedidos;

std::vector<Pedido> pedidos(const std::string& ruta) {
    std::lock_guard<std::mutex> l(g_mu);
    std::vector<Pedido> out;
    for (const auto& p : g_pedidos)
        if (p.ruta == ruta) out.push_back(p);
    return out;
}

/// Un servidor HTTP mínimo en su hilo (el principal se va a trabar a propósito).
class Servidor : public QThread {
   public:
    std::atomic<int> puerto{0};
    void run() override {
        QTcpServer srv;
        srv.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&srv, &QTcpServer::newConnection, [&]() {
            QTcpSocket* s = srv.nextPendingConnection();
            auto buf = std::make_shared<QByteArray>();
            QObject::connect(s, &QTcpSocket::readyRead, [s, buf]() {
                *buf += s->readAll();
                const int fin = buf->indexOf("\r\n\r\n");
                if (fin < 0) return;
                const QByteArray cab = buf->left(fin);
                const int i = cab.toLower().indexOf("content-length:");
                const int largo = i < 0 ? 0 : cab.mid(i + 15, cab.indexOf("\r\n", i) - i - 15).trimmed().toInt();
                if (buf->size() < fin + 4 + largo) return;
                const QByteArray linea = cab.left(cab.indexOf("\r\n"));
                {
                    std::lock_guard<std::mutex> l(g_mu);
                    g_pedidos.push_back({linea.split(' ').value(1).toStdString(), buf->mid(fin + 4, largo).toStdString(), reloj::now()});
                }
                s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}");
                s->disconnectFromHost();
            });
        });
        puerto = srv.serverPort();
        exec();
    }
};

/// La interfaz trabada: el hilo principal sin atender nada durante `ms`.
void trabar(int ms) {
    const auto fin = reloj::now() + std::chrono::milliseconds(ms);
    while (reloj::now() < fin) {
    }
}

/// Espera (atendiendo eventos) hasta que `cond` o se acaba el tiempo.
template <class F>
bool esperar(F cond, int ms) {
    const auto fin = reloj::now() + std::chrono::milliseconds(ms);
    while (reloj::now() < fin) {
        if (cond()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

long long msEntre(reloj::time_point a, reloj::time_point b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}
} // namespace

int main(int argc, char** argv) {
    QTemporaryDir dir;
    // Todo dentro de la carpeta temporal: Runtime/gateway-admin.json, Config/, Credenciales/.
    qputenv(ba::platform::kDataRootEnv, dir.path().toUtf8());
    QCoreApplication app(argc, argv);
    const std::filesystem::path datos = dir.path().toStdString();

    Servidor srv;
    srv.start();
    while (srv.puerto == 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const std::string base = "http://127.0.0.1:" + std::to_string(srv.puerto.load());
    std::filesystem::create_directories(ba::platform::stateDir());
    std::ofstream(ba::platform::stateDir() / "gateway-admin.json") << json{{"port", srv.puerto.load()}, {"token", "tok"}}.dump();
    {
        ba::app::agent::Notificador n(datos);
        CHECK(!n.agregar({{"tipo", "webhook"}, {"nombre", "Guardia"}}, base + "/hook").contains("error"), "un notificador (webhook local)");
    }

    // El motor sigue: los cuadros avanzan en su propio hilo.
    std::atomic<std::uint64_t> cuadros{0};
    std::atomic<bool> fin{false};
    std::thread motor([&]() {
        while (!fin) {
            ++cuadros;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    std::atomic<int> latidos{0};
    LatidoAire lat;
    lat.iniciar(datos, [&]() { ++latidos; }, [&]() { return cuadros.load(); });
    auto aviso = std::make_unique<AvisoFueraDeUi>(datos);
    aviso->escuchar(lat);

    // --- Con aire: la interfaz se traba 3,5 s ---------------------------------------------
    lat.uiTick(true);
    const auto ultimoTick = reloj::now();
    const int latidosAntes = latidos;
    trabar(3500);
    const auto alGateway = pedidos("/api/v1/control/enviar");
    const auto alHook = pedidos("/hook");
    CHECK(alGateway.size() == 1, "con la interfaz trabada, la alarma llega al gateway (una vez)");
    if (!alGateway.empty()) {
        const json c = json::parse(alGateway[0].cuerpo, nullptr, false);
        CHECK(c.value("todas", false) && c["msg"].value("type", "") == "alarma" && c["msg"].value("id", "") == "ui_trabada" &&
                  c["msg"].value("activa", false),
              "a todos los aparatos del control web: {type: alarma, id: ui_trabada, activa}");
        const long long ms = msEntre(ultimoTick, alGateway[0].cuando);
        std::printf("   al control web a los %lld ms de la última señal de la interfaz\n", ms);
        CHECK(ms <= 3000, "en ≤ 3 s");
    }
    CHECK(alHook.size() == 1 && alHook[0].cuerpo.find("La interfaz no responde") != std::string::npos, "y al notificador (webhook)");
    if (!alHook.empty()) CHECK(msEntre(ultimoTick, alHook[0].cuando) <= 3000, "el notificador también en ≤ 3 s");
    CHECK(latidos - latidosAntes >= 10, "el latido siguió (el motor avanza: el supervisor no mata)");

    // --- Vuelve la interfaz: se avisa que terminó -----------------------------------------
    lat.uiTick(true);
    CHECK(esperar([]() { return pedidos("/api/v1/control/enviar").size() == 2; }, 2000), "al volver, otro aviso");
    {
        const auto g = pedidos("/api/v1/control/enviar");
        if (g.size() == 2) {
            const json c = json::parse(g[1].cuerpo, nullptr, false);
            CHECK(c["msg"].value("id", "") == "ui_trabada" && !c["msg"].value("activa", true), "con activa: false (la alarma terminó)");
        }
    }
    CHECK(esperar([]() { return pedidos("/hook").size() == 2; }, 2000), "también por el notificador");

    // --- Sin aire: trabarse no es alarma --------------------------------------------------
    lat.uiTick(false);
    trabar(2800);
    lat.uiTick(false);
    esperar([]() { return false; }, 600);
    CHECK(pedidos("/api/v1/control/enviar").size() == 2, "sin aire, una traba no avisa");

    // --- notificar: [] → solo el control web ----------------------------------------------
    CHECK(lat.ponerPolitica({{"notificar", json::array()}}).empty(), "política: a ningún notificador");
    CHECK(lat.estado()["politica"]["notificar"] == json::array(), "queda guardada");
    CHECK(!lat.ponerPolitica({{"notificar", 3}}).empty(), "notificar inválido: error");
    lat.uiTick(true);
    trabar(2800);
    lat.uiTick(true);
    esperar([]() { return pedidos("/api/v1/control/enviar").size() == 4; }, 2000);
    CHECK(pedidos("/api/v1/control/enviar").size() == 4, "al control web sí (empieza y termina)");
    CHECK(pedidos("/hook").size() == 2, "a los notificadores no");
    {
        LatidoAire otro; // lo guardado se lee al arrancar
        otro.iniciar(datos, nullptr, nullptr);
        CHECK(otro.estado()["politica"]["notificar"] == json::array(), "supervisor.json guarda notificar");
        otro.detener();
    }

    aviso.reset(); // se desengancha del latido antes de irse
    lat.detener();
    fin = true;
    motor.join();
    srv.quit();
    srv.wait();
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
