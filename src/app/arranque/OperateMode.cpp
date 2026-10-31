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
#include "diag/Correlacion.hpp"
#include "diag/Registro.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18n.hpp"
#include "Motor.hpp" // motor: el retorno del invitado sin audio
#include "Reloj.hpp" // motor: iniciarGStreamer
#include "GpuPerdida.hpp" // motor: la GPU perdida (F3-09)
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/GstEntorno.hpp"
#include "platform/LocalSocket.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"
#include "platform/Volumen.hpp"
#include "persist/ColaEscritura.hpp"
#include "persist/Escritor.hpp"
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

// El slot y la versión, los de ba_core: el launcher readopta solo a una A de su misma versión.
constexpr std::size_t kHealthSlot = ba::ipc::kSlotSaludApp;
constexpr std::uint32_t kProcessVersion = ba::ipc::kVersionProceso;
constexpr int kTickMs = 100;

std::atomic<bool> g_stop{false};

/// El audio del invitado del lugar `slot`: su entrada, con su retorno (de fábrica, el programa
/// sin su voz). Sin audio de invitados (guests.audio) su voz no está en el programa, pero igual
/// necesita su anillo de retorno: el gateway nunca le manda el programa común a un invitado
/// al aire, y sin anillo le apaga el retorno (gateway/RetornoLugar.hpp).
void audioDeInvitado(BroadcastEngine& engine, bool conAudio, int slot) {
    if (!conAudio) {
        if (motor::Motor* m = engine.motor()) m->retorno(slot, "programa", "");
        return;
    }
    if (!engine.addGuestAudio(slot))
        std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "invitado %1 (audio) omitido: %2"),
                                                     {std::to_string(slot), engine.lastError()})
                                        .c_str());
}

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
    if (const char* v = std::getenv("BA_PRUEBA_RETOMA_SALIDAS")) c.retomaEnPrueba = c.modoPrueba && std::string(v) == "1";
    // El modo seguro ESCALONADO (F1-10, lo arma el launcher: fault/ModoSeguro.hpp).
    // Los niveles 1 y 2 apagan piezas (cada una pregunta delProceso()) y las
    // escenas vuelven; solo el nivel 3 (o el viejo --safe-mode) es lo de abajo.
    ba::fault::delProceso() = ba::fault::leerArgumentos(argc, argv);
    c.safeMode = ba::fault::delProceso().escenasVacias();
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

/// Las fuentes que apaga el modo seguro (F1-10): las entradas NDI y las capturas de ventana.
bool apagadaPorModoSeguro(const SourceSpec& src) {
    const ba::fault::ModoSeguro& m = ba::fault::delProceso();
    if (!m.activo()) return false;
    if (m.apagada(ba::fault::Pieza::CapturaVentana) && src.id == video::kTipoVentana) return true;
    if (m.apagada(ba::fault::Pieza::Ndi)) {
        const nlohmann::json s = nlohmann::json::parse(src.settingsJson.empty() ? "{}" : src.settingsJson, nullptr, false);
        if (inputs::tipoLogico(src.id, s.is_object() ? s : nlohmann::json::object()) == "ba_ndi_input" || src.id == "ba_ndi_input")
            return true;
    }
    return false;
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
    // Los que escuchan son del dueño (los view models): lo que cambió el plano de control
    // les llega encolado, en su hilo (F3-03).
    if (!esDueno() && encolarEnDueno_) {
        enDueno([this, c]() { avisar(c); });
        return;
    }
    // Sobre una copia: quien escucha puede darse de baja mientras se le avisa.
    const auto lista = observadores_;
    for (const auto& [id, fn] : lista)
        if (fn) fn(c);
}

std::uint32_t AppRuntime::pid() const { return currentPid(); }

std::string& AppRuntime::errorDelHilo() {
    thread_local std::string e;
    return e;
}

const std::string& AppRuntime::lastError() const { return errorDelHilo(); }

void AppRuntime::setEncolarEnDueno(std::function<void(std::function<void()>)> encolar) {
    encolarEnDueno_ = std::move(encolar);
}

bool AppRuntime::esDueno() const {
    const std::thread::id d = dueno_.load();
    return d == std::thread::id() || d == std::this_thread::get_id();
}

void AppRuntime::enDueno(std::function<void()> fn) const {
    if (esDueno() || !encolarEnDueno_) return fn();
    encolarEnDueno_(std::move(fn));
}

AppRuntime::AjustesAire AppRuntime::ajustesAire() const {
    if (!esDueno()) {
        std::lock_guard<std::mutex> l(ajustesAireMu_);
        return ajustesAire_;
    }
    AjustesAire a;
    a.rec = cfg_.rec;
    a.encoder = cfg_.encoder;
    a.stream = cfg_.streamCfg;
    a.claveEnv = cfg_.streamKeyEnv;
    a.transicionMs = cfg_.transition.durationMs;
    a.carpetaEvento = carpetaDelEvento_ ? carpetaDelEvento_() : fs::path();
    std::lock_guard<std::mutex> l(ajustesAireMu_);
    ajustesAire_ = a;
    return a;
}

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
    inputs::alMotor(spec); // se guarda con el tipo del motor (esquema 2 de app.json)
    if (!engine_.addGlobalAudio(spec)) {
        errorDelHilo() = engine_.lastError();
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
    for (int slot = antes + 1; slot <= n; ++slot) audioDeInvitado(engine_, cfg_.guestAudio, slot);
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
                SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                            "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
                if (existe(spec.name)) continue;
                spec.visible = false;
                if (!addSource(cfg_.scenes.front().name, spec))
                    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "«%1» omitido: %2"),
                                                                 {spec.name, errorDelHilo()})
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
    for (int slot = 1; slot <= cfg_.guests; ++slot) audioDeInvitado(engine_, cfg_.guestAudio, slot);
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
            SourceSpec spec = inputs::videoInvitadoSpec(slot, "guest", "Invitado " + std::to_string(slot));
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
        a.id = inputs::kMicType; // por GStreamer (baanillo): su rack corre en la cadena
        a.settingsJson = inputs::ajustesAudioAnillo("remoto", k);
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
            SourceSpec v = inputs::videoInvitadoSpec(k, "remoto", "Remoto con la palabra" + sufijo);
            v.visible = false;
            cfg_.scenes.front().sources.push_back(v);
        }
        // Lo que comparte (su pantalla o la otra cámara del celular), oculto como la pantalla de un invitado.
        if (!hayContenido && !cfg_.scenes.empty()) {
            SourceSpec v = inputs::videoInvitadoSpec(k, "remoto_contenido", "Remoto con la palabra" + sufijo + " — contenido");
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
            SourceSpec spec = inputs::videoInvitadoSpec(slot, "share", "Invitado " + std::to_string(slot) + " — pantalla");
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
                SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                            "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
                spec.visible = false;
                cfg_.scenes.front().sources.push_back(spec);
            }
    }
}

