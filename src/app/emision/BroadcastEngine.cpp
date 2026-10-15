// ============================================================================
// src/app/emision/BroadcastEngine.cpp — Motor de emision: ciclo de vida, salidas y vistas
//
// Parte de BroadcastEngine (ver BroadcastEngine.hpp): arranque del motor de
// GStreamer, las salidas (transmisión y destinos, grabación con copia y
// pistas, repetición, grabación por entrada, señal limpia), las vistas para la
// interfaz, el programa hacia el Proceso B y lo que se mide del programa
// (loudness, picos, brillo, la sincronía con un aplauso).
// ============================================================================

#include "emision/BroadcastEngine.hpp"
#include "ajustes/Ajustes.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/NdiSource.hpp"
#include "audio/GstRack.hpp"
#include "audio/GstVoz.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"
#include "camaras/SincroLabios.hpp"
#include "diag/Registro.hpp"
#include "diag/SalidaAsincrona.hpp"
#include "platform/Paths.hpp"
#include "ipc/HealthBlock.hpp" // nowQpc() para los timestamps del IPC

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace ba::app {

using json = nlohmann::json;

bool fpsDeTexto(const std::string& t, int& num, int& den) {
    if (t.empty()) return false;
    const auto barra = t.find('/');
    if (barra != std::string::npos) {
        const int n = std::atoi(t.substr(0, barra).c_str());
        const int d = std::atoi(t.substr(barra + 1).c_str());
        if (n <= 0 || d <= 0) return false;
        num = n;
        den = d;
        return true;
    }
    std::string c = t;
    std::replace(c.begin(), c.end(), ',', '.');
    const double f = std::atof(c.c_str());
    if (f <= 0 || f > 1000) return false;
    // Las cadencias NTSC (23,976 / 29,97 / 59,94 / 119,88): N*1000/1001.
    const double ntsc = std::round(f * 1.001);
    if (std::abs(f - std::floor(f)) > 1e-6 && std::abs(ntsc * 1000.0 / 1001.0 - f) < 0.01) {
        num = static_cast<int>(ntsc) * 1000;
        den = 1001;
        return true;
    }
    num = static_cast<int>(std::lround(f * 1000));
    den = 1000;
    if (num % 1000 == 0) {
        num /= 1000;
        den = 1;
    }
    return true;
}

std::string textoDeFps(int num, int den) {
    if (den <= 1) return std::to_string(num);
    return std::to_string(num) + "/" + std::to_string(den);
}

std::string fpsLegible(int num, int den) {
    if (den <= 1) return std::to_string(num);
    char b[32];
    std::snprintf(b, sizeof b, "%.3g", static_cast<double>(num) / den);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

namespace {
std::atomic<int> g_lienzoAncho{0}, g_lienzoAlto{0};
std::atomic<double> g_fps{30.0};
} // namespace

double fpsActual() { return g_fps.load(); }

bool lienzoActual(int& ancho, int& alto) {
    ancho = g_lienzoAncho.load();
    alto = g_lienzoAlto.load();
    return ancho > 0 && alto > 0;
}

namespace {

constexpr const wchar_t* kProgramVideoName = L"BA.Program";

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string enmascarar(const std::string& server, const std::string& key) {
    std::string s = server;
    if (!key.empty()) s += " (clave …" + (key.size() > 4 ? key.substr(key.size() - 4) : key) + ")";
    return s;
}

/// La dirección completa para el motor: en RTMP la clave va al final; en SRT, como passphrase.
std::string urlConClave(const std::string& server, const std::string& key) {
    if (key.empty()) return server;
    if (server.rfind("srt://", 0) == 0)
        return server + (server.find('?') == std::string::npos ? "?" : "&") + "passphrase=" + key;
    return server + (server.empty() || server.back() == '/' ? "" : "/") + key;
}

motor::Lienzo lienzoDe(const VideoConfig& v) {
    motor::Lienzo l;
    l.ancho = v.baseWidth;
    l.alto = v.baseHeight;
    l.fpsN = v.fpsNum;
    l.fpsD = std::max(1, v.fpsDen);
    l.color = v.color == "601" ? "bt601" : v.color == "2100pq" ? "bt2100-pq" : v.color == "2100hlg" ? "bt2100-hlg" : "bt709";
    l.rangoCompleto = v.rango == "completo";
    l.diezBits = v.formato == "p010" || v.formato == "i010";
    return l;
}

/// BGRA (con su stride) → RGBA empaquetado.
void aRgba(const motor::Cuadro& c, std::vector<std::uint8_t>& dst) {
    dst.resize(static_cast<std::size_t>(c.ancho) * c.alto * 4);
    for (int y = 0; y < c.alto; ++y) {
        const std::uint8_t* s = c.bgra.data() + static_cast<std::size_t>(y) * c.stride;
        std::uint8_t* d = dst.data() + static_cast<std::size_t>(y) * c.ancho * 4;
        for (int x = 0; x < c.ancho; ++x) {
            d[4 * x] = s[4 * x + 2];
            d[4 * x + 1] = s[4 * x + 1];
            d[4 * x + 2] = s[4 * x];
            d[4 * x + 3] = s[4 * x + 3];
        }
    }
}

std::string marcaDeTiempo() {
    return std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

struct BroadcastEngine::MedicionSincro {
    std::mutex mu;
    std::vector<camaras::Muestra> audio, video;
    std::vector<std::uint8_t> anterior;
};

// ============================================================================
// Ciclo de vida
// ============================================================================

BroadcastEngine::BroadcastEngine() = default;

BroadcastEngine::~BroadcastEngine() { shutdown(); }

void BroadcastEngine::setError(std::string msg) { lastError_ = std::move(msg); }

bool BroadcastEngine::startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio) {
    if (started_) return true;
    paths_ = paths;
    video_ = video;
    audio_ = audio;
    std::string err;
    if (!motor::iniciarGStreamer(&err)) {
        setError("GStreamer no arrancó: " + err);
        return false;
    }
    // Los elementos propios de la app, antes de armar cualquier entrada que los use
    // (racks, voces, NDI): si no, avisaban «no element» y reintentaban.
    audio::gstrack::registrarElemento();
    audio::gstvoz::registrarElemento();
    ndi::registrarElemento();
    // Los avisos del motor, al registro de la app.
    motor::ponerRegistro([](motor::Nivel n, const std::string& t) {
        ba::diag::registro().linea(n == motor::Nivel::Error  ? ba::diag::Nivel::Error
                                   : n == motor::Nivel::Aviso ? ba::diag::Nivel::Aviso
                                                              : ba::diag::Nivel::Info,
                                   "motor", 0, t);
    });
    motor::Motor::Config c;
    c.lienzo = lienzoDe(video);
    c.reloj = relojTipo_;
    c.relojServidor = relojServidor_;
    c.relojPuerto = relojPuerto_;
    c.relojDominio = relojDominio_;
    motor_ = std::make_unique<motor::Motor>(c);
    internasPedidas_.clear();
    // Lo que se le encargó y falló después (las órdenes van en cola: ver
    // Motor::orden). Llega desde el hilo del motor.
    motor_->alFallar([this](const std::string& que, const std::string& error) {
        std::function<void(const std::string&)> fn;
        {
            std::lock_guard<std::mutex> l(falloMotorM_);
            fn = falloMotor_;
        }
        if (fn) fn("el motor no pudo hacer «" + que + "»: " + error);
    });
    const std::string e = motor_->iniciar();
    if (!e.empty()) {
        setError(e);
        motor_.reset();
        return false;
    }
    started_ = true;
    {
        std::lock_guard<std::mutex> l(salidasColaM_);
        salidasSeguir_ = true;
    }
    {
        std::ifstream fd(ba::platform::configRoot() / "Config" / "disco.json");
        const json j = json::parse(fd, nullptr, false);
        const std::string m = j.is_object() ? j.value("priorizar_programa", std::string("preguntar")) : "preguntar";
        modoDisco_ = m == "auto" ? 1 : m == "nunca" ? 2 : 0;
    }
    hiloSalidas_ = std::thread([this]() { bucleSalidas(); });
    if (relojPublicar_ > 0) motor_->publicarReloj(relojPublicar_);
    fuentes::ponerMotorApp(this);
    // Lo que pinta la app (chat, gráficos, subtítulos) va a su entrada «app».
    fuentes::ponerSalida([this](const std::string& n, const std::uint8_t* d, int w, int h, int st) {
        if (motor_) motor_->empujarCuadro(n, d, w, h, st);
    });
    fuentes::ponerCambiar([this](const std::string& n, const json& cambios) {
        return updateSourceSettings(n, cambios.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    });
    fuentes::ponerConsultas({[this]() {
                                 std::vector<std::pair<std::string, std::string>> v;
                                 for (const auto& n : ordenFuentes_) v.emplace_back(n, sourceTypeId(n));
                                 return v;
                             },
                             [this](const std::string& n) { return json::parse(sourceSettingsJson(n), nullptr, false); },
                             [this](const std::string& e, const std::string& el, const std::string& p, const std::string& v) {
                                 return motor_ ? motor_->propiedadEntrada(e, el, p, v) : std::string("no hay motor");
                             },
                             [this](const std::string& e, const std::string& el) {
                                 return motor_ ? motor_->propiedadesEntrada(e, el) : json::object();
                             },
                             [this](const std::string& n, const std::string& tipo, const json& aj, bool monitor,
                                    const std::vector<int>& pistas) {
                                 if (sources_.count(n) > 0) return true;
                                 SourceSpec spec;
                                 spec.id = tipo;
                                 spec.name = n;
                                 spec.settingsJson = aj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                                 if (!addGlobalAudio(spec)) return false;
                                 setSourcePistas(n, pistas);
                                 setSourceMonitoring(n, monitor);
                                 return true;
                             },
                             [this](const std::string& n) { removeGlobalAudio(n); }});
    g_lienzoAncho = video.baseWidth;
    g_lienzoAlto = video.baseHeight;
    g_fps = video.fpsDen > 0 ? static_cast<double>(video.fpsNum) / video.fpsDen : 30.0;
    // Lo que se mide del programa: loudness y picos (audio), brillo y quietud (video).
    oyenteAudio_ = motor_->escucharPrograma([this](const float* d, std::size_t n) {
        static thread_local std::vector<float> l, r;
        l.resize(n);
        r.resize(n);
        float pl = 0, pr = 0;
        for (std::size_t i = 0; i < n; ++i) {
            l[i] = d[2 * i];
            r[i] = d[2 * i + 1];
            pl = std::max(pl, std::fabs(l[i]));
            pr = std::max(pr, std::fabs(r[i]));
        }
        const float* planos[2] = {l.data(), r.data()};
        loudness_->alimentar(planos, 2, static_cast<std::uint32_t>(n));
        for (auto [a, v] : {std::pair{&peakL_, pl}, std::pair{&peakR_, pr}}) {
            float cur = a->load(std::memory_order_relaxed);
            while (v > cur && !a->compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
            }
        }
        if (auto m = medSincro_) {
            // Picos cada 5 ms con la hora en que sonó (ahora es cuando sale).
            const std::int64_t fin = ahoraMs();
            for (std::size_t i = 0; i < n; i += 240) {
                float p = 0;
                for (std::size_t k = i; k < std::min(n, i + 240); ++k) p = std::max(p, std::max(std::fabs(l[k]), std::fabs(r[k])));
                const double db = p > 1e-6f ? 20.0 * std::log10(p) : -120.0;
                const std::int64_t ms = fin - static_cast<std::int64_t>((n - i) * 1000 / 48000);
                std::lock_guard<std::mutex> lk(m->mu);
                m->audio.push_back({ms, db});
            }
        }
    });
    motor_->verPedir("programa@alarmas", 160, 90);
    motor_->verAvisar("programa@alarmas", [this](const motor::Cuadro& c) {
        ++progCuadros_;
        std::uint64_t suma = 0, firma = 1469598103934665603ULL;
        int n = 0;
        std::vector<std::uint8_t> luma;
        const bool sincro = medSincro_ != nullptr;
        if (sincro) luma.resize(static_cast<std::size_t>(c.ancho) * c.alto);
        for (int y = 0; y < c.alto; ++y)
            for (int x = 0; x < c.ancho; ++x) {
                const std::uint8_t* px = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
                const std::uint32_t lu = (px[2] * 54u + px[1] * 183u + px[0] * 19u) >> 8;
                if (sincro) luma[static_cast<std::size_t>(y) * c.ancho + x] = static_cast<std::uint8_t>(lu);
                if (y % 3 == 0 && x % 4 == 0) {
                    suma += lu;
                    ++n;
                    firma = (firma ^ (lu >> 4)) * 1099511628211ULL;
                }
            }
        progLuma_ = n > 0 ? static_cast<double>(suma) / n : 0;
        if (firma != progFirma_.load() || progCambioMs_.load() == 0) {
            progFirma_ = firma;
            progCambioMs_ = ahoraMs();
        }
        if (auto m = medSincro_) {
            std::lock_guard<std::mutex> lk(m->mu);
            if (!m->anterior.empty() && m->anterior.size() == luma.size()) {
                // El bloque (16×9) que más cambió: unas manos que aplauden son chicas en el cuadro.
                double maximo = 0;
                for (int by = 0; by + 9 <= c.alto; by += 9)
                    for (int bx = 0; bx + 16 <= c.ancho; bx += 16) {
                        int s = 0;
                        for (int y = by; y < by + 9; ++y)
                            for (int x = bx; x < bx + 16; ++x) {
                                const std::size_t i = static_cast<std::size_t>(y) * c.ancho + x;
                                s += std::abs(static_cast<int>(luma[i]) - static_cast<int>(m->anterior[i]));
                            }
                        maximo = std::max(maximo, s / (16.0 * 9.0));
                    }
                m->video.push_back({ahoraMs(), maximo});
            }
            m->anterior.swap(luma);
        }
    });
    return true;
}

void BroadcastEngine::shutdown() {
    {
        std::lock_guard<std::mutex> lc(capturaMu_);
        if (motor_)
            for (const auto& [clave, v] : capturas_) motor_->verSoltar(clave);
        capturas_.clear();
    }
    if (!started_) return;
    // Las capturas que están esperando un cuadro (capturarAparte) usan el motor.
    for (int i = 0; i < 80 && capturasEnCurso_.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    started_ = false;
    stopAllSourceRecordings();
    stopCleanFeed();
    stopExtras();
    stopStreaming();
    stopRecording();
    detenerSalidas(); // corre lo encolado (las paradas) y se apaga
    stopReturnFeeds();
    disablePreview();
    for (const auto& [canal, v] : scenePreviews_) motor_->verSoltar(v.clave);
    scenePreviews_.clear();
    motor_->verAvisar("programa@alarmas", nullptr);
    motor_->verSoltar("programa@alarmas");
    if (oyenteAudio_ != 0) motor_->dejarDeEscuchar(oyenteAudio_);
    oyenteAudio_ = 0;
    fuentes::ponerMotorApp(nullptr);
    fuentes::ponerSalida(nullptr);
    fuentes::ponerCambiar(nullptr);
    fuentes::ponerConsultas({});
    fuentes::bajaTodas();
    motor_->detener();
    motor_.reset();
    scenes_.clear();
    sources_.clear();
    ordenFuentes_.clear();
    micInputs_.clear();
    currentScene_.clear();
    for (auto& d : dsk_) d = Dsk{};
    ftb_ = placa_ = ftbAudio_ = false;
}

BroadcastEngine::ProgramaVideo BroadcastEngine::programaVideo() const {
    ProgramaVideo v;
    const std::int64_t c = progCambioMs_.load();
    if (c == 0) return v;
    v.valido = true;
    v.luma = progLuma_.load();
    v.quietoSeg = static_cast<double>(ahoraMs() - c) / 1000.0;
    return v;
}

json BroadcastEngine::latenciaEstudio() const {
    json out{{"latencia_ms", 0}, {"tope_ms", 0}, {"automatico", true}, {"tarde", json::array()}};
    if (!motor_) return out;
    motor::Reloj& r = motor_->reloj();
    out["latencia_ms"] = r.latencia() / 1000000;
    out["tope_ms"] = r.topeMs();
    out["automatico"] = r.topeAutomatico();
    for (const auto& [quien, ns] : r.anotadas())
        if (r.llegaTarde(ns)) out["tarde"].push_back({{"entrada", quien}, {"latencia_ms", ns / 1000000}});
    return out;
}

void BroadcastEngine::ponerTopeEstudio(long long ms) {
    if (motor_) motor_->ponerTopeEstudio(ms);
}

int BroadcastEngine::enviarCaption(const std::string&, double) { return 0; }

// ============================================================================
// El programa hacia el Proceso B
// ============================================================================

bool BroadcastEngine::publishReturnFeeds() {
    if (!started_) {
        setError("publishReturnFeeds sin startup");
        return false;
    }
    if (returnFeedsActive_) return true;
    // El audio (BA.Audio.ContentForReturn) lo escribe el motor desde que arranca.
    ba::ipc::SlotConfig vcfg{video_.outputWidth, video_.outputHeight};
    if (!programVideo_.publish(kProgramVideoName, vcfg)) {
        setError("no se pudo publicar el canal de video de programa");
        return false;
    }
    const std::string e = motor_->verPedir("programa@gateway", video_.outputWidth, video_.outputHeight);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    motor_->verAvisar("programa@gateway", [this](const motor::Cuadro& c) { enviarRetorno(c); });
    returnFeedsActive_ = true;
    return true;
}

void BroadcastEngine::enviarRetorno(const motor::Cuadro& c) {
    if (!programVideo_.published() || c.ancho != video_.outputWidth || c.alto != video_.outputHeight) return;
    const std::size_t fila = static_cast<std::size_t>(c.ancho) * 4;
    const std::uint8_t* px = c.bgra.data();
    if (static_cast<std::size_t>(c.stride) != fila) {
        programPacked_.resize(fila * static_cast<std::size_t>(c.alto));
        for (int y = 0; y < c.alto; ++y)
            std::memcpy(programPacked_.data() + fila * y, c.bgra.data() + static_cast<std::size_t>(c.stride) * y, fila);
        px = programPacked_.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++programFrameIndex_;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    programVideo_.send(px, fila * static_cast<std::size_t>(c.alto), meta);
}

void BroadcastEngine::retornoEnUso(bool si) {
    if (!returnFeedsActive_ || !motor_ || si == !retornoPausado_) return;
    if (si) {
        if (motor_->verPedir("programa@gateway", video_.outputWidth, video_.outputHeight).empty())
            motor_->verAvisar("programa@gateway", [this](const motor::Cuadro& c) { enviarRetorno(c); });
    } else {
        motor_->verAvisar("programa@gateway", nullptr);
        motor_->verSoltar("programa@gateway");
    }
    retornoPausado_ = !si;
}

void BroadcastEngine::alFallarMotor(std::function<void(const std::string&)> fn) {
    std::lock_guard<std::mutex> l(falloMotorM_);
    falloMotor_ = std::move(fn);
}

void BroadcastEngine::alCambiarAudioFuente(std::function<void(const std::string&, const char*)> fn) {
    cambioAudio_ = std::move(fn);
}

bool BroadcastEngine::escenaACanal(int k, const std::string& nombre) {
    if (k < 0 || k >= ba::ipc::kCanalesEscena) {
        setError("canal de escena fuera de rango");
        return false;
    }
    CanalEscena& c = canalesEscena_[static_cast<std::size_t>(k)];
    if (c.escena == nombre) return true;
    if (!c.clave.empty() && motor_) {
        motor_->verAvisar(c.clave, nullptr);
        motor_->verSoltar(c.clave);
    }
    {
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.escena.clear();
        c.clave.clear();
    }
    if (nombre.empty()) return true;
    if (!started_ || !motor_) {
        setError("escena para pantallas: el motor no arranco");
        return false;
    }
    if (escena(nombre) == nullptr) {
        setError("escena para pantallas: no existe '" + nombre + "'");
        return false;
    }
    // Lo que el gateway le manda a un televisor es a lo sumo 1280x720 (ver
    // MediaPump::Cfg::programVideoW): componer más grande solo gasta.
    const int w = std::min(1280, video_.baseWidth);
    const int h = std::max(2, (w * video_.baseHeight / std::max(1, video_.baseWidth)) & ~1);
    if (!c.sender || c.ancho != w || c.alto != h) {
        // Un canal publicado no cambia de tamaño (ver SpoutSender::publish):
        // con otro lienzo se rehace, y el gateway se vuelve a suscribir solo.
        auto s = std::make_unique<ba::ipc::SpoutSender>();
        if (!s->publish(ba::ipc::nombreCanalEscena(k).c_str(), ba::ipc::SlotConfig{w, h})) {
            setError("no se pudo publicar el canal de escena " + std::to_string(k));
            return false;
        }
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.sender = std::move(s);
        c.ancho = w;
        c.alto = h;
    }
    const std::string clave = "escena:" + nombre + "@pantallas" + std::to_string(k);
    const std::string e = motor_->verPedir(clave, w, h);
    if (!e.empty()) {
        setError("escena para pantallas: " + e);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.escena = nombre;
        c.clave = clave;
    }
    motor_->verAvisar(clave, [this, k](const motor::Cuadro& q) { enviarEscena(k, q); });
    diag::registro().linea(diag::Nivel::Info, "pantallas", 0,
                           "escena «" + nombre + "» en el canal " + std::to_string(k) + " (" + std::to_string(w) +
                               "x" + std::to_string(h) + ")");
    return true;
}

std::string BroadcastEngine::escenaDeCanal(int k) const {
    if (k < 0 || k >= ba::ipc::kCanalesEscena) return {};
    return canalesEscena_[static_cast<std::size_t>(k)].escena;
}

int BroadcastEngine::tomarCanalEscena(const std::string& escena, const std::string& dueno) {
    if (escena.empty()) return -1;
    int canal = -1;
    for (int k = 0; k < ba::ipc::kCanalesEscena && canal < 0; ++k)
        if (escenaDeCanal(k) == escena) canal = k;
    for (int k = 0; k < ba::ipc::kCanalesEscena && canal < 0; ++k)
        if (escenaDeCanal(k).empty()) canal = k;
    if (canal < 0) return -1;
    if (escenaDeCanal(canal).empty() && !escenaACanal(canal, escena)) return -1;
    duenosEscena_[static_cast<std::size_t>(canal)].insert(dueno);
    return canal;
}

void BroadcastEngine::soltarCanalesEscena(const std::string& dueno) {
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) {
        auto& d = duenosEscena_[static_cast<std::size_t>(k)];
        if (d.erase(dueno) > 0 && d.empty()) escenaACanal(k, ""); // Pantallas lo vuelve a pedir si lo usa
    }
}

bool BroadcastEngine::canalEscenaConDuenos(int k) const {
    return k >= 0 && k < ba::ipc::kCanalesEscena && !duenosEscena_[static_cast<std::size_t>(k)].empty();
}

void BroadcastEngine::enviarEscena(int k, const motor::Cuadro& q) {
    std::lock_guard<std::mutex> l(canalesEscenaM_);
    CanalEscena& c = canalesEscena_[static_cast<std::size_t>(k)];
    if (c.escena.empty() || !c.sender || q.ancho != c.ancho || q.alto != c.alto) return;
    const std::size_t fila = static_cast<std::size_t>(q.ancho) * 4;
    const std::uint8_t* px = q.bgra.data();
    if (static_cast<std::size_t>(q.stride) != fila) {
        c.filas.resize(fila * static_cast<std::size_t>(q.alto));
        for (int y = 0; y < q.alto; ++y)
            std::memcpy(c.filas.data() + fila * y, q.bgra.data() + static_cast<std::size_t>(q.stride) * y, fila);
        px = c.filas.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++c.cuadros;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    c.sender->send(px, fila * static_cast<std::size_t>(q.alto), meta);
}

void BroadcastEngine::stopReturnFeeds() {
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) escenaACanal(k, "");
    if (!returnFeedsActive_ || !motor_) return;
    if (!retornoPausado_) {
        motor_->verAvisar("programa@gateway", nullptr);
        motor_->verSoltar("programa@gateway");
    }
    retornoPausado_ = false;
    returnFeedsActive_ = false;
}

// ============================================================================
// Vistas para la interfaz
// ============================================================================

bool BroadcastEngine::enablePreview(int width, int height) {
    if (!started_ || width <= 0 || height <= 0) {
        setError("enablePreview sin startup o con tamaño invalido");
        return false;
    }
    if (previewActive_) return true;
    const std::string e = motor_->verPedir("programa@ui", width, height);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    previewActive_ = true;
    return true;
}

void BroadcastEngine::disablePreview() {
    if (!previewActive_ || !motor_) return;
    motor_->verSoltar("programa@ui");
    previewActive_ = false;
}

bool BroadcastEngine::latestPreview(std::vector<std::uint8_t>& dst, int& width, int& height, int& stride,
                                    std::uint64_t& serial) const {
    if (!previewActive_ || !motor_) return false;
    motor::Cuadro c;
    if (!motor_->verUltimo("programa@ui", c, serial)) return false;
    dst = std::move(c.bgra);
    width = c.ancho;
    height = c.alto;
    stride = c.stride;
    return true;
}

void BroadcastEngine::programPeaksDb(float& left, float& right) {
    const auto aDb = [](float lin) { return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin); };
    left = aDb(peakL_.exchange(0.0f));
    right = aDb(peakR_.exchange(0.0f));
}

bool BroadcastEngine::enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) {
    if (canal == "armada" && started_ && motor_) {
        // LO ARMADO ES EL BUS DE PREVIEW DEL MOTOR: una composición que vive
        // siempre y a la que armar le cambia la escena con un corte (las capas
        // nuevas reemplazan a las viejas cuando ya tienen cuadro). Antes cada
        // escena armada tenía su propia vista y su propia composición: en cada
        // TAKE se soltaba una y se armaba otra de cero (ramas, shaders) y el
        // preview parpadeaba en negro o sin capas, y además iba a 10-15 cuadros/s.
        const std::string e = motor_->armar(name);
        if (!e.empty()) {
            setError("armar: " + e);
            return false;
        }
        if (name.empty()) {
            disableScenePreview(canal);
            return true;
        }
        auto ya = scenePreviews_.find(canal);
        if (ya != scenePreviews_.end()) {
            ya->second.name = name;
            return true;
        }
        const int w = maxWidth > 0 ? std::min(maxWidth, video_.baseWidth) : video_.baseWidth;
        const int h = std::max(2, w * video_.baseHeight / std::max(1, video_.baseWidth));
        VistaPrevia v;
        v.clave = "preview@armada";
        v.name = name;
        const std::string ev = motor_->verPedir(v.clave, w, h);
        if (!ev.empty()) {
            setError("vista previa: " + ev);
            return false;
        }
        scenePreviews_[canal] = v;
        medirVista(v.clave);
        return true;
    }
    auto it = scenePreviews_.find(canal);
    if (it != scenePreviews_.end() && it->second.name == name) return true;
    disableScenePreview(canal);
    if (name.empty()) return true;
    if (!started_) {
        setError("vista previa de escena: el motor no arranco");
        return false;
    }
    const std::string base = escena(name) != nullptr ? "escena:" + name : sources_.count(name) > 0 ? "entrada:" + name : "";
    if (base.empty()) {
        setError("vista previa de escena: no existe '" + name + "'");
        return false;
    }
    const int w = maxWidth > 0 ? std::min(maxWidth, video_.baseWidth) : video_.baseWidth;
    const int h = std::max(2, w * video_.baseHeight / std::max(1, video_.baseWidth));
    VistaPrevia v;
    v.clave = base + "@" + canal;
    v.name = name;
    const std::string e = motor_->verPedir(v.clave, w, h);
    if (!e.empty()) {
        setError("vista previa: " + e);
        return false;
    }
    scenePreviews_[canal] = v;
    medirVista(v.clave);
    return true;
}

void BroadcastEngine::medirVista(const std::string& clave) {
    // BA_MEDIR_PREVIEW=1: cada cuadro de la vista, con su brillo medio y cuánto
    // tardó desde el anterior, al registro (para encontrar parpadeos: un cuadro
    // negro, a medio armar o que falta alrededor de un TAKE).
    const char* medir = std::getenv("BA_MEDIR_PREVIEW");
    if (medir == nullptr || std::string(medir) != "1" || !motor_) return;
    auto antes = std::make_shared<std::int64_t>(0);
    motor_->verAvisar(clave, [clave, antes](const motor::Cuadro& c) {
        std::uint64_t suma = 0, n = 0;
        for (int y = 0; y < c.alto; y += 8)
            for (int x = 0; x < c.ancho; x += 8) {
                const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
                suma += (p[0] + 2 * p[1] + p[2]) / 4;
                ++n;
            }
        const std::int64_t ahora = g_get_monotonic_time();
        const std::int64_t dt = *antes ? (ahora - *antes) / 1000 : 0;
        *antes = ahora;
        ba::diag::registro().info("medir " + clave + ": brillo " + std::to_string(n ? suma / n : 0) + " dt " +
                                  std::to_string(dt) + " ms serie " + std::to_string(c.serie));
    });
}

void BroadcastEngine::disableScenePreview(const std::string& canal) {
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end()) return;
    if (motor_) motor_->verSoltar(it->second.clave);
    scenePreviews_.erase(it);
}

std::string BroadcastEngine::scenePreviewName(const std::string& canal) const {
    auto it = scenePreviews_.find(canal);
    return it == scenePreviews_.end() ? std::string() : it->second.name;
}

bool BroadcastEngine::renderScenePreview(const std::string& canal, std::vector<std::uint8_t>& dst, int& width,
                                         int& height, int& stride, std::uint64_t& serial) {
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end() || !motor_) return false;
    // Solo si hay uno nuevo: antes daba siempre el último y la interfaz lo
    // copiaba y le daba vuelta los colores 30 veces por segundo aunque fuera el
    // mismo (la serie de las vistas es una sola y solo crece).
    motor::Cuadro c;
    if (!motor_->verUltimo(it->second.clave, c, serial)) return false;
    dst = std::move(c.bgra);
    width = c.ancho;
    height = c.alto;
    stride = c.stride;
    return true;
}

void BroadcastEngine::capturarAparte(const std::string& name, int maxWidth, Captura listo) {
    std::string clave;
    int w = 0, h = 0;
    bool esEscena = false;
    const std::string e = prepararCaptura(name, maxWidth, clave, w, h, esEscena);
    if (!e.empty()) {
        listo("captura: " + e, {}, 0, 0);
        return;
    }
    ++capturasEnCurso_;
    std::thread([this, clave, w, h, esEscena, listo = std::move(listo)]() {
        std::vector<std::uint8_t> rgba;
        int ancho = 0, alto = 0;
        const std::string err = capturarVista(clave, w, h, esEscena, rgba, ancho, alto);
        listo(err.empty() ? std::string() : "captura: " + err, std::move(rgba), ancho, alto);
        --capturasEnCurso_;
    }).detach();
}

bool BroadcastEngine::captureSource(const std::string& name, int maxWidth, std::vector<std::uint8_t>& rgba, int& width,
                                    int& height) {
    std::string clave;
    int w = 0, h = 0;
    bool esEscena = false;
    std::string e = prepararCaptura(name, maxWidth, clave, w, h, esEscena);
    if (e.empty()) e = capturarVista(clave, w, h, esEscena, rgba, width, height);
    if (!e.empty()) {
        setError("captura: " + e);
        return false;
    }
    return true;
}

std::string BroadcastEngine::prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                             bool& esEscena) const {
    if (!started_) return "el motor no arranco";
    const std::string base = name.empty()                 ? std::string("programa")
                             : escena(name) != nullptr    ? "escena:" + name
                             : sources_.count(name) > 0   ? "entrada:" + name
                                                          : std::string();
    if (base.empty()) return "no existe la fuente o escena '" + name + "'";
    w = video_.baseWidth;
    h = video_.baseHeight;
    if (!name.empty() && base.rfind("entrada:", 0) == 0) {
        int sw = 0, sh = 0;
        if (sourceSize(name, sw, sh) && sw > 0 && sh > 0) {
            w = sw;
            h = sh;
        }
    }
    if (maxWidth > 0 && w > maxWidth) {
        h = std::max(2, h * maxWidth / w);
        w = maxWidth;
    }
    w &= ~1;
    h &= ~1;
    esEscena = base.rfind("escena:", 0) == 0;
    clave = base + "@captura";
    return {};
}

