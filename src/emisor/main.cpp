// ============================================================================
// src/emisor/main.cpp — `ba-emisor`: la salida de una salida, en otro proceso (F3-01)
//
//   ba-emisor --salida=<id>            (de fábrica, «principal»)
//             [--runtime=<carpeta>]    (de fábrica, <datos>/Runtime)
//             [--canal-video=<nombre>] [--canal-audio=<nombre>]   (las pruebas)
//             [--sin-salud]            (sin BA.Health: suelto, para probar)
//
// Lo lanza el launcher como un hijo más (Supervisor: B, los emisores, A) cuando
// `emisor.aparte` está prendido en launcher.json. Late en su slot de BA.Health
// (slotSaludEmisor: el launcher lo relanza si se cae o se cuelga), atiende el cierre
// ordenado en control.emisor.<id> (y SIGTERM), y en cada vuelta (100 ms):
//
//   - mira el pedido (Runtime/emisor/<id>.json, lo escribe la app; Pedido.hpp) y,
//     si cambió, lo aplica (Emisor::aplicar: lo que no cambió sigue como está);
//   - atiende la entrada y las salidas (Emisor::tic);
//   - cada segundo deja su estado en Runtime/emisor/<id>.estado.json (lo lee la app
//     para mostrar la transmisión y la grabación como si fueran suyas).
//
// Arranca con lo que diga el pedido: tras una caída suya o un corte de luz sigue
// transmitiendo y grabando sin esperar a la app (una grabación retomada va a otro
// archivo: nombreLibre). Al cerrarse en orden para la transmisión, cierra los
// archivos y deja en su estado `cerrado` con el `seq` del pedido que tenía: al volver
// a abrir, ESE pedido no se retoma (se espera uno nuevo de la app). No toca el
// pedido (es de la app).
// ============================================================================

#include "Emisor.hpp"
#include "Pedido.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

using ba::i18n::llenar;
using ba::i18n::traducir;

std::atomic<bool> g_parar{false};

#ifdef _WIN32
BOOL WINAPI alCtrl(DWORD tipo) {
    if (tipo == CTRL_C_EVENT || tipo == CTRL_BREAK_EVENT || tipo == CTRL_SHUTDOWN_EVENT || tipo == CTRL_CLOSE_EVENT)
        g_parar.store(true);
    return TRUE;
}
#else
extern "C" void alSenal(int) { g_parar.store(true); }
#endif

std::uint32_t pidPropio() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

std::string valorDe(const std::string& arg, const char* prefijo) {
    const std::string p(prefijo);
    return arg.rfind(p, 0) == 0 ? arg.substr(p.size()) : std::string();
}