int AppRuntime::start() {
    // El que arranca es el dueño del motor (la interfaz o el bucle de la consola): toma el
    // turno al usarlo, como el plano de control (F3-03, emision/TurnoMotor.hpp).
    dueno_ = std::this_thread::get_id();
    TurnoMotor::participar("dueño");
    // Lo que dejó la corrida anterior, ANTES de que nada lo pise (--record y --stream
    // escriben el documento apenas arrancan): hasta que termine de volver como estaba,
    // en el disco sigue diciendo lo de antes (writeOnAirState espera a aireListo_).
    leerAireAnterior();
    // --- Salud: A late en su slot para el watchdog del launcher. Sin launcher
    // (desarrollo) se crea el bloque para poder correr solo.
    if (!ba::ipc::HealthBlock::open(ba::ipc::HealthBlock::kName, health_).ok) {
        ba::ipc::HealthBlock::create(ba::ipc::HealthBlock::kName, ba::ipc::kSlotsSalud, health_);
    }
    if (health_.valid()) {
        health_.setIdentity(kHealthSlot, currentPid(), kProcessVersion);
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Running);
        health_.beat(kHealthSlot);
    }
    // El arranque, medido como el de la interfaz (StudioViewModel): cada etapa de más de
    // 150 ms queda en el registro, desde acá (GStreamer incluido: la retoma tras una
    // caída tiene que estar en < 3 s, F3-10, y estas marcas dicen dónde se va).
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
    marca(ba::i18n::traducir("arranque", "GStreamer"));

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
    paths.configDir = (root_ / "Config" / "plugin_config").string();

    std::printf("ba-app: %s\n",
                ba::i18n::llenar(cfg_.safeMode ? ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4 (modo seguro)")
                                               : ba::i18n::traducir("arranque", "arrancando motor %1x%2@%3, codificador=%4"),
                                 {std::to_string(cfg_.video.baseWidth), std::to_string(cfg_.video.baseHeight),
                                  std::to_string(cfg_.video.fpsNum), cfg_.encoder.videoEncoder})
                    .c_str());
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
    marca(ba::i18n::traducir("arranque", "el reloj y los tipos de fuente"));
    if (!engine_.startup(paths, cfg_.video, AudioConfig{})) {
        errorDelHilo() = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no arranco: %1"), {errorDelHilo()}).c_str());
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
    // El objetivo guardado (Config/loudness.json), también al motor (AGC y techo).
    objetivoLoudness().cargar();
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
            // Modo seguro nivel 2 (o la caída atribuida): NDI y capturas de ventana
            // no se crean. Quedan en la configuración: vuelven con el arranque normal.
            if (apagadaPorModoSeguro(src)) {
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "modo seguro: «%1» (%2) no se crea"),
                                                            {src.name, sc.name}));
                continue;
            }
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
    // El retorno, al tamaño con que el gateway lo manda (escalado en la GPU, F2-13).
    engine_.ponerTopeRetorno(gateway_.programVideoW, gateway_.programVideoH);
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
    // VOLVER COMO ESTABA, todo acá y de una (F3-04): la escena de cada salida, el negro
    // y la placa, y lo que salía. Antes la placa volvía desde un temporizador de la
    // interfaz, un segundo y medio después y solo con interfaz.
    arranque::avisar(ba::i18n::traducir("arranque", "Volviendo a como estaba…"));
    restaurarAire();
    marca(ba::i18n::traducir("arranque", "volver como estaba"));
    if (cfg_.modoPrueba && !cfg_.retomaEnPrueba) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "modo prueba (--test): no se retoman las salidas (grabación, transmisión)"));
    }
    aireListo_ = true;
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
    ticControl();
    ticMotor();
}

void AppRuntime::ticControl() {
    control_.poll([&](const ba::ipc::Message& m) {
        if (m.type == ba::ipc::Type::Shutdown) {
            // Con la orden del launcher abierta (F4-02): la línea lleva su id.
            const ba::diag::correlacion::Ambito orden(m.correlacion);
            ba::diag::registro().info(ba::i18n::traducir("arranque", "cierre pedido por el launcher"));
            requestStop();
        }
    });
    // El documento de aire lo escribe su dueño: este hilo (ver setDuenoDelAire).
    documento_.volcar();
}

void AppRuntime::ticMotor() {
    // El latido de la INTERFAZ: este reloj corre en su hilo, así que si la interfaz se
    // traba el supervisor lo sabe (LatidoAire decide si hay que esperar o relanzar).
    if (health_.valid()) {
        std::lock_guard<std::mutex> l(healthMu_);
        health_.beat(kHealthSlot);
    }
    if (started_) {
        const EngineStatus st = engine_.status();
        latido_.uiTick(st.streaming || st.recording);
        ajustesAire(); // la copia de los ajustes de aire, para el plano de control
        // (Un codificador que se cae en el aire, una grabación o una transmisión que se
        // cortan: las atiende el hilo de salidas del motor, ver BroadcastEngine::ticSalidas.)
        // Lo que sale de cada salida (la escena, el negro y la placa), para volver tras un
        // corte (P0.10, F3-04): el documento compara y solo se escribe si cambió algo.
        writeOnAirState();
        // La GPU del video se perdió (F3-09): el motor no se rearma solo, lo rearma la app.
        if (ba::motor::gpuPerdida()) rearmarPorGpu();
        publicarSalidas(st);
    }
    engine_.aplicarRecortesPendientes(); // los de cámaras que recién tienen tamaño
}

void AppRuntime::publicarSalidas(const EngineStatus& st) {
    // Qué salidas hay, para quien las pida desde otro hilo (el MCP valida `salida` con
    // esto en el hilo de la herramienta, sin tocar el motor).
    nlohmann::json l = nlohmann::json::array();
    for (const auto& s : st.salidas) l.push_back({{"id", s.id}, {"nombre", s.nombre}, {"por_omision", s.porOmision}});
    std::lock_guard<std::mutex> g(salidasMu_);
    salidasPublicadas_ = std::move(l);
}

nlohmann::json AppRuntime::salidasPublicadas() const {
    std::lock_guard<std::mutex> g(salidasMu_);
    return salidasPublicadas_;
}

void AppRuntime::setDuenoDelAire(std::function<void()> despertar) { documento_.setDueno(std::move(despertar)); }

bool AppRuntime::setOperatorFeed(const std::string& videoSource, const std::string& audioSource) {
    const bool ok = operator_.configure(engine_, videoSource, audioSource);
    cfg_.operatorVideo = videoSource;
    cfg_.operatorAudio = audioSource;
    if (!ok) errorDelHilo() = ba::i18n::traducir("arranque", "no existe la fuente elegida para el operador");
    return ok;
}

