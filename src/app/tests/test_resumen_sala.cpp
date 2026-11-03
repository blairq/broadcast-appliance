// test_resumen_sala.cpp — la foto de la sala no se procesa en la interfaz (sala/ResumenSala)
//
// Con 120 invitados la interfaz se trababa 3 a 26 s procesando la foto de la sala (cada una
// con la red de todos, varias por segundo). Se prueba que:
//   1. la primera foto llega entera (todas las partes) y las listas quedan como el resumen;
//   2. 200 invitados con fotos a 10 Hz y una interfaz lenta (toma una de cada cinco): LA
//      ÚLTIMA GANA (lo tomado es siempre la última depositada; las demás se descartan, nunca
//      se encolan) y cada paquete trae cambios acotados: sin reset, sin rehacer, solo las
//      filas y los roles que cambiaron;
//   3. la red sola (cambia en cada foto) no despierta a la gente ni a las listas, y a los
//      lugares los despierta a lo sumo una vez por segundo;
//   4. uno que entra: una fila insertada (sin reset); uno que se va: una quitada;
//   5. B nueva (epoch) se acumula en la foto que queda aunque se descarten las de en medio;
//   6. de punta a punta con ConferenceClient y una B de mentira: 30 fotos mientras la
//      interfaz está trabada llegan como UN aviso con la última.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "platform/Paths.hpp"
#include "sala/ResumenSala.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/JsonListModel.hpp"

using namespace ba::app;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

/// Una foto de la sala con `n` invitados adentro (lugares 1..n), `espera` en el lobby y la
/// red de cada uno según `tic` (cambia en cada foto). `micDe`: a quién se le apagó el mic.
QJsonObject foto(int n, int tic, int espera = 0, int micDe = -1, const QString& epoch = {}, bool reanudado = false) {
    QJsonArray gente;
    for (int i = 1; i <= n; ++i)
        gente.append(QJsonObject{{"id", QString("p%1").arg(i)},
                                 {"name", QString("Invitado %1").arg(i)},
                                 {"state", "admitted"},
                                 {"slot", i},
                                 {"connected", true},
                                 {"mediaUp", true},
                                 {"mic", i != micDe},
                                 {"cam", true},
                                 {"speaker", "none"},
                                 {"retorno", "ok"},
                                 {"usedMs", tic * 100},
                                 {"red", QJsonObject{{"kbps_in", 900 + (tic * 7 + i) % 50},
                                                     {"perdida_pct", (tic + i) % 3},
                                                     {"rtt_ms", 20 + (tic + i) % 9},
                                                     {"semaforo", "verde"}}}});
    for (int i = 1; i <= espera; ++i)
        gente.append(QJsonObject{{"id", QString("e%1").arg(i)},
                                 {"name", QString("Espera %1").arg(i)},
                                 {"state", "waiting"},
                                 {"slot", 0},
                                 {"lobbySlot", i <= 8 ? i : 0},
                                 {"connected", true},
                                 {"red", QJsonObject{{"kbps_in", tic}}}});
    QJsonObject f{{"room", "sala"},
                  {"pin", "1234"},
                  {"maxGuests", 250},
                  {"slotsFree", 250 - n},
                  {"participants", gente},
                  {"shares", QJsonArray{}},
                  {"chat", QJsonArray{}},
                  {"order", QJsonArray{}},
                  {"speakers", QJsonObject{{"enabled", false}, {"defaultSec", 120}}},
                  {"uso", QJsonObject{{"cpuPct", 40}}},
                  {"serverNowMs", 1'000'000.0 + tic * 100}};
    if (!epoch.isEmpty()) {
        f.insert("epoch", epoch);
        f.insert("reanudado", reanudado);
    }
    return f;
}

