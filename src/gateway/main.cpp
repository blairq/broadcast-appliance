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
#include <cstdlib> // std::getenv (inyeccion de secretos por entorno)
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ba_core (frozen headers).
#include "ipc/HealthBlock.hpp"
#include "security/SecretsStore.hpp" // randomSecretHex (clave HMAC por arranque)

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

/// Nombre de la variable de entorno con la que el launcher (Proceso 0) puede
/// inyectar la clave HMAC de los tokens de invitado. Se usa el entorno y no la
/// linea de comandos a proposito: los argumentos son visibles en la lista de
/// procesos de la maquina, el entorno de un hijo no.
constexpr const char* kEnvHmacSecret = "BA_GATEWAY_HMAC_SECRET";
/// Idem para el PIN de la sala.
constexpr const char* kEnvRoomPin = "BA_GATEWAY_PIN";

/// Lee una variable de entorno; cadena vacia si no esta definida.
std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return (v != nullptr) ? std::string(v) : std::string();
}

/// Genera un PIN numerico de `digits` digitos con la entropia de ba_core.
///
/// Se filtran los nibbles hexadecimales que caen en '0'..'9' (muestreo por
/// rechazo): cada nibble es uniforme en [0,16) y, condicionado a ser < 10,
/// uniforme en [0,10). No introduce el sesgo de un modulo 10 sobre un byte.
///
/// :param digits: cantidad de digitos del PIN.
/// :param out: recibe el PIN; vacio si no se pudo generar.
/// :returns: true si se genero un PIN de `digits` digitos.
bool randomPin(std::size_t digits, std::string& out) {
    out.clear();
    for (int attempt = 0; attempt < 8 && out.size() < digits; ++attempt) {
        std::string hex;
        if (!ba::security::randomSecretHex(32, hex)) {
            return false;
        }
        for (char c : hex) {
            if (c >= '0' && c <= '9') {
                out.push_back(c);
                if (out.size() == digits) break;
            }
        }
    }
    if (out.size() != digits) {
        out.clear();
        return false;
    }
    return true;
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
    // Log rotativo en <root>/Logs (spec §11.4). Sin esto el gateway solo
    // escribia a stderr y en campo no quedaba ningun rastro de B en disco.
    if (!cfg.logsDir.empty()) {
        log.openFile(cfg.logsDir);
    }

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

    // --- Auth de invitado (TokenService) ------------------------------------
    //
    // Clave HMAC: se genera ALEATORIA en cada arranque, salvo que el launcher
    // inyecte una por entorno. B es a la vez el emisor y el verificador de los
    // tokens, y estos viven 300 s, asi que una clave por proceso alcanza y
    // evita tener un secreto en disco que proteger. Una clave fija en el
    // binario seria equivalente a no tener autenticacion: el bundle es
    // portable y cualquiera que lo tenga podria firmar tokens validos.
    //
    // Fail-closed: si no hay entropia NO se sigue con un valor por defecto; el
    // proceso aborta, porque arrancar con una clave predecible es peor que no
    // arrancar.
    std::string hmacSecret = envOrEmpty(kEnvHmacSecret);
    if (!hmacSecret.empty()) {
        log.info("auth: clave HMAC inyectada por el entorno (" + std::string(kEnvHmacSecret) + ")");
    } else if (ba::security::randomSecretHex(32, hmacSecret)) {
        log.info("auth: clave HMAC generada para este arranque (256 bits)");
    } else {
        log.error("auth: sin fuente de entropia para la clave HMAC; no se arranca", -1);
        health.setState(kHealthSlot, ProcessState::Terminated);
        return 1;
    }

    // PIN de la sala: por entorno, o generado al azar y logueado para que el
    // operador lo lea. Sin PIN configurado no se emite ningun token (el
    // validador de TokenAuth rechaza el PIN vacio), asi que la degradacion es
    // segura, pero un PIN aleatorio deja el gateway usable sin volverlo
    // adivinable.
    if (cfg.pinFromCli) {
        log.warn("auth: --pin llega por linea de comandos y es visible en la lista de procesos; "
                 "preferir " +
                     std::string(kEnvRoomPin),
                 -1);
    }
    std::string roomPin = !cfg.pin.empty() ? cfg.pin : envOrEmpty(kEnvRoomPin);
    if (roomPin.empty()) {
        if (randomPin(6, roomPin)) {
            log.info("auth: PIN de sala generado para este arranque: " + roomPin);
        } else {
            log.error("auth: no se pudo generar un PIN; la sala queda cerrada hasta un ChangePin", -1);
        }
    }
    // Umbral del lockout GLOBAL del TokenService. El freno real contra la
    // fuerza bruta es el rate-limit POR IP del servidor de señalizacion (5
    // intentos / 60 s por origen). Este contador global se deja deliberadamente
    // alto para que actue solo como tope de ultimo recurso ante un ataque
    // distribuido: con el valor anterior (5) los fallos de UN solo cliente
    // cerraban /api/join para todos los invitados, que era una negacion de
    // servicio trivial contra la sala.
    constexpr int kGlobalPinAttempts = 100;
    constexpr int kGlobalLockoutSec = 60;
    TokenAuth auth(std::move(hmacSecret), roomPin, kGlobalPinAttempts, kGlobalLockoutSec, 300);

    // --- Hub de sesiones WebRTC (1..guests).
    WebrtcHub hub(cfg.guests, cfg.guestVideoW, cfg.guestVideoH, cfg.audioRate, cfg.audioChannels);

    // --- Motor de medios (IPC ↔ WebRTC).
    MediaPump::Cfg pumpCfg;
    pumpCfg.returnAudioName = toWide("BA.Audio.ContentForReturn");
    pumpCfg.programVideoName = toWide("BA.Program");
    // Los vectores de nombres se indexan POR SLOT (1..guests): el indice 0 es
    // un hueco reservado, no un invitado. MediaPump::start() los recorre con el
    // numero de slot, asi que un vector sin ese hueco desplaza cada invitado un
    // puesto (el slot 1 publicaria en BA.Audio.Guest.2) y deja al ultimo sin
    // canal. Ver MediaPump::Cfg.
    pumpCfg.guestAudioNames.emplace_back(); // indice 0: reservado
    pumpCfg.guestVideoNames.emplace_back(); // indice 0: reservado
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
