// ============================================================================
// src/app/arranque/SaludProceso.cpp — ver SaludProceso.hpp
// ============================================================================

#include "arranque/SaludProceso.hpp"

#include <chrono>
#include <cstdio>

#include "arranque/AppRuntime.hpp"
#include "diag/Correlacion.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Hilo.hpp"
#include "platform/LocalSocket.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::app {

namespace {

// El slot y la versión, los de ba_core: el launcher readopta solo a una A de su misma versión.
constexpr std::size_t kHealthSlot = ba::ipc::kSlotSaludApp;
constexpr std::uint32_t kProcessVersion = ba::ipc::kVersionProceso;

/// El pedido de reinicio a B (bloquea hasta ~4 s). Sin tocar el estado de AppRuntime: lo
/// usa el hilo de reiniciarSalaEnElFondo.
bool pedirReinicioGateway(std::string& error) {
    // B relee gateway.json al volver: lo guardado tiene que estar en el disco.
    ba::persist::cola().vaciar();
    // Cierre ordenado de B por su canal de control: el launcher lo relanza y
    // B relee gateway.json. Sin launcher, B simplemente se cierra.
    ba::platform::LocalStream s;
    std::string err;
    if (!ba::platform::LocalStream::connect("control.gateway", 1000, s, &err)) {
        error = ba::i18n::llenar(ba::i18n::traducir("arranque", "sin canal con el gateway: %1"), {err});
        return false;
    }
    ba::ipc::ControlEndpoint ep(s, ba::ipc::ControlEndpoint::kLauncher);
    ba::ipc::Message m;
    m.type = ba::ipc::Type::Shutdown;
    m.payload = "{}";
    m.correlacion = ba::diag::correlacion::actual(); // la orden que lo pidió, si hay (F4-02)
    if (!ep.sendRequest(m)) {
        error = ba::i18n::traducir("arranque", "no se pudo mandar el pedido al gateway");
        return false;
    }
    // La conexión queda abierta hasta que el gateway la cierre al salir: B mira
    // su canal cada 250 ms y, en Windows, un pipe cuyo cliente ya se fue pierde
    // lo que tenía sin leer. Antes se cerraba enseguida y el pedido no llegaba
    // nunca: decía «reiniciándose» y el gateway seguía igual.
    ba::ipc::Message r;
    for (int i = 0; i < 60 && ep.connected(); ++i) {
        ep.poll(r);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (ep.connected()) {
        error = ba::i18n::traducir("arranque", "el gateway no contestó al pedido de reinicio");
        return false;
    }
    return true;
}

} // namespace

SaludProceso::~SaludProceso() {
    pararLatidoArranque();
    esperarReinicio();
}

std::uint32_t SaludProceso::pid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

void SaludProceso::abrir() {
    // A late en su slot para el watchdog del launcher. Sin launcher (desarrollo) se crea el
    // bloque para poder correr solo.
    if (!ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_).ok) {
        ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, ba::ipc::kSlotsSalud, health_);
    }
    if (health_.valid()) {
        health_.setIdentity(kHealthSlot, pid(), kProcessVersion);
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Running);
        health_.beat(kHealthSlot);
    }
}

void SaludProceso::latir() {
    if (!health_.valid()) return;
    std::lock_guard<std::mutex> l(healthMu_);
    health_.beat(kHealthSlot);
}

void SaludProceso::terminado() {
    if (health_.valid()) health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
}

void SaludProceso::empezarLatidoArranque() {
    // Arrancar no es estar colgado. Con GStreamer ya iniciado, el motor igual tarda unos
    // segundos en armarse; sin latir, el launcher lo daba por colgado a los 20 s y lo mataba
    // en pleno arranque (pasó). Late desde su hilo hasta que el latido de los cuadros toma la
    // posta, con un tope de 2 minutos: un arranque colgado de verdad se sigue viendo.
    if (!health_.valid() || latidoArranque_.joinable()) return;
    latidoArranqueSigue_ = true;
    latidoArranque_ = std::thread([this]() {
        ba::platform::nombrarHilo("latido-arranque");
        for (int i = 0; i < 240 && latidoArranqueSigue_.load(); ++i) {
            latir();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
}

void SaludProceso::pararLatidoArranque() {
    latidoArranqueSigue_ = false;
    if (latidoArranque_.joinable()) latidoArranque_.join();
}

void SaludProceso::iniciarLatidoAire(const std::filesystem::path& root, std::function<std::uint64_t()> cuadros) {
    if (!health_.valid()) return;
    latido_.iniciar(root, [this] { latir(); }, std::move(cuadros));
}

void SaludProceso::latirInterfaz(bool arrancado, bool hayAire) {
    // El latido de la INTERFAZ: este reloj corre en su hilo, así que si la interfaz se
    // traba el supervisor lo sabe (LatidoAire decide si hay que esperar o relanzar). Si hay
    // aire lo dice la última vuelta del motor: la interfaz no lo toca.
    latir();
    if (arrancado) latido_.uiTick(hayAire);
}

void SaludProceso::abrirCanal() {
    std::string err;
    if (!control_.start("control.app", &err)) {
        std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "sin canal de control (%1)"), {err}).c_str());
    }
}

void SaludProceso::atenderCanal() {
    control_.poll([&](const ba::ipc::Message& m) {
        if (m.type == ba::ipc::Type::Shutdown) {
            // Con la orden del launcher abierta (F4-02): la línea lleva su id.
            const ba::diag::correlacion::Ambito orden(m.correlacion);
            ba::diag::registro().info(ba::i18n::traducir("arranque", "cierre pedido por el launcher"));
            AppRuntime::requestStop();
        }
    });
}

bool SaludProceso::gatewayAlive() const {
    return health_.valid() && health_.read(0).pid != 0 && !health_.isStale(0);
}

std::uint32_t SaludProceso::gatewayPid() const { return health_.valid() ? health_.read(0).pid : 0; }

void SaludProceso::reiniciarSalaEnElFondo(std::function<void(const std::string& error)> listo) {
    if (reiniciandoSala_.exchange(true)) {
        if (listo) rt_.despuesEnDueno([listo, e = ba::i18n::traducir("arranque", "ya se está reiniciando la conferencia")]() { listo(e); });
        return;
    }
    if (reinicioSala_.joinable()) reinicioSala_.join(); // el anterior ya terminó
    reinicioSala_ = std::thread([this, listo = std::move(listo)]() {
        ba::platform::nombrarHilo("sala-reinicio");
        std::string e;
        pedirReinicioGateway(e);
        reiniciandoSala_ = false;
        if (listo) rt_.despuesEnDueno([listo, e]() { listo(e); });
    });
}

void SaludProceso::esperarReinicio() {
    if (reinicioSala_.joinable()) reinicioSala_.join();
}

} // namespace ba::app
