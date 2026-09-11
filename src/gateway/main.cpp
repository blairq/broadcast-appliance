// ============================================================================
// src/gateway/main.cpp — `ba-gateway` (Proceso B) — Gateway WebRTC real
//
// Proposito:
//   Reemplaza al fake-gateway de la Fase 1 por el gateway real (spec §3.3, §8).
//   Punto de entrada del Proceso B: orquesta los componentes y ejecuta el
//   bucle de vida. Componentes:
//     - TokenAuth        : auth de invitado (ba_core::security::TokenService).
//     - WebrtcHub        : una PeerConnection libwebrtc por invitado.
//     - MediaPump        : enruta IPC (ring de retorno + BA.Program) → tracks
//                          WebRTC, y el uplink del invitado → BA.Guest.<n>.
//     - SignalingServer  : HTTP + WebSocket en 127.0.0.1 (señalización + app web).
//     - CloudflaredSupervisor : tunel cloudflared (autostart + recovery).
//     - HealthBlock      : publica su salud en BA.Health slot 0 (B).
//
// Modelo de threading:
//   - Thread PRINCIPAL = hilo de control: instala el handler de senal, abre el
//     HealthBlock, crea los componentes, arranca los threads (senalizacion,
//     pump, supervisor) y ejecuta el bucle de vida: beat(0) cada 250 ms y
//     chequeo de parada (duracion transcurrida o SIGINT). Al terminar: detiene
//     los componentes en orden (senalizacion → pump → cloudflared), publica
//     Terminated y loguea el cierre.
//   - Cada componente gestiona sus propios threads (ver sus headers).
//   - NINGUN thread lanza: los fallos se loguean y se degradan. La parada se
//     senala con un std::atomic<bool>; no hay excepciones cruzando el borde de
//     un thread.
//
// Estrategia de errores:
//   - Si un componente no puede arrancar (puerto en uso, cloudflared ausente),
//     se loguea (componente "gateway", codigo de Status) y se CONTINUA: un
//     componente caido no tumba al gateway (sin tunel solo no hay conexiones
//     nuevas; sin pump el invitado no recibe retorno).
//   - El HealthBlock: primero intenta open() (el launcher ya lo creo); si no
//     existe, create() con 4 slots para poder correr en solitario.
//   - Sin dialogos de sistema: todo el log va a stderr con el componente
//     "gateway" (formato `nivel | gateway | codigo | contexto`).
//
// Compila en Windows/MSVC y en Linux; todo lo Windows-only va detras de
// #ifdef _WIN32. No depende de Qt ni de libobs.
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ba_core (frozen headers).
#include "ipc/HealthBlock.hpp"

// Componentes del gateway.
#include "Cloudflared.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {

using namespace ba::gateway;
using namespace ba::ipc;

/// Version del proceso B (se publica en BA.Health).
constexpr uint32_t kProcessVersion = 2;
/// Slot de salud de B (B ocupa el slot 0, como el fake-gateway).
constexpr std::size_t kHealthSlot = 0;
constexpr std::size_t kHealthSlotCount = 4;

/// Convierte un string UTF-8 a std::wstring (para los nombres de flujos IPC).
std::wstring toWide(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out;
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    }
    return out;
#else
    // Linux / build offline: los nombres son ASCII; copia byte a byte.
    std::wstring out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
#endif
}

/// Senal de parada (SIGINT en POSIX, CtrlHandler en Windows).
std::atomic<bool> g_stop{false};

#ifdef _WIN32
BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        g_stop.store(true);
    }
    return TRUE;
}
#else
void sigHandler(int) { g_stop.store(true); }
#endif

} // namespace

