// ============================================================================
// src/app/arranque/OperateMode.cpp — ver OperateMode.hpp
// ============================================================================

#include "arranque/AjustesMotor.hpp"
#include "ajustes/Ajustes.hpp"
#include "evento/FotosConfig.hpp"
#include "arranque/OperateMode.hpp"
#include "arranque/RutasPortables.hpp"


#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "arranque/AppRuntime.hpp"
#include "audio/ObjetivoLoudness.hpp"
#include "emision/RetornosInvitados.hpp"
#include "emision/SalidasConfig.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "arranque/ArranqueAviso.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/ChatOverlaySource.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/TiposBasicos.hpp"
#include "video/FuentesCaptura.hpp"
#include "emision/FuentePdf.hpp"
#include "escenas/FuenteGrilla.hpp"
#include "emision/FuenteQr.hpp"
#include "graficos/GraficoSource.hpp"
#include "emision/InputSpecs.hpp"
#include "arranque/InicioConElSistema.hpp"
#include "video/ElementosGst.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "Reloj.hpp" // motor: iniciarGStreamer
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/GstEntorno.hpp"
#include "platform/LocalSocket.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"
#include "security/CredentialStore.hpp"

#include <nlohmann/json.hpp>

#include "ipc/LugaresSala.hpp"
#include "platform/Hilo.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::app {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kHealthSlot = 1; // A (spec §4.4; B es 0)
constexpr std::uint32_t kProcessVersion = 2;
constexpr int kTickMs = 100;

std::atomic<bool> g_stop{false};

extern "C" void onStopSignal(int) { g_stop.store(true); }

std::uint32_t currentPid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string argValue(int argc, char** argv, const char* key, const std::string& def) {
    const std::string prefix = std::string(key) + "=";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind(prefix, 0) == 0) {
            return a.substr(prefix.size());
        }
    }
    return def;
}

bool hasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

void applyCli(AppConfig& c, int argc, char** argv, const fs::path& root) {
    const std::string enc = argValue(argc, argv, "--encoder", "");
    if (!enc.empty()) {
        c.encoder.videoEncoder = enc;
    }
    const std::string br = argValue(argc, argv, "--bitrate", "");
    if (!br.empty()) {
        c.encoder.videoBitrateKbps = std::atoi(br.c_str());
    }
    const std::string res = argValue(argc, argv, "--resolution", "");
    const std::size_t x = res.find('x');
    if (x != std::string::npos) {
        c.video.baseWidth = c.video.outputWidth = std::atoi(res.substr(0, x).c_str());
        c.video.baseHeight = c.video.outputHeight = std::atoi(res.substr(x + 1).c_str());
    }
    const std::string fps = argValue(argc, argv, "--fps", "");
    if (!fps.empty()) {
        fpsDeTexto(fps, c.video.fpsNum, c.video.fpsDen);
    }
    if (hasFlag(argc, argv, "--record")) {
        c.record = true;
    }
    if (hasFlag(argc, argv, "--no-record")) {
        c.record = false;
    }
    const std::string recDir = argValue(argc, argv, "--rec-dir", "");
    if (!recDir.empty()) {
        const fs::path p = recDir;
        c.rec.directory = (p.is_absolute() ? p : root / p).string();
    }
    const std::string server = argValue(argc, argv, "--stream-server", "");
    if (!server.empty()) {
        c.stream = true;
        c.streamCfg.server = server;
    }
    const std::string guests = argValue(argc, argv, "--guests", "");
    if (!guests.empty()) {
        c.guests = std::atoi(guests.c_str());
    }
    c.durationSec = std::atoi(argValue(argc, argv, "--duration", "0").c_str());
    c.statusEverySec = std::atoi(argValue(argc, argv, "--status-every", "5").c_str());
    c.restore = hasFlag(argc, argv, "--restore");
    c.modoPrueba = hasFlag(argc, argv, "--test") || hasFlag(argc, argv, "--debug");
    c.safeMode = hasFlag(argc, argv, "--safe-mode");
    if (c.safeMode) {
        // Modo seguro (circuit breaker del launcher): lo minimo que funciona.
        //
        // NO se le tocan las escenas a la configuracion. Antes se les vaciaban
        // las fuentes ACA, en memoria, para arrancar sin ellas; el problema es
        // que despues cualquier guardado escribia ESA configuracion vacia
        // encima del app.json del operador y le borraba el trabajo de verdad.
        // Paso: doce escenas con setenta y ocho fuentes quedaron en cero, y se
        // recuperaron de la copia .antes-del-modo-seguro de milagro.
        //
        // Ahora la configuracion queda INTACTA y lo unico que cambia es que al
        // armar las escenas no se crean las fuentes (ver `startEngine`). Asi el
        // modo seguro es lo que tiene que ser: una forma de arrancar, no una
        // forma de perder datos.
        c.encoder.videoEncoder = "x264";
    }
    if (c.guests < 0) {
        c.guests = 0;
    }
    if (c.guests > ba::ipc::kMaxInvitados) {
        c.guests = ba::ipc::kMaxInvitados;
    }
}

void printStatus(const BroadcastEngine& eng) {
    const EngineStatus s = eng.status();
    char fps[32];
    std::snprintf(fps, sizeof fps, "%.1f", s.fps);
    const std::string si = ba::i18n::traducir("arranque", "si"), no = ba::i18n::traducir("arranque", "no");
    std::printf("ba-app: %s\n",
                ba::i18n::llenar(ba::i18n::traducir("arranque", "fps=%1 enc=%2 grabando=%3 transmitiendo=%4 perdidos=%5/%6 enviado=%7KB"),
                                 {fps, s.videoEncoder.empty() ? "-" : s.videoEncoder, s.recording ? si : no,
                                  s.streaming ? si : no, std::to_string(s.droppedFrames), std::to_string(s.totalFrames),
                                  std::to_string(static_cast<unsigned long long>(s.bytesSent / 1024))})
                    .c_str());
    std::fflush(stdout);
}

} // namespace

// ============================================================================
// AppRuntime
// ============================================================================

void AppRuntime::requestStop() { g_stop.store(true); }

bool AppRuntime::stopRequested() { return g_stop.load(); }

int AppRuntime::observar(std::function<void(Cambio)> fn) {
    observadores_.emplace_back(++proximoObservador_, std::move(fn));
    return proximoObservador_;
}

void AppRuntime::dejarDeObservar(int id) {
    observadores_.erase(std::remove_if(observadores_.begin(), observadores_.end(),
                                       [id](const auto& o) { return o.first == id; }),
                        observadores_.end());
}

void AppRuntime::avisar(Cambio c) const {
    // Sobre una copia: quien escucha puede darse de baja mientras se le avisa.
    const auto lista = observadores_;
    for (const auto& [id, fn] : lista)
        if (fn) fn(c);
}

std::uint32_t AppRuntime::pid() const { return currentPid(); }

void AppRuntime::configure(int argc, char** argv) {
    // Binarios de la raiz del bundle; datos del disco real (BA_DATA_ROOT).
    bin_ = ba::platform::bundleRoot();
    // La configuración activa (Configuraciones/<nombre>/): todo lo de la app sale de acá;
    // lo de la máquina (Modelos/, Herramientas/, Logs/, Runtime/, Credenciales/) se pide
    // aparte con ba::platform::dataRoot().
    root_ = ba::platform::configRoot();
    if (bin_.empty()) {
        bin_ = fs::current_path();
    }
    if (root_.empty()) {
        root_ = bin_;
    }
    // Volver a la configuración de un evento (histórico, H6): se preparó con
    // la app andando y se aplica acá, antes de que nadie lea Config/.
    {
        const auto ahora = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const nlohmann::json r = ba::evento::FotosConfig::aplicarPendiente(root_ / "Config", ahora);
        if (!r.is_null())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "historico", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("historico", "se volvió a la configuración de «%1»: %2 (lo de antes en Config/.restaurar/%3)"),
                                                        {r.value("desde", std::string()),
                                                         r.value("aplicados", nlohmann::json::array()).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                                         r.value("respaldo", std::string())}));
    }
    const fs::path cfgPath = argValue(argc, argv, "--config", appConfigPath(root_).string());
    cfg_ = loadAppConfig(cfgPath, root_);
    ba::ajustes::cargar(cfg_.ajustes); // los avanzados de Configuración, lo antes posible
    gateway_ = loadGatewaySettings(gatewayConfigPath(root_));
    launcher_ = loadLauncherSettings(launcherConfigPath(root_));
    applyCli(cfg_, argc, argv, root_);
}

SourceSpec* AppRuntime::findSpec(const std::string& scene, const std::string& name) {
    SceneConfig* sc = findScene(cfg_, scene);
    if (sc == nullptr) {
        return nullptr;
    }
    for (auto& s : sc->sources) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void AppRuntime::addAudioInputs() {
    // Microfonos y lineas del equipo: canal global, como el audio de los
    // invitados (suenan en todas las escenas, no son items de escena).
    for (const auto& spec : cfg_.audioInputs) {
        if (!engine_.addGlobalAudio(spec))
            std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "entrada de audio '%1' omitida: %2"),
                                                         {spec.name, engine_.lastError()})
                                            .c_str());
    }
}

