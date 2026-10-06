// ============================================================================
// src/app/BroadcastEngine.cpp — Motor de emision: ciclo de vida, salidas y vistas
//
// Parte de BroadcastEngine (ver BroadcastEngine.hpp): arranque del motor de
// GStreamer, las salidas (transmisión y destinos, grabación con copia y
// pistas, repetición, grabación por entrada, señal limpia), las vistas para la
// interfaz, el programa hacia el Proceso B y lo que se mide del programa
// (loudness, picos, brillo, la sincronía con un aplauso).
// ============================================================================

#include "BroadcastEngine.hpp"
#include "FuentesApp.hpp"
#include "NdiSource.hpp"
#include "audio/GstRack.hpp"
#include "audio/GstVoz.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"
#include "camaras/SincroLabios.hpp"
#include "diag/Registro.hpp"
#include "ipc/HealthBlock.hpp" // nowQpc() para los timestamps del IPC

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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
    const std::string e = motor_->iniciar();
    if (!e.empty()) {
        setError(e);
        motor_.reset();
        return false;
    }
    started_ = true;
    if (relojPublicar_ > 0) motor_->publicarReloj(relojPublicar_);
    fuentes::ponerMotorApp(this);
    // Lo que pinta la app (chat, gráficos, subtítulos) va a su entrada «app».
    fuentes::ponerSalida([this](const std::string& n, const std::uint8_t* d, int w, int h, int st) {
        if (motor_) motor_->empujarCuadro(n, d, w, h, st);
    });
    fuentes::ponerCambiar([this](const std::string& n, const json& cambios) {
        return updateSourceSettings(n, cambios.dump());
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
                                 spec.settingsJson = aj.dump();
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
    if (!started_) return;
    started_ = false;
    stopAllSourceRecordings();
    stopCleanFeed();
    stopExtras();
    stopStreaming();
    stopRecording();
    replay_.reset();
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
    motor_->verAvisar("programa@gateway", [this](const motor::Cuadro& c) {
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
    });
    returnFeedsActive_ = true;
    return true;
}

void BroadcastEngine::stopReturnFeeds() {
    if (!returnFeedsActive_ || !motor_) return;
    motor_->verAvisar("programa@gateway", nullptr);
    motor_->verSoltar("programa@gateway");
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

bool BroadcastEngine::latestPreview(std::vector<std::uint8_t>& dst, int& width, int& height, std::uint64_t& serial) const {
    if (!previewActive_ || !motor_) return false;
    motor::Cuadro c;
    if (!motor_->verUltimo("programa@ui", c, serial)) return false;
    const std::size_t fila = static_cast<std::size_t>(c.ancho) * 4;
    dst.resize(fila * static_cast<std::size_t>(c.alto));
    for (int y = 0; y < c.alto; ++y) std::memcpy(dst.data() + fila * y, c.bgra.data() + static_cast<std::size_t>(c.stride) * y, fila);
    width = c.ancho;
    height = c.alto;
    return true;
}

void BroadcastEngine::programPeaksDb(float& left, float& right) {
    const auto aDb = [](float lin) { return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin); };
    left = aDb(peakL_.exchange(0.0f));
    right = aDb(peakR_.exchange(0.0f));
}

bool BroadcastEngine::enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) {
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
    return true;
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

bool BroadcastEngine::renderScenePreview(const std::string& canal, std::vector<std::uint8_t>& dst, int& width, int& height) {
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end() || !motor_) return false;
    motor::Cuadro c;
    std::uint64_t s = 0; // siempre el último (el que llama lo pide cuando dibuja)
    if (!motor_->verUltimo(it->second.clave, c, s)) return false;
    aRgba(c, dst);
    width = c.ancho;
    height = c.alto;
    return true;
}