std::string BroadcastEngine::capturarVista(const std::string& clave, int w, int h, bool esEscena,
                                           std::vector<std::uint8_t>& rgba, int& width, int& height) {
    std::lock_guard<std::mutex> lc(capturaMu_);
    const auto ahora = std::chrono::steady_clock::now();
    // Las que nadie pidió en 10 s, afuera.
    for (auto it = capturas_.begin(); it != capturas_.end();)
        if (it->first != clave && ahora - it->second.uso > std::chrono::seconds(ajustes::entero("video.vistaReposoS", 10))) {
            motor_->verSoltar(it->first);
            it = capturas_.erase(it);
        } else {
            ++it;
        }
    // La vista de esta fuente: si ya está con este tamaño, se reusa (sin armar nada).
    auto viva = capturas_.find(clave);
    const bool nueva = viva == capturas_.end() || viva->second.ancho != w || viva->second.alto != h;
    if (nueva) {
        if (viva != capturas_.end()) motor_->verSoltar(clave);
        if (!motor_->verPedir(clave, w, h).empty()) {
            capturas_.erase(clave);
            return "'" + clave.substr(0, clave.find('@')) + "' no tiene imagen";
        }
        capturas_[clave] = {w, h, ahora};
    }
    capturas_[clave].uso = ahora;
    motor::Cuadro c;
    std::uint64_t s = 0;
    bool ok = false;
    // Una escena que no está al aire se compone recién ahora: sus primeros cuadros
    // pueden salir sin las capas que todavía arrancan (en la GPU, compilar los
    // shaders tarda): se toma un cuadro de después. Una vista ya viva, al toque.
    const auto desde = std::chrono::steady_clock::now();
    const auto espera = nueva && esEscena ? std::chrono::milliseconds(ajustes::entero("video.esperaCapturaMs", 700)) : std::chrono::milliseconds(0);
    for (int i = 0; i < 60; ++i) {
        const bool hay = motor_->verUltimo(clave, c, s);
        ok = ok || hay;
        if (ok && std::chrono::steady_clock::now() - desde >= espera) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ok) {
        motor_->verSoltar(clave);
        capturas_.erase(clave);
        return "'" + clave.substr(0, clave.find('@')) + "' no dio imagen (sin señal o apagada)";
    }
    aRgba(c, rgba);
    width = c.ancho;
    height = c.alto;
    return {};
}