bool AppRuntime::gatewayAlive() const {
    return health_.valid() && health_.read(0).pid != 0 && !health_.isStale(0);
}

std::uint32_t AppRuntime::gatewayPid() const { return health_.valid() ? health_.read(0).pid : 0; }

void AppRuntime::stop() {
    const TurnoMotor::Guarda turno(engine_.turno());
    if (!started_) {
        return;
    }
    started_ = false;
    // Cierre ordenado: nada que retomar en el proximo arranque (en ninguna salida). Salvo
    // en un corte de luz con poca batería (F1-11): el que cierra es el sistema que se apaga,
    // no el operador; al-aire.json queda como estaba (sin «cerrado») y al volver se retoma.
    if (!emergenciaEnergia_) {
        streamStopWanted_ = recordStopWanted_ = true;
        grabandoOtras_.clear();
        transmitiendoOtras_.clear();
        destinosAlAire_.clear();
        cerrando_ = true;
    }
    writeOnAirState();
    documento_.volcar(); // el dueño (el plano de control) puede estar ya parado
    // Las grabaciones por entrada, ANTES que el resto: cada una tiene su vista
    // de render y su archivo abierto, y si el cierre las deja vivas el archivo
    // queda sin cerrar (o sea, sin indice y a veces sin poder abrirse).
    engine_.stopAllSourceRecordings();
    engine_.stopCleanFeed();
    latido_.detener(); // antes de apagar el motor (lee sus cuadros)
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cerrando (salidas, escena, motor)").c_str());
    control_.stop();
    operator_.shutdown(); // antes de apagar el motor: suelta vista y fuentes
    engine_.shutdown();
    if (health_.valid()) {
        health_.setState(kHealthSlot, ba::ipc::ProcessState::Terminated);
    }
    // Lo encolado para el disco (el estado de aire de recién, los ajustes), escrito
    // antes de irse.
    if (!ba::persist::cola().vaciar())
        ba::diag::registro().linea(ba::diag::Nivel::Error, "persistencia", 0,
                                   ba::i18n::traducir("persistencia", "al cerrar quedó algo sin guardar (el disco no responde)"));
    std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "cierre ordenado").c_str());
}

std::string AppRuntime::carpetaGrabacion() const {
    const AjustesAire a = ajustesAire();
    return a.carpetaEvento.empty() ? a.rec.directory : (a.carpetaEvento / "Grabaciones").string();
}

std::vector<std::string> AppRuntime::carpetasDeGrabaciones() const {
    std::vector<std::string> out{carpetaGrabacion()};
    if (out.front() != cfg_.rec.directory) out.push_back(cfg_.rec.directory);
    return out;
}

bool AppRuntime::startRecording(const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) {
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    const bool porOmision = id == salidas::kPorOmision;
    std::error_code ec;
    const AjustesAire a = ajustesAire();
    RecordConfig rec = a.rec;
    rec.directory = carpetaGrabacion();
    // La copia en el otro disco, también por evento: <copia>/Eventos/<evento>/Grabaciones.
    if (!rec.copiaDir.empty()) {
        const fs::path& ev = a.carpetaEvento;
        std::error_code e2;
        const fs::path rel = ev.empty() ? fs::path() : fs::relative(ev, root_ / "Eventos", e2);
        if (!ev.empty() && !e2 && !rel.empty() && rel.string().rfind("..", 0) != 0)
            rec.copiaDir = (fs::path(rec.copiaDir) / "Eventos" / rel / "Grabaciones").string();
        fs::create_directories(rec.copiaDir, e2);
    }
    fs::create_directories(rec.directory, ec);
    const EngineStatus antes = engine_.status();
    if (!engine_.startRecording(rec, codificadorDe(id), id)) {
        errorDelHilo() = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("grabacion", "la grabacion no arranco: %1"), {errorDelHilo()}).c_str());
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
        errorDelHilo() = engine_.lastError();
        return false;
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "grabacion", 0,
                               ba::i18n::llenar(ba::i18n::traducir("grabacion", "grabando la entrada \"%1\" aparte"), {source}));
    avisar(Cambio::Salidas);
    return true;
}

