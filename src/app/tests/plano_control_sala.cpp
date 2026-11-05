// ============================================================================
// src/app/tests/plano_control_sala.cpp — test_plano_control, S y C: la sala y el DSK
//
// Asamblea, sala y línea de tiempo con la interfaz trabada; control y DSK (las capas son del
// dueño del motor). Ver test_plano_control.cpp (qué prueba cada letra) y
// PlanoControlPrueba.hpp (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "control/Dsk.hpp"
#include "emision/VentanillaMotor.hpp"
#include "evento/EventoStore.hpp"
#include "mcp/McpFoto.hpp"
#include "persist/ArchivoConfig.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/CapturaError.hpp"

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using ::ba::app::errorDe;

namespace ba::app::pruebas::plano {

using namespace ba::app::mcp;

void probarSala(Banco& b, const Resto& resto) {
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
        CHECK(s1.value("paso", 0) == 1 && s1.value("interfaz", json::object()).value("edad_ms", -1) >= 0, "S: recién registrada y con la interfaz trabada, la copia sembrada contesta en el acto");
        CHECK_TIEMPO(ms1 < 600, "S: recién registrada y con la interfaz trabada, la copia sembrada contesta en el acto");
        CHECK(s2.value("paso", 0) == 2, "S: la línea deja la copia al día cuando cambia (sin que nadie pregunte)");
        CHECK_TIEMPO(ms2 < 600, "S: la línea deja la copia al día cuando cambia (sin que nadie pregunte)");
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
        CHECK(enCurso && enElPool == raizS / carpeta && deOtro.empty() && msPool >= 0, "S: con la interfaz trabada, el pool encuentra el evento en curso con la raíz de los eventos");
        CHECK_TIEMPO(msPool < 300, "S: con la interfaz trabada, el pool encuentra el evento en curso con la raíz de los eventos");
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
        CHECK(lat.size() == 3 && enLaInterfaz.load() == 0 && sinTurno.load() == 0, "S: con la interfaz trabada, la atención, el micrófono y el corte de la línea se hacen en el control");
        CHECK_TIEMPO(peor < 200, "S: con la interfaz trabada, la atención, el micrófono y el corte de la línea se hacen en el control");
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
        CHECK(errorDe([&] { dsk.mostrarUi(1, true, false); }).isEmpty(), "C: subir se pide");
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
        CHECK(jC.is_object() && !jC["result"].value("isError", true) && hechoEnLaTraba, "C: con la interfaz trabada, lo pedido (la interfaz y la herramienta) se hace en el acto");
        CHECK_TIEMPO(msC < 600, "C: con la interfaz trabada, lo pedido (la interfaz y la herramienta) se hace en el acto");
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


    resto();
}

} // namespace ba::app::pruebas::plano