/// Lo que ve la interfaz: las listas y cuánto le avisaron.
struct Vista {
    JsonListModel espera{sala::clavesEspera()}, adentro{sala::clavesAdentro()}, lugares{sala::clavesLugares()};
    int resets = 0, filasAvisadas = 0, insertadas = 0, quitadas = 0, rolesRed = 0;
    Vista() {
        for (JsonListModel* m : {&espera, &adentro, &lugares}) {
            QObject::connect(m, &QAbstractItemModel::modelReset, [this]() { ++resets; });
            QObject::connect(m, &QAbstractItemModel::rowsInserted, [this](const QModelIndex&, int a, int b) { insertadas += b - a + 1; });
            QObject::connect(m, &QAbstractItemModel::rowsRemoved, [this](const QModelIndex&, int a, int b) { quitadas += b - a + 1; });
            QObject::connect(m, &QAbstractItemModel::dataChanged,
                             [this](const QModelIndex& a, const QModelIndex& b, const QList<int>&) { filasAvisadas += b.row() - a.row() + 1; });
        }
        const int rolRed = Qt::UserRole + 1 + static_cast<int>(sala::clavesLugares().indexOf("red"));
        QObject::connect(&lugares, &QAbstractItemModel::dataChanged, [this, rolRed](const QModelIndex&, const QModelIndex&, const QList<int>& r) {
            if (r.contains(rolRed)) ++rolesRed;
        });
    }
    void cero() { resets = filasAvisadas = insertadas = quitadas = rolesRed = 0; }
    void aplicar(const sala::PaqueteSala& p) {
        if ((p.partes & sala::Espera) != 0) espera.aplicar(p.espera);
        if ((p.partes & sala::Adentro) != 0) adentro.aplicar(p.adentro);
        if ((p.partes & sala::Lugares) != 0) lugares.aplicar(p.lugares);
    }
    bool igualA(const sala::ResumenSala& r) const {
        return espera.items() == r.espera && adentro.items() == r.adentro && lugares.items() == r.lugares;
    }
};

int pasos(const PlanFilas& p) { return static_cast<int>(p.pasos.size()); }