bool AppRuntime::stopSourceRecording(const std::string& source) {
    if (!engine_.stopSourceRecording(source)) {
        errorDelHilo() = engine_.lastError();
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

std::vector<std::string> AppRuntime::cerrarGrabacionesPorEnergia() {
    const TurnoMotor::Guarda turno(engine_.turno());
    emergenciaEnergia_ = true;
    std::vector<std::string> cerradas;
    for (const std::string& id : engine_.salidaIds()) {
        const bool graba = id == salidas::kPorOmision ? !recordStopWanted_ : grabandoOtras_.count(id) > 0;
        if (!graba) continue;
        // Como la para el operador (EOS: el archivo queda cerrado y se abre), pero anotada.
        porEnergia_.insert(id);
        stopRecording(id);
        cerradas.push_back(id);
    }
    if (cerradas.empty()) writeOnAirState();
    return cerradas;
}

void AppRuntime::retomarGrabacionesPorEnergia() {
    const TurnoMotor::Guarda turno(engine_.turno());
    emergenciaEnergia_ = false;
    const std::set<std::string> volver = std::move(porEnergia_);
    porEnergia_.clear();
    for (const std::string& id : volver) startRecording(id == salidas::kPorOmision ? std::string() : id);
    writeOnAirState();
}

void AppRuntime::stopRecording(const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
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

/// La credencial de la transmisión principal de una salida: la de la por omisión es
/// la de siempre; las demás, aparte (las de destinos.json son «stream-<destino>»).
std::string credencialDeSalida(const std::string& salida) {
    return salida.empty() || salida == salidas::kPorOmision ? std::string(kStreamCredential) : "salida-" + salida + "-stream";
}

bool readLastStream(std::string& server, std::string& key, const std::string& salida = {}) {
    std::string raw;
    if (!ba::security::CredentialStore::bundle().read(credencialDeSalida(salida), raw)) {
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

salidas::AlAire AppRuntime::aireActual() const {
    const TurnoMotor::Guarda turno(engine_.turno());
    // Cada salida con su escena, lo que transmite (la principal y sus destinos) y si
    // graba; la por omisión además en los campos de siempre (salidas::aJson), que es
    // lo que lee un arranque de antes de las salidas.
    salidas::AlAire a;
    a.cerrado = cerrando_;
    for (const std::string& id : engine_.salidaIds()) {
        const bool po = id == salidas::kPorOmision;
        salidas::Aire& x = a.salidas[id];
        x.escena = engine_.currentScene(id);
        x.transmitiendo = po ? !streamStopWanted_ : transmitiendoOtras_.count(id) > 0;
        // Las que cerró el corte de luz siguen pedidas: vuelven con la energía; y las de un
        // corte que esperan la carpeta del evento (retomarGrabaciones), también.
        x.grabando = (po ? !recordStopWanted_ : grabandoOtras_.count(id) > 0) || porEnergia_.count(id) > 0 ||
                     (grabacionesPorRetomar_ && aireAnterior_.de(id).grabando);
        for (const auto& [destino, salida] : destinosAlAire_)
            if (salida == id) x.destinos.push_back(destino);
        x.placa = salidas::placaDeJson(engine_.estadoPlaca(po ? std::string() : id));
    }
    return a;
}

void AppRuntime::writeOnAirState() {
    const TurnoMotor::Guarda turno(engine_.turno());
    // Mientras vuelve como estaba, el documento sigue diciendo lo de la corrida anterior:
    // un corte en pleno arranque vuelve a eso, no a un arranque a medias.
    if (!aireListo_) return;
    // La sección de las salidas del documento único (F3-04). El documento compara: si no
    // cambió nada no despierta a nadie; si cambió, lo escribe su dueño entero, en dos
    // generaciones, primero que los ajustes.
    documento_.ponerSalidas(aireActual().salidas, cerrando_);
}

void AppRuntime::aireCambio() { writeOnAirState(); }

void AppRuntime::leerAireAnterior() {
    documento_.setCarpeta(ba::platform::stateDir());
    const salidas::DocumentoAire::Anterior l = documento_.leerAnterior();
    aireAnterior_ = l.aire;
    lineaDesconocida_ = l.danado || l.lineaDanada;
    if (l.descartadas > 0 && !l.danado)
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::traducir("arranque", "al-aire.json: la última generación estaba rota (corte a "
                                                                  "mitad); se retoma la anterior"));
    if (l.deVersionVieja)
        ba::diag::registro().linea(ba::diag::Nivel::Info, "arranque", 0,
                                   ba::i18n::traducir("arranque", "al-aire.json de la versión anterior: placa.json y "
                                                                  "linea-aire.json pasan a ser secciones del documento de aire"));
    if (l.danado) {
        // Las dos generaciones rotas (o el archivo viejo roto): no se sabe qué había al
        // aire. Lo peor es arrancar callado con «nada al aire»: se trata como un corte
        // (la línea de tiempo vuelve en HOLD y pregunta qué está pasando) y se avisa.
        avisarPersistencia(ba::i18n::traducir("arranque", "El estado de aire guardado (al-aire.json) volvió dañado: no se "
                                                          "sabe qué había al aire. Revisá cada salida (escena, transmisión, "
                                                          "grabación) y ponela como corresponde."));
    } else if (l.lineaDanada) {
        avisarPersistencia(ba::i18n::traducir("arranque", "El estado de la línea de tiempo volvió dañado tras el corte: la "
                                                          "línea quedó en HOLD sin saber en qué paso iba. Contá qué está "
                                                          "pasando y reenganchala a mano."));
    }
    corteAnterior_ = !aireAnterior_.cerrado;
    // Un sistema de archivos sin diario (FAT, exFAT, típico de un disco USB) puede
    // romper la carpeta entera en un corte, aunque cada archivo se escriba bien.
    if (ba::persist::sinDiario(root_))
        avisarPersistencia(ba::i18n::llenar(ba::i18n::traducir("arranque", "La carpeta de datos está en un disco %1 (sin "
                                                                            "diario): un corte de luz puede dañarla. Pasala "
                                                                            "a un disco APFS, NTFS o ext4."),
                                            {ba::persist::sistemaDeArchivos(root_)}));
}

std::vector<std::string> AppRuntime::avisosPersistencia() const {
    std::lock_guard<std::mutex> l(avisosMu_);
    return avisosPersistencia_;
}

void AppRuntime::avisarPersistencia(const std::string& texto) {
    ba::diag::registro().linea(ba::diag::Nivel::Error, "persistencia", 0, texto);
    std::lock_guard<std::mutex> l(avisosMu_);
    if (std::find(avisosPersistencia_.begin(), avisosPersistencia_.end(), texto) == avisosPersistencia_.end())
        avisosPersistencia_.push_back(texto);
}

void AppRuntime::setCarpetaDelEvento(std::function<fs::path()> fn) {
    carpetaDelEvento_ = std::move(fn);
    if (carpetaDelEvento_) retomarGrabaciones();
}

void AppRuntime::retomarGrabaciones() {
    const TurnoMotor::Guarda turno(engine_.turno());
    if (!grabacionesPorRetomar_) return;
    grabacionesPorRetomar_ = false;
    for (const auto& [id, s] : aireAnterior_.salidas) {
        if (!s.grabando || !engine_.existeSalida(id)) continue;
        if (id == salidas::kPorOmision ? engine_.status().recording : grabandoOtras_.count(id) > 0) continue;
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la grabación de la salida «%1»"), {id}));
        if (!startRecording(id))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la grabación de la salida «%1» no se pudo retomar: %2"), {id, errorDelHilo()}));
    }
}

void AppRuntime::restaurarAire() {
    const TurnoMotor::Guarda turno(engine_.turno());
    const salidas::AlAire& aire = aireAnterior_;
    // La escena que estaba al aire (app.json puede tener una vieja: no se guarda en cada
    // corte). También con --test: no sale nada al aire.
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
                                                        {id, e, errorDelHilo()}));
    }
    // El negro y la placa vuelven siempre (son de la máquina, no de la corrida): si la app
    // se cerró o se cayó con la placa puesta, vuelve con la placa puesta.
    placaRestaurada_ = restaurarPlacas(aire);
    // VOLVER COMO ESTABA lo que salía: un cierre ordenado deja el documento en «nada al
    // aire» (stop()). Si dice que se transmitía o grababa, la corrida anterior terminó
    // sin cerrarse (corte de luz, cuelgue, la máquina se apagó): se retoma, también en un
    // arranque normal (no solo con --restore del launcher). Con --test (pruebas: el MCP
    // sin aprobaciones) NUNCA se arranca al aire.
    restoreOutputs();
}

bool AppRuntime::restaurarPlacas(const salidas::AlAire& aire) {
    bool algo = false;
    for (const auto& [id, s] : aire.salidas) {
        if (!s.placa.puesta() || !engine_.existeSalida(id)) continue;
        const std::string salida = id == salidas::kPorOmision ? std::string() : id;
        if (s.placa.placa && !engine_.placa(true, s.placa.texto, s.placa.imagen, 0, salida))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la placa de la salida «%1» no volvió: %2"),
                                                        {id, engine_.lastError()}));
        if (s.placa.ftb && !engine_.ftb(true, 0, s.placa.ftbAudio, salida))
            ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "el negro de la salida «%1» no volvió: %2"),
                                                        {id, engine_.lastError()}));
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "vuelve lo que estaba encima de la salida «%1»: %2"),
                                                    {id, s.placa.placa && s.placa.ftb ? ba::i18n::traducir("arranque", "el negro y la placa")
                                                         : s.placa.placa              ? ba::i18n::traducir("arranque", "la placa")
                                                                                      : ba::i18n::traducir("arranque", "el negro")}));
        algo = true;
    }
    return algo;
}

