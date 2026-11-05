// ============================================================================
// src/app/tests/plano_control_imagen.cpp — test_plano_control, I: la imagen
//
// El grafo de video es del dueño del motor: su revisión, su vista en la foto, la alarma «sin
// señal» y una herramienta MCP del grafo con la interfaz trabada. Ver test_plano_control.cpp
// (qué prueba cada letra) y PlanoControlPrueba.hpp (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "agent/EntradasAlarmas.hpp"
#include "emision/VentanillaMotor.hpp"

#include <mutex>
#include <thread>
#include <vector>

namespace ba::app::pruebas::plano {

using namespace ba::app::mcp;

namespace {
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

void probarImagen(Banco& b, const Resto& resto) {
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
        CHECK(grafoI->enLaInterfaz.load() == 0 && grafoI->sinTurno.load() == 0, "I: con la interfaz trabada, el grafo sigue revisando en el control y con el turno");
        CHECK_TIEMPO(revisionesEnLaTraba >= 10, "I: con la interfaz trabada, el grafo sigue revisando en el control y con el turno");
        CHECK(dondeSinSenal.load() == plano.hilo(), "I: la alarma «sin señal» sale en el vigía con la interfaz trabada");
        CHECK(dondeOrden.load() == plano.hilo(), "I: lo que pidió la interfaz se hizo en el control durante la traba");
        const json jm = json::parse(rMcp, nullptr, false);
        CHECK(jm.is_object() && !jm["result"].value("isError", true) && dondeHerramienta.load() == plano.hilo() && herramientaConTurno.load(), "I: la herramienta del grafo corre en el control, con el turno, con la interfaz trabada");
        CHECK_TIEMPO(msMcp < 500, "I: la herramienta del grafo corre en el control, con el turno, con la interfaz trabada");
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


    b.grafoI = grafoI.get();
    b.vigiaI = vigiaI.get();
    resto();
}

} // namespace ba::app::pruebas::plano