// --- B de mentira para el caso de punta a punta --------------------------------------------
struct FalsoB {
    QTcpServer srv;
    QPointer<QTcpSocket> sse;
    FalsoB() {
        srv.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&srv, &QTcpServer::newConnection, [this]() {
            while (QTcpSocket* s = srv.nextPendingConnection()) {
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s]() {
                    if (!s->readAll().startsWith("GET /api/v1/events")) return;
                    sse = s;
                    const std::string r = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n";
                    s->write(r.data(), static_cast<qint64>(r.size()));
                    mandar(foto(20, 0, 0, -1, "E1", false), "E1:1");
                });
            }
        });
    }
    void mandar(const QJsonObject& f, const QByteArray& id) {
        if (!sse) return;
        sse->write("id: " + id + "\nevent: room\ndata: " + QJsonDocument(f).toJson(QJsonDocument::Compact) + "\n\n");
    }
    /// Escribe todo lo pendiente SIN el bucle (la interfaz está «trabada»).
    void vaciar() {
        while (sse && sse->bytesToWrite() > 0) sse->waitForBytesWritten(1000);
    }
};

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

    constexpr int kN = 200;
    std::int64_t ahora = 5'000'000;
    sala::BuzonSala buzon;
    buzon.setLugares(kN);
    Vista v;

    // 1) La primera foto: entera.
    CHECK(buzon.depositar(foto(kN, 0, 3), ahora), "la primera foto avisa");
    std::shared_ptr<const sala::PaqueteSala> p = buzon.tomar();
    CHECK(p && p->primera && p->partes == sala::Todo, "la primera: todas las partes");
    v.aplicar(*p);
    CHECK(v.igualA(*p->r) && v.adentro.count() == kN && v.espera.count() == 3 && v.lugares.count() == kN,
          "las listas quedan como el resumen (200 adentro, 3 esperando, 200 lugares)");
    CHECK(!v.adentro.items().at(0).toObject().contains("red"), "las filas no llevan la red entera (solo sus roles)");
    CHECK(buzon.tomar() == nullptr, "tomado, no queda nada");

    // 2) 10 Hz durante 3 s con una interfaz que toma una de cada cinco.
    v.cero();
    int tomadas = 0, avisos = 0, maxPasosAdentro = 0, maxPasosLugares = 0;
    bool siempreLaUltima = true, sinRehacer = true;
    for (int tic = 1; tic <= 30; ++tic) {
        ahora += 100;
        const int mic = tic % 7 == 0 ? tic : -1; // de vez en cuando alguien apaga el mic
        if (buzon.depositar(foto(kN, tic, 3, mic), ahora)) ++avisos;
        if (tic % 5 != 0) continue;
        p = buzon.tomar();
        ++tomadas;
        const std::uint64_t ultima = buzon.cuenta().depositadas;
        siempreLaUltima = siempreLaUltima && p && p->numero == ultima;
        sinRehacer = sinRehacer && !p->adentro.rehacer && !p->lugares.rehacer && !p->espera.rehacer;
        maxPasosAdentro = std::max(maxPasosAdentro, pasos(p->adentro));
        maxPasosLugares = std::max(maxPasosLugares, pasos(p->lugares));
        v.aplicar(*p);
        if (!v.igualA(*p->r)) siempreLaUltima = false;
    }
    const sala::BuzonSala::Cuenta c = buzon.cuenta();
    CHECK(siempreLaUltima, "la última gana: lo que toma la interfaz es siempre la última foto, y las listas quedan como ella");
    CHECK(avisos == tomadas, "un aviso por cada vez que la interfaz toma (nunca se encolan fotos)");
    CHECK(c.descartadas == 30 - static_cast<std::uint64_t>(tomadas), "las de en medio se descartan");
    CHECK(sinRehacer && v.resets == 0 && v.insertadas == 0 && v.quitadas == 0, "sin reset ni filas rehechas");
    CHECK(maxPasosAdentro <= 2, "adentro: solo las filas que cambiaron (el mic de uno, el usedMs no es rol)");
    CHECK(maxPasosLugares <= kN && v.rolesRed <= 4 * kN, "lugares: la red, a lo sumo una vez por segundo por lugar");

    // 3) Solo la red cambió, antes del segundo: no despierta nada.
    ahora += 100;
    buzon.depositar(foto(kN, 31, 3), ahora);
    p = buzon.tomar();
    CHECK((p->partes & (sala::Gente | sala::Adentro | sala::Espera | sala::Participantes | sala::Sala)) == 0,
          "la red sola no cambia la gente ni las listas");
    ahora += 1000;
    buzon.depositar(foto(kN, 32, 3), ahora);
    p = buzon.tomar();
    CHECK((p->partes & sala::Lugares) != 0 && (p->partes & sala::Gente) == 0, "pasado el segundo, la red de los lugares se refresca");

    // 4) Entra uno y se va otro.
    v.aplicar(*p);
    v.cero();
    ahora += 100;
    buzon.depositar(foto(kN, 33, 4), ahora); // uno más en el lobby
    p = buzon.tomar();
    v.aplicar(*p);
    CHECK((p->partes & sala::Gente) != 0 && v.insertadas == 1 && v.resets == 0 && p->llegaronAlLobby.size() == 1,
          "uno que llega al lobby: una fila insertada, sin reset, y se avisa quién");
    v.cero();
    ahora += 100;
    buzon.depositar(foto(kN - 1, 34, 4), ahora); // se fue el último de adentro
    p = buzon.tomar();
    v.aplicar(*p);
    CHECK(v.quitadas >= 1 && v.resets == 0 && v.igualA(*p->r), "uno que se va: su fila quitada, sin reset");

    // 5) B nueva entre fotos descartadas.
    ahora += 100;
    buzon.depositar(foto(kN, 35, 0, -1, "E2", false), ahora);
    buzon.depositar(foto(kN, 36), ahora + 50);
    p = buzon.tomar();
    CHECK(p->nueva && p->epoch == "E2" && p->numero == buzon.cuenta().depositadas,
          "B nueva: se republica aunque su foto se haya descartado (y llega la última)");
    buzon.depositar(foto(kN, 37, 0, -1, "E2", true), ahora + 100);
    CHECK(!buzon.tomar()->nueva, "la misma B que reanudó: no");

    // 6) De punta a punta: 30 fotos con la interfaz trabada son UN aviso.
    FalsoB b;
    std::filesystem::create_directories(ba::platform::stateDir());
    std::ofstream(ba::platform::stateDir() / "gateway-admin.json") << json{{"port", b.srv.serverPort()}, {"token", "tok"}}.dump();
    ConferenceClient cli;
    int cambios = 0;
    QObject::connect(&cli, &ConferenceClient::salaCambio, [&cambios](unsigned) { ++cambios; });
    CHECK(esperar([&]() { return cli.online() && cambios == 1; }, 5000), "se conecta y llega la primera foto");
    cambios = 0;
    const std::uint64_t antes = cli.cuentaFotos().depositadas;
    for (int i = 1; i <= 30; ++i) b.mandar(foto(20, i), "E1:" + QByteArray::number(i + 1));
    b.vaciar();
    // La interfaz «trabada»: no corre su bucle mientras el lector lee y resume las 30.
    QElapsedTimer t;
    t.start();
    while (cli.cuentaFotos().depositadas < antes + 30 && t.elapsed() < 5000) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(cli.cuentaFotos().depositadas == antes + 30, "el lector leyó las 30 sin la interfaz");
    esperar([]() { return false; }, 300);
    CHECK(cambios == 1, "con la interfaz trabada, 30 fotos llegan como UN aviso");
    CHECK(cli.snapshot().value("serverNowMs").toDouble() == 1'000'000.0 + 30 * 100, "y es la última");
    CHECK(cli.resumen().adentro.size() == 20 && cli.cuentaFotos().descartadas >= 29, "las demás se descartaron");

    std::printf(g_fallas == 0 ? "test_resumen_sala: OK\n" : "test_resumen_sala: %d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