void AppRuntime::restoreOutputs(bool deEstaCorrida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    const salidas::AlAire& aire = aireAnterior_;
    if (aire.cerrado || aire.salidas.empty()) return; // se cerró en orden: nada al aire
    // La escena sí; las salidas de una corrida anterior, nunca con --test (salvo el banco de
    // caos). Las de esta (el rearme por la GPU) estaban saliendo recién: vuelven siempre.
    if (cfg_.modoPrueba && !cfg_.retomaEnPrueba && !deEstaCorrida) return;
    // Las grabaciones, cuando se sepa DÓNDE: la carpeta del evento la da la interfaz
    // (setCarpetaDelEvento) después de este arranque; si se retoman antes caen en la
    // carpeta de fábrica, fuera del evento. Sin evento, retomarGrabaciones() a secas.
    grabacionesPorRetomar_ = true;
    if (carpetaDelEvento_) retomarGrabaciones();
    // TODAS las salidas, cada una con lo suyo: transmisión principal y destinos.
    for (const auto& [id, s] : aire.salidas) {
        if (!engine_.existeSalida(id)) continue;
        const bool po = id == salidas::kPorOmision;
        if (s.transmitiendo && !engine_.haySalidaEn(id, /*contarGrabacion=*/false)) {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró (corte de luz o cuelgue): se retoma la transmisión de la salida «%1»"), {id}));
            // La principal, con su clave de siempre; las demás, con la que usaron (cifrada).
            std::string server, key;
            const bool ok = po ? startStreaming() : (readLastStream(server, key, id) && startStreaming(server, key, false, id));
            if (!ok)
                ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "la transmisión de la salida «%1» no se pudo retomar: %2"),
                                                            {id, key.empty() && !po ? ba::i18n::traducir("arranque", "no quedó guardada su clave") : errorDelHilo()}));
        }
        for (const std::string& destino : s.destinos) {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("arranque", "la corrida anterior no se cerró: se retoma el destino «%1» de la salida «%2»"), {destino, id}));
            if (const std::string e = iniciarDestino(destino); !e.empty())
                ba::diag::registro().linea(ba::diag::Nivel::Error, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "el destino «%1» no se pudo retomar: %2"), {destino, e}));
        }
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
    if (!startSourceRecording(fuente)) return errorDelHilo();
    return {};
}

bool AppRuntime::salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                   const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    if (!startStreaming(server, key, cambiarEnCaliente, salida)) return false;
    if (engine_.ultimoInicio() != BroadcastEngine::Inicio::YaIgual && alVivoManual_) {
        const std::string srv = server.empty() ? ajustesAire().stream.server : server;
        // Lo anotan el bus y el histórico, que son del dueño.
        if (!srv.empty()) enDueno([f = alVivoManual_, srv]() { f(srv); });
    }
    return true;
}

bool AppRuntime::startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                                const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    const AjustesAire a = ajustesAire();
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) {
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay una salida «%1»"), {salida});
        return false;
    }
    // Otra salida: su transmisión principal va a lo que se pida (la clave de la
    // por omisión y la última guardada son de la por omisión: no se reusan, que
    // la misma clave dos veces pisa la transmisión). La clave se guarda cifrada
    // aparte, solo para retomarla tras un corte.
    if (id != salidas::kPorOmision) {
        if (server.empty() || key.empty()) {
            errorDelHilo() = ba::i18n::traducir("salidas", "para otra salida, pasá el servidor y la clave (o usá un destino "
                                                       "guardado con esa salida)");
            return false;
        }
        StreamConfig sc = a.stream;
        sc.server = server;
        sc.key = key;
        const bool salia = engine_.haySalidaEn(id);
        if (!engine_.startStreaming(sc, codificadorDe(id), cambiarEnCaliente, id)) {
            errorDelHilo() = engine_.lastError();
            return false;
        }
        if (!salia) engine_.loudness(id).reiniciar();
        // Para volver tras un corte: que transmitía y adónde (la clave, cifrada).
        transmitiendoOtras_.insert(id);
        ba::security::CredentialStore::bundle().write(
            credencialDeSalida(id), nlohmann::json{{"server", server}, {"key", key}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        writeOnAirState();
        avisar(Cambio::Salidas);
        return true;
    }
    StreamConfig sc = a.stream;
    if (!server.empty()) {
        sc.server = server;
    }
    if (!key.empty()) {
        sc.key = key;
    } else {
        const char* envKey = std::getenv(a.claveEnv.c_str());
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
        errorDelHilo() = ba::i18n::traducir("salidas", "no hay servidor de transmision configurado");
        return false;
    }
    const EngineStatus antes = engine_.status();
    if (!engine_.startStreaming(sc, a.encoder, cambiarEnCaliente)) {
        errorDelHilo() = engine_.lastError();
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("salidas", "la transmision no arranco: %1"), {errorDelHilo()}).c_str());
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
    const TurnoMotor::Guarda turno(engine_.turno());
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) return;
    if (id == salidas::kPorOmision)
        streamStopWanted_ = true;
    else
        transmitiendoOtras_.erase(id);
    writeOnAirState();
    engine_.stopStreaming(id);
    avisar(Cambio::Salidas);
}

namespace {
nlohmann::json leerDestinos(const std::filesystem::path& root) {
    std::string bytes;
    ba::persist::cola().leer(root / "Config" / "destinos.json", bytes);
    const nlohmann::json j = nlohmann::json::parse(bytes, nullptr, false);
    return j.is_array() ? j : nlohmann::json::array();
}
void escribirDestinos(const std::filesystem::path& root, const nlohmann::json& d) {
    config::guardar(root / "Config" / "destinos.json", d);
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
        if (!d.value("url_respaldo", std::string()).empty())
            d["respaldo_conectado"] = e.value("ingest_respaldo", nlohmann::json::object()).value("conectada", false);
        out.push_back(d);
    }
    return out;
}

