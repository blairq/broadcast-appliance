// ============================================================================
// src/app/tests/test_plano_control.cpp — el plano de control con la interfaz trabada (F3-03)
//
// Sin QQmlEngine (QCoreApplication): el plano, el servidor MCP en su hilo y el vigía
// de las alarmas, con «aire sintético» (transmitiendo, programa en negro). El hilo
// principal hace de interfaz y se TRABA a propósito (un bucle ocupado, como un QML
// pesado) mientras un cliente HTTP en otro hilo pide cosas:
//
//   1. El reloj del control sigue (y el del motor, que es de la interfaz, no).
//   2. /healthz y una herramienta de la foto contestan en el acto.
//   3. Una herramienta de la interfaz contesta «ocupada» al vencer el tope, con la
//      foto de emergencia, y NO se hace cuando la interfaz vuelve.
//   4. La alarma «programa en negro» sale en el hilo de control (donde los
//      notificadores la llevan afuera) durante la traba.
//   5. Con la interfaz libre, la misma herramienta corre en la interfaz; una
//      herramienta que se llama DESDE la interfaz contesta en la interfaz.
//   6. detener() devuelve lo adoptado a su hilo.
// ============================================================================
#include "agent/AlertEngine.hpp"
#include "arranque/PlanoControl.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpHttp.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using ba::app::PlanoControl;
using namespace ba::app::mcp;
using json = nlohmann::json;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        } else {                                                                         \
            std::printf("ok: %s\n", msg);                                                \
        }                                                                                \
    } while (0)

/// Un pedido HTTP bloqueante (desde un hilo sin bucle de eventos). Devuelve el cuerpo.
std::string pedir(quint16 puerto, const std::string& metodo, const std::string& ruta, const std::string& token,
                  const std::string& cuerpo = {}) {
    QTcpSocket s;
    s.connectToHost("127.0.0.1", puerto);
    if (!s.waitForConnected(3000)) return "SIN CONEXION";
    std::string req = metodo + " " + ruta + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token +
                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(cuerpo.size()) + "\r\n\r\n" + cuerpo;
    s.write(req.data(), static_cast<qint64>(req.size()));
    s.waitForBytesWritten(3000);
    QByteArray todo;
    while (s.waitForReadyRead(20000)) todo += s.readAll();
    todo += s.readAll();
    const int fin = todo.indexOf("\r\n\r\n");
    return fin < 0 ? std::string() : todo.mid(fin + 4).toStdString();
}

std::string llamar(quint16 puerto, const std::string& token, const std::string& herramienta, int id) {
    const json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", herramienta}, {"arguments", json::object()}}}};
    return pedir(puerto, "POST", "/mcp", token, m.dump());
}

