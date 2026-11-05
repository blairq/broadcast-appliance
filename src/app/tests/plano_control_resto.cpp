// ============================================================================
// src/app/tests/plano_control_resto.cpp — test_plano_control, R, E y R2
//
// La configuración y el equipo (en la foto; aplicar, en el control), la carpeta del evento y
// la retoma de las grabaciones, la energía, la agenda al parar y la ventanilla que se va. Ver
// test_plano_control.cpp (qué prueba cada letra) y PlanoControlPrueba.hpp (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "arranque/AppSettings.hpp"
#include "arranque/EnergiaAire.hpp"
#include "emision/VentanillaMotor.hpp"

#include <mutex>
#include <thread>
#include <vector>

namespace ba::app::pruebas::plano {

void probarResto(Banco& b, const Resto& resto) {
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
                return std::string("Configuración guardada.");
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
        CHECK(aplicadaMs.load() >= 0 && ordenesEnLaInterfaz.load() == 0 && fotosSinTurno.load() == 0, "R: con la interfaz trabada, aplicar se hace en el control (con el turno)");
        CHECK_TIEMPO(aplicadaMs.load() < 200, "R: con la interfaz trabada, aplicar se hace en el control (con el turno)");
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
                  std::count(llegadas.begin(), llegadas.end(), std::string("respuesta Configuración guardada.")) == 1 &&
                  std::count(llegadas.begin(), llegadas.end(), std::string("respuesta microfono")) == 1,
              "R: el resumen vuelve después de la foto (el formulario recarga con lo nuevo)");
    }

    // --- E. La carpeta del evento y la retoma de las grabaciones, del dueño ----------------
    // Como AppRuntime (setCarpetaDelEvento, retomarGrabaciones, ajustesAire): la carpeta es
    // del dueño; desde otro hilo, ajustesAire() devuelve la COPIA que armó el dueño. Tras un
    // corte, la grabación que espera la carpeta se retoma al ponerla. Puesta desde la
    // interfaz y retomada ahí, la copia todavía no tenía la carpeta: la grabación iba a
    // Recordings/ (la regresión del banco de caos). Ahora las dos se le encargan al dueño.
    {
        using ba::app::VentanillaMotor;
        struct Aire {
            std::function<std::string()> carpeta; ///< carpetaDelEvento_
            std::string copia;                    ///< ajustesAire_.carpetaEvento (la del plano)
            std::mutex mu;
            bool porRetomar = true; ///< grabacionesPorRetomar_ (un corte con la grabación andando)
            std::vector<std::string> grabaciones; ///< dónde arrancó cada una
            std::atomic<int> enLaInterfaz{0};
        } a;
        const QThread* dueno = plano.hilo();
        const auto esDueno = [&]() { return QThread::currentThread() == dueno; };
        const auto encolar = [&](std::function<void()> fn) { plano.encolarConTurno(std::move(fn)); };
        // ajustesAire(): el dueño arma la copia; los demás leen la última.
        const auto ajustesAire = [&]() {
            std::lock_guard<std::mutex> l(a.mu);
            if (esDueno()) a.copia = a.carpeta ? a.carpeta() : std::string();
            return a.copia;
        };
        std::function<void()> retomar;
        std::function<void(std::function<std::string()>)> ponerCarpeta;
        retomar = [&]() {
            if (!esDueno()) return encolar([&]() { retomar(); });
            if (QThread::currentThread() == interfaz) ++a.enLaInterfaz;
            if (!a.porRetomar) return;
            a.porRetomar = false;
            const std::string c = ajustesAire(); // startRecording → recordingPath
            a.grabaciones.push_back(c.empty() ? "Recordings" : c + "/Grabaciones");
        };
        ponerCarpeta = [&](std::function<std::string()> fn) {
            if (!esDueno()) return encolar([&, fn]() { ponerCarpeta(fn); });
            a.carpeta = std::move(fn);
            ajustesAire();
            if (a.carpeta) retomar();
        };
        // El dueño armó la copia al arrancar, sin evento todavía (restoreOutputs).
        std::atomic<bool> armada{false};
        encolar([&]() {
            ajustesAire();
            armada = true;
        });
        atender(100);
        CHECK(armada.load() && ajustesAire().empty(), "E: la copia del plano arrancó sin la carpeta del evento");
        // La interfaz arma el evento (StudioViewModel) y se traba enseguida.
        VentanillaMotor vE({.encolar = encolar, .alTerminarOrden = []() {}, .adoptar = [&plano](QObject* o) { plano.adoptar(o); }});
        const auto desde = std::chrono::steady_clock::now();
        std::atomic<double> retomadaMs{-1};
        vE.pedir("carpeta_evento", [&]() {
            ponerCarpeta([]() { return std::string("Eventos/ZZ caos"); });
            retomadaMs = msDesde(desde);
        });
        // Y algo de la interfaz que todavía llama directo (como la consola o un test viejo).
        ponerCarpeta([]() { return std::string("Eventos/ZZ caos"); });
        trabarInterfaz(500);
        std::printf("  medido: la grabación retomada en el dueño a los %.1f ms de poner la carpeta (interfaz trabada)\n",
                    retomadaMs.load());
        CHECK(a.grabaciones.size() == 1 && a.grabaciones.front() == "Eventos/ZZ caos/Grabaciones",
              "E: la grabación retomada va a la carpeta del evento, no a Recordings/ (una sola vez)");
        CHECK(retomadaMs.load() >= 0 && a.enLaInterfaz.load() == 0, "E: se retoma en el dueño, con la interfaz trabada");
        CHECK_TIEMPO(retomadaMs.load() < 300, "E: se retoma en el dueño, con la interfaz trabada");
        atender(100);
        CHECK(ajustesAire() == "Eventos/ZZ caos", "E: la copia del plano ya tiene la carpeta");
    }

    // --- R2. La energía, la agenda al parar y la ventanilla que se va ----------------------
    // (a) EnergiaAire es del dueño (adoptada con su reloj): con la interfaz trabada, a poca
    //     batería las grabaciones se cierran EN EL CONTROL antes de que se apague la UPS.
    // (b) Lo que quedó en la agenda al parar el plano NO se pierde: corre en el hilo que
    //     recibe el motor, después de que se hace dueño, en orden; lo que se encola con el
    //     plano parado también corre ahí (encolado).
    // (c) La ventanilla que se va antes de que corra su orden: la orden corre sin tocarla y
    //     la respuesta no llega (sin `this` crudo).
    {
        using ba::app::EnergiaAire;
        using ba::app::VentanillaMotor;
        ba::app::TurnoMotor turno2;
        PlanoControl plano2({.control = []() {}, .motor = []() {}, .interfaz = []() {}});
        plano2.setTurno({[&turno2](int ms) { return turno2.tomarHasta(ms); }, [&turno2]() { turno2.soltar(); }});
        plano2.arrancar(20, 20);

        // (a)
        std::atomic<int> poca{0};
        std::atomic<const QThread*> dondeCerro{nullptr};
        std::atomic<bool> emergencia{false};
        EnergiaAire::Acciones acc;
        acc.cerrarGrabaciones = [&]() {
            dondeCerro = QThread::currentThread();
            emergencia = true;
            return std::vector<std::string>{"principal"};
        };
        acc.retomarGrabaciones = [&]() { emergencia = false; };
        acc.emergencia = [&]() { return emergencia.load(); };
        acc.leer = [&]() {
            ba::platform::EstadoEnergia e;
            e.conocido = e.hayBateria = true;
            e.enBateria = poca.load() != 0;
            e.porcentaje = poca.load() != 0 ? 5 : 100;
            e.minutos = poca.load() != 0 ? 2 : -1;
            e.fuente = "ups";
            return e;
        };
        auto energia = std::make_unique<EnergiaAire>(acc);
        plano2.adoptar(energia.get(), [e = energia.get()]() { e->vigilar(30); });
        atender(100);
        poca = 1; // se corta la luz y la UPS se va quedando
        trabarInterfaz(400);
        CHECK(dondeCerro.load() == plano2.hilo() && emergencia.load(),
              "R2: con la interfaz trabada, la energía cierra las grabaciones en el control");
        CHECK(energia->estado().value("emergencia", false) && energia->estado().value("en_bateria", false),
              "R2: su estado se lee desde la interfaz (con candado)");

        // (b) La interfaz tiene el turno: lo encolado espera en la agenda.
        turno2.tomar();
        std::vector<std::string> orden;
        std::atomic<const QThread*> dondeQuedo{nullptr};
        for (const char* n : {"uno", "dos", "tres"})
            plano2.encolarConTurno([&orden, &dondeQuedo, n]() {
                orden.push_back(n);
                dondeQuedo = QThread::currentThread();
            });
        // (c) Una ventanilla que se va con su orden en la agenda.
        std::atomic<int> ordenesC{0}, respuestasC{0};
        QObject quien;
        {
            auto vC = std::make_unique<VentanillaMotor>(VentanillaMotor::Dueno{
                .encolar = [&plano2](std::function<void()> fn) { plano2.encolarConTurno(std::move(fn)); },
                .alTerminarOrden = []() {},
                .adoptar = {}});
            vC->pedir(&quien, "se_va", [&ordenesC]() { return ++ordenesC; }, [&respuestasC](int) { ++respuestasC; });
        } // la ventanilla se va acá, con su orden pendiente
        atender(60); // las órdenes llegan a la agenda y esperan el turno
        CHECK(orden.empty(), "R2: con el turno en la interfaz, la agenda espera");
        bool recibio = false;
        bool duenoAntes = false;
        plano2.detener([&]() {
            recibio = true;
            duenoAntes = orden.empty();
        });
        turno2.soltar();
        CHECK(recibio && duenoAntes, "R2: al parar, el que recibe el motor se hace dueño antes de correr lo pendiente");
        CHECK((orden == std::vector<std::string>{"uno", "dos", "tres"}) && dondeQuedo.load() == interfaz,
              "R2: lo que quedó en la agenda corre en el hilo que recibe el motor, en orden (no se pierde)");
        CHECK(ordenesC.load() == 1, "R2: la orden de la ventanilla que se fue corre igual (sin tocarla)");
        std::atomic<const QThread*> dondeDespues{nullptr};
        plano2.encolarConTurno([&dondeDespues]() { dondeDespues = QThread::currentThread(); });
        CHECK(dondeDespues.load() == nullptr, "R2: con el plano parado, encolar no corre en el acto");
        atender(50);
        CHECK(dondeDespues.load() == interfaz, "R2: lo encolado con el plano parado corre en el dueño nuevo");
        // El cierre (después de app.exec(), sin bucle): lo que piden los destructores de las
        // vistas, también un `despues`, corre al atender lo pendiente, en orden; antes se
        // encolaba en un bucle que ya no corría y se perdía.
        std::vector<int> alCerrar;
        plano2.encolarConTurno([&alCerrar]() { alCerrar.push_back(1); });
        plano2.despues(60000, [&alCerrar]() { alCerrar.push_back(2); });
        plano2.encolar([&alCerrar]() { alCerrar.push_back(3); });
        CHECK(alCerrar.empty(), "R2: parado, lo encolado espera al dueño nuevo");
        CHECK(plano2.atenderPendientes() == 3 && (alCerrar == std::vector<int>{1, 2, 3}),
              "R2: sin bucle, lo pendiente corre al atenderlo (también el `despues`), en orden");
        // Parar otra vez no vuelve a recibir el motor (se hacía dueño dos veces al cerrar).
        int recibe = 0;
        plano2.detener([&recibe]() { ++recibe; });
        CHECK(recibe == 0, "R2: detener otra vez no vuelve a correr alRecibir");
        CHECK(respuestasC.load() == 0, "R2: la respuesta a una ventanilla que se fue no llega");
        CHECK(energia->thread() == interfaz, "R2: la energía vuelve a la interfaz al parar");
    }


    resto();
}

} // namespace ba::app::pruebas::plano