bool AppRuntime::addAudioInput(const SourceSpec& pedida) {
    SourceSpec spec = pedida;
    if (engine_.sourceTypeExists(inputs::kMedioType)) inputs::migrarMedio(spec);
    if (!engine_.addGlobalAudio(spec)) {
        lastError_ = engine_.lastError();
        return false;
    }
    const auto it = std::find_if(cfg_.audioInputs.begin(), cfg_.audioInputs.end(),
                                 [&](const SourceSpec& s) { return s.name == spec.name; });
    if (it == cfg_.audioInputs.end())
        cfg_.audioInputs.push_back(spec);
    else
        *it = spec;
    avisar(Cambio::Audio);
    return true;
}

bool AppRuntime::removeAudioInput(const std::string& name) {
    const bool ok = engine_.removeGlobalAudio(name);
    cfg_.audioInputs.erase(std::remove_if(cfg_.audioInputs.begin(), cfg_.audioInputs.end(),
                                          [&](const SourceSpec& s) { return s.name == name; }),
                           cfg_.audioInputs.end());
    avisar(Cambio::Audio);
    return ok;
}

void AppRuntime::ajustarInvitados(int n) {
    const int antes = cfg_.guests;
    cfg_.guests = n;
    gateway_.guests = n;
    if (cfg_.guestAudio)
        for (int slot = antes + 1; slot <= n; ++slot)
            if (!engine_.addGuestAudio(slot))
                std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "invitado %1 (audio) omitido: %2"),
                                                             {std::to_string(slot), engine_.lastError()})
                                                .c_str());
    // Y el video de los lugares nuevos (la cámara y la pantalla de cada uno): sin
    // esto, con el cupo en 4 se veían solo 2 invitados en Entradas. Ocultas en la
    // primera escena, como las del arranque: el operador las pone donde quiera.
    if (cfg_.guestVideo && !cfg_.scenes.empty()) {
        const auto existe = [this](const std::string& nombre) {
            for (const auto& sc : cfg_.scenes)
                for (const auto& s : sc.sources)
                    if (s.name == nombre) return true;
            return false;
        };
        for (int slot = antes + 1; slot <= n; ++slot)
            for (const bool share : {false, true}) {
                SourceSpec spec;
                spec.id = "ba_guest_video";
                spec.name = "Invitado " + std::to_string(slot) + (share ? " — pantalla" : "");
                if (existe(spec.name)) continue;
                spec.settingsJson =
                    "{\"slot\":" + std::to_string(slot) + ",\"channel\":\"" + (share ? "share" : "guest") + "\"}";
                spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
                spec.visible = false;
                if (!addSource(cfg_.scenes.front().name, spec))
                    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "«%1» omitido: %2"),
                                                                 {spec.name, lastError_})
                                                    .c_str());
            }
    }
    // Al bajar el cupo (la sala ya lo aceptó: no hay nadie en esos lugares), las
    // entradas de video de los lugares que se van salen de todas las escenas: si no,
    // en Video seguían «Invitado 4…6» sin nadie. El audio queda (en silencio: lleva
    // atado su retorno mix-minus). Si el cupo vuelve a subir, se crean de nuevo.
    if (n < antes) {
        std::vector<std::pair<std::string, std::string>> sacar; // escena, fuente
        for (const auto& sc : cfg_.scenes)
            for (const auto& s : sc.sources) {
                if (!inputs::esVideoInvitado(s.id, s.settingsJson)) continue;
                const nlohmann::json j = nlohmann::json::parse(s.settingsJson.empty() ? "{}" : s.settingsJson, nullptr, false);
                const std::string canal = j.is_object() ? j.value("channel", std::string("guest")) : "guest";
                if (j.is_object() && j.value("slot", 0) > n && (canal == "guest" || canal == "share"))
                    sacar.emplace_back(sc.name, s.name);
            }
        for (const auto& [escena, fuente] : sacar) removeSource(escena, fuente);
    }
    save();
}

void AppRuntime::addGuests() {
    // Audio de invitados: global (canal propio), siempre en la mezcla.
    for (int slot = 1; slot <= cfg_.guests; ++slot) {
        if (cfg_.guestAudio && !engine_.addGuestAudio(slot)) {
            std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "invitado %1 (audio) omitido: %2"),
                                                         {std::to_string(slot), engine_.lastError()})
                                            .c_str());
        }
    }
    // Video: si ninguna escena tiene invitados (config nueva o vieja), se
    // agregan a la primera escena en fila; despues el operador los mueve.
    bool any = false;
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            any = any || inputs::esVideoInvitado(s.id, s.settingsJson);
        }
    }
    if (!any && cfg_.guestVideo && !cfg_.scenes.empty()) {
        for (int slot = 1; slot <= cfg_.guests; ++slot) {
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot);
            spec.settingsJson = "{\"slot\":" + std::to_string(slot) + "}";
            spec.rect = guestRect(slot, cfg_.guests);
            cfg_.scenes.front().sources.push_back(spec);
        }
    }
    // R3 (docs/funciones/SESION-REMOTA.md): el integrante remoto con la palabra, como una
    // entrada más. El audio es global (suena en cualquier escena, con su rack en
    // Audio); el video va oculto a pantalla completa en la primera escena, como la
    // pantalla de un invitado: el operador lo muestra (o lo usa en otra escena).
    // Dos canales: el que tiene la palabra y, en un debate entre remotos, el otro (canal 2).
    for (int k = 1; k <= 2; ++k) {
        const std::string sufijo = k == 1 ? "" : " " + std::to_string(k);
        const std::string slot = std::to_string(k);
        SourceSpec a;
        a.name = "Remoto con la palabra" + sufijo + " (audio)";
        if (engine_.sourceTypeExists(inputs::kMicType)) {
            a.id = inputs::kMicType; // por GStreamer (baanillo): su rack corre en la cadena
            a.settingsJson = inputs::ajustesAudioAnillo("remoto", k);
        } else {
            a.id = "ba_guest_audio";
            a.settingsJson = "{\"slot\":" + slot + ",\"channel\":\"remoto\"}";
        }
        if (!engine_.addGlobalAudio(a))
            std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "el audio de «%1» no entró: %2"),
                                                         {a.name, engine_.lastError()})
                                            .c_str());
        bool hayVideo = false, hayContenido = false;
        const std::string deSlot = "\"slot\":" + slot;
        for (const auto& sc : cfg_.scenes)
            for (const auto& s : sc.sources) {
                if (!inputs::esVideoInvitado(s.id, s.settingsJson) || s.settingsJson.find(deSlot) == std::string::npos) continue;
                hayVideo = hayVideo || s.settingsJson.find("\"remoto\"") != std::string::npos;
                hayContenido = hayContenido || s.settingsJson.find("\"remoto_contenido\"") != std::string::npos;
            }
        if (!hayVideo && !cfg_.scenes.empty()) {
            SourceSpec v;
            v.id = "ba_guest_video";
            v.name = "Remoto con la palabra" + sufijo;
            v.settingsJson = "{\"slot\":" + slot + ",\"channel\":\"remoto\"}";
            v.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            v.visible = false;
            cfg_.scenes.front().sources.push_back(v);
        }
        // Lo que comparte (su pantalla o la otra cámara del celular), oculto como la pantalla de un invitado.
        if (!hayContenido && !cfg_.scenes.empty()) {
            SourceSpec v;
            v.id = "ba_guest_video";
            v.name = "Remoto con la palabra" + sufijo + " — contenido";
            v.settingsJson = "{\"slot\":" + slot + ",\"channel\":\"remoto_contenido\"}";
            v.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            v.visible = false;
            cfg_.scenes.front().sources.push_back(v);
        }
    }
    // Pantalla o ventana que comparte cada invitado: a pantalla completa y
    // OCULTA; el operador la muestra cuando la quiere al aire.
    bool anyShare = false;
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            anyShare =
                anyShare || (inputs::esVideoInvitado(s.id, s.settingsJson) && s.settingsJson.find("share") != std::string::npos);
        }
    }
    if (!anyShare && cfg_.guestVideo && !cfg_.scenes.empty()) {
        for (int slot = 1; slot <= cfg_.guests; ++slot) {
            SourceSpec spec;
            spec.id = "ba_guest_video";
            spec.name = "Invitado " + std::to_string(slot) + " — pantalla";
            spec.settingsJson = "{\"slot\":" + std::to_string(slot) + ",\"channel\":\"share\"}";
            spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            spec.visible = false;
            cfg_.scenes.front().sources.push_back(spec);
        }
    }
    // Cada lugar con su cámara y su pantalla, aunque ya hubiera invitados: si el
    // cupo se subió (a 4) con escenas que tenían los de 2, los nuevos no se creaban
    // y en Entradas se veían solo 2. Los que faltan, ocultos en la primera escena.
    if (cfg_.guestVideo && !cfg_.scenes.empty()) {
        const auto hay = [this](int slot, const std::string& canal) {
            for (const auto& sc : cfg_.scenes)
                for (const auto& s : sc.sources) {
                    if (!inputs::esVideoInvitado(s.id, s.settingsJson)) continue;
                    const nlohmann::json j = nlohmann::json::parse(s.settingsJson.empty() ? "{}" : s.settingsJson, nullptr, false);
                    if (j.is_object() && j.value("slot", 0) == slot && j.value("channel", std::string("guest")) == canal) return true;
                }
            return false;
        };
        for (int slot = 1; slot <= cfg_.guests; ++slot)
            for (const bool share : {false, true}) {
                if (hay(slot, share ? "share" : "guest")) continue;
                SourceSpec spec;
                spec.id = "ba_guest_video";
                spec.name = "Invitado " + std::to_string(slot) + (share ? " — pantalla" : "");
                spec.settingsJson =
                    "{\"slot\":" + std::to_string(slot) + ",\"channel\":\"" + (share ? "share" : "guest") + "\"}";
                spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
                spec.visible = false;
                cfg_.scenes.front().sources.push_back(spec);
            }
    }
}

