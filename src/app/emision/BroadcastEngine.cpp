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
#include "emision/SalidasConfig.hpp"
#include "Pedido.hpp"
#include "ipc/CanalesSalida.hpp"
#include "emision/NdiSource.hpp"
#include "audio/GstRack.hpp"
#include "audio/GstVoz.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"
#include "camaras/SincroLabios.hpp"
#include "diag/Registro.hpp"
#include "diag/SalidaAsincrona.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"
#include "platform/Volumen.hpp"
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
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace ba::app {

using json = nlohmann::json;

// La por omisión se llama igual en el motor, acá y en lo que se guarda (salidas.json).
static_assert(std::string_view(BroadcastEngine::kSalidaPorOmision) == motor::Motor::kSalidaPorOmision &&
              std::string_view(salidas::kPorOmision) == motor::Motor::kSalidaPorOmision);

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

BroadcastEngine::BroadcastEngine() {
    // La por omisión existe siempre: lo que no dice salida va a ella.
    auto p = std::make_shared<Salida>();
    p->id = p->nombre = kSalidaPorOmision;
    p->retornoEnUso = true; // BA.Program, como siempre: hasta que se sepa que nadie lo mira
    p->emisor = salidasConEmisor().count(p->id) > 0; // la codifica un ba-emisor (F3-01)
    salidas_[p->id] = p;
    ordenSalidas_.push_back(p->id);
}

BroadcastEngine::~BroadcastEngine() { shutdown(); }

// ============================================================================
// Las salidas (docs/planes/PLAN-SALIDAS.md, etapa 2)
// ============================================================================

std::string BroadcastEngine::Salida::claveRetorno() const {
    return porOmision() ? std::string("programa@gateway") : "programa:" + id + "@gateway";
}

std::wstring BroadcastEngine::Salida::canalRetorno() const {
    return porOmision() ? std::wstring(kProgramVideoName) : ba::ipc::nombreCanalSalida(id);
}

BroadcastEngine::Salida* BroadcastEngine::laSalida(const std::string& id) {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    if (it != salidas_.end()) return it->second.get();
    setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no hay una salida «%1»"), {id}));
    return nullptr;
}

const BroadcastEngine::Salida* BroadcastEngine::laSalida(const std::string& id) const {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    return it == salidas_.end() ? nullptr : it->second.get();
}

std::shared_ptr<BroadcastEngine::Salida> BroadcastEngine::laSalidaCompartida(const std::string& id) {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    if (it != salidas_.end()) return it->second;
    setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no hay una salida «%1»"), {id}));
    return nullptr;
}

std::vector<std::shared_ptr<BroadcastEngine::Salida>> BroadcastEngine::lasSalidas() const {
    std::lock_guard<std::mutex> l(salidasM_);
    std::vector<std::shared_ptr<Salida>> out;
    for (const auto& id : ordenSalidas_) out.push_back(salidas_.at(id));
    return out;
}

std::vector<std::string> BroadcastEngine::salidaIds() const { return ordenSalidas_; }

bool BroadcastEngine::existeSalida(const std::string& id) const { return laSalida(id) != nullptr; }

std::string BroadcastEngine::idDeSalida(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    return s != nullptr ? s->id : std::string();
}

bool BroadcastEngine::crearSalida(const std::string& id, const std::string& nombre) {
    if (!started_ || !motor_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    if (std::string e = salidas::validarId(id); !e.empty()) {
        setError(e);
        return false;
    }
    if (salidas_.count(id) > 0) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya hay una salida «%1»"), {id}));
        return false;
    }
    // En el motor: su programa, su preview y su mezcla (va en su cola: un error
    // que llegue después vuelve por alFallarMotor).
    if (std::string e = motor_->crearSalida(id, nombre); !e.empty()) {
        setError(e);
        return false;
    }
    auto s = std::make_shared<Salida>();
    s->id = id;
    s->nombre = nombre.empty() ? id : nombre;
    s->emisor = salidasConEmisor().count(id) > 0;
    if (s->emisor) s->retornoEnUso = true; // su emisor lo lee siempre
    // Su mezcla arranca como la de la por omisión (lo mismo hace el motor con sus mandos).
    for (const auto& [n, f] : sources_) {
        s->volumen[n] = f.spec.volume;
        s->mute[n] = f.spec.muted;
    }
    {
        std::lock_guard<std::mutex> l(salidasM_);
        salidas_[id] = s;
        ordenSalidas_.push_back(id);
    }
    medirSalida(*s);
    if (returnFeedsActive_) publicarRetorno(s);
    aplicarLoudness();
    return true;
}

bool BroadcastEngine::borrarSalida(const std::string& id) {
    if (id.empty() || id == kSalidaPorOmision) {
        setError(ba::i18n::traducir("emision", "la salida por omisión no se puede borrar"));
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(id);
    if (!s) return false;
    // Lo que sale de ella se para en el hilo de salidas, después de lo que ya estaba encolado.
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        s->streamPedido = s->grabacionPedida = s->limpioPedido = false;
        s->extrasPedidos.clear();
    }
    if (hiloSalidas_.joinable())
        enSalidas("borrarSalida", [this, s]() {
            pararTodoYa(*s);
            return std::string();
        });
    soltarRetorno(*s);
    dejarDeMedir(*s);
    {
        std::lock_guard<std::mutex> l(salidasM_);
        salidas_.erase(id);
        ordenSalidas_.erase(std::remove(ordenSalidas_.begin(), ordenSalidas_.end(), id), ordenSalidas_.end());
    }
    // El motor la desarma aparte (y una nueva con el mismo id espera ese desarme).
    if (motor_)
        if (std::string e = motor_->borrarSalida(id); !e.empty()) {
            setError(e);
            return false;
        }
    return true;
}

void BroadcastEngine::medirSalida(Salida& s) {
    if (!motor_ || s.oyenteAudio != 0) return;
    // La sincronía labios/voz se mide en el programa de la por omisión (es una cámara contra su micrófono).
    const bool sincro = s.porOmision();
    s.oyenteAudio = motor_->escucharPrograma(
        [this, m = s.medidas, sincro](const float* d, std::size_t n) {
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
            m->loudness.alimentar(planos, 2, static_cast<std::uint32_t>(n));
            for (auto [a, v] : {std::pair{&m->picoL, pl}, std::pair{&m->picoR, pr}}) {
                float cur = a->load(std::memory_order_relaxed);
                while (v > cur && !a->compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
                }
            }
            if (!sincro) return;
            if (auto ms = medSincro_) {
                // Picos cada 5 ms con la hora en que sonó (ahora es cuando sale).
                const std::int64_t fin = ahoraMs();
                for (std::size_t i = 0; i < n; i += 240) {
                    float p = 0;
                    for (std::size_t k = i; k < std::min(n, i + 240); ++k) p = std::max(p, std::max(std::fabs(l[k]), std::fabs(r[k])));
                    const double db = p > 1e-6f ? 20.0 * std::log10(p) : -120.0;
                    const std::int64_t t = fin - static_cast<std::int64_t>((n - i) * 1000 / 48000);
                    std::lock_guard<std::mutex> lk(ms->mu);
                    ms->audio.push_back({t, db});
                }
            }
        },
        s.id);
}

void BroadcastEngine::dejarDeMedir(Salida& s) {
    if (s.oyenteAudio != 0 && motor_) motor_->dejarDeEscuchar(s.oyenteAudio);
    s.oyenteAudio = 0;
}

audio::LoudnessPrograma& BroadcastEngine::loudness(const std::string& salida) {
    const Salida* s = laSalida(salida);
    return (s != nullptr ? *s : porOmision()).medidas->loudness;
}

void BroadcastEngine::anotarAac(Salida& s, int kbps) {
    const int antes = s.kbpsAac.load();
    if (kbps <= 0 || (antes > 0 && antes <= kbps)) return;
    s.kbpsAac = kbps;
    if (kbps < 128 && (antes == 0 || antes >= 128)) aplicarLoudness(); // cambia el techo
}

void BroadcastEngine::aplicarLoudness() {
    if (!motor_) return;
    const audio::LoudnessPrograma::Objetivo ob = porOmision().medidas->loudness.objetivo();
    for (const auto& s : lasSalidas()) {
        double techo = ob.tp;
        // AES TD1004: un códec con pérdida a poca tasa reconstruye picos por encima de
        // lo que entró; con AAC por debajo de 128 kbps, 1 dB más de margen.
        const int kbps = s->kbpsAac.load();
        if (kbps > 0 && kbps < 128) techo = std::min(techo, -2.0);
        // En la cola del motor: un error vuelve por alFallar.
        motor_->ponerLoudness(ob.agc, ob.lufs, techo, s->porOmision() ? std::string() : s->id);
    }
}

int BroadcastEngine::pistasGrabacionMask() const { return porOmision().pistasGrabacionMask; }

void BroadcastEngine::setError(std::string msg) { lastError_ = std::move(msg); }

bool BroadcastEngine::startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio) {
    if (started_) return true;
    paths_ = paths;
    video_ = video;
    audio_ = audio;
    std::string err;
    if (!motor::iniciarGStreamer(&err)) {
        sinGStreamer_ = true;
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "GStreamer no arrancó: %1"), {err}));
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
    c.emisores = salidasConEmisor(); // su audio también al anillo del emisor (F3-01)
    motor_ = std::make_unique<motor::Motor>(c);
    // Un lienzo que parpadea con la red eléctrica del país (F2-17): se avisa, no se cambia.
    for (const std::string& aviso : motor::avisosDeLaRed(c.lienzo, motor::redElectricaDelSistema()))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0, aviso);
    internasPedidas_.clear();
    // Lo que se le encargó y falló después (las órdenes van en cola: ver
    // Motor::orden). Llega desde el hilo del motor.
    motor_->alFallar([this](const std::string& que, const std::string& error) {
        std::function<void(const std::string&)> fn;
        {
            std::lock_guard<std::mutex> l(falloMotorM_);
            fn = falloMotor_;
        }
        if (fn) fn(ba::i18n::llenar(ba::i18n::traducir("emision", "el motor no pudo hacer «%1»: %2"), {que, error}));
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
    // Lo que lee el emisor aparte: su canal de video publicado y su vista pedida desde
    // ya (aunque el gateway no lo mire y aunque no se publique el retorno).
    for (const auto& s : lasSalidas())
        if (s->emisor) {
            s->retornoEnUso = true;
            publicarRetorno(s);
        }
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
                                 return motor_ ? motor_->propiedadEntrada(e, el, p, v) : ba::i18n::traducir("emision", "no hay motor");
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
    // Lo que se mide del programa: loudness y picos (audio, de cada salida), brillo y
    // quietud (video, el de la por omisión).
    medirSalida(porOmision());
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
    stopExtras();
    for (const auto& id : salidaIds()) {
        stopCleanFeed(id);
        stopStreaming(id);
        stopRecording(id);
    }
    detenerSalidas(); // corre lo encolado (las paradas) y se apaga
    stopReturnFeeds();
    disablePreview();
    for (const auto& [canal, v] : scenePreviews_) motor_->verSoltar(v.clave);
    scenePreviews_.clear();
    motor_->verAvisar("programa@alarmas", nullptr);
    motor_->verSoltar("programa@alarmas");
    for (const auto& s : lasSalidas()) dejarDeMedir(*s);
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
    // Las salidas que no son la por omisión se van con el motor (resetVideo las vuelve
    // a crear); la por omisión queda, sin nada al aire (su loudness sigue: hay quien
    // tiene su referencia).
    {
        std::lock_guard<std::mutex> l(salidasM_);
        for (auto it = salidas_.begin(); it != salidas_.end();)
            it = it->first == kSalidaPorOmision ? std::next(it) : salidas_.erase(it);
        ordenSalidas_ = {kSalidaPorOmision};
    }
    Salida& p = porOmision();
    p.alAire.clear();
    p.armada.clear();
    for (auto& d : p.dsk) d = Dsk{};
    p.ftb = p.placa = p.ftbAudio = false;
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
    json out{{"latencia_ms", 0}, {"tope_ms", 0}, {"automatico", true}, {"tarde", json::array()}, {"entradas", json::array()}};
    if (!motor_) return out;
    motor::Reloj& r = motor_->reloj();
    // La fijada (la que usan todos), lo que piden ahora y quién lo pide.
    out["latencia_ms"] = r.latencia() / 1000000;
    out["objetivo_ms"] = r.objetivo() / 1000000;
    out["la_marca"] = r.laMarca();
    out["puede_bajarse"] = r.puedeBajarse();
    out["baja_sola"] = !alAire_.load();
    out["tope_ms"] = r.topeMs();
    out["automatico"] = r.topeAutomatico();
    out["margen_ms"] = ajustes::entero("video.margenComposicionMs", 5); // el del video (el audio usa audio.margenMezclaMs)
    // El modo baja latencia (F2-05): el tope que se pone con un botón; lo que pasa de ahí va
    // con su propia demora. Se guarda con el evento, como cualquier tope.
    out["baja_latencia_ms"] = ajustes::entero("estudio.bajaLatenciaMs", 110);
    out["baja_latencia"] = !r.topeAutomatico() && r.topeMs() == out["baja_latencia_ms"].get<int>();
    for (const auto& [quien, ns] : r.anotadas())
        if (r.llegaTarde(ns)) out["tarde"].push_back({{"entrada", quien}, {"latencia_ms", ns / 1000000}});
    // Cada entrada: su clase, lo que pide, lo que tarda y si va con su propia demora.
    const auto foto = motor_->instantanea();
    // Los buses de oído (retornos, escucha en vivo: F2-01) y las entradas que no llegan a tiempo.
    out["oido"] = foto ? foto->value("oido", json::object()) : json::object();
    // Con nombre: items() sobre un json temporal recorre memoria liberada.
    const json ents = foto ? foto->value("entradas", json::object()) : json::object();
    for (const auto& [id, e] : ents.items()) {
            if (!e.is_object() || (!e.value("video", false) && !e.value("audio", false))) continue;
            const json ev = e.value("edad_video", json());
            const json ea = e.value("edad_audio", json());
            const json& edad = ev.is_object() ? ev : ea;
            // La edad real: la medida viene achicada por el retraso propio (se mide después del corrimiento).
            const double propio = e.value("retraso_propio_ms", 0.0);
            out["entradas"].push_back({{"entrada", id},
                                       {"clase", e.value("clase", std::string("sala"))},
                                       {"clase_de_fabrica", e.value("clase_de_fabrica", true)},
                                       {"pide_ms", e.value("pide_ms", 0.0)},
                                       {"edad_p50_ms", edad.is_object() ? edad.value("p50_ms", 0.0) + propio : 0.0},
                                       {"edad_p95_ms", edad.is_object() ? edad.value("p95_ms", 0.0) + propio : 0.0},
                                       {"retraso_propio_ms", e.value("retraso_propio_ms", 0.0)},
                                       {"tiembla_ms", e.value("tiembla_ms", 0.0)},
                                       {"llega_tarde", e.value("llega_tarde", false)},
                                       {"en_escena", e.value("en_escena", false)},
                                       {"audio", e.value("audio", false)},
                                       {"entra_al_oido", e.value("entra_al_oido", true)},
                                       {"video", e.value("video", false)},
                                       {"marca", id == out["la_marca"].get<std::string>()}});
        }
    // Los cuadros que llegaron tarde al compositor del programa en el último minuto (por entrada):
    // si aparecen, el colchón no alcanza.
    int tarde = 0;
    json porEntrada = json::object();
    if (foto)
        for (const auto& c : foto->value("video", json::object()).value("programa", json::object()).value("capas", json::array())) {
            const json el = c.value("edad_llega", json());
            const int t = el.is_object() ? el.value("tarde_60s", 0) : 0;
            tarde += t;
            if (t > 0) porEntrada[c.value("entrada", std::string())] = t;
        }
    out["cuadros_tarde_60s"] = tarde;
    out["cuadros_tarde_por_entrada"] = porEntrada;
    for (auto& e : out["entradas"]) e["cuadros_tarde_60s"] = porEntrada.value(e.value("entrada", std::string()), 0);
    return out;
}

bool BroadcastEngine::bajarLatenciaEstudio() { return motor_ && motor_->bajarLatenciaEstudio(); }

void BroadcastEngine::ponerTopeEstudio(long long ms) {
    if (motor_) motor_->ponerTopeEstudio(ms);
}

int BroadcastEngine::enviarCaption(const std::string&, double) { return 0; }

// ============================================================================
// El programa hacia el Proceso B
// ============================================================================

bool BroadcastEngine::publishReturnFeeds() {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "publishReturnFeeds sin startup"));
        return false;
    }
    if (returnFeedsActive_) return true;
    // El audio de cada salida (BA.Audio.ContentForReturn, BA.Audio.Salida.<id>) lo
    // escribe el motor desde que la arma.
    bool ok = true;
    for (const auto& s : lasSalidas()) ok = publicarRetorno(s) && ok;
    if (!ok) return false;
    returnFeedsActive_ = true;
    return true;
}

