// ============================================================================
// src/app/AppSettings.hpp — Modelo de configuracion de todo el appliance
//
// Proposito:
//   Unico lugar que sabe leer y escribir la configuracion. La interfaz la
//   edita a traves de estas estructuras (nunca toca JSON) y el modo consola la
//   lee igual. Tres archivos en <datos>/Config/:
//     app.json       Proceso A: video, codificacion, grabacion, transmision,
//                    invitados, escenas y fuentes, transicion, apariencia.
//     gateway.json   Proceso B: puerto, sala, tunel, resoluciones.
//                    (B lo lee al arrancar; la interfaz lo reinicia al aplicar.)
//     launcher.json  Proceso 0: copia sombra, dialogo de recuperacion.
//   Los secretos (clave de transmision, PIN, token del tunel) NO van en estos
//   archivos: van por variable de entorno o por la boveda cifrada.
//
// Compatibilidad:
//   Un app.json viejo con "sources" en el primer nivel se lee como una unica
//   escena "Principal".
// ============================================================================

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "BroadcastEngine.hpp"

namespace ba::app {

/// Tipografia por defecto de las fuentes de texto (`text_ft2_source_v2`).
///
/// "Sans" es un alias generico de fontconfig: se resuelve en Linux, pero en
/// Windows no existe y FT2 falla ("FT2-text: Failed to load font Sans"),
/// dejando el texto invisible. "Segoe UI" viene en todo Windows 10+, que es el
/// minimo que pide el appliance.
#if defined(_WIN32)
inline constexpr const char* kDefaultFontFace = "Segoe UI";
#else
inline constexpr const char* kDefaultFontFace = "Sans";
#endif

/// Una escena y sus fuentes (en orden de abajo hacia arriba).
struct SceneConfig {
    std::string name;
    std::vector<SourceSpec> sources;
};

/// Transicion entre escenas.
struct TransitionConfig {
    std::string type = "fade"; ///< "cut", "fade", "swipe", "slide", "fade_to_color"
    int durationMs = 300;
};

/// Configuracion de A (app.json) + opciones de linea de comandos.
struct AppConfig {
    VideoConfig video;
    EncoderConfig encoder;
    bool record = false; ///< grabar al arrancar
    RecordConfig rec;
    bool stream = false; ///< transmitir al arrancar
    StreamConfig streamCfg;
    std::string streamKeyEnv = "BA_STREAM_KEY";
    int guests = 2;
    bool guestVideo = true;
    bool guestAudio = true;
    std::string guestAudioPreset = "podium"; ///< channel strip de los invitados ("" = sin)
    bool returnFeeds = true;
    /// Fuentes (por nombre) con las que el operador aparece ante el LOBBY:
    /// quien espera solo ve/oye esto. Vacio = placa de espera / sin voz.
    std::string operatorVideo;
    std::string operatorAudio;
    std::vector<SceneConfig> scenes;
    std::string currentScene;
    TransitionConfig transition;
    std::string theme = "dark"; ///< "dark" | "light" | "system"
    int previewWidth = 960;
    bool mcpEnabled = true; ///< servidor MCP (asistente de IA) en 127.0.0.1
    int mcpPort = 8790;
    /// Aprobacion humana de acciones del asistente: "invitados" (si leyo
    /// contenido de invitados/publico), "siempre" o "nunca". Solo se cambia
    /// desde la interfaz: ninguna herramienta MCP puede tocarla.
    std::string mcpApproval = "invitados";

    // Solo linea de comandos (no se guardan):
    int durationSec = 0;
    int statusEverySec = 5;
    bool safeMode = false;
    bool restore = false; // --restore: el launcher relanzo A tras una caida
    std::filesystem::path configPath;
};

/// Configuracion de B (gateway.json).
struct GatewaySettings {
    int guests = 2; ///< copia de AppConfig::guests (la fuente de verdad es A)
    std::uint16_t port = 8787;
    std::string bindAddress = "127.0.0.1"; ///< "0.0.0.0" = accesible en la red local
    std::string room = "sala1";
    bool tunnel = true;         ///< usar cloudflared si hay token
    std::string tunnelHostname; ///< dominio publico del tunel de Cloudflare
    std::uint16_t metricsPort = 9099;
    int videoW = 1280; ///< slot de video del invitado hacia A
    int videoH = 720;
    int guestVideoW = 640; ///< retorno de programa hacia el invitado
    int guestVideoH = 360;
    std::uint16_t adminPort = 8788; ///< API de control de la sala (127.0.0.1)
    bool lobby = true;              ///< el operador admite a cada invitado
    int lobbySlots = 4;             ///< del lobby, cuantos muestran su camara al operador
    int maxBackstage = 12;          ///< modo oradores: cuantos en la fila ven el programa
    int backstagePreviews = 6;      ///< de la fila, cuantos muestran su camara al operador
    int maxUploadMB = 2048;         ///< tope por archivo compartido por un invitado
    // --- Acceso directo (sin Cloudflare) --------------------------------------
    bool https = true; ///< HTTPS propio (camara/micro exigen pagina segura)
    std::uint16_t httpsPort = 8789;
    std::uint16_t rtcPortMin = 40000; ///< rango UDP de WebRTC
    std::uint16_t rtcPortMax = 40099;
    bool wgEnabled = false;        ///< tunel WireGuard hacia un servidor con IP publica
    std::string wgEndpoint;        ///< host:puerto
    std::string wgServerPublicKey; ///< base64
    std::string wgAddress = "10.99.0.2/24";
    std::string wgPublicAddress; ///< IP/dominio publico que usan los invitados
    int wgKeepalive = 25;
};

/// Configuracion del launcher (launcher.json).
struct LauncherSettings {
    bool shadowCopy = false;
    bool recoveryUi = true;
};

/// Rutas de los tres archivos bajo `dataRoot`.
std::filesystem::path appConfigPath(const std::filesystem::path& dataRoot);
std::filesystem::path gatewayConfigPath(const std::filesystem::path& dataRoot);
std::filesystem::path launcherConfigPath(const std::filesystem::path& dataRoot);

/// Configuracion de A por defecto (escena de ejemplo: fondo + titulo).
AppConfig defaultAppConfig(const std::filesystem::path& dataRoot);

/// Lee app.json; si no existe, escribe el de por defecto y lo devuelve.
AppConfig loadAppConfig(const std::filesystem::path& path, const std::filesystem::path& dataRoot);

/// Guarda app.json (escritura atomica: temporal + rename).
bool saveAppConfig(const std::filesystem::path& path, const AppConfig& cfg,
                   const std::filesystem::path& dataRoot, std::string* err = nullptr);

GatewaySettings loadGatewaySettings(const std::filesystem::path& path);
bool saveGatewaySettings(const std::filesystem::path& path, const GatewaySettings& s,
                         std::string* err = nullptr);

LauncherSettings loadLauncherSettings(const std::filesystem::path& path);
bool saveLauncherSettings(const std::filesystem::path& path, const LauncherSettings& s,
                          std::string* err = nullptr);

/// Busca una escena por nombre (nullptr si no existe).
SceneConfig* findScene(AppConfig& cfg, const std::string& name);

} // namespace ba::app