int AppRuntime::start() {
    // --- Salud: A late en su slot para el watchdog del launcher. Sin launcher
    // (desarrollo) se crea el bloque para poder correr solo.
    if (!ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_).ok) {
        ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, kHealthSlot + 1, health_);
    }
    if (health_.valid()) {
        health_.setIdentity(kHealthSlot, currentPid(), kProcessVersion);
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Running);
        health_.beat(kHealthSlot);
    }
    // GSTREAMER PRIMERO, EN OTRO HILO. La primera vez en una PC (o con el disco en
    // otra letra) rehace su lista de plugins: cada uno pasa por el escáner y uno
    // que se traba mirando el hardware cuesta 60 s. En otra PC tardó más de 2
    // minutos; con gst_init en este hilo el cartel quedaba quieto («No responde»),
    // el latido del arranque se cortaba a los 2 minutos y el launcher lo mataba;
    // la lista se guarda al final, así que cada reinicio empezaba de cero y la
    // app no arrancaba nunca (2026-10-14). Mientras GStreamer revisa, este hilo
    // late y repinta el cartel, sin tope: no puede quedar colgado para siempre,
    // GStreamer corta a cada plugin a los 60 s.
    {
        // Antes de revisar, GStreamer cuenta los plugins: el cartel dice cuántos son,
        // cuál está revisando y cuántos lleva («37 de 312»).
        const bool rehacer = ba::platform::registroPorRehacer();
        arranque::avisar(rehacer ? ba::i18n::traducir("arranque", "Contando los plugins de video…")
                                 : ba::i18n::traducir("arranque", "Iniciando GStreamer…"));
        const auto desde = std::chrono::steady_clock::now();
        ba::motor::iniciarGStreamer(nullptr, [&]() {
            if (health_.valid()) {
                std::lock_guard<std::mutex> l(healthMu_);
                health_.beat(kHealthSlot);
            }
            const long long s =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - desde).count();
            const std::string reloj = std::to_string(s) + " s";
            const std::string primera =
                rehacer ? " · " + ba::i18n::traducir("arranque", "la primera vez en esta PC tarda más") : "";
            const ba::motor::ProgresoGst pg = ba::motor::progresoGStreamer();
            arranque::Aviso a;
            if (pg.total > 0 && pg.revisados > 0) {
                const int n = std::min(pg.revisados, pg.total);
                a.que = ba::i18n::llenar(ba::i18n::traducir("arranque", "Revisando los plugins de video: %1 de %2"),
                                         {std::to_string(n), std::to_string(pg.total)});
                a.detalle = pg.actual + " · " + reloj + primera;
                a.parte = static_cast<double>(n) / pg.total;
            } else if (pg.total > 0) {
                a.que = ba::i18n::llenar(ba::i18n::traducir("arranque", "Revisando los plugins de video: son %1"),
                                         {std::to_string(pg.total)});
                a.detalle = reloj + primera;
                a.parte = 0;
            } else {
                a.que = rehacer ? ba::i18n::traducir("arranque", "Contando los plugins de video…")
                                : ba::i18n::traducir("arranque", "Iniciando GStreamer…");
                if (s >= 2) a.detalle = reloj;
            }
            arranque::seguir(a);
        });
    }

    // EL LATIDO DEL ARRANQUE: arrancar no es estar colgado. Con GStreamer ya
    // iniciado, el motor igual tarda unos segundos en armarse; sin latir, el
    // launcher lo daba por colgado a los 20 s y lo mataba en pleno arranque (pasó).
    // Late desde su hilo hasta que el latido de los cuadros toma la posta, con un
    // tope de 2 minutos: un arranque colgado de verdad se sigue viendo.
    struct LatidoArranque {
        std::atomic<bool> seguir{true};
        std::thread hilo;
        ~LatidoArranque() { parar(); }
        void parar() {
            seguir = false;
            if (hilo.joinable()) hilo.join();
        }
    } latidoArranque;
    if (health_.valid())
        latidoArranque.hilo = std::thread([this, &latidoArranque]() {
            ba::platform::nombrarHilo("latido-arranque");
            for (int i = 0; i < 240 && latidoArranque.seguir.load(); ++i) {
                {
                    std::lock_guard<std::mutex> l(healthMu_);
                    health_.beat(kHealthSlot);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });

    EnginePaths paths;
    paths.moduleBinDir = (bin_ / "obs-plugins" / "64bit").string();
    paths.moduleDataDir = (bin_ / "data" / "obs-plugins").string();
    paths.obsDataDir = (bin_ / "data" / "libobs").string();
    paths.configDir = (root_ / "Config" / "plugin_config").string();

    std::printf("ba-app: %s\n",
                ba::i18n::llenar(cfg_.safeMode ? ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4 (modo seguro)")
                                               : ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4"),
                                 {std::to_string(cfg_.video.baseWidth), std::to_string(cfg_.video.baseHeight),
                                  std::to_string(cfg_.video.fpsNum), cfg_.encoder.videoEncoder})
                    .c_str());
    // El arranque del motor, medido como el de la interfaz (StudioViewModel):
    // cada etapa de más de 150 ms queda en el registro.
    const auto relojArranque = std::chrono::steady_clock::now();
    auto ultimaMarca = relojArranque;
    const auto marca = [&ultimaMarca](const std::string& que) {
        const auto ahora = std::chrono::steady_clock::now();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(ahora - ultimaMarca).count();
        if (ms > 150)
            ba::diag::registro().info(
                ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: %1 tardó %2 ms"), {que, std::to_string(ms)}));
        ultimaMarca = ahora;
    };
    arranque::avisar(ba::i18n::traducir("arranque", "Arrancando el motor de video y audio…"));
    // El reloj del estudio (Video → Sincronía): con él arranca el motor.
    {
        std::ifstream f(root_ / "Config" / "video-reloj.json");
        const nlohmann::json r = nlohmann::json::parse(f, nullptr, false);
        if (r.is_object()) {
            engine_.ponerReloj(r.value("tipo", std::string("sistema")), r.value("servidor", std::string()),
                               r.value("puerto", 0), r.value("dominio", 0));
            if (r.value("publicar_puerto", 0) > 0) engine_.publicarReloj(r.value("publicar_puerto", 0));
        }
    }
    // Los tipos que pinta la app (antes de cargar escenas: sus valores de fábrica).
    chatoverlay::registerSourceType();
    graficos::registerSourceType();
    registrarTiposBasicos();
    video::registrarTiposCaptura();
    fuentesqr::registrar();
    fuentespdf::registrar();
    grilla::registrarTipo();
    if (!engine_.startup(paths, cfg_.video, AudioConfig{})) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no arranco: %1"), {lastError_}).c_str());
        if (health_.valid()) {
            health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
        }
        if (engine_.sinGStreamer()) {
            std::printf("ba-app: %s\n",
                        ba::i18n::traducir("arranque", "sin GStreamer en el programa (bin/64bit): el bundle está incompleto.").c_str());
            return ba::platform::exit_code::kEnvironment;
        }
        return 2;
    }
    marca(ba::i18n::traducir("arranque", "el motor"));
    aplicarAjustesMotor(cfg_); // placa sin uso, entradas, transmisión, disco (Configuración)
    health_.beat(kHealthSlot);
    started_ = true;
    // Los cambios de ajustes que piden piezas sin el motor a mano (rótulos,
    // gráficos) pasan por acá: así quedan también en la configuración.
    fuentes::ponerCambiar([this](const std::string& n, const nlohmann::json& c) { return updateSourceSettings(n, c.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)); });
    audio::ObjetivoLoudness(root_, engine_.loudness()).cargar(); // el objetivo guardado (Config/loudness.json)
    // El latido que mira los cuadros del motor (hilo propio): una traba de la
    // interfaz ya no hace que el supervisor corte el aire. Desde acá late él.
    latidoArranque.parar();
    if (health_.valid())
        latido_.iniciar(
            root_,
            [this] {
                std::lock_guard<std::mutex> l(healthMu_);
                health_.beat(kHealthSlot);
            },
            [this]() -> std::uint64_t { return engine_.cuadrosPrograma(); });

    marca(ba::i18n::traducir("arranque", "loudness y latido"));
    arranque::avisar(ba::i18n::traducir("arranque", "Preparando los lugares de los invitados…"));
    addGuests();
    marca(ba::i18n::traducir("arranque", "los invitados"));
    arranque::avisar(ba::i18n::traducir("arranque", "Abriendo los micrófonos…"));
    addAudioInputs();
    marca(ba::i18n::traducir("arranque", "las entradas de audio"));
    arranque::avisar(ba::i18n::traducir("arranque", "Armando las escenas…"));
    // Lo que oye cada invitado (mix-minus de fábrica): lo guardado en Config/retornos.json.
    emision::RetornosInvitados(*this).cargar();
    // Los volúmenes generales de los grupos, antes que las fuentes: cada una ya entra con el suyo.
    for (const auto& [grupo, g] : cfg_.gruposAudio) engine_.setGananciaGrupo(grupo, g);
    engine_.setTransition(cfg_.transition.type, cfg_.transition.durationMs);
    for (const auto& sc : cfg_.scenes) {
        arranque::avisar(ba::i18n::llenar(ba::i18n::traducir("arranque", "Armando la escena «%1»…"), {sc.name}));
        engine_.createScene(sc.name);
        // En modo seguro las escenas se crean VACIAS: si una fuente es la que
        // venia tumbando el arranque, asi no se la toca. La configuracion no
        // se modifica, o sea que al arrancar normal vuelven todas solas.
        if (cfg_.safeMode) continue;
        for (const auto& src : sc.sources) {
            // Una fuente rota (camara desenchufada, id mal escrito) no tumba
            // el proceso: se avisa y la emision sigue con el resto.
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = engine_.addSource(sc.name, src);
            const long long ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 50)
                ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: «%1» (%2, %3) tardó %4 ms"),
                                                           {src.name, sc.name, src.id, std::to_string(ms)}));
            if (!ok) {
                std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "fuente '%1' omitida: %2"),
                                                             {src.name, engine_.lastError()})
                                                .c_str());
            }
        }
    }
    marca(ba::i18n::traducir("arranque", "las escenas"));
    engine_.setCurrentScene(cfg_.currentScene);
    // Las demás salidas (Config/salidas.json), con las fuentes ya puestas: cada una
    // arranca con los volúmenes de la por omisión (su escena vuelve abajo, con al-aire.json).
    crearSalidasGuardadas();
    if (cfg_.returnFeeds && !engine_.publishReturnFeeds()) {
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "sin retorno hacia B: %1"), {engine_.lastError()}).c_str());
    }
    marca(ba::i18n::traducir("arranque", "la escena al aire y el retorno"));
    // Lobby: la voz de quien espera (monitoreo) y lo que ve del operador.
    operator_.startLobbyMonitor(gateway_.lobbySlots);
    if (!operator_.configure(engine_, cfg_.operatorVideo, cfg_.operatorAudio)) {
        std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "feed del operador incompleto (fuente inexistente)").c_str());
    }

    marca(ba::i18n::traducir("arranque", "el lobby y el operador"));
    {
        std::string candidatos;
        for (const auto& id : engine_.videoEncoderCandidates(cfg_.encoder)) candidatos += " " + id;
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "codificadores candidatos:%1"), {candidatos}).c_str());
    }

    marca(ba::i18n::traducir("arranque", "los codificadores"));
    if (cfg_.replaySegundos > 0) aplicarReplay();
    if (cfg_.record) {
        startRecording();
    }
    if (cfg_.stream) {
        startStreaming();
    }
    // VOLVER COMO ESTABA: un cierre ordenado deja al-aire.json en «nada al aire»
    // (stop()). Si dice que se transmitía o grababa, la corrida anterior terminó
    // sin cerrarse (corte de luz, cuelgue, la máquina se apagó): se retoma,
    // también en un arranque normal (no solo con --restore del launcher).
    // Con --test (pruebas: el MCP sin aprobaciones) NUNCA se arranca al aire.
    {
        // ¿Se cortó la anterior? Un archivo de antes de «cerrado» cuenta como cierre ordenado.
        const salidas::AlAire aire = salidas::leerAlAire(config::leerObjeto(ba::platform::stateDir() / "al-aire.json"));
        corteAnterior_ = !aire.cerrado;
        // La escena que estaba al aire (app.json puede tener una vieja: no se
        // guarda en cada corte). También con --test: no sale nada al aire.
        const std::string escena = aire.de(salidas::kPorOmision).escena;
        if (corteAnterior_ && !escena.empty() && escena != engine_.currentScene() && setScene(escena))
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró: vuelve la escena «%1»"),
                                                        {escena}));
        // Las demás salidas: su escena solo está ahí (no van en app.json), así que vuelve siempre.
        for (const std::string& id : engine_.salidaIds()) {
            const std::string e = aire.de(id).escena;
            if (id != salidas::kPorOmision && !e.empty() && !setScene(e, id))
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "la salida «%1» no pudo volver a «%2»: %3"),
                                                            {id, e, lastError_}));
        }
    }
    marca(ba::i18n::traducir("arranque", "las salidas"));
    arranque::avisar(ba::i18n::traducir("arranque", "Volviendo a como estaba…"));
    restoreOutputs();
    marca(ba::i18n::traducir("arranque", "volver como estaba"));
    if (cfg_.modoPrueba) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "modo prueba (--test): no se retoman las salidas (grabación, transmisión)"));
    }
    writeOnAirState();
    // Si arranca solo con el equipo y se movió la carpeta: la ruta nueva.
    inicio::revisar();
    // El tope de la latencia del estudio guardado (P0.6).
    if (cfg_.estudioTopeMs != -1) {
        engine_.ponerTopeEstudio(cfg_.estudioTopeMs);
    }
    // Que no falte ningún elemento de GStreamer sin que se sepa (P0.3).
    marca(ba::i18n::traducir("arranque", "estado y latencia"));
    video::revisarElementosGstAlArrancar();
    marca(ba::i18n::traducir("arranque", "revisar los elementos de GStreamer"));

    std::string err;
    if (!control_.start("control.app", &err)) {
        std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "sin canal de control (%1)"), {err}).c_str());
    }
    std::printf("ba-app: %s\n",
                ba::i18n::llenar(ba::i18n::traducir("arranque", "en marcha (pid %1)"), {std::to_string(currentPid())}).c_str());
    return 0;
}

