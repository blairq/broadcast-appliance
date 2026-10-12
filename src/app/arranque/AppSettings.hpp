// ============================================================================
// src/app/arranque/AppSettings.hpp — Modelo de configuracion de todo el appliance
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

#include "emision/BroadcastEngine.hpp"

namespace ba::app {

/// Tipografia por defecto de las fuentes de texto (`text_ft2_source_v2`).
///
/// "Sans" es un alias generico de fontconfig: se resuelve en Linux, pero en
/// Windows no existe y FT2 falla ("FT2-text: Failed to load font Sans"),
/// dejando el texto invisible. "Segoe UI" viene en todo Windows 10+, que es el
/// minimo que pide el appliance. En macOS pasa lo mismo: Helvetica viene en
/// todas las versiones.
#if defined(_WIN32)
inline constexpr const char* kDefaultFontFace = "Segoe UI";
#elif defined(__APPLE__)
inline constexpr const char* kDefaultFontFace = "Helvetica";
#else
inline constexpr const char* kDefaultFontFace = "Sans";
#endif

/// Una escena y sus fuentes (en orden de abajo hacia arriba).
struct SceneConfig {
    std::string name;
    /// Carpeta para ordenar la lista ("" = sin carpeta). Solo organiza la
    /// vista: el motor no la usa para nada.
    std::string folder;
    std::vector<SourceSpec> sources;
    /// Protegida: no se borra ni se renombra, y la interfaz lo dice. Es para
    /// la escena base de la que se copian las demas —la que si alguien borra,
    /// se pierde el trabajo de todos— y para las que no se tocan en vivo.
    /// Copiarla SI se puede: de eso se trata.
    bool protegida = false;
};

/// Transicion entre escenas.
struct TransitionConfig {
    std::string type = "fade"; ///< "cut", "fade", "swipe", "slide", "fade_to_color"
    int durationMs = 300;
};

/// «Mejorar imagen» (filtro ba_mejora_imagen) en las entradas de video: lo
/// general; cada entrada puede seguirlo, forzarlo o apagarlo (Cámaras PTZ).
struct MejoraConfig {
    bool activa = false;
    int ruido = 60;   ///< 0..100
    int color = 70;   ///< 0..100
    int nitidez = 50; ///< 0..100
};

/// Configuracion de A (app.json) + opciones de linea de comandos.
struct AppConfig {
    VideoConfig video;
    MejoraConfig mejora;
    EncoderConfig encoder;
    bool record = false; ///< grabar al arrancar
    RecordConfig rec;
    /// Legalizador broadcast-safe en cada entrada (balegal): no | pal | ntsc | rango.
    std::string legalizar = "no";
    int placaReposoS = 120; ///< la placa de una entrada sin uso se desarma a los N s (motor::ponerReposoPlaca)
    // Configuración → Cámaras y entradas (motor::ponerAjustesEntradas)
    int sinSenalMs = 3000;      ///< sin cuadros tanto tiempo: placa «sin señal»
    int rearmarSinDatosS = 10;  ///< sin datos ni error tanto tiempo: se rearma
    int reintentoEntradaMaxMs = 2000; ///< espera más larga entre reintentos de una entrada que no arma
    // Configuración → Transmisión (motor::ponerReintentoTransmision)
    int reintentoTransmisionMaxS = 10;
    // Configuración → Grabación: el disco (BroadcastEngine::ponerUmbralesDisco)
    double discoApurosS = 3.0;  ///< lo grabado espera tanto para entrar al disco: «apuros»
    int discoRecuperadoS = 30;  ///< todo al día tanto tiempo: sale de «apuros»
    // Configuración → Asistente (MCP) y Modelos locales
    int mcpAprobacionMin = 10;  ///< una aprobación pendiente se rechaza sola a los N min
    int modeloContexto = 32768; ///< contexto del modelo local de chat (tokens)
    int modeloCapasGpu = 999;   ///< capas en la GPU (999 = todas; 0 = solo CPU)
    /// Los avanzados de la app (ba::ajustes; tabla en ui/AjustesAvanzados): solo los cambiados.
    nlohmann::json ajustes = nlohmann::json::object();
    int replaySegundos = 0; ///< repetición: los últimos N s en memoria (0 = apagada)
    /// Tope de la latencia del estudio (ms; -1 = automático: 250 ms). Una entrada
    /// que pide más va con su propia demora y avisa (P0.6).
    int estudioTopeMs = -1;
    bool stream = false; ///< transmitir al arrancar
    StreamConfig streamCfg;
    std::string streamKeyEnv = "BA_STREAM_KEY";
    int guests = 2;
    /// Entradas de audio del equipo (microfonos, lineas). Como el audio de los
    /// invitados, van a un canal GLOBAL: suenan en todas las escenas, asi que
    /// no pertenecen a ninguna.
    std::vector<SourceSpec> audioInputs;
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
    std::string balistica = "pico"; ///< medidores: pico | ppm (EBU IIb) | vu
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
    bool modoPrueba = false; // --test/--debug: pruebas (nunca se arranca al aire)
    std::filesystem::path configPath;
};

/// Configuracion de B (gateway.json).
struct GatewaySettings {
    int guests = 2; ///< copia de AppConfig::guests (la fuente de verdad es A)
    /// Lugares que la sala deja listos al arrancar (sus canales); el cupo cambia en caliente hasta 16 igual.
    int guestsReserva = 4;
    /// El proxy por nombre (T61): la sección "proxy" de gateway.json tal cual
    /// (activo, dominio, puertos, servicios, acme). Ver src/gateway/proxy/GestorProxy.hpp.
    nlohmann::json proxy = nlohmann::json::object();
    std::uint16_t port = 8787;
    std::string bindAddress = "127.0.0.1"; ///< "0.0.0.0" = accesible en la red local
    /// La dirección que va en el QR y en el enlace de los invitados (una IP o un nombre);
    /// vacío = automática (la IP de la interfaz elegida, o la primera de la red).
    std::string urlHost;
    std::string room = "sala1";
    bool tunnel = false;        ///< usar cloudflared (solo si hay token: sin token se lee apagado)
    std::string tunnelHostname; ///< dominio publico del tunel de Cloudflare
    std::uint16_t metricsPort = 9099;
    int programVideoW = 1280; ///< retorno del programa a las pantallas de la sala
    int programVideoH = 720;
    int vistasRemotas = 16;   ///< integrantes que miran un canal a la vez en su celular
    int sinVideoPlacaMs = 1000; ///< invitado sin video tanto tiempo: su lugar muestra la placa
    int pinIntentos = 5;        ///< PIN mal tantas veces desde una IP: bloqueo
    int pinBloqueoS = 60;
    nlohmann::json ajustes = nlohmann::json::object(); ///< los avanzados de la sala (ba::ajustes)
    int videoW = 1280; ///< slot de video del invitado hacia A
    int videoH = 720;
    int guestVideoW = 640; ///< retorno de programa hacia el invitado
    int guestVideoH = 360;
    std::uint16_t adminPort = 8788; ///< API de control de la sala (127.0.0.1)
    bool lobby = true;              ///< el operador admite a cada invitado
    bool autoIntake = false;        ///< el asistente atiende solo a los que esperan
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
    std::uint16_t wgHttpPort = 80;   ///< puertos que el servidor redirige a nuestra IP del túnel
    std::uint16_t wgHttpsPort = 443;
    // --- Pinggy (gateway.json "pinggy"; ver docs/planes/PINGGY.md) ------------
    // El token de túnel NO va acá: vive cifrado en Credenciales/ como "pinggy-token".
    bool pinggyActivo = false;
    std::string pinggyRegion = "auto";        ///< auto | us | eu | ap
    std::string pinggyDominioTipo = "azar";   ///< azar | pinggy | propio
    std::string pinggyHost;                   ///< subdominio fijo o dominio propio ("" con "azar")
    std::string pinggyCnameDestino;           ///< dominio propio: a dónde apunta el CNAME (<id>.a.pinggy.link)
    std::uint16_t pinggyPuertoTunel = 8786;   ///< escucha local propia del túnel (127.0.0.1)
    bool pinggyTurn = true;                   ///< TURN por el túnel (video de los de afuera)
    std::uint16_t pinggyTurnTcpPuerto = 0;    ///< puerto TCP fijo (Pro); 0 = el que dé pinggy
    bool pinggyTurnUdp = false;
    std::uint16_t pinggyTurnUdpPuerto = 0;
    std::string pinggyListaBlanca;            ///< IPs/redes permitidas, separadas por coma ("" = todas)
};

/// Configuracion del launcher (launcher.json).
struct LauncherSettings {
    bool shadowCopy = false;
    bool recoveryUi = true;
    // Arranque y recuperación (los lee el launcher al arrancar: Supervisor::Config)
    int cuelgueS = 5;              ///< latido quieto tanto tiempo = colgado
    int graciaArranqueS = 20;      ///< recién lanzado: todavía no se vigila
    int maxCaidas = 5;             ///< más caídas que esto en la ventana: modo seguro
    int ventanaCaidasMin = 2;
    int reiniciosSinPreguntar = 2; ///< fallas seguidas que se reinician sin preguntar
    nlohmann::json ajustes = nlohmann::json::object(); ///< los avanzados del arranque (ba::ajustes)
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

/// Si al cargar, un archivo de configuración estaba roto y se usó su generación
/// anterior: el nombre del archivo (vacío si no pasó).
std::string& configRecuperada();

} // namespace ba::app