bool BroadcastEngine::captureSource(const std::string& name, int maxWidth, std::vector<std::uint8_t>& rgba, int& width,
                                    int& height) {
    if (!started_) {
        setError("captura: el motor no arranco");
        return false;
    }
    const std::string base = name.empty()                 ? std::string("programa")
                             : escena(name) != nullptr    ? "escena:" + name
                             : sources_.count(name) > 0   ? "entrada:" + name
                                                          : std::string();
    if (base.empty()) {
        setError("captura: no existe la fuente o escena '" + name + "'");
        return false;
    }
    int w = video_.baseWidth, h = video_.baseHeight;
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
    const std::string clave = base + "@captura";
    if (!motor_->verPedir(clave, w & ~1, h & ~1).empty()) {
        setError("captura: '" + name + "' no tiene imagen");
        return false;
    }
    motor::Cuadro c;
    std::uint64_t s = 0;
    bool ok = false;
    // Una escena que no está al aire se compone recién ahora: sus primeros cuadros
    // pueden salir sin las capas que todavía arrancan (en la GPU, compilar los
    // shaders tarda): se toma un cuadro de después.
    const auto desde = std::chrono::steady_clock::now();
    const auto espera = base.rfind("escena:", 0) == 0 ? std::chrono::milliseconds(700) : std::chrono::milliseconds(0);
    for (int i = 0; i < 60; ++i) {
        const bool hay = motor_->verUltimo(clave, c, s);
        ok = ok || hay;
        if (ok && std::chrono::steady_clock::now() - desde >= espera) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    motor_->verSoltar(clave);
    if (!ok) {
        setError("captura: '" + name + "' no dio imagen (sin señal o apagada)");
        return false;
    }
    aRgba(c, rgba);
    width = c.ancho;
    height = c.alto;
    return true;
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

bool BroadcastEngine::startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente) {
    if (!started_) {
        setError("startStreaming sin startup");
        ultimoInicio_ = Inicio::Error;
        return false;
    }
    if (stream_ && stream_->andando()) {
        const bool mismo = (stream.server.empty() || stream.server == activeServer_) && (stream.key.empty() || stream.key == activeKey_);
        if (mismo) {
            ultimoInicio_ = Inicio::YaIgual;
            return true;
        }
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
        setError("no se pudo iniciar la transmision: " + e);
        return false;
    }
    stream_ = std::move(tx);
    activeServer_ = stream.server;
    activeKey_ = stream.key;
    ultimoInicio_ = Inicio::Iniciado;
    return true;
}

void BroadcastEngine::stopStreaming() {
    if (!stream_) return;
    stream_->parar();
    stream_.reset();
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
    auto it = extras_.find(id);
    if (it != extras_.end() && it->second.tx && it->second.tx->andando()) return true;
    stopExtra(id);
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
        setError("el destino no arrancó: " + e);
        return false;
    }
    extras_[id] = std::move(x);
    return true;
}

void BroadcastEngine::stopExtra(const std::string& id) {
    auto it = extras_.find(id);
    if (it == extras_.end()) return;
    if (it->second.tx) it->second.tx->parar();
    extras_.erase(it);
}

void BroadcastEngine::stopExtras() {
    while (!extras_.empty()) stopExtra(extras_.begin()->first);
}

nlohmann::json BroadcastEngine::extrasEstado() const {
    json out = json::object();
    for (const auto& [id, x] : extras_) {
        const json st = x.tx ? x.tx->estado() : json::object();
        out[id] = {{"activo", st.value("andando", false)},
                   {"reconectando", st.value("reconectando", false)},
                   {"perdidos", st.value("perdidos", 0)},
                   {"cuadros", st.value("cuadros", 0)},
                   {"bytes", st.value("bytes", std::uint64_t{0})},
                   {"kbps", st.value("kbps", 0.0)},
                   {"destino", enmascarar(x.server, x.key)}};
    }
    return out;
}

nlohmann::json BroadcastEngine::saludSalidas() const {
    const auto de = [](const motor::Transmision* t) {
        const json st = t != nullptr ? t->estado() : json::object();
        return json{{"activo", st.value("andando", false)},
                    {"reconectando", st.value("reconectando", false)},
                    {"bytes", st.value("bytes", std::uint64_t{0})},
                    {"perdidos", st.value("perdidos", 0)},
                    {"cuadros", st.value("cuadros", 0)},
                    {"congestion", 0.0}};
    };
    json ex = json::object();
    for (const auto& [id, x] : extras_) ex[id] = de(x.tx.get());
    return {{"principal", de(stream_.get())}, {"extras", ex}, {"saltados_encoder", 0}, {"lag_render", 0}};
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
    if (rec_ && rec_->grabando()) return true;
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
    motor::Grabacion::Config c;
    c.carpeta = rec.directory;
    c.nombre = "ba-" + marcaDeTiempo();
    c.tramoSeg = 0; // un archivo por grabación (como antes); el MKV ya aguanta un corte
    c.kbps = rec.calidad == "archivo" ? enc.videoBitrateKbps * 3 : enc.videoBitrateKbps;
    c.kbpsAudio = std::max(enc.audioBitrateKbps, rec.calidad == "archivo" ? 256 : 160);
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    c.codificadores = videoEncoderCandidates(enc);
    auto g = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*motor_, pistas);
    const std::string e = g->iniciar(f);
    if (!e.empty()) {
        setError("no se pudo iniciar la grabacion: " + e);
        return false;
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
    return true;
}