void BroadcastEngine::ponerTopeRetorno(int ancho, int alto) {
    if (ancho <= 0 || alto <= 0 || (ancho == topeRetornoAncho_ && alto == topeRetornoAlto_)) return;
    topeRetornoAncho_ = ancho;
    topeRetornoAlto_ = alto;
    if (!returnFeedsActive_) return;
    // Andando: cada salida rehace su canal y su vista al tamaño nuevo.
    for (const auto& s : lasSalidas()) {
        soltarVistaRetorno(*s);
        publicarRetorno(s);
    }
}

ba::ipc::SlotConfig BroadcastEngine::canalRetorno() const {
    // Encajada en el tope, con el aspecto de la salida y sin agrandarla (agrandar no
    // agrega detalle, solo bytes). Par: lo piden los conversores a I420.
    const int ow = std::max(2, video_.outputWidth), oh = std::max(2, video_.outputHeight);
    double k = std::min({1.0, static_cast<double>(topeRetornoAncho_) / ow, static_cast<double>(topeRetornoAlto_) / oh});
    ba::ipc::SlotConfig c{std::max(2, static_cast<int>(ow * k) & ~1), std::max(2, static_cast<int>(oh * k) & ~1)};
    // El período del lienzo: el gateway duerme hasta el próximo cuadro (ipc/Mirada.hpp, F2-14).
    if (video_.fpsNum > 0 && video_.fpsDen > 0)
        c.periodoNs = static_cast<std::uint64_t>(video_.fpsDen) * 1'000'000'000ULL / static_cast<std::uint64_t>(video_.fpsNum);
    return c;
}