std::string AppRuntime::guardarDestino(const nlohmann::json& d, std::string* idOut) {
    const std::string url = d.value("url", std::string());
    // El punto de ingest de respaldo (la misma clave): RTMP o SRT, como el principal; "" lo saca.
    const std::string urlRespaldo = d.value("url_respaldo", std::string());
    if (!urlRespaldo.empty() && urlRespaldo.rfind("rtmp://", 0) != 0 && urlRespaldo.rfind("rtmps://", 0) != 0 && urlRespaldo.rfind("srt://", 0) != 0)
        return ba::i18n::traducir("salidas", "`url_respaldo`: rtmp://, rtmps:// o srt://");
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
        if (!urlRespaldo.empty()) (*existente)["url_respaldo"] = urlRespaldo;
    } else {
        for (const char* k : {"nombre", "url", "respaldo", "salida"})
            if (d.contains(k)) (*existente)[k] = d[k];
        if (d.contains("url_respaldo")) {
            if (urlRespaldo.empty()) existente->erase("url_respaldo");
            else (*existente)["url_respaldo"] = urlRespaldo;
        }
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
    if (!engine_.setReplay(segundos, cfg_.rec.directory + "/repeticiones", codificadorDe(id), id))
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
    if (!engine_.startCleanFeed(url, clave, dir, codificadorDe(salida), salida)) return engine_.lastError();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               destino.empty() ? ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia grabando en %1"), {dir})
                                               : ba::i18n::llenar(ba::i18n::traducir("salidas", "señal limpia a %1"), {destino}));
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::borrarDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_.turno());
    nlohmann::json lista = leerDestinos(root_), nueva = nlohmann::json::array();
    for (const auto& x : lista)
        if (x.value("id", std::string()) != id) nueva.push_back(x);
    if (nueva.size() == lista.size()) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1»"), {id});
    engine_.stopExtra(id);
    if (destinosAlAire_.erase(id) > 0) writeOnAirState();
    ba::security::CredentialStore::bundle().remove("stream-" + id);
    escribirDestinos(root_, nueva);
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::iniciarDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_.turno());
    std::string errores;
    bool alguno = false;
    for (const auto& d : leerDestinos(root_)) {
        const std::string did = d.value("id", std::string());
        if (id != "todos" && did != id) continue;
        alguno = true;
        std::string clave;
        ba::security::CredentialStore::bundle().read("stream-" + did, clave);
        if (engine_.startExtra(did, d.value("url", std::string()), clave, codificadorDe(salidas::salidaDeDestino(d)),
                               salidas::salidaDeDestino(d),
                               d.value("url_respaldo", std::string())))
            destinosAlAire_[did] = salidas::salidaDeDestino(d); // vuelve tras un corte
        else
            errores += (errores.empty() ? "" : "; ") + did + ": " + engine_.lastError();
    }
    if (!alguno) return ba::i18n::llenar(ba::i18n::traducir("salidas", "no hay un destino «%1» (stream_destinos los lista)"), {id});
    writeOnAirState();
    // Aunque alguno haya fallado, otros pudieron arrancar: la lista cambia igual.
    avisar(Cambio::Salidas);
    return errores;
}

nlohmann::json AppRuntime::discoGrabacion() const {
    const TurnoMotor::Guarda turno(engine_.turno());
    const AjustesAire a = ajustesAire();
    const RecordConfig& rec = a.rec;
    std::error_code ec;
    fs::create_directories(rec.directory, ec);
    // Lo que ya escribe el motor (todas las salidas: grabación, copia, señal limpia, ISO)
    // y, si la de la por omisión no graba, lo que sumaría al arrancarla (con su copia):
    // el disco se reparte entre todo lo que va a él.
    const nlohmann::json estado = engine_.discoEstado();
    std::vector<ba::platform::EscrituraDisco> e;
    for (const nlohmann::json& v : estado.value("restante", nlohmann::json::object()).value("volumenes", nlohmann::json::array())) {
        std::string que;
        for (const nlohmann::json& q : v.value("que", nlohmann::json::array())) que += (que.empty() ? "" : ", ") + q.get<std::string>();
        e.push_back({que, v.value("carpeta", std::string()), v.value("kbps", 0.0)});
    }
    const int kbps = BroadcastEngine::kbpsGrabacion(rec, a.encoder);
    const bool graba = engine_.grabacionEstado().value("activa", false);
    if (!graba) {
        e.push_back({ba::i18n::traducir("emision", "grabación"), rec.directory, static_cast<double>(kbps)});
        if (!rec.copiaDir.empty())
            e.push_back({ba::i18n::traducir("emision", "copia de la grabación"), rec.copiaDir, static_cast<double>(kbps)});
    }
    nlohmann::json d = ba::platform::aJson(ba::platform::discoRestante(e));
    d["carpeta"] = rec.directory;
    d["kbps_grabacion"] = kbps;
    // La copia en el mismo disco que la grabación no es una copia: lo que graba ahora
    // (el motor lo mide) o, sin grabar, lo que está configurado.
    nlohmann::json mismo = estado.value("mismo_volumen", nlohmann::json::array());
    if (!graba && !rec.copiaDir.empty() && ba::platform::mismoVolumen(rec.directory, rec.copiaDir))
        mismo.push_back({{"salida", std::string(BroadcastEngine::kSalidaPorOmision)}, {"grabacion", rec.directory}, {"copia", rec.copiaDir}});
    d["copia_mismo_volumen"] = !mismo.empty();
    d["mismo_volumen"] = std::move(mismo);
    return d;
}

void AppRuntime::detenerDestino(const std::string& id) {
    const TurnoMotor::Guarda turno(engine_.turno());
    if (id == "todos")
        destinosAlAire_.clear();
    else
        destinosAlAire_.erase(id);
    writeOnAirState();
    if (id == "todos")
        engine_.stopExtras();
    else
        engine_.stopExtra(id);
    avisar(Cambio::Salidas);
}

// --- Escenas y fuentes ----------------------------------------------------------

bool AppRuntime::addScene(const std::string& name) {
    if (!engine_.createScene(name)) {
        errorDelHilo() = engine_.lastError();
        return false;
    }
    cfg_.scenes.push_back(SceneConfig{name, {}});
    avisar(Cambio::Escenas);
    return true;
}

bool AppRuntime::copyScene(const std::string& from, const std::string& to) {
    // `errorDelHilo()` es pegajoso: guarda lo ultimo que fallo, de cuando sea. Como
    // aca se lo usa DESPUES de haber salido bien —para contar que una fuente no
    // se pudo recrear— hay que limpiarlo al entrar, o la copia informa como
    // propio un error de otra operacion anterior.
    errorDelHilo().clear();
    const SceneConfig* origen = nullptr;
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == from) origen = &s;
    if (origen == nullptr) {
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {from});
        return false;
    }
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == to) {
            errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "ya hay una escena llamada \"%1\""), {to});
            return false;
        }
    if (!engine_.createScene(to)) {
        errorDelHilo() = engine_.lastError();
        return false;
    }
    // Se copia la CONFIGURACION, no las fuentes del motor: compartirlas haria que
    // mover algo en la copia moviera el original. Recrearlas desde la
    // especificacion da dos escenas de verdad independientes, que es lo que uno
    // espera al copiar.
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
            errorDelHilo() = ba::i18n::llenar(
                ba::i18n::traducir("escenas", "la escena se copio, pero la fuente \"%1\" no se pudo recrear: %2"),
                {spec.name, errorDelHilo()});
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
    errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {name});
    return false;
}

