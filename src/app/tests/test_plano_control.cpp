// ============================================================================
// src/app/tests/test_plano_control.cpp — el plano de control con la interfaz trabada (F3-03)
//
// Sin QQmlEngine (QCoreApplication): el plano, el servidor MCP en su hilo y el vigía
// de las alarmas, con «aire sintético» (transmitiendo, programa en negro). El hilo
// principal hace de interfaz y se TRABA a propósito (un bucle ocupado, como un QML
// pesado) mientras un cliente HTTP en otro hilo pide cosas:
//
//   1. Los relojes del control y del motor (su dueño es el control) siguen; el de la
//      interfaz (su latido) no.
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
//   8. Las de AIRE en el control (mcp/McpMando.hpp: scene_switch, scene_preview,
//      preview_al_aire, aire_ftb, stream_start, record_start), con un aire sintético que
//      escribe un documento de aire real: con la interfaz trabada se hacen, en el hilo de
//      control y con el turno del motor (emision/TurnoMotor.hpp), y el documento cambia en
//      el acto; nada corre en la interfaz (contador) y los avisos a las vistas le llegan
//      encolados cuando vuelve. Con la interfaz trabada ADENTRO del motor (con el turno)
//      más que el tope, contestan «motor ocupado» y no se hacen después; menos, esperan.
//   9. Lo que solo sabe la interfaz, por copia (copiarDeLaInterfaz: linea_estado,
//      linea_ver, stream_salud, monitors): trabada, la última copia con su edad en el
//      acto; libre, al día. Y las alarmas de la sala (de B si la foto de la interfaz se
//      quedó quieta), la energía y el disco (directas) salen en el hilo de control con la
//      interfaz trabada. Las de la sala, el modo vivo, las caídas, el sistema y los
//      notificadores van en el 7 (room_status, network_status, modo_vivo_estado,
//      supervisor_caidas, system_info, notificador_listar).
//  10. LA VENTANILLA (emision/VentanillaMotor.hpp: el motor es del plano de control), con
//      un motor sintético cuyo dueño es el control: lo que la interfaz pide (cortar, TAKE,
//      agregar y mover una fuente, grabar) corre en el control, con el turno y sin esperar
//      a la interfaz trabada; la foto que publica el dueño NO cambia en la interfaz
//      mientras está trabada y le llega entera al volver, con las partes que cambiaron, y
//      cada respuesta llega DESPUÉS de la foto con su cambio. La red de seguridad: la
//      interfaz que entra directo (por vuelta) tiene el turno hasta que termina su evento;
//      mientras tanto la agenda reintenta sin trabar el control (sus relojes siguen), lo
//      pedido se hace al soltarlo y la entrada directa queda contada con su lugar.
// ============================================================================
#include "agent/AlertEngine.hpp"
#include "agent/EntradasAlarmas.hpp"
#include "arranque/AppSettings.hpp"
#include "arranque/PlanoControl.hpp"
#include "emision/DocumentoAire.hpp"
#include "emision/TurnoMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpFoto.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpMando.hpp"
#include "persist/ColaEscritura.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
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
    std::atomic<int> control{0}, motor{0}, latido{0};
    std::atomic<const QThread*> dondeMotor{nullptr};
    PlanoControl plano({.control = [&]() { ++control; },
                        .motor =
                            [&]() {
                                ++motor;
                                dondeMotor = QThread::currentThread();
                            },
                        .interfaz = [&]() { ++latido; }});
    // El turno del motor (la agenda lo toma): como en la app, uno solo.
    ba::app::TurnoMotor turno;
    plano.setTurno({[&turno](int ms) { return turno.tomarHasta(ms); }, [&turno]() { turno.soltar(); }});
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
    e.interfaz = [&plano](McpCore::Tarea t) { plano.encolarEnInterfaz(std::move(t)); };
    e.control = [&plano](McpCore::Tarea t) { plano.encolar(std::move(t)); };
    e.foto = [&pool](McpCore::Tarea t) { pool.start(std::move(t)); };
    e.despues = [&plano](int ms, McpCore::Tarea t) { plano.despues(ms, std::move(t)); };
    e.enInterfaz = [&plano]() { return plano.enInterfaz(); };
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
    const int controlAntes = control.load(), motorAntes = motor.load(), latidoAntes = latido.load();
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
    const int controlTrabada = control.load() - controlAntes, motorTrabada = motor.load() - motorAntes,
              latidoTrabada = latido.load() - latidoAntes;
    const int disparosTrabada = disparos.load();
    cliente.join();
    std::printf("  medido: healthz %.1f ms, foto %.1f ms, interfaz ocupada %.1f ms; vueltas del control %d, del motor %d y del "
                "latido de la interfaz %d en 3 s de traba\n",
                msHealth, msFoto, msUi, controlTrabada, motorTrabada, latidoTrabada);
    CHECK(controlTrabada >= 100, "1: el reloj del control siguió con la interfaz trabada");
    CHECK(motorTrabada >= 100 && dondeMotor.load() == plano.hilo(), "1: el del motor también (es del control, su dueño)");
    CHECK(latidoTrabada == 0, "1: el latido de la interfaz no (es de ella: así el supervisor sabe que se trabó)");
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
    CHECK(latido.load() > latidoAntes, "1: el latido vuelve con la interfaz");

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
    // Lo que la interfaz tenía y ahora da su dueño sin ella: la sala (B), el modo en vivo
    // y la caída (la foto de AppViewModel), los archivos del sistema, los notificadores.
    f.sala = [&](const std::string& ruta) -> std::optional<json> {
        leida();
        if (ruta == "/api/v1/room")
            return json{{"room", "Sala"}, {"participants", json::array({{{"id", "p1"}, {"name", "Ana"}}})}, {"chat", json::array()}};
        if (ruta == "/api/v1/network") return json{{"https", true}};
        return std::nullopt;
    };
    f.modoVivo = [&]() {
        leida();
        return json{{"en_vivo", true}, {"activo", true}};
    };
    f.caidas = [&](int) {
        leida();
        return json::array({{{"causa", "colgado"}}});
    };
    f.caidaReciente = [&]() {
        leida();
        return json{{"causa", "colgado"}, {"habia_aire", true}};
    };
    f.sistema = [&]() {
        leida();
        return json{{"dataRoot", datos.string()}, {"credentials", json::array()}};
    };
    f.notificadores = [&]() {
        leida();
        return json::array({{{"id", "n-1"}, {"tipo", "webhook"}}});
    };
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
    {
        // Las que leían objetos de la interfaz: con ella trabada, lo de su dueño.
        std::map<std::string, json> r;
        for (const auto& p : pedidos) {
            const json j = json::parse(p.resultado, nullptr, false);
            if (j.is_object()) r[p.herramienta] = j["result"].value("structuredContent", json::object());
        }
        CHECK(r["room_status"].value("participants", json::array()).size() == 1 && r["network_status"].value("https", false) &&
                  r["modo_vivo_estado"].value("en_vivo", false) && r["supervisor_caidas"].value("recien", json::object()).value("habia_aire", false) &&
                  r["system_info"].contains("dataRoot") && r["notificador_listar"].value("notificadores", json::array()).size() == 1,
              "7: room_status, network_status, modo_vivo_estado, supervisor_caidas, system_info y notificador_listar, trabada, con lo de su dueño");
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

    // --- 9. Lo que solo sabe la interfaz, por copia; y las alarmas que no la esperan ------
    // (a) Una de lectura cuyo dato es SOLO de la interfaz (como linea_estado, linea_ver,
    // stream_salud, monitors): copiarDeLaInterfaz la pasa al pool con una copia por
    // argumentos. Libre: al día. Trabada: la última de esos argumentos, con su edad, en
    // el acto; la de unos argumentos que nunca se copiaron espera la primera vez y dice
    // que no hay copia. Las copias se hacen en la interfaz.
    std::atomic<int> copiasLinea{0};
    std::atomic<bool> copiaLineaFuera{false};
    std::atomic<int> pasoDeLaLinea{1};
    {
        Tool t;
        t.name = "linea_sintetica";
        t.clase = Clase::Lectura;
        copiarDeLaInterfaz(core, t,
                           [&](const json& a) -> LeidoEnLaInterfaz {
                               ++copiasLinea;
                               if (QThread::currentThread() != interfaz) copiaLineaFuera = true;
                               if (a.value("salida", std::string()) == "nadie") return {{}, "No hay esa salida en la línea."};
                               return {json{{"salida", a.value("salida", std::string("principal"))}, {"paso", pasoDeLaLinea.load()}}, {}};
                           },
                           EsperaDeLaCopia{100, 300});
        core.addTool(std::move(t));
    }
    CHECK(core.findTool("linea_sintetica") != nullptr && core.findTool("linea_sintetica")->hilo == Hilo::Foto,
          "9a: la de la copia corre en el pool");
    const auto llamarLibre = [&](const json& args, int id) {
        std::string r;
        std::thread c([&]() { r = llamar(http.port(), token, "linea_sintetica", id, args); });
        while (r.empty()) atender(10);
        c.join();
        return json::parse(r, nullptr, false);
    };
    {
        const json j = llamarLibre(json::object(), 900);
        const json st = j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        CHECK(st.value("paso", 0) == 1 && st.value("interfaz", json::object()).value("al_dia", false),
              "9a: con la interfaz libre, la copia al día");
        const json e = llamarLibre({{"salida", "nadie"}}, 901);
        CHECK(e.is_object() && e["result"].value("isError", false), "9a: el error de la herramienta llega como error");
    }
    pasoDeLaLinea = 2; // cambia en la interfaz, que se traba antes de copiarlo
    {
        std::string rVieja, rNueva;
        double msVieja = -1, msNueva = -1;
        const int antes = copiasLinea.load();
        std::thread c([&]() {
            auto t = std::chrono::steady_clock::now();
            rVieja = llamar(http.port(), token, "linea_sintetica", 902, json::object());
            msVieja = msDesde(t);
            t = std::chrono::steady_clock::now();
            rNueva = llamar(http.port(), token, "linea_sintetica", 903, {{"salida", "otra"}});
            msNueva = msDesde(t);
        });
        trabarInterfaz(1500);
        c.join();
        const int enLaTraba = copiasLinea.load() - antes;
        const json jv = json::parse(rVieja, nullptr, false);
        const json sv = jv.is_object() ? jv["result"].value("structuredContent", json::object()) : json::object();
        std::printf("  medido: copia con la interfaz trabada %.1f ms (espera 100); sin copia previa %.1f ms (espera la primera 300)\n",
                    msVieja, msNueva);
        CHECK(jv.is_object() && !jv["result"].value("isError", true) && sv.value("paso", 0) == 1 &&
                  !sv.value("interfaz", json::object()).value("al_dia", true) &&
                  sv.value("interfaz", json::object()).value("edad_ms", -1) >= 0 && msVieja < 400,
              "9a: trabada: la última copia, con su edad, en el acto");
        const json jn = json::parse(rNueva, nullptr, false);
        CHECK(jn.is_object() && jn["result"].value("isError", false) &&
                  !jn["result"].value("structuredContent", json::object()).value("interfaz", json::object()).value("al_dia", true) &&
                  msNueva >= 250 && msNueva < 900,
              "9a: trabada y sin copia de esos argumentos: espera la primera vez y lo dice");
        CHECK(enLaTraba == 0, "9a: nada se copió durante la traba");
        atender(300);
        CHECK(copiasLinea.load() - antes == 2 && !copiaLineaFuera.load(), "9a: al volver, una copia por argumentos, en la interfaz");
        const json j = llamarLibre(json::object(), 904);
        CHECK(j.is_object() && j["result"].value("structuredContent", json::object()).value("paso", 0) == 2,
              "9a: y la siguiente, al día");
    }

    // (b) Las alarmas de la sala, la energía y el disco con la interfaz trabada. Como en
    // la app (AlarmasViewModel): la sala viene de la foto de la interfaz y, si se queda
    // quieta, de B (el respaldo); la energía y el disco, directas (se miden en el vigía).
    std::mutex bMu;
    bool anaConectada = true;
    std::atomic<bool> enBateria{false};
    std::atomic<double> libreGb{200.0};
    std::atomic<int> preguntasAB{0}, preguntasEnLaInterfaz{0};
    const auto sala = [&]() {
        const std::lock_guard<std::mutex> l(bMu);
        return json{{"serverNowMs", 0},
                    {"participants", json::array({{{"id", "p1"}, {"name", "Ana"}, {"state", "admitted"}, {"connected", anaConectada}}})},
                    {"chat", json::array()}};
    };
    ba::app::agent::AlertInputs deLaInterfaz;
    deLaInterfaz.room = sala; // la interfaz la tiene de los eventos de B: la misma
    ba::app::agent::AlertInputs directas;
    directas.status = []() {
        ba::app::agent::AlertInputs::Status st;
        st.recording = true;
        return st;
    };
    directas.energia = [&]() {
        return json{{"en_bateria", enBateria.load()}, {"porcentaje", 80}, {"minutos", 60}, {"fuente", "ups"}, {"accion", "avisar"}};
    };
    directas.disco = [&]() { return json{{"libre_gb", libreGb.load()}, {"horas", -1}}; };
    ba::app::agent::AlertInputs respaldo;
    respaldo.room = [&]() {
        ++preguntasAB;
        if (QThread::currentThread() == interfaz) ++preguntasEnLaInterfaz;
        return sala();
    };
    ba::app::agent::EntradasAlarmas entradas(deLaInterfaz, directas, respaldo, 500);
    entradas.refrescar();
    ba::app::agent::AlertEngine vigia9(entradas.paraElVigia(), "");
    for (const std::string tipo : {"guest_drop", "energia", "disk"})
        vigia9.create({{"type", tipo}, {"name", tipo}, {"params", tipo == "disk" ? json{{"minGb", 5.0}} : json::object()}});
    std::mutex disparosMu;
    std::map<std::string, const QThread*> disparos9;
    QObject::connect(
        &vigia9, &ba::app::agent::AlertEngine::fired, &vigia9,
        [&](const json& ev) {
            const std::lock_guard<std::mutex> l(disparosMu);
            disparos9[ev.value("type", std::string())] = QThread::currentThread();
        },
        Qt::DirectConnection);
    vigia9.start(100);
    plano.adoptar(&vigia9);
    // El reloj de la interfaz copia su foto (en la app, cada 200 ms).
    QTimer refresco;
    QObject::connect(&refresco, &QTimer::timeout, [&]() { entradas.refrescar(); });
    refresco.start(100);
    atender(600); // el vigía se entera de cómo está todo (Ana conectada)
    const int preguntasAntes = preguntasAB.load();
    {
        const std::lock_guard<std::mutex> l(disparosMu);
        CHECK(disparos9.empty() && preguntasAntes == 0, "9b: con la interfaz andando, nada que avisar y nada que preguntarle a B");
    }
    {
        // Se traba la interfaz; recién ahí Ana se cae, se corta la luz y se llena el disco.
        std::thread cambios([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            {
                const std::lock_guard<std::mutex> l(bMu);
                anaConectada = false;
            }
            enBateria = true;
            libreGb = 1.0;
        });
        trabarInterfaz(2500);
        cambios.join();
    }
    {
        const std::lock_guard<std::mutex> l(disparosMu);
        for (const std::string tipo : {"guest_drop", "energia", "disk"}) {
            const bool salio = disparos9.count(tipo) && disparos9[tipo] == plano.hilo();
            CHECK(salio, ("9b: con la interfaz trabada, la alarma «" + tipo + "» sale en el hilo de control").c_str());
        }
    }
    CHECK(preguntasAB.load() > 0 && preguntasEnLaInterfaz.load() == 0,
          "9b: la sala se le preguntó a B desde el vigía, solo mientras la foto de la interfaz estaba quieta");
    refresco.stop();

    // --- 8. Las de aire, en el control y con el turno del motor -------------------------
    // Como en la app: participan del turno la interfaz (el dueño del motor) y el control.
    ba::app::TurnoMotor::participar("interfaz");
    plano.encolar([]() { ba::app::TurnoMotor::participar("control"); });
    core.setTurno({[&turno](int ms) { return turno.tomarHasta(ms); }, [&turno]() { turno.soltar(); },
                   [&turno]() { return turno.estado(); }});
    const fs::path datos8 = fs::temp_directory_path() / "ba-test-plano-control-aire";
    fs::remove_all(datos8);
    fs::create_directories(datos8 / "Runtime");
    ba::app::salidas::DocumentoAire doc8(datos8 / "Runtime");
    // El aire sintético: lo que la app tiene en el motor, los conmutadores y el runtime.
    struct Aire {
        std::string alAire = "Plano general", armada;
        bool ftb = false, transmite = false, graba = false;
    } aire;
    std::atomic<int> enLaInterfaz{0}, sinTurno{0}, operaciones{0}, avisosVista{0}, avisosFuera{0};
    // Cada operación: dónde corrió y con qué; el documento de aire, ya (lo que hace
    // AppRuntime::writeOnAirState con el turno); el aviso a la vista, encolado al dueño
    // (AppRuntime::enDueno).
    const auto operar = [&](const std::function<void()>& cambio) {
        const ba::app::TurnoMotor::Guarda g(turno); // como los métodos del motor: recursivo
        ++operaciones;
        if (QThread::currentThread() == interfaz) ++enLaInterfaz;
        if (!turno.loTengo()) ++sinTurno;
        cambio();
        std::map<std::string, ba::app::salidas::Aire> sal;
        auto& a = sal["principal"];
        a.escena = aire.alAire;
        a.transmitiendo = aire.transmite;
        a.grabando = aire.graba;
        a.placa.ftb = aire.ftb;
        doc8.ponerSalidas(sal, false);
        doc8.volcar();
        plano.encolarEnInterfaz([&]() {
            ++avisosVista;
            if (QThread::currentThread() != interfaz) ++avisosFuera;
        });
    };
    MandoDeAire mando;
    mando.escenas = []() { return std::vector<std::string>{"Plano general", "Entrevista", "Cierre"}; };
    mando.alAire = [&](const std::string&) { return aire.alAire; };
    mando.armada = [&](const std::string&) { return aire.armada; };
    mando.poner = [&](const std::string&, const std::string& e) {
        operar([&]() { aire.alAire = e; });
        return std::string();
    };
    mando.armar = [&](const std::string&, const std::string& e) {
        operar([&]() { aire.armada = e; });
        return std::string();
    };
    mando.take = [&](const std::string&, const std::string&, int) {
        operar([&]() { std::swap(aire.alAire, aire.armada); });
        return std::string();
    };
    mando.tbar = [](const std::string&, double, bool) { return std::string(); };
    mando.manualHacia = [](const std::string&) { return std::string(); };
    mando.ftb = [&](const std::string&, bool activar, int, const std::string&) {
        operar([&]() { aire.ftb = activar; });
        return std::string();
    };
    mando.placa = [](const std::string&, bool, const std::string&, const std::string&, int) { return std::string(); };
    mando.estadoPlaca = [&](const std::string&) { return json{{"ftb", aire.ftb}}; };
    mando.transmitir = [&](const std::string&, const std::string&, const std::string&, bool) {
        MandoDeAire::Inicio r;
        operar([&]() {
            r.yaIgual = aire.transmite;
            aire.transmite = true;
        });
        r.ok = true;
        return r;
    };
    mando.cortarTransmision = [&](const std::string&) { operar([&]() { aire.transmite = false; }); };
    mando.iniciarDestino = [](const std::string&) { return std::string(); };
    mando.detenerDestino = [](const std::string&) {};
    mando.destinos = []() { return json::array(); };
    mando.variableClave = []() { return std::string("BA_STREAM_KEY"); };
    mando.grabacion = [&](const std::string&) {
        return json{{"activa", aire.graba}, {"archivo", aire.graba ? "programa-0001.mkv" : ""}};
    };
    mando.disco = []() { return json{{"horas", 12.0}}; };
    mando.grabar = [&](const std::string&) {
        operar([&]() { aire.graba = true; });
        return std::string();
    };
    mando.pararGrabacion = [&](const std::string&) { operar([&]() { aire.graba = false; }); };
    mando.algoSale = [&]() { return aire.transmite || aire.graba; };
    mando.contenedor = []() { return std::string("mkv"); };
    const std::size_t antesAire = core.tools().size();
    registrarHerramientasDeAire(core, mando);
    bool todasDeControl = core.tools().size() == antesAire + 11;
    for (std::size_t i = antesAire; i < core.tools().size(); ++i)
        todasDeControl = todasDeControl && core.tools()[i].hilo == Hilo::Control;
    CHECK(todasDeControl, "8: las 11 de aire son del control");
    // Lo de aire pedido por algo automático con aire andando lo confirma una persona; acá no
    // hay a quién preguntarle (como con --test).
    core.setHayAire([&]() { return aire.transmite || aire.graba; });
    core.setSinAprobaciones(true);

    struct PasoAire {
        std::string herramienta;
        json args;
        std::function<bool(const ba::app::salidas::Aire&)> seVe; ///< en el documento, en el acto
        std::string resultado;
        double ms = -1;
        bool seVio = false;
    };
    using SA = ba::app::salidas::Aire;
    std::vector<PasoAire> pasos;
    pasos.push_back({"scene_switch", {{"name", "Entrevista"}}, [](const SA& a) { return a.escena == "Entrevista"; }});
    pasos.push_back({"scene_preview", {{"name", "Cierre"}}, [](const SA& a) { return a.escena == "Entrevista"; }});
    pasos.push_back({"preview_al_aire", json::object(), [](const SA& a) { return a.escena == "Cierre"; }});
    pasos.push_back({"aire_ftb", {{"activar", true}}, [](const SA& a) { return a.placa.ftb; }});
    pasos.push_back({"stream_start", json::object(), [](const SA& a) { return a.transmitiendo; }});
    pasos.push_back({"record_start", json::object(), [](const SA& a) { return a.grabando; }});
    pasos.push_back({"aire_ftb", {{"activar", false}}, [](const SA& a) { return !a.placa.ftb; }});
    const int vueltasAntes = control.load();
    std::thread clienteAire([&]() {
        for (std::size_t i = 0; i < pasos.size(); ++i) {
            const auto t = std::chrono::steady_clock::now();
            pasos[i].resultado = llamar(http.port(), token, pasos[i].herramienta, 400 + static_cast<int>(i), pasos[i].args);
            pasos[i].ms = msDesde(t);
            pasos[i].seVio = pasos[i].seVe(doc8.foto().de("principal"));
        }
    });
    trabarInterfaz(static_cast<int>(pasos.size()) * 150 + 1500);
    clienteAire.join();
    const int avisosEnLaTraba = avisosVista.load();
    bool todasHechas = true, todasSeVen = true;
    double peorAire = 0;
    for (const auto& p : pasos) {
        const json r = json::parse(p.resultado, nullptr, false);
        const bool bien = r.is_object() && r.contains("result") && !r["result"].value("isError", true);
        if (!bien || !p.seVio) std::printf("  %s: %s\n", p.herramienta.c_str(), p.resultado.substr(0, 300).c_str());
        todasHechas = todasHechas && bien && p.ms < 600;
        todasSeVen = todasSeVen && p.seVio;
        peorAire = std::max(peorAire, p.ms);
    }
    std::printf("  medido: %zu de aire con la interfaz trabada, la peor %.1f ms; %d operaciones; el control dio %d vueltas\n",
                pasos.size(), peorAire, operaciones.load(), control.load() - vueltasAntes);
    CHECK(todasHechas, "8: con la interfaz trabada, TAKE, FTB, transmitir y grabar se hacen en el acto");
    CHECK(todasSeVen, "8: el aire responde: el documento de aire cambia en el acto");
    CHECK(operaciones.load() >= static_cast<int>(pasos.size()) && enLaInterfaz.load() == 0,
          "8: nada se hizo en el hilo de la interfaz");
    CHECK(sinTurno.load() == 0, "8: todo con el turno del motor");
    CHECK(avisosEnLaTraba == 0, "8: los avisos a la vista esperan a la interfaz (no corren en el control)");
    atender(200);
    CHECK(avisosVista.load() == operaciones.load() && avisosFuera.load() == 0,
          "8: al volver, la interfaz recibe cada aviso, en su hilo");

    // La interfaz trabada ADENTRO del motor (con el turno) más que el tope: «ocupado».
    {
        std::string r;
        double ms = -1;
        const int antes = operaciones.load();
        const std::uint64_t ocupadoAntes = core.motorOcupado();
        turno.tomar();
        std::thread c([&]() {
            const auto t = std::chrono::steady_clock::now();
            r = llamar(http.port(), token, "scene_switch", 500, {{"name", "Plano general"}});
            ms = msDesde(t);
        });
        trabarInterfaz(1500);
        turno.soltar();
        c.join();
        atender(300);
        const json j = json::parse(r, nullptr, false);
        const json st = j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        std::printf("  medido: con el turno en la interfaz, «motor ocupado» a los %.1f ms (tope 800)\n", ms);
        CHECK(j.is_object() && j["result"].value("isError", false) && st.value("motor_ocupado", false) &&
                  st.value("turno", json::object()).value("quien", std::string()) == "interfaz" && st.contains("foto"),
              "8: trabada adentro del motor: «motor ocupado», con quién lo tiene y la foto");
        CHECK(ms >= 750 && ms < 1400, "8: al vencer el tope");
        CHECK(operaciones.load() == antes && aire.alAire == "Cierre" && core.motorOcupado() == ocupadoAntes + 1,
              "8: y no se hizo después");
    }
    // Menos que el tope: el control espera y lo hace.
    {
        std::string r;
        double ms = -1;
        turno.tomar();
        std::thread c([&]() {
            const auto t = std::chrono::steady_clock::now();
            r = llamar(http.port(), token, "scene_switch", 501, {{"name", "Plano general"}});
            ms = msDesde(t);
        });
        trabarInterfaz(300);
        turno.soltar();
        trabarInterfaz(300);
        c.join();
        const json j = json::parse(r, nullptr, false);
        CHECK(j.is_object() && !j["result"].value("isError", true) && ms >= 250 && aire.alAire == "Plano general" &&
                  doc8.foto().de("principal").escena == "Plano general",
              "8: el turno suelto antes del tope: el control espera y lo hace");
        std::printf("  medido: esperando el turno 300 ms, contestó a los %.1f ms; turno %s\n", ms, turno.estado().dump().c_str());
    }
    atender(200);
    CHECK(ba::persist::cola().vaciar(), "8: el documento de aire quedó escrito");
    fs::remove_all(datos8);

    // --- 10. La ventanilla: la interfaz pide y lee la foto; el motor es del control ------
    {
        using ba::app::FotoMotor;
        using ba::app::TurnoMotor;
        using ba::app::VentanillaMotor;
        // Un motor sintético cuyo dueño es el control: lo tocan las órdenes (en la agenda,
        // con el turno) y lo lee quien arma la foto (también ahí).
        struct Motor {
            std::vector<std::string> escenas{"Plano general", "Entrevista", "Cierre"};
            std::map<std::string, std::vector<std::string>> fuentes{{"Plano general", {"Cámara 1"}},
                                                                    {"Entrevista", {"Cámara 2", "Zócalo"}},
                                                                    {"Cierre", {}}};
            std::string alAire = "Plano general", armada;
            bool graba = false;
        } m;
        std::atomic<int> ordenes{0}, ordenesEnLaInterfaz{0}, ordenesSinTurno{0}, fotosSinTurno{0};
        std::shared_ptr<const FotoMotor> ultima;
        VentanillaMotor* ventanilla = nullptr;
        // Lo que hace AppRuntime::publicarFoto: arma, compara por partes y publica.
        const auto publicar = [&]() {
            if (!turno.loTengo()) ++fotosSinTurno;
            auto f = std::make_shared<FotoMotor>();
            for (const std::string& e : m.escenas) {
                FotoMotor::Escena x;
                x.nombre = e;
                for (const std::string& fu : m.fuentes[e]) {
                    FotoMotor::Fuente y;
                    y.nombre = fu;
                    y.tipo = "ba_color";
                    x.fuentes.push_back(std::move(y));
                }
                f->escenas.push_back(std::move(x));
            }
            FotoMotor::Salida s;
            s.id = "principal";
            s.porOmision = true;
            s.alAire = m.alAire;
            s.armada = m.armada;
            s.grabando = m.graba;
            f->salidas.push_back(s);
            f->estado.graba = m.graba;
            unsigned partes = FotoMotor::Todas;
            if (ultima) {
                partes = 0;
                if (f->escenas != ultima->escenas) partes |= FotoMotor::Escenas;
                if (f->salidas != ultima->salidas) partes |= FotoMotor::Salidas;
                if (f->estado != ultima->estado) partes |= FotoMotor::Estado;
            }
            if (partes == 0) return;
            ultima = f;
            ventanilla->publicar(f, partes);
        };
        VentanillaMotor v({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                           .alTerminarOrden = publicar,
                           .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
        ventanilla = &v;
        // La primera foto, como al arrancar (el dueño todavía era este hilo).
        turno.tomar();
        publicar();
        turno.soltar();
        v.recibirAhora();
        CHECK(v.foto().salida({}) != nullptr && v.foto().salida({})->alAire == "Plano general" && v.foto().escenas.size() == 3,
              "10: la primera foto está antes de armar las vistas");
        // Lo que ve la interfaz: el orden de lo que le llega (las fotos y las respuestas).
        std::vector<std::string> llegadas;
        unsigned partesRecibidas = 0;
        QObject::connect(&v, &VentanillaMotor::cambio, &v, [&](unsigned p) {
            llegadas.push_back("foto");
            partesRecibidas |= p;
        });
        QObject quien; // la vista que pide (si se fuera, la respuesta no le llega)
        const auto operar = [&](const std::function<void()>& cambio) {
            ++ordenes;
            if (QThread::currentThread() == interfaz) ++ordenesEnLaInterfaz;
            if (!turno.loTengo()) ++ordenesSinTurno;
            cambio();
        };
        std::vector<double> latencias; // de pedirla a hecha (en el control)
        std::mutex latMu;
        const auto pedirOrden = [&](const char* que, std::function<void()> cambio) {
            const auto desde = std::chrono::steady_clock::now();
            v.pedir(
                &quien, que,
                [&, que, desde, cambio]() {
                    operar(cambio);
                    const std::lock_guard<std::mutex> l(latMu);
                    latencias.push_back(msDesde(desde));
                    return std::string(que);
                },
                [&](const std::string& r) { llegadas.push_back("respuesta " + r); });
        };
        // Lo que pide la interfaz (cortar, armar, TAKE, agregar y mover una fuente, grabar)
        // y enseguida se traba.
        const int controlAntes9 = control.load();
        pedirOrden("cortar", [&]() { m.alAire = "Entrevista"; });
        pedirOrden("armar", [&]() { m.armada = "Cierre"; });
        pedirOrden("take", [&]() { std::swap(m.alAire, m.armada); });
        pedirOrden("fuente_agregar", [&]() { m.fuentes["Cierre"].push_back("Placa final"); });
        pedirOrden("fuente_mover", [&]() { std::reverse(m.fuentes["Entrevista"].begin(), m.fuentes["Entrevista"].end()); });
        pedirOrden("grabar", [&]() { m.graba = true; });
        trabarInterfaz(1500);
        // Todavía trabada (sin atender eventos): lo pedido ya se hizo en el control, y la foto
        // de la interfaz es la de antes (no cambia debajo de sus pies).
        const int hechasEnLaTraba = ordenes.load();
        const bool fotoQuieta = v.foto().salida({})->alAire == "Plano general" && !v.foto().estado.graba;
        double peorOrden = 0;
        {
            const std::lock_guard<std::mutex> l(latMu);
            for (double x : latencias) peorOrden = std::max(peorOrden, x);
        }
        std::printf("  medido: %d órdenes hechas con la interfaz trabada, la peor a los %.1f ms de pedirla; %d vueltas del control\n",
                    hechasEnLaTraba, peorOrden, control.load() - controlAntes9);
        CHECK(hechasEnLaTraba == 6 && peorOrden < 200, "10: con la interfaz trabada, lo pedido se hace en el acto");
        CHECK(ordenesEnLaInterfaz.load() == 0 && ordenesSinTurno.load() == 0 && fotosSinTurno.load() == 0,
              "10: en el control y con el turno (la foto también)");
        CHECK(fotoQuieta && llegadas.empty(), "10: la foto de la interfaz no cambió durante la traba");
        atender(200); // la interfaz vuelve
        const FotoMotor& f = v.foto();
        const FotoMotor::Salida* s = f.salida({});
        CHECK(s != nullptr && s->alAire == "Cierre" && s->armada == "Entrevista" && s->grabando && f.estado.graba &&
                  f.fuentesDe("Cierre") == std::vector<std::string>{"Placa final"} &&
                  f.fuentesDe("Entrevista") == (std::vector<std::string>{"Zócalo", "Cámara 2"}),
              "10: al volver, la foto tiene todo lo hecho");
        CHECK((partesRecibidas & (FotoMotor::Escenas | FotoMotor::Salidas | FotoMotor::Estado)) ==
                  (FotoMotor::Escenas | FotoMotor::Salidas | FotoMotor::Estado),
              "10: con las partes que cambiaron (escenas, salidas, estado)");
        const auto respuestas =
            std::count_if(llegadas.begin(), llegadas.end(), [](const std::string& x) { return x.rfind("respuesta", 0) == 0; });
        CHECK(!llegadas.empty() && llegadas.front() == "foto" && respuestas == 6, "10: las respuestas llegan, y después de la foto");

        // La red de seguridad: la interfaz entra directo (por vuelta) y se traba adentro.
        TurnoMotor::participarPorVuelta("interfaz");
        QObject::connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock, &v,
                         []() { TurnoMotor::soltarVuelta(); });
        const std::uint64_t directasAntes = turno.totalDirectas();
        const std::uint64_t reintentosAntes = plano.estado()["agenda"].value("reintentos", std::uint64_t{0});
        std::atomic<double> esperada{-1};
        std::atomic<int> controlEnElEvento{0};
        QTimer::singleShot(0, &v, [&]() {
            // Un área sin migrar que toca el motor (AppRuntime::config/engine): el turno es de
            // la interfaz hasta que termina este evento.
            turno.entrarDirecto(std::source_location::current());
            const auto desde = std::chrono::steady_clock::now();
            const int c0 = control.load();
            v.pedir(
                &quien, "esperando",
                [&, desde]() {
                    operar([]() {});
                    esperada = msDesde(desde);
                    return std::string("esperando");
                },
                [](const std::string&) {});
            trabarInterfaz(600);
            controlEnElEvento = control.load() - c0;
        });
        atender(1000);
        const std::uint64_t reintentos = plano.estado()["agenda"].value("reintentos", std::uint64_t{0}) - reintentosAntes;
        std::printf("  medido: con el turno en la interfaz 600 ms, la orden se hizo a los %.1f ms; %d vueltas del control "
                    "mientras; %llu reintentos de la agenda; directas %s\n",
                    esperada.load(), controlEnElEvento.load(), static_cast<unsigned long long>(reintentos),
                    turno.directas().dump().c_str());
        CHECK(esperada.load() >= 550 && esperada.load() < 900, "10: la orden esperó a que la interfaz soltara el turno, y se hizo");
        CHECK(controlEnElEvento.load() >= 15 && reintentos > 0, "10: mientras tanto el control siguió (la agenda reintenta, no espera)");
        CHECK(turno.totalDirectas() == directasAntes + 1 && turno.directas()["lugares"].contains("main"),
              "10: la entrada directa quedó contada, con su lugar");
        CHECK(!turno.loTengo(), "10: al terminar el evento la interfaz soltó el turno");
        TurnoMotor::participar("interfaz");
    }

    // --- R. El resto: la configuración y el equipo, en la foto; aplicar, en el control ----
    // Lo que hacen SettingsModel (aplicar), la voz (el micrófono del operador), el asistente
    // (el modelo local) y los chequeos del equipo: leen las partes Configuracion y Equipo y
    // le piden al dueño. La configuración la cambia SOLO el control; la interfaz tiene copias
    // que no cambian (las de su foto), aunque el dueño cambie la suya mientras está trabada.
    {
        using ba::app::AppConfig;
        using ba::app::FotoMotor;
        using ba::app::GatewaySettings;
        using ba::app::LauncherSettings;
        using ba::app::VentanillaMotor;
        struct Dueno {
            AppConfig cfg;
            GatewaySettings gw;
            LauncherSettings la;
            std::string reloj = "sistema";
            unsigned sucia = FotoMotor::Todas;
        } d;
        d.cfg.mcpAprobacionMin = 2;
        d.cfg.operatorAudio = "Mic 1";
        d.gw.room = "Sala";
        d.la.idioma = "es";
        std::shared_ptr<const FotoMotor> ultima;
        VentanillaMotor* ventanilla = nullptr;
        std::atomic<int> fotosSinTurno{0}, ordenesEnLaInterfaz{0};
        // Como AppRuntime::armarFoto: copias nuevas solo de lo sucio; se comparan por puntero.
        const auto publicar = [&]() {
            if (!turno.loTengo()) ++fotosSinTurno;
            auto f = std::make_shared<FotoMotor>(ultima ? *ultima : FotoMotor{});
            if ((d.sucia & FotoMotor::Configuracion) != 0) {
                f->configuracion.app = std::make_shared<const AppConfig>(d.cfg);
                f->configuracion.gateway = std::make_shared<const GatewaySettings>(d.gw);
                f->configuracion.launcher = std::make_shared<const LauncherSettings>(d.la);
            }
            if ((d.sucia & FotoMotor::Equipo) != 0) f->equipo.relojTipo = d.reloj;
            d.sucia = 0;
            unsigned partes = FotoMotor::Todas;
            if (ultima) {
                partes = 0;
                if (f->configuracion != ultima->configuracion) partes |= FotoMotor::Configuracion;
                if (f->equipo != ultima->equipo) partes |= FotoMotor::Equipo;
            }
            if (partes == 0) return;
            ultima = f;
            ventanilla->publicar(f, partes);
        };
        VentanillaMotor v({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                           .alTerminarOrden = publicar,
                           .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
        ventanilla = &v;
        turno.tomar();
        publicar();
        turno.soltar();
        v.recibirAhora();
        CHECK(v.foto().configuracion.app != nullptr && v.foto().configuracion.app->operatorAudio == "Mic 1" &&
                  v.foto().configuracion.gateway->room == "Sala" && v.foto().configuracion.launcher->idioma == "es",
              "R: la configuración está en la primera foto");
        std::vector<std::string> llegadas;
        unsigned partesRecibidas = 0;
        QObject::connect(&v, &VentanillaMotor::cambio, &v, [&](unsigned p) {
            llegadas.push_back("foto");
            partesRecibidas |= p;
        });
        QObject formulario;
        // Lo que tenía la interfaz antes de pedir (un formulario que lo guardó, la voz).
        const auto antes = v.fotoCompartida();
        // Aplicar (SettingsModel::aplicarEn), el micrófono (Voz::setMicrofono) y el reloj.
        const auto desde = std::chrono::steady_clock::now();
        std::atomic<double> aplicadaMs{-1};
        v.pedir(
            &formulario, "ajustes_aplicar",
            [&]() {
                if (QThread::currentThread() == interfaz) ++ordenesEnLaInterfaz;
                d.cfg.mcpAprobacionMin = 7;
                d.gw.room = "Sala R";
                d.la.idioma = "pt";
                d.sucia |= FotoMotor::Configuracion;
                aplicadaMs = msDesde(desde);
                return std::string("Configuracion guardada.");
            },
            [&](const std::string& r) { llegadas.push_back("respuesta " + r); });
        v.pedir(
            &formulario, "voz_microfono",
            [&]() {
                if (QThread::currentThread() == interfaz) ++ordenesEnLaInterfaz;
                d.cfg.operatorAudio = "Mic 2";
                d.sucia |= FotoMotor::Configuracion;
                return true;
            },
            [&](bool) { llegadas.push_back("respuesta microfono"); });
        v.pedir("reloj", [&]() {
            d.reloj = "ptp";
            d.sucia |= FotoMotor::Equipo;
        });
        trabarInterfaz(1000);
        const bool quieta = v.foto().configuracion.app->mcpAprobacionMin == 2 && v.foto().configuracion.gateway->room == "Sala" &&
                            v.foto().equipo.relojTipo == "sistema" && llegadas.empty();
        std::printf("  medido: aplicar los ajustes con la interfaz trabada, hecho a los %.1f ms de pedirlo\n", aplicadaMs.load());
        CHECK(aplicadaMs.load() >= 0 && aplicadaMs.load() < 200 && ordenesEnLaInterfaz.load() == 0 && fotosSinTurno.load() == 0,
              "R: con la interfaz trabada, aplicar se hace en el control (con el turno)");
        CHECK(quieta, "R: la foto de la interfaz no cambió durante la traba");
        atender(200);
        const FotoMotor& f = v.foto();
        CHECK(f.configuracion.app->mcpAprobacionMin == 7 && f.configuracion.app->operatorAudio == "Mic 2" &&
                  f.configuracion.gateway->room == "Sala R" && f.configuracion.launcher->idioma == "pt" &&
                  f.equipo.relojTipo == "ptp",
              "R: al volver, la foto trae la configuración aplicada y el reloj");
        CHECK((partesRecibidas & (FotoMotor::Configuracion | FotoMotor::Equipo)) == (FotoMotor::Configuracion | FotoMotor::Equipo) &&
                  (partesRecibidas & (FotoMotor::Escenas | FotoMotor::Salidas)) == 0,
              "R: con sus partes (Configuracion, Equipo) y no otras");
        CHECK(antes->configuracion.app->mcpAprobacionMin == 2 && antes->configuracion.app->operatorAudio == "Mic 1" &&
                  antes->configuracion.gateway->room == "Sala",
              "R: la copia que tenía la interfaz no cambió debajo de sus pies");
        CHECK(!llegadas.empty() && llegadas.front() == "foto" &&
                  std::count(llegadas.begin(), llegadas.end(), std::string("respuesta Configuracion guardada.")) == 1 &&
                  std::count(llegadas.begin(), llegadas.end(), std::string("respuesta microfono")) == 1,
              "R: el resumen vuelve después de la foto (el formulario recarga con lo nuevo)");
    }

    plano.detener();
    CHECK(http.thread() == interfaz && vigia.thread() == interfaz && vigia9.thread() == interfaz, "6: lo adoptado vuelve a su hilo");
    pool.waitForDone();
    std::printf(g_fallos == 0 ? "test_plano_control: OK\n" : "test_plano_control: %d fallo(s)\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