bool BroadcastEngine::publicarRetorno(const std::shared_ptr<Salida>& s) {
    const ba::ipc::SlotConfig vcfg = canalRetorno();
    {
        std::lock_guard<std::mutex> l(s->retornoM);
        // Un canal publicado no cambia de tamaño (ver SpoutSender::publish): con otro
        // lienzo (resetVideo) se rehace, y el gateway se vuelve a suscribir solo al ver
        // otra instancia, como con los canales de escena. Antes se intentaba publicar
        // otra vez el mismo y fallaba: la salida al gateway quedaba colgada.
        if (s->retorno.published() && (s->retornoAncho != vcfg.width || s->retornoAlto != vcfg.height))
            s->retorno = ba::ipc::SpoutSender();
        if (!s->retorno.published()) {
            if (!s->retorno.publish(s->canalRetorno().c_str(), vcfg)) {
                setError(s->porOmision() ? ba::i18n::traducir("emision", "no se pudo publicar el canal de video de programa")
                                         : ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo publicar el canal de video de la salida «%1»"),
                                                            {s->id}));
                return false;
            }
            s->retornoAncho = vcfg.width;
            s->retornoAlto = vcfg.height;
        }
    }
    return !s->retornoEnUso || pedirVistaRetorno(s);
}

bool BroadcastEngine::pedirVistaRetorno(const std::shared_ptr<Salida>& s) {
    if (!motor_ || s->retornoPedido) return true;
    const ba::ipc::SlotConfig tam = canalRetorno();
    const std::string e = motor_->verPedir(s->claveRetorno(), tam.width, tam.height);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    // La salida va en el aviso: vive mientras el motor pueda llamarlo (verAvisar con
    // nullptr lo suelta antes de volver).
    motor_->verAvisar(s->claveRetorno(), [this, s](const motor::Cuadro& c) { enviarRetorno(*s, c); });
    s->retornoPedido = true;
    return true;
}

void BroadcastEngine::soltarVistaRetorno(Salida& s) {
    if (!s.retornoPedido || !motor_) return;
    motor_->verAvisar(s.claveRetorno(), nullptr);
    motor_->verSoltar(s.claveRetorno());
    s.retornoPedido = false;
}

void BroadcastEngine::soltarRetorno(Salida& s) {
    soltarVistaRetorno(s);
    std::lock_guard<std::mutex> l(s.retornoM);
    s.retorno = ba::ipc::SpoutSender();
}

void BroadcastEngine::enviarRetorno(Salida& s, const motor::Cuadro& c) {
    // Los bytes son los del buffer de la vista (sin copiar): la única copia es al canal.
    std::lock_guard<std::mutex> l(s.retornoM);
    if (!s.retorno.published() || c.ancho != s.retornoAncho || c.alto != s.retornoAlto) return;
    const std::size_t fila = static_cast<std::size_t>(c.ancho) * 4;
    const std::uint8_t* px = c.bgra.data();
    if (static_cast<std::size_t>(c.stride) != fila) {
        s.retornoFilas.resize(fila * static_cast<std::size_t>(c.alto));
        for (int y = 0; y < c.alto; ++y)
            std::memcpy(s.retornoFilas.data() + fila * y, c.bgra.data() + static_cast<std::size_t>(c.stride) * y, fila);
        px = s.retornoFilas.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++s.retornoCuadros;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    meta.contenidoQpc = c.contenidoQpc; // para alinearlo con su audio en el gateway (F2-11)
    s.retorno.send(px, fila * static_cast<std::size_t>(c.alto), meta);
}

void BroadcastEngine::retornoEnUso(bool si, const std::string& salida) {
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s || s->retornoEnUso == si) return;
    // Con emisor aparte no se suelta por reposo: es lo que él codifica (F3-01).
    if (!si && s->emisor) return;
    s->retornoEnUso = si;
    if (!returnFeedsActive_ || !motor_) return;
    if (si)
        pedirVistaRetorno(s);
    else
        soltarVistaRetorno(*s);
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
        setError(ba::i18n::traducir("emision", "canal de escena fuera de rango"));
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
        setError(ba::i18n::traducir("emision", "escena para pantallas: el motor no arranco"));
        return false;
    }
    if (escena(nombre) == nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "escena para pantallas: no existe '%1'"), {nombre}));
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
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo publicar el canal de escena %1"), {std::to_string(k)}));
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
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "escena para pantallas: %1"), {e}));
        return false;
    }
    {
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.escena = nombre;
        c.clave = clave;
    }
    motor_->verAvisar(clave, [this, k](const motor::Cuadro& q) { enviarEscena(k, q); });
    diag::registro().linea(diag::Nivel::Info, "pantallas", 0,
                           ba::i18n::llenar(ba::i18n::traducir("pantallas", "escena «%1» en el canal %2 (%3x%4)"), {nombre, std::to_string(k), std::to_string(w), std::to_string(h)}));
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

void BroadcastEngine::soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo) {
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) {
        if (salvo.count(k) > 0) continue;
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
    for (const auto& s : lasSalidas()) soltarVistaRetorno(*s);
    porOmision().retornoEnUso = true; // como al arrancar
    returnFeedsActive_ = false;
}

// ============================================================================
// Vistas para la interfaz
// ============================================================================

bool BroadcastEngine::enablePreview(int width, int height) {
    if (!started_ || width <= 0 || height <= 0) {
        setError(ba::i18n::traducir("emision", "enablePreview sin startup o con tamaño invalido"));
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

bool BroadcastEngine::latestPreview(motor::Cuadro& out, std::uint64_t& serial) const {
    return previewActive_ && motor_ && motor_->verUltimo("programa@ui", out, serial);
}

void BroadcastEngine::programPeaksDb(float& left, float& right, const std::string& salida) {
    const auto aDb = [](float lin) { return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin); };
    const Salida* s = laSalida(salida);
    Medidas& m = *(s != nullptr ? *s : porOmision()).medidas;
    left = aDb(m.picoL.exchange(0.0f));
    right = aDb(m.picoR.exchange(0.0f));
}

bool BroadcastEngine::armar(const std::string& name, const std::string& salida) {
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (!name.empty() && escena(name) == nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    if (!started_ || !motor_) {
        s->armada = name; // el próximo motor la arma (resetVideo)
        return true;
    }
    // LO ARMADO ES EL BUS DE PREVIEW DEL MOTOR: una composición que vive
    // siempre y a la que armar le cambia la escena con un corte (las capas
    // nuevas reemplazan a las viejas cuando ya tienen cuadro). Antes cada
    // escena armada tenía su propia vista y su propia composición: en cada
    // TAKE se soltaba una y se armaba otra de cero (ramas, shaders) y el
    // preview parpadeaba en negro o sin capas, y además iba a 10-15 cuadros/s.
    const std::string e = motor_->armar(name, s->id);
    if (!e.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "armar: %1"), {e}));
        return false;
    }
    s->armada = name;
    return true;
}

bool BroadcastEngine::enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) {
    if (canal == "armada" && started_ && motor_) {
        // La vista del bus de preview de la por omisión (lo armado lo pone el
        // conmutador con armar: acá solo se mira).
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
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {ev}));
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
        setError(ba::i18n::traducir("emision", "vista previa de escena: el motor no arranco"));
        return false;
    }
    // «entera:X»: la entrada X antes del recorte del PTZ digital (la toma entera).
    const bool entera = name.rfind("entera:", 0) == 0 && sources_.count(name.substr(7)) > 0;
    const std::string base = entera                     ? "entrada-entera:" + name.substr(7)
                             : escena(name) != nullptr  ? "escena:" + name
                             : sources_.count(name) > 0 ? "entrada:" + name
                                                        : "";
    if (base.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa de escena: no existe '%1'"), {name}));
        return false;
    }
    const int w = maxWidth > 0 ? std::min(maxWidth, video_.baseWidth) : video_.baseWidth;
    const int h = std::max(2, w * video_.baseHeight / std::max(1, video_.baseWidth));
    VistaPrevia v;
    v.clave = base + "@" + canal;
    v.name = name;
    const std::string e = motor_->verPedir(v.clave, w, h);
    if (!e.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {e}));
        return false;
    }
    scenePreviews_[canal] = v;
    medirVista(v.clave);
    return true;
}

bool BroadcastEngine::enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida,
                                          int maxWidth) {
    const Salida* s = laSalida(salida);
    if (s == nullptr || (bus != "programa" && bus != "preview")) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: no hay una salida '%1'"), {salida}));
        return false;
    }
    // El nombre anotado es la salida: si se la borra y vuelve, la vista se vuelve a pedir
    // (FrameService mira scenePreviewName) igual que una entrada.
    const std::string nombre = bus + ":" + s->id;
    auto it = scenePreviews_.find(canal);
    if (it != scenePreviews_.end() && it->second.name == nombre) return true;
    disableScenePreview(canal);
    if (!started_ || !motor_) return false;
    const int w = maxWidth > 0 ? std::min(maxWidth, video_.baseWidth) : video_.baseWidth;
    const int h = std::max(2, w * video_.baseHeight / std::max(1, video_.baseWidth));
    VistaPrevia v;
    // «programa:<id>» y «preview:<id>» son las composiciones de esa salida (motor/Vistas.hpp).
    v.clave = nombre + "@" + canal;
    v.name = nombre;
    if (const std::string e = motor_->verPedir(v.clave, w, h); !e.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {e}));
        return false;
    }
    scenePreviews_[canal] = v;
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
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("emision", "medir %1: brillo %2 dt %3 ms serie %4"), {clave, std::to_string(n ? suma / n : 0), std::to_string(dt), std::to_string(c.serie)}));
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

