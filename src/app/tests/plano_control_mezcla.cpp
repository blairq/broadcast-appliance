// ============================================================================
// src/app/tests/plano_control_mezcla.cpp — test_plano_control, M: la mezcla
//
// La parte Mezcla de la foto, el canal de niveles y las alarmas del audio con la interfaz
// trabada. Ver test_plano_control.cpp (qué prueba cada letra) y PlanoControlPrueba.hpp
// (cómo se parte).
// ============================================================================
#include "tests/PlanoControlPrueba.hpp"

#include "agent/EntradasAlarmas.hpp"
#include "emision/NivelesMotor.hpp"
#include "NivelAudio.hpp"
#include "emision/VentanillaMotor.hpp"

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

namespace ba::app::pruebas::plano {

void probarMezcla(Banco& b, const Resto& resto) {
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
    

    b.vigiaM = &vigiaM;
    resto();
}

} // namespace ba::app::pruebas::plano