// ============================================================================
// Transmisión y destinos
// ============================================================================

std::string BroadcastEngine::destinoActivo() const { return enmascarar(activeServer_, activeKey_); }

namespace {
/// Lo que leen las salidas del programa: su video y su audio (pista 1 y las pedidas).
motor::Fuentes fuentesDelPrograma(motor::Motor& m, const std::vector<int>& pistasExtra, bool limpio = false) {
    motor::Fuentes f;
    f.video = m.productorDeVideo(limpio ? "limpio" : "programa", &f.latVideoNs);
    std::int64_t lat = 0;
    f.audios.push_back(m.productorDeBus("programa", &lat));
    f.latAudioNs = lat;
    for (int p : pistasExtra) {
        std::int64_t l = 0;
        const std::string prod = m.productorDeBus("pista" + std::to_string(p), &l);
        if (!prod.empty()) {
            f.audios.push_back(prod);
            f.latAudioNs = std::max(f.latAudioNs, l);
        }
    }
    return f;
}
} // namespace

struct BroadcastEngine::Repeticion {
    std::unique_ptr<motor::Repeticion> rep;
    std::string dir;
    mutable std::mutex mu;
    std::string ultimo;
    std::atomic<bool> guardando{false};
};

// ============================================================================
// El hilo de salidas (docs/planes/PLAN-MOTOR-SIN-ESPERAS.md, S4)
//
// Transmisión, destinos extra, grabación (y su copia), repetición, grabaciones
// por entrada y señal limpia viven en este hilo: arrancarlas, pararlas y
// atenderlas (una transmisión caída se rearma probando codificadores; parar una
// grabación espera el cierre del archivo) son cambios de estado que tardan. La
// interfaz encola (enSalidas) y lee la foto que este hilo publica cada 100 ms.
// Lo que no arrancó vuelve como aviso (avisarFallo → alFallarMotor).
// ============================================================================