bool BroadcastEngine::renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial) {
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end() || !motor_) return false;
    // Solo si hay uno nuevo: antes daba siempre el último y la interfaz lo
    // copiaba y le daba vuelta los colores 30 veces por segundo aunque fuera el
    // mismo (la serie de las vistas es una sola y solo crece). Y sin copiarlo: la
    // interfaz comparte los bytes con la vista (F3-11).
    return motor_->verUltimo(it->second.clave, out, serial);
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
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "captura: %1"), {e}));
        return false;
    }
    return true;
}

std::string BroadcastEngine::prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                             bool& esEscena) const {
    if (!started_) return ba::i18n::traducir("emision", "el motor no arranco");
    const std::string base = name.empty()                 ? std::string("programa")
                             : escena(name) != nullptr    ? "escena:" + name
                             : sources_.count(name) > 0   ? "entrada:" + name
                                                          : std::string();
    if (base.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente o escena '%1'"), {name});
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
            return ba::i18n::llenar(ba::i18n::traducir("emision", "'%1' no tiene imagen"), {clave.substr(0, clave.rfind('@'))});
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
        return ba::i18n::llenar(ba::i18n::traducir("emision", "'%1' no dio imagen (sin señal o apagada)"), {clave.substr(0, clave.rfind('@'))});
    }
    aRgba(c, rgba);
    width = c.ancho;
    height = c.alto;
    return {};
}

// ============================================================================
// Transmisión y destinos
// ============================================================================

std::string BroadcastEngine::destinoActivo(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    if (s == nullptr) return {};
    std::lock_guard<std::mutex> l(pedidosM_);
    return enmascarar(s->activeServer, s->activeKey);
}

namespace {
/// Lo que leen las salidas del programa de una salida: su video y su audio (pista 1
/// y las pedidas; las pistas 2-6 por ahora siguen a la por omisión, ver Motor).
motor::Fuentes fuentesDelPrograma(motor::Motor& m, const std::string& salida, const std::vector<int>& pistasExtra,
                                  bool limpio = false) {
    motor::Fuentes f;
    f.video = m.productorDeVideo(limpio ? "limpio" : "programa", &f.latVideoNs, salida);
    std::int64_t lat = 0;
    f.audios.push_back(m.productorDeBus("programa", &lat, salida));
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

/// La transmisión a `url` con la codificación de `enc` (la principal y los destinos extra:
/// con el mismo perfil comparten el codificador de la salida).
motor::Transmision::Config configTransmision(const std::string& url, const EncoderConfig& enc, const VideoConfig& video,
                                             std::vector<std::string> codificadores) {
    motor::Transmision::Config c;
    c.url = url;
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video.fpsNum / std::max(1, video.fpsDen));
    c.codificadores = std::move(codificadores);
    c.control = motor::controlTasaDe(enc.rateControl);
    return c;
}
} // namespace

struct BroadcastEngine::Repeticion {
    std::unique_ptr<motor::Repeticion> rep;
    std::string dir;
    mutable std::mutex mu;
    std::string ultimo;
    std::atomic<bool> guardando{false};
};

// Acá Repeticion ya está completa (la Salida la tiene en un unique_ptr).
BroadcastEngine::Salida::Salida() = default;
BroadcastEngine::Salida::~Salida() = default;

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
        for (const auto& s : lasSalidas()) pararTodoYa(*s);
        return std::string();
    });
    {
        std::lock_guard<std::mutex> l(salidasColaM_);
        salidasSeguir_ = false;
    }
    salidasCv_.notify_one();
    hiloSalidas_.join();
    std::lock_guard<std::mutex> l(pedidosM_);
    for (const auto& s : lasSalidas()) {
        s->streamPedido = s->grabacionPedida = s->limpioPedido = false;
        s->extrasPedidos.clear();
    }
    individualesPedidas_.clear();
}

void BroadcastEngine::pararTodoYa(Salida& s) {
    if (s.clean) {
        if (s.clean->tx) s.clean->tx->parar();
        if (s.clean->grab) s.clean->grab->parar();
        s.clean.reset();
    }
    for (auto& [id, x] : s.extras)
        if (x.tx) x.tx->parar();
    s.extras.clear();
    if (s.stream) s.stream->parar();
    s.stream.reset();
    // Lo que sale por el emisor aparte no se para acá: apagar o rearmar el motor no
    // corta el aire (lo para el operador, o el cierre ordenado del launcher).
    if (!s.emisor) stopRecordingYa(s);
    s.replay.reset();
}

nlohmann::json BroadcastEngine::fotoSalidas() const {
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    return fotoSalidas_;
}

json BroadcastEngine::fotoDe(const Salida& s) const {
    json f = json::object();
    f["principal"] = s.stream ? s.stream->estado() : json::object();
    json ex = json::object();
    for (const auto& [id, x] : s.extras) {
        json st = x.tx ? x.tx->estado() : json::object();
        st["destino"] = enmascarar(x.server, x.key);
        st["salida"] = s.id;
        ex[id] = std::move(st);
    }
    f["extras"] = std::move(ex);
    // El archivo en curso: el último tramo (con la grabación partida, o seguida en otra parte o carpeta).
    std::string archivo = s.recordPath, copia = s.recordCopyPath;
    if (s.rec)
        if (const auto a = s.rec->archivos(); !a.empty()) archivo = a.back();
    if (s.recCopia)
        if (const auto a = s.recCopia->archivos(); !a.empty()) copia = a.back();
    const json eg = s.rec ? s.rec->estado() : json::object();
    f["grabacion"] = {{"activa", eg.value("grabando", false)},
                      {"caida", eg.value("caida", false)}, // pedida y cortada: se está rearmando
                      {"estado", eg},
                      {"archivo", archivo},
                      {"copia", copia},
                      {"copia_activa", s.recCopia && s.recCopia->grabando()}};
    // Perdió un codificador en el aire: sigue con otro (la lista negra de su video).
    f["codificador_respaldo"] = !s.fuenteVideo.empty() && !motor::fabricasCaidas(s.fuenteVideo).empty();
    std::string ultimo;
    if (s.replay) {
        std::lock_guard<std::mutex> l(s.replay->mu);
        ultimo = s.replay->ultimo;
    }
    f["replay"] = {{"activa", s.replay && s.replay->rep->andando()},
                   {"segundos", s.replay ? s.replay->rep->segundos() : 0},
                   {"ultimo", ultimo}};
    if (s.clean) {
        const json st = s.clean->tx ? s.clean->tx->estado() : json::object();
        f["limpio"] = {{"activa", s.clean->tx ? st.value("andando", false) : (s.clean->grab && s.clean->grab->grabando())},
                       {"reconectando", st.value("reconectando", false)},
                       {"destino", s.clean->destino},
                       {"archivo", s.clean->path},
                       {"cuadros", st.value("cuadros", 0)},
                       {"perdidos", st.value("perdidos", 0)}};
    } else {
        f["limpio"] = {{"activa", false}};
    }
    if (s.emisor) fotoDelEmisor(s, f); // su transmisión y su grabación, en el otro proceso
    return f;
}

json BroadcastEngine::fotoDeSalida(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    if (s == nullptr) return json::object();
    return fotoSalidas().value("salidas", json::object()).value(s->id, json::object());
}

void BroadcastEngine::publicarFotoSalidas() {
    const auto todas = lasSalidas();
    // Con algo al aire, la latencia del estudio no baja sola (bajarla mueve la línea de tiempo
    // de todas las salidas): solo sube si hace falta; sin nada al aire, se acomoda sola.
    {
        bool aire = false;
        for (const auto& s : todas) {
            // La grabación cortada que se está rearmando también es «al aire».
            aire = aire || (s->stream && s->stream->estado().value("andando", false)) || (s->rec && (s->rec->grabando() || s->rec->caida()));
            aire = aire || (s->emisor && aireEnEmisor(*s));
            for (const auto& [id, x] : s->extras) aire = aire || (x.tx && x.tx->estado().value("andando", false));
        }
        alAire_ = aire;
        if (motor_) motor_->permitirBajarLatencia(!aire);
    }
    // Cada salida en `salidas`; afuera, lo de siempre: el de la por omisión (y los
    // destinos extra de todas, que tienen ids únicos y dicen de qué salida son).
    json porSalida = json::object(), extras = json::object();
    bool grabaAlguna = false;
    for (const auto& s : todas) {
        json fs = fotoDe(*s);
        for (const auto& [id, x] : fs["extras"].items()) extras[id] = x;
        grabaAlguna = grabaAlguna || fs["grabacion"].value("activa", false);
        porSalida[s->id] = std::move(fs);
    }
    json f = porSalida.value(std::string(kSalidaPorOmision), json::object());
    f["extras"] = std::move(extras);
    f["salidas"] = std::move(porSalida);
    json ind = json::array();
    for (const auto& [n, r] : sourceRecs_)
        ind.push_back({{"nombre", n}, {"path", r.path}, {"audio_propio", r.audioPropio}, {"grabando", r.grab && r.grab->grabando()},
                       {"bytes", r.grab ? r.grab->estado().value("bytes", std::uint64_t{0}) : std::uint64_t{0}},
                       {"pausada", r.grab && r.grab->pausada()}});
    f["individuales"] = std::move(ind);
    f["codificaciones"] = motor::codificacionesAbiertas(); // las sesiones de codificador abiertas, de todas las salidas
    medirDiscoRestante();
    f["disco"] = {{"apuros", discoApuros_},
                  {"restante", discoRestante_},
                  {"mismo_volumen", mismoVolumen_},
                  {"atraso_s", atrasoDisco_},
                  {"registro_trabado_ms", registroTrabadoMs_},
                  {"prioridad", prioridad_},
                  {"automatica", prioridadAuto_},
                  {"modo", modoDisco()},
                  {"pausadas", pausadas_},
                  {"graba_programa", grabaAlguna},
                  {"desde_s", discoApuros_ ? (ahoraMs() - apurosDesdeMs_) / 1000 : 0}};
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    fotoSalidas_ = std::move(f);
}

