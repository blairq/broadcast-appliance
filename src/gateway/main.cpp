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
//     - PinggySupervisor : tunel de pinggy.io (ba-pinggy) hacia la escucha
//                          propia del tunel (todo externo) y el TURN de la caja.
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

#include <algorithm>
#include "ajustes/Ajustes.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib> // std::getenv (inyeccion de secretos por entorno)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Diagnostico del proceso, que es lo unico especifico del sistema que hay en
// este archivo: el uso de CPU y memoria (getrusage) y el hilo mas cargado
// (/proc/self/task). En Windows eso lo dan GetProcessTimes y
// GetProcessMemoryInfo, mas abajo, y el detalle por hilo directamente no esta.
#if !defined(_WIN32)
#include <dirent.h>
#include <sys/resource.h>
#endif

// ba_core (frozen headers).
#include "ipc/CanalesEscena.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "security/CredentialStore.hpp"
#include "security/SecretsStore.hpp" // randomSecretHex (clave HMAC por arranque)

// Componentes del gateway.
#include "Cloudflared.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "Pinggy.hpp"

#include "diag/Caidas.hpp"
#include "diag/Perfil.hpp"
#include "diag/SalidaAsincrona.hpp"
#include "MediaPump.hpp"
#include "net/NetUtil.hpp"
#include "turn/Ice.hpp"
#include "turn/Servidor.hpp"
#include "net/TlsTerminator.hpp"
#include "proxy/GestorProxy.hpp"
#include "net/WgCrypto.hpp"
#include "net/WgDevice.hpp"
#if BA_HAVE_LWIP
#include "net/IpStack.hpp"
#endif
#include "Room.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "TokenAuth.hpp"
#include "Webrtc.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <psapi.h> // PROCESS_MEMORY_COUNTERS (la K32* vive en kernel32)
#include <windows.h>
#include <winsock2.h> // getaddrinfo / inet_ntop (LEAN_AND_MEAN: windows.h no trae winsock.h)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
/// Token del tunel de Cloudflare (se pasa al hijo como TUNNEL_TOKEN).
constexpr const char* kEnvTunnelToken = "BA_TUNNEL_TOKEN";

/// Token de pinggy.io (se pasa al hijo ba-pinggy por su entorno, nunca por argv).
/// Si no esta, la credencial "pinggy-token" (cifrada en Credenciales/).
constexpr const char* kEnvPinggyToken = "BA_PINGGY_TOKEN";

/// Token de la API de control del operador (si el launcher lo inyecta).
constexpr const char* kEnvAdminToken = "BA_ADMIN_TOKEN";

/// Publica como encontrar la API de control: <bundle>/Runtime/gateway-admin.json con
/// puerto y token, legible SOLO por el usuario (0600 en POSIX; en Windows el
/// directorio de runtime cuelga del perfil del usuario). La app Qt lo lee.
bool writeAdminFile(const std::filesystem::path& file, const std::string& content) {
    const std::filesystem::path tmp = file.string() + ".tmp";
#ifdef _WIN32
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << content;
        if (!f) return false;
    }
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const bool ok = ::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
    ::close(fd);
    if (!ok) return false;
#endif
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

/// Clave privada WireGuard de ESTE equipo: BA_WG_PRIVATE_KEY (base64) o la
/// credencial "wireguard-key" (cifrada en Credenciales/; se genera la primera
/// vez). El equipo portable lleva su identidad y una actualizacion no la pisa.
bool loadOrCreateWgKey(ba::gateway::net::Key32& priv, std::string& origin);

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