void AppRuntime::tick() {
    control_.poll([&](const ba::ipc::Message& m) {
        if (m.type == ba::ipc::Type::Shutdown) {
            std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cierre pedido por el launcher").c_str());
            requestStop();
        }
    });
    if (health_.valid()) {
        std::lock_guard<std::mutex> l(healthMu_);
        health_.beat(kHealthSlot);
    }
    if (started_) {
        const EngineStatus st = engine_.status();
        latido_.uiTick(st.streaming || st.recording);
        // Hoy no hace nada (las salidas se atienden en su propio hilo, ticSalidas): una
        // transmisión caída se rearma con la misma lista de codificadores y una grabación
        // con error queda parada. Ver F1-05 y F1-06 en docs/arquitectura/04-plan-unificado.md.
        engine_.atenderEncoderCaido(!streamStopWanted_, !recordStopWanted_);
        // La escena al aire de cada salida, para volver a ella tras un corte (P0.10).
        if (firmaDelAire() != escenaEscrita_) writeOnAirState();
    }
    engine_.aplicarRecortesPendientes(); // los de cámaras que recién tienen tamaño
}

bool AppRuntime::setOperatorFeed(const std::string& videoSource, const std::string& audioSource) {
    const bool ok = operator_.configure(engine_, videoSource, audioSource);
    cfg_.operatorVideo = videoSource;
    cfg_.operatorAudio = audioSource;
    if (!ok) lastError_ = ba::i18n::traducir("arranque", "no existe la fuente elegida para el operador");
    return ok;
}

bool AppRuntime::gatewayAlive() const {
    return health_.valid() && health_.read(0).pid != 0 && !health_.isStale(0);
}

std::uint32_t AppRuntime::gatewayPid() const { return health_.valid() ? health_.read(0).pid : 0; }

void AppRuntime::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    // Cierre ordenado: nada que retomar en el proximo arranque.
    streamStopWanted_ = recordStopWanted_ = true;
    cerrando_ = true;
    writeOnAirState();
    // Las grabaciones por entrada, ANTES que el resto: cada una tiene su vista
    // de render y su archivo abierto, y si el cierre las deja vivas el archivo
    // queda sin cerrar (o sea, sin indice y a veces sin poder abrirse).
    engine_.stopAllSourceRecordings();
    engine_.stopCleanFeed();
    latido_.detener(); // antes de apagar libobs (lee sus cuadros)
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cerrando (salidas, escena, motor)").c_str());
    control_.stop();
    operator_.shutdown(); // antes de apagar libobs: suelta vista y fuentes
    engine_.shutdown();
    if (health_.valid()) {
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
    }
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cierre ordenado").c_str());
}

std::string AppRuntime::carpetaGrabacion() const {
    const fs::path ev = carpetaDelEvento_ ? carpetaDelEvento_() : fs::path();
    return ev.empty() ? cfg_.rec.directory : (ev / "Grabaciones").string();
}

std::vector<std::string> AppRuntime::carpetasDeGrabaciones() const {
    std::vector<std::string> out{carpetaGrabacion()};
    if (out.front() != cfg_.rec.directory) out.push_back(cfg_.rec.directory);
    return out;
}

bool AppRuntime::startRecording(const std::string& salida) {
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    const bool porOmision = id == salidas::kPorOmision;
    std::error_code ec;
    RecordConfig rec = cfg_.rec;
    rec.directory = carpetaGrabacion();
    // La copia en el otro disco, también por evento: <copia>/Eventos/<evento>/Grabaciones.
    if (!rec.copiaDir.empty() && carpetaDelEvento_) {
        const fs::path ev = carpetaDelEvento_();
        std::error_code e2;
        const fs::path rel = ev.empty() ? fs::path() : fs::relative(ev, root_ / "Eventos", e2);
        if (!ev.empty() && !e2 && !rel.empty() && rel.string().rfind("..", 0) != 0)
            rec.copiaDir = (fs::path(rec.copiaDir) / "Eventos" / rel / "Grabaciones").string();
        fs::create_directories(rec.copiaDir, e2);
    }
    fs::create_directories(rec.directory, ec);
    const EngineStatus antes = engine_.status();
    if (!engine_.startRecording(rec, cfg_.encoder, id)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("grabacion", "la grabacion no arranco: %1"), {lastError_}).c_str());
        return false;
    }
    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("grabacion", "grabando en %1"), {rec.directory}).c_str());
    // El integrado del loudness arranca con la sesión (si no había nada saliendo de esa salida).
    if (porOmision) {
        if (!antes.streaming && !antes.recording) engine_.loudness().reiniciar();
        recordStopWanted_ = false;
    } else {
        if (!engine_.haySalidaEn(id)) engine_.loudness(id).reiniciar();
        grabandoOtras_.insert(id);
    }
    writeOnAirState();
    avisar(Cambio::Salidas);
    return true;
}