void BroadcastEngine::enSalidas(const std::string& que, std::function<std::string()> f) {
    {
        std::lock_guard<std::mutex> l(salidasColaM_);
        salidasCola_.emplace_back(que, std::move(f));
    }
    salidasCv_.notify_one();
}

void BroadcastEngine::avisarFallo(const std::string& texto) {
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "salidas", 0, texto);
    std::function<void(const std::string&)> fn;
    {
        std::lock_guard<std::mutex> l(falloMotorM_);
        fn = falloMotor_;
    }
    if (fn) fn(texto);
}

void BroadcastEngine::bucleSalidas() {
    for (;;) {
        std::deque<std::pair<std::string, std::function<std::string()>>> ordenes;
        bool seguir = true;
        {
            std::unique_lock<std::mutex> l(salidasColaM_);
            salidasCv_.wait_for(l, std::chrono::milliseconds(100),
                                [this]() { return !salidasCola_.empty() || !salidasSeguir_; });
            ordenes.swap(salidasCola_);
            seguir = salidasSeguir_;
        }
        for (auto& [que, f] : ordenes) {
            const std::string e = f();
            if (!e.empty()) avisarFallo(e);
        }
        if (!seguir && ordenes.empty()) break;
        ticSalidas();
        publicarFotoSalidas();
    }
    publicarFotoSalidas();
}