bool BroadcastEngine::startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente,
                                     const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "startStreaming sin startup"));
        ultimoInicio_ = Inicio::Error;
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) {
        ultimoInicio_ = Inicio::Error;
        return false;
    }
    bool estaba = false;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        estaba = s->streamPedido;
        if (estaba) {
            const bool mismo = (stream.server.empty() || stream.server == s->activeServer) && (stream.key.empty() || stream.key == s->activeKey);
            if (mismo) {
                ultimoInicio_ = Inicio::YaIgual;
                return true;
            }
        }
    }
    if (estaba) {
        if (!cambiarEnCaliente) {
            ultimoInicio_ = Inicio::YaOtroDestino;
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya está transmitiendo a %1; para cambiar a %2 hay que pedirlo explícitamente (cambiar en caliente)"), {destinoActivo(s->id), enmascarar(stream.server, stream.key)}));
            return false;
        }
        stopStreaming(s->id);
    }
    ultimoInicio_ = Inicio::Error;
    if (stream.server.empty()) {
        setError(ba::i18n::traducir("emision", "startStreaming sin servidor"));
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        s->streamPedido = true;
        s->activeServer = stream.server;
        s->activeKey = stream.key;
    }
    ultimoInicio_ = Inicio::Iniciado;
    enSalidas("startStreaming", [this, s, stream, enc]() { return startStreamingYa(*s, stream, enc); });
    return true;
}

std::string BroadcastEngine::startStreamingYa(Salida& s, const StreamConfig& stream, const EncoderConfig& enc) {
    anotarAac(s, enc.audioBitrateKbps);
    if (s.emisor) {
        // Al emisor aparte: la misma configuración. Si ya transmite a ese destino (la app
        // vuelve tras caerse), sigue esa: la conexión no se toca.
        const motor::Transmision::Config c =
            configTransmision(urlConClave(stream.server, stream.key), enc, video_, videoEncoderCandidates(enc));
        leerEmisor(s);
        const bool adopta = emisorLaTiene(s, "transmision");
        const std::string e = pedirAlEmisor(s, [&](emisor::Pedido& p) {
            if (!(adopta && p.transmision && p.transmision->url == c.url)) p.transmision = c;
        });
        if (!e.empty()) {
            std::lock_guard<std::mutex> l(pedidosM_);
            s.streamPedido = false;
        }
        return e;
    }
    auto tx = std::make_unique<motor::Transmision>(motor_->reloj(), configTransmision(urlConClave(stream.server, stream.key), enc, video_,
                                                                                      videoEncoderCandidates(enc)));
    const motor::Fuentes f = fuentesDelPrograma(*motor_, s.id, {});
    s.fuenteVideo = f.video;
    const std::string e = tx->iniciar(f);
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        s.streamPedido = false;
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la transmisión: %1"), {e});
    }
    s.stream = std::move(tx);
    return {};
}

void BroadcastEngine::stopStreaming(const std::string& salida) {
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        s->streamPedido = false;
    }
    enSalidas("stopStreaming", [this, s]() {
        if (s->stream) s->stream->parar();
        s->stream.reset();
        if (s->emisor) return pedirAlEmisor(*s, [](emisor::Pedido& p) { p.transmision.reset(); });
        return std::string();
    });
}

bool BroadcastEngine::startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                                 const std::string& salida, const std::string& serverRespaldo) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    if (server.empty()) {
        setError(ba::i18n::traducir("emision", "el destino no tiene servidor"));
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return false;
    {
        // El id de un destino es único entre todas las salidas: si ya sale (por la que sea), está.
        std::lock_guard<std::mutex> l(pedidosM_);
        for (const auto& [sid, o] : salidas_)
            if (o->extrasPedidos.count(id) > 0) return true;
        s->extrasPedidos.insert(id);
    }
    enSalidas("startExtra", [this, s, id, server, key, enc, serverRespaldo]() { return startExtraYa(*s, id, server, key, enc, serverRespaldo); });
    return true;
}

std::string BroadcastEngine::startExtraYa(Salida& s, const std::string& id, const std::string& server, const std::string& key,
                                          const EncoderConfig& enc, const std::string& serverRespaldo) {
    auto it = s.extras.find(id);
    if (it != s.extras.end() && it->second.tx && it->second.tx->andando()) return {};
    if (it != s.extras.end()) {
        if (it->second.tx) it->second.tx->parar();
        s.extras.erase(it);
    }
    anotarAac(s, enc.audioBitrateKbps);
    motor::Transmision::Config c = configTransmision(urlConClave(server, key), enc, video_, videoEncoderCandidates(enc));
    // El punto de ingest de respaldo, con la misma clave: otra rama del mismo codificador.
    if (!serverRespaldo.empty()) c.urlRespaldo = urlConClave(serverRespaldo, key);
    Extra x;
    x.server = server;
    x.key = key;
    x.serverRespaldo = serverRespaldo;
    x.tx = std::make_unique<motor::Transmision>(motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*motor_, s.id, {});
    s.fuenteVideo = f.video;
    const std::string e = x.tx->iniciar(f);
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        s.extrasPedidos.erase(id);
        return ba::i18n::llenar(ba::i18n::traducir("emision", "el destino «%1» no arrancó: %2"), {id, e});
    }
    s.extras[id] = std::move(x);
    return {};
}

void BroadcastEngine::stopExtra(const std::string& id) {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        for (const auto& [sid, s] : salidas_) s->extrasPedidos.erase(id);
    }
    enSalidas("stopExtra", [this, id]() {
        for (const auto& s : lasSalidas()) {
            auto it = s->extras.find(id);
            if (it == s->extras.end()) continue;
            if (it->second.tx) it->second.tx->parar();
            s->extras.erase(it);
        }
        return std::string();
    });
}

void BroadcastEngine::stopExtras() {
    std::set<std::string> ids;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        for (const auto& [sid, s] : salidas_) ids.insert(s->extrasPedidos.begin(), s->extrasPedidos.end());
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
                   {"destino", st.value("destino", std::string())},
                   {"salida", st.value("salida", std::string(kSalidaPorOmision))},
                   {"ingest_respaldo", st.value("respaldo", json::object())}}; // el punto de respaldo, si tiene
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

bool BroadcastEngine::startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "startRecording sin startup"));
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return false;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (s->grabacionPedida) return true;
        s->grabacionPedida = true;
    }
    s->lastRec = rec;
    s->lastRecValido = true;
    const std::string pedido = rec.container.empty() ? std::string("mkv") : rec.container;
    // MP4/MOV no sobreviven a un corte: se graba en MKV y se pasa al terminar.
    const std::string remuxA = pedido == "mp4" || pedido == "mov" ? pedido : std::string();
    // Las pistas 2-6 (la Matriz) siguen a la mezcla de la por omisión: solo ella las graba.
    std::vector<int> pistas;
    s->pistasGrabacionMask = 0;
    for (int p : rec.pistas)
        if (s->porOmision() && p >= 2 && p <= 6 && std::find(pistas.begin(), pistas.end(), p) == pistas.end()) {
            pistas.push_back(p);
            s->pistasGrabacionMask |= 1 << (p - 1);
        }
    std::sort(pistas.begin(), pistas.end());
    // Las de las demás salidas llevan su id: dos salidas grabando a la vez no se pisan.
    const std::string nombre = s->porOmision() ? "ba-" + marcaDeTiempo() : "ba-" + s->id + "-" + marcaDeTiempo();
    enSalidas("startRecording", [this, s, rec, enc, pistas, nombre, remuxA, alTerminar = rec.remuxAlTerminar]() {
        s->remuxSal = remuxA; // lo que mira stopRecordingYa, en este hilo
        s->remuxAlTerminarSal = alTerminar;
        return startRecordingYa(*s, rec, enc, pistas, nombre);
    });
    return true;
}

namespace {
/// Los bitrates de la grabación del programa: «archivo» graba al triple que la transmisión.
std::pair<int, int> kbpsVideoAudio(const RecordConfig& rec, const EncoderConfig& enc) {
    const bool archivo = rec.calidad == "archivo";
    return {archivo ? enc.videoBitrateKbps * 3 : enc.videoBitrateKbps, std::max(enc.audioBitrateKbps, archivo ? 256 : 160)};
}
} // namespace

int BroadcastEngine::kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool porOmision) {
    const auto [video, audio] = kbpsVideoAudio(rec, enc);
    // Las pistas 2-6 (solo la por omisión las graba) suman su audio.
    std::set<int> pistas;
    for (int p : rec.pistas)
        if (porOmision && p >= 2 && p <= 6) pistas.insert(p);
    return video + audio * static_cast<int>(1 + pistas.size());
}

