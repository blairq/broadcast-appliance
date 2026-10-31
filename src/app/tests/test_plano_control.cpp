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
//   7. Las herramientas de la foto DE VERDAD (mcp/McpFoto.hpp: status, salidas_listar,
//      sistema_recursos, los registros, el bus, la lista de aire, GStreamer), con un
//      estado sintético (un documento de aire real en una carpeta de datos temporal):
//      con la interfaz trabada contestan en el acto, desde el pool, con lo que sale al
//      aire al día; ninguna fuente se lee en la interfaz ni en el control; la copia de la
//      interfaz se pide una sola vez y llega cuando ella vuelve; y no cambia nada (los
//      archivos de Config/, Runtime/ y Logs/, el documento de aire y sus escrituras).
// ============================================================================
#include "agent/AlertEngine.hpp"
#include "arranque/PlanoControl.hpp"
#include "emision/DocumentoAire.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpFoto.hpp"
#include "mcp/McpHttp.hpp"
#include "persist/ColaEscritura.hpp"

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
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

std::string llamar(quint16 puerto, const std::string& token, const std::string& herramienta, int id,
                   const json& args = json::object()) {
    const json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"}, {"params", {{"name", herramienta}, {"arguments", args}}}};
    return pedir(puerto, "POST", "/mcp", token, m.dump());
}

namespace fs = std::filesystem;