void BroadcastEngine::detenerSalidas() {
    if (!hiloSalidas_.joinable()) return;
    // Lo que quede andando se para EN el hilo de salidas (después de lo encolado).
    enSalidas("apagar", [this]() {
        for (auto& [n, r] : sourceRecs_)
            if (r.grab) r.grab->parar();
        sourceRecs_.clear();
        if (clean_) {
            if (clean_->tx) clean_->tx->parar();
            if (clean_->grab) clean_->grab->parar();
            clean_.reset();
        }
        for (auto& [id, x] : extras_)
            if (x.tx) x.tx->parar();
        extras_.clear();
        if (stream_) stream_->parar();
        stream_.reset();
        stopRecordingYa();
        replay_.reset();
        return std::string();
    });
    {
        std::lock_guard<std::mutex> l(salidasColaM_);
        salidasSeguir_ = false;
    }
    salidasCv_.notify_one();
    hiloSalidas_.join();
    std::lock_guard<std::mutex> l(pedidosM_);
    streamPedido_ = grabacionPedida_ = limpioPedido_ = false;
    extrasPedidos_.clear();
    individualesPedidas_.clear();
}

nlohmann::json BroadcastEngine::fotoSalidas() const {
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    return fotoSalidas_;
}

void BroadcastEngine::publicarFotoSalidas() {
    json f = json::object();
    f["principal"] = stream_ ? stream_->estado() : json::object();
    json ex = json::object();
    for (const auto& [id, x] : extras_) {
        json st = x.tx ? x.tx->estado() : json::object();
        st["destino"] = enmascarar(x.server, x.key);
        ex[id] = std::move(st);
    }
    f["extras"] = std::move(ex);
    f["grabacion"] = {{"activa", rec_ && rec_->grabando()},
                      {"estado", rec_ ? rec_->estado() : json::object()},
                      {"archivo", recordPath_},
                      {"copia", recordCopyPath_},
                      {"copia_activa", recCopia_ && recCopia_->grabando()}};
    std::string ultimo;
    if (replay_) {
        std::lock_guard<std::mutex> l(replay_->mu);
        ultimo = replay_->ultimo;
    }
    f["replay"] = {{"activa", replay_ && replay_->rep->andando()},
                   {"segundos", replay_ ? replay_->rep->segundos() : 0},
                   {"ultimo", ultimo}};
    json ind = json::array();
    for (const auto& [n, r] : sourceRecs_)
        ind.push_back({{"nombre", n}, {"path", r.path}, {"audio_propio", r.audioPropio}, {"grabando", r.grab && r.grab->grabando()},
                       {"bytes", r.grab ? r.grab->estado().value("bytes", std::uint64_t{0}) : std::uint64_t{0}},
                       {"pausada", r.grab && r.grab->pausada()}});
    f["individuales"] = std::move(ind);
    if (clean_) {
        const json st = clean_->tx ? clean_->tx->estado() : json::object();
        f["limpio"] = {{"activa", clean_->tx ? st.value("andando", false) : (clean_->grab && clean_->grab->grabando())},
                       {"reconectando", st.value("reconectando", false)},
                       {"destino", clean_->destino},
                       {"archivo", clean_->path},
                       {"cuadros", st.value("cuadros", 0)},
                       {"perdidos", st.value("perdidos", 0)}};
    } else {
        f["limpio"] = {{"activa", false}};
    }
    f["disco"] = {{"apuros", discoApuros_},
                  {"atraso_s", atrasoDisco_},
                  {"registro_trabado_ms", registroTrabadoMs_},
                  {"prioridad", prioridad_},
                  {"automatica", prioridadAuto_},
                  {"modo", modoDisco()},
                  {"pausadas", pausadas_},
                  {"graba_programa", rec_ && rec_->grabando()},
                  {"desde_s", discoApuros_ ? (ahoraMs() - apurosDesdeMs_) / 1000 : 0}};
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    fotoSalidas_ = std::move(f);
}

bool BroadcastEngine::startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente) {
    if (!started_) {
        setError("startStreaming sin startup");
        ultimoInicio_ = Inicio::Error;
        return false;
    }
    bool estaba = false;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        estaba = streamPedido_;
        if (estaba) {
            const bool mismo = (stream.server.empty() || stream.server == activeServer_) && (stream.key.empty() || stream.key == activeKey_);
            if (mismo) {
                ultimoInicio_ = Inicio::YaIgual;
                return true;
            }
        }
    }
    if (estaba) {
        if (!cambiarEnCaliente) {
            ultimoInicio_ = Inicio::YaOtroDestino;
            setError("ya está transmitiendo a " + destinoActivo() + "; para cambiar a " + enmascarar(stream.server, stream.key) +
                     " hay que pedirlo explícitamente (cambiar en caliente)");
            return false;
        }
        stopStreaming();
    }
    ultimoInicio_ = Inicio::Error;
    if (stream.server.empty()) {
        setError("startStreaming sin servidor");
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        streamPedido_ = true;
        activeServer_ = stream.server;
        activeKey_ = stream.key;
    }
    ultimoInicio_ = Inicio::Iniciado;
    enSalidas("startStreaming", [this, stream, enc]() { return startStreamingYa(stream, enc); });
    return true;
}

std::string BroadcastEngine::startStreamingYa(const StreamConfig& stream, const EncoderConfig& enc) {
    motor::Transmision::Config c;
    c.url = urlConClave(stream.server, stream.key);
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    c.codificadores = videoEncoderCandidates(enc);
    auto tx = std::make_unique<motor::Transmision>(motor_->reloj(), c);
    const std::string e = tx->iniciar(fuentesDelPrograma(*motor_, {}));
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        streamPedido_ = false;
        return "no se pudo iniciar la transmisión: " + e;
    }
    stream_ = std::move(tx);
    return {};
}

void BroadcastEngine::stopStreaming() {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        streamPedido_ = false;
    }
    enSalidas("stopStreaming", [this]() {
        if (stream_) stream_->parar();
        stream_.reset();
        return std::string();
    });
}

bool BroadcastEngine::startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    if (server.empty()) {
        setError("el destino no tiene servidor");
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (!extrasPedidos_.insert(id).second) return true;
    }
    enSalidas("startExtra", [this, id, server, key, enc]() { return startExtraYa(id, server, key, enc); });
    return true;
}

std::string BroadcastEngine::startExtraYa(const std::string& id, const std::string& server, const std::string& key,
                                          const EncoderConfig& enc) {
    auto it = extras_.find(id);
    if (it != extras_.end() && it->second.tx && it->second.tx->andando()) return {};
    if (it != extras_.end()) {
        if (it->second.tx) it->second.tx->parar();
        extras_.erase(it);
    }
    motor::Transmision::Config c;
    c.url = urlConClave(server, key);
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    c.codificadores = videoEncoderCandidates(enc);
    Extra x;
    x.server = server;
    x.key = key;
    x.tx = std::make_unique<motor::Transmision>(motor_->reloj(), c);
    const std::string e = x.tx->iniciar(fuentesDelPrograma(*motor_, {}));
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        extrasPedidos_.erase(id);
        return "el destino «" + id + "» no arrancó: " + e;
    }
    extras_[id] = std::move(x);
    return {};
}

void BroadcastEngine::stopExtra(const std::string& id) {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        extrasPedidos_.erase(id);
    }
    enSalidas("stopExtra", [this, id]() {
        auto it = extras_.find(id);
        if (it == extras_.end()) return std::string();
        if (it->second.tx) it->second.tx->parar();
        extras_.erase(it);
        return std::string();
    });
}

void BroadcastEngine::stopExtras() {
    std::set<std::string> ids;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        ids = extrasPedidos_;
    }
    for (const auto& id : ids) stopExtra(id);
}

nlohmann::json BroadcastEngine::extrasEstado() const {
    json out = json::object();
    const json ex = fotoSalidas().value("extras", json::object());
    for (const auto& [id, st] : ex.items())
        out[id] = {{"activo", st.value("andando", false)},
                   {"reconectando", st.value("reconectando", false)},
                   {"perdidos", st.value("perdidos", 0)},
                   {"cuadros", st.value("cuadros", 0)},
                   {"bytes", st.value("bytes", std::uint64_t{0})},
                   {"kbps", st.value("kbps", 0.0)},
                   {"destino", st.value("destino", std::string())}};
    return out;
}

nlohmann::json BroadcastEngine::saludSalidas() const {
    const auto de = [](const json& st) {
        return json{{"activo", st.value("andando", false)},
                    {"reconectando", st.value("reconectando", false)},
                    {"bytes", st.value("bytes", std::uint64_t{0})},
                    {"perdidos", st.value("perdidos", 0)},
                    {"cuadros", st.value("cuadros", 0)},
                    {"congestion", 0.0}};
    };
    const json f = fotoSalidas();
    const json extras = f.value("extras", json::object()); // nunca .items() de un temporal
    json ex = json::object();
    for (const auto& [id, st] : extras.items()) ex[id] = de(st);
    return {{"principal", de(f.value("principal", json::object()))}, {"extras", ex}, {"saltados_encoder", 0}, {"lag_render", 0}};
}

// ============================================================================
// Grabación
// ============================================================================

struct BroadcastEngine::EstadoRemux {
    std::mutex mu;
    std::string archivo, destino, error;
    std::atomic<float> pct{0};
    std::atomic<bool> andando{false}, ok{false};
};