/// La fecha de modificación del pedido (para no releerlo en cada vuelta).
std::filesystem::file_time_type cuandoCambio(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

int correr(const std::vector<std::string>& args) {
    std::string id = ba::ipc::kIdSalidaPorOmision, runtimeArg;
    ba::emisor::Emisor::Config ec;
    bool conSalud = true;
    for (const std::string& a : args) {
        if (std::string v = valorDe(a, "--salida="); !v.empty()) id = v;
        else if (std::string v = valorDe(a, "--runtime="); !v.empty()) runtimeArg = v;
        else if (std::string v = valorDe(a, "--canal-video="); !v.empty()) ec.canalVideo = v;
        else if (std::string v = valorDe(a, "--canal-audio="); !v.empty()) ec.canalAudio = v;
        else if (a == "--sin-salud") conSalud = false;
    }
    if (!ba::ipc::idDeSalidaValido(id)) {
        std::fprintf(stderr, "ba-emisor: %s\n", llenar(traducir("emisor", "id de salida inválido: «%1»"), {id}).c_str());
        return 2;
    }
    ec.id = id;
    const std::filesystem::path datos = ba::platform::dataRoot();
    ba::i18n::usar(ba::i18n::idiomaDeLaMaquina(datos));
    const std::filesystem::path runtime =
        runtimeArg.empty() ? datos / "Runtime" : std::filesystem::path(std::u8string(runtimeArg.begin(), runtimeArg.end()));
    // Los ajustes de la máquina (launcher.json → "ajustes"): emisor.*, stream.*, record.*.
    {
        std::ifstream in(ba::platform::launcherJsonFor(datos));
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false, true);
        if (in && j.is_object()) ba::ajustes::cargar(j.value("ajustes", nlohmann::json::object()));
    }

    // BA.Health: el slot que le toca a esta salida (el launcher lo pasa en BA_SLOT_SALUD).
    ba::ipc::HealthBlock salud;
    std::uint32_t slot = ba::ipc::slotSaludEmisor(0);
    if (const char* s = std::getenv("BA_SLOT_SALUD"); s != nullptr && *s != '\0') slot = static_cast<std::uint32_t>(std::atoi(s));
    if (conSalud) {
        auto st = ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, salud);
        if (!st.ok) st = ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, ba::ipc::kSlotsSalud, salud);
        if (!st.ok) {
            std::fprintf(stderr, "ba-emisor: %s\n", traducir("emisor", "no se pudo abrir BA.Health").c_str());
            conSalud = false;
        } else {
            salud.setIdentity(slot, pidPropio(), ba::ipc::kVersionProceso);
            salud.beat(slot);
        }
    }

    std::string err;
    // Late mientras GStreamer arranca: la primera vez en una PC rehace su lista de
    // plugins (puede tardar minutos) y el launcher lo daría por colgado.
    if (!ba::motor::iniciarGStreamer(&err, [&]() {
            if (conSalud) salud.beat(slot);
        })) {
        std::fprintf(stderr, "ba-emisor: %s\n", err.c_str());
        return 3;
    }
    // Sin GPU: lo que llega del canal ya está en memoria del sistema, y el codificador
    // de la placa lo toma de ahí (un contexto de GL o D3D12 más no ayuda en nada acá).
    ba::motor::ponerGpuVideo(false);
    ba::motor::Reloj reloj;
    if (!reloj.valido()) {
        std::fprintf(stderr, "ba-emisor: %s\n", reloj.error().c_str());
        return 3;
    }

    ba::ipc::ControlService control;
    if (!control.start(ba::ipc::canalControlEmisor(id), &err))
        ba::motor::registrar(ba::motor::Nivel::Aviso, llenar(traducir("emisor", "sin canal de control (%1): queda la señal"), {err}));

    ba::emisor::Emisor emisor(reloj, ec);
    const std::filesystem::path rutaPedido = ba::emisor::rutaPedido(runtime, id);
    const std::filesystem::path rutaEstado = ba::emisor::rutaEstado(runtime, id);
    ba::motor::registrar(ba::motor::Nivel::Info,
                         llenar(traducir("emisor", "emisor de «%1»: pedido en %2"), {id, rutaPedido.string()}));
    std::filesystem::file_time_type visto{};
    std::uint64_t seq = 0;
    bool primero = true;
    // Si la vez anterior se cerró en orden, el pedido de entonces no se retoma.
    std::optional<std::uint64_t> seqCerrado;
    {
        std::string txt;
        const nlohmann::json e = ba::persist::leerArchivo(rutaEstado, txt) ? nlohmann::json::parse(txt, nullptr, false) : nlohmann::json();
        if (e.is_object() && e.value("cerrado", false) && e.contains("seq_visto") && e["seq_visto"].is_number_unsigned())
            seqCerrado = e["seq_visto"].get<std::uint64_t>();
    }
    // EL LATIDO va en su hilo: parar una grabación espera que se cierre el archivo
    // (hasta 5 s, y la copia otros 5) y el launcher lo daría por colgado. Late mientras
    // el bucle dé vueltas; si el bucle se traba más de `emisor.colgadoS` (15 s), deja de
    // latir y el launcher hace lo de siempre (volcado y relanzamiento).
    std::atomic<std::int64_t> ultimaVuelta{0};
    const auto msAhora = []() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    ultimaVuelta = msAhora();
    std::atomic<bool> finLatido{false};
    std::thread latido;
    if (conSalud)
        latido = std::thread([&]() {
            const std::int64_t colgadoMs = static_cast<std::int64_t>(ba::ajustes::entero("emisor.colgadoS", 15)) * 1000;
            while (!finLatido.load()) {
                if (msAhora() - ultimaVuelta.load() < colgadoMs) salud.beat(slot);
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        });
    auto proximoEstado = std::chrono::steady_clock::now();
    while (!g_parar.load()) {
        const auto ahora = std::chrono::steady_clock::now();
        ultimaVuelta = msAhora();
        control.poll([](const ba::ipc::Message& m) {
            if (m.type == ba::ipc::Type::Shutdown) g_parar.store(true);
        });
        // El pedido: solo si cambió el archivo (y entonces, solo si cambió `seq`).
        if (const auto t = cuandoCambio(rutaPedido); primero || t != visto) {
            ba::emisor::Pedido p;
            if (ba::emisor::leer(rutaPedido, p) && (primero || p.seq != seq)) {
                visto = t;
                seq = p.seq;
                primero = false;
                if (seqCerrado && *seqCerrado == p.seq) {
                    ba::motor::registrar(ba::motor::Nivel::Info,
                                         llenar(traducir("emisor", "pedido %1: se cerró en orden con él; espera uno nuevo"),
                                                {std::to_string(p.seq)}));
                } else {
                    ba::motor::registrar(ba::motor::Nivel::Info,
                                         llenar(traducir("emisor", "pedido %1: transmisión %2, grabación %3"),
                                                {std::to_string(p.seq), traducir("emisor", p.transmision ? "sí" : "no"),
                                                 traducir("emisor", p.grabacion ? "sí" : "no")}));
                    emisor.aplicar(p);
                }
            } else if (!primero) {
                visto = t;
            }
        }
        emisor.tic();
        if (ahora >= proximoEstado) {
            nlohmann::json e = emisor.estado();
            e["pid"] = pidPropio();
            e["seq_visto"] = seq;
            e["hora_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            ba::persist::escribir(rutaEstado, e.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
            proximoEstado = ahora + std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ba::motor::registrar(ba::motor::Nivel::Info, traducir("emisor", "cierre ordenado: se cierran los archivos"));
    ultimaVuelta = msAhora();
    emisor.parar();
    finLatido = true;
    if (latido.joinable()) latido.join();
    if (conSalud) salud.setState(slot, ba::ipc::ProcessState::Terminated);
    nlohmann::json e = emisor.estado();
    e["pid"] = pidPropio();
    e["seq_visto"] = seq;
    e["cerrado"] = true;
    ba::persist::escribir(rutaEstado, e.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
    control.stop();
    return 0;
}

} // namespace

#ifdef _WIN32
int main(int argc, char** argv) {
    SetConsoleCtrlHandler(alCtrl, TRUE);
#else
int main(int argc, char** argv) {
    std::signal(SIGINT, alSenal);
    std::signal(SIGTERM, alSenal);
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return correr(std::vector<std::string>(argv + 1, argv + argc));
}
