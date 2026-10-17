// ============================================================================
// src/gateway/Config.hpp — Configuracion del Proceso B (CLI + rutas)
//
// Proposito:
//   Concentra la configuracion del gateway: argumentos de linea de comandos y
//   las rutas derivadas del layout portable (spec §5.1, §5.3). El gateway es
//   headless y lo lanza el launcher (Proceso 0), que puede pasarselos por CLI;
//   si no, se usan valores por defecto coherentes con el layout.
//
//   Rutas (spec §5.3: "Todas se derivan de GetModuleFileNameW"):
//     <root>/bin/64bit/ba-gateway.exe   <- modulo
//     <root>/Web/guest/                 <- app web del invitado
//     <root>/bin/64bit/cloudflared.exe  <- tunel
//     <root>/Logs/                      <- logs
//   En Windows la raiz se deriva del path del modulo (la letra de unidad
//   cambia entre equipos). En Linux (build offline) se usa el cwd.
//
// Modelo de threading:
//   Se construye una vez en main() antes de arrancar threads y se lee en solo.
//   No hay estado mutable tras el parse, por lo que no necesita lock.
//
// Estrategia de errores:
//   No lanza. parseArgs acota los valores a rangos validos (guests 1..kMaxGuests, puertos
//   1..65535, duracion >= 0). Un argumento desconocido se ignora (tolerante).
// ============================================================================
#pragma once

#include <algorithm>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/Paths.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::gateway {

/// Rango de invitados soportado (spec §3.2, §8.2: 1 a 4 invitados).
inline constexpr int kMinGuests = 1;
// Tope de invitados en el programa. Cada uno es una conexion WebRTC que hay
// que decodificar: el techo real lo pone la maquina (ver el panel de uso de la
// sala), no este numero.
inline constexpr int kMaxGuests = 16;