std::string BroadcastEngine::startRecordingYa(Salida& s, const RecordConfig& rec, const EncoderConfig& enc,
                                              const std::vector<int>& pistas, const std::string& nombre) {
    if (s.rec && s.rec->grabando()) return {};
    motor::Grabacion::Config c;
    c.carpeta = rec.directory;
    c.nombre = nombre;
    // En tramos de 15 min de fábrica: un archivo dañado no se lleva toda la grabación (0 = uno solo).
    c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
    std::tie(c.kbps, c.kbpsAudio) = kbpsVideoAudio(rec, enc);
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video_.fpsNum / std::max(1, video_.fpsDen));
    c.codificadores = videoEncoderCandidates(enc);
    c.colchonSeg = std::max(5, rec.colchonSeg); // lo que más importa: el disco trabado sin perder nada (Configuración → Grabación)
    // «como_stream»: el mismo perfil que la transmisión (comparten el codificador; calidad
    // constante no sirve para la red y la transmisión la toma como VBR). «archivo»: calidad
    // constante (CRF) con el triple de bitrate como tope, su propio perfil.
    if (rec.calidad == "archivo") {
        c.control = motor::ControlTasa::Calidad;
        c.crf = rec.calidadCrf;
    } else {
        c.control = motor::controlTasaDe(enc.rateControl);
        if (c.control == motor::ControlTasa::Calidad) c.control = motor::ControlTasa::VBR;
    }
    c.carpetaRespaldo = rec.copiaDir; // si el disco de la grabación falla, la parte que sigue va a la de la copia
    // La copia en otro disco: lo mismo ya codificado, en otra carpeta (con
    // «record.copiaCodificadorPropio», un encode aparte).
    motor::Grabacion::Config cc = c;
    cc.carpeta = rec.copiaDir;
    cc.carpetaRespaldo.clear();
    cc.codificadorPropio = ajustes::siNo("record.copiaCodificadorPropio", false);
    if (s.emisor) {
        // Al emisor aparte. Si ya graba (la app vuelve tras caerse), sigue en ese archivo.
        // Las pistas 2-6 todavía no van por el emisor: solo la del programa.
        leerEmisor(s);
        const bool adopta = emisorLaTiene(s, "grabacion");
        const std::string e = pedirAlEmisor(s, [&](emisor::Pedido& p) {
            if (adopta && p.grabacion) return;
            p.grabacion = c;
            if (rec.copiaDir.empty())
                p.copia.reset();
            else
                p.copia = cc;
        });
        if (!e.empty()) {
            std::lock_guard<std::mutex> l(pedidosM_);
            s.grabacionPedida = false;
            return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la grabación: %1"), {e});
        }
        if (!pistas.empty())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "grabacion", 0,
                                       ba::i18n::traducir("grabacion", "con el emisor aparte se graba solo la pista del programa (las pistas 2-6 todavía no)"));
        s.kbpsRec = kbpsGrabacion(rec, enc, false);
        const auto& g = *s.pedidoEmisor->grabacion;
        s.recordPath = (std::filesystem::path(g.carpeta) / (g.nombre + "-00000.mkv")).string();
        s.recordCopyPath = s.pedidoEmisor->copia
                               ? (std::filesystem::path(s.pedidoEmisor->copia->carpeta) / (s.pedidoEmisor->copia->nombre + "-00000.mkv")).string()
                               : std::string();
        return {};
    }
    auto g = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*motor_, s.id, pistas);
    s.fuenteVideo = f.video;
    const std::string e = g->iniciar(f);
    if (!e.empty()) {
        std::lock_guard<std::mutex> l(pedidosM_);
        s.grabacionPedida = false;
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la grabación: %1"), {e});
    }
    s.rec = std::move(g);
    s.kbpsRec = kbpsGrabacion(rec, enc, s.porOmision());
    s.recordPath = (std::filesystem::path(rec.directory) / (c.nombre + "-00000.mkv")).string();
    // La copia en otro disco: lo mismo ya codificado (si no arranca, la principal sigue). Con
    // «record.copiaCodificadorPropio», un encode aparte (cubre también un codificador que se traba).
    s.recordCopyPath.clear();
    if (!rec.copiaDir.empty()) {
        auto copia = std::make_unique<motor::Grabacion>(motor_->reloj(), cc);
        if (copia->iniciar(f).empty()) {
            s.recCopia = std::move(copia);
            s.recordCopyPath = (std::filesystem::path(rec.copiaDir) / (cc.nombre + "-00000.mkv")).string();
        } else {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "grabacion", 0, ba::i18n::llenar(ba::i18n::traducir("grabacion", "la copia en %1 no arrancó"), {rec.copiaDir}));
        }
    }
    return {};
}

void BroadcastEngine::stopRecording(const std::string& salida) {
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        s->grabacionPedida = false;
    }
    enSalidas("stopRecording", [this, s]() {
        stopRecordingYa(*s);
        return std::string();
    });
}

void BroadcastEngine::stopRecordingYa(Salida& s) {
    if (s.emisor) {
        leerEmisor(s);
        if (!s.pedidoEmisor->grabacion && !s.pedidoEmisor->copia && !emisorLaTiene(s, "grabacion")) return;
        if (const std::string e = pedirAlEmisor(s, [](emisor::Pedido& p) {
                p.grabacion.reset();
                p.copia.reset();
            });
            !e.empty()) {
            avisarFallo(e);
            return;
        }
        // Como parar() acá: se espera que el emisor cierre el archivo (su estado sin
        // grabación con este pedido aplicado), para pasarlo a MP4/MOV si se pidió.
        const std::uint64_t seq = s.pedidoEmisor->seq;
        std::vector<std::string> archivos;
        for (int i = 0; i < 100; ++i) {
            s.estadoEmisorMs = 0;
            leerEmisor(s);
            const json g = s.estadoEmisor.value("grabacion", json());
            if (g.is_object() && g.contains("archivos") && g["archivos"].is_array() && !g["archivos"].empty())
                archivos = g["archivos"].get<std::vector<std::string>>();
            if (s.estadoEmisor.value("seq", std::uint64_t{0}) >= seq && !g.is_object()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!archivos.empty()) s.recordPath = archivos.back();
        if (!s.remuxSal.empty() && s.remuxAlTerminarSal && !archivos.empty()) remuxVarios(archivos, s.remuxSal, true);
        return;
    }
    if (s.recCopia) {
        s.recCopia->parar();
        s.recCopia.reset();
    }
    if (!s.rec) return;
    const bool estaba = s.rec->grabando();
    s.rec->parar(); // espera el cierre del archivo: por eso en este hilo
    const auto archivos = s.rec->archivos();
    if (!archivos.empty()) s.recordPath = archivos.back();
    s.rec.reset();
    // Con la grabación partida en tramos (Configuración → avanzado), se pasan todos.
    if (estaba && !s.remuxSal.empty() && s.remuxAlTerminarSal && !archivos.empty()) remuxVarios(archivos, s.remuxSal, true);
}

std::string BroadcastEngine::archivoGrabacion(const std::string& salida) const {
    return fotoDeSalida(salida).value("grabacion", json::object()).value("archivo", std::string());
}

std::string BroadcastEngine::archivoCopiaGrabacion(const std::string& salida) const {
    return fotoDeSalida(salida).value("grabacion", json::object()).value("copia", std::string());
}

nlohmann::json BroadcastEngine::grabacionEstado(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    if (s == nullptr) return json::object();
    json pistas = json::array({1});
    for (int i = 1; i < 6; ++i)
        if (s->pistasGrabacionMask & (1 << i)) pistas.push_back(i + 1);
    const json g = fotoDeSalida(s->id).value("grabacion", json::object());
    const json st = g.value("estado", json::object());
    return {{"activa", g.value("activa", false)},
            {"caida", g.value("caida", false)},
            {"parte", st.value("parte", 1)},
            {"cortes", st.value("cortes", 0)},
            {"archivo", g.value("archivo", std::string())},
            {"copia", g.value("copia", std::string())},
            {"copia_activa", g.value("copia_activa", false)},
            {"calidad", s->lastRecValido ? s->lastRec.calidad : std::string("como_stream")},
            {"encoder", st.value("codificador", std::string())},
            {"pistas", pistas},
            {"encoder_respaldo_activo", fotoDeSalida(s->id).value("codificador_respaldo", false)}};
}

bool BroadcastEngine::remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) {
    return remuxVarios({archivo}, formato, borrarOrigen);
}