/// Cada archivo de la carpeta de datos con su contenido (para ver que nada cambió).
std::map<std::string, std::string> fotoDeArchivos(const fs::path& raiz) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(raiz, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file()) continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::stringstream c;
        c << f.rdbuf();
        out[fs::relative(it->path(), raiz).generic_string()] = c.str();
    }
    return out;
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
    // Como en la app: arrancado en la interfaz y después adoptado (el reloj se muda con él).
    vigia.start(100);
    plano.adoptar(&vigia);

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

    // --- 7. Las herramientas de la foto, con un estado sintético ------------------------
    const fs::path datos = fs::temp_directory_path() / "ba-test-plano-control";
    fs::remove_all(datos);
    for (const char* d : {"Config", "Runtime", "Logs"}) fs::create_directories(datos / d);
    std::ofstream(datos / "Config" / "salidas.json") << R"({"salidas":[{"id":"principal","nombre":"Principal"}]})";
    std::ofstream(datos / "Logs" / "app.log") << "linea uno\nlinea dos\n";
    ba::app::salidas::DocumentoAire doc(datos / "Runtime");
    {
        std::map<std::string, ba::app::salidas::Aire> s;
        s["principal"].escena = "Plano general";
        s["principal"].transmitiendo = true;
        s["principal"].placa.placa = true;
        s["principal"].placa.texto = "Cuarto intermedio";
        doc.ponerSalidas(s, false);
        doc.volcar();
    }
    CHECK(ba::persist::cola().vaciar(), "7: el documento de aire está escrito");
    // Una fuente leída en la interfaz o en el control sería una carrera: se anota dónde corre cada una.
    std::atomic<int> fuentesMalLeidas{0}, fuentesLeidas{0};
    const auto leida = [&]() {
        ++fuentesLeidas;
        if (QThread::currentThread() == interfaz || QThread::currentThread() == plano.hilo()) ++fuentesMalLeidas;
    };
    std::atomic<int> copiasStatus{0}, copiasSalidas{0};
    std::atomic<bool> copiaFueraDeLaInterfaz{false};
    ba::app::mcp::FuentesDeFoto f;
    f.aire = [&]() {
        leida();
        return ba::app::salidas::aJson(doc.foto());
    };
    f.hayAire = [&]() {
        leida();
        return doc.hayAire();
    };
    f.latido = [&]() {
        leida();
        return json{{"vivo", true}};
    };
    f.modoSeguro = [&]() {
        leida();
        return json{{"activo", false}};
    };
    f.salidasMotor = [&]() {
        leida();
        const json p{{"andando", true}, {"perdidos", 3}, {"cuadros", 9000}, {"reconectando", false}};
        return json{{"principal", p},
                    {"grabacion", {{"activa", false}}},
                    {"salidas", {{"principal", {{"principal", p}, {"grabacion", {{"activa", false}}}}}}}};
    };
    f.salidasPublicadas = [&]() {
        leida();
        return json::array({{{"id", "principal"}, {"nombre", "Principal"}, {"por_omision", true}}});
    };
    f.recursos = [&](int, int) {
        leida();
        return json{{"muestras", 5}, {"cpu", {{"valor", 12.5}}}, {"termica", {{"nivel", "normal"}}}};
    };
    f.servicio = [&]() {
        leida();
        return json{{"transporte", json::array({{{"canal", "programa"}}})}, {"termica", {{"pasos", 0}}}};
    };
    f.derivaPlacas = [&]() {
        leida();
        return json::array();
    };
    f.elementosGst = [&]() {
        leida();
        return json::array({{{"elemento", "compositor"}, {"esta", true}}});
    };
    f.temas = [&](const std::string&) {
        leida();
        return json::array({{{"tema", "aire/escena"}, {"valor", "Plano general"}}});
    };
    f.ultimoTema = [&](const std::string& t) {
        leida();
        return t == "aire/escena" ? json{{"tema", t}, {"valor", "Plano general"}} : json();
    };
    f.logs = datos / "Logs";
    // Lo de la interfaz (como en la app: lo que solo ella sabe; la armada de cada salida).
    f.statusInterfaz = [&]() {
        ++copiasStatus;
        if (QThread::currentThread() != interfaz) copiaFueraDeLaInterfaz = true;
        return json{{"currentScene", "Plano general"},
                    {"streaming", true},
                    {"room", {{"online", true}}},
                    {"salidas", json::array({{{"id", "principal"}, {"armada", "Entrevista"}}})}};
    };
    f.salidasInterfaz = [&]() {
        ++copiasSalidas;
        if (QThread::currentThread() != interfaz) copiaFueraDeLaInterfaz = true;
        return json{{"tope", 4}, {"salidas", json::array({{{"id", "principal"}, {"armada", "Entrevista"}}})}};
    };
    f.esperaInterfazMs = 100;
    core.setHayAire([&]() { return doc.hayAire(); });
    const std::size_t antes = core.tools().size();
    ba::app::mcp::registrarHerramientasDeFoto(core, f);
    bool todasDeLaFoto = core.tools().size() > antes;
    for (std::size_t i = antes; i < core.tools().size(); ++i)
        todasDeLaFoto = todasDeLaFoto && core.tools()[i].clase == Clase::Lectura && core.tools()[i].hilo == Hilo::Foto;
    CHECK(todasDeLaFoto, "7: todas son de lectura y de la foto");

    const auto archivosAntes = fotoDeArchivos(datos);
    const std::string aireAntes = ba::app::salidas::aJson(doc.foto()).dump();
    const std::uint64_t escriturasAntes = doc.escrituras();
    struct Pedido {
        std::string herramienta;
        json args;
        std::string resultado;
        double ms = -1;
    };
    std::vector<Pedido> pedidos;
    for (std::size_t i = antes; i < core.tools().size(); ++i) {
        Pedido p{core.tools()[i].name, json::object(), {}, -1};
        if (p.herramienta == "eventos_ultimo") p.args = {{"tema", "aire/escena"}};
        if (p.herramienta == "sistema_recursos") p.args = {{"historial_s", 30}};
        if (p.herramienta == "log_leer") p.args = {{"minimo", "traza"}};
        pedidos.push_back(std::move(p));
    }
    pedidos.push_back({"status", json::object(), {}, -1}); // otra vez: la copia se pide una sola
    std::thread clienteFoto([&]() {
        for (std::size_t i = 0; i < pedidos.size(); ++i) {
            const auto t = std::chrono::steady_clock::now();
            pedidos[i].resultado = llamar(http.port(), token, pedidos[i].herramienta, 100 + static_cast<int>(i), pedidos[i].args);
            pedidos[i].ms = msDesde(t);
        }
    });
    trabarInterfaz(static_cast<int>(pedidos.size()) * 150 + 1500);
    clienteFoto.join();
    const int copiasEnLaTraba = copiasStatus.load() + copiasSalidas.load();
    bool todasContestaron = true;
    double peor = 0;
    for (const auto& p : pedidos) {
        const json r = json::parse(p.resultado, nullptr, false);
        const bool bien = r.is_object() && r.contains("result") && !r["result"].value("isError", true);
        if (!bien) std::printf("  %s: %s\n", p.herramienta.c_str(), p.resultado.substr(0, 300).c_str());
        todasContestaron = todasContestaron && bien && p.ms < 600;
        peor = std::max(peor, p.ms);
    }
    std::printf("  medido: %zu pedidos de la foto con la interfaz trabada, el peor %.1f ms; %d fuentes leídas\n",
                pedidos.size(), peor, fuentesLeidas.load());
    CHECK(todasContestaron, "7: con la interfaz trabada, todas contestan en el acto y sin error");
    CHECK(fuentesLeidas.load() > 0 && fuentesMalLeidas.load() == 0, "7: ninguna fuente se leyó en la interfaz ni en el control");
    CHECK(copiasEnLaTraba == 0, "7: la copia de la interfaz no se hizo durante la traba");
    {
        const json r = json::parse(pedidos.back().resultado, nullptr, false);
        const json st = r.is_object() ? r["result"].value("structuredContent", json::object()) : json::object();
        CHECK(!st.value("interfaz", json::object()).value("al_dia", true) && st.value("streaming", false) &&
                  st.value("currentScene", std::string()) == "Plano general" && st.value("droppedFrames", 0) == 3 &&
                  st.value("latido", json::object()).value("vivo", false),
              "7: status trabado: lo que sale, al día (documento y foto de salidas); dice que lo demás es viejo");
        const json sal = st.value("salidas", json::array());
        CHECK(!sal.empty() && sal[0].value("placa", json::object()).value("placa", false) && sal[0].value("streaming", false),
              "7: status trabado: cada salida con su placa y lo que sale");
    }
    atender(300); // la interfaz vuelve: la copia pedida se hace (una por herramienta)
    CHECK(copiasStatus.load() == 1 && copiasSalidas.load() == 1 && !copiaFueraDeLaInterfaz.load(),
          "7: la copia se pidió una sola vez y se hizo en la interfaz al volver");
    {
        std::string r;
        std::thread otraVez([&]() { r = llamar(http.port(), token, "salidas_listar", 300); });
        while (r.empty()) atender(10);
        otraVez.join();
        const json j = json::parse(r, nullptr, false);
        const json st = j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        const json sal = st.value("salidas", json::array());
        CHECK(st.value("interfaz", json::object()).value("al_dia", false) && st.value("tope", 0) == 4 && !sal.empty() &&
                  sal[0].value("armada", std::string()) == "Entrevista",
              "7: con la interfaz libre, la copia está al día");
    }
    CHECK(ba::persist::cola().vaciar(), "7: nada pendiente de escribir");
    CHECK(fotoDeArchivos(datos) == archivosAntes, "7: no cambió ningún archivo de Config/, Runtime/ ni Logs/");
    CHECK(ba::app::salidas::aJson(doc.foto()).dump() == aireAntes && doc.escrituras() == escriturasAntes,
          "7: el documento de aire quedó igual (ni se puso ni se escribió nada)");
    fs::remove_all(datos);

    plano.detener();
    CHECK(http.thread() == interfaz && vigia.thread() == interfaz, "6: lo adoptado vuelve a su hilo");
    pool.waitForDone();
    std::printf(g_fallos == 0 ? "test_plano_control: OK\n" : "test_plano_control: %d fallo(s)\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