void BroadcastEngine::stopRecording() {
    if (recCopia_) {
        recCopia_->parar();
        recCopia_.reset();
    }
    if (!rec_) return;
    const bool estaba = rec_->grabando();
    rec_->parar();
    const auto archivos = rec_->archivos();
    if (!archivos.empty()) recordPath_ = archivos.back();
    rec_.reset();
    if (estaba && !remuxA_.empty() && remuxAlTerminar_ && !recordPath_.empty()) remux(recordPath_, remuxA_, true);
}

nlohmann::json BroadcastEngine::grabacionEstado() const {
    json pistas = json::array({1});
    for (int i = 1; i < 6; ++i)
        if (pistasGrabacionMask_ & (1 << i)) pistas.push_back(i + 1);
    const json st = rec_ ? rec_->estado() : json::object();
    return {{"activa", rec_ && rec_->grabando()},
            {"archivo", recordPath_},
            {"copia", recordCopyPath_},
            {"copia_activa", recCopia_ && recCopia_->grabando()},
            {"calidad", lastRecValido_ ? lastRec_.calidad : std::string("como_stream")},
            {"encoder", st.value("codificador", std::string())},
            {"pistas", pistas},
            {"encoder_respaldo_activo", false}};
}

bool BroadcastEngine::remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) {
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
    std::thread([st, archivo, destino, borrarOrigen]() {
        const std::string e = motor::remuxar(archivo, destino, [&](double p) { st->pct = static_cast<float>(p * 100.0); });
        // Antes de borrar el MKV: que exista y que no sea mucho más chico.
        std::error_code ec;
        const auto tamO = std::filesystem::file_size(archivo, ec);
        const auto tamD = std::filesystem::file_size(destino, ec);
        const bool verificado = e.empty() && !ec && tamD > 0 && tamD * 10 >= tamO * 9;
        {
            std::lock_guard<std::mutex> l(st->mu);
            if (!verificado) st->error = e.empty() ? "el archivo nuevo no se verificó (tamaño)" : e;
        }
        if (verificado && borrarOrigen) std::filesystem::remove(archivo, ec);
        st->ok = verificado;
        st->pct = 100;
        st->andando = false;
    }).detach();
    return true;
}

nlohmann::json BroadcastEngine::remuxEstado() const {
    if (!remux_) return json::object();
    std::lock_guard<std::mutex> l(remux_->mu);
    return {{"archivo", remux_->archivo}, {"destino", remux_->destino}, {"pct", remux_->pct.load()},
            {"andando", remux_->andando.load()}, {"ok", remux_->ok.load()}, {"error", remux_->error}};
}

// --- Repetición -----------------------------------------------------------------

struct BroadcastEngine::Repeticion {
    std::unique_ptr<motor::Repeticion> rep;
    std::string dir;
    mutable std::mutex mu;
    std::string ultimo;
    std::atomic<bool> guardando{false};
};

bool BroadcastEngine::setReplay(int segundos, const std::string& dir, const EncoderConfig& enc) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    replay_.reset();
    if (segundos <= 0) return true;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto r = std::make_unique<Repeticion>();
    r->dir = dir;
    r->rep = std::make_unique<motor::Repeticion>(motor_->reloj(), (std::filesystem::path(dir) / ".tramos").string(), segundos,
                                                 enc.videoBitrateKbps);
    const std::string e = r->rep->iniciar(fuentesDelPrograma(*motor_, {}));
    if (!e.empty()) {
        setError("no arrancó la repetición: " + e);
        return false;
    }
    replay_ = std::move(r);
    return true;
}

bool BroadcastEngine::guardarReplay() {
    if (!replay_ || !replay_->rep->andando()) {
        setError("la repetición está apagada (replay_ajustes con segundos)");
        return false;
    }
    if (replay_->guardando) return true;
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
    return true;
}

std::string BroadcastEngine::ultimoReplay() const {
    if (!replay_) return {};
    std::lock_guard<std::mutex> l(replay_->mu);
    return replay_->ultimo;
}