bool BroadcastEngine::startRecording(const RecordConfig& rec, const EncoderConfig& enc) {
    if (!started_) {
        setError("startRecording sin startup");
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (grabacionPedida_) return true;
        grabacionPedida_ = true;
    }
    lastRec_ = rec;
    lastRecValido_ = true;
    const std::string pedido = rec.container.empty() ? std::string("mkv") : rec.container;
    // MP4/MOV no sobreviven a un corte: se graba en MKV y se pasa al terminar.
    remuxA_ = pedido == "mp4" || pedido == "mov" ? pedido : std::string();
    remuxAlTerminar_ = rec.remuxAlTerminar;
    std::vector<int> pistas;
    pistasGrabacionMask_ = 0;
    for (int p : rec.pistas)
        if (p >= 2 && p <= 6 && std::find(pistas.begin(), pistas.end(), p) == pistas.end()) {
            pistas.push_back(p);
            pistasGrabacionMask_ |= 1 << (p - 1);
        }
    std::sort(pistas.begin(), pistas.end());
    const std::string nombre = "ba-" + marcaDeTiempo();
    enSalidas("startRecording", [this, rec, enc, pistas, nombre, remuxA = remuxA_, alTerminar = remuxAlTerminar_]() {
        remuxSal_ = remuxA; // lo que mira stopRecordingYa, en este hilo
        remuxAlTerminarSal_ = alTerminar;
        return startRecordingYa(rec, enc, pistas, nombre);
    });
    return true;
}

std::string BroadcastEngine::startRecordingYa(const RecordConfig& rec, const EncoderConfig& enc, const std::vector<int>& pistas,
                                              const std::string& nombre) {
    if (rec_ && rec_->grabando()) return {};
    motor::Grabacion::Config c;
    c.carpeta = rec.directory;
    c.nombre = nombre;
    c.tramoSeg = ajustes::entero("record.tramoMin", 0) * 60; // 0: un archivo por grabación; el MKV ya aguanta un corte
    c.kbps = rec.calidad == "archivo" ? enc.videoBitrateKbps * 3 : enc.videoBitrateKbps;
    c.kbpsAudio = std::max(enc.audioBitrateKbps, rec.calidad == "archivo" ? 256 : 160);
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    c.codificadores = videoEncoderCandidates(enc);
    c.colchonSeg = std::max(5, rec.colchonSeg); // lo que más importa: el disco trabado sin perder nada (Configuración → Grabación)
    auto g = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*motor_, pistas);
    const std::string e = g->iniciar(f);
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        grabacionPedida_ = false;
        return "no se pudo iniciar la grabación: " + e;
    }
    rec_ = std::move(g);
    recordPath_ = (std::filesystem::path(rec.directory) / (c.nombre + "-00000.mkv")).string();
    // La copia en otro disco: otro encode igual (si no arranca, la principal sigue).
    recordCopyPath_.clear();
    if (!rec.copiaDir.empty()) {
        motor::Grabacion::Config cc = c;
        cc.carpeta = rec.copiaDir;
        auto copia = std::make_unique<motor::Grabacion>(motor_->reloj(), cc);
        if (copia->iniciar(f).empty()) {
            recCopia_ = std::move(copia);
            recordCopyPath_ = (std::filesystem::path(rec.copiaDir) / (cc.nombre + "-00000.mkv")).string();
        } else {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "grabacion", 0, "la copia en " + rec.copiaDir + " no arrancó");
        }
    }
    return {};
}

void BroadcastEngine::stopRecording() {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        grabacionPedida_ = false;
    }
    enSalidas("stopRecording", [this]() {
        stopRecordingYa();
        return std::string();
    });
}

void BroadcastEngine::stopRecordingYa() {
    if (recCopia_) {
        recCopia_->parar();
        recCopia_.reset();
    }
    if (!rec_) return;
    const bool estaba = rec_->grabando();
    rec_->parar(); // espera el cierre del archivo: por eso en este hilo
    const auto archivos = rec_->archivos();
    if (!archivos.empty()) recordPath_ = archivos.back();
    rec_.reset();
    // Con la grabación partida en tramos (Configuración → avanzado), se pasan todos.
    if (estaba && !remuxSal_.empty() && remuxAlTerminarSal_ && !archivos.empty()) remuxVarios(archivos, remuxSal_, true);
}

std::string BroadcastEngine::archivoGrabacion() const {
    return fotoSalidas().value("grabacion", json::object()).value("archivo", std::string());
}

std::string BroadcastEngine::archivoCopiaGrabacion() const {
    return fotoSalidas().value("grabacion", json::object()).value("copia", std::string());
}

nlohmann::json BroadcastEngine::grabacionEstado() const {
    json pistas = json::array({1});
    for (int i = 1; i < 6; ++i)
        if (pistasGrabacionMask_ & (1 << i)) pistas.push_back(i + 1);
    const json g = fotoSalidas().value("grabacion", json::object());
    const json st = g.value("estado", json::object());
    return {{"activa", g.value("activa", false)},
            {"archivo", g.value("archivo", std::string())},
            {"copia", g.value("copia", std::string())},
            {"copia_activa", g.value("copia_activa", false)},
            {"calidad", lastRecValido_ ? lastRec_.calidad : std::string("como_stream")},
            {"encoder", st.value("codificador", std::string())},
            {"pistas", pistas},
            {"encoder_respaldo_activo", false}};
}

bool BroadcastEngine::remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) {
    return remuxVarios({archivo}, formato, borrarOrigen);
}

bool BroadcastEngine::remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen) {
    if (archivos.empty()) return false;
    const std::string archivo = archivos.front();
    std::lock_guard<std::mutex> lr(remuxM_); // lo lanza también el hilo de salidas (al parar)
    if (remux_ && remux_->andando) {
        setError("ya hay un remux andando");
        return false;
    }
    auto st = std::make_shared<EstadoRemux>();
    const std::string destino = std::filesystem::path(archivo).replace_extension("." + formato).string();
    st->archivo = archivo;
    st->destino = destino;
    st->andando = true;
    remux_ = st;
    std::thread([st, archivos, formato, borrarOrigen]() {
        bool todos = true;
        for (std::size_t i = 0; i < archivos.size(); ++i) {
            const std::string& origen = archivos[i];
            const std::string dest = std::filesystem::path(origen).replace_extension("." + formato).string();
            {
                std::lock_guard<std::mutex> l(st->mu);
                st->archivo = origen;
                st->destino = dest;
            }
            const double base = static_cast<double>(i) / static_cast<double>(archivos.size());
            const std::string e = motor::remuxar(origen, dest, [&](double p) {
                st->pct = static_cast<float>((base + p / static_cast<double>(archivos.size())) * 100.0);
            });
            // Antes de borrar el MKV: que exista y que no sea mucho más chico.
            std::error_code ec;
            const auto tamO = std::filesystem::file_size(origen, ec);
            const auto tamD = std::filesystem::file_size(dest, ec);
            const bool verificado = e.empty() && !ec && tamD > 0 && tamD * 10 >= tamO * 9;
            if (!verificado) {
                std::lock_guard<std::mutex> l(st->mu);
                st->error = e.empty() ? "el archivo nuevo no se verificó (tamaño)" : e;
                todos = false;
            }
            if (verificado && borrarOrigen) std::filesystem::remove(origen, ec);
        }
        st->ok = todos;
        st->pct = 100;
        st->andando = false;
    }).detach();
    return true;
}

nlohmann::json BroadcastEngine::remuxEstado() const {
    std::shared_ptr<EstadoRemux> r;
    {
        std::lock_guard<std::mutex> lr(remuxM_);
        r = remux_;
    }
    if (!r) return json::object();
    const auto& remux_ = r;
    std::lock_guard<std::mutex> l(remux_->mu);
    return {{"archivo", remux_->archivo}, {"destino", remux_->destino}, {"pct", remux_->pct.load()},
            {"andando", remux_->andando.load()}, {"ok", remux_->ok.load()}, {"error", remux_->error}};
}

// --- Repetición -----------------------------------------------------------------

bool BroadcastEngine::setReplay(int segundos, const std::string& dir, const EncoderConfig& enc) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    enSalidas("setReplay", [this, segundos, dir, enc]() { return setReplayYa(segundos, dir, enc); });
    return true;
}

std::string BroadcastEngine::setReplayYa(int segundos, const std::string& dir, const EncoderConfig& enc) {
    replay_.reset();
    if (segundos <= 0) return {};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto r = std::make_unique<Repeticion>();
    r->dir = dir;
    r->rep = std::make_unique<motor::Repeticion>(motor_->reloj(), (std::filesystem::path(dir) / ".tramos").string(), segundos,
                                                 enc.videoBitrateKbps);
    const std::string e = r->rep->iniciar(fuentesDelPrograma(*motor_, {}));
    if (!e.empty()) return "no arrancó la repetición: " + e;
    if (prioridad_) r->rep->pausar(true);
    replay_ = std::move(r);
    return {};
}

bool BroadcastEngine::guardarReplay() {
    if (!fotoSalidas().value("replay", json::object()).value("activa", false)) {
        setError("la repetición está apagada (replay_ajustes con segundos)");
        return false;
    }
    enSalidas("guardarReplay", [this]() {
        if (!replay_ || !replay_->rep->andando() || replay_->guardando) return std::string();
        Repeticion* r = replay_.get();
        r->guardando = true;
        const std::string destino = (std::filesystem::path(r->dir) / ("repeticion-" + marcaDeTiempo() + ".mkv")).string();
        std::thread([r, destino]() {
            const std::string e = r->rep->guardar(destino);
            {
                std::lock_guard<std::mutex> l(r->mu);
                if (e.empty()) r->ultimo = destino;
            }
            if (!e.empty()) ba::diag::registro().linea(ba::diag::Nivel::Aviso, "repeticion", 0, "no se guardó: " + e);
            r->guardando = false;
        }).detach();
        return std::string();
    });
    return true;
}