bool AppRuntime::startSourceRecording(const std::string& source) {
    std::error_code ec;
    // Las individuales van a su propia carpeta: en Recordings/ ya estan las del
    // programa, y mezclar veinte archivos de entradas con las del evento hace
    // que despues nadie encuentre nada.
    const std::string dir = carpetaGrabacion() + "/entradas";
    fs::create_directories(dir, ec);
    if (!engine_.startSourceRecording(source, dir, cfg_.encoder)) {
        lastError_ = engine_.lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               ba::i18n::llenar(ba::i18n::traducir("grabacion", "grabando la entrada \"%1\" aparte"), {source}));
    avisar(Cambio::Salidas);
    return true;
}

bool AppRuntime::stopSourceRecording(const std::string& source) {
    if (!engine_.stopSourceRecording(source)) {
        lastError_ = engine_.lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               ba::i18n::llenar(ba::i18n::traducir("grabacion", "para la grabacion aparte de \"%1\""), {source}));
    avisar(Cambio::Salidas);
    return true;
}

void AppRuntime::stopAllSourceRecordings() {
    engine_.stopAllSourceRecordings();
    avisar(Cambio::Salidas);
}

void AppRuntime::stopRecording(const std::string& salida) {
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) return;
    if (id == salidas::kPorOmision)
        recordStopWanted_ = true;
    else
        grabandoOtras_.erase(id);
    writeOnAirState();
    engine_.stopRecording(id);
    avisar(Cambio::Salidas);
}

namespace {
// Ultima salida al aire (servidor + clave), cifrada en Credenciales/: evita
// tipear la clave cada vez y permite retomar la emision tras una caida.
constexpr const char* kStreamCredential = "stream-last";

bool readLastStream(std::string& server, std::string& key) {
    std::string raw;
    if (!ba::security::CredentialStore::bundle().read(kStreamCredential, raw)) {
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (!j.is_object()) {
        return false;
    }
    server = j.value("server", "");
    key = j.value("key", "");
    return !key.empty();
}
} // namespace

bool AppRuntime::streamKeySaved() const {
    if (keySaved_ < 0) { // descifrar cuesta: se consulta una vez
        std::string server;
        std::string key;
        keySaved_ = readLastStream(server, key) ? 1 : 0;
    }
    return keySaved_ == 1;
}

std::string AppRuntime::firmaDelAire() const {
    std::string f;
    for (const std::string& id : engine_.salidaIds()) f += id + '\n' + engine_.currentScene(id) + '\n';
    return f;
}

void AppRuntime::writeOnAirState() const {
    escenaEscrita_ = firmaDelAire();
    // Cada salida con su escena y lo que sale; la por omisión además en los campos de
    // siempre (salidas::aJson), que es lo que lee un arranque de antes de las salidas.
    salidas::AlAire a;
    a.cerrado = cerrando_;
    for (const std::string& id : engine_.salidaIds()) {
        const bool po = id == salidas::kPorOmision;
        a.salidas[id] = {engine_.currentScene(id), po && !streamStopWanted_,
                         po ? !recordStopWanted_ : grabandoOtras_.count(id) > 0};
    }
    const nlohmann::json j = salidas::aJson(a);
    // Temporal + rename: un corte a mitad no deja el archivo vacío.
    const std::filesystem::path dest = ba::platform::stateDir() / "al-aire.json";
    const std::filesystem::path tmp = dest.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
}

void AppRuntime::restoreOutputs() {
    const nlohmann::json j = config::leerObjeto(ba::platform::stateDir() / "al-aire.json");
    if (j.empty()) {
        return;
    }
    if (cfg_.modoPrueba) return; // la escena sí; las salidas, nunca con --test
    // Las grabaciones de las demás salidas (sus transmisiones son destinos: no se retoman, como siempre).
    const salidas::AlAire aire = salidas::leerAlAire(j);
    for (const auto& [id, s] : aire.salidas) {
        if (id == salidas::kPorOmision || !s.grabando || !engine_.existeSalida(id)) continue;
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró: se retoma la grabación de la salida «%1»"), {id}));
        startRecording(id);
    }
    if (j.value("recording", false) && !engine_.status().recording) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la grabación"));
        startRecording();
    }
    if (j.value("streaming", false) && !engine_.status().streaming) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la transmisión"));
        if (!startStreaming())
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la transmisión no se pudo retomar: %1"), {lastError_}));
    }
}

std::string AppRuntime::grabarAparte(const std::string& fuente) {
    const nlohmann::json d = discoGrabacion();
    const double horas = d.value("horas", -1.0);
    if (horas >= 0 && horas < kDiscoMinimoAparteH)
        return ba::i18n::traducir("grabacion", "Queda disco para menos de media hora: liberá espacio antes de sumar grabaciones aparte.");
    if (static_cast<int>(engine_.sourceRecordings().size()) >= BroadcastEngine::kMaxIndividuales)
        return ba::i18n::llenar(ba::i18n::traducir("grabacion", "Ya se graban %1 aparte (el tope): pará alguna."),
                                {std::to_string(BroadcastEngine::kMaxIndividuales)});
    if (!startSourceRecording(fuente)) return lastError_;
    return {};
}

bool AppRuntime::salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                   const std::string& salida) {
    if (!startStreaming(server, key, cambiarEnCaliente, salida)) return false;
    if (engine_.ultimoInicio() != BroadcastEngine::Inicio::YaIgual && alVivoManual_) {
        const std::string srv = server.empty() ? cfg_.streamCfg.server : server;
        if (!srv.empty()) alVivoManual_(srv);
    }
    return true;
}

bool AppRuntime::startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                const std::string& salida) {
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    // Otra salida: su transmisión principal va a lo que se pida (la clave de la
    // por omisión y la última guardada son de la por omisión: no se reusan, que
    // la misma clave dos veces pisa la transmisión). No se guarda la clave.
    if (id != salidas::kPorOmision) {
        if (server.empty() || key.empty()) {
            lastError_ = ba::i18n::traducir("salidas", "para otra salida, pasá el servidor y la clave (o usá un destino "
                                                       "guardado con esa salida)");
            return false;
        }
        StreamConfig sc = cfg_.streamCfg;
        sc.server = server;
        sc.key = key;
        const bool salia = engine_.haySalidaEn(id);
        if (!engine_.startStreaming(sc, cfg_.encoder, cambiarEnCaliente, id)) {
            lastError_ = engine_.lastError();
            return false;
        }
        if (!salia) engine_.loudness(id).reiniciar();
        writeOnAirState();
        avisar(Cambio::Salidas);
        return true;
    }
    StreamConfig sc = cfg_.streamCfg;
    if (!server.empty()) {
        sc.server = server;
    }
    if (!key.empty()) {
        sc.key = key;
    } else {
        const char* envKey = std::getenv(cfg_.streamKeyEnv.c_str());
        sc.key = envKey != nullptr ? envKey : "";
        if (sc.key.empty()) {
            // Sin clave tipeada ni en el entorno: la de la ultima salida al aire.
            std::string lastServer;
            if (readLastStream(lastServer, sc.key) && server.empty() && !lastServer.empty()) {
                sc.server = lastServer;
            }
        }
    }
    if (sc.server.empty()) {
        lastError_ = ba::i18n::traducir("salidas", "no hay servidor de transmision configurado");
        return false;
    }
    const EngineStatus antes = engine_.status();
    if (!engine_.startStreaming(sc, cfg_.encoder, cambiarEnCaliente)) {
        lastError_ = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("salidas", "la transmision no arranco: %1"), {lastError_}).c_str());
        return false;
    }
    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("salidas", "transmitiendo a %1"), {sc.server}).c_str());
    if (!antes.streaming && !antes.recording) engine_.loudness().reiniciar();
    streamStopWanted_ = false;
    if (!sc.key.empty()) {
        ba::security::CredentialStore::bundle().write(
            kStreamCredential, nlohmann::json{{"server", sc.server}, {"key", sc.key}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        keySaved_ = 1;
    }
    writeOnAirState();
    avisar(Cambio::Salidas);
    return true;
}

void AppRuntime::stopStreaming(const std::string& salida) {
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) return;
    if (id == salidas::kPorOmision) streamStopWanted_ = true;
    writeOnAirState();
    engine_.stopStreaming(id);
    avisar(Cambio::Salidas);
}