nlohmann::json BroadcastEngine::replayEstado() const {
    return {{"activa", replay_ && replay_->rep->andando()},
            {"segundos", replay_ ? replay_->rep->segundos() : 0},
            {"ultimo", ultimoReplay()}};
}

void BroadcastEngine::atenderEncoderCaido(bool, bool) {
    // Las salidas del motor se rearman solas; acá se atienden sus buses.
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
    if (sourceRecs_.count(source) > 0) {
        setError("ya se esta grabando \"" + source + "\" por separado");
        return false;
    }
    if (static_cast<int>(sourceRecs_.size()) >= kMaxIndividuales) {
        setError("ya hay " + std::to_string(kMaxIndividuales) + " entradas grabandose por separado, que es el tope");
        return false;
    }
    if (sources_.count(source) == 0) {
        setError("no existe la fuente \"" + source + "\"");
        return false;
    }
    const json e = estadoMotor().value("entradas", json::object()).value(source, json::object());
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
    c.tramoSeg = 0;
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    // Las sesiones de codificación de la placa quedan para el programa: x264.
    c.codificadores = {"x264enc"};
    r.grab = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
    const std::string err = r.grab->iniciar(f);
    if (!err.empty()) {
        setError("no arranco la grabacion de \"" + source + "\": " + err);
        return false;
    }
    r.path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
    sourceRecs_[source] = std::move(r);
    return true;
}

bool BroadcastEngine::stopSourceRecording(const std::string& source) {
    const auto it = sourceRecs_.find(source);
    if (it == sourceRecs_.end()) {
        setError("no se estaba grabando \"" + source + "\" por separado");
        return false;
    }
    if (it->second.grab) it->second.grab->parar();
    sourceRecs_.erase(it);
    return true;
}

void BroadcastEngine::stopAllSourceRecordings() {
    while (!sourceRecs_.empty()) stopSourceRecording(sourceRecs_.begin()->first);
}

std::vector<BroadcastEngine::GrabacionIndividual> BroadcastEngine::sourceRecordings() const {
    std::vector<GrabacionIndividual> out;
    for (const auto& [n, r] : sourceRecs_) out.push_back({n, r.path, r.audioPropio, r.grab && r.grab->grabando()});
    return out;
}

bool BroadcastEngine::startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                                     const EncoderConfig& enc) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    if (clean_) return true;
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
        if (!e.empty()) {
            setError("no arrancó la señal limpia: " + e);
            return false;
        }
        r->destino = enmascarar(destino, clave);
    } else {
        motor::Grabacion::Config c;
        c.carpeta = dir;
        c.nombre = "senal-limpia-" + marcaDeTiempo();
        c.tramoSeg = 0;
        c.kbps = enc.videoBitrateKbps;
        c.fps = fps;
        c.codificadores = {"x264enc"};
        r->grab = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
        const std::string e = r->grab->iniciar(f);
        if (!e.empty()) {
            setError("no arrancó la señal limpia: " + e);
            return false;
        }
        r->path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
        r->destino = r->path;
    }
    clean_ = std::move(r);
    return true;
}

void BroadcastEngine::stopCleanFeed() {
    if (!clean_) return;
    if (clean_->tx) clean_->tx->parar();
    if (clean_->grab) clean_->grab->parar();
    clean_.reset();
}

nlohmann::json BroadcastEngine::cleanFeedEstado() const {
    if (!clean_) return {{"activa", false}};
    const json st = clean_->tx ? clean_->tx->estado() : json::object();
    return {{"activa", clean_->tx ? st.value("andando", false) : (clean_->grab && clean_->grab->grabando())},
            {"reconectando", st.value("reconectando", false)},
            {"destino", clean_->destino},
            {"archivo", clean_->path},
            {"cuadros", st.value("cuadros", 0)},
            {"perdidos", st.value("perdidos", 0)}};
}

// ============================================================================
// Estado
// ============================================================================

EngineStatus BroadcastEngine::status() const {
    EngineStatus st;
    st.lastError = lastError_;
    if (!started_) return st;
    st.videoActive = true;
    st.audioActive = true;
    st.fps = static_cast<double>(video_.fpsNum) / std::max(1, video_.fpsDen);
    if (stream_) {
        const json t = stream_->estado();
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
    if (rec_) {
        st.recording = rec_->grabando();
        if (st.videoEncoder.empty()) st.videoEncoder = rec_->estado().value("codificador", std::string());
    }
    return st;
}

} // namespace ba::app
