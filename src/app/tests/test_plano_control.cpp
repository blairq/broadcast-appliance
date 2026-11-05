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
//
// LAS PARTES (PlanoControlPrueba.hpp): un solo programa y una sola corrida, en este orden; cada
// tema en su archivo, que se compilan a la vez: el principio (1-6) y el final (T y el DSK por
// escena) acá; 7 y 9 en plano_control_foto.cpp; 8 y 10 en plano_control_aire.cpp; S y C en
// plano_control_sala.cpp; M en plano_control_mezcla.cpp; R, E y R2 en plano_control_resto.cpp;
// I en plano_control_imagen.cpp.
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "control/Dsk.hpp"
#include "emision/NivelesMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "persist/ColaEscritura.hpp"

#include <QAbstractEventDispatcher>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using ba::app::PlanoControl;
using namespace ba::app::mcp;
using namespace ba::app::pruebas::plano;

namespace ba::app::pruebas::plano {

/// Lo último, con todo lo de las partes vivo: el plano se detiene y lo adoptado vuelve; T y el
/// contenido de una capa DSK por el id de su escena.
void probarFinal(Banco& b) {
    [[maybe_unused]] auto& app = b.app;
    [[maybe_unused]] const QThread* interfaz = b.interfaz;
    [[maybe_unused]] auto& control = b.control;
    [[maybe_unused]] auto& motor = b.motor;
    [[maybe_unused]] auto& latido = b.latido;
    [[maybe_unused]] auto& plano = b.plano;
    [[maybe_unused]] auto& turno = b.turno;
    [[maybe_unused]] auto& core = b.core;
    [[maybe_unused]] auto& pool = b.pool;
    [[maybe_unused]] const std::string& token = b.token;
    [[maybe_unused]] auto& http = b.http;
    auto& vigia = b.vigia;
    using ba::app::FotoMotor;
    using ba::app::NivelesMotor;
    using ba::app::VentanillaMotor;

    plano.detener();
    CHECK(http.thread() == interfaz && vigia.thread() == interfaz && b.vigia9->thread() == interfaz && b.vigiaM->thread() == interfaz,
          "6: lo adoptado vuelve a su hilo");
    CHECK(b.grafoI->thread() == interfaz && b.vigiaI->thread() == interfaz, "I: el grafo y su vigía vuelven a la interfaz al parar");
    pool.waitForDone();

    // --- T. La interfaz trabada 5 s NO frena el reloj del motor ni la agenda -----------------
    // Lo medido con 120 invitados: un evento de la interfaz de 13-21 s que entraba UNA vez al
    // motor tenía el turno todo el evento; el reloj del motor (AppRuntime::ticMotor: los
    // picos del programa, el loudness) y la agenda (la orden de lugares) se frenaban y el
    // audio de los retornos llegaba tarde. Con un reloj del motor como el de la app (mide
    // SIN el turno, NivelesMotor::medir, y hace lo demás CON el turno si se lo dan):
    //   (a) un método del motor con su Guarda, llamado desde la interfaz, toma el turno POR
    //       LLAMADA: después la interfaz trabada 5 s no lo tiene; el reloj hace TODO y lo
    //       pedido se hace en el acto;
    //   (b) un oyente de la foto que entra a cfg_ (por la vuelta) lo suelta al terminar: el
    //       oyente siguiente, trabado 2 s, no lo tiene (VentanillaMotor::escuchar);
    //   (c) la red de seguridad: un evento que tiene cfg_ 5 s (por la vuelta) frena lo que
    //       necesita el turno, pero NO lo que mide el reloj (los picos y el loudness llegan
    //       al canal de niveles en cada vuelta), y la tenencia queda anotada con su lugar.
    {
        using TM = ba::app::TurnoMotor;
        TM turnoT;
        NivelesMotor nivT;
        std::atomic<int> vueltasT{0}, conTurnoT{0}, sinTurnoT{0};
        std::atomic<int> picos{0}, loud{0};
        // Los medidores de una salida sintética (como BroadcastEngine::medidores).
        std::vector<NivelesMotor::Medidor> medidores{{"principal",
                                                      [&picos]() {
                                                          ++picos;
                                                          return std::pair<float, float>{-20.0f, -21.0f};
                                                      },
                                                      [&loud]() { return json{{"m", -23.0}, {"n", ++loud}}; }}};
        nivT.setPorOmision("principal");
        // El reloj del motor como AppRuntime::ticMotor (en el hilo de control: tomarHasta 5).
        PlanoControl planoT({.control = []() {},
                             .motor =
                                 [&]() {
                                     const int v = ++vueltasT;
                                     if (!turnoT.tomarHasta(TM::participa() ? 5 : 0)) {
                                         ++sinTurnoT;
                                         nivT.medir(v % NivelesMotor::kLoudnessCadaVueltas == 0);
                                         return;
                                     }
                                     ++conTurnoT;
                                     nivT.ponerMedidores(medidores);
                                     nivT.medir(v % NivelesMotor::kLoudnessCadaVueltas == 0);
                                     turnoT.soltar();
                                 },
                             .interfaz = []() {}});
        planoT.setTurno({[&turnoT](int ms) { return turnoT.tomarHasta(ms); }, [&turnoT]() { turnoT.soltar(); }});
        planoT.arrancar(20, 20);
        planoT.enControlYEsperar([]() { TM::participar("control"); });
        VentanillaMotor vT({.encolar = [&planoT](std::function<void()> fn) { planoT.encolarConTurno(std::move(fn)); },
                            .alTerminarOrden = []() {},
                            .adoptar = {}});
        QObject quienT;
        // La interfaz, como en la app: por vuelta, y suelta antes de dormir.
        TM::participarPorVuelta("interfaz");
        QObject::connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock, &quienT,
                         []() { TM::soltarVuelta(); });
        atender(200); // que el reloj guarde los medidores

        // Lo que pide la interfaz en medio de su traba: cuánto tarda en hacerse (en el control).
        const auto pedirYMedir = [&](std::atomic<double>& ms) {
            const auto desde = std::chrono::steady_clock::now();
            vT.pedir(&quienT, "t_orden", [&ms, desde]() { ms = msDesde(desde); }, []() {});
        };

        // (a) Un método del motor (su Guarda) y después 5 s de la interfaz en lo suyo.
        std::atomic<double> msA{-1};
        int conA = 0, sinA = 0;
        bool teniaA = true;
        const std::uint64_t directasA = turnoT.totalDirectas();
        QTimer::singleShot(0, &quienT, [&]() {
            {
                const TM::Guarda g(turnoT); // p. ej. rt.engine().sceneNames() desde una vista
            }
            teniaA = turnoT.loTengo();
            pedirYMedir(msA);
            const int c0 = conTurnoT.load(), s0 = sinTurnoT.load();
            trabarInterfaz(5000);
            conA = conTurnoT.load() - c0;
            sinA = sinTurnoT.load() - s0;
        });
        atender(5600);
        std::printf("  medido: (a) la interfaz 5 s en lo suyo tras llamar al motor: %d vueltas del reloj con el turno, %d sin; "
                    "lo pedido, a los %.1f ms\n",
                    conA, sinA, msA.load());
        CHECK(!teniaA, "T(a): al volver del método del motor, la interfaz ya no tiene el turno (por llamada)");
        CHECK(sinA == 0, "T(a): con la interfaz trabada 5 s, el reloj del motor tuvo el turno en cada vuelta");
        CHECK_TIEMPO(conA >= 150, "T(a): con la interfaz trabada 5 s, el reloj del motor tuvo el turno en cada vuelta");
        CHECK(msA.load() >= 0, "T(a): lo pedido durante la traba se hizo en el acto (la agenda no esperó)");
        CHECK_TIEMPO(msA.load() < 300, "T(a): lo pedido durante la traba se hizo en el acto (la agenda no esperó)");
        CHECK(turnoT.totalDirectas() == directasA + 1, "T(a): la entrada directa se contó (una vez)");

        // (b) Dos oyentes de la foto: el primero entra a cfg_ (por la vuelta), el segundo se traba.
        std::atomic<double> msB{-1};
        int conB = 0;
        bool teniaB = true, primero = false;
        vT.escuchar(&quienT, [&](unsigned) {
            turnoT.entrarDirecto(std::source_location::current()); // p. ej. rt.config().guests
            primero = turnoT.loTengo();
        });
        vT.escuchar(&quienT, [&](unsigned) {
            teniaB = turnoT.loTengo();
            pedirYMedir(msB);
            const int c0 = conTurnoT.load();
            trabarInterfaz(2000);
            conB = conTurnoT.load() - c0;
        });
        vT.publicar(std::make_shared<const FotoMotor>(), FotoMotor::Escenas);
        atender(2600);
        std::printf("  medido: (b) el oyente que sigue a uno que entró a cfg_, trabado 2 s: %d vueltas con el turno; lo pedido, a "
                    "los %.1f ms\n",
                    conB, msB.load());
        CHECK(primero && !teniaB, "T(b): el turno que tomó un oyente se suelta antes del siguiente");
        CHECK(msB.load() >= 0, "T(b): mientras el siguiente se traba, el reloj y la agenda siguen con el turno");
        CHECK_TIEMPO(conB >= 60 && msB.load() < 300, "T(b): mientras el siguiente se traba, el reloj y la agenda siguen con el turno");

        // (c) La red de seguridad: un evento con cfg_ tomado 5 s (lo que todavía no pasó a la foto).
        int conC = 0, sinC = 0, picosC = 0;
        std::uint64_t medidasC = 0;
        QTimer::singleShot(0, &quienT, [&]() {
            turnoT.entrarDirecto(std::source_location::current());
            const int c0 = conTurnoT.load(), s0 = sinTurnoT.load(), p0 = picos.load();
            const std::uint64_t m0 = nivT.medidas();
            trabarInterfaz(5000);
            conC = conTurnoT.load() - c0;
            sinC = sinTurnoT.load() - s0;
            picosC = picos.load() - p0;
            medidasC = nivT.medidas() - m0;
        });
        atender(5600);
        const json tenencia = turnoT.estado().value("interfaz", json::object());
        std::printf("  medido: (c) la interfaz con cfg_ 5 s: %d vueltas con el turno, %d sin; %d picos y %llu loudness al canal; "
                    "tenencia %s\n",
                    conC, sinC, picosC, static_cast<unsigned long long>(medidasC), tenencia.dump().c_str());
        CHECK(conC <= 1, "T(c): con cfg_ tomado, lo del turno se saltea (el reloj no espera a la interfaz)");
        CHECK_TIEMPO(sinC >= 150, "T(c): con cfg_ tomado, lo del turno se saltea (el reloj no espera a la interfaz)");
        CHECK_TIEMPO(picosC >= 150 && medidasC >= 50, "T(c): pero el pico del programa y el loudness llegan al canal en cada vuelta");
        CHECK(nivT.programa("principal").first > -20.5f && nivT.loudnessDe({}).value("m", 0.0) == -23.0,
              "T(c): el canal de niveles tiene lo medido sin el turno (la por omisión también como \"\")");
        CHECK(tenencia.value("max_ms", 0.0) >= 4900 && tenencia.value("largas", 0) >= 1 &&
                  tenencia.value("max_lugar", std::string()) == "pruebas::plano::probarFinal",
              "T(c): lo que la interfaz tuvo el turno quedó anotado, con dónde lo tomó");
        CHECK(!turnoT.loTengo(), "T: al terminar cada evento la interfaz soltó el turno");
        TM::participar("interfaz");
        planoT.detener();
    }