namespace {
nlohmann::json leerDestinos(const std::filesystem::path& root) {
    std::ifstream f(root / "Config" / "destinos.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    return j.is_array() ? j : nlohmann::json::array();
}
void escribirDestinos(const std::filesystem::path& root, const nlohmann::json& d) {
    std::error_code ec;
    std::filesystem::create_directories(root / "Config", ec);
    std::ofstream o(root / "Config" / "destinos.json");
    o << d.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}
} // namespace

nlohmann::json AppRuntime::destinos() const {
    const nlohmann::json est = engine_.extrasEstado();
    nlohmann::json out = nlohmann::json::array();
    for (nlohmann::json d : leerDestinos(root_)) {
        const std::string id = d.value("id", std::string());
        d["salida"] = salidas::salidaDeDestino(d);
        d["tiene_clave"] = ba::security::CredentialStore::bundle().exists("stream-" + id);
        const nlohmann::json e = est.value(id, nlohmann::json::object());
        d["activo"] = e.value("activo", false);
        d["reconectando"] = e.value("reconectando", false);
        d["perdidos"] = e.value("perdidos", 0);
        d["cuadros"] = e.value("cuadros", 0);
        d["bytes"] = e.value("bytes", 0);
        out.push_back(d);
    }
    return out;
}

std::string AppRuntime::guardarDestino(const nlohmann::json& d, std::string* idOut) {
    const std::string url = d.value("url", std::string());
    nlohmann::json lista = leerDestinos(root_);
    std::string id = d.value("id", std::string());
    // La salida de la que toma el programa (sin campo = la por omisión): tiene que existir.
    if (d.contains("salida") && !d.value("salida", std::string()).empty() && !engine_.existeSalida(d.value("salida", std::string())))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {d.value("salida", std::string())});
    nlohmann::json* existente = nullptr;
    for (auto& x : lista)
        if (!id.empty() && x.value("id", std::string()) == id) existente = &x;
    if (existente == nullptr) {
        if (url.rfind("rtmp://", 0) != 0 && url.rfind("rtmps://", 0) != 0 && url.rfind("srt://", 0) != 0 &&
            url.rfind("rist://", 0) != 0)
            return ba::i18n::traducir("salidas", "`url`: rtmp://, rtmps://, srt:// o rist://");
        if (id.empty()) {
            int n = 1;
            const auto usado = [&](const std::string& c) {
                for (const auto& x : lista)
                    if (x.value("id", std::string()) == c) return true;
                return false;
            };
            while (usado("destino-" + std::to_string(n))) ++n;
            id = "destino-" + std::to_string(n);
        }
        lista.push_back({{"id", id}, {"nombre", d.value("nombre", id)}, {"url", url}, {"respaldo", d.value("respaldo", false)}});
        existente = &lista.back();
        if (d.contains("salida")) (*existente)["salida"] = d["salida"];
    } else {
        for (const char* k : {"nombre", "url", "respaldo", "salida"})
            if (d.contains(k)) (*existente)[k] = d[k];
    }
    // SRT: el modo (caller/listener/rendezvous) y la latencia van en la URL, que es
    // como los lee el muxer (ffmpeg: latency en microsegundos). La passphrase es la clave.
    if (d.contains("modo") || d.contains("latencia_ms")) {
        const std::string actual = (*existente).value("url", std::string());
        if (actual.rfind("srt://", 0) != 0) return ba::i18n::traducir("salidas", "`modo` y `latencia_ms` son solo para srt://");
        const auto q = actual.find('?');
        std::string u = actual.substr(0, q);
        std::vector<std::pair<std::string, std::string>> params;
        if (q != std::string::npos) {
            std::stringstream ss(actual.substr(q + 1));
            for (std::string kv; std::getline(ss, kv, '&');)
                if (!kv.empty()) {
                    const auto e = kv.find('=');
                    params.emplace_back(kv.substr(0, e), e == std::string::npos ? "" : kv.substr(e + 1));
                }
        }
        const auto poner = [&params](const std::string& k, const std::string& v) {
            for (auto& [pk, pv] : params)
                if (pk == k) return void(pv = v);
            params.emplace_back(k, v);
        };
        if (d.contains("modo")) {
            const std::string m = d.value("modo", std::string());
            if (m != "caller" && m != "listener" && m != "rendezvous") return ba::i18n::traducir("salidas", "`modo`: caller, listener o rendezvous");
            poner("mode", m);
        }
        if (d.contains("latencia_ms")) {
            const int ms = d.value("latencia_ms", 0);
            if (ms < 20 || ms > 8000) return ba::i18n::traducir("salidas", "`latencia_ms`: entre 20 y 8000");
            poner("latency", std::to_string(ms * 1000));
        }
        for (std::size_t i = 0; i < params.size(); ++i)
            u += (i == 0 ? "?" : "&") + params[i].first + (params[i].second.empty() ? "" : "=" + params[i].second);
        (*existente)["url"] = u;
    }
    if (d.contains("clave") && d["clave"].is_string() && !d["clave"].get<std::string>().empty())
        ba::security::CredentialStore::bundle().write("stream-" + id, d["clave"].get<std::string>());
    escribirDestinos(root_, lista);
    if (idOut != nullptr) *idOut = id;
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::aplicarReplay() {
    const int s = cfg_.replaySegundos;
    if (!engine_.setReplay(s, cfg_.rec.directory + "/repeticiones", cfg_.encoder))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: %1"), {engine_.lastError()});
    return s > 0 ? ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: guarda los últimos %1 s."), {std::to_string(s)})
                 : ba::i18n::traducir("salidas", "Repetición apagada.");
}

std::string AppRuntime::ponerReplay(int segundos, const std::string& salida) {
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
    segundos = std::clamp(segundos, 0, 600);
    if (id == salidas::kPorOmision) {
        cfg_.replaySegundos = segundos;
        save();
        const std::string nota = aplicarReplay();
        return segundos > 0 && !engine_.replayEstado().value("activa", false) ? nota : std::string();
    }
    if (!engine_.setReplay(segundos, cfg_.rec.directory + "/repeticiones", cfg_.encoder, id))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "Repetición: %1"), {engine_.lastError()});
    avisar(Cambio::Salidas);
    return {};
}

void AppRuntime::detenerSenalLimpia(const std::string& salida) {
    engine_.stopCleanFeed(salida);
    avisar(Cambio::Salidas);
}

std::string AppRuntime::iniciarSenalLimpia(const std::string& destino, const std::string& salida) {
    std::string url, clave, dir;
    if (!destino.empty()) {
        for (const auto& d : leerDestinos(root_))
            if (d.value("id", std::string()) == destino) url = d.value("url", std::string());
        if (url.empty())
            return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1» (stream_destinos los lista)"), {destino});
        ba::security::CredentialStore::bundle().read("stream-" + destino, clave);
    } else {
        std::error_code ec;
        dir = carpetaGrabacion() + "/limpia";
        fs::create_directories(dir, ec);
    }
    if (!engine_.startCleanFeed(url, clave, dir, cfg_.encoder, salida)) return engine_.lastError();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               destino.empty() ? ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia grabando en %1"), {dir})
                                               : ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia a %1"), {destino}));
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::borrarDestino(const std::string& id) {
    nlohmann::json lista = leerDestinos(root_), nueva = nlohmann::json::array();
    for (const auto& x : lista)
        if (x.value("id", std::string()) != id) nueva.push_back(x);
    if (nueva.size() == lista.size()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1»"), {id});
    engine_.stopExtra(id);
    ba::security::CredentialStore::bundle().remove("stream-" + id);
    escribirDestinos(root_, nueva);
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::iniciarDestino(const std::string& id) {
    std::string errores;
    bool alguno = false;
    for (const auto& d : leerDestinos(root_)) {
        const std::string did = d.value("id", std::string());
        if (id != "todos" && did != id) continue;
        alguno = true;
        std::string clave;
        ba::security::CredentialStore::bundle().read("stream-" + did, clave);
        if (!engine_.startExtra(did, d.value("url", std::string()), clave, cfg_.encoder, salidas::salidaDeDestino(d)))
            errores += (errores.empty() ? "" : "; ") + did + ": " + engine_.lastError();
    }
    if (!alguno) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1» (stream_destinos los lista)"), {id});
    // Aunque alguno haya fallado, otros pudieron arrancar: la lista cambia igual.
    avisar(Cambio::Salidas);
    return errores;
}

nlohmann::json AppRuntime::discoGrabacion() const {
    std::error_code ec;
    fs::create_directories(cfg_.rec.directory, ec);
    const auto info = fs::space(cfg_.rec.directory, ec);
    const double gb = ec ? -1.0 : static_cast<double>(info.available) / 1e9;
    const int kbps = cfg_.encoder.videoBitrateKbps + cfg_.encoder.audioBitrateKbps;
    const double horas = gb >= 0 && kbps > 0 ? gb / (kbps * 1.05 * 3600.0 / 8.0 / 1e6) : -1.0;
    return {{"libre_gb", gb}, {"horas", horas}, {"kbps", kbps}, {"carpeta", cfg_.rec.directory}};
}

void AppRuntime::detenerDestino(const std::string& id) {
    if (id == "todos")
        engine_.stopExtras();
    else
        engine_.stopExtra(id);
    avisar(Cambio::Salidas);
}

// --- Escenas y fuentes ----------------------------------------------------------

bool AppRuntime::addScene(const std::string& name) {
    if (!engine_.createScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.scenes.push_back(SceneConfig{name, {}});
    avisar(Cambio::Escenas);
    return true;
}

bool AppRuntime::copyScene(const std::string& from, const std::string& to) {
    // `lastError_` es pegajoso: guarda lo ultimo que fallo, de cuando sea. Como
    // aca se lo usa DESPUES de haber salido bien —para contar que una fuente no
    // se pudo recrear— hay que limpiarlo al entrar, o la copia informa como
    // propio un error de otra operacion anterior.
    lastError_.clear();
    const SceneConfig* origen = nullptr;
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == from) origen = &s;
    if (origen == nullptr) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {from});
        return false;
    }
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == to) {
            lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "ya hay una escena llamada \"%1\""), {to});
            return false;
        }
    if (!engine_.createScene(to)) {
        lastError_ = engine_.lastError();
        return false;
    }
    // Se copia la CONFIGURACION, no las fuentes del motor: una fuente de
    // libobs pertenece a su escena, y compartirlas haria que mover algo en la
    // copia moviera el original. Recrearlas desde la especificacion da dos
    // escenas de verdad independientes, que es lo que uno espera al copiar.
    SceneConfig copia = *origen;
    copia.name = to;
    copia.protegida = false; // la copia se puede tocar: para eso se copio
    cfg_.scenes.push_back(copia);
    for (const SourceSpec& spec : origen->sources) {
        SourceSpec s = spec;
        if (!addSource(to, s)) {
            // Lo que no se pudo recrear se dice, pero no se deshace todo: una
            // escena copiada a medias se arregla a mano; una que desaparecio
            // sin explicacion, no.
            lastError_ = ba::i18n::llenar(
                ba::i18n::traducir("escenas", "la escena se copio, pero la fuente \"%1\" no se pudo recrear: %2"),
                {spec.name, lastError_});
        }
    }
    avisar(Cambio::Escenas);
    return true;
}

