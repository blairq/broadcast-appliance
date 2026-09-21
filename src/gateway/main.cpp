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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib> // std::getenv (inyeccion de secretos por entorno)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

// ba_core (frozen headers).
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "security/CredentialStore.hpp"
#include "security/SecretsStore.hpp" // randomSecretHex (clave HMAC por arranque)

// Componentes del gateway.
#include "Cloudflared.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
#include "net/NetUtil.hpp"
#include "net/TlsTerminator.hpp"
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
nlohmann::json processUsage() {
    static double lastCpuSec = 0;
    static std::chrono::steady_clock::time_point lastAt{};
    double cpuSec = 0;
    double rssMb = 0;
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        const auto to100ns = [](const FILETIME& f) {
            return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
        };
        cpuSec = static_cast<double>(to100ns(k) + to100ns(u)) / 1e7;
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
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
    return nlohmann::json{{"cpuPct", std::round(std::clamp(pct, 0.0, 100.0) * 10) / 10},
                          {"memMb", std::round(rssMb)},
                          {"nucleos", std::max(1u, std::thread::hardware_concurrency())}};
}

} // namespace

int main(int argc, char** argv) {
    ba::platform::redirectUserDirs(); // portable: nada al perfil del usuario
    // Credenciales en claro de versiones anteriores -> Credenciales/ cifradas.
    ba::security::CredentialStore::bundle().importLegacy(ba::platform::dataRoot());
    Config cfg;
    // Config/gateway.json (lo escribe la interfaz de A); la linea de comandos
    // lo pisa.
    cfg.loadFile((ba::platform::dataRoot() / "Config" / "gateway.json").string());
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
    constexpr int kGlobalPinAttempts = 100;
    constexpr int kGlobalLockoutSec = 60;
    TokenAuth auth(std::move(hmacSecret), roomPin, kGlobalPinAttempts, kGlobalLockoutSec, 300);

    // --- Hub de sesiones WebRTC (1..guests).
    // Pool de sesiones: una por participante que publica (adentro, en el lobby
    // o en la fila de oradores, que ve el programa).
    WebrtcHub hub(cfg.guests + cfg.lobbySlots + cfg.maxBackstage, cfg.guestVideoW, cfg.guestVideoH,
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
    pumpCfg.operatorAudioName = toWide("BA.Audio.Operator");
    // Nombres por slot con el [0] reservado (no hay slot 0).
    for (auto* v : {&pumpCfg.guestAudioNames, &pumpCfg.guestVideoNames, &pumpCfg.shareVideoNames,
                    &pumpCfg.lobbyAudioNames, &pumpCfg.lobbyVideoNames}) {
        v->emplace_back();
    }
    for (int s = 1; s <= cfg.guests; ++s) {
        pumpCfg.guestAudioNames.push_back(toWide("BA.Audio.Guest." + std::to_string(s)));
        pumpCfg.guestVideoNames.push_back(toWide("BA.Guest." + std::to_string(s)));
        pumpCfg.shareVideoNames.push_back(toWide("BA.GuestShare." + std::to_string(s)));
    }
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
    pumpCfg.guestVideoW = cfg.guestVideoW;
    pumpCfg.guestVideoH = cfg.guestVideoH;
    MediaPump pump(hub, pumpCfg, log);

    // --- Sala (lobby, participantes, chat, archivos) -------------------------
    Room::Cfg roomCfg;
    roomCfg.name = cfg.room;
    roomCfg.maxGuests = cfg.guests;
    roomCfg.lobby = cfg.lobby;
    roomCfg.lobbySlots = cfg.lobbySlots;
    roomCfg.backstageSlots = cfg.backstagePreviews;
    Room room(roomCfg);
    const std::string guestUrl = "http://" +
                                 std::string(cfg.bindAddress == "0.0.0.0" ? "127.0.0.1" : cfg.bindAddress) +
                                 ":" + std::to_string(cfg.port) + "/";
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
    sigCfg.bindAddress = cfg.bindAddress;
    sigCfg.webroot = cfg.webroot;
    sigCfg.room = cfg.room;
    sigCfg.maxGuests = cfg.guests;
    sigCfg.adminPort = cfg.adminPort;
    sigCfg.adminWebroot = cfg.adminWebroot;
    {
        std::filesystem::path guestRoot(cfg.webroot);
        if (!guestRoot.has_filename()) guestRoot = guestRoot.parent_path(); // "Web/guest/"
        sigCfg.boardWebroot = (guestRoot.parent_path() / "board").string();
    }
    sigCfg.adminToken = adminToken;
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

    std::unique_ptr<net::TlsTerminator> tls;
    if (cfg.https) {
        net::TlsTerminator::Config tc;
        tc.bindAddress = cfg.bindAddress;
        tc.port = cfg.httpsPort;
        tc.backendPort = cfg.port;
        tc.credentialsDir = ba::platform::credentialsDir().string();
        tc.names = lanIps;
        if (!publicHost.empty()) tc.names.push_back(publicHost);
        if (!publicIp.empty() && publicIp != publicHost) tc.names.push_back(publicIp);
        tls = std::make_unique<net::TlsTerminator>(tc, logLine);
        if (!tls->start()) {
            log.warn("https: no arranco; los invitados solo tienen http (sin camara fuera de localhost)", -1);
            tls.reset();
        }
    }

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
    } else if (tls && cfg.bindAddress == "0.0.0.0" && !lanIps.empty()) {
        bestUrl = "https://" + lanIps.front() + ":" + std::to_string(tls->port()) + "/";
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

    server.setJoinUrlProvider([&](const std::string& target) -> nlohmann::json {
        // La sala va en el fragmento (#room=): la app del invitado la precarga
        // y el fragmento nunca viaja al servidor. El PIN NO va en el QR.
        const std::string frag = "#room=" + cfg.room;
        if (target == "remote") {
            if (!cfg.tunnelHostname.empty()) return {{"url", "https://" + cfg.tunnelHostname + "/" + frag}};
            if (wg && !publicHost.empty())
                return {{"url", (tls ? "https://" : "http://") + publicHost + "/" + frag}};
            return {
                {"error", "remote"},
                {"message",
                 "Falta el dominio del tunel de Cloudflare (Configuracion > Gateway > Dominio publico)."}};
        }
        if (cfg.bindAddress == "127.0.0.1" || lanIps.empty()) {
            return {{"error", "local"},
                    {"message", "El gateway solo escucha en esta maquina: elegi 'Red local' en Configuracion "
                                "> Gateway > Acceso."}};
        }
        const std::string ip = cfg.bindAddress == "0.0.0.0" ? lanIps.front() : cfg.bindAddress;
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
            if (cfg.bindAddress == "0.0.0.0") {
                for (const auto& ip : lanIps)
                    urls.push_back("https://" + ip + ":" + std::to_string(tls->port()) + "/");
            }
        }
        if (!publicHost.empty() && wg) urls.push_back((tls ? "https://" : "http://") + publicHost + "/");
        st["urls"] = urls;
        st["bestUrl"] = bestUrl;
        st["lan"] = lanIps;
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
            if (!writeAdminFile(adminFile, info.dump())) {
                log.warn("control: no se pudo publicar " + adminFile.string(), -1);
                adminFile.clear();
            }
        }
    }
    pump.start();
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
        room.setUsage(processUsage());
    }

    log.info("parando: deteniendo componentes...");

    // --- Shutdown ordenado: senalizacion → pump → cloudflared → Terminated.
#if BA_HAVE_LWIP
    if (ipstack) ipstack->stop();
#endif
    if (wg) wg->stop();
    if (tls) tls->stop();
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