/// Configuracion del gateway (CLI + rutas). Ver el header para el detalle.
struct Config {
    // --- CLI ---------------------------------------------------------------
    int guests = 2;                        // 1..kMaxGuests (default 2)
    int guestsReserve = 0;                 // lugares con canales desde el arranque (el cupo sube igual hasta kMaxInvitados); 0 = guests
    uint16_t port = 8787;                  // puerto de señalización en 127.0.0.1
    uint16_t metricsPort = 9099;           // puerto de métricas de cloudflared (loopback)
    std::string bindAddress = "127.0.0.1"; // escucha de la senalizacion
    std::string urlHost;                   // lo que va en el QR y el enlace (IP o nombre); vacio = automatico
    bool tunnel = false;                   // lanzar cloudflared (si hay token); se prende en Configuración → Cloudflare
    std::string tunnelHostname;            // dominio publico del tunel de Cloudflare (sala.midominio.com)
    std::string room = "sala1";            // sala por defecto
    // PIN de la sala. VACIO por defecto a proposito: un PIN fijo en el binario
    // es publico (el bundle es portable). Si queda vacio, main() lo toma de
    // BA_GATEWAY_PIN o genera uno aleatorio y lo loguea. A lo cambia en
    // caliente con ChangePin (spec §4.1).
    std::string pin;
    bool pinFromCli = false;  // --pin usado: se avisa (visible en `ps`/Tareas)
    std::string webroot;      // ruta a Web/guest (se rellena en derivePaths)
    std::string logsDir;      // ruta a Logs/
    std::string tunnelToken;  // token de tunel de Cloudflare (env del hijo)
    long long durationMs = 0; // 0 = correr hasta señal; >0 = duracion de prueba
    int audioRate = 48000;    // sample rate del retorno de audio
    int audioChannels = 2;    // canales del retorno de audio
    int videoW = 1280;        // resolucion del slot de video (BA.Guest.<n>)
    int videoH = 720;
    int programVideoW = 1280; // resolucion del retorno del PROGRAMA (pantallas)
    int programVideoH = 720;
    int guestVideoW = 640; // resolucion del retorno de video al invitado
    int guestVideoH = 360;
    // --- Sala / API de control ---------------------------------------------
    uint16_t adminPort = 8788; // API del operador (siempre 127.0.0.1)
    bool lobby = true;         // el operador admite a cada invitado
    int lobbySlots = 4;        // cuantos del lobby muestran su camara al operador
    int maxBackstage = 12;     // modo oradores: cuantos en la fila ven el programa
    int vistasRemotas = 16;    // sesión remota: cuántos integrantes miran a la vez un canal en su celular
    int backstagePreviews = 6; // de la fila, cuantos (los primeros) muestran su camara al operador
    int maxUploadMB = 2048;    // tope por archivo compartido
    // Configuración → Invitados / Sala (la interfaz los escribe en gateway.json).
    int sinVideoPlacaMs = 1000; // invitado sin video tanto tiempo: su lugar muestra la placa
    int pinIntentos = 5;        // PIN mal tantas veces desde una IP: se la bloquea
    int pinBloqueoS = 60;       // por cuánto
    nlohmann::json ajustes = nlohmann::json::object(); // los avanzados (ba::ajustes), de gateway.json
    // --- Acceso directo de invitados ----------------------------------------
    bool https = true;           // HTTPS propio (camara/micro exigen pagina segura)
    uint16_t httpsPort = 8789;   // en bindAddress (8443 suele estar tomado: LXD, etc.)
    /// HTTPS de los aparatos de control (botonera, control web) en todas las interfaces,
    /// cuando la sala escucha solo en esta máquina: son de la red local por naturaleza.
    /// 0 = no (entonces solo llegan si la sala se abre a la red).
    uint16_t controlLanPort = 8792; // 8790 es el MCP de la app y 8791 el login de Facebook
    /// La entrada HTTP de la puerta de la sala (televisores, Companion). 0 = no.
    uint16_t puertaHttpPort = 8793;
    uint16_t rtcPortMin = 40000; // rango UDP de WebRTC (reglas de firewall / tunel)
    uint16_t rtcPortMax = 40099;
    // WireGuard hacia un servidor con IP publica (tunel en espacio de usuario).
    bool wgEnabled = false;
    nlohmann::json proxy = nlohmann::json::object(); // T61: el proxy por nombre (ver proxy/GestorProxy.hpp)
    std::string wgEndpoint;                 // host:puerto del servidor WireGuard
    std::string wgServerPublicKey;          // base64
    std::string wgAddress = "10.99.0.2/24"; // nuestra IP en el tunel
    std::string wgPublicAddress;            // IP publica que usan los invitados (ICE/enlaces)
    int wgKeepalive = 25;
    uint16_t wgHttpPort = 80; // puertos que el servidor redirige a nuestra IP del tunel
    uint16_t wgHttpsPort = 443;
    // pinggy.io (docs/planes/PINGGY.md): túnel de ba-pinggy hacia la escucha
    // propia del túnel (puertoTunel, todo lo que entra es externo) y, con el TURN
    // de la caja, sus forwardings. El token NO va acá: BA_PINGGY_TOKEN o la
    // credencial "pinggy-token".
    bool pinggyActivo = false;
    std::string pinggyRegion = "auto"; // auto|us|eu|ap
    std::string pinggyHost;            // dominio propio o subdominio persistente (Pro)
    uint16_t pinggyPuertoTunel = 8786; // 127.0.0.1: la escucha del túnel en la señalización
    uint16_t pinggyTurnTcpPuerto = 0;  // puerto público TCP fijo del TURN (Pro); 0 = el que dé pinggy
    bool pinggyTurnUdp = false;
    bool pinggyTurn = true; // TURN por el túnel (el video de los invitados de afuera)
    uint16_t pinggyTurnUdpPuerto = 0;
    std::string pinggyListaBlanca; // IPs/CIDR separadas por coma; vacío = todas
    // ba-web (src/web): el servidor web público, adelante del túnel. pinggy le manda el
    // tráfico a él (127.0.0.1:webPuerto) y él atiende el sitio y le pasa el resto a la
    // escucha del túnel (pinggyPuertoTunel). Si no está el binario, pinggy va directo.
    bool webActivo = true;
    uint16_t webPuerto = 8785;
    int webProcesos = 0; // 0 = auto (los núcleos, hasta 4)
    // ¿ba-web atiende también el HTTPS de la red local? En Windows, de fábrica no: el
    // firewall pregunta por cada programa que escucha en la red (y permitir pide ser
    // administrador); ba-gateway ya está permitido y ba-web sería otro aviso, y si nadie lo
    // acepta, la red local deja de entrar. Ahí lo sigue atendiendo el gateway, como antes.
#ifdef _WIN32
    bool webLan = false;
#else
    bool webLan = true;
#endif
    std::string webPath; // <root>/bin/64bit/ba-web[.exe]
    std::string configDir; // <datos>/Config
    std::string sharesDir; // <datos>/Shares (archivos de los invitados)