/// Punto de entrada del gateway real (Proceso B).
///
/// :param argc: cantidad de argumentos de linea de comandos.
/// :param argv: argumentos (ver Config::parseArgs).
/// :returns: 0 en cierre ordenado; 1 si no se pudo abrir el HealthBlock.
int main(int argc, char** argv) {
    Config cfg;
    cfg.parseArgs(argc, argv);
    cfg.derivePaths();
    Log log;

    log.info("arranque: guests=" + std::to_string(cfg.guests) + " port=" + std::to_string(cfg.port) +
             " room=" + cfg.room + " webroot=" + cfg.webroot +
             " webrtc=" + (BA_GATEWAY_HAVE_WEBRTC ? "libwebrtc" : "NULL"));

    // Handler de senal para parada ordenada (SIGINT / Ctrl+C).
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
#else
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);
#endif

    // --- HealthBlock (BA.Health): open si el launcher lo creo, si no create.
    HealthBlock health;
    {
        Status s = HealthBlock::open(HealthBlock::kName, health);
        if (!s.ok) {
            s = HealthBlock::create(HealthBlock::kName, kHealthSlotCount, health);
            if (!s.ok) {
                log.error("no se pudo crear BA.Health", s.code);
                return 1;
            }
        }
    }
    const uint32_t pid = static_cast<uint32_t>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        static_cast<long>(getpid())
#endif
    );
    health.setIdentity(kHealthSlot, pid, kProcessVersion);
    health.setState(kHealthSlot, ProcessState::Running);
    log.info("BA.Health slot 0 = Running (pid=" + std::to_string(pid) + ")");

    // --- Auth de invitado (TokenService). La clave HMAC es estable por
    // instalacion (el launcher la inyectaria; aqui un default determinista).
    TokenAuth auth("ba-gateway-hmac-secret", cfg.pin, 5, 60, 300);

    // --- Hub de sesiones WebRTC (1..guests).
    WebrtcHub hub(cfg.guests, cfg.guestVideoW, cfg.guestVideoH, cfg.audioRate, cfg.audioChannels);

    // --- Motor de medios (IPC ↔ WebRTC).
    MediaPump::Cfg pumpCfg;
    pumpCfg.returnAudioName = toWide("BA.Audio.ContentForReturn");
    pumpCfg.programVideoName = toWide("BA.Program");
    for (int s = 1; s <= cfg.guests; ++s) {
        pumpCfg.guestAudioNames.push_back(toWide("BA.Audio.Guest." + std::to_string(s)));
        pumpCfg.guestVideoNames.push_back(toWide("BA.Guest." + std::to_string(s)));
    }
    pumpCfg.audioRate = cfg.audioRate;
    pumpCfg.audioChannels = cfg.audioChannels;
    pumpCfg.programW = cfg.videoW;
    pumpCfg.programH = cfg.videoH;
    pumpCfg.guestVideoW = cfg.guestVideoW;
    pumpCfg.guestVideoH = cfg.guestVideoH;
    MediaPump pump(hub, pumpCfg, log);

    // --- Servidor de senalizacion (HTTP + WS) en 127.0.0.1.
    SignalingServer::Cfg sigCfg;
    sigCfg.port = cfg.port;
    sigCfg.webroot = cfg.webroot;
    sigCfg.room = cfg.room;
    sigCfg.maxGuests = cfg.guests;
    SignalingServer server(sigCfg, auth, hub, log);
    server.setPump(&pump);

    // --- Supervisor de cloudflared (tunel).
    CloudflaredSupervisor cloudflared(cfg.cloudflaredPath, cfg.tunnelToken, cfg.metricsPort,
                                      cfg.cloudflaredLog, log);

    // --- Arranque de los componentes (degradacion elegante).
    if (!server.start()) {
        log.error("senalizacion no arranco; el gateway sigue sin tunel web", -1);
    }
    pump.start();
    cloudflared.start();

    // --- Bucle de vida: beat(0) cada 250 ms hasta duracion o senal.
    const auto start = std::chrono::steady_clock::now();
    const auto beatPeriod = std::chrono::milliseconds(250);
    health.beat(kHealthSlot); // primer latido inmediato

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (cfg.durationMs > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (elapsed >= cfg.durationMs) break;
        }
        std::this_thread::sleep_for(beatPeriod);
        health.beat(kHealthSlot);
    }

    log.info("parando: deteniendo componentes...");

    // --- Shutdown ordenado: senalizacion → pump → cloudflared → Terminated.
    server.stop();
    pump.stop();
    hub.closeAll();
    cloudflared.stop();
    health.setState(kHealthSlot, ProcessState::Terminated);
    log.info("cierre ordenado: BA.Health slot 0 = Terminated; exito");
    return 0;
}