bool AppRuntime::sceneProtected(const std::string& name) const {
    for (const SceneConfig& s : cfg_.scenes)
        if (s.name == name) return s.protegida;
    return false;
}

bool AppRuntime::removeScene(const std::string& name) {
    if (sceneProtected(name)) {
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {name});
        return false;
    }
    if (!engine_.removeScene(name)) {
        errorDelHilo() = engine_.lastError();
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
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {from});
        return false;
    }
    if (!engine_.renameScene(from, to)) {
        errorDelHilo() = engine_.lastError();
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
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
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
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
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
    const TurnoMotor::Guarda turno(engine_.turno());
    return anotarAire(engine_.setCurrentScene(name, salida), name, salida);
}

bool AppRuntime::setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    return anotarAire(engine_.setCurrentSceneCon(name, tipo, ms, salida), name, salida);
}

bool AppRuntime::anotarAire(bool ok, const std::string& name, const std::string& salida) {
    if (!ok) {
        errorDelHilo() = engine_.lastError();
        return false;
    }
    anotarEscenaAlAire(name, salida);
    return true;
}

void AppRuntime::anotarEscenaAlAire(const std::string& name, const std::string& salida) {
    const TurnoMotor::Guarda turno(engine_.turno());
    // La de la por omisión va también en app.json (la de las demás, solo en al-aire.json);
    // la configuración es del dueño: se la anota él.
    if (engine_.idDeSalida(salida) == salidas::kPorOmision) enDueno([this, name]() { cfg_.currentScene = name; });
    // El documento de aire, ya: con la interfaz trabada, la vuelta del motor (que lo
    // pone en cada una) no llega, y lo que sale tiene que volver igual tras un corte.
    writeOnAirState();
    avisar(Cambio::Aire);
}

// --- Las salidas del aparato ----------------------------------------------------

audio::ObjetivoLoudness AppRuntime::objetivoLoudness() {
    return audio::ObjetivoLoudness(
        root_, [this](const std::string& salida) { return engine_.loudnessDe(salida); }, [this]() { engine_.aplicarLoudness(); });
}

void AppRuntime::crearSalidasGuardadas() {
    bool alguna = false;
    for (const auto& s : salidas::leer(config::leerObjeto(archivoSalidas())).salidas) {
        if (engine_.existeSalida(s.id)) continue;
        if (!engine_.crearSalida(s.id, s.nombre)) {
            ba::diag::registro().linea(ba::diag::Nivel::Error, "salidas", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("salidas", "la salida «%1» no arrancó: %2"),
                                                        {s.id, engine_.lastError()}));
            continue;
        }
        // Su mezcla como quedó (arrancó con la de la por omisión). Una fuente que ya no
        // está no tiene fader: se saltea (queda anotada por si vuelve).
        for (const auto& [fuente, m] : s.mezcla) {
            engine_.setSourceVolume(fuente, static_cast<float>(m.volumen), s.id);
            engine_.setSourceMuted(fuente, m.mute, s.id);
        }
        for (const auto& [grupo, g] : s.grupos) engine_.setGananciaGrupo(grupo, static_cast<float>(g), s.id);
        alguna = true;
    }
    if (alguna) objetivoLoudness().cargar(); // el objetivo de loudness que guardó cada una
}

EncoderConfig AppRuntime::codificadorDe(const std::string& salida) const {
    EncoderConfig e = ajustesAire().encoder;
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty() || id == salidas::kPorOmision) return e;
    const salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (const salidas::Salida* s = l.buscar(id); s != nullptr && !s->codificador.empty()) e.videoEncoder = s->codificador;
    return e;
}

void AppRuntime::guardarMando(const std::string& salida, const std::string& fuente) {
    // El fader y el mute de otra salida: en Config/salidas.json (encolado: un arrastre
    // de fader escribe lo último, no cada paso).
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (salidas::ponerMando(l, salida, fuente, {engine_.sourceVolume(fuente, salida), engine_.sourceMuted(fuente, salida)}))
        config::guardar(archivoSalidas(), salidas::aJson(l));
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
                       {"escena", engine_.currentScene(s.id)},
                       {"codificador", s.codificador}});
    return {{"tope", l.tope}, {"salidas", arr}};
}

std::string AppRuntime::crearSalida(const std::string& id, const std::string& nombre, const std::string& color) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::agregar(l, {id, nombre, color, 0, {}, {}}); !e.empty()) return e;
    // Que la máquina dé abasto lo dice sistema_recursos: el tope es el límite que puso el operador.
    if (!engine_.crearSalida(id, l.buscar(id)->nombre)) return engine_.lastError();
    if (!config::guardar(archivoSalidas(), salidas::aJson(l)))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "salidas", 0,
                                   ba::i18n::traducir("salidas", "no se pudo guardar Config/salidas.json"));
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida nueva: «%1»"), {id}));
    writeOnAirState();
    publicarSalidas(engine_.status());
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::editarSalida(const std::string& id, const nlohmann::json& cambios) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::editar(l, id, cambios); !e.empty()) return e;
    config::guardar(archivoSalidas(), salidas::aJson(l));
    publicarSalidas(engine_.status());
    avisar(Cambio::Salidas);
    return {};
}

