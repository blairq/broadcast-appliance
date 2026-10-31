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
//   I. IMAGEN (video/VideoGrafos es del dueño): un modelo del motor adoptado por el plano,
//      con su reloj de revisión (con el turno o salteando la vuelta), su vista en la parte
//      Video de la foto, la señal de cada entrada leída directo por el vigía y una
//      herramienta MCP del grafo en el control. Con la interfaz trabada: el grafo sigue
//      revisando en el control, la alarma «sin señal» sale en el vigía, lo que pidió la
//      interfaz y la herramienta se hacen; la vista de la interfaz no cambia y llega entera
//      al volver.
//   S. ASAMBLEA, SALA Y LÍNEA: la copia de linea_estado sembrada al registrarla y puesta al
//      día por la línea contesta en el acto con la interfaz trabada (recién arrancada
//      esperaba y fallaba); el histórico se lee en el pool con la raíz de los eventos; la
//      atención automática, el micrófono de la fila y el corte de la línea se piden y se
//      hacen en el control con la interfaz trabada, con la parte Sala de la foto al volver.
//   C. CONTROL Y DSK (control/Dsk.hpp: las capas son del dueño del motor), con el modelo de
//      verdad (CapasDsk, su archivo) y un motor sintético: lo que pide la interfaz (subir
//      una capa) y lo que pide una herramienta del control (subirla en otra salida) se
//      hacen en el control con el turno mientras la interfaz está trabada; lo que ve la
//      interfaz (control::Dsk) no cambia en la traba y al volver trae las dos, con la parte
//      Superficies; lo que no se puede se dice en el acto sin pedir nada, y lo que el motor
//      rechaza llega después como `fallo`.
//   M. LA MEZCLA (la parte Mezcla de la foto, el canal de niveles, las alarmas del audio):
//      con la interfaz trabada, el fader, el mute y el PFL pedidos se hacen en el control
//      con el turno, la foto de la mezcla no cambia en la interfaz y le llega al volver; los
//      niveles (que publica el hilo del motor) se leen del canal sin el turno; la saturación
//      y el silencio de cada micrófono salen en el control, leídos directo (no de la
//      interfaz); el pico del programa: la vista lo toma y el MCP lee el último.
// ============================================================================
#include "agent/AlertEngine.hpp"
#include "agent/EntradasAlarmas.hpp"
#include "arranque/AppSettings.hpp"
#include "arranque/PlanoControl.hpp"
#include "control/Dsk.hpp"
#include "emision/DocumentoAire.hpp"
#include "emision/NivelesMotor.hpp"
#include "emision/TurnoMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "evento/EventoStore.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpFoto.hpp"
#include "mcp/McpHttp.hpp"
#include "mcp/McpMando.hpp"
#include "persist/ArchivoConfig.hpp"
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
/// I. Un modelo del motor que vive en el dueño, como video/VideoGrafos: su reloj de revisión
/// (hijo, se muda con él) corre con el turno o saltea la vuelta; publica su vista.
struct GrafoDelDueno : QObject {
    ba::app::TurnoMotor& turno;
    const QThread* interfaz;
    std::atomic<bool>& camaraConCuadros; ///< el «motor»: si llegan cuadros de la cámara
    QTimer* revision = nullptr;
    std::atomic<int> revisiones{0}, enLaInterfaz{0}, sinTurno{0};
    std::vector<std::string> tarjetas;
    json vista = json::object();
    std::function<void()> publicar; ///< AppRuntime::fotoSucia(Video) + publicarFoto
    GrafoDelDueno(ba::app::TurnoMotor& t, const QThread* i, std::atomic<bool>& c) : turno(t), interfaz(i), camaraConCuadros(c) {
        revision = new QTimer(this);
        revision->setInterval(50);
        QObject::connect(revision, &QTimer::timeout, this, [this]() {
            if (!turno.tomarHasta(0)) return; // la interfaz lo tiene: la próxima
            revisar();
            turno.soltar();
        });
        revision->start();
    }
    /// En el dueño, con el turno (VideoGrafos::sincronizar → publicarVista).
    void revisar() {
        ++revisiones;
        if (QThread::currentThread() == interfaz) ++enLaInterfaz;
        if (!turno.loTengo()) ++sinTurno;
        json v = json::object();
        v["entradas"] = json::array({"Cámara 1"});
        v["grafos"]["Cámara 1"]["tarjetas"] = tarjetas;
        v["estados"]["Cámara 1"]["gstreamer"]["sin_datos_s"] = camaraConCuadros ? 0.1 : 5.0;
        if (v == vista) return;
        vista = std::move(v);
        if (publicar) publicar();
    }
    /// Lo que lee el vigía (AlarmasViewModel: directas.senales, con el turno y sin esperar).
    std::vector<std::pair<std::string, bool>> senales() const {
        const double sin = vista.value("estados", json::object())
                               .value("Cámara 1", json::object())
                               .value("gstreamer", json::object())
                               .value("sin_datos_s", 0.0);
        return {{"Cámara 1", sin >= 0 && sin < 1.5}};
    }
};
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

    // --- S. Asamblea, sala y línea de tiempo (F3-03) ----------------------------------------
    // (a) La línea vive en la interfaz y el MCP la lee por copia (linea_estado, linea_ver):
    // la copia se SIEMBRA al registrarla y la línea la pone al día cuando cambia
    // (CopiaDeLaInterfaz::copiarYa). Recién arrancada, con la interfaz trabada, contesta en
    // el acto con lo último (antes esperaba la primera copia 5 s y fallaba).
    {
        std::atomic<int> pasoS{1};
        std::shared_ptr<CopiaDeLaInterfaz> copiaS;
        {
            Tool t;
            t.name = "linea_estado_sembrada";
            t.clase = Clase::Lectura;
            copiaS = copiarDeLaInterfaz(
                core, t, [&](const json&) -> LeidoEnLaInterfaz { return {json{{"paso", pasoS.load()}}, {}}; },
                EsperaDeLaCopia{100, 3000});
            core.addTool(std::move(t));
        }
        copiaS->copiarYa(); // al registrarla, en la interfaz
        const auto llamarTrabada = [&](int id, double& ms) {
            std::string r;
            std::thread c([&]() {
                const auto t0 = std::chrono::steady_clock::now();
                r = llamar(http.port(), token, "linea_estado_sembrada", id, json::object());
                ms = msDesde(t0);
            });
            trabarInterfaz(800);
            c.join();
            const json j = json::parse(r, nullptr, false);
            return j.is_object() ? j["result"].value("structuredContent", json::object()) : json::object();
        };
        double ms1 = -1, ms2 = -1;
        const json s1 = llamarTrabada(950, ms1);
        // La línea avanzó (la interfaz, libre un momento, deja la copia al día al cambiar).
        pasoS = 2;
        copiaS->copiarYa();
        const json s2 = llamarTrabada(951, ms2);
        std::printf("  medido: linea_estado sembrada con la interfaz trabada: %.1f ms y %.1f ms (sin sembrar esperaba 3000)\n", ms1, ms2);
        CHECK(s1.value("paso", 0) == 1 && ms1 < 600 && s1.value("interfaz", json::object()).value("edad_ms", -1) >= 0,
              "S: recién registrada y con la interfaz trabada, la copia sembrada contesta en el acto");
        CHECK(s2.value("paso", 0) == 2 && ms2 < 600, "S: la línea deja la copia al día cuando cambia (sin que nadie pregunte)");
        atender(200);
    }

    // (b) El histórico (evento_bitacora, evento_grabaciones, evento_sellos, historico_buscar)
    // se lee con la raíz de los eventos, en el pool: con la interfaz trabada encuentra el
    // evento en curso y su carpeta (EventoStore::carpetaExistente, solo disco).
    {
        const fs::path raizS = fs::temp_directory_path() / ("ba-test-plano-s-" + std::to_string(QCoreApplication::applicationPid()));
        fs::remove_all(raizS);
        ba::evento::EventoStore store(raizS);
        std::string err;
        const std::string carpeta = store.crear("Sesión ordinaria", &err);
        const bool enCurso = !carpeta.empty() && store.iniciar(carpeta, 1000, &err);
        std::filesystem::path enElPool, deOtro;
        double msPool = -1;
        QThreadPool::globalInstance()->start([&]() {
            const auto t0 = std::chrono::steady_clock::now();
            const ba::evento::EventoStore s(raizS);
            enElPool = s.carpetaExistente({});
            deOtro = s.carpetaExistente("no/existe");
            msPool = msDesde(t0);
        });
        trabarInterfaz(400);
        QThreadPool::globalInstance()->waitForDone();
        CHECK(enCurso && enElPool == raizS / carpeta && deOtro.empty() && msPool >= 0 && msPool < 300,
              "S: con la interfaz trabada, el pool encuentra el evento en curso con la raíz de los eventos");
        fs::remove_all(raizS);
    }

    // (c) Lo que la sala, la asamblea y la línea le hacen al motor se PIDE (corre en el
    // control, con el turno) y lo que leen sale de la foto, parte Sala: guardar la atención
    // automática, abrir un micrófono de la fila, cortar a la escena del paso. Lo pedido se
    // ve como pendiente hasta la respuesta (ControlSala::atencionAutomatica).
    {
        using ba::app::FotoMotor;
        using ba::app::VentanillaMotor;
        struct Motor {
            bool atencion = false;
            std::map<std::string, bool> mudo{{"Banca 3", true}};
            std::string alAire = "Plano general";
        } m;
        std::shared_ptr<const FotoMotor> ultima;
        VentanillaMotor* vs = nullptr;
        std::atomic<int> enLaInterfaz{0}, sinTurno{0};
        const auto publicar = [&]() {
            auto f = std::make_shared<FotoMotor>();
            f->sala.atencionAutomatica = m.atencion;
            f->sala.audio = {"Banca 3"};
            FotoMotor::Salida s;
            s.id = "principal";
            s.porOmision = true;
            s.alAire = m.alAire;
            f->salidas.push_back(s);
            unsigned partes = FotoMotor::Todas;
            if (ultima) {
                partes = 0;
                if (f->sala != ultima->sala) partes |= FotoMotor::Sala;
                if (f->salidas != ultima->salidas) partes |= FotoMotor::Salidas;
            }
            if (partes == 0) return;
            ultima = f;
            vs->publicar(f, partes);
        };
        VentanillaMotor v({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                           .alTerminarOrden = publicar,
                           .adoptar = {}});
        vs = &v;
        turno.tomar();
        publicar();
        turno.soltar();
        v.recibirAhora();
        unsigned partesS = 0;
        QObject::connect(&v, &VentanillaMotor::cambio, &v, [&](unsigned p) { partesS |= p; });
        QObject quien;
        std::optional<bool> atencionPedida; // el pendiente de ControlSala
        const auto atencion = [&]() { return atencionPedida ? *atencionPedida : v.foto().sala.atencionAutomatica; };
        std::vector<double> lat;
        std::mutex latMu;
        const auto enControl = [&](std::function<void()> fn) {
            const auto t0 = std::chrono::steady_clock::now();
            return [&, fn, t0]() {
                if (QThread::currentThread() == interfaz) ++enLaInterfaz;
                if (!turno.loTengo()) ++sinTurno;
                fn();
                const std::lock_guard<std::mutex> l(latMu);
                lat.push_back(msDesde(t0));
            };
        };
        atencionPedida = true;
        v.pedir(&quien, "sala_atencion", enControl([&]() { m.atencion = true; }), [&]() { atencionPedida.reset(); });
        v.pedir("asamblea_microfono", enControl([&]() { m.mudo["Banca 3"] = false; }));
        v.pedir("linea_tomar", enControl([&]() { m.alAire = "Orador"; }));
        const bool pendienteVisto = atencion();
        trabarInterfaz(1000);
        double peor = 0;
        {
            const std::lock_guard<std::mutex> l(latMu);
            for (double x : lat) peor = std::max(peor, x);
        }
        const bool quieta = !v.foto().sala.atencionAutomatica && v.foto().salida({})->alAire == "Plano general";
        std::printf("  medido: sala/asamblea/línea con la interfaz trabada: %zu órdenes, la peor a los %.1f ms\n", lat.size(), peor);
        CHECK(lat.size() == 3 && peor < 200 && enLaInterfaz.load() == 0 && sinTurno.load() == 0,
              "S: con la interfaz trabada, la atención, el micrófono y el corte de la línea se hacen en el control");
        CHECK(pendienteVisto && quieta, "S: lo pedido se ve como pendiente; la foto no cambia durante la traba");
        atender(200);
        CHECK(!atencionPedida && atencion() && v.foto().salida({})->alAire == "Orador" && !m.mudo["Banca 3"] &&
                  (partesS & (FotoMotor::Sala | FotoMotor::Salidas)) == (FotoMotor::Sala | FotoMotor::Salidas),
              "S: al volver, la foto trae la parte Sala y el corte, y el pendiente se suelta");
    }

    // --- C. Control y DSK: las capas son del dueño del motor; la interfaz pide y lee la foto --
    {
        using ba::app::FotoMotor;
        using ba::app::VentanillaMotor;
        using ba::app::control::CapasDsk;
        const fs::path datosC = fs::temp_directory_path() / "ba-test-plano-control-dsk";
        fs::remove_all(datosC);
        fs::create_directories(datosC / "Config");
        // El motor sintético: qué capa está arriba en cada salida; lo que se le pide, contado
        // por hilo y por turno.
        std::map<std::string, std::map<int, bool>> arriba;
        std::atomic<int> llamadasMotor{0}, motorEnLaInterfaz{0}, motorSinTurno{0};
        const auto contar = [&]() {
            ++llamadasMotor;
            if (QThread::currentThread() == interfaz) ++motorEnLaInterfaz;
            if (!turno.loTengo()) ++motorSinTurno;
        };
        CapasDsk::Motor m;
        m.armar = [&](int, const std::vector<std::string>& cont, const json&, const std::string&) {
            contar();
            return cont.empty() || cont.front() != "rota"; // una fuente que el motor no tiene
        };
        m.mostrar = [&](int capa, bool visible, int, const std::string& salida) {
            contar();
            arriba[salida.empty() ? "principal" : salida][capa] = visible;
            return true;
        };
        m.estado = [](const std::string&) { return json::array(); };
        m.error = []() { return std::string("no hay una fuente «rota»"); };
        CapasDsk capas(datosC, std::move(m));
        std::shared_ptr<const FotoMotor> ultimaC;
        VentanillaMotor* vp = nullptr;
        // Lo que hacen AppRuntime::publicarFoto y el aporte de ControlViewModel.
        const auto publicarC = [&]() {
            auto f = std::make_shared<FotoMotor>();
            for (const char* id : {"principal", "sala"}) {
                FotoMotor::Salida s;
                s.id = id;
                s.porOmision = std::string(id) == "principal";
                f->salidas.push_back(s);
            }
            f->dsk = capas.paraLaFoto({"principal", "sala"});
            unsigned partes = FotoMotor::Todas;
            if (ultimaC) partes = f->dsk != ultimaC->dsk ? static_cast<unsigned>(FotoMotor::Superficies) : 0u;
            if (partes == 0) return;
            ultimaC = f;
            vp->publicar(f, partes);
        };
        VentanillaMotor vC({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                            .alTerminarOrden = publicarC,
                            .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
        vp = &vC;
        // Lo que cambia una herramienta del control ensucia la foto (AppRuntime::fotoSucia).
        capas.setAlCambiar([&]() { plano.encolarConTurno(publicarC); });
        turno.tomar();
        publicarC();
        turno.soltar();
        vC.recibirAhora();
        ba::app::control::Dsk dsk(capas, vC);
        int cambiosUi = 0, avisosBus = 0;
        std::vector<QString> fallos;
        QObject::connect(&dsk, &ba::app::control::Dsk::cambio, &dsk, [&]() { ++cambiosUi; });
        QObject::connect(&dsk, &ba::app::control::Dsk::fallo, &dsk, [&](const QString& e) { fallos.push_back(e); });
        dsk.setAlCambiar([&](const json&) { ++avisosBus; });
        // Una herramienta del control como dsk_mostrar (mcp/McpBotoneraTools.cpp): al modelo, en el control.
        {
            Tool t;
            t.name = "dsk_mostrar_c";
            t.clase = Clase::Escritura;
            t.hilo = Hilo::Control;
            t.run = [&capas](const json& a, Done done) {
                const std::string e = capas.mostrar(a.value("capa", 1), a.value("visible", true), false, a.value("salida", std::string()));
                done(e.empty() ? ToolResult::ok("arriba") : ToolResult::error(e));
            };
            core.addTool(std::move(t));
        }
        // Armar dos capas (desde la interfaz) y volver.
        CHECK(dsk.definir(1, {{"nombre", "Logo"}, {"contenido", {"Mosca"}}}).empty() &&
                  dsk.plantillaLsa(2, "Cámara LSA", "abajo_derecha", 0.28).empty(),
              "C: definir se pide (no espera al motor)");
        atender(200);
        CHECK(dsk.capas().size() == 4 && dsk.capas()[0].value("nombre", std::string()) == "Logo" && dsk.fija(2),
              "C: la interfaz ve las definiciones en la foto");
        const int llamadasAntes = llamadasMotor.load();
        const int cambiosAntes = cambiosUi;
        // Lo que no se puede, en el acto y sin pedir nada.
        CHECK(!dsk.mostrar(3, true, false).empty() && !dsk.mostrar(2, false, false).empty() && !dsk.mostrar(9, true, false).empty(),
              "C: vacía, fija sin confirmar o fuera de rango: el error en el acto");
        // La interfaz sube la 1 y se traba; mientras, una herramienta sube la 2 en «sala».
        CHECK(dsk.mostrarUi(1, true, false).isEmpty(), "C: subir se pide");
        std::string rC;
        double msC = -1;
        std::thread cliente([&]() {
            const auto t = std::chrono::steady_clock::now();
            rC = llamar(http.port(), token, "dsk_mostrar_c", 700, {{"capa", 2}, {"visible", true}, {"salida", "sala"}});
            msC = msDesde(t);
        });
        trabarInterfaz(1200);
        cliente.join();
        const bool hechoEnLaTraba = arriba["principal"][1] && arriba["sala"][2];
        const bool vistaQuieta = !dsk.arriba(1) && !dsk.arriba(2, "sala") && cambiosUi == cambiosAntes;
        std::printf("  medido: con la interfaz trabada, la capa subió en el control (la herramienta contestó a los %.1f ms); "
                    "%d llamadas al motor\n",
                    msC, llamadasMotor.load() - llamadasAntes);
        const json jC = json::parse(rC, nullptr, false);
        CHECK(jC.is_object() && !jC["result"].value("isError", true) && msC < 600 && hechoEnLaTraba,
              "C: con la interfaz trabada, lo pedido (la interfaz y la herramienta) se hace en el acto");
        CHECK(motorEnLaInterfaz.load() == 0 && motorSinTurno.load() == 0, "C: el motor, solo en el control y con el turno");
        CHECK(vistaQuieta, "C: lo que ve la interfaz no cambia durante la traba");
        atender(200);
        CHECK(dsk.arriba(1) && !dsk.arriba(2) && dsk.arriba(2, "sala") && !dsk.arriba(1, "sala") && cambiosUi > cambiosAntes &&
                  avisosBus > 0,
              "C: al volver, la foto trae las dos (cada salida la suya) y la vista y el bus se enteran");
        // Lo que el motor rechaza: se pide y llega como fallo.
        CHECK(dsk.definir(3, {{"contenido", {"rota"}}}).empty(), "C: definir con una fuente que el motor no tiene se pide");
        atender(200);
        CHECK(fallos.size() == 1 && fallos[0].contains("rota") && dsk.capas()[2].value("contenido", json::array()).empty(),
              "C: el motor la rechaza: llega como fallo y no queda guardada");
        CHECK(ba::persist::cola().vaciar(), "C: el archivo de las capas quedó escrito");
        const json guardado = ba::persist::leerConfig(datosC / "Config" / "dsk.json");
        bool enArchivo = false;
        for (const auto& c : guardado.value("capas", json::array()))
            if (c.value("capa", 0) == 2) enArchivo = c.value("visible_en", json::object()).value("sala", false);
        CHECK(enArchivo, "C: y dice lo que está arriba en cada salida (vuelve tras un corte)");
        fs::remove_all(datosC);
    }

    // --- M. Mezcla: la foto de la mezcla, el canal de niveles y las alarmas del audio -------
    // (emision/FotoMotor.hpp «Mezcla», emision/NivelesMotor.hpp.) Con un motor de audio
    // sintético cuyo dueño es el control: lo que la interfaz pide (el fader, el mute, el
    // PFL) se hace en el control con el turno y la foto de la mezcla le llega al volver; los
    // niveles los publica «el hilo del motor» al canal y se leen sin el turno; y las alarmas
    // de cada micrófono (saturación, silencio) salen en el control con la interfaz trabada.
    using ba::app::FotoMotor;
    using ba::app::NivelesMotor;
    using ba::app::VentanillaMotor;
    struct Mezcla {
        std::map<std::string, float> volumen{{"Micrófono 1", 1.0f}, {"Micrófono 2", 1.0f}};
        std::map<std::string, bool> mudo{{"Micrófono 1", false}, {"Micrófono 2", false}};
        std::string pfl;
    } mzM;
    NivelesMotor canalM;
    std::atomic<int> ordenesM{0}, enLaInterfazM{0}, sinTurnoM{0};
    std::shared_ptr<const FotoMotor> ultimaM;
    std::mutex ultimaMu; // AppRuntime::ultimaFoto: la última publicada, para otros hilos
    VentanillaMotor* vmM = nullptr;
    // Lo que hace el dueño (AppRuntime::publicarFoto con armarMezcla): la parte Mezcla.
    const auto publicarM = [&]() {
        auto f = std::make_shared<FotoMotor>();
        f->mezcla.porOmision = "principal";
        for (const auto& [n, vM] : mzM.volumen) {
            FotoMotor::CanalAudio c;
            c.nombre = n;
            c.tipo = "ba_gst_fuente";
            c.volumen["principal"] = vM;
            c.mudo["principal"] = mzM.mudo[n];
            f->mezcla.canales.push_back(std::move(c));
            f->fuentes.emplace_back(n, "ba_gst_fuente");
        }
        f->mezcla.pfl = mzM.pfl;
        f->mezcla.pflModo = "pfl";
        unsigned partes = FotoMotor::Todas;
        if (ultimaM) partes = f->mezcla != ultimaM->mezcla ? static_cast<unsigned>(FotoMotor::Mezcla) : 0u;
        if (partes == 0) return;
        {
            const std::lock_guard<std::mutex> l(ultimaMu);
            ultimaM = f;
        }
        vmM->publicar(f, partes);
    };
    VentanillaMotor vM({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                       .alTerminarOrden = publicarM,
                       .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
    vmM = &vM;
    turno.tomar();
    publicarM();
    turno.soltar();
    vM.recibirAhora();
    unsigned partesM = 0;
    QObject::connect(&vM, &VentanillaMotor::cambio, &vM, [&](unsigned p) { partesM |= p; });

    // «El hilo del motor»: publica los niveles cada 40 ms (Motor::alPublicarNiveles).
    // El micrófono 1 satura y el 2 queda en silencio.
    std::atomic<bool> seguirMotorM{true};
    std::thread hiloMotorM([&]() {
        while (seguirMotorM) {
            auto n = std::make_shared<NivelesMotor::Niveles>();
            ba::motor::NivelAudio a, b, p;
            a.id = "Micrófono 1";
            a.pico[0] = a.pico[1] = -0.2f;
            b.id = "Micrófono 2";
            p.id = "programa";
            p.bus = true;
            p.pico[0] = -6.0f;
            n->push_back(a);
            n->push_back(b);
            n->push_back(p);
            canalM.ponerNiveles(std::move(n));
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
    // El reloj del motor (AppRuntime::ticMotor) suma el pico del programa al canal; la
    // interfaz lo toma (el vúmetro con su caída) y el MCP lee el de la última vuelta.
    std::atomic<bool> sumandoM{true};
    QTimer sumarM;
    QObject::connect(&sumarM, &QTimer::timeout, &sumarM, [&]() {
        if (sumandoM) canalM.sumarPrograma("", -3.0f, -4.0f);
    });
    sumarM.start(100);
    plano.adoptar(&sumarM);

    // Las alarmas de cada entrada de audio, directas (AlarmasViewModel::directas): de la
    // última foto publicada y del canal, en el hilo del vigía (el control).
    ba::app::agent::AlertInputs uiM, directasM;
    std::atomic<const QThread*> dondeLeyoAudioM{nullptr};
    directasM.audio = [&]() {
        dondeLeyoAudioM = QThread::currentThread();
        std::vector<ba::app::agent::AlertInputs::Audio> out;
        std::shared_ptr<const FotoMotor> f;
        {
            const std::lock_guard<std::mutex> l(ultimaMu);
            f = ultimaM;
        }
        if (f)
            for (const FotoMotor::CanalAudio& c : f->mezcla.canales)
                out.push_back({c.nombre, canalM.picoEntrada(c.nombre), f->mezcla.mudo(c.nombre)});
        return out;
    };
    ba::app::agent::EntradasAlarmas entradasM(uiM, directasM);
    ba::app::agent::AlertEngine vigiaM(entradasM.paraElVigia(), "");
    vigiaM.create({{"type", "audio_peak"}, {"name", "satura"}, {"params", {{"source", "Micrófono 1"}, {"holdMs", 200}}}});
    vigiaM.create({{"type", "audio_silence"},
                   {"name", "callado"},
                   {"params", {{"source", "Micrófono 2"}, {"forSec", 0.5}, {"onlyWhenLive", false}}}});
    std::mutex disparosMuM;
    std::vector<std::pair<std::string, const QThread*>> disparosM;
    QObject::connect(
        &vigiaM, &ba::app::agent::AlertEngine::fired, &vigiaM,
        [&](const json& ev) {
            const std::lock_guard<std::mutex> l(disparosMuM);
            disparosM.emplace_back(ev.value("ruleName", std::string()), QThread::currentThread());
        },
        Qt::DirectConnection);
    vigiaM.start(100);
    plano.adoptar(&vigiaM);

    // Lo que pide la interfaz (el fader, el mute, el PFL: MixerModel, Matriz::ponerPfl) y
    // enseguida se traba.
    QObject quienM;
    const auto pedirMz = [&](const char* que, std::function<void()> cambio) {
        vM.pedir(
            &quienM, que,
            [&, cambio]() {
                ++ordenesM;
                if (QThread::currentThread() == interfaz) ++enLaInterfazM;
                if (!turno.loTengo()) ++sinTurnoM;
                cambio();
            },
            []() {});
    };
    pedirMz("mezcla_volumen", [&]() { mzM.volumen["Micrófono 1"] = 0.5f; });
    pedirMz("mezcla_mute", [&]() { mzM.mudo["Micrófono 2"] = false; mzM.mudo["Micrófono 1"] = false; });
    pedirMz("matriz_pfl", [&]() { mzM.pfl = "Micrófono 2"; });
    const auto desdeTrabaM = std::chrono::steady_clock::now();
    std::vector<float> picosLeidosM; // lo que lee otro hilo del canal durante la traba
    std::thread lectorM([&]() {
        for (int i = 0; i < 10; ++i) {
            picosLeidosM.push_back(canalM.picoEntrada("Micrófono 1"));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    trabarInterfaz(1500);
    lectorM.join();
    const bool fotoQuietaM = vM.foto().mezcla.volumen("Micrófono 1") == 1.0f && vM.foto().mezcla.pfl.empty();
    std::vector<std::pair<std::string, const QThread*>> enLaTrabaM;
    {
        const std::lock_guard<std::mutex> l(disparosMuM);
        enLaTrabaM = disparosM;
    }
    const bool saturaEnControl = std::any_of(enLaTrabaM.begin(), enLaTrabaM.end(), [&](const auto& d) {
        return d.first == "satura" && d.second == plano.hilo();
    });
    const bool calladoEnControl = std::any_of(enLaTrabaM.begin(), enLaTrabaM.end(), [&](const auto& d) {
        return d.first == "callado" && d.second == plano.hilo();
    });
    std::printf("  medido: M: %d órdenes de la mezcla hechas con la interfaz trabada (%.0f ms); %zu alarmas de audio en la traba; "
                "%zu lecturas del canal desde otro hilo\n",
                ordenesM.load(), msDesde(desdeTrabaM), enLaTrabaM.size(), picosLeidosM.size());
    CHECK(ordenesM.load() == 3 && enLaInterfazM.load() == 0 && sinTurnoM.load() == 0,
          "M: con la interfaz trabada, el fader, el mute y el PFL se hacen en el control, con el turno");
    CHECK(fotoQuietaM, "M: la foto de la mezcla de la interfaz no cambió durante la traba");
    CHECK(!picosLeidosM.empty() && std::all_of(picosLeidosM.begin(), picosLeidosM.end(), [](float p) { return p > -1.0f; }),
          "M: los niveles se leen del canal sin el turno ni la interfaz");
    CHECK(saturaEnControl, "M: la saturación de un micrófono sale en el control con la interfaz trabada");
    CHECK(calladoEnControl, "M: el silencio de un micrófono también");
    CHECK(dondeLeyoAudioM.load() == plano.hilo(), "M: el audio de cada entrada lo lee el vigía (directo), no la interfaz");
    atender(200); // la interfaz vuelve
    CHECK(vM.foto().mezcla.volumen("Micrófono 1") == 0.5f && vM.foto().mezcla.pfl == "Micrófono 2" &&
              (partesM & FotoMotor::Mezcla) != 0,
          "M: al volver, la foto de la mezcla tiene lo pedido (la parte Mezcla)");
    // El pico del programa: la interfaz toma el mayor desde la vez anterior (y lo vacía);
    // el de la última vuelta queda para el MCP.
    canalM.setPorOmision("principal");
const auto tomadoM = canalM.tomarPrograma("");
    sumandoM = false;
    atender(150);
    const auto otraVezM = canalM.tomarPrograma("principal"); // su id es la por omisión
    canalM.setPorOmision("principal");
    const auto ultimoM = canalM.programa("principal");
    CHECK(tomadoM.first == -3.0f && tomadoM.second == -4.0f && otraVezM.first == -100.0f && ultimoM.first == -3.0f,
          "M: el pico del programa: la vista lo toma, el MCP lee el último sin robárselo");
    seguirMotorM = false;
    hiloMotorM.join();
    QMetaObject::invokeMethod(&sumarM, [&sumarM]() { sumarM.stop(); }, Qt::BlockingQueuedConnection);
    
    // --- I. Imagen: el grafo de video es del dueño del motor ----------------------------
    // Como en la app (video/VideoGrafos, ui/VideoViewModel, ui/AlarmasViewModel): un modelo
    // del motor que vive en el hilo de control (adoptado) con su reloj de revisión (con el
    // turno, salteando la vuelta si no está libre), que publica su vista en la parte Video
    // de la foto; la interfaz lo ve por la foto y le pide por la ventanilla; el vigía mira
    // la señal de cada entrada directo en él; una herramienta MCP del grafo corre en el
    // control. Con la interfaz trabada: la revisión sigue, la alarma «sin señal» sale, lo
    // pedido y la herramienta se hacen; la vista llega entera al volver.
    std::atomic<bool> camaraConCuadros{true};
    auto grafoI = std::make_unique<GrafoDelDueno>(turno, interfaz, camaraConCuadros);
    std::unique_ptr<ba::app::agent::AlertEngine> vigiaI;
    {
        using ba::app::FotoMotor;
        using ba::app::VentanillaMotor;
        std::shared_ptr<const FotoMotor> ultimaI;
        std::atomic<int> fotosSinTurno{0};
        VentanillaMotor* vI = nullptr;
        // Lo que hace AppRuntime::publicarFoto con el aporte de VideoGrafos (la parte Video).
        const auto publicarI = [&]() {
            if (!turno.loTengo()) ++fotosSinTurno;
            auto f = std::make_shared<FotoMotor>();
            f->video = grafoI->vista;
            if (ultimaI && f->video == ultimaI->video) return;
            ultimaI = f;
            vI->publicar(f, FotoMotor::Video);
        };
        VentanillaMotor ventI({.encolar = [&plano](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); },
                               .alTerminarOrden = publicarI,
                               .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
        vI = &ventI;
        // La vista de la interfaz (VideoViewModel): lee la parte Video cuando llega.
        nlohmann::json vistaInterfaz = nlohmann::json::object();
        unsigned partesI = 0;
        QObject::connect(&ventI, &VentanillaMotor::cambio, &ventI, [&](unsigned p) {
            partesI |= p;
            if ((p & FotoMotor::Video) != 0) vistaInterfaz = ventI.foto().video;
        });
        // Armado en la interfaz y adoptado al final (ImagenViewModel); publica con el turno.
        grafoI->publicar = [&]() { plano.encolarConTurno(publicarI); };
        plano.adoptar(grafoI.get());
        // El vigía: la señal de cada entrada, directa del grafo (en su hilo, con el turno).
        ba::app::agent::AlertInputs directasI;
        directasI.senales = [&, cache = std::make_shared<std::vector<std::pair<std::string, bool>>>()]() {
            if (turno.tomarHasta(50)) {
                *cache = grafoI->senales();
                turno.soltar();
            }
            return *cache;
        };
        ba::app::agent::EntradasAlarmas entradasI({}, directasI);
        vigiaI = std::make_unique<ba::app::agent::AlertEngine>(entradasI.paraElVigia(), "");
        vigiaI->create({{"type", "input_signal"}, {"name", "sin señal"}, {"params", {{"forSec", 0.3}}}});
        std::atomic<const QThread*> dondeSinSenal{nullptr};
        QObject::connect(
            vigiaI.get(), &ba::app::agent::AlertEngine::fired, vigiaI.get(),
            [&](const json& ev) {
                if (ev.value("type", std::string()) == "input_signal") dondeSinSenal = QThread::currentThread();
            },
            Qt::DirectConnection);
        vigiaI->start(100);
        plano.adoptar(vigiaI.get());
        // Una herramienta del grafo (como video_tarjeta_agregar): Hilo::Control.
        std::atomic<const QThread*> dondeHerramienta{nullptr};
        std::atomic<bool> herramientaConTurno{false};
        {
            Tool t;
            t.name = "video_sintetico";
            t.clase = Clase::Escritura;
            t.hilo = Hilo::Control;
            t.run = [&](const json&, Done done) {
                dondeHerramienta = QThread::currentThread();
                herramientaConTurno = turno.loTengo();
                grafoI->tarjetas.push_back("croma (MCP)");
                grafoI->revisar();
                done(ToolResult::ok("hecho"));
            };
            core.addTool(std::move(t));
        }
        atender(400);
        const nlohmann::json vistaAntes = vistaInterfaz;
        CHECK(vistaAntes.value("entradas", nlohmann::json::array()).size() == 1, "I: la vista del grafo llega a la interfaz por la foto");
        // Lo que pide la interfaz (VideoViewModel::agregar), y se traba; mientras, la cámara
        // pierde la señal y el asistente agrega otra tarjeta por MCP.
        const int revisionesAntes = grafoI->revisiones.load();
        std::atomic<const QThread*> dondeOrden{nullptr};
        ventI.pedir("video_agregar", [&]() {
            dondeOrden = QThread::currentThread();
            grafoI->tarjetas.push_back("color");
            grafoI->revisar();
        });
        std::string rMcp;
        double msMcp = -1;
        std::thread cliente([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            camaraConCuadros = false;
            const auto t = std::chrono::steady_clock::now();
            rMcp = llamar(http.port(), token, "video_sintetico", 901);
            msMcp = msDesde(t);
        });
        trabarInterfaz(1500);
        cliente.join();
        // Todavía trabada: la foto de la interfaz es la de antes.
        const bool quieta = vistaInterfaz == vistaAntes;
        const int revisionesEnLaTraba = grafoI->revisiones.load() - revisionesAntes;
        std::printf("  medido: %d revisiones del grafo con la interfaz trabada 1,5 s; la herramienta del grafo contestó a los %.1f ms\n",
                    revisionesEnLaTraba, msMcp);
        CHECK(revisionesEnLaTraba >= 10 && grafoI->enLaInterfaz.load() == 0 && grafoI->sinTurno.load() == 0,
              "I: con la interfaz trabada, el grafo sigue revisando en el control y con el turno");
        CHECK(dondeSinSenal.load() == plano.hilo(), "I: la alarma «sin señal» sale en el vigía con la interfaz trabada");
        CHECK(dondeOrden.load() == plano.hilo(), "I: lo que pidió la interfaz se hizo en el control durante la traba");
        const json jm = json::parse(rMcp, nullptr, false);
        CHECK(jm.is_object() && !jm["result"].value("isError", true) && msMcp < 500 && dondeHerramienta.load() == plano.hilo() &&
                  herramientaConTurno.load(),
              "I: la herramienta del grafo corre en el control, con el turno, con la interfaz trabada");
        CHECK(quieta, "I: la vista de la interfaz no cambió durante la traba");
        atender(300); // la interfaz vuelve
        const nlohmann::json tj = vistaInterfaz.value("grafos", nlohmann::json::object())
                                      .value("Cámara 1", nlohmann::json::object())
                                      .value("tarjetas", nlohmann::json::array());
        CHECK((partesI & FotoMotor::Video) != 0 && tj.size() == 2 &&
                  vistaInterfaz["estados"]["Cámara 1"]["gstreamer"].value("sin_datos_s", 0.0) > 1.5,
              "I: al volver, la vista tiene lo hecho (las dos tarjetas y la entrada sin señal)");
        CHECK(fotosSinTurno.load() == 0, "I: la foto de la vista se arma con el turno");
        grafoI->publicar = nullptr; // la ventanilla de esta sección se va
        plano.detener();
        vigiaI->disconnect(); // lo que anota los disparos era de esta sección
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
    CHECK(http.thread() == interfaz && vigia.thread() == interfaz && vigia9.thread() == interfaz && vigiaM.thread() == interfaz,
          "6: lo adoptado vuelve a su hilo");
    CHECK(grafoI->thread() == interfaz && vigiaI->thread() == interfaz, "I: el grafo y su vigía vuelven a la interfaz al parar");
    pool.waitForDone();
    std::printf(g_fallos == 0 ? "test_plano_control: OK\n" : "test_plano_control: %d fallo(s)\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