    // --- El contenido de una capa DSK, por el id de cada escena (escenas/RefEscena.hpp) ---
    {
        using ba::app::control::CapasDsk;
        const fs::path datosE = fs::temp_directory_path() / "ba-test-plano-control-dsk-ids";
        fs::remove_all(datosE);
        fs::create_directories(datosE / "Config");
        // Un dsk.json de antes: la escena por el nombre, al lado de una fuente.
        std::ofstream(datosE / "Config" / "dsk.json")
            << R"({"capas":[{"capa":1,"contenido":["Zócalo","Logo"],"visible":true}]})";
        ba::app::escenas::TablaEscenas tabla({{"esc-00000001", "Zócalo"}});
        std::vector<std::string> armado;
        CapasDsk::Motor m;
        m.armar = [&](int, const std::vector<std::string>& cont, const json&, const std::string&) {
            armado = cont;
            return true;
        };
        m.mostrar = [](int, bool, int, const std::string&) { return true; };
        m.estado = [](const std::string&) { return json::array(); };
        m.error = []() { return std::string(); };
        m.escenas = [&tabla]() { return tabla; };
        CapasDsk capas(datosE, std::move(m));
        capas.restaurar();
        CHECK(armado == std::vector<std::string>({"esc-00000001", "Logo"}), "DSK: al motor, la escena por el id");
        tabla = ba::app::escenas::TablaEscenas({{"esc-00000001", "Zócalo grande"}});
        capas.seguirEscenas();
        const json c = capas.capas()[0];
        CHECK(c["contenido"] == json::array({"Zócalo grande", "Logo"}) && c["contenido_ids"][0] == "esc-00000001",
              "DSK: renombrada, la capa la sigue (con el nombre nuevo)");
        CHECK(capas.mostrar(1, true, false).empty() && armado.empty() == false, "DSK: sube igual");
        // Otro contenido a mano: los ids salen de los nombres nuevos.
        CHECK(capas.definir(1, {{"contenido", json::array({"Logo", "Zócalo grande"})}}).empty() &&
                  armado == std::vector<std::string>({"Logo", "esc-00000001"}),
              "DSK: otro contenido, por el id de su escena");
        CHECK(ba::persist::cola().vaciar(), "DSK: lo guardado, escrito");
        fs::remove_all(datosE);
    }
}

} // namespace ba::app::pruebas::plano

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
    CHECK_TIEMPO(controlTrabada >= 100, "1: el reloj del control siguió con la interfaz trabada");
    CHECK(dondeMotor.load() == plano.hilo(), "1: el del motor también (es del control, su dueño)");
    CHECK_TIEMPO(motorTrabada >= 100, "1: el del motor también (es del control, su dueño)");
    CHECK(latidoTrabada == 0, "1: el latido de la interfaz no (es de ella: así el supervisor sabe que se trabó)");
    CHECK(rHealth == "ok\n", "2: /healthz contesta");
    CHECK_TIEMPO(msHealth < 500, "2: /healthz contesta");
    const json jf = json::parse(rFoto, nullptr, false);
    CHECK(jf.is_object() && !jf["result"].value("isError", true), "2: la herramienta de la foto contesta en el acto");
    CHECK_TIEMPO(msFoto < 500, "2: la herramienta de la foto contesta en el acto");
    CHECK(dondeFoto.load() != interfaz && dondeFoto.load() != plano.hilo(), "2: corrió en el pool");
    const json ju = json::parse(rUi, nullptr, false);
    CHECK(ju.is_object() && ju["result"].value("isError", false) &&
              ju["result"]["structuredContent"].value("interfaz_ocupada", false) &&
              ju["result"]["structuredContent"].contains("foto"),
          "3: la de la interfaz contesta «ocupada» con la foto");
    CHECK(msUi >= 750, "3: al vencer el tope (800 ms)");
    CHECK_TIEMPO(msUi < 2500, "3: al vencer el tope (800 ms)");
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


    // --- Las partes, en orden: cada una llama a la siguiente con lo suyo vivo ------------
    Banco b{app, interfaz, control, motor, latido, plano, turno, core, pool, token, http, vigia, disparos};
    probarFoto(b, [&b] {
        probarAire(b, [&b] {
            probarSala(b, [&b] {
                probarMezcla(b, [&b] {
                    probarResto(b, [&b] { probarImagen(b, [&b] { probarFinal(b); }); });
                });
            });
        });
    });

    std::printf(g_fallos + g_fallosTiempo == 0 ? "test_plano_control: OK\n" : "test_plano_control: %d fallo(s) y %d de tiempo\n",
                g_fallos, g_fallosTiempo);
    return ba::pruebas::resultadoConTiempos("test_plano_control", g_fallos, g_fallosTiempo);
}