bool loadOrCreateWgKey(ba::gateway::net::Key32& priv, std::string& origin) {
    namespace net = ba::gateway::net;
    if (const char* env = std::getenv("BA_WG_PRIVATE_KEY"); env != nullptr && *env != '\0') {
        origin = "BA_WG_PRIVATE_KEY";
        return net::fromBase64(priv, env);
    }
    const ba::security::CredentialStore store = ba::security::CredentialStore::bundle();
    std::string b64;
    if (store.read("wireguard-key", b64)) {
        while (!b64.empty() && (b64.back() == '\n' || b64.back() == '\r' || b64.back() == ' '))
            b64.pop_back();
        if (net::fromBase64(priv, b64)) {
            origin = "Credenciales/wireguard-key";
            return true;
        }
    }
    net::Key32 pub{};
    if (!net::x25519Generate(priv, pub)) return false;
    origin = "Credenciales/wireguard-key (nueva)";
    return store.write("wireguard-key", net::toBase64(priv));
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
namespace {

/// CPU (porcentaje del total de la maquina desde la vuelta anterior) y memoria
/// residente del propio proceso. Sin dependencias: /proc en Linux,
/// GetProcessTimes en Windows, task_info en macOS.
nlohmann::json processUsage(const WebrtcHub* hub) {
    static double lastCpuSec = 0;
    static std::chrono::steady_clock::time_point lastAt{};
    double cpuSec = 0;
    double rssMb = 0;
#if defined(_WIN32)
    // Nombres largos a proposito: los cortos (c, e, k, u) tapaban el `u` del
    // JSON que se arma mas abajo en esta misma funcion, y el compilador de
    // Windows se quedaba en «redefinition of 'u' with a different type». Con
    // este archivo compilando solo en Linux, eso no lo veia nadie.
    FILETIME ftCreacion, ftSalida, ftKernel, ftUsuario;
    if (GetProcessTimes(GetCurrentProcess(), &ftCreacion, &ftSalida, &ftKernel, &ftUsuario)) {
        const auto to100ns = [](const FILETIME& f) {
            return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
        };
        cpuSec = static_cast<double>(to100ns(ftKernel) + to100ns(ftUsuario)) / 1e7;
    }
    // K32GetProcessMemoryInfo y no GetProcessMemoryInfo: la version K32 esta en
    // kernel32, que ya se enlaza, y evita tener que arrastrar psapi.lib.
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0)
        rssMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
#else
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        cpuSec = static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
                 static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
#if defined(__APPLE__)
        rssMb = static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
        rssMb = static_cast<double>(ru.ru_maxrss) / 1024.0; // Linux informa KB
#endif
    }
#endif
    const auto now = std::chrono::steady_clock::now();
    double pct = 0;
    if (lastAt.time_since_epoch().count() != 0) {
        const double wall = std::chrono::duration<double>(now - lastAt).count();
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        if (wall > 0) pct = 100.0 * (cpuSec - lastCpuSec) / (wall * cores);
    }
    lastCpuSec = cpuSec;
    lastAt = now;
    nlohmann::json u{{"cpuPct", std::round(std::clamp(pct, 0.0, 100.0) * 10) / 10},
                     {"memMb", std::round(rssMb)},
                     {"nucleos", std::max(1u, std::thread::hardware_concurrency())}};

    // Ancho de banda de video, que es lo unico que dice si el limite de la
    // sala es la RED y no la maquina. Se informa en kbit/s sobre el intervalo
    // entre dos llamadas, no el acumulado: lo que interesa es el caudal de
    // AHORA, no cuanto paso desde que arranco.
    //
    // Cuenta el video que el gateway PROCESA (ver WebrtcHub::trafico): con un
    // gateway suelto, sin A del otro lado, da cero aunque los invitados esten
    // mandando.
    if (hub != nullptr) {
        static uint64_t lastIn = 0;
        static uint64_t lastOut = 0;
        static std::chrono::steady_clock::time_point lastNet{};
        const WebrtcHub::Trafico tr = hub->trafico();
        const double wallN = lastNet.time_since_epoch().count() == 0
                                 ? 0
                                 : std::chrono::duration<double>(now - lastNet).count();
        if (wallN > 0.2) {
            // El contador solo sube, pero si alguna vez se reiniciara, restar
            // daria un numero absurdo: se acota en cero.
            const auto kbps = [wallN](uint64_t ahora, uint64_t antes) {
                return ahora < antes ? 0.0 : std::round((ahora - antes) * 8.0 / 1000.0 / wallN);
            };
            u["kbpsEntrada"] = kbps(tr.entrada, lastIn);
            u["kbpsSalida"] = kbps(tr.salida, lastOut);
        }
        if (wallN > 0.2 || lastNet.time_since_epoch().count() == 0) {
            lastIn = tr.entrada;
            lastOut = tr.salida;
            lastNet = now;
        }
    }
#if defined(__linux__)
    // Hilo mas cargado (porcentaje de UN nucleo). libwebrtc reparte la
    // decodificacion de cada invitado en su propio hilo, pero la red, el worker
    // y la senalizacion son UNO para todas las conexiones: si ese hilo se va a
    // 100 el techo no es la maquina sino ese nucleo.
    static std::map<int, double> lastThread;
    static std::chrono::steady_clock::time_point lastThreadAt{};
    const double ticks = static_cast<double>(sysconf(_SC_CLK_TCK));
    std::map<int, double> nowThread;
    std::string topName;
    double topPct = -1; // asi se informa el hilo aunque todos esten en cero
    const double wallT = lastThreadAt.time_since_epoch().count() == 0
                             ? 0
                             : std::chrono::duration<double>(now - lastThreadAt).count();
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) {
            const int tid = std::atoi(e->d_name);
            if (tid <= 0) continue;
            std::ifstream st("/proc/self/task/" + std::string(e->d_name) + "/stat");
            std::string line;
            if (!std::getline(st, line)) continue;
            // El nombre va entre parentesis y puede traer espacios.
            const std::size_t close = line.rfind(')');
            if (close == std::string::npos) continue;
            std::istringstream rest(line.substr(close + 2));
            std::string field;
            double utime = 0;
            double stime = 0;
            for (int i = 3; i <= 15 && rest >> field; ++i) {
                if (i == 14) utime = std::atof(field.c_str());
                if (i == 15) stime = std::atof(field.c_str());
            }
            const double cpu = ticks > 0 ? (utime + stime) / ticks : 0;
            nowThread[tid] = cpu;
            const auto prev = lastThread.find(tid);
            if (wallT > 0 && prev != lastThread.end()) {
                const double p = 100.0 * (cpu - prev->second) / wallT;
                if (p > topPct) {
                    topPct = p;
                    const std::size_t open = line.find('(');
                    topName = open != std::string::npos ? line.substr(open + 1, close - open - 1) : "";
                }
            }
        }
        closedir(d);
    }
    lastThread = std::move(nowThread);
    lastThreadAt = now;
    if (!topName.empty()) {
        u["hiloTop"] = topName;
        u["hiloPct"] = std::round(std::clamp(topPct, 0.0, 100.0) * 10) / 10;
    }
#endif
    return u;
}

} // namespace