std::string AppRuntime::borrarSalida(const std::string& id, const std::string& susDestinos) {
    const TurnoMotor::Guarda turno(engine_.turno());
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    // Sus destinos: a otra salida o borrados, lo que se decidió (antes de tocar nada).
    nlohmann::json destinos = leerDestinos(root_);
    std::vector<std::string> borrados;
    std::string aDonde = susDestinos; // «borrar», o la otra salida (si existe, por su id)
    if (!aDonde.empty() && aDonde != "borrar")
        if (const std::string r = engine_.idDeSalida(aDonde); !r.empty()) aDonde = r;
    if (std::string e = salidas::destinosDeBorrada(destinos, l, id, aDonde, &borrados); !e.empty()) return e;
    if (std::string e = salidas::quitar(l, id); !e.empty()) return e;
    // Lo que sale de ella (transmisiones, grabación, repetición, limpia) se para con ella.
    if (engine_.existeSalida(id) && !engine_.borrarSalida(id)) return engine_.lastError();
    objetivoLoudness().olvidar(id);
    grabandoOtras_.erase(id);
    config::guardar(archivoSalidas(), salidas::aJson(l));
    // Los destinos que salían con ella ya se cortaron (borrarSalida): no vuelven tras un corte.
    for (auto it = destinosAlAire_.begin(); it != destinosAlAire_.end();)
        it = it->second == id ? destinosAlAire_.erase(it) : std::next(it);
    for (const auto& b : borrados) ba::security::CredentialStore::bundle().remove("stream-" + b);
    escribirDestinos(root_, destinos);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida borrada: «%1»"), {id}));
    writeOnAirState();
    publicarSalidas(engine_.status());
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
        errorDelHilo() = engine_.lastError();
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
    inputs::alMotor(spec); // se guarda con el tipo del motor (esquema 2 de app.json)
    if (!engine_.addSource(scene, spec)) {
        errorDelHilo() = engine_.lastError();
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
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
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
        errorDelHilo() = ba::i18n::llenar(
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
        errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
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
        errorDelHilo() = engine_.lastError();
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
        errorDelHilo() = engine_.lastError();
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
    if (const std::string id = engine_.idDeSalida(salida); id != BroadcastEngine::kSalidaPorOmision) {
        guardarMando(id, name); // la mezcla de otra salida: es de su salida, no de la escena
        avisar(Cambio::Audio);
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

bool AppRuntime::setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida) {
    if (grupo.empty()) return false;
    const std::string id = engine_.idDeSalida(salida);
    if (id.empty()) return false;
    const float g = std::max(0.0f, mul);
    engine_.setGananciaGrupo(grupo, g, id);
    if (id != BroadcastEngine::kSalidaPorOmision) {
        salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
        if (salidas::ponerGrupo(l, id, grupo, g)) config::guardar(archivoSalidas(), salidas::aJson(l));
    } else if (std::abs(g - 1.0f) < 1e-6f) {
        cfg_.gruposAudio.erase(grupo);
    } else {
        cfg_.gruposAudio[grupo] = g;
    }
    avisar(Cambio::Audio);
    return true;
}

bool AppRuntime::setMuted(const std::string& name, bool muted, const std::string& salida) {
    if (!engine_.setSourceMuted(name, muted, salida)) {
        return false;
    }
    if (const std::string id = engine_.idDeSalida(salida); id != BroadcastEngine::kSalidaPorOmision) {
        guardarMando(id, name);
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
        errorDelHilo() = engine_.lastError();
        return false;
    }
    cfg_.video = v;
    reubicarFuentes();
    return true;
}

void AppRuntime::reubicarFuentes() {
    // Las ubicaciones son fracciones del lienzo: se recalculan en pixeles.
    for (const auto& sc : cfg_.scenes) {
        for (const auto& s : sc.sources) {
            engine_.setSourceRect(sc.name, s.name, s.rect);
        }
    }
}

namespace {
/// El lienzo para componer en la CPU (F3-09): a 720p como mucho, con la misma forma. No
/// se guarda: el próximo arranque vuelve a probar la GPU con el de la configuración.
VideoConfig videoParaCpu(VideoConfig v) {
    constexpr int kAlto = 720;
    const auto achicar = [](int& w, int& h) {
        if (h <= kAlto || h <= 0) return;
        w = (w * kAlto / h) & ~1;
        h = kAlto;
    };
    achicar(v.baseWidth, v.baseHeight);
    achicar(v.outputWidth, v.outputHeight);
    return v;
}
} // namespace

void AppRuntime::rearmarPorGpu() {
    const TurnoMotor::Guarda turno(engine_.turno());
    const auto desde = std::chrono::steady_clock::now();
    const nlohmann::json antes = ba::motor::estadoGpu();
    ba::diag::registro().linea(ba::diag::Nivel::Error, "motor", 0,
                               ba::i18n::llenar(ba::i18n::traducir("arranque", "se perdió la GPU del video (%1): se rearma el motor"),
                                                {antes.value("motivo", std::string())}));
    // Lo que sale ahora es lo que se retoma (como después de un corte, pero sin leer el
    // disco). Las salidas se paran con el motor viejo: lo que estaba saliendo vuelve a
    // empezar (ba-emisor sostiene la transmisión con su placa mientras tanto, F3-01).
    const salidas::AlAire aire = aireActual();
    VideoConfig usado = cfg_.video;
    ba::motor::Gpu gpu = ba::motor::Gpu::Ninguna;
    const bool ok = engine_.resetVideo(cfg_.video, [&]() {
        gpu = ba::motor::rearmarGpu();
        usado = gpu == ba::motor::Gpu::Ninguna ? videoParaCpu(cfg_.video) : cfg_.video;
        return usado;
    });
    if (!ok) {
        // Sin motor no hay aire: que el supervisor lo relance (el latido de los cuadros se
        // corta solo) y vuelva como de un corte.
        ba::diag::registro().linea(ba::diag::Nivel::Error, "motor", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no se pudo rearmar tras perder la GPU: %1"),
                                                    {engine_.lastError()}));
        // Si no llegó a apagarlo, la marca sigue: que no se reintente en cada tic.
        if (ba::motor::gpuPerdida()) ba::motor::rearmarGpu();
        return;
    }
    reubicarFuentes();
    operator_.configure(engine_, cfg_.operatorVideo, cfg_.operatorAudio); // lo que ve el lobby, del motor nuevo
    // Las salidas, como restoreOutputs después de un corte: las de otras salidas se vuelven a
    // anotar al arrancar (startRecording/startStreaming).
    grabandoOtras_.clear();
    transmitiendoOtras_.clear();
    destinosAlAire_.clear();
    aireAnterior_ = aire;
    aireAnterior_.cerrado = false;
    restaurarPlacas(aire);
    restoreOutputs(/*deEstaCorrida=*/true);
    writeOnAirState();
    avisar(Cambio::Salidas);
    avisar(Cambio::Escenas);
    const long long ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - desde).count();
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0,
                               ba::i18n::llenar(ba::i18n::traducir("arranque", "motor rearmado tras perder la GPU en %1 ms: video en %2, %3×%4"),
                                                {std::to_string(ms), ba::motor::nombreGpu(), std::to_string(usado.baseWidth),
                                                 std::to_string(usado.baseHeight)}));
}

bool AppRuntime::applyEncoder(const EncoderConfig& e) {
    if (!engine_.resetEncoders()) {
        errorDelHilo() = engine_.lastError();
        return false;
    }
    cfg_.encoder = e;
    return true;
}

bool AppRuntime::save() {
    // La configuración es del dueño: desde el plano de control se la encarga (lo que sale
    // ya quedó en el documento de aire, que no espera a nadie).
    if (!esDueno()) {
        enDueno([this]() { save(); });
        return true;
    }
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
        errorDelHilo() = err;
        return false;
    }
    return true;
}

namespace {
/// El pedido de reinicio a B (bloquea hasta ~4 s). Sin tocar el estado de
/// AppRuntime: lo usan restartGateway y el hilo de reiniciarSalaEnElFondo.
bool pedirReinicioGateway(std::string& error) {
    // B relee gateway.json al volver: lo guardado tiene que estar en el disco.
    ba::persist::cola().vaciar();
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
    m.correlacion = ba::diag::correlacion::actual(); // la orden que lo pidió, si hay (F4-02)
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
    if (!ok) errorDelHilo() = e;
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