std::string BroadcastEngine::ultimoReplay() const {
    return fotoSalidas().value("replay", json::object()).value("ultimo", std::string());
}

nlohmann::json BroadcastEngine::replayEstado() const {
    const json r = fotoSalidas().value("replay", json::object());
    return {{"activa", r.value("activa", false)}, {"segundos", r.value("segundos", 0)}, {"ultimo", r.value("ultimo", std::string())}};
}

void BroadcastEngine::atenderEncoderCaido(bool, bool) {
    // Las salidas se atienden solas en su hilo (ticSalidas, cada 100 ms).
}

void BroadcastEngine::ticSalidas() {
    if (stream_) stream_->tic();
    for (auto& [id, x] : extras_)
        if (x.tx) x.tx->tic();
    if (rec_) rec_->tic();
    if (recCopia_) recCopia_->tic();
    if (replay_) replay_->rep->tic();
    for (auto& [n, r] : sourceRecs_) {
        if (r.grab) r.grab->tic();
        if (r.tx) r.tx->tic();
    }
    if (clean_) {
        if (clean_->grab) clean_->grab->tic();
        if (clean_->tx) clean_->tx->tic();
    }
    vigilarDisco();
}

// --- El disco en apuros ---------------------------------------------------------------------

namespace {
/// ¿Las dos carpetas están en el mismo disco? (Si no se sabe: sí, para no dejar de cuidarlo.)
bool mismoDisco(const std::string& a, const std::string& b) {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    auto raiz = [](const std::string& p) {
        std::string r = fs::path(p).root_name().string();
        for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return r;
    };
    return raiz(a) == raiz(b);
#else
    auto disp = [](fs::path p) -> long long {
        std::error_code ec;
        while (!p.empty() && !fs::exists(p, ec)) p = p.parent_path();
        struct stat st {};
        return ::stat(p.string().c_str(), &st) == 0 ? static_cast<long long>(st.st_dev) : -1;
    };
    const long long da = disp(a), db = disp(b);
    return da < 0 || db < 0 || da == db;
#endif
}

// Umbrales: entra en apuros con 3 s esperando al disco (o el registro trabado
// 3 s escribiendo); sale con todo al día durante 30 s. Los dos primeros se
// ajustan en Configuración → Grabación (ponerUmbralesDisco).
std::atomic<double> kAtrasoApurosS{3.0};
std::atomic<std::int64_t> kRegistroTrabadoMs{3000}, kBienDuranteMs{30000};
constexpr double kAtrasoBienS = 0.5;
} // namespace

void BroadcastEngine::ponerUmbralesDisco(double apurosS, int recuperadoS) {
    kAtrasoApurosS = std::clamp(apurosS, 0.5, 60.0);
    kRegistroTrabadoMs = static_cast<std::int64_t>(kAtrasoApurosS.load() * 1000);
    kBienDuranteMs = static_cast<std::int64_t>(std::clamp(recuperadoS, 5, 3600)) * 1000;
}

std::string BroadcastEngine::modoDisco() const {
    const int m = modoDisco_.load();
    return m == 1 ? "auto" : m == 2 ? "nunca" : "preguntar";
}

std::string BroadcastEngine::setModoDisco(const std::string& modo) {
    if (modo != "preguntar" && modo != "auto" && modo != "nunca") return "modo: preguntar, auto o nunca";
    modoDisco_ = modo == "auto" ? 1 : modo == "nunca" ? 2 : 0;
    std::error_code ec;
    std::filesystem::create_directories(ba::platform::configRoot() / "Config", ec);
    std::ofstream o(ba::platform::configRoot() / "Config" / "disco.json");
    o << json{{"priorizar_programa", modo}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    // En apuros y ahora «auto»: ya.
    if (modo == "auto") enSalidas("modoDisco", [this]() {
            if (discoApuros_ && !prioridad_) aplicarPrioridad(true, true);
            return std::string();
        });
    return {};
}

void BroadcastEngine::priorizarPrograma(bool si) {
    enSalidas("priorizarPrograma", [this, si]() {
        aplicarPrioridad(si, false);
        return std::string();
    });
}

void BroadcastEngine::simularDiscoTrabado(int ms) {
    enSalidas("simularDiscoTrabado", [this, ms]() {
        if (rec_) rec_->simularDiscoTrabado(ms);
        return std::string();
    });
}

nlohmann::json BroadcastEngine::discoEstado() const {
    return fotoSalidas().value("disco", json::object());
}

void BroadcastEngine::aplicarPrioridad(bool si, bool automatica) {
    if (si == prioridad_) return;
    prioridad_ = si;
    prioridadAuto_ = si && automatica;
    pausadas_.clear();
    const std::string dirProg = recordPath_.empty() ? std::string() : std::filesystem::path(recordPath_).parent_path().string();
    auto pausar = [&](motor::Grabacion* g, const std::string& nombre) {
        if (g == nullptr || !g->grabando()) return;
        g->pausar(si);
        if (si) pausadas_.push_back(nombre);
    };
    for (auto& [n, r] : sourceRecs_) pausar(r.grab.get(), "ISO " + n);
    if (replay_ && replay_->rep) {
        replay_->rep->pausar(si);
        if (si && replay_->rep->andando()) pausadas_.push_back("repetición");
    }
    // La copia, solo si va al mismo disco (en otro no molesta, y es el respaldo).
    if (recCopia_ && (!si || dirProg.empty() || mismoDisco(dirProg, std::filesystem::path(recordCopyPath_).parent_path().string())))
        pausar(recCopia_.get(), "copia de la grabación");
    if (clean_ && clean_->grab) pausar(clean_->grab.get(), "señal limpia a disco");
    ba::diag::retenerSalidaEstandar(si);
    if (si) pausadas_.push_back("registro (en memoria)");
    std::string lista;
    for (const auto& p : pausadas_) lista += (lista.empty() ? "" : ", ") + p;
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "disco", 0,
                              si ? std::string("el disco, para la grabación del programa") + (automatica ? " (solo)" : "") +
                                       ": en pausa " + lista
                                 : "el disco vuelve a ser de todos: siguen las grabaciones pausadas y el registro");
}

void BroadcastEngine::vigilarDisco() {
    const std::int64_t ahora = ahoraMs();
    double atraso = 0;
    auto mirar = [&](const motor::Grabacion* g) {
        if (g != nullptr && g->grabando() && !g->pausada()) atraso = std::max(atraso, g->atrasoS());
    };
    mirar(rec_.get());
    mirar(recCopia_.get());
    for (auto& [n, r] : sourceRecs_) mirar(r.grab.get());
    if (clean_) mirar(clean_->grab.get());
    if (replay_ && replay_->rep) atraso = std::max(atraso, replay_->rep->atrasoS());
    const ba::diag::EstadoSalida reg = ba::diag::estadoSalidaEstandar();
    atrasoDisco_ = atraso;
    registroTrabadoMs_ = reg.escribiendoMs;
    const bool mal = atraso > kAtrasoApurosS || reg.escribiendoMs > kRegistroTrabadoMs;
    const bool bien = atraso < kAtrasoBienS && reg.escribiendoMs < 500;
    if (!discoApuros_ && mal) {
        discoApuros_ = true;
        apurosDesdeMs_ = ahora;
        discoBienDesdeMs_ = 0;
        char t[64];
        std::snprintf(t, sizeof t, "%.0f s", atraso);
        const std::string causa = atraso > kAtrasoApurosS ? std::string("lo grabado espera ") + t + " para entrar al disco"
                                                          : "el registro no puede escribir";
        const bool grabaPrograma = rec_ && rec_->grabando();
        if (modoDisco_.load() == 1 && grabaPrograma) {
            aplicarPrioridad(true, true);
            avisarFallo("El disco no da abasto (" + causa + "): se le dio el disco a la grabación del programa. "
                        "Las ISO, la repetición y el registro siguen cuando el disco se recupere.");
        } else {
            avisarFallo("El disco no da abasto (" + causa + ")." +
                        (grabaPrograma && modoDisco_.load() == 0
                             ? " Conviene darle el disco a la grabación del programa (pausar ISO, repetición y registro): "
                               "el botón está en el aviso de arriba."
                             : ""));
        }
    } else if (discoApuros_) {
        if (!bien) {
            discoBienDesdeMs_ = 0;
        } else if (discoBienDesdeMs_ == 0) {
            discoBienDesdeMs_ = ahora;
        } else if (ahora - discoBienDesdeMs_ > kBienDuranteMs) {
            discoApuros_ = false;
            const bool habia = prioridad_;
            aplicarPrioridad(false, false);
            avisarFallo(habia ? "El disco anda bien de nuevo: siguen las ISO, la repetición y el registro."
                              : "El disco anda bien de nuevo.");
        }
    }
}

// --- Sincronía labios/voz con un aplauso ------------------------------------------



bool BroadcastEngine::empezarMedicionSincro() {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    if (!medSincro_) medSincro_ = std::make_shared<MedicionSincro>();
    return true;
}

nlohmann::json BroadcastEngine::terminarMedicionSincro() {
    auto m = medSincro_;
    if (!m) return {{"ok", false}, {"problema", "no se estaba midiendo"}};
    medSincro_.reset();
    std::vector<camaras::Muestra> a, v;
    {
        std::lock_guard<std::mutex> l(m->mu);
        a.swap(m->audio);
        v.swap(m->video);
    }
    const auto ga = camaras::buscarGolpes(a, 20); // 20 dB sobre el piso del audio
    const auto gv = camaras::buscarGolpes(v, 12); // 12 niveles de luma sobre el piso del bloque
    const camaras::Medicion r = camaras::medirRetardo(ga, gv);
    return {{"ok", r.ok},           {"retardo_ms", r.retardoMs}, {"dispersion_ms", r.dispersionMs},
            {"aplausos", r.aplausos}, {"confiable", r.confiable}, {"problema", r.problema},
            {"golpes_audio", ga.size()}, {"golpes_video", gv.size()}, {"muestras_audio", a.size()},
            {"cuadros", v.size()}};
}