int main(int argc, char** argv) {
    // Que escribir a Logs/gateway.log nunca trabe un hilo (diag/SalidaAsincrona.hpp).
    ba::diag::desacoplarSalidaEstandar();
    std::atexit([] { ba::diag::vaciarSalidaEstandar(500); });
    ba::platform::redirectUserDirs(); // portable: nada al perfil del usuario
    // Credenciales en claro de versiones anteriores -> Credenciales/ cifradas.
    ba::security::CredentialStore::bundle().importLegacy(ba::platform::configRoot());
    Config cfg;
    // Config/gateway.json (lo escribe la interfaz de A); la linea de comandos
    // lo pisa.
    cfg.loadFile((ba::platform::configRoot() / "Config" / "gateway.json").string());
    ba::ajustes::cargar(cfg.ajustes); // Configuración → avanzados de la sala
    cfg.parseArgs(argc, argv);
    cfg.derivePaths();
    Log log;
    // Log rotativo en <root>/Logs (spec §11.4). Sin esto el gateway solo
    // escribia a stderr y en campo no quedaba ningun rastro de B en disco.
    if (!cfg.logsDir.empty()) {
        log.openFile(cfg.logsDir);
        // Y que una caida deje la pila escrita (ver diag/Caidas.hpp).
        ba::diag::instalarManejadorDeCaidas("gateway");
        ba::diag::Perfil::arrancar("gateway", cfg.logsDir); // solo en un build con BA_PERFILAR
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
    // Red de seguridad: ningun socket o pipe cerrado del otro lado debe tumbar
    // al gateway (los envios ya usan MSG_NOSIGNAL / SO_NOSIGPIPE).
    std::signal(SIGPIPE, SIG_IGN);
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
    const int kGlobalPinAttempts = ba::ajustes::entero("room.pinGlobalIntentos", 100);
    const int kGlobalLockoutSec = ba::ajustes::entero("room.pinGlobalBloqueoS", 60);
    TokenAuth auth(std::move(hmacSecret), roomPin, kGlobalPinAttempts, kGlobalLockoutSec,
                   ba::ajustes::entero("room.tokenTtlS", 300));

    // --- Hub de sesiones WebRTC (1..guests).
    // Pool de sesiones: una por participante que publica (adentro, en el lobby
    // o en la fila de oradores, que ve el programa).
    WebrtcHub hub(cfg.guestsReserve + cfg.lobbySlots + cfg.maxBackstage + 2 /* los remotos con la palabra (un debate) */ +
                      cfg.vistasRemotas /* lo que miran los integrantes en su celular */, cfg.guestVideoW, cfg.guestVideoH,
                  cfg.audioRate, cfg.audioChannels);

    // --- Motor de medios (IPC ↔ WebRTC).
    MediaPump::Cfg pumpCfg;
    pumpCfg.returnAudioName = toWide("BA.Audio.ContentForReturn");
    pumpCfg.programVideoName = toWide("BA.Program");
    // Los vectores de nombres se indexan POR SLOT (1..guests): el indice 0 es
    // un hueco reservado, no un invitado. MediaPump::start() los recorre con el
    // numero de slot, asi que un vector sin ese hueco desplaza cada invitado un
    // puesto (el slot 1 publicaria en BA.Audio.Guest.2) y deja al ultimo sin
    // canal. Ver MediaPump::Cfg.
    pumpCfg.operatorVideoName = toWide("BA.Operator");
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) pumpCfg.escenaVideoNames.push_back(ba::ipc::nombreCanalEscena(k));
    pumpCfg.operatorAudioName = toWide("BA.Audio.Operator");
    // Nombres por slot con el [0] reservado (no hay slot 0).
    for (auto* v : {&pumpCfg.guestAudioNames, &pumpCfg.guestVideoNames, &pumpCfg.shareVideoNames,
                    &pumpCfg.lobbyAudioNames, &pumpCfg.lobbyVideoNames, &pumpCfg.returnSlotNames}) {
        v->emplace_back();
    }
    for (int s = 1; s <= cfg.guestsReserve; ++s) { // los reservados: el cupo cambia en caliente
        pumpCfg.guestAudioNames.push_back(toWide("BA.Audio.Guest." + std::to_string(s)));
        pumpCfg.returnSlotNames.push_back(toWide("BA.Audio.Return." + std::to_string(s)));
        pumpCfg.guestVideoNames.push_back(toWide("BA.Guest." + std::to_string(s)));
        pumpCfg.shareVideoNames.push_back(toWide("BA.GuestShare." + std::to_string(s)));
    }
    // R3: el remoto con la palabra (un canal; [0] reservado como los demás).
    // Dos canales: el que tiene la palabra y, en un debate entre remotos, el otro.
    pumpCfg.remotoAudioNames = {std::wstring(), toWide("BA.Audio.Remoto.1"), toWide("BA.Audio.Remoto.2")};
    pumpCfg.remotoVideoNames = {std::wstring(), toWide("BA.Remoto.1"), toWide("BA.Remoto.2")};
    pumpCfg.remotoContenidoNames = {std::wstring(), toWide("BA.RemotoContenido.1"), toWide("BA.RemotoContenido.2")};
    for (int k = 1; k <= cfg.lobbySlots; ++k) {
        pumpCfg.lobbyAudioNames.push_back(toWide("BA.Audio.Lobby." + std::to_string(k)));
        pumpCfg.lobbyVideoNames.push_back(toWide("BA.Lobby." + std::to_string(k)));
    }
    pumpCfg.backstageVideoNames.push_back({});
    for (int k = 1; k <= cfg.backstagePreviews; ++k)
        pumpCfg.backstageVideoNames.push_back(toWide("BA.Backstage." + std::to_string(k)));
    pumpCfg.audioRate = cfg.audioRate;
    pumpCfg.audioChannels = cfg.audioChannels;
    pumpCfg.programW = cfg.videoW;
    pumpCfg.programH = cfg.videoH;
    pumpCfg.programVideoW = cfg.programVideoW;
    pumpCfg.sinVideoNs = static_cast<uint64_t>(cfg.sinVideoPlacaMs) * 1000000ULL;
    pumpCfg.programVideoH = cfg.programVideoH;
    pumpCfg.guestVideoW = cfg.guestVideoW;
    pumpCfg.guestVideoH = cfg.guestVideoH;
    MediaPump pump(hub, pumpCfg, log);

    // --- Sala (lobby, participantes, chat, archivos) -------------------------
    Room::Cfg roomCfg;
    roomCfg.name = cfg.room;
    roomCfg.maxGuests = cfg.guests;
    roomCfg.reserva = cfg.guestsReserve;
    roomCfg.lobby = cfg.lobby;
    roomCfg.lobbySlots = cfg.lobbySlots;
    roomCfg.backstageSlots = cfg.backstagePreviews;
    Room room(roomCfg);
    // La señalización siempre se alcanza en 127.0.0.1 (con «red local» escucha
    // en todas; con una IP elegida, en 127.0.0.1: ver sigCfg).
    const std::string guestUrl = "http://127.0.0.1:" + std::to_string(cfg.port) + "/";
    // El enlace que copia el operador lleva la sala en el fragmento (#room=),
    // igual que el QR: el invitado no tiene que tipear un dato que no conoce.
    room.setHostInfo(roomPin, guestUrl + "#room=" + cfg.room);

    // Archivos que comparten los invitados: de B, se limpian en cada arranque
    // (A copia lo que toma a su propia carpeta de medios).
    ShareStore shares(pathFromUtf8(cfg.sharesDir),
                      static_cast<uint64_t>(cfg.maxUploadMB) * 1024ull * 1024ull);
    shares.clear();

    // Token de la API de control: del launcher o aleatorio por arranque.
    std::string adminToken = envOrEmpty(kEnvAdminToken);
    if (adminToken.empty() && !ba::security::randomSecretHex(32, adminToken)) {
        log.error("control: sin entropia para el token de la API; la sala queda sin control", -1);
        adminToken.clear();
    }

    // --- Servidor HTTP + WS (invitados) y API de control (operador).
    SignalingServer::Cfg sigCfg;
    sigCfg.port = cfg.port;
    // Con UNA IP elegida, esa IP la atiende el HTTPS (por donde entran los de la
    // red local: sin HTTPS el navegador no da cámara); la señalización queda en
    // 127.0.0.1, que es adonde le reenvían el HTTPS y el túnel de Cloudflare.
    const bool unaInterfaz = cfg.bindAddress != "127.0.0.1" && cfg.bindAddress != "0.0.0.0";
    sigCfg.bindAddress = unaInterfaz ? std::string("127.0.0.1") : cfg.bindAddress;
    sigCfg.webroot = cfg.webroot;
    sigCfg.room = cfg.room;
    sigCfg.maxGuests = cfg.guests;
    sigCfg.pinIntentos = cfg.pinIntentos;
    sigCfg.pinBloqueoS = cfg.pinBloqueoS;
    sigCfg.adminPort = cfg.adminPort;
    sigCfg.adminWebroot = cfg.adminWebroot;
    // Las caras propias de los botones (fotos y videos cortos): <datos>/Media/botonera.
    sigCfg.carasBotonera = (pathFromUtf8(cfg.configDir).parent_path() / "Media" / "botonera").string();
    {
        std::filesystem::path guestRoot(cfg.webroot);
        if (!guestRoot.has_filename()) guestRoot = guestRoot.parent_path(); // "Web/guest/"
        sigCfg.boardWebroot = (guestRoot.parent_path() / "board").string();
        sigCfg.prompterWebroot = (guestRoot.parent_path() / "prompter").string();
        sigCfg.correccionWebroot = (guestRoot.parent_path() / "correccion").string();
        sigCfg.rotulosWebroot = (guestRoot.parent_path() / "rotulos").string();
        sigCfg.pantallaWebroot = (guestRoot.parent_path() / "pantalla").string();
        sigCfg.castWebroot = (guestRoot.parent_path() / "cast").string();
        sigCfg.comunWebroot = (guestRoot.parent_path() / "comun").string();
        sigCfg.publicoWebroot = (guestRoot.parent_path() / "publico").string();
        sigCfg.botoneraWebroot = (guestRoot.parent_path() / "botonera").string();
        sigCfg.sesionWebroot = (guestRoot.parent_path() / "sesion").string();
    }
    // Clave del control del teleprompter: nueva en cada arranque. No hace
    // falta que sobreviva -- el operador manda el enlace (o el QR) cuando va a
    // usarlo, y una clave que no se guarda en ningun lado no se filtra.
    if (!ba::security::randomSecretHex(16, sigCfg.prompterKey)) sigCfg.prompterKey.clear();
    // La del puesto de corrección de subtítulos (T44): también por arranque.
    if (!ba::security::randomSecretHex(16, sigCfg.correccionKey)) sigCfg.correccionKey.clear();
    // Idem los rotulos, con su propia clave: que el que rotula desde el celular
    // no pueda ademas frenarle el texto al orador.
    if (!ba::security::randomSecretHex(16, sigCfg.rotulosKey)) sigCfg.rotulosKey.clear();
    // Y la del publico. Esta la tiene TODO el que esta en la sala —viaja en un
    // QR que se proyecta o se reparte—, asi que no es un secreto fuerte y no
    // pretende serlo: es lo que evita que la cola de preguntas la llene
    // cualquiera que pase por el tunel. Nueva en cada arranque, como las otras:
    // las preguntas del evento de ayer no son las de hoy.
    if (!ba::security::randomSecretHex(16, sigCfg.publicoKey)) sigCfg.publicoKey.clear();
    sigCfg.adminToken = adminToken;
    // La escucha propia del tunel de pinggy (127.0.0.1:8786): todo lo que entra
    // por ahi es externo, venga con la IP que venga (ver SignalingServer.hpp).
    sigCfg.tunel = cfg.pinggyActivo;
    sigCfg.tunnelPort = cfg.pinggyPuertoTunel;
    SignalingServer server(sigCfg, auth, hub, room, shares, log);
    server.setPump(&pump);

    // --- Acceso directo de invitados: red de WebRTC, HTTPS y tunel ------------
    namespace net = ba::gateway::net;
    const auto logLine = [&log](const std::string& l) { log.info(l); };
    // Rango UDP fijo (reglas de firewall y redireccion del tunel). Con tunel,
    // WebRTC atiende tambien en 127.0.0.1: ahi entrega el puente del tunel.
    hub.setNetwork(cfg.rtcPortMin, cfg.rtcPortMax, cfg.wgEnabled);
    std::string publicIp;
    std::string publicHost;
    if (cfg.wgEnabled && !cfg.wgPublicAddress.empty()) {
        uint16_t ignored = 0;
        net::splitHostPort(cfg.wgPublicAddress, publicHost, ignored, 0);
        addrinfo hints{};
        hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (getaddrinfo(publicHost.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
            char buf[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr, buf, sizeof(buf));
            publicIp = buf;
            freeaddrinfo(res);
        } else {
            log.warn("tunel: no se pudo resolver la direccion publica " + publicHost, -1);
        }
        server.setPublicAddress(publicIp);
    }
    const std::vector<std::string> lanIps = net::localIPv4s();
    // Lo que va en el QR y el enlace por la red local: lo elegido en Configuración; si
    // no, la IP de la interfaz donde escucha; si no, la primera de la red (localIPv4s
    // las ordena: la de verdad antes que las virtuales).
    const std::string hostLocal = !cfg.urlHost.empty()                                          ? cfg.urlHost
                                  : (cfg.bindAddress != "0.0.0.0" && cfg.bindAddress != "127.0.0.1") ? cfg.bindAddress
                                  : !lanIps.empty()                                             ? lanIps.front()
                                                                                                : std::string();
    log.info("red: IP de la máquina " + [&] {
        std::string l;
        for (const auto& x : lanIps) l += (l.empty() ? "" : ", ") + x;
        return l.empty() ? std::string("(ninguna)") : l;
    }() + "; en el QR: " + (hostLocal.empty() ? std::string("(ninguna)") : hostLocal));

    std::unique_ptr<net::TlsTerminator> tls;
    if (cfg.https) {
        net::TlsTerminator::Config tc;
        tc.bindAddress = cfg.bindAddress;
        tc.port = cfg.httpsPort;
        tc.backendPort = cfg.port;
        tc.credentialsDir = ba::platform::credentialsDir().string();
        tc.names = lanIps;
        if (!cfg.urlHost.empty() && std::find(tc.names.begin(), tc.names.end(), cfg.urlHost) == tc.names.end())
            tc.names.push_back(cfg.urlHost); // el certificado también para el nombre elegido
        if (!publicHost.empty()) tc.names.push_back(publicHost);
        if (!publicIp.empty() && publicIp != publicHost) tc.names.push_back(publicIp);
        tls = std::make_unique<net::TlsTerminator>(tc, logLine);
        if (!tls->start()) {
            log.warn("https: no arranco; los invitados solo tienen http (sin camara fuera de localhost)", -1);
            tls.reset();
        }
    }

    // El proxy por nombre (T61): un DNS y un certificado por servicio, en el 443/80.
    std::unique_ptr<proxy::GestorProxy> gestorProxy;
    if (cfg.proxy.value("activo", false)) {
        gestorProxy = std::make_unique<proxy::GestorProxy>(cfg.proxy, cfg.port, ba::platform::credentialsDir().string(),
                                                           lanIps, logLine);
        if (!gestorProxy->start()) {
            log.warn("proxy: no arrancó (ver el motivo arriba)", -1);
            gestorProxy.reset();
        }
    }
    server.setProxy(gestorProxy.get());

    std::unique_ptr<net::WgDevice> wg;
#if BA_HAVE_LWIP
    std::unique_ptr<net::IpStack> ipstack;
#endif
    std::string wgPublicKey;
#if BA_HAVE_LWIP
    // La identidad WireGuard de este equipo existe aunque el tunel este
    // apagado: asi se puede generar la configuracion del servidor antes.
    {
        net::Key32 priv{};
        net::Key32 pub{};
        std::string origin;
        if (loadOrCreateWgKey(priv, origin) && net::x25519Public(pub, priv)) {
            wgPublicKey = net::toBase64(pub);
        }
        net::wipe(priv.data(), priv.size());
    }
#endif
    if (cfg.wgEnabled) {
#if BA_HAVE_LWIP
        net::WgConfig wc;
        std::string keyOrigin;
        uint32_t tunnelIp = 0;
        int prefix = 24;
        if (!loadOrCreateWgKey(wc.privateKey, keyOrigin)) {
            log.error("tunel: sin clave privada WireGuard; tunel deshabilitado", -1);
        } else if (!net::fromBase64(wc.peerPublicKey, cfg.wgServerPublicKey)) {
            log.error("tunel: falta la clave publica del servidor (wireguard.serverPublicKey)", -1);
        } else if (!net::parseCidr(cfg.wgAddress, tunnelIp, prefix)) {
            log.error("tunel: direccion invalida (wireguard.address = " + cfg.wgAddress + ")", -1);
        } else {
            if (const char* psk = std::getenv("BA_WG_PSK"); psk != nullptr && *psk != '\0') {
                net::fromBase64(wc.presharedKey, psk);
            }
            net::splitHostPort(cfg.wgEndpoint, wc.endpointHost, wc.endpointPort, 51820);
            wc.persistentKeepaliveSec = cfg.wgKeepalive;
            net::Key32 pub{};
            net::x25519Public(pub, wc.privateKey);
            wgPublicKey = net::toBase64(pub);
            wg = std::make_unique<net::WgDevice>(wc, logLine);
            ipstack =
                std::make_unique<net::IpStack>(*wg, net::IpStack::Config{tunnelIp, prefix, 1420}, logLine);
            ipstack->forwardTcp(cfg.wgHttpPort, cfg.port, true);
            if (tls) ipstack->forwardTcp(cfg.wgHttpsPort, tls->port(), true);
            ipstack->forwardUdpRange(cfg.rtcPortMin, cfg.rtcPortMax);
            if (wg->start() && ipstack->start()) {
                log.info("tunel: WireGuard hacia " + cfg.wgEndpoint + " como " + cfg.wgAddress + " (clave " +
                         keyOrigin + "; publica " + wgPublicKey + ")");
            } else {
                log.error("tunel: no arranco WireGuard/pila IP", -1);
                ipstack.reset();
                wg.reset();
            }
        }
#else
        log.error("tunel: este build no trae lwIP (make deps-lwip); tunel deshabilitado", -1);
#endif
    }

    // Enlace para los invitados: el mas util que haya.
    std::string bestUrl = guestUrl;
    if (!publicHost.empty() && wg) {
        bestUrl = (tls ? "https://" : "http://") + publicHost + "/";
    } else if (tls && cfg.bindAddress != "127.0.0.1" && !hostLocal.empty()) {
        bestUrl = "https://" + hostLocal + ":" + std::to_string(tls->port()) + "/";
    } else if (tls) {
        bestUrl = "https://127.0.0.1:" + std::to_string(tls->port()) + "/";
    }
    room.setHostInfo(roomPin, bestUrl + "#room=" + cfg.room);

    server.setServerConfigGenerator([&](const nlohmann::json& req) {
        // Claves NUEVAS para el servidor + su wg0.conf con las redirecciones
        // (HTTP/HTTPS y el rango UDP de WebRTC) hacia nuestra IP del tunel.
        nlohmann::json out{{"ok", false}};
        uint32_t ourIp = 0;
        int prefix = 24;
        if (wgPublicKey.empty() || !net::parseCidr(cfg.wgAddress, ourIp, prefix)) {
            out["error"] = "sin clave o direccion del tunel";
            return out;
        }
        net::Key32 sPriv{};
        net::Key32 sPub{};
        if (!net::x25519Generate(sPriv, sPub)) {
            out["error"] = "entropia";
            return out;
        }
        const uint32_t serverIp = (ourIp & (prefix == 0 ? 0u : 0xFFFFFFFFu << (32 - prefix))) | 1u;
        const auto ip = [](uint32_t v) {
            return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "." +
                   std::to_string((v >> 8) & 255) + "." + std::to_string(v & 255);
        };
        const std::string ours = ip(ourIp);
        const std::string iface = req.value("interface", std::string("eth0"));
        std::string endpointHost;
        uint16_t listenPort = 51820;
        net::splitHostPort(req.value("endpoint", cfg.wgEndpoint), endpointHost, listenPort, 51820);
        const std::string ports = std::to_string(cfg.wgHttpPort) + "," + std::to_string(cfg.wgHttpsPort);
        const std::string udp = std::to_string(cfg.rtcPortMin) + ":" + std::to_string(cfg.rtcPortMax);
        const auto rules = [&](const char* op) {
            std::string r;
            r += std::string("iptables -t nat ") + op + " PREROUTING -i " + iface +
                 " -p tcp -m multiport --dports " + ports + " -j DNAT --to-destination " + ours + "; ";
            r += std::string("iptables -t nat ") + op + " PREROUTING -i " + iface + " -p udp --dport " + udp +
                 " -j DNAT --to-destination " + ours + "; ";
            r += std::string("iptables ") + op + " FORWARD -i " + iface + " -o %i -j ACCEPT; ";
            r += std::string("iptables ") + op + " FORWARD -i %i -o " + iface +
                 " -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT";
            return r;
        };
        std::string conf;
        conf += "# /etc/wireguard/wg0.conf -- servidor con IP publica para Broadcast Appliance\n";
        conf += "# Instalar: apt install wireguard iptables   Activar: systemctl enable --now wg-quick@wg0\n";
        conf += "# '" + iface + "' es la interfaz publica del servidor: cambiala si se llama distinto.\n";
        conf += "# Abrir en el firewall del proveedor: UDP " + std::to_string(listenPort) + ", TCP " + ports +
                ", UDP " + udp + ".\n\n";
        conf += "[Interface]\n";
        conf += "Address = " + ip(serverIp) + "/" + std::to_string(prefix) + "\n";
        conf += "ListenPort = " + std::to_string(listenPort) + "\n";
        conf += "PrivateKey = " + net::toBase64(sPriv) + "\n";
        conf += "PostUp = sysctl -w net.ipv4.ip_forward=1; " + rules("-A") + "\n";
        conf += "PostDown = " + rules("-D") + "\n\n";
        conf += "[Peer]\n# Broadcast Appliance (este equipo)\n";
        conf += "PublicKey = " + wgPublicKey + "\n";
        conf += "AllowedIPs = " + ours + "/32\n";
        net::wipe(sPriv.data(), sPriv.size());
        out = {{"ok", true},
               {"serverPublicKey", net::toBase64(sPub)},
               {"serverAddress", ip(serverIp)},
               {"endpoint", endpointHost + ":" + std::to_string(listenPort)},
               {"config", conf}};
        return out;
    });

    // --- TURN de la caja (docs/planes/PINGGY.md, P3) ---------------------------
    // >>> ENGANCHE DEL TURN: aca se construye y arranca el servidor TURN
    // (ba::gateway::turn::Servidor, src/gateway/turn/) en 127.0.0.1:turnPuertoLocal,
    // ANTES del supervisor de pinggy, y se pone turnActivo = true si arranco.
    // Con eso pinggy suma los forwardings tlstcp/tcp (y udp) hacia el TURN, y
    // pinggy->turnPublico() (host:puerto, tipo en pinggy->estado().turnTipo)
    // es lo que va en los iceServers de los navegadores (turns: si es tlstcp,
    // turn:?transport=tcp si es tcp; estado().turnUdp para turn: por UDP).
    // Detenerlo en el cierre ordenado, despues de pinggy (ver abajo). <<<
    bool turnActivo = false;
    const uint16_t turnPuertoLocal = 3478;
    // El TURN de la caja (src/gateway/turn): solo con pinggy y «TURN por el túnel».
    // Relaya únicamente hacia las IP de esta máquina y el rango de WebRTC.
    ba::gateway::turn::Servidor::Config turnCfg;
    std::unique_ptr<ba::gateway::turn::Servidor> turnSrv;
    if (cfg.pinggyActivo && cfg.pinggyTurn) {
        turnCfg.puertoTcp = turnPuertoLocal;
        turnCfg.puertoUdp = cfg.pinggyTurnUdp ? turnPuertoLocal : 0;
        // También en la red local: en la sesión remota de la asamblea los navegadores
        // se conectan ENTRE ELLOS (integrante con integrante); uno de la sala y uno de
        // afuera solo se encuentran si los dos tienen un relay en esta caja. Pinggy
        // sigue entrando por 127.0.0.1. La política de peers no cambia.
        turnCfg.direccion = "0.0.0.0";
        const std::vector<std::string> ips = ba::gateway::net::localIPv4s();
        turnCfg.peersPermitidos = ips;
        turnCfg.peersPermitidos.push_back("127.0.0.1");
        turnCfg.direccionRelay = ips.empty() ? std::string("127.0.0.1") : ips.front();
        turnCfg.bindRelay = "0.0.0.0";
        turnCfg.rtcMin = cfg.rtcPortMin;
        turnCfg.rtcMax = cfg.rtcPortMax;
        turnCfg.secreto = ba::gateway::turn::secretoAleatorio();
        turnSrv = std::make_unique<ba::gateway::turn::Servidor>(turnCfg);
        std::string e;
        if (turnSrv->start(&e)) {
            turnActivo = true;
            log.info("turn: escuchando en el puerto " + std::to_string(turnPuertoLocal) +
                     (turnCfg.puertoUdp ? " (TCP y UDP)" : " (TCP)"));
        } else {
            log.warn("turn: no arrancó (" + e + "): los invitados de afuera no van a tener video", -1);
            turnSrv.reset();
        }
    }

    // --- Supervisor de pinggy (acceso remoto; docs/planes/PINGGY.md) ----------
    // Precedencia para enlaces y QR remotos (setJoinUrlProvider y origenSeguro):
    // pinggy si esta listo, si no el dominio de Cloudflare, si no WireGuard. Los
    // dos tuneles pueden estar prendidos a la vez: cada uno llega a su escucha.
    std::unique_ptr<PinggySupervisor> pinggy;
    if (cfg.pinggyActivo) {
        PinggySupervisor::Cfg pc;
        pc.exe = cfg.pinggyPath;
        pc.logPath = cfg.pinggyLog;
        pc.region = cfg.pinggyRegion;
        pc.host = cfg.pinggyHost;
        pc.listaBlanca = cfg.pinggyListaBlanca;
        pc.puertoTunel = cfg.pinggyPuertoTunel;
        pc.turn = turnActivo;
        pc.turnLocal = turnPuertoLocal;
        pc.turnTcpPuerto = cfg.pinggyTurnTcpPuerto;
        pc.turnUdp = cfg.pinggyTurnUdp;
        pc.turnUdpPuerto = cfg.pinggyTurnUdpPuerto;
        // ba-pinggy pone PROXY v1 en TODOS los forwardings (es global): con el
        // TURN prendido va sin (el TURN no lo entiende) y la IP real sale del
        // X-Forwarded-For que agrega pinggy (--xff). Igual es todo externo.
        pc.haproxy = turnActivo ? "no" : "v1";
        // Nunca por el tunel: la señalizacion comun (confia en 127.0.0.1 como red
        // local), su HTTPS, la API de control, el MCP y las metricas.
        pc.prohibidos.insert({cfg.port, cfg.httpsPort, cfg.adminPort, cfg.metricsPort});
        pc.token = envOrEmpty(kEnvPinggyToken);
        if (pc.token.empty()) {
            std::string t;
            if (ba::security::CredentialStore::bundle().read("pinggy-token", t)) {
                while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
                pc.token = t;
            }
        }
        pinggy = std::make_unique<PinggySupervisor>(std::move(pc), log);
    }
    const auto urlPinggy = [&]() { return pinggy ? pinggy->urlPublica() : std::string(); };

    // GET /api/ice: los iceServers para los navegadores. Los extremos públicos del
    // TURN cambian si pinggy reconecta: se arman en cada pedido, no al arrancar.
    server.setIce([&](bool externo) {
        ba::gateway::turn::ExtremosIce x;
        if (pinggy && turnSrv) {
            const PinggySupervisor::Estado st = pinggy->estado();
            const auto partir = [](const std::string& hp, std::string& host, uint16_t& puerto) {
                const auto dos = hp.rfind(':');
                if (dos == std::string::npos) return false;
                host = hp.substr(0, dos);
                puerto = static_cast<uint16_t>(std::atoi(hp.c_str() + dos + 1));
                return !host.empty() && puerto != 0;
            };
            std::string host;
            uint16_t puerto = 0;
            if (partir(st.turnPublico, host, puerto)) {
                if (st.turnTipo == "tlstcp") {
                    x.hostPublico = host; // turns:host:puerto (pinggy termina el TLS)
                    x.puertoTls = puerto;
                } else {
                    x.tcpPublico = st.turnPublico; // turn:host:puerto?transport=tcp
                }
            }
            x.udpPublico = st.turnUdp;
        }
        // Desde la red local: el TURN por la IP de la caja (para el P2P con los de afuera).
        if (turnSrv && !externo && turnCfg.direccionRelay != "127.0.0.1") {
            x.hostLan = turnCfg.direccionRelay;
            x.puertoLanTcp = turnCfg.puertoTcp;
            x.puertoLanUdp = turnCfg.puertoUdp;
        }
        return ba::gateway::turn::respuestaIce(turnCfg, turnSrv && turnSrv->activo(), x, externo);
    });

    server.setRemotoStatus([&]() {
        nlohmann::json pg = pinggy ? pinggy->estadoJson()
                                   : nlohmann::json{{"estado", "apagado"},
                                                    {"urls", nlohmann::json::array()},
                                                    {"urlPublica", ""},
                                                    {"error", ""},
                                                    {"turn", {{"publico", ""}, {"tipo", ""}, {"udp", ""}}}};
        pg["activo"] = cfg.pinggyActivo; // lo configurado (el estado dice si anda)
        pg["puertoTunel"] = server.tunnelPort();
        pg["turnServidor"] = turnSrv ? turnSrv->estado() : nlohmann::json{{"activo", false}};
        return nlohmann::json{
            {"pinggy", pg},
            {"cloudflare",
             {{"activo", cfg.tunnel && !cfg.tunnelToken.empty()},
              {"hostname", cfg.tunnelHostname}}},
            // Que URL usan los enlaces y el QR remotos.
            {"urlRemota", !urlPinggy().empty()             ? urlPinggy()
                          : !cfg.tunnelHostname.empty() ? "https://" + cfg.tunnelHostname
                                                        : std::string()},
            {"precedencia", nlohmann::json::array({"pinggy", "cloudflare", "wireguard"})}};
    });

    server.setJoinUrlProvider([&](const std::string& target) -> nlohmann::json {
        // La sala va en el fragmento (#room=): la app del invitado la precarga
        // y el fragmento nunca viaja al servidor. El PIN NO va en el QR.
        const std::string frag = "#room=" + cfg.room;
        if (target == "remote") {
            // pinggy primero si esta listo (ver la precedencia arriba).
            if (const std::string pg = urlPinggy(); !pg.empty()) return {{"url", pg + "/" + frag}};
            if (!cfg.tunnelHostname.empty()) return {{"url", "https://" + cfg.tunnelHostname + "/" + frag}};
            if (wg && !publicHost.empty())
                return {{"url", (tls ? "https://" : "http://") + publicHost + "/" + frag}};
            if (pinggy)
                return {{"error", "remote"},
                        {"message", "El tunel de pinggy todavia no esta listo (" + pinggy->estado().estado +
                                        "). Mira Configuracion > Acceso remoto."}};
            return {
                {"error", "remote"},
                {"message",
                 "Falta el acceso remoto: pinggy (Configuracion > Acceso remoto) o el dominio del tunel de "
                 "Cloudflare (Configuracion > Gateway > Dominio publico)."}};
        }
        if (cfg.bindAddress == "127.0.0.1") {
            return {{"error", "local"},
                    {"message", "El gateway solo escucha en esta maquina: elegi 'Red local' o una IP en "
                                "Configuracion > Gateway > Acceso."}};
        }
        if (hostLocal.empty()) {
            return {{"error", "local"},
                    {"message", "No encontre ninguna IP de red en esta maquina: elegila o escribila en "
                                "Configuracion > Gateway > Direccion para el QR."}};
        }
        const std::string& ip = hostLocal;
        if (tls) return {{"url", "https://" + ip + ":" + std::to_string(tls->port()) + "/" + frag}};
        return {{"url", "http://" + ip + ":" + std::to_string(cfg.port) + "/" + frag},
                {"warning", "Sin HTTPS el navegador no da camara ni microfono por la red local."}};
    });

    server.setNetworkStatus([&]() {
        nlohmann::json st;
        nlohmann::json urls = nlohmann::json::array();
        urls.push_back(guestUrl);
        if (tls) {
            urls.push_back("https://127.0.0.1:" + std::to_string(tls->port()) + "/");
            if (!hostLocal.empty() && cfg.bindAddress != "127.0.0.1")
                urls.push_back("https://" + hostLocal + ":" + std::to_string(tls->port()) + "/");
            if (cfg.bindAddress == "0.0.0.0") {
                for (const auto& ip : lanIps)
                    if (ip != hostLocal) urls.push_back("https://" + ip + ":" + std::to_string(tls->port()) + "/");
            }
        }
        if (!publicHost.empty() && wg) urls.push_back((tls ? "https://" : "http://") + publicHost + "/");
        if (const std::string pg = urlPinggy(); !pg.empty()) urls.push_back(pg + "/");
        st["urls"] = urls;
        st["bestUrl"] = bestUrl;
        st["lan"] = lanIps;
        st["urlHost"] = hostLocal;
        // El origen con certificado DE VERDAD, si lo hay. Importa para los
        // televisores: un receptor de Chromecast es una pagina HTTPS y el
        // navegador le prohibe cargar contenido http (mixed content), asi que
        // nuestro HTTPS autofirmado tampoco le sirve. Sin tunel, no hay forma
        // de mostrarle una pagina NUESTRA, y conviene decirlo que descubrirlo
        // con un televisor colgado en "cargando".
        // pinggy (si esta listo) antes que Cloudflare: los dos tienen certificado de verdad.
        if (const std::string pg = urlPinggy(); !pg.empty())
            st["origenSeguro"] = pg;
        else
            st["origenSeguro"] =
                cfg.tunnelHostname.empty() ? nlohmann::json() : nlohmann::json("https://" + cfg.tunnelHostname);
        st["bindAddress"] = cfg.bindAddress;
        st["rtcPorts"] = {cfg.rtcPortMin, cfg.rtcPortMax};
        st["https"] = tls ? nlohmann::json{{"enabled", true},
                                           {"port", tls->port()},
                                           {"selfSigned", tls->selfSigned()},
                                           {"fingerprint", tls->fingerprint()}}
                          : nlohmann::json{{"enabled", false}};
        nlohmann::json w{{"enabled", cfg.wgEnabled},    {"publicKey", wgPublicKey},
                         {"address", cfg.wgAddress},    {"endpoint", cfg.wgEndpoint},
                         {"publicAddress", publicHost}, {"publicIp", publicIp}};
        if (wg) {
            const auto s = wg->stats();
            w["up"] = s.up;
            w["peer"] = s.endpoint;
            w["rxBytes"] = s.rxBytes;
            w["txBytes"] = s.txBytes;
            w["handshakes"] = s.handshakes;
            w["lastHandshakeMs"] = s.lastHandshakeMs;
        } else {
            w["up"] = false;
        }
        st["wireguard"] = w;
        return st;
    });

    // --- Supervisor de cloudflared (tunel).
    // Token del tunel: por entorno (BA_TUNNEL_TOKEN). Por linea de comandos
    // queda visible en la lista de procesos, igual que el PIN.
    if (!cfg.tunnelToken.empty()) {
        log.warn("tunel: --tunnel-token es visible en la lista de procesos; preferir " +
                     std::string(kEnvTunnelToken),
                 -1);
    } else {
        cfg.tunnelToken = envOrEmpty(kEnvTunnelToken);
    }
    CloudflaredSupervisor cloudflared(cfg.cloudflaredPath, cfg.tunnelToken, cfg.metricsPort,
                                      cfg.cloudflaredLog, log);

    // --- Arranque de los componentes (degradacion elegante).
    if (!server.start()) {
        log.error("senalizacion no arranco; el gateway sigue sin tunel web", -1);
    }
    std::filesystem::path adminFile;
    if (server.adminPort() != 0) {
        const auto dir = ba::platform::stateDir(); // dentro del bundle portable
        if (!dir.empty()) {
            adminFile = dir / "gateway-admin.json";
            const nlohmann::json info{{"port", server.adminPort()},
                                      {"token", adminToken},
                                      {"pid", static_cast<long long>(pid)},
                                      {"guestPort", cfg.port}};
            if (!writeAdminFile(adminFile, info.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) {
                log.warn("control: no se pudo publicar " + adminFile.string(), -1);
                adminFile.clear();
            }
        }
    }
    pump.start();
    if (pinggy) {
        if (server.tunnelPort() == 0)
            log.error("pinggy: sin la escucha del tunel (127.0.0.1:" + std::to_string(cfg.pinggyPuertoTunel) +
                          "); no se lanza",
                      -1);
        else
            pinggy->start();
    }
    if (cfg.tunnel) {
        cloudflared.start();
    } else {
        log.info("tunel: desactivado por configuracion (solo acceso local)");
    }

    // --- Bucle de vida: beat(0) cada 250 ms hasta duracion o senal.
    const auto start = std::chrono::steady_clock::now();
    const auto beatPeriod = std::chrono::milliseconds(250);
    health.beat(kHealthSlot); // primer latido inmediato

    // --- Canal de control (spec §4.1): el launcher pide el cierre ordenado
    // por aca. Sin canal (nombre ocupado) se sigue: queda la señal del SO.
    ba::ipc::ControlService control;
    {
        std::string err;
        if (control.start("control.gateway", &err)) {
            log.info("control: escuchando en 'control.gateway'");
        } else {
            log.warn("control: sin canal de control (" + err + ")", -1);
        }
    }

    while (!g_stop.load(std::memory_order_relaxed)) {
        control.poll([&](const ba::ipc::Message& m) {
            if (m.type == ba::ipc::Type::Shutdown) {
                log.info("control: shutdown pedido por el launcher");
                g_stop.store(true);
            }
        });
        if (cfg.durationMs > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (elapsed >= cfg.durationMs) break;
        }
        std::this_thread::sleep_for(beatPeriod);
        health.beat(kHealthSlot);
        hub.reapViewers(); // espectadores de las vistas web que se fueron sin avisar
        room.tick(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count());
        // Uso del propio proceso: es lo que se satura cuando hay muchos
        // invitados (cada uno es un video que hay que decodificar). Va en el
        // snapshot para que el operador lo vea en la pagina Sala.
        room.setUsage(processUsage(&hub));
    }

    log.info("parando: deteniendo componentes...");

    // --- Shutdown ordenado: senalizacion → pump → cloudflared → Terminated.
#if BA_HAVE_LWIP
    if (ipstack) ipstack->stop();
#endif
    if (wg) wg->stop();
    if (tls) tls->stop();
    if (pinggy) pinggy->stop(); // antes que la escucha del tunel
    if (turnSrv) turnSrv->stop();
    server.stop();
    if (!adminFile.empty()) {
        std::error_code ec;
        std::filesystem::remove(adminFile, ec);
    }
    pump.stop();
    hub.closeAll();
    cloudflared.stop();
    health.setState(kHealthSlot, ProcessState::Terminated);
    log.info("cierre ordenado: BA.Health slot 0 = Terminated; exito");
    return 0;
}
