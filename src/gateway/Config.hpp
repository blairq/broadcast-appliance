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
//   No lanza. parseArgs acota los valores a rangos validos (guests 1..4, puertos
//   1..65535, duracion >= 0). Un argumento desconocido se ignora (tolerante).
// ============================================================================
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::gateway {

/// Rango de invitados soportado (spec §3.2, §8.2: 1 a 4 invitados).
inline constexpr int kMinGuests = 1;
inline constexpr int kMaxGuests = 4;

/// Configuracion del gateway (CLI + rutas). Ver el header para el detalle.
struct Config {
    // --- CLI ---------------------------------------------------------------
    int guests = 2;              // 1..kMaxGuests (default 2)
    uint16_t port = 8787;        // puerto de señalización en 127.0.0.1
    uint16_t metricsPort = 9099; // puerto de métricas de cloudflared (loopback)
    std::string room = "sala1";  // sala por defecto
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
    int guestVideoW = 640; // resolucion del retorno de video al invitado
    int guestVideoH = 360;

    // --- Rutas derivadas (solo-Windows en el layout real) ------------------
    std::string appRoot;         // raiz del layout portable
    std::string cloudflaredPath; // <root>/bin/64bit/cloudflared.exe
    std::string cloudflaredLog;  // <root>/Logs/cloudflared.log

    /// Parsea los argumentos de linea de comandos.
    ///
    /// Opcionales: --guests N, --port P, --metrics-port MP, --room R,
    /// --pin PIN, --webroot PATH, --logs DIR, --tunnel-token T,
    /// --duration-ms M, --audio-rate R, --video-w W, --video-h H,
    /// --guest-video-w W, --guest-video-h H. Desconocidos: se ignoran.
    ///
    /// :param argc: cantidad de argumentos.
    /// :param argv: argumentos.
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
            } else if (arg == "--port") {
                port = static_cast<uint16_t>(nextInt());
            } else if (arg == "--metrics-port") {
                metricsPort = static_cast<uint16_t>(nextInt());
            } else if (arg == "--room") {
                room = nextStr();
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
        if (port == 0) port = 8787;
        if (metricsPort == 0) metricsPort = 9099;
        if (durationMs < 0) durationMs = 0;
        if (audioRate <= 0) audioRate = 48000;
        if (audioChannels <= 0) audioChannels = 2;
        if (videoW <= 0) videoW = 1280;
        if (videoH <= 0) videoH = 720;
        if (guestVideoW <= 0) guestVideoW = 640;
        if (guestVideoH <= 0) guestVideoH = 360;
    }

    /// Deriva las rutas del layout a partir del modulo (Windows) o del cwd
    /// (Linux). Si `webroot` ya fue fijado por CLI no se sobreescribe.
    ///
    /// :returns: true si se pudo resolver la raiz; false si no (el caller
    ///           decide que hacer: normalmente seguir con los defaults).
    bool derivePaths() {
#ifdef _WIN32
        wchar_t modPath[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, modPath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return false;
        std::wstring mod(modPath, n);
        // mod = <root>/bin/64bit/ba-gateway.exe
        // dir = <root>/bin/64bit ; raiz = dos niveles arriba.
        auto parentDir = [](std::wstring p) -> std::wstring {
            const size_t slash = p.find_last_of(L"\\/");
            return (slash == std::wstring::npos) ? L"" : p.substr(0, slash);
        };
        const std::wstring binDir = parentDir(mod);       // <root>/bin/64bit
        const std::wstring binParent = parentDir(binDir); // <root>/bin
        const std::wstring root = parentDir(binParent);   // <root>
        appRoot = toUtf8(root);
        cloudflaredPath = toUtf8(binDir + L"\\cloudflared.exe");
        if (logsDir.empty()) logsDir = toUtf8(root + L"\\Logs");
        cloudflaredLog = toUtf8(std::wstring(root, 0) + L"\\Logs\\cloudflared.log");
        if (webroot.empty()) webroot = toUtf8(root + L"\\Web\\guest");
#else
        // Linux / build offline: cwd como raiz.
        appRoot = ".";
        cloudflaredPath = "./cloudflared";
        if (logsDir.empty()) logsDir = "./Logs";
        cloudflaredLog = "./Logs/cloudflared.log";
        if (webroot.empty()) webroot = "./Web/guest";
#endif
        return true;
    }

   private:
    static std::string toUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
#ifdef _WIN32
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr,
                                    nullptr);
        std::string out;
        if (n > 0) {
            out.resize(static_cast<size_t>(n));
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr,
                                nullptr);
        }
        return out;
#else
        (void)w;
        return std::string();
#endif
    }
};

} // namespace ba::gateway