bool AppRuntime::setSceneProtected(const std::string& name, bool protegida) {
    for (SceneConfig& s : cfg_.scenes)
        if (s.name == name) {
            s.protegida = protegida;
            avisar(Cambio::Escenas);
            return true;
        }
    lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {name});
    return false;
}

bool AppRuntime::sceneProtected(const std::string& name) const {
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == name) return s.protegida;
    return false;
}

bool AppRuntime::removeScene(const std::string& name) {
    if (sceneProtected(name)) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {name});
        return false;
    }
    if (!engine_.removeScene(name)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.scenes.erase(std::remove_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                                     [&](const SceneConfig& s) { return s.name == name; }),
                      cfg_.scenes.end());
    cfg_.currentScene = engine_.currentScene();
    avisar(Cambio::Escenas);
    return true;
}

bool AppRuntime::renameScene(const std::string& from, const std::string& to) {
    if (sceneProtected(from)) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {from});
        return false;
    }
    if (!engine_.renameScene(from, to)) {
        lastError_ = engine_.lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, from)) {
        sc->name = to;
    }
    cfg_.currentScene = engine_.currentScene();
    avisar(Cambio::Escenas);
    return true;
}

std::string AppRuntime::sceneFolder(const std::string& name) const {
    const SceneConfig* sc = findScene(const_cast<AppConfig&>(cfg_), name);
    return sc != nullptr ? sc->folder : std::string();
}

bool AppRuntime::setSceneFolder(const std::string& name, const std::string& folder) {
    SceneConfig* sc = findScene(cfg_, name);
    if (sc == nullptr) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
        return false;
    }
    sc->folder = folder;
    avisar(Cambio::Escenas);
    return true;
}

std::vector<std::string> AppRuntime::sceneFolders() const {
    std::vector<std::string> out;
    for (const auto& sc : cfg_.scenes)
        if (!sc.folder.empty() && std::find(out.begin(), out.end(), sc.folder) == out.end())
            out.push_back(sc.folder);
    return out;
}

std::vector<std::string> AppRuntime::orderedSceneNames() const {
    const std::vector<std::string> live = const_cast<AppRuntime*>(this)->engine_.sceneNames();
    std::vector<std::string> out;
    for (const auto& sc : cfg_.scenes)
        if (std::find(live.begin(), live.end(), sc.name) != live.end()) out.push_back(sc.name);
    for (const auto& n : live)
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    return out;
}

bool AppRuntime::moveScene(const std::string& name, const std::string& before) {
    if (name == before) return true;
    const auto it = std::find_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                                 [&](const SceneConfig& s) { return s.name == name; });
    if (it == cfg_.scenes.end()) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
        return false;
    }
    SceneConfig moved = *it;
    cfg_.scenes.erase(it);
    auto dst = cfg_.scenes.end();
    if (!before.empty()) {
        dst = std::find_if(cfg_.scenes.begin(), cfg_.scenes.end(),
                           [&](const SceneConfig& s) { return s.name == before; });
        if (dst != cfg_.scenes.end()) moved.folder = dst->folder; // cae en su misma carpeta
    }
    cfg_.scenes.insert(dst, std::move(moved));
    avisar(Cambio::Escenas);
    return true;
}

bool AppRuntime::setScene(const std::string& name, const std::string& salida) {
    return anotarAire(engine_.setCurrentScene(name, salida), name, salida);
}

bool AppRuntime::setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    return anotarAire(engine_.setCurrentSceneCon(name, tipo, ms, salida), name, salida);
}

bool AppRuntime::anotarAire(bool ok, const std::string& name, const std::string& salida) {
    if (!ok) {
        lastError_ = engine_.lastError();
        return false;
    }
    // La de la por omisión va también en app.json (la de las demás, solo en al-aire.json).
    if (engine_.idDeSalida(salida) == salidas::kPorOmision) cfg_.currentScene = name;
    avisar(Cambio::Aire);
    return true;
}

// --- Las salidas del aparato ----------------------------------------------------

void AppRuntime::crearSalidasGuardadas() {
    for (const auto& s : salidas::leer(config::leerObjeto(archivoSalidas())).salidas)
        if (!engine_.existeSalida(s.id) && !engine_.crearSalida(s.id, s.nombre))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "salidas", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("salidas", "la salida «%1» no arrancó: %2"),
                                                        {s.id, engine_.lastError()}));
}

nlohmann::json AppRuntime::salidas() const {
    const salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : l.salidas)
        arr.push_back({{"id", s.id},
                       {"nombre", s.nombre},
                       {"color", s.color},
                       {"orden", s.orden},
                       {"por_omision", s.id == salidas::kPorOmision},
                       {"andando", engine_.existeSalida(s.id)},
                       {"escena", engine_.currentScene(s.id)}});
    return {{"tope", l.tope}, {"salidas", arr}};
}

std::string AppRuntime::crearSalida(const std::string& id, const std::string& nombre, const std::string& color) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::agregar(l, {id, nombre, color, 0}); !e.empty()) return e;
    // Que la máquina dé abasto lo dice sistema_recursos: el tope es el límite que puso el operador.
    if (!engine_.crearSalida(id, l.buscar(id)->nombre)) return engine_.lastError();
    if (!config::guardar(archivoSalidas(), salidas::aJson(l)))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "salidas", 0,
                                   ba::i18n::traducir("salidas", "no se pudo guardar Config/salidas.json"));
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida nueva: «%1»"), {id}));
    writeOnAirState();
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::editarSalida(const std::string& id, const nlohmann::json& cambios) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::editar(l, id, cambios); !e.empty()) return e;
    config::guardar(archivoSalidas(), salidas::aJson(l));
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::borrarSalida(const std::string& id) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::quitar(l, id); !e.empty()) return e;
    // Lo que sale de ella (transmisiones, grabación, repetición, limpia) se para con ella.
    if (engine_.existeSalida(id) && !engine_.borrarSalida(id)) return engine_.lastError();
    grabandoOtras_.erase(id);
    config::guardar(archivoSalidas(), salidas::aJson(l));
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida borrada: «%1»"), {id}));
    writeOnAirState();
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::setTopeSalidas(int tope) {
    if (tope < 1 || tope > salidas::kTopeMaximo)
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "el tope va de 1 a %1"), {std::to_string(salidas::kTopeMaximo)});
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (tope < static_cast<int>(l.salidas.size()))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "ya hay %1 salidas: borrá alguna antes de bajar el tope"),
                                {std::to_string(l.salidas.size())});
    l.tope = tope;
    config::guardar(archivoSalidas(), salidas::aJson(l));
    avisar(Cambio::Salidas);
    return {};
}

bool AppRuntime::setTransition(const std::string& type, int durationMs) {
    if (!engine_.setTransition(type, durationMs)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.transition = TransitionConfig{type, durationMs};
    avisar(Cambio::Aire);
    return true;
}

namespace {
/// Los ajustes de una fuente con las rutas relativas a los datos resueltas (RutasPortables).
std::string conRutasDeAhora(const std::string& ajustes) {
    const nlohmann::json j = nlohmann::json::parse(ajustes.empty() ? "{}" : ajustes, nullptr, false);
    return j.is_object() ? rutasAbsolutas(j).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : ajustes;
}
} // namespace

bool AppRuntime::addSource(const std::string& scene, const SourceSpec& pedida) {
    SourceSpec spec = pedida; // se guarda como la creó el motor (un archivo: ba_gst_medio)
    // Una ruta relativa a los datos («Media/x.png», también en el location= de un
    // pipeline) es de la configuración en uso: el motor la necesita entera.
    spec.settingsJson = conRutasDeAhora(spec.settingsJson);
    if (engine_.sourceTypeExists(inputs::kMedioType)) inputs::migrarMedio(spec);
    if (engine_.sourceTypeExists(inputs::kMicType)) {
        inputs::migrarVideoInvitado(spec);
        inputs::migrarNdi(spec);
    }
    if (!engine_.addSource(scene, spec)) {
        lastError_ = engine_.lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        SourceSpec stored = spec;
        stored.settingsJson = engine_.sourceSettingsJson(spec.name);
        sc->sources.push_back(stored);
    }
    avisar(Cambio::Fuentes);
    return true;
}

bool AppRuntime::removeSource(const std::string& scene, const std::string& name) {
    if (!engine_.removeSource(scene, name)) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        sc->sources.erase(std::remove_if(sc->sources.begin(), sc->sources.end(),
                                         [&](const SourceSpec& s) { return s.name == name; }),
                          sc->sources.end());
    }
    avisar(Cambio::Fuentes);
    return true;
}