// ============================================================================
// Grabación por entrada y señal limpia
// ============================================================================

bool BroadcastEngine::startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc) {
    if (!started_) {
        setError("startSourceRecording sin startup");
        return false;
    }
    if (sources_.count(source) == 0) {
        setError("no existe la fuente \"" + source + "\"");
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (individualesPedidas_.count(source) > 0) {
            setError("ya se esta grabando \"" + source + "\" por separado");
            return false;
        }
        if (static_cast<int>(individualesPedidas_.size()) >= kMaxIndividuales) {
            setError("ya hay " + std::to_string(kMaxIndividuales) + " entradas grabandose por separado, que es el tope");
            return false;
        }
        individualesPedidas_.insert(source);
    }
    enSalidas("startSourceRecording", [this, source, dir, enc]() { return startSourceRecordingYa(source, dir, enc); });
    return true;
}

std::string BroadcastEngine::startSourceRecordingYa(const std::string& source, const std::string& dir, const EncoderConfig& enc) {
    if (sourceRecs_.count(source) > 0) return {};
    // La foto del motor (no estadoMotor: ese es de la interfaz).
    const auto foto = motor_->instantanea();
    const json e = foto ? foto->value("entradas", json::object()).value(source, json::object()) : json::object();
    motor::Fuentes f;
    if (e.value("video", false)) {
        f.video = source + ":video";
        f.latVideoNs = motor_->reloj().latencia();
    }
    SourceRec r;
    // Su audio, si tiene; si no, el del programa (referencia para sincronizar al editar).
    r.audioPropio = e.value("audio", false);
    if (r.audioPropio) {
        f.audios.push_back(source + ":audio");
        f.latAudioNs = motor_->reloj().latencia();
    } else {
        std::int64_t l = 0;
        f.audios.push_back(motor_->productorDeBus("programa", &l));
        f.latAudioNs = l;
    }
    std::string limpio;
    for (const char c : source) limpio += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') ? c : '-';
    motor::Grabacion::Config c;
    c.carpeta = dir;
    c.nombre = "entrada-" + limpio + "-" + marcaDeTiempo();
    c.tramoSeg = ajustes::entero("record.tramoMin", 0) * 60;
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    // Las sesiones de codificación de la placa quedan para el programa: x264.
    c.codificadores = {"x264enc"};
    c.colchonSeg = std::max(1, ajustes::entero("record.colchonIsoS", 10)); // hasta 8 a la vez: menos memoria (con el disco en apuros se pausan)
    r.grab = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
    const std::string err = r.grab->iniciar(f);
    if (!err.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        individualesPedidas_.erase(source);
        return "no arrancó la grabación de \"" + source + "\": " + err;
    }
    r.path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
    if (prioridad_) r.grab->pausar(true); // el disco es del programa: arranca en pausa
    sourceRecs_[source] = std::move(r);
    return {};
}

bool BroadcastEngine::stopSourceRecording(const std::string& source) {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (individualesPedidas_.erase(source) == 0) {
            setError("no se estaba grabando \"" + source + "\" por separado");
            return false;
        }
    }
    enSalidas("stopSourceRecording", [this, source]() {
        const auto it = sourceRecs_.find(source);
        if (it == sourceRecs_.end()) return std::string();
        if (it->second.grab) it->second.grab->parar();
        sourceRecs_.erase(it);
        return std::string();
    });
    return true;
}

void BroadcastEngine::stopAllSourceRecordings() {
    std::set<std::string> todas;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        todas = individualesPedidas_;
    }
    for (const auto& s : todas) stopSourceRecording(s);
}

std::vector<BroadcastEngine::GrabacionIndividual> BroadcastEngine::sourceRecordings() const {
    std::vector<GrabacionIndividual> out;
    const json ind = fotoSalidas().value("individuales", json::array());
    for (const auto& r : ind)
        out.push_back({r.value("nombre", std::string()), r.value("path", std::string()), r.value("audio_propio", false),
                       r.value("grabando", false), r.value("bytes", std::uint64_t{0}), r.value("pausada", false)});
    return out;
}

bool BroadcastEngine::startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                                     const EncoderConfig& enc) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (limpioPedido_) return true;
        limpioPedido_ = true;
    }
    enSalidas("startCleanFeed", [this, destino, clave, dir, enc]() { return startCleanFeedYa(destino, clave, dir, enc); });
    return true;
}

std::string BroadcastEngine::startCleanFeedYa(const std::string& destino, const std::string& clave, const std::string& dir,
                                              const EncoderConfig& enc) {
    if (clean_) return {};
    const auto fallo = [this](const std::string& e) {
        std::lock_guard<std::mutex> l(pedidosM_);
        limpioPedido_ = false;
        return "no arrancó la señal limpia: " + e;
    };
    auto r = std::make_unique<SourceRec>();
    const motor::Fuentes f = fuentesDelPrograma(*motor_, {}, true); // el programa SIN capas
    const int fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    if (!destino.empty()) {
        motor::Transmision::Config c;
        c.url = urlConClave(destino, clave);
        c.kbps = enc.videoBitrateKbps;
        c.kbpsAudio = enc.audioBitrateKbps;
        c.gopSeg = std::max(1, enc.keyintSec);
        c.fps = fps;
        c.codificadores = {"x264enc"};
        r->tx = std::make_unique<motor::Transmision>(motor_->reloj(), c);
        const std::string e = r->tx->iniciar(f);
        if (!e.empty()) return fallo(e);
        r->destino = enmascarar(destino, clave);
    } else {
        motor::Grabacion::Config c;
        c.carpeta = dir;
        c.nombre = "senal-limpia-" + marcaDeTiempo();
        c.tramoSeg = ajustes::entero("record.tramoMin", 0) * 60;
        c.kbps = enc.videoBitrateKbps;
        c.fps = fps;
        c.codificadores = {"x264enc"};
        r->grab = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
        const std::string e = r->grab->iniciar(f);
        if (!e.empty()) return fallo(e);
        r->path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
        r->destino = r->path;
    }
    clean_ = std::move(r);
    return {};
}

void BroadcastEngine::stopCleanFeed() {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        limpioPedido_ = false;
    }
    enSalidas("stopCleanFeed", [this]() {
        if (!clean_) return std::string();
        if (clean_->tx) clean_->tx->parar();
        if (clean_->grab) clean_->grab->parar();
        clean_.reset();
        return std::string();
    });
}

nlohmann::json BroadcastEngine::cleanFeedEstado() const {
    return fotoSalidas().value("limpio", json{{"activa", false}});
}

// ============================================================================
// Estado
// ============================================================================

bool BroadcastEngine::haySalida(bool contarGrabacion) const {
    const json f = fotoSalidas();
    if (f.value("principal", json::object()).value("andando", false)) return true;
    for (const auto& [id, x] : f.value("extras", json::object()).items())
        if (x.value("andando", false)) return true;
    // La señal limpia se transmite (tiene destino) o se graba.
    const json limpio = f.value("limpio", json::object());
    if (limpio.value("activa", false) && (contarGrabacion || !limpio.value("destino", std::string()).empty()))
        return true;
    return contarGrabacion && f.value("grabacion", json::object()).value("activa", false);
}

std::vector<std::string> BroadcastEngine::fuentesEnPrograma() const {
    std::vector<std::string> out;
    if (const EscenaApp* e = escena(currentScene_))
        for (const auto& i : e->items)
            if (i.visible) out.push_back(i.fuente);
    for (const std::string& f : fuentesDsk()) out.push_back(f);
    return out;
}

bool BroadcastEngine::fuenteAlAire(const std::string& fuente) const {
    if (fuente.empty() || !haySalida()) return false;
    const auto ss = fuentesEnPrograma();
    return std::find(ss.begin(), ss.end(), fuente) != ss.end();
}

EngineStatus BroadcastEngine::status() const {
    EngineStatus st;
    st.lastError = lastError_;
    if (!started_) return st;
    st.videoActive = true;
    st.audioActive = true;
    st.fps = static_cast<double>(video_.fpsNum) / std::max(1, video_.fpsDen);
    const json f = fotoSalidas();
    const json t = f.value("principal", json::object());
    if (!t.empty()) {
        st.streaming = t.value("andando", false);
        st.droppedFrames = t.value("perdidos", 0);
        st.totalFrames = t.value("cuadros", 0);
        st.bytesSent = t.value("bytes", std::uint64_t{0});
        st.videoEncoder = t.value("codificador", std::string());
        st.reconnecting = t.value("reconectando", false);
        if (st.reconnecting) {
            const std::string e = t.value("error", std::string());
            st.outputProblem = e.find("refused") != std::string::npos   ? "No se pudo conectar con el servidor"
                               : e.find("auth") != std::string::npos    ? "El servidor rechazó la transmisión (¿la clave es correcta?)"
                                                                        : "Se cortó la conexión con el servidor";
        }
    }
    const json g = f.value("grabacion", json::object());
    st.recording = g.value("activa", false);
    if (st.videoEncoder.empty()) st.videoEncoder = g.value("estado", json::object()).value("codificador", std::string());
    return st;
}

} // namespace ba::app
