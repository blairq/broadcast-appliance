// ============================================================================
// src/app/tests/plano_control_aire.cpp — test_plano_control, 8 y 10: el aire y la ventanilla
//
// Las herramientas de aire en el control con el turno del motor (McpMando), y la ventanilla
// del motor (VentanillaMotor) con la interfaz trabada. Ver test_plano_control.cpp (qué
// prueba cada número) y PlanoControlPrueba.hpp (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "emision/DocumentoAire.hpp"
#include "emision/VentanillaMotor.hpp"
#include "mcp/McpMando.hpp"
#include "persist/ColaEscritura.hpp"

#include <QAbstractEventDispatcher>

#include <mutex>
#include <thread>
#include <vector>

namespace ba::app::pruebas::plano {

using namespace ba::app::mcp;

void probarAire(Banco& b, const Resto& resto) {
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
    // Lo que pasó llega después, en el hilo de la herramienta (como AppRuntime::alResultado).
    mando.transmitir = [&](const std::string&, const std::string&, const std::string&, bool, MandoDeAire::Hecho hecho) {
        MandoDeAire::Inicio r;
        operar([&]() {
            r.yaIgual = aire.transmite;
            aire.transmite = true;
        });
        r.ok = true;
        QTimer::singleShot(0, [hecho]() { hecho({true, {}, {}}); });
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
    mando.grabar = [&](const std::string&, MandoDeAire::Hecho hecho) {
        operar([&]() { aire.graba = true; });
        QTimer::singleShot(0, [hecho]() { hecho({true, {}, "programa-0001.mkv"}); });
        return std::string();
    };
    mando.pararGrabacion = [&](const std::string&) { operar([&]() { aire.graba = false; }); };
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
    bool todasHechas = true, todasEnElActo8 = true, todasSeVen = true;
    double peorAire = 0;
    for (const auto& p : pasos) {
        const json r = json::parse(p.resultado, nullptr, false);
        const bool bien = r.is_object() && r.contains("result") && !r["result"].value("isError", true);
        if (!bien || !p.seVio) std::printf("  %s: %s\n", p.herramienta.c_str(), p.resultado.substr(0, 300).c_str());
        todasHechas = todasHechas && bien;
        todasEnElActo8 = todasEnElActo8 && p.ms < 600;
        todasSeVen = todasSeVen && p.seVio;
        peorAire = std::max(peorAire, p.ms);
    }
    std::printf("  medido: %zu de aire con la interfaz trabada, la peor %.1f ms; %d operaciones; el control dio %d vueltas\n",
                pasos.size(), peorAire, operaciones.load(), control.load() - vueltasAntes);
    CHECK(todasHechas, "8: con la interfaz trabada, TAKE, FTB, transmitir y grabar se hacen");
    CHECK_TIEMPO(todasEnElActo8, "8: y en el acto (< 600 ms)");
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
        CHECK(ms >= 750, "8: al vencer el tope");
        CHECK_TIEMPO(ms < 1400, "8: al vencer el tope");
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
        CHECK(hechasEnLaTraba == 6, "10: con la interfaz trabada, lo pedido se hace en el acto");
        CHECK_TIEMPO(peorOrden < 200, "10: con la interfaz trabada, lo pedido se hace en el acto");
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
        CHECK(esperada.load() >= 550, "10: la orden esperó a que la interfaz soltara el turno, y se hizo");
        CHECK_TIEMPO(esperada.load() < 900, "10: la orden esperó a que la interfaz soltara el turno, y se hizo");
        CHECK(reintentos > 0, "10: mientras tanto el control siguió (la agenda reintenta, no espera)");
        CHECK_TIEMPO(controlEnElEvento.load() >= 15, "10: mientras tanto el control siguió (la agenda reintenta, no espera)");
        CHECK(turno.totalDirectas() == directasAntes + 1 && turno.directas()["lugares"].contains("pruebas::plano::probarAire"),
              "10: la entrada directa quedó contada, con su lugar");
        CHECK(!turno.loTengo(), "10: al terminar el evento la interfaz soltó el turno");
        TurnoMotor::participar("interfaz");
    }


    resto();
}

} // namespace ba::app::pruebas::plano