    // --- Rutas derivadas (solo-Windows en el layout real) ------------------
    std::string appRoot;         // raiz del layout portable
    std::string cloudflaredPath; // <root>/bin/64bit/cloudflared.exe
    std::string cloudflaredLog;  // <root>/Logs/cloudflared.log
    std::string pinggyPath;      // <root>/bin/64bit/ba-pinggy[.exe]
    std::string pinggyLog;       // <root>/Logs/pinggy.log

    /// Parsea los argumentos de linea de comandos.
    ///
    /// Opcionales: --guests N, --port P, --metrics-port MP, --room R,
    /// --pin PIN, --webroot PATH, --logs DIR, --tunnel-token T,
    /// --duration-ms M, --audio-rate R, --video-w W, --video-h H,
    /// --guest-video-w W, --guest-video-h H. Desconocidos: se ignoran.
    ///
    /// :param argc: cantidad de argumentos.
    /// :param argv: argumentos.
    /// Lee Config/gateway.json (si existe). Claves: guests, port,
    /// bindAddress, room, tunnel, metricsPort, videoW/H, guestVideoW/H,
    /// adminPort, lobby, maxUploadMB.
    void loadFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) return;
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, false, true);
        if (!j.is_object()) return;
        guests = j.value("guests", guests);
        guestsReserve = j.value("guestsReserva", guestsReserve);
        if (j.contains("proxy") && j["proxy"].is_object()) proxy = j["proxy"];
        port = j.value("port", port);
        bindAddress = j.value("bindAddress", bindAddress);
        urlHost = j.value("urlHost", urlHost);
        room = j.value("room", room);
        tunnel = j.value("tunnel", tunnel);
        tunnelHostname = j.value("tunnelHostname", tunnelHostname);
        metricsPort = j.value("metricsPort", metricsPort);
        videoW = j.value("videoW", videoW);
        videoH = j.value("videoH", videoH);
        programVideoW = j.value("programVideoW", programVideoW);
        programVideoH = j.value("programVideoH", programVideoH);
        guestVideoW = j.value("guestVideoW", guestVideoW);
        guestVideoH = j.value("guestVideoH", guestVideoH);
        adminPort = j.value("adminPort", adminPort);
        lobby = j.value("lobby", lobby);
        lobbySlots = j.value("lobbySlots", lobbySlots);
        maxBackstage = j.value("maxBackstage", maxBackstage);
        vistasRemotas = j.value("vistasRemotas", vistasRemotas);
        backstagePreviews = j.value("backstagePreviews", backstagePreviews);
        https = j.value("https", https);
        httpsPort = j.value("httpsPort", httpsPort);
        controlLanPort = j.value("controlLanPort", controlLanPort);
        puertaHttpPort = j.value("puertaHttpPort", puertaHttpPort);
        rtcPortMin = j.value("rtcPortMin", rtcPortMin);
        rtcPortMax = j.value("rtcPortMax", rtcPortMax);
        if (j.contains("wireguard") && j["wireguard"].is_object()) {
            const auto& w = j["wireguard"];
            wgEnabled = w.value("enabled", wgEnabled);
            wgEndpoint = w.value("endpoint", wgEndpoint);
            wgServerPublicKey = w.value("serverPublicKey", wgServerPublicKey);
            wgAddress = w.value("address", wgAddress);
            wgPublicAddress = w.value("publicAddress", wgPublicAddress);
            wgKeepalive = w.value("keepalive", wgKeepalive);
            wgHttpPort = w.value("httpPort", wgHttpPort);
            wgHttpsPort = w.value("httpsPort", wgHttpsPort);
        }
        if (j.contains("pinggy") && j["pinggy"].is_object()) {
            const auto& pg = j["pinggy"];
            pinggyActivo = pg.value("activo", pinggyActivo);
            pinggyRegion = pg.value("region", pinggyRegion);
            pinggyHost = pg.value("host", pinggyHost);
            pinggyPuertoTunel = pg.value("puertoTunel", pinggyPuertoTunel);
            pinggyTurnTcpPuerto = pg.value("turnTcpPuerto", pinggyTurnTcpPuerto);
            pinggyTurnUdp = pg.value("turnUdp", pinggyTurnUdp);
            pinggyTurn = pg.value("turn", pinggyTurn);
            pinggyTurnUdpPuerto = pg.value("turnUdpPuerto", pinggyTurnUdpPuerto);
            pinggyListaBlanca = pg.value("listaBlanca", pinggyListaBlanca);
            if (pinggyRegion != "us" && pinggyRegion != "eu" && pinggyRegion != "ap") pinggyRegion = "auto";
            if (pinggyPuertoTunel == 0) pinggyPuertoTunel = 8786;
        }
        if (j.contains("web") && j["web"].is_object()) {
            const auto& w = j["web"];
            webActivo = w.value("activo", webActivo);
            webPuerto = w.value("puerto", webPuerto);
            webProcesos = std::clamp(w.value("procesos", webProcesos), 0, 64);
            webLan = w.value("lan", webLan);
            if (webPuerto == 0) webPuerto = 8785;
        }
        maxUploadMB = j.value("maxUploadMB", maxUploadMB);
        sinVideoPlacaMs = std::clamp(j.value("sinVideoPlacaMs", sinVideoPlacaMs), 200, 30000);
        if (j.contains("ajustes") && j["ajustes"].is_object()) ajustes = j["ajustes"];
        pinIntentos = std::clamp(j.value("pinIntentos", pinIntentos), 1, 100);
        pinBloqueoS = std::clamp(j.value("pinBloqueoS", pinBloqueoS), 5, 86400);
    }

    void parseArgs(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto nextStr = [&]() -> std::string {
                if (i + 1 >= argc) return std::string();
                return std::string(argv[++i]);
            };
            auto nextInt = [&]() -> long long {
                if (i + 1 >= argc) return 0;
                return std::strtol(argv[++i], nullptr, 10);
            };
            if (arg == "--guests") {
                guests = static_cast<int>(nextInt());
            } else if (arg == "--guests-reserve") {
                guestsReserve = static_cast<int>(nextInt());
            } else if (arg == "--port") {
                port = static_cast<uint16_t>(nextInt());
            } else if (arg == "--metrics-port") {
                metricsPort = static_cast<uint16_t>(nextInt());
            } else if (arg == "--room") {
                room = nextStr();
            } else if (arg == "--bind") {
                bindAddress = nextStr();
            } else if (arg == "--admin-port") {
                adminPort = static_cast<uint16_t>(nextInt());
            } else if (arg == "--no-lobby") {
                lobby = false;
            } else if (arg == "--no-tunnel") {
                tunnel = false;
            } else if (arg == "--pin") {
                pin = nextStr();
                pinFromCli = !pin.empty();
            } else if (arg == "--webroot") {
                webroot = nextStr();
            } else if (arg == "--logs") {
                logsDir = nextStr();
            } else if (arg == "--tunnel-token") {
                tunnelToken = nextStr();
            } else if (arg == "--duration-ms") {
                durationMs = nextInt();
            } else if (arg == "--audio-rate") {
                audioRate = static_cast<int>(nextInt());
            } else if (arg == "--video-w") {
                videoW = static_cast<int>(nextInt());
            } else if (arg == "--video-h") {
                videoH = static_cast<int>(nextInt());
            } else if (arg == "--guest-video-w") {
                guestVideoW = static_cast<int>(nextInt());
            } else if (arg == "--guest-video-h") {
                guestVideoH = static_cast<int>(nextInt());
            }
            // Desconocidos: se ignoran (el gateway es tolerante).
        }
        if (guests < kMinGuests) guests = kMinGuests;
        if (guests > kMaxGuests) guests = kMaxGuests;
        guestsReserve = std::clamp(std::max(guestsReserve, guests), guests, kMaxGuests);
        if (port == 0) port = 8787;
        if (metricsPort == 0) metricsPort = 9099;
        if (durationMs < 0) durationMs = 0;
        if (audioRate <= 0) audioRate = 48000;
        if (audioChannels <= 0) audioChannels = 2;
        if (videoW <= 0) videoW = 1280;
        if (videoH <= 0) videoH = 720;
        if (programVideoW <= 0) programVideoW = 1280;
        if (programVideoH <= 0) programVideoH = 720;
        if (guestVideoW <= 0) guestVideoW = 640;
        if (guestVideoH <= 0) guestVideoH = 360;
        if (maxUploadMB <= 0) maxUploadMB = 2048;
        if (lobbySlots < 0) lobbySlots = 0;
        if (lobbySlots > 8) lobbySlots = 8;
        if (maxBackstage < 0) maxBackstage = 0;
        if (maxBackstage > 32) maxBackstage = 32;
        vistasRemotas = std::clamp(vistasRemotas, 0, 64);
        if (backstagePreviews < 0) backstagePreviews = 0;
        if (backstagePreviews > 8) backstagePreviews = 8; // A se suscribe a BA.Backstage.1..8
        if (rtcPortMax < rtcPortMin) rtcPortMax = rtcPortMin;
        if (rtcPortMax - rtcPortMin > 150) rtcPortMax = static_cast<uint16_t>(rtcPortMin + 150);
    }

    /// Deriva las rutas del layout a partir de la ubicacion del ejecutable
    /// (`ba::platform::bundleRoot`), igual en todas las plataformas: B puede
    /// lanzarse desde cualquier directorio. Si `webroot` ya fue fijado por CLI
    /// no se sobreescribe.
    ///
    /// :returns: true si se pudo resolver la raiz; false si no (se sigue con
    ///           el directorio de trabajo como raiz).
    bool derivePaths() {
        // Binarios de la raiz del bundle (quiza la copia sombra); datos
        // (Web/, Logs/) de la raiz real que informa el launcher.
        std::filesystem::path bundle = ba::platform::bundleRoot();
        std::filesystem::path root = ba::platform::dataRoot();
        const bool ok = !bundle.empty();
        if (!ok) {
            bundle = ".";
        }
        if (root.empty()) {
            root = bundle;
        }
        const std::filesystem::path binDir = bundle / "bin" / "64bit";
#ifdef _WIN32
        const char* exe = "cloudflared.exe";
        const char* exePinggy = "ba-pinggy.exe";
        const char* exeWeb = "ba-web.exe";
#else
        const char* exe = "cloudflared";
        const char* exePinggy = "ba-pinggy";
        const char* exeWeb = "ba-web";
#endif
        appRoot = utf8(root);
        cloudflaredPath = utf8(binDir / exe);
        if (logsDir.empty()) logsDir = utf8(root / "Logs");
        cloudflaredLog = utf8(root / "Logs" / "cloudflared.log");
        pinggyPath = utf8(binDir / exePinggy);
        pinggyLog = utf8(root / "Logs" / "pinggy.log");
        webPath = utf8(binDir / exeWeb);
        // La web viene con el programa: del bundle; si no está (la copia
        // sombra solo trae bin/, obs-plugins/ y data/), de la raíz real.
        std::error_code ec;
        const std::filesystem::path web =
            std::filesystem::exists(bundle / "Web", ec) ? bundle / "Web" : root / "Web";
        if (webroot.empty()) webroot = utf8(web / "guest");
        // Lo de la configuración activa (Configuraciones/<nombre>/): su Config/ y lo que comparten los invitados.
        const std::filesystem::path conf = ba::platform::configRoot().empty() ? root : ba::platform::configRoot();
        if (sharesDir.empty()) sharesDir = utf8(conf / "Shares");
        if (configDir.empty()) configDir = utf8(conf / "Config");
        return ok;
    }

   private:
    /// Ruta a UTF-8 (en Windows `path::string()` usa la codepage ANSI y rompe
    /// rutas con acentos, que en un disco portable son esperables).
    static std::string utf8(const std::filesystem::path& p) {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    }
};

} // namespace ba::gateway