double msDesde(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

/// La interfaz trabada: el hilo principal no atiende eventos durante `ms`.
void trabarInterfaz(int ms) {
    const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    volatile std::uint64_t x = 0;
    while (std::chrono::steady_clock::now() < hasta) ++x;
}

/// La interfaz libre `ms` (atiende eventos).
void atender(int ms) {
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QThread* interfaz = QThread::currentThread();

    // --- El plano, con relojes que solo cuentan --------------------------------------
    std::atomic<int> control{0}, motor{0};
    PlanoControl plano({[&]() { ++control; }, [&]() { ++motor; }});
    plano.arrancar(20, 20);

    // --- El MCP: una herramienta de la interfaz y una de la foto ---------------------
    McpCore core;
    std::atomic<bool> hizoLaDeInterfaz{false};
    std::atomic<const QThread*> dondeCorrio{nullptr};
    {
        Tool t;
        t.name = "ui_lenta";
        t.clase = Clase::Escritura;
        t.run = [&](const json&, Done done) {
            hizoLaDeInterfaz = true;
            dondeCorrio = QThread::currentThread();
            done(ToolResult::ok("hecho"));
        };
        core.addTool(std::move(t));
    }
    std::atomic<const QThread*> dondeFoto{nullptr};
    {
        Tool t;
        t.name = "foto_lee";
        t.clase = Clase::Lectura;
        t.hilo = Hilo::Foto;
        t.run = [&](const json&, Done done) {
            dondeFoto = QThread::currentThread();
            done(ToolResult().data({{"aire", "foto"}}));
        };
        core.addTool(std::move(t));
    }
    QThreadPool pool;
    McpCore::Ejecutores e;
    e.interfaz = [&plano](McpCore::Tarea t) { plano.encolarEnDueno(std::move(t)); };
    e.control = [&plano](McpCore::Tarea t) { plano.encolar(std::move(t)); };
    e.foto = [&pool](McpCore::Tarea t) { pool.start(std::move(t)); };
    e.despues = [&plano](int ms, McpCore::Tarea t) { plano.despues(ms, std::move(t)); };
    e.enInterfaz = [&plano]() { return plano.enDueno(); };
    e.enControl = [&plano]() { return plano.enControl(); };
    e.topeInterfazMs = 800;
    core.setEjecutores(std::move(e));
    core.setFotoDeEmergencia([]() { return json{{"aire", {{"streaming", true}}}}; });
    const std::string token = "0123456789abcdef0123456789abcdef";
    McpHttp http(core, QString::fromStdString(token));
    plano.adoptar(&http);
    bool escucha = false;
    QMetaObject::invokeMethod(&http, [&]() { escucha = http.listen(0); }, Qt::BlockingQueuedConnection);
    CHECK(escucha && http.port() != 0, "el servidor escucha en el hilo de control");

    // --- El vigía de las alarmas, con aire sintético ---------------------------------
    ba::app::agent::AlertInputs in;
    in.status = []() {
        ba::app::agent::AlertInputs::Status s;
        s.streaming = true;
        return s;
    };
    in.programaVideo = []() { return json{{"valido", true}, {"luma", 2}, {"quieto_s", 0}}; };
    ba::app::agent::AlertEngine vigia(in, "");
    const json regla = vigia.create({{"type", "program_black"}, {"name", "negro"}, {"params", {{"forSec", 0.5}}}});
    CHECK(regla.contains("id"), "la regla de negro");
    std::atomic<int> disparos{0};
    std::atomic<const QThread*> dondeDisparo{nullptr};
    // Como el aviso a los notificadores en la app: conexión directa, en el hilo del vigía.
    QObject::connect(
        &vigia, &ba::app::agent::AlertEngine::fired, &vigia,
        [&](const json&) {
            ++disparos;
            dondeDisparo = QThread::currentThread();
        },
        Qt::DirectConnection);
    plano.adoptar(&vigia, [&vigia]() { vigia.start(100); });

    atender(200); // que arranque todo

    // --- La interfaz trabada ----------------------------------------------------------
    std::string rHealth, rFoto, rUi;
    double msHealth = -1, msFoto = -1, msUi = -1;
    const int controlAntes = control.load(), motorAntes = motor.load();
    std::thread cliente([&]() {
        auto t = std::chrono::steady_clock::now();
        rHealth = pedir(http.port(), "GET", "/healthz", token);
        msHealth = msDesde(t);
        t = std::chrono::steady_clock::now();
        rFoto = llamar(http.port(), token, "foto_lee", 1);
        msFoto = msDesde(t);
        t = std::chrono::steady_clock::now();
        rUi = llamar(http.port(), token, "ui_lenta", 2);
        msUi = msDesde(t);
    });
    trabarInterfaz(3000);
    const int controlTrabada = control.load() - controlAntes, motorTrabada = motor.load() - motorAntes;
    const int disparosTrabada = disparos.load();
    cliente.join();
    std::printf("  medido: healthz %.1f ms, foto %.1f ms, interfaz ocupada %.1f ms; vueltas del control %d y del motor %d en 3 s de traba\n",
                msHealth, msFoto, msUi, controlTrabada, motorTrabada);
    CHECK(controlTrabada >= 100, "1: el reloj del control siguió con la interfaz trabada");
    CHECK(motorTrabada == 0, "1: el del motor (de la interfaz) no");
    CHECK(rHealth == "ok\n" && msHealth < 500, "2: /healthz contesta");
    const json jf = json::parse(rFoto, nullptr, false);
    CHECK(jf.is_object() && !jf["result"].value("isError", true) && msFoto < 500, "2: la herramienta de la foto contesta en el acto");
    CHECK(dondeFoto.load() != interfaz && dondeFoto.load() != plano.hilo(), "2: corrió en el pool");
    const json ju = json::parse(rUi, nullptr, false);
    CHECK(ju.is_object() && ju["result"].value("isError", false) &&
              ju["result"]["structuredContent"].value("interfaz_ocupada", false) &&
              ju["result"]["structuredContent"].contains("foto"),
          "3: la de la interfaz contesta «ocupada» con la foto");
    CHECK(msUi >= 750 && msUi < 2500, "3: al vencer el tope (800 ms)");
    CHECK(disparosTrabada >= 1 && dondeDisparo.load() == plano.hilo(), "4: la alarma salió en el hilo de control durante la traba");

    atender(500); // la interfaz vuelve: lo vencido NO se hace
    CHECK(!hizoLaDeInterfaz.load() && core.vencidas() == 1, "3: lo que venció no se hizo después");
    CHECK(motor.load() > motorAntes, "1: el reloj del motor vuelve con la interfaz");

    // --- La interfaz libre ------------------------------------------------------------
    std::string rLibre;
    std::thread otro([&]() { rLibre = llamar(http.port(), token, "ui_lenta", 3); });
    while (rLibre.empty()) atender(20);
    otro.join();
    const json jl = json::parse(rLibre, nullptr, false);
    CHECK(jl.is_object() && !jl["result"].value("isError", true) && hizoLaDeInterfaz.load() && dondeCorrio.load() == interfaz,
          "5: con la interfaz libre corre en la interfaz");
    // Desde la interfaz (la botonera, una receta): en el acto y la respuesta en la interfaz.
    std::atomic<const QThread*> dondeVolvio{nullptr};
    bool listo = false;
    core.callTool({{"name", "foto_lee"}, {"arguments", json::object()}}, [&](json) {
        dondeVolvio = QThread::currentThread();
        listo = true;
    }, CallContext{});
    for (int i = 0; i < 100 && !listo; ++i) atender(10);
    CHECK(listo && dondeVolvio.load() == interfaz, "5: la respuesta vuelve al hilo del que llamó");

    plano.detener();
    CHECK(http.thread() == interfaz && vigia.thread() == interfaz, "6: lo adoptado vuelve a su hilo");
    pool.waitForDone();
    std::printf(g_fallos == 0 ? "test_plano_control: OK\n" : "test_plano_control: %d fallo(s)\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