bool BroadcastEngine::remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen) {
    if (archivos.empty()) return false;
    const std::string archivo = archivos.front();
    std::lock_guard<std::mutex> lr(remuxM_); // lo lanza también el hilo de salidas (al parar)
    if (remux_ && remux_->andando) {
        setError(ba::i18n::traducir("emision", "ya hay un remux andando"));
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
                st->error = e.empty() ? ba::i18n::traducir("emision", "el archivo nuevo no se verificó (tamaño)") : e;
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

bool BroadcastEngine::setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return false;
    enSalidas("setReplay", [this, s, segundos, dir, enc]() { return setReplayYa(*s, segundos, dir, enc); });
    return true;
}

std::string BroadcastEngine::setReplayYa(Salida& s, int segundos, const std::string& dir, const EncoderConfig& enc) {
    s.replay.reset();
    if (segundos <= 0) return {};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto r = std::make_unique<Repeticion>();
    r->dir = dir;
    r->rep = std::make_unique<motor::Repeticion>(motor_->reloj(), (std::filesystem::path(dir) / ".tramos").string(), segundos,
                                                 enc.videoBitrateKbps);
    const std::string e = r->rep->iniciar(fuentesDelPrograma(*motor_, s.id, {}));
    if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la repetición: %1"), {e});
    if (prioridad_) r->rep->pausar(true);
    s.replay = std::move(r);
    return {};
}

bool BroadcastEngine::guardarReplay(const std::string& salida) {
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return false;
    if (!fotoDeSalida(s->id).value("replay", json::object()).value("activa", false)) {
        setError(ba::i18n::traducir("emision", "la repetición está apagada (replay_ajustes con segundos)"));
        return false;
    }
    enSalidas("guardarReplay", [s]() {
        if (!s->replay || !s->replay->rep->andando() || s->replay->guardando) return std::string();
        Repeticion* r = s->replay.get();
        r->guardando = true;
        const std::string pre = s->porOmision() ? std::string("repeticion-") : "repeticion-" + s->id + "-";
        const std::string destino = (std::filesystem::path(r->dir) / (pre + marcaDeTiempo() + ".mkv")).string();
        std::thread([r, destino]() {
            const std::string e = r->rep->guardar(destino);
            {
                std::lock_guard<std::mutex> l(r->mu);
                if (e.empty()) r->ultimo = destino;
            }
            if (!e.empty()) ba::diag::registro().linea(ba::diag::Nivel::Aviso, "repeticion", 0, ba::i18n::llenar(ba::i18n::traducir("repeticion", "no se guardó: %1"), {e}));
            r->guardando = false;
        }).detach();
        return std::string();
    });
    return true;
}

std::string BroadcastEngine::ultimoReplay(const std::string& salida) const {
    return fotoDeSalida(salida).value("replay", json::object()).value("ultimo", std::string());
}

nlohmann::json BroadcastEngine::replayEstado(const std::string& salida) const {
    const json r = fotoDeSalida(salida).value("replay", json::object());
    return {{"activa", r.value("activa", false)}, {"segundos", r.value("segundos", 0)}, {"ultimo", r.value("ultimo", std::string())}};
}

bool BroadcastEngine::encoderRespaldoActivo() const {
    const json todas = fotoSalidas().value("salidas", json::object());
    for (const auto& [id, f] : todas.items())
        if (f.value("codificador_respaldo", false)) return true;
    return false;
}

// Una sola atención: cada salida (y cada codificador que lee, dentro de su tic) se
// rearma sola acá, cada 100 ms: la transmisión reconecta, la grabación sigue en otra
// parte, el codificador que falló pasa a la lista negra y se sigue con otro.
void BroadcastEngine::ticSalidas() {
    for (const auto& s : lasSalidas()) {
        if (s->emisor) leerEmisor(*s); // lo que hace su emisor (cada medio segundo)
        if (s->stream) s->stream->tic();
        for (auto& [id, x] : s->extras)
            if (x.tx) x.tx->tic();
        if (s->rec) s->rec->tic();
        if (s->recCopia) s->recCopia->tic();
        if (s->replay) s->replay->rep->tic();
        if (s->clean) {
            if (s->clean->grab) s->clean->grab->tic();
            if (s->clean->tx) s->clean->tx->tic();
        }
    }
    for (auto& [n, r] : sourceRecs_) {
        if (r.grab) r.grab->tic();
        if (r.tx) r.tx->tic();
    }
    vigilarDisco();
}

// --- El disco en apuros ---------------------------------------------------------------------

namespace {
// Umbrales: entra en apuros con 3 s esperando al disco (o el registro trabado
// 3 s escribiendo); sale con todo al día durante 30 s. Los dos primeros se
// ajustan en Configuración → Grabación (ponerUmbralesDisco).
std::atomic<double> kAtrasoApurosS{3.0};
std::atomic<std::int64_t> kRegistroTrabadoMs{3000}, kBienDuranteMs{30000};
constexpr double kAtrasoBienS = 0.5;
} // namespace

void BroadcastEngine::medirDiscoRestante() {
    const std::int64_t ahora = ahoraMs();
    if (discoRestanteMs_ != 0 && ahora - discoRestanteMs_ < 5000) return;
    discoRestanteMs_ = ahora;
    namespace fs = std::filesystem;
    using ba::i18n::traducir;
    // Todo lo que escribe ahora, de todas las salidas: cada disco reparte su espacio
    // libre entre lo que va a él (la copia en el mismo disco lo gasta el doble de rápido).
    std::vector<ba::platform::EscrituraDisco> e;
    json mismo = json::array();
    for (const auto& s : lasSalidas()) {
        const std::string de = s->porOmision() ? std::string() : " (" + s->id + ")";
        const bool graba = s->rec && s->rec->grabando(), copia = s->recCopia && s->recCopia->grabando();
        const fs::path dirRec = fs::path(s->recordPath).parent_path(), dirCopia = fs::path(s->recordCopyPath).parent_path();
        if (graba) e.push_back({traducir("emision", "grabación") + de, dirRec, static_cast<double>(s->kbpsRec)});
        if (copia) e.push_back({traducir("emision", "copia de la grabación") + de, dirCopia, static_cast<double>(s->kbpsRec)});
        // Las dos en el mismo disco: si el disco falla o se llena, se pierden las dos.
        if (graba && copia && ba::platform::mismoVolumen(dirRec, dirCopia))
            mismo.push_back({{"salida", s->id}, {"grabacion", dirRec.string()}, {"copia", dirCopia.string()}});
        if (s->clean && s->clean->grab && s->clean->grab->grabando())
            e.push_back({traducir("emision", "señal limpia a disco") + de, fs::path(s->clean->path).parent_path(), static_cast<double>(s->clean->kbps)});
    }
    for (const auto& [n, r] : sourceRecs_)
        if (r.grab && r.grab->grabando())
            e.push_back({ba::i18n::llenar(traducir("emision", "ISO %1"), {n}), fs::path(r.path).parent_path(), static_cast<double>(r.kbps)});
    discoRestante_ = ba::platform::aJson(ba::platform::discoRestante(e));
    mismoVolumen_ = std::move(mismo);
}

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
    if (modo != "preguntar" && modo != "auto" && modo != "nunca") return ba::i18n::traducir("emision", "modo: preguntar, auto o nunca");
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
        if (porOmision().rec) porOmision().rec->simularDiscoTrabado(ms);
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
    auto pausar = [&](motor::Grabacion* g, const std::string& nombre) {
        if (g == nullptr || !g->grabando()) return;
        g->pausar(si);
        if (si) pausadas_.push_back(nombre);
    };
    for (auto& [n, r] : sourceRecs_) pausar(r.grab.get(), ba::i18n::llenar(ba::i18n::traducir("emision", "ISO %1"), {n}));
    // La grabación del programa de cada salida sigue; lo demás de cada una, en pausa.
    for (const auto& s : lasSalidas()) {
        const std::string de = s->porOmision() ? std::string() : " (" + s->id + ")";
        if (s->replay && s->replay->rep) {
            s->replay->rep->pausar(si);
            if (si && s->replay->rep->andando()) pausadas_.push_back(ba::i18n::traducir("emision", "repetición") + de);
        }
        // La copia, solo si va al mismo disco (en otro no molesta, y es el respaldo).
        const std::string dirProg = s->recordPath.empty() ? std::string() : std::filesystem::path(s->recordPath).parent_path().string();
        if (s->recCopia && (!si || dirProg.empty() || ba::platform::mismoVolumen(dirProg, std::filesystem::path(s->recordCopyPath).parent_path())))
            pausar(s->recCopia.get(), ba::i18n::traducir("emision", "copia de la grabación") + de);
        if (s->clean && s->clean->grab) pausar(s->clean->grab.get(), ba::i18n::traducir("emision", "señal limpia a disco") + de);
    }
    ba::diag::retenerSalidaEstandar(si);
    if (si) pausadas_.push_back(ba::i18n::traducir("emision", "registro (en memoria)"));
    std::string lista;
    for (const auto& p : pausadas_) lista += (lista.empty() ? "" : ", ") + p;
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "disco", 0,
                              si ? ba::i18n::llenar(automatica ? ba::i18n::traducir("disco", "el disco, para la grabación del programa (solo): en pausa %1")
                                                               : ba::i18n::traducir("disco", "el disco, para la grabación del programa: en pausa %1"),
                                                    {lista})
                                 : ba::i18n::traducir("disco", "el disco vuelve a ser de todos: siguen las grabaciones pausadas y el registro"));
}

namespace {
std::string discoBien(bool habia) {
    return habia ? ba::i18n::traducir("emision", "El disco anda bien de nuevo: siguen las ISO, la repetición y el registro.")
                 : ba::i18n::traducir("emision", "El disco anda bien de nuevo.");
}
} // namespace

bool BroadcastEngine::esAvisoDeDiscoBien(const std::string& texto) {
    return texto == discoBien(true) || texto == discoBien(false);
}