bool AppRuntime::setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba,
                               float derecha, float abajo) {
    if (!engine_.setSourceCrop(scene, name, izquierda, arriba, derecha, abajo)) {
        lastError_ = ba::i18n::llenar(
            ba::i18n::traducir("escenas", "no se pudo recortar '%1' en '%2' (¿recorte más grande que la imagen?)"), {name, scene});
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->recorte[0] = izquierda;
        s->recorte[1] = arriba;
        s->recorte[2] = derecha;
        s->recorte[3] = abajo;
    }
    avisar(Cambio::Capa);
    return true;
}

bool AppRuntime::setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) {
    if (!engine_.setSourceEfectos(scene, name, efectos)) {
        lastError_ = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) s->efectos = efectos;
    avisar(Cambio::Capa);
    return true;
}

bool AppRuntime::moveSource(const std::string& scene, const std::string& name, int delta) {
    if (!engine_.moveSource(scene, name, delta)) {
        return false;
    }
    if (SceneConfig* sc = findScene(cfg_, scene)) {
        auto& v = sc->sources;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].name == name) {
                const std::size_t j = delta > 0 ? i + 1 : (i == 0 ? 0 : i - 1);
                if (j < v.size() && j != i) {
                    std::swap(v[i], v[j]);
                }
                break;
            }
        }
    }
    avisar(Cambio::Fuentes);
    return true;
}

bool AppRuntime::renameSource(const std::string& from, const std::string& to) {
    if (!engine_.renameSource(from, to)) {
        lastError_ = engine_.lastError();
        return false;
    }
    // La misma fuente puede estar en varias escenas, y entre las entradas de audio.
    for (auto& sc : cfg_.scenes)
        for (auto& s : sc.sources)
            if (s.name == from) s.name = to;
    for (auto& s : cfg_.audioInputs)
        if (s.name == from) s.name = to;
    avisar(Cambio::Fuentes);
    return true;
}

bool AppRuntime::setSourceVisible(const std::string& scene, const std::string& name, bool visible) {
    if (!engine_.setSourceVisible(scene, name, visible)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->visible = visible;
    }
    avisar(Cambio::Capa);
    return true;
}

bool AppRuntime::setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) {
    if (!engine_.setSourceRect(scene, name, rect)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->rect = rect;
    }
    avisar(Cambio::Capa);
    return true;
}

bool AppRuntime::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    if (!engine_.updateSourceSettings(name, conRutasDeAhora(settingsJson))) {
        lastError_ = engine_.lastError();
        return false;
    }
    // La misma fuente puede estar en varias escenas: se sincronizan todas.
    const std::string current = engine_.sourceSettingsJson(name);
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.settingsJson = current;
            }
        }
    }
    // Y las entradas globales (micrófonos: p. ej. sus filtros de audio).
    for (auto& s : cfg_.audioInputs)
        if (s.name == name) s.settingsJson = current;
    avisar(Cambio::Capa);
    return true;
}

bool AppRuntime::setVolume(const std::string& name, float mul, const std::string& salida) {
    if (!engine_.setSourceVolume(name, mul, salida)) {
        return false;
    }
    if (engine_.idDeSalida(salida) != BroadcastEngine::kSalidaPorOmision) {
        avisar(Cambio::Audio); // la mezcla de otra salida: es de su salida, no de la escena
        return true;
    }
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.volume = mul;
            }
        }
    }
    for (auto& s : cfg_.audioInputs)
        if (s.name == name) s.volume = mul;
    avisar(Cambio::Audio);
    return true;
}

bool AppRuntime::setGananciaGrupo(const std::string& grupo, float mul) {
    if (grupo.empty()) return false;
    const float g = std::max(0.0f, mul);
    engine_.setGananciaGrupo(grupo, g);
    if (std::abs(g - 1.0f) < 1e-6f) cfg_.gruposAudio.erase(grupo);
    else cfg_.gruposAudio[grupo] = g;
    avisar(Cambio::Audio);
    return true;
}

bool AppRuntime::setMuted(const std::string& name, bool muted, const std::string& salida) {
    if (!engine_.setSourceMuted(name, muted, salida)) {
        return false;
    }
    if (engine_.idDeSalida(salida) != BroadcastEngine::kSalidaPorOmision) {
        avisar(Cambio::Audio);
        return true;
    }
    for (auto& sc : cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.muted = muted;
            }
        }
    }
    for (auto& s : cfg_.audioInputs)
        if (s.name == name) s.muted = muted;
    avisar(Cambio::Audio);
    return true;
}

// --- Configuracion ----------------------------------------------------------------

bool AppRuntime::applyVideo(const VideoConfig& v) {
    if (!engine_.resetVideo(v)) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.video = v;
    // Las ubicaciones son fracciones del lienzo: se recalculan en pixeles.
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            engine_.setSourceRect(sc.name, s.name, s.rect);
        }
    }
    return true;
}

bool AppRuntime::applyEncoder(const EncoderConfig& e) {
    if (!engine_.resetEncoders()) {
        lastError_ = engine_.lastError();
        return false;
    }
    cfg_.encoder = e;
    return true;
}

bool AppRuntime::save() {
    std::string err;
    // EN MODO SEGURO NO SE GUARDA. Es un estado degradado y transitorio: lo
    // que haya en memoria vale para esta corrida y nada mas. Escribirlo seria
    // hacer permanente una situacion de emergencia, y ya paso una vez
    // (escenas vaciadas). La copia de seguridad se deja igual, por si alguien
    // llega hasta aca con el app.json ya dañado de antes.
    if (cfg_.safeMode) {
        if (!safeBackupDone_) {
            safeBackupDone_ = true;
            std::error_code ec;
            std::filesystem::path backup = cfg_.configPath;
            backup += ".antes-del-modo-seguro";
            if (std::filesystem::exists(cfg_.configPath, ec) && !std::filesystem::exists(backup, ec)) {
                std::filesystem::copy_file(cfg_.configPath, backup, ec);
                std::printf("ba-app: %s\n",
                            ba::i18n::llenar(ba::i18n::traducir("config", "modo seguro: copia de la configuracion en %1"),
                                             {backup.string()})
                                .c_str());
            }
        }
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "config", 0,
                                   ba::i18n::traducir("config", "modo seguro: NO se guarda la configuracion. Lo que cambies "
                                                                "ahora vale solo hasta que se cierre."));
        return true;
    }
    gateway_.guests = cfg_.guests; // B lee la cantidad de invitados de A
    if (!saveAppConfig(cfg_.configPath, cfg_, root_, &err) ||
        !saveGatewaySettings(gatewayConfigPath(root_), gateway_, &err) ||
        !saveLauncherSettings(launcherConfigPath(root_), launcher_, &err)) {
        lastError_ = err;
        return false;
    }
    return true;
}

namespace {
/// El pedido de reinicio a B (bloquea hasta ~4 s). Sin tocar el estado de
/// AppRuntime: lo usan restartGateway y el hilo de reiniciarSalaEnElFondo.
bool pedirReinicioGateway(std::string& error) {
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

bool AppRuntime::restartGateway() {
    std::string e;
    const bool ok = pedirReinicioGateway(e);
    if (!ok) lastError_ = e;
    return ok;
}

void AppRuntime::reiniciarSalaEnElFondo(std::function<void(const std::string& error)> listo) {
    if (reiniciandoSala_.exchange(true)) {
        if (listo) listo(ba::i18n::traducir("arranque", "ya se está reiniciando la conferencia"));
        return;
    }
    if (reinicioSala_.joinable()) reinicioSala_.join(); // el anterior ya terminó
    reinicioSala_ = std::thread([this, listo = std::move(listo)]() {
        ba::platform::nombrarHilo("sala-reinicio");
        std::string e;
        pedirReinicioGateway(e);
        reiniciandoSala_ = false;
        if (listo) listo(e);
    });
}

AppRuntime::~AppRuntime() {
    if (reinicioSala_.joinable()) reinicioSala_.join();
}

CanvasRect guestRect(int slot, int count) {
    const float w = 1.0f / static_cast<float>(count > 0 ? count : 1);
    return CanvasRect{w * static_cast<float>(slot - 1), 0.62f, w, 0.36f};
}

// ============================================================================
// Modo consola
// ============================================================================

int runOperate(int argc, char** argv) {
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    AppRuntime rt;
    rt.configure(argc, argv);
    const int rc = rt.start();
    if (rc != 0) {
        return rc;
    }
    printStatus(rt.engine());

    const auto begin = std::chrono::steady_clock::now();
    auto lastStatus = begin;
    const AppConfig& cfg = rt.config();
    while (!AppRuntime::stopRequested()) {
        rt.tick();
        const auto now = std::chrono::steady_clock::now();
        if (cfg.durationSec > 0 && now - begin >= std::chrono::seconds(cfg.durationSec)) {
            break;
        }
        if (cfg.statusEverySec > 0 && now - lastStatus >= std::chrono::seconds(cfg.statusEverySec)) {
            printStatus(rt.engine());
            lastStatus = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }
    rt.stop();
    return 0;
}

} // namespace ba::app