void BroadcastEngine::vigilarDisco() {
    const std::int64_t ahora = ahoraMs();
    double atraso = 0;
    auto mirar = [&](const motor::Grabacion* g) {
        if (g != nullptr && g->grabando() && !g->pausada()) atraso = std::max(atraso, g->atrasoS());
    };
    bool grabaPrograma = false;
    for (const auto& s : lasSalidas()) {
        mirar(s->rec.get());
        mirar(s->recCopia.get());
        if (s->clean) mirar(s->clean->grab.get());
        if (s->replay && s->replay->rep) atraso = std::max(atraso, s->replay->rep->atrasoS());
        grabaPrograma = grabaPrograma || (s->rec && s->rec->grabando());
    }
    for (auto& [n, r] : sourceRecs_) mirar(r.grab.get());
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
        const std::string causa = atraso > kAtrasoApurosS ? ba::i18n::llenar(ba::i18n::traducir("emision", "lo grabado espera %1 para entrar al disco"), {t})
                                                          : ba::i18n::traducir("emision", "el registro no puede escribir");
        if (modoDisco_.load() == 1 && grabaPrograma) {
            aplicarPrioridad(true, true);
            avisarFallo(ba::i18n::llenar(ba::i18n::traducir("emision", "El disco no da abasto (%1): se le dio el disco a la grabación del programa. "
                                                                   "Las ISO, la repetición y el registro siguen cuando el disco se recupere."),
                                         {causa}));
        } else {
            avisarFallo(ba::i18n::llenar(
                grabaPrograma && modoDisco_.load() == 0
                    ? ba::i18n::traducir("emision", "El disco no da abasto (%1). Conviene darle el disco a la grabación del programa (pausar ISO, repetición y registro): "
                                                    "el botón está en el aviso de arriba.")
                    : ba::i18n::traducir("emision", "El disco no da abasto (%1)."),
                {causa}));
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
            avisarFallo(discoBien(habia));
        }
    }
}

// --- Sincronía labios/voz con un aplauso ------------------------------------------



bool BroadcastEngine::empezarMedicionSincro() {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    if (!medSincro_) medSincro_ = std::make_shared<MedicionSincro>();
    return true;
}

nlohmann::json BroadcastEngine::terminarMedicionSincro() {
    auto m = medSincro_;
    if (!m) return {{"ok", false}, {"problema", ba::i18n::traducir("emision", "no se estaba midiendo")}};
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
        setError(ba::i18n::traducir("emision", "startSourceRecording sin startup"));
        return false;
    }
    if (sources_.count(source) == 0) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente \"%1\""), {source}));
        return false;
    }
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (individualesPedidas_.count(source) > 0) {
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya se esta grabando \"%1\" por separado"), {source}));
            return false;
        }
        if (static_cast<int>(individualesPedidas_.size()) >= kMaxIndividuales) {
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya hay %1 entradas grabandose por separado, que es el tope"), {std::to_string(kMaxIndividuales)}));
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
    c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
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
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la grabación de \"%1\": %2"), {source, err});
    }
    r.path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
    r.kbps = c.kbps + c.kbpsAudio;
    if (prioridad_) r.grab->pausar(true); // el disco es del programa: arranca en pausa
    sourceRecs_[source] = std::move(r);
    return {};
}

bool BroadcastEngine::stopSourceRecording(const std::string& source) {
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (individualesPedidas_.erase(source) == 0) {
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no se estaba grabando \"%1\" por separado"), {source}));
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
                                     const EncoderConfig& enc, const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return false;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        if (s->limpioPedido) return true;
        s->limpioPedido = true;
    }
    enSalidas("startCleanFeed", [this, s, destino, clave, dir, enc]() { return startCleanFeedYa(*s, destino, clave, dir, enc); });
    return true;
}

std::string BroadcastEngine::startCleanFeedYa(Salida& s, const std::string& destino, const std::string& clave,
                                              const std::string& dir, const EncoderConfig& enc) {
    if (s.clean) return {};
    const auto fallo = [this, &s](const std::string& e) {
        std::lock_guard<std::mutex> l(pedidosM_);
        s.limpioPedido = false;
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la señal limpia: %1"), {e});
    };
    auto r = std::make_unique<SourceRec>();
    const motor::Fuentes f = fuentesDelPrograma(*motor_, s.id, {}, true); // el programa SIN capas
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
        c.nombre = (s.porOmision() ? std::string("senal-limpia-") : "senal-limpia-" + s.id + "-") + marcaDeTiempo();
        c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
        c.kbps = enc.videoBitrateKbps;
        c.fps = fps;
        c.codificadores = {"x264enc"};
        r->grab = std::make_unique<motor::Grabacion>(motor_->reloj(), c);
        const std::string e = r->grab->iniciar(f);
        if (!e.empty()) return fallo(e);
        r->path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
        r->destino = r->path;
        r->kbps = c.kbps + c.kbpsAudio;
    }
    s.clean = std::move(r);
    return {};
}

void BroadcastEngine::stopCleanFeed(const std::string& salida) {
    std::shared_ptr<Salida> s = laSalidaCompartida(salida);
    if (!s) return;
    {
        std::lock_guard<std::mutex> l(pedidosM_);
        s->limpioPedido = false;
    }
    enSalidas("stopCleanFeed", [s]() {
        if (!s->clean) return std::string();
        if (s->clean->tx) s->clean->tx->parar();
        if (s->clean->grab) s->clean->grab->parar();
        s->clean.reset();
        return std::string();
    });
}

nlohmann::json BroadcastEngine::cleanFeedEstado(const std::string& salida) const {
    return fotoDeSalida(salida).value("limpio", json{{"activa", false}});
}

// ============================================================================
// Estado
// ============================================================================

namespace {
/// Sale algo de la foto de una salida (ver BroadcastEngine::fotoDe).
bool saleAlgo(const json& f, bool contarGrabacion) {
    if (f.value("principal", json::object()).value("andando", false)) return true;
    const json extras = f.value("extras", json::object()); // con nombre: .items() de un temporal lo usa destruido
    for (const auto& [id, x] : extras.items())
        if (x.value("andando", false)) return true;
    // La señal limpia se transmite (tiene destino) o se graba.
    const json limpio = f.value("limpio", json::object());
    if (limpio.value("activa", false) && (contarGrabacion || !limpio.value("destino", std::string()).empty()))
        return true;
    return contarGrabacion && f.value("grabacion", json::object()).value("activa", false);
}
} // namespace

bool BroadcastEngine::haySalida(bool contarGrabacion) const {
    const json todas = fotoSalidas().value("salidas", json::object());
    for (const auto& [id, f] : todas.items())
        if (saleAlgo(f, contarGrabacion)) return true;
    return false;
}

bool BroadcastEngine::haySalidaEn(const std::string& salida, bool contarGrabacion) const {
    return saleAlgo(fotoDeSalida(salida), contarGrabacion);
}

std::vector<std::string> BroadcastEngine::fuentesEnPrograma() const {
    std::vector<std::string> out;
    for (const auto& id : ordenSalidas_)
        for (const std::string& f : fuentesEnPrograma(id))
            if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    return out;
}

std::vector<std::string> BroadcastEngine::fuentesEnPrograma(const std::string& salida) const {
    std::vector<std::string> out;
    const Salida* s = laSalida(salida);
    if (s == nullptr) return out;
    if (const EscenaApp* e = escena(s->alAire))
        for (const auto& i : e->items) {
            if (!i.visible) continue;
            // «Banca con la palabra»: lo que se ve es su cámara. Recortada de la toma
            // entera (digital) no cuenta: mover el PTZ de esa cámara no la cambia.
            const auto f = sources_.find(i.fuente);
            if (f != sources_.end() && f->second.banca) {
                const int r = ranuraDe(f->second);
                if (r >= 1 && r <= static_cast<int>(ranurasBanca_.size()) && !ranurasBanca_[r - 1].entrada.empty() &&
                    !ranurasBanca_[r - 1].entera)
                    out.push_back(ranurasBanca_[r - 1].entrada);
                continue;
            }
            out.push_back(i.fuente);
        }
    for (const std::string& f : fuentesDsk(s->id)) out.push_back(f);
    return out;
}

bool BroadcastEngine::fuenteAlAire(const std::string& fuente) const {
    if (fuente.empty()) return false;
    // En alguna salida que esté saliendo.
    for (const auto& id : ordenSalidas_) {
        if (!haySalidaEn(id)) continue;
        const auto ss = fuentesEnPrograma(id);
        if (std::find(ss.begin(), ss.end(), fuente) != ss.end()) return true;
    }
    return false;
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
            st.outputProblem = e.find("refused") != std::string::npos   ? ba::i18n::traducir("emision", "No se pudo conectar con el servidor")
                               : e.find("auth") != std::string::npos    ? ba::i18n::traducir("emision", "El servidor rechazó la transmisión (¿la clave es correcta?)")
                                                                        : ba::i18n::traducir("emision", "Se cortó la conexión con el servidor");
        }
    }
    const json g = f.value("grabacion", json::object());
    st.recording = g.value("activa", false);
    if (st.videoEncoder.empty()) st.videoEncoder = g.value("estado", json::object()).value("codificador", std::string());
    // El resumen de cada salida (la por omisión, igual que los campos de arriba).
    const json todas = f.value("salidas", json::object());
    for (const auto& id : ordenSalidas_) {
        const Salida& s = *salidas_.at(id);
        const json fs = todas.value(id, json::object());
        EngineStatus::Salida r;
        r.id = id;
        r.nombre = s.nombre;
        r.porOmision = s.porOmision();
        r.escena = s.alAire;
        const json p = fs.value("principal", json::object());
        r.streaming = p.value("andando", false);
        r.reconnecting = p.value("reconectando", false);
        r.recording = fs.value("grabacion", json::object()).value("activa", false);
        r.recordingDown = fs.value("grabacion", json::object()).value("caida", false);
        const json ex = fs.value("extras", json::object());
        for (const auto& [eid, x] : ex.items()) r.destinos += x.value("andando", false) ? 1 : 0;
        r.limpia = fs.value("limpio", json::object()).value("activa", false);
        r.repeticion = fs.value("replay", json::object()).value("activa", false);
        st.salidas.push_back(std::move(r));
    }
    return st;
}

} // namespace ba::app
