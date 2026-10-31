// ============================================================================
// src/gateway/MediaPump.cpp — implementacion del motor de medios (ver header)
//
// Estrategia de errores: no lanza. Un canal que no se puede crear se loguea y
// se salta (no tumba al pump); un pop/receive fallido se reintenta.
// ============================================================================

#include "MediaPump.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <set>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "ipc/CanalesSalida.hpp"
#include "ipc/HealthBlock.hpp"

#if BA_GATEWAY_HAVE_WEBRTC
#include "third_party/libyuv/include/libyuv/scale_argb.h"
#endif
#include "ColaVoz.hpp"
#include "Labios.hpp"
#include "Log.hpp"
#include "RetornoLugar.hpp"
#include "platform/Ritmo.hpp"
#include "i18n/I18n.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

using namespace ba::ipc;

/// El nombre de un canal (siempre ASCII: BA.Audio.…, BA.Program…) para el registro y las fotos.
std::string angosto(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (const wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

/// Un número con un decimal, para los textos del registro («12.5 fps»).
std::string unDecimal(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

/// PID del proceso actual (para el writerPid de los rings de salida).
uint32_t currentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(static_cast<long>(getpid()));
#endif
}

/// Generacion del escritor (se incrementa por arranque; aqui siempre 1).
constexpr uint32_t kGeneration = 1;

/// Escala un frame BGRA de (sw x sh) a (dw x dh) por vecino mas cercano.
///
/// :param src: pixeles de origen (sw*sh*4 bytes, BGRA).
/// :param sw/sh: resolucion de origen.
/// :param dst: buffer de destino (dw*dh*4 bytes).
/// :param dw/dh: resolucion de destino.
/// Escala BGRA. Achicar es lo normal acá: el programa es 1080p y el retorno
/// 720p.
///
/// CON FILTRO, no tomando el píxel más cercano. Achicar 1080p a 720p tirando
/// dos de cada tres filas deja fuera dos tercios de la imagen: el texto de un
/// zócalo y los bordes finos titilan, y en un televisor grande se ve como
/// suciedad. El filtro de caja promedia lo que se descarta, que es la
/// diferencia entre «más chico» y «peor».
///
/// Y es de libyuv, o sea con SIMD. El de antes hacía dos divisiones enteras
/// por píxel —novecientas mil por cuadro— en C escalar. Donde no hay libyuv
/// (el backend NULL, el de los tests) queda el de antes: ahí no hay nadie
/// mirando la imagen, solo se comprueba que los bytes lleguen.
void scaleBgra(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
#if BA_GATEWAY_HAVE_WEBRTC
    libyuv::ARGBScale(src, sw * 4, sw, sh, dst, dw * 4, dw, dh, libyuv::kFilterBox);
#else
    for (int y = 0; y < dh; ++y) {
        const int sy = (y * sh) / dh;
        const uint8_t* srow = src + static_cast<size_t>(sy) * sw * 4;
        uint8_t* drow = dst + static_cast<size_t>(y) * dw * 4;
        for (int x = 0; x < dw; ++x) {
            const int sx = (x * sw) / dw;
            const uint8_t* sp = srow + static_cast<size_t>(sx) * 4;
            drow[0] = sp[0]; // B
            drow[1] = sp[1]; // G
            drow[2] = sp[2]; // R
            drow[3] = sp[3]; // A
            drow += 4;
        }
    }
#endif
}

/// Ajusta (sw x sh) DENTRO de (dw x dh) conservando el aspecto, centrado y
/// con barras negras (letterbox/pillarbox, spec §4.2: el slot no cambia de
/// tamaño; B acomoda el frame adentro).
void fitBgra(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    std::memset(dst, 0, static_cast<size_t>(dw) * dh * 4);
    for (size_t i = 3; i < static_cast<size_t>(dw) * dh * 4; i += 4) {
        dst[i] = 0xFF; // alfa opaco en las barras
    }
    if (sw <= 0 || sh <= 0) return;
    int fw = dw;
    int fh = static_cast<int>(static_cast<long long>(sh) * dw / sw);
    if (fh > dh) {
        fh = dh;
        fw = static_cast<int>(static_cast<long long>(sw) * dh / sh);
    }
    const int ox = (dw - fw) / 2;
    const int oy = (dh - fh) / 2;
    for (int y = 0; y < fh; ++y) {
        const int sy = static_cast<int>(static_cast<long long>(y) * sh / fh);
        const uint8_t* srow = src + static_cast<size_t>(sy) * sw * 4;
        uint8_t* drow = dst + (static_cast<size_t>(oy + y) * dw + ox) * 4;
        for (int x = 0; x < fw; ++x) {
            const int sx = static_cast<int>(static_cast<long long>(x) * sw / fw);
            std::memcpy(drow + static_cast<size_t>(x) * 4, srow + static_cast<size_t>(sx) * 4, 4);
        }
    }
}

/// Dónde va un cuadro de `w`×`h` que sube un participante, en un canal de `cw`×`ch` (F2-13,
/// V3): la caja más chica con la forma del canal que lo contiene, sin agrandarlo. Si tiene
/// la forma del canal, él mismo (640×360 en un canal de 1280×720 viaja a 640×360: antes se
/// agrandaba acá, en la CPU, y A lo subía a la GPU cuatro veces más grande); si no, con
/// barras alrededor pero a su escala (un celular parado); si es más grande que el canal, el
/// canal entero (se achica, como siempre). La composición de A lo escala en la GPU: con la
/// misma forma que el canal, el encuadre en la escena no cambia.
void cajaDelCuadro(int w, int h, int cw, int ch, int& bw, int& bh) {
    bw = cw;
    bh = ch;
    if (w <= 0 || h <= 0 || w > cw || h > ch) return;
    const long long a = static_cast<long long>(w) * ch, b = static_cast<long long>(h) * cw;
    if (a >= b) { // más ancho (o igual) que el canal: manda el ancho
        bw = w;
        bh = static_cast<int>((static_cast<long long>(w) * ch + cw - 1) / cw);
    } else {
        bh = h;
        bw = static_cast<int>((static_cast<long long>(h) * cw + ch - 1) / ch);
    }
    bw = std::min(cw, std::max(w, bw + (bw & 1)));
    bh = std::min(ch, std::max(h, bh + (bh & 1)));
}

/// Dibuja la placa de "esperando invitado" del slot: fondo oscuro, una franja
/// con el color del slot y `slot` marcas para identificarlo de un vistazo.
void drawPlate(uint8_t* dst, int w, int h, int slot) {
    static const uint8_t kColors[][3] = {
        {0xE0, 0x8A, 0x2B}, {0x3B, 0xB0, 0x4C}, {0x2B, 0x5D, 0xE0}, {0xB0, 0x3B, 0xA6}};
    const uint8_t* c = kColors[(slot - 1) & 3];
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * w * 4;
        const bool band = y >= h * 45 / 100 && y < h * 55 / 100;
        for (int x = 0; x < w; ++x) {
            uint8_t* p = row + static_cast<size_t>(x) * 4;
            // BGRA
            p[0] = band ? c[2] : 0x22;
            p[1] = band ? c[1] : 0x22;
            p[2] = band ? c[0] : 0x22;
            p[3] = 0xFF;
        }
    }
    // Marcas blancas bajo la franja: tantas como el numero de slot.
    const int mark = std::max(4, h / 20);
    const int y0 = h * 60 / 100;
    for (int i = 0; i < slot; ++i) {
        const int x0 = w / 2 - (slot * 2 - 1) * mark / 2 + i * 2 * mark;
        for (int y = y0; y < y0 + mark && y < h; ++y) {
            for (int x = x0; x < x0 + mark && x < w; ++x) {
                if (x >= 0) {
                    std::memset(dst + (static_cast<size_t>(y) * w + x) * 4, 0xFF, 4);
                }
            }
        }
    }
}

/// Placa del lobby cuando A no publica la camara del operador: fondo oscuro con
/// una franja naranja (la del producto) en el centro.
void drawWaitPlate(uint8_t* dst, int w, int h) {
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * w * 4;
        const bool band = y >= h * 48 / 100 && y < h * 52 / 100;
        for (int x = 0; x < w; ++x) {
            uint8_t* p = row + static_cast<size_t>(x) * 4;
            const bool mid = band && x > w / 3 && x < w * 2 / 3;
            p[0] = mid ? 0x2B : 0x14; // B
            p[1] = mid ? 0x6A : 0x12; // G
            p[2] = mid ? 0xFF : 0x12; // R
            p[3] = 0xFF;
        }
    }
}

/// Cuánta voz espera a mezclarse en otra bajada (ColaVoz.hpp): 300 ms estéreo. Más sería
/// retraso en una charla; si el reloj se para, lo viejo se tira.
size_t kTopeColaVoz(int rate) { return static_cast<size_t>(rate) * 2u * 3u / 10u; }

uint64_t monoNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

MediaPump::MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log) : hub_(hub), cfg_(cfg), log_(log) {}

MediaPump::~MediaPump() { stop(); }

void MediaPump::start() {
    const auto name = [](const std::vector<std::wstring>& v, size_t i) {
        return i < v.size() ? v[i] : std::wstring();
    };

    const auto makeVideo = [this](const std::wstring& n, int w, int h) { return crearVideo(n, w, h); };
    const auto makeAudio = [this](const std::wstring& n) { return crearAudio(n); };

    const size_t nombres = cfg_.guestVideoNames.empty() ? 0 : cfg_.guestVideoNames.size() - 1;
    const size_t guests =
        cfg_.lugaresIniciales > 0 ? std::min(nombres, static_cast<size_t>(cfg_.lugaresIniciales)) : nombres;
    const size_t lobby = cfg_.lobbyVideoNames.empty() ? 0 : cfg_.lobbyVideoNames.size() - 1;
    lobbyVideo_.clear();
    backstageVideo_.clear();
    lobbyAudio_.clear();
    lobbyVideo_.emplace_back();
    lobbyAudio_.emplace_back();
    backstageVideo_.emplace_back();
    const size_t backstage = cfg_.backstageVideoNames.empty() ? 0 : cfg_.backstageVideoNames.size() - 1;
    for (size_t k = 1; k <= backstage; ++k) {
        backstageVideo_.push_back(
            makeVideo(name(cfg_.backstageVideoNames, k), cfg_.backstageW, cfg_.backstageH));
        backstageVideo_.back()->minIntervalNs = 100'000'000; // ~10 fps: es una miniatura
    }
    ampliar(static_cast<int>(guests)); // los del cupo de arranque; los demás, al subir el cupo
    for (size_t k = 1; k <= lobby; ++k) {
        lobbyVideo_.push_back(makeVideo(name(cfg_.lobbyVideoNames, k), cfg_.lobbyW, cfg_.lobbyH));
        lobbyAudio_.push_back(makeAudio(name(cfg_.lobbyAudioNames, k)));
    }
    // R3: el integrante remoto con la palabra (a la app, como una entrada más).
    remotoVideo_.clear();
    remotoAudio_.clear();
    remotoVideo_.emplace_back();
    remotoAudio_.emplace_back();
    remotoContenido_.clear();
    remotoContenido_.emplace_back();
    const size_t remotos = cfg_.remotoVideoNames.empty() ? 0 : cfg_.remotoVideoNames.size() - 1;
    for (size_t k = 1; k <= remotos; ++k) {
        remotoVideo_.push_back(makeVideo(name(cfg_.remotoVideoNames, k), cfg_.programW, cfg_.programH));
        remotoContenido_.push_back(makeVideo(name(cfg_.remotoContenidoNames, k), cfg_.programW, cfg_.programH));
        remotoAudio_.push_back(makeAudio(name(cfg_.remotoAudioNames, k)));
    }
    // La atención hablada de la sesión remota: un canal (de a uno por voz).
    atencionAudio_.reset();
    if (!cfg_.atencionAudioName.empty()) atencionAudio_ = makeAudio(cfg_.atencionAudioName);
    // El registro por tótem: un canal por conversación a la vez (1..kMaxTotems).
    registroAudio_.clear();
    registroAudio_.emplace_back();
    for (size_t k = 1; k < cfg_.registroAudioNames.size() && k <= static_cast<size_t>(kMaxTotems); ++k)
        registroAudio_.push_back(makeAudio(cfg_.registroAudioNames[k]));
    registroVideo_.clear();
    registroVideo_.emplace_back();
    for (size_t k = 1; k < cfg_.registroVideoNames.size() && k <= static_cast<size_t>(kMaxTotems); ++k)
        registroVideo_.push_back(makeVideo(cfg_.registroVideoNames[k], cfg_.registroW, cfg_.registroH));
    {
        std::lock_guard<std::mutex> lock(routesLock_);
        routes_.assign(static_cast<size_t>(hub_.sessions()) + 1, MediaRoute{});
        pedidas_ = routes_;
    }

    stop_.store(false);
    threads_.emplace_back(&MediaPump::retornoAudioThread, this, cfg_.returnAudioName, std::string(), &ifb_, nullptr);
    threads_.emplace_back(&MediaPump::videoBajadaThread, this, cfg_.programVideoName, traducir("gateway", "retorno video"),
                          [](const MediaRoute& r) { return r.miraSalida(""); }, cfg_.returnAudioName, nullptr);
    for (size_t k = 0; k < cfg_.escenaVideoNames.size(); ++k) {
        const int canal = static_cast<int>(k);
        threads_.emplace_back(&MediaPump::videoBajadaThread, this, cfg_.escenaVideoNames[k],
                              llenar(traducir("gateway", "escena %1"), {std::to_string(canal)}), [canal](const MediaRoute& r) {
                                  return r.kind == MediaRoute::Kind::Escena && r.index == canal;
                              }, std::wstring(), nullptr); // una escena no es lo que suena: nada que alinear
    }
    threads_.emplace_back(&MediaPump::operatorAudioThread, this);
    if (!cfg_.atendedorAudioName.empty())
        threads_.emplace_back(&MediaPump::vozAudioThread, this, cfg_.atendedorAudioName,
                              traducir("gateway", "atencion: voz del que atiende (BA.Audio.Atendedor)"),
                              [](const MediaRoute& r) { return r.oyeAlAtendedor(); });
    for (size_t k = 1; k < cfg_.registroVozAudioNames.size() && k <= static_cast<size_t>(kMaxTotems); ++k) {
        if (cfg_.registroVozAudioNames[k].empty()) continue;
        const int canal = static_cast<int>(k);
        threads_.emplace_back(&MediaPump::vozRegistroThread, this, cfg_.registroVozAudioNames[k], canal);
    }
    threads_.emplace_back(&MediaPump::operatorVideoThread, this);
    threads_.emplace_back(&MediaPump::plateThread, this);
    enMarcha_.store(true);
    ajustarBajadas(); // por si alguna ruta ya pedía una salida
    log_.info(llenar(traducir("gateway", "MediaPump: en marcha (slots=%1, lobby=%2, fila=%3, sesiones=%4)"),
                     {std::to_string(guests), std::to_string(lobby), std::to_string(backstage),
                      std::to_string(hub_.sessions())}));
}

void MediaPump::stop() {
    if (threads_.empty()) return; // idempotente (main y el destructor)
    enMarcha_.store(false);
    stop_.store(true);
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    std::map<std::string, std::unique_ptr<BajadaSalida>> bajadas;
    {
        std::lock_guard<std::mutex> l(bajadasLock_);
        bajadas.swap(bajadas_);
    }
    for (auto& [id, b] : bajadas) {
        if (b->video.joinable()) b->video.join();
        if (b->audio.joinable()) b->audio.join();
    }
    log_.info(traducir("gateway", "MediaPump: detenido"));
}

std::unique_ptr<MediaPump::VideoOut> MediaPump::crearVideo(const std::wstring& n, int w, int h) {
    const auto narrow = [](const std::wstring& x) { return std::string(x.begin(), x.end()); };
    auto out = std::make_unique<VideoOut>();
    out->w = w;
    out->h = h;
    out->nombre = narrow(n);
    out->scratch.resize(static_cast<size_t>(w) * h * 4u);
    if (!n.empty()) {
        if (out->sender.publish(n.c_str(), SlotConfig{w, h})) {
            log_.info(llenar(traducir("gateway", "video out: %1 listo (%2x%3)"),
                             {narrow(n), std::to_string(w), std::to_string(h)}));
        } else {
            log_.warn(llenar(traducir("gateway", "video out: no se pudo publicar %1 (se salta)"), {narrow(n)}), -1);
        }
    }
    return out;
}

std::unique_ptr<MediaPump::AudioOut> MediaPump::crearAudio(const std::wstring& n) {
    const auto narrow = [](const std::wstring& x) { return std::string(x.begin(), x.end()); };
    auto out = std::make_unique<AudioOut>();
    if (!n.empty()) {
        if (SharedAudioRing::init(n.c_str(), static_cast<uint32_t>(cfg_.audioChannels),
                                  audio_ring::kDefaultCapacityBlocks, currentPid(), kGeneration, out->ring)
                .ok) {
            log_.info(llenar(traducir("gateway", "audio out: %1 listo"), {narrow(n)}));
        } else {
            log_.warn(llenar(traducir("gateway", "audio out: no se pudo crear %1 (se salta)"), {narrow(n)}), -1);
        }
    }
    return out;
}

void MediaPump::ampliar(int slots) {
    const auto name = [](const std::vector<std::wstring>& v, size_t i) { return i < v.size() ? v[i] : std::wstring(); };
    std::lock_guard<std::mutex> l(canalesLock_);
    const int hasta = std::min(slots, kMaxInvitados);
    const int antes = lugares_;
    for (int s = lugares_ + 1; s <= hasta; ++s) {
        const size_t i = static_cast<size_t>(s);
        videosInvitado_.push_back(crearVideo(name(cfg_.guestVideoNames, i), cfg_.programW, cfg_.programH));
        guestVideo_[i].store(videosInvitado_.back().get(), std::memory_order_release);
        videosInvitado_.push_back(crearVideo(name(cfg_.shareVideoNames, i), cfg_.programW, cfg_.programH));
        shareVideo_[i].store(videosInvitado_.back().get(), std::memory_order_release);
        audiosInvitado_.push_back(crearAudio(name(cfg_.guestAudioNames, i)));
        guestAudio_[i].store(audiosInvitado_.back().get(), std::memory_order_release);
        lugares_ = s;
    }
    if (lugares_ > antes && antes > 0)
        log_.info(llenar(traducir("gateway", "MediaPump: lugares %1 -> %2 (sin reiniciar)"),
                         {std::to_string(antes), std::to_string(lugares_)}));
}

int MediaPump::lugares() const {
    std::lock_guard<std::mutex> l(canalesLock_);
    return lugares_;
}

// --- Rutas -------------------------------------------------------------------

void MediaPump::setRoute(int session, const MediaRoute& route) {
    {
        std::lock_guard<std::mutex> lock(routesLock_);
        if (session < 1 || session >= static_cast<int>(routes_.size())) return;
        pedidas_[static_cast<size_t>(session)] = route;
        routes_[static_cast<size_t>(session)] = salidas_.vigente(route);
    }
    ajustarBajadas();
}

void MediaPump::ponerSalidas(const SalidasConocidas& salidas) {
    {
        std::lock_guard<std::mutex> lock(routesLock_);
        salidas_ = salidas;
        for (size_t s = 1; s < routes_.size(); ++s) routes_[s] = salidas_.vigente(pedidas_[s]);
    }
    ajustarBajadas();
}

void MediaPump::ajustarBajadas() {
    // Las que alguien mira, calculadas CON bajadasLock_ tomado: dos cambios de ruta a
    // la vez no pueden dejar levantada una que el segundo ya soltó.
    std::vector<std::unique_ptr<BajadaSalida>> sueltas;
    {
        std::lock_guard<std::mutex> l(bajadasLock_);
        std::set<std::string> miradas;
        if (enMarcha_.load()) {
            std::lock_guard<std::mutex> lr(routesLock_);
            for (size_t s = 1; s < routes_.size(); ++s)
                if (!routes_[s].salida.empty() && routes_[s].miraSalida(routes_[s].salida)) miradas.insert(routes_[s].salida);
        }
        for (auto it = bajadas_.begin(); it != bajadas_.end();) {
            if (miradas.count(it->first) != 0) {
                ++it;
                continue;
            }
            it->second->parar.store(true);
            log_.info(llenar(traducir("gateway", "salida %1: nadie la mira, se suelta su bajada"), {it->first}));
            sueltas.push_back(std::move(it->second));
            it = bajadas_.erase(it);
        }
        for (const std::string& id : miradas) {
            if (bajadas_.count(id) != 0 || !ba::ipc::idDeSalidaValido(id)) continue;
            auto b = std::make_unique<BajadaSalida>();
            const std::string etiqueta = llenar(traducir("gateway", "salida %1"), {id});
            b->video = std::thread(&MediaPump::videoBajadaThread, this, ba::ipc::nombreCanalSalida(id), etiqueta,
                                   std::function<bool(const MediaRoute&)>([id](const MediaRoute& r) { return r.miraSalida(id); }),
                                   ba::ipc::nombreAudioSalida(id), &b->parar);
            b->audio = std::thread(&MediaPump::retornoAudioThread, this, ba::ipc::nombreAudioSalida(id), id, &b->ifb,
                                   &b->parar);
            log_.info(llenar(traducir("gateway", "salida %1: alguien la mira, se levanta su bajada"), {id}));
            bajadas_.emplace(id, std::move(b));
        }
    }
    // Fuera del candado: los hilos tardan a lo sumo un ciclo (50 ms) en ver `parar`.
    for (auto& b : sueltas) {
        if (b->video.joinable()) b->video.join();
        if (b->audio.joinable()) b->audio.join();
    }
}

std::vector<std::string> MediaPump::bajadasVivas() const {
    std::lock_guard<std::mutex> l(bajadasLock_);
    std::vector<std::string> out;
    for (const auto& [id, b] : bajadas_) out.push_back(id);
    return out;
}

std::map<std::string, int> MediaPump::miranSalidas() const {
    std::map<std::string, int> out;
    std::lock_guard<std::mutex> lock(routesLock_);
    for (size_t s = 1; s < routes_.size(); ++s)
        if (!routes_[s].salida.empty() && routes_[s].miraSalida(routes_[s].salida)) ++out[routes_[s].salida];
    return out;
}

MediaRoute MediaPump::route(int session) const {
    std::lock_guard<std::mutex> lock(routesLock_);
    if (session < 1 || session >= static_cast<int>(routes_.size())) return {};
    return routes_[static_cast<size_t>(session)];
}

template <typename Pred> std::vector<int> MediaPump::sessionsWhere(Pred pred) const {
    std::vector<int> out;
    std::lock_guard<std::mutex> lock(routesLock_);
    for (size_t s = 1; s < routes_.size(); ++s) {
        if (pred(routes_[s])) out.push_back(static_cast<int>(s));
    }
    return out;
}

// --- Bajada: programa ---------------------------------------------------------

namespace {

/// Cada cuanto se mira si A escribio audio: muy por debajo del bloque (10 ms).
constexpr int kMiradaAudioMs = 2;
/// Lo que tarda B en leer un bloque de audio después de que A lo escribe (la mitad de la
/// mirada, de media): se suma a lo que tarda el audio para alinear el video (F2-11).
constexpr std::int64_t kLecturaAudioNs = std::int64_t{kMiradaAudioMs} * 1'000'000 / 2;
/// Lo que dura un bloque de los anillos de audio (480 cuadros a 48 kHz): para contar faltantes
/// en los que sella el motor con la hora del medio (MedidaTransporte).
constexpr std::uint64_t kBloqueNs = std::uint64_t{audio_ring::kBlockFrames} * 1'000'000'000ULL / audio_ring::kSampleRate;

} // namespace

void MediaPump::retornoAudioThread(std::wstring nombre, std::string salida, ColaIfb* ifb,
                                   const std::atomic<bool>* parar) {
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block;
    // Mix-minus: un lector por slot (se abren cuando A los publica). Solo los de la
    // salida por omisión: las otras bajan su retorno entero.
    const size_t nSlots = salida.empty() ? cfg_.returnSlotNames.size() : 0;
    std::vector<SharedAudioRing> slotReaders(nSlots);
    std::vector<bool> slotOpen(nSlots, false);
    std::vector<std::vector<float>> slotBlock(nSlots);
    std::vector<std::unique_ptr<MedidaTransporte>> slotMedida(nSlots);
    std::vector<std::chrono::steady_clock::time_point> slotVentana(nSlots);
    std::vector<float> silencio;
    std::vector<const float*> deSlot(nSlots, nullptr);
    MedidaTransporte medida(salida.empty() ? "BA.Audio.ContentForReturn" : "BA.Audio.Salida." + salida, "audio",
                            kBloqueNs);
    auto ventana = std::chrono::steady_clock::now();
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(kMiradaAudioMs)};
    auto lastSlotRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (sigue(parar)) {
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastSlotRetry >= std::chrono::milliseconds(1000)) {
                lastSlotRetry = now;
                for (size_t s = 1; s < nSlots; ++s) {
                    if (slotOpen[s] && slotReaders[s].stale()) {
                        slotReaders[s] = SharedAudioRing{};
                        slotOpen[s] = false;
                        mixMinusAbierto_[s].store(false);
                        log_.warn(llenar(traducir("gateway", "retorno mix-minus del slot %1 cerrado: ese invitado queda "
                                                             "sin retorno hasta que vuelva"),
                                         {std::to_string(s)}), -1);
                    }
                    if (!slotOpen[s] && !cfg_.returnSlotNames[s].empty() &&
                        SharedAudioRing::openReader(cfg_.returnSlotNames[s].c_str(), slotReaders[s]).ok) {
                        slotOpen[s] = true;
                        mixMinusAbierto_[s].store(true);
                        if (!slotMedida[s])
                            slotMedida[s] = std::make_unique<MedidaTransporte>("BA.Audio.Return." + std::to_string(s),
                                                                               "audio", kBloqueNs);
                        slotMedida[s]->reabierto();
                        slotVentana[s] = now;
                        log_.info(llenar(traducir("gateway", "retorno mix-minus del slot %1 abierto"), {std::to_string(s)}));
                    }
                }
            }
        }
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(nombre.c_str(), reader).ok) {
                    open = true;
                    medida.reabierto();
                    // La por omisión dice lo mismo que siempre.
                    log_.info(salida.empty() ? traducir("gateway", "retorno audio: BA.Audio.ContentForReturn abierto")
                                             : llenar(traducir("gateway", "salida %1: retorno de audio abierto"), {salida}));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (reader.stale()) {
            reader = SharedAudioRing{};
            open = false;
            log_.info(salida.empty()
                          ? traducir("gateway", "retorno audio: A cambio o se cerro; reabriendo")
                          : llenar(traducir("gateway", "salida %1: retorno de audio: A cambio o se cerro; reabriendo"), {salida}));
            continue;
        }
        leerPendientes(reader, block, medida, [&](const PoppedBlock& blk, size_t n) {
            const std::uint64_t ahoraQpc = HealthBlock::nowQpc();
            // El bloque de cada lugar con mix-minus: uno por bloque del programa (RetornoLugar.hpp).
            if (silencio.size() < n) silencio.assign(n, 0.0f);
            for (size_t s = 1; s < nSlots; ++s)
                deSlot[s] = slotOpen[s] ? retorno::bloqueDelLugar(slotReaders[s], slotBlock[s], silencio.data(), n,
                                                                   *slotMedida[s], ahoraQpc)
                                        : nullptr;
            // IFB: la voz del operador de este bloque (silencio si no hay), una vez para todos.
            std::vector<float> voz;
            bool hayVoz = false;
            {
                std::lock_guard<std::mutex> l(ifb->lock);
                if (!ifb->voz.empty()) {
                    voz.assign(n, 0.0f);
                    const size_t k = std::min(n, ifb->voz.size());
                    std::copy(ifb->voz.begin(), ifb->voz.begin() + static_cast<long>(k), voz.begin());
                    ifb->voz.erase(ifb->voz.begin(), ifb->voz.begin() + static_cast<long>(k));
                    hayVoz = true;
                }
            }
            const int prueba = pruebaSesion_.load();
            std::vector<float> mezcla;
            for (int s : sessionsWhere([&salida](const MediaRoute& r) { return r.oyeSalida(salida); })) {
                const MediaRoute r = route(s);
                // Con el retorno apagado (silencio) igual le llega la voz del operador si le habla.
                const float* fuente = retorno::fuente(r, blk.samples, deSlot, silencio.data());
                if ((r.talk && hayVoz) || s == prueba) {
                    mezcla.assign(fuente, fuente + n);
                    if (r.talk && hayVoz) // el programa baja (~-8 dB) para que la voz se entienda
                        for (size_t i = 0; i < n; ++i) mezcla[i] = std::clamp(mezcla[i] * 0.4f + voz[i], -1.0f, 1.0f);
                    if (s == prueba) {
                        // Tono de 1 kHz a -12 dBFS, prendido según un código seudoaleatorio de
                        // 16 tramos de 250 ms (no periódico: un ruido que se repite —un pitido
                        // cada segundo— no se confunde con el eco).
                        const std::int64_t desde = pruebaDesdeNs_.load();
                        const std::int64_t ahoraNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                         std::chrono::steady_clock::now().time_since_epoch()).count();
                        const bool on = codigoPrueba((ahoraNs - desde) / 250'000'000LL);
                        const double paso = 2.0 * 3.14159265358979 * 1000.0 / cfg_.audioRate;
                        for (size_t i = 0; i + 1 < n; i += 2) {
                            const float t = on ? 0.25f * static_cast<float>(std::sin(pruebaFase_)) : 0.0f;
                            pruebaFase_ += paso;
                            mezcla[i] = std::clamp(mezcla[i] * 0.3f + t, -1.0f, 1.0f);
                            mezcla[i + 1] = std::clamp(mezcla[i + 1] * 0.3f + t, -1.0f, 1.0f);
                        }
                        if (pruebaFase_ > 1e6) pruebaFase_ = std::fmod(pruebaFase_, 2.0 * 3.14159265358979);
                    }
                    fuente = mezcla.data();
                }
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalAudio(fuente, n, cfg_.audioRate);
            }
        });
        cerrarVentana(medida, ventana, true);
        for (size_t s = 1; s < nSlots; ++s)
            if (slotOpen[s]) cerrarVentana(*slotMedida[s], slotVentana[s], true);
        ritmo.esperar();
    }
    for (size_t s = 1; s < nSlots; ++s) mixMinusAbierto_[s].store(false);
}

/// Cuándo se mira si A publicó un cuadro nuevo: justo después del próximo que debería
/// publicar (su último envío más su período, de la cabecera del canal: ipc/Mirada.hpp, F2-14).
///
/// NO a un ritmo propio. Mirando cada 33 ms contra alguien que publica cada 33 ms, las dos
/// cadencias baten entre si y se pierde casi la mitad de los cuadros: medido, 13,6 fps de
/// 30. Y lo peor no es la mitad que falta, es que lo que llega llega parejo pero tarde y a
/// destiempo, asi que el navegador del otro lado ensancha su colchon y la pantalla se va un
/// segundo atras de la sala. Mirar cada 4 ms lo evitaba con 250 despertadas por segundo por
/// canal; el plazo cuelga ahora de la hora del que publica (no bate) y son ~una por cuadro.
/// Si A se atrasa, de a pasos cortos; quieto hace más de 1 s, de a kPasoQuieto.
constexpr auto kPasoQuieto = std::chrono::milliseconds(15);

int MediaPump::miranPrograma() const {
    return static_cast<int>(sessionsWhere([](const MediaRoute& r) { return r.miraSalida(""); }).size());
}

// --- Transporte: contadores de lo que se lee (F0-02) ---------------------------

namespace {

/// Cada cuánto se cierra la ventana de los contadores (y a lo sumo una línea por canal).
constexpr auto kVentanaTransporte = std::chrono::seconds(10);
/// El nivel de operación del anillo de audio son 1-3 bloques: más es latencia que se junta.
constexpr std::uint64_t kOcupacionTope = 3;
/// La edad de un cuadro de video al leerlo es el sondeo más el IPC: pasar de esto es que B no
/// llega. (La de un bloque de audio incluye la latencia de la mezcla de A, que es de diseño:
/// en audio miran la ocupación, los descartes y los faltantes, no la edad.)
constexpr double kEdadTopeVideoMs = 100.0;

} // namespace

namespace {

nlohmann::json fotoJson(const FotoTransporte& f) {
    nlohmann::json j{{"nombre", f.nombre},
                     {"tipo", f.tipo},
                     {"ventana_s", f.ventanaS},
                     {"leidos", f.leidos},
                     {"descartados", f.descartados},
                     {"descartados_total", f.descartadosTotal},
                     {"edad_p50_ms", f.edadP50Ms},
                     {"edad_p99_ms", f.edadP99Ms}};
    if (f.tipo == "audio") {
        j["ocupacion_max"] = f.ocupacionMax;
        j["faltantes"] = f.faltantes;
        j["faltantes_total"] = f.faltantesTotal;
    }
    return j;
}

} // namespace

bool MediaPump::cerrarVentana(MedidaTransporte& m, std::chrono::steady_clock::time_point& desde, bool registrar,
                              FotoTransporte* fuera) {
    const auto ahora = std::chrono::steady_clock::now();
    if (ahora - desde < kVentanaTransporte) return false;
    const FotoTransporte f = m.cerrar(std::chrono::duration<double>(ahora - desde).count());
    desde = ahora;
    const bool audio = f.tipo == "audio";
    const bool anomala = audio ? f.anomalia(kOcupacionTope, 1e9) : f.anomalia(UINT64_MAX, kEdadTopeVideoMs);
    bool antes = false;
    {
        std::lock_guard<std::mutex> l(transporteLock_);
        FotoCanal& c = transporte_[f.nombre];
        antes = c.anomala;
        c.foto = f;
        c.anomala = anomala;
    }
    // Al registro solo lo raro, y una línea cuando se normaliza: lo normal no inunda.
    if (registrar && anomala) {
        if (audio)
            log_.warn(llenar(traducir("gateway", "transporte %1: en %2 s, %3 bloques descartados, %4 faltantes, hasta %5 "
                                                 "sin leer, edad p50 %6 ms / p99 %7 ms"),
                             {f.nombre, unDecimal(f.ventanaS), std::to_string(f.descartados), std::to_string(f.faltantes),
                              std::to_string(f.ocupacionMax), unDecimal(f.edadP50Ms), unDecimal(f.edadP99Ms)}),
                      -1);
        else
            log_.warn(llenar(traducir("gateway", "transporte %1: en %2 s, %3 cuadros salteados, edad p50 %4 ms / p99 %5 ms"),
                             {f.nombre, unDecimal(f.ventanaS), std::to_string(f.descartados), unDecimal(f.edadP50Ms),
                              unDecimal(f.edadP99Ms)}),
                      -1);
    } else if (registrar && antes) {
        log_.info(llenar(traducir("gateway", "transporte %1: normal de nuevo"), {f.nombre}));
    }
    if (fuera != nullptr) *fuera = f;
    return true;
}

nlohmann::json MediaPump::transporte() const {
    nlohmann::json out = nlohmann::json::array();
    std::lock_guard<std::mutex> l(transporteLock_);
    for (const auto& [nombre, c] : transporte_) {
        nlohmann::json j = fotoJson(c.foto);
        j["anomalia"] = c.anomala;
        out.push_back(std::move(j));
    }
    return out;
}

std::vector<int> MediaPump::lugaresSinRetorno() const {
    std::vector<int> out;
    if (cfg_.returnSlotNames.empty()) return out; // sin mix-minus: todos oyen el programa
    std::lock_guard<std::mutex> l(routesLock_);
    for (size_t s = 1; s < routes_.size(); ++s) {
        const MediaRoute& r = routes_[s];
        if (r.kind != MediaRoute::Kind::Program || r.index <= 0 || !r.salida.empty()) continue;
        if (r.index > kMaxInvitados || !mixMinusAbierto_[static_cast<size_t>(r.index)].load()) out.push_back(r.index);
    }
    return out;
}

namespace {
/// Hasta `qpc` (el reloj de los sellos) en el de los plazos: lo que falta, desde ahora.
std::chrono::steady_clock::time_point plazoDeQpc(std::uint64_t qpc) {
    const std::uint64_t ahora = HealthBlock::nowQpc();
    return std::chrono::steady_clock::now() + std::chrono::nanoseconds(qpc > ahora ? qpc - ahora : 0);
}
} // namespace

nlohmann::json MediaPump::copiasVideo() const {
    return {{"cuadros", copias_.cuadros.load()},
            {"leidos_bytes", copias_.leidos.load()},
            {"escalados_bytes", copias_.escalados.load()},
            {"empujes", copias_.empujes.load()},
            {"demorados", copias_.demorados.load()},
            {"conversiones", CuadroSaliente::conversiones()},
            {"convertidos_bytes", CuadroSaliente::bytesConvertidos()}};
}

void MediaPump::videoBajadaThread(std::wstring nombre, std::string etiqueta,
                                  std::function<bool(const MediaRoute&)> para, std::wstring audio,
                                  const std::atomic<bool>* parar) {
    SpoutReceiver receiver;
    bool sub = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    int srcW = 0;
    int srcH = 0;
    std::vector<uint8_t> src;
    // El tamaño del retorno se decide recien al saber cuanto mide el programa:
    // agrandarlo no agrega detalle, solo gasta banda y CPU. A lo publica ya al tamaño de
    // las sesiones (escalado en su GPU, F2-13): lo normal es que acá no se escale nada.
    int outW = 0;
    int outH = 0;
    std::vector<uint8_t> scaled;
    std::int64_t trabajoUs = 0;
    std::int64_t cuadros = 0;
    auto ultimoInforme = std::chrono::steady_clock::now();
    MedidaTransporte medida(angosto(nombre), "video");
    // Labios (F2-11): lo que mira (no conversa) sale a la hora de su audio. Lo que tarda el
    // audio es cuánto después de su sello lo escribe A en su anillo (se mira la cabecera, sin
    // leer: los bloques los lee el hilo del retorno), suavizado; más lo que tarda B en leerlo.
    labios::Espera<CuadroSaliente> espera;
    SharedAudioRing anilloAudio;
    auto anilloMirado = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    labios::EdadAudio edadAudio;
    const auto conversan = [&para](const MediaRoute& r) { return para(r) && r.conversa(); };
    const auto miran = [&para](const MediaRoute& r) { return para(r) && !r.conversa(); };
    const auto empujar = [this](const std::vector<int>& a, const CuadroSaliente& c) {
        for (int s : a)
            if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalVideo(c);
        copias_.empujes.fetch_add(a.size(), std::memory_order_relaxed);
    };
    const auto aLosQueMiran = [&](const CuadroSaliente& c) { empujar(sessionsWhere(miran), c); };
    while (sigue(parar)) {
        if (!sub) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                // El tamaño lo fija A al publicar (su salida), no la config de B.
                if (receiver.subscribe(nombre.c_str())) {
                    sub = true;
                    srcW = receiver.width();
                    srcH = receiver.height();
                    src.assign(receiver.frameBytes(), 0);
                    outW = std::min(cfg_.programVideoW, srcW);
                    outH = std::min(cfg_.programVideoH, srcH);
                    if (outW <= 0 || outH <= 0) {
                        outW = srcW;
                        outH = srcH;
                    }
                    medida.reabierto();
                    log_.info(llenar(traducir("gateway", "%1: suscrito (%2x%3) -> %4x%5"),
                                     {etiqueta, std::to_string(srcW), std::to_string(srcH), std::to_string(outW),
                                      std::to_string(outH)}));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (receiver.stale()) {
            receiver = SpoutReceiver{};
            sub = false;
            outW = 0;
            outH = 0;
            espera.vaciar();
            log_.info(llenar(traducir("gateway", "%1: A cambio o se cerro; resuscribiendo"), {etiqueta}));
            continue;
        }
        if (!audio.empty()) {
            const auto ahora = std::chrono::steady_clock::now();
            if (anilloAudio.header() != nullptr && anilloAudio.stale()) {
                anilloAudio = SharedAudioRing{};
                edadAudio = labios::EdadAudio{};
            }
            if (anilloAudio.header() == nullptr && ahora - anilloMirado >= std::chrono::milliseconds(500)) {
                anilloMirado = ahora;
                SharedAudioRing::openReader(audio.c_str(), anilloAudio);
            }
            if (anilloAudio.header() != nullptr) edadAudio.medir(anilloAudio.retrasoDelEscritorNs() + kLecturaAudioNs);
        }
        if (receiver.hasData()) {
            FrameMeta meta{};
            if (receiver.receive(src.data(), src.size(), meta)) {
                // Cuanto hace que A publico este cuadro. `meta.qpc` lo pone A al publicar y
                // los dos procesos miden con el mismo reloj monotonico, asi que la resta es
                // el SALTO A->B solo: el sondeo mas el IPC. Y los cuadros que A publico y no
                // llegamos a leer (saltos del indice).
                const std::uint64_t ahoraQpc = HealthBlock::nowQpc();
                medida.cuadro(meta.frameIndex, meta.qpc, ahoraQpc);
                const int w = meta.ancho != 0 ? static_cast<int>(meta.ancho) : srcW;
                const int h = meta.alto != 0 ? static_cast<int>(meta.alto) : srcH;
                copias_.cuadros.fetch_add(1, std::memory_order_relaxed);
                copias_.leidos.fetch_add(static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4u,
                                         std::memory_order_relaxed);
                const auto ya = sessionsWhere(conversan);
                const bool hayQueMiran = !sessionsWhere(miran).empty();
                if ((!ya.empty() || hayQueMiran) && outW > 0 && outH > 0) {
                    const auto t0 = std::chrono::steady_clock::now();
                    // Una sola conversión para todas las sesiones (CuadroSaliente, F2-13).
                    CuadroSaliente c;
                    const int dw = std::min(outW, w), dh = std::min(outH, h);
                    if (dw == w && dh == h) {
                        c = CuadroSaliente::desdeBgra(src.data(), w, h);
                    } else {
                        scaled.resize(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u);
                        scaleBgra(src.data(), w, h, scaled.data(), dw, dh);
                        copias_.escalados.fetch_add(scaled.size(), std::memory_order_relaxed);
                        c = CuadroSaliente::desdeBgra(scaled.data(), dw, dh);
                    }
                    // Quien conversa, ya (T4); quien mira, a la hora de su audio (F2-11).
                    empujar(ya, c);
                    if (hayQueMiran) {
                        const std::uint64_t cuando = labios::salida(meta.contenidoQpc, ahoraQpc, edadAudio.ns());
                        if (cuando <= ahoraQpc && espera.size() == 0) {
                            aLosQueMiran(c);
                        } else {
                            espera.guardar(cuando, std::move(c), aLosQueMiran);
                            copias_.demorados.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    // Cuanto cuesta preparar un cuadro. Importa para el RETARDO:
                    // si esto pasa del presupuesto (33 ms a 30 fps) la cola se
                    // va llenando y la pantalla se atrasa sola, sin que nada
                    // parezca roto. Se avisa cada 10 s, no por cuadro.
                    trabajoUs += std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - t0)
                                     .count();
                    ++cuadros;
                }
            }
        }
        espera.sacarListos(HealthBlock::nowQpc(), aLosQueMiran);
        if (FotoTransporte foto; cerrarVentana(medida, ultimoInforme, true, &foto)) {
            if (cuadros > 0) {
                const double seg = foto.ventanaS;
                const double medioMs = static_cast<double>(trabajoUs) / cuadros / 1000.0;
                const double fps = cuadros / seg;
                const double edadMs = foto.edadP50Ms; // la mediana: un cuadro trabado no la mueve
                if (medioMs > 25.0)
                    log_.warn(llenar(traducir("gateway", "%1: %2 fps, %3 ms por cuadro, %4 ms de A a B (no llega: la "
                                                         "pantalla se va a atrasar)"),
                                     {etiqueta, unDecimal(fps), unDecimal(medioMs), unDecimal(edadMs)}),
                              0);
                else
                    log_.info(llenar(traducir("gateway", "%1: %2 fps, %3 ms por cuadro, %4 ms de A a B"),
                                     {etiqueta, unDecimal(fps), unDecimal(medioMs), unDecimal(edadMs)}));
            }
            trabajoUs = 0;
            cuadros = 0;
        }
        // Hasta el próximo cuadro que A debería publicar (F2-14) o el próximo que espera su
        // audio, lo que venga antes.
        auto plazo = receiver.proximaMirada(kPasoQuieto);
        if (espera.proximo() != 0) plazo = std::min(plazo, plazoDeQpc(espera.proximo()));
        ba::platform::dormirHasta(plazo);
    }
}

// --- Bajada: operador (solo al lobby) -----------------------------------------

void MediaPump::operatorAudioThread() {
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block;
    MedidaTransporte medida(angosto(cfg_.operatorAudioName), "audio"); // sellado al escribir: sin faltantes por sello
    auto ventana = std::chrono::steady_clock::now();
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(kMiradaAudioMs)};
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (!cfg_.operatorAudioName.empty() && now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(cfg_.operatorAudioName.c_str(), reader).ok) {
                    open = true;
                    medida.reabierto();
                    log_.info(traducir("gateway", "operador: microfono (BA.Audio.Operator) abierto"));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (reader.stale()) {
            reader = SharedAudioRing{};
            open = false;
            continue;
        }
        leerPendientes(reader, block, medida, [&](const PoppedBlock& blk, size_t n) {
            // La voz del operador SOLO a quien le esta hablando: el resto del
            // lobby no escucha conversaciones ajenas.
            for (int s : sessionsWhere(
                     [](const MediaRoute& r) { return r.kind == MediaRoute::Kind::Lobby && r.talk; })) {
                if (WebrtcSession* sess = hub_.session(s))
                    sess->pushLocalAudio(blk.samples, n, cfg_.audioRate);
            }
            // Adentro (programa o fila) no se le empuja aparte: ya recibe el retorno
            // y dos fuentes en el mismo track se pisan. Va a la cola del IFB del
            // retorno que recibe: el de su salida.
            std::set<std::string> conIfb;
            {
                std::lock_guard<std::mutex> l(routesLock_);
                for (size_t s = 1; s < routes_.size(); ++s)
                    if (routes_[s].kind != MediaRoute::Kind::Lobby && routes_[s].talk) conIfb.insert(routes_[s].salida);
            }
            const size_t tope = kTopeColaVoz(cfg_.audioRate); // 300 ms estéreo
            for (const std::string& salida : conIfb) {
                if (salida.empty()) {
                    std::lock_guard<std::mutex> l(ifb_.lock);
                    colavoz::encolar(ifb_.voz, blk.samples, n, tope);
                    continue;
                }
                std::lock_guard<std::mutex> l(bajadasLock_);
                const auto it = bajadas_.find(salida);
                if (it == bajadas_.end()) continue;
                std::lock_guard<std::mutex> lq(it->second->ifb.lock);
                colavoz::encolar(it->second->ifb.voz, blk.samples, n, tope);
            }
        });
        cerrarVentana(medida, ventana, true);
        ritmo.esperar();
    }
}

void MediaPump::vozAudioThread(std::wstring nombre, std::string etiqueta, std::function<bool(const MediaRoute&)> para) {
    // La forma de operatorAudioThread, con otro canal y otro destino: una voz
    // sintetizada le llega SOLO a quien le habla. La del que atiende, a quien atiende
    // hablando (en el lobby de la sala o esperando en la sesión remota:
    // MediaRoute::oyeAlAtendedor; si el operador le está hablando a esa misma
    // persona, gana el operador: empujar dos fuentes al mismo track las pisa). La de
    // cada canal de registro, a la conexión que tiene ese canal.
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block;
    MedidaTransporte medida(angosto(nombre), "audio"); // sellado al escribir: sin faltantes por sello
    auto ventana = std::chrono::steady_clock::now();
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(kMiradaAudioMs)};
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(nombre.c_str(), reader).ok) {
                    open = true;
                    medida.reabierto();
                    log_.info(llenar(traducir("gateway", "%1 abierta"), {etiqueta}));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (reader.stale()) {
            reader = SharedAudioRing{};
            open = false;
            continue;
        }
        leerPendientes(reader, block, medida, [&](const PoppedBlock& blk, size_t n) {
            for (int s : sessionsWhere(para)) {
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalAudio(blk.samples, n, cfg_.audioRate);
            }
        });
        cerrarVentana(medida, ventana, true);
        ritmo.esperar();
    }
}

void MediaPump::vozRegistroThread(std::wstring nombre, int canal) {
    // La forma de vozAudioThread: el anillo del asistente de este canal marca el reloj (la app
    // lo llena cada 10 ms, con voz o con silencio, mientras el canal conversa). Al tótem le
    // baja esa voz más la de quien lo atiende por web; a quien lo atiende, esa voz más la de la
    // persona del tótem. Cada uno oye al otro y al asistente, y nadie se oye a sí mismo.
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block, alTotem, alQueAtiende;
    const std::string etiqueta = llenar(traducir("gateway", "registro: voz del asistente del canal %1"), {std::to_string(canal)});
    MonitorRegistro& m = monitorRegistro_[static_cast<size_t>(canal)];
    MedidaTransporte medida(angosto(nombre), "audio"); // sellado al escribir: sin faltantes por sello
    auto ventana = std::chrono::steady_clock::now();
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(kMiradaAudioMs)};
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(nombre.c_str(), reader).ok) {
                    open = true;
                    medida.reabierto();
                    log_.info(llenar(traducir("gateway", "%1 abierta"), {etiqueta}));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (reader.stale()) {
            reader = SharedAudioRing{};
            open = false;
            continue;
        }
        leerPendientes(reader, block, medida, [&](const PoppedBlock& blk, size_t n) {
            const auto totem = sessionsWhere([canal](const MediaRoute& r) { return r.oyeVozRegistro(canal); });
            const auto miran = sessionsWhere([canal](const MediaRoute& r) { return r.miraRegistro(canal); });
            alTotem.assign(blk.samples, blk.samples + n);
            alQueAtiende.assign(blk.samples, blk.samples + n);
            {
                std::lock_guard<std::mutex> l(m.lock);
                colavoz::mezclar(m.habla, alTotem.data(), n);
                if (!miran.empty()) colavoz::mezclar(m.persona, alQueAtiende.data(), n);
                else m.persona.clear();
            }
            for (int s : totem)
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalAudio(alTotem.data(), n, cfg_.audioRate);
            for (int s : miran)
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalAudio(alQueAtiende.data(), n, cfg_.audioRate);
        });
        cerrarVentana(medida, ventana, true);
        ritmo.esperar();
    }
}

void MediaPump::operatorVideoThread() {
    SpoutReceiver receiver;
    bool sub = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto lastFrame = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto lastPlate = lastFrame;
    int srcW = 0;
    int srcH = 0;
    std::vector<uint8_t> src;
    std::vector<uint8_t> scaled(static_cast<size_t>(cfg_.guestVideoW) * cfg_.guestVideoH * 4u);
    std::vector<uint8_t> plate(scaled.size());
    drawWaitPlate(plate.data(), cfg_.guestVideoW, cfg_.guestVideoH);
    const CuadroSaliente placa = CuadroSaliente::desdeBgra(plate.data(), cfg_.guestVideoW, cfg_.guestVideoH); // una vez
    const auto lobbySessions = [this]() {
        return sessionsWhere([](const MediaRoute& r) { return r.kind == MediaRoute::Kind::Lobby; });
    };
    MedidaTransporte medida(angosto(cfg_.operatorVideoName), "video");
    auto ventana = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (!sub && !cfg_.operatorVideoName.empty() && now - lastRetry >= std::chrono::milliseconds(500)) {
            lastRetry = now;
            if (receiver.subscribe(cfg_.operatorVideoName.c_str())) {
                sub = true;
                srcW = receiver.width();
                srcH = receiver.height();
                src.assign(receiver.frameBytes(), 0);
                medida.reabierto();
                log_.info(llenar(traducir("gateway", "operador: camara (BA.Operator) suscrita (%1x%2)"),
                                 {std::to_string(srcW), std::to_string(srcH)}));
            }
        }
        if (sub && receiver.stale()) {
            receiver = SpoutReceiver{};
            sub = false;
        }
        if (sub && receiver.hasData()) {
            FrameMeta meta{};
            if (receiver.receive(src.data(), src.size(), meta)) {
                lastFrame = now;
                medida.cuadro(meta.frameIndex, meta.qpc, HealthBlock::nowQpc());
                const auto targets = lobbySessions();
                if (!targets.empty()) {
                    fitBgra(src.data(), srcW, srcH, scaled.data(), cfg_.guestVideoW, cfg_.guestVideoH);
                    // Una conversión para todo el lobby (CuadroSaliente, F2-13).
                    const CuadroSaliente c = CuadroSaliente::desdeBgra(scaled.data(), cfg_.guestVideoW, cfg_.guestVideoH);
                    for (int s : targets) {
                        if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalVideo(c);
                    }
                }
            }
        }
        // Sin camara del operador (o caida hace mas de 1 s): placa de espera a
        // 5 fps, para que el invitado vea que esta conectado.
        if (now - lastFrame > std::chrono::seconds(1) && now - lastPlate >= std::chrono::milliseconds(200)) {
            lastPlate = now;
            for (int s : lobbySessions()) {
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalVideo(placa);
            }
        }
        if (sub) cerrarVentana(medida, ventana, true);
        // Hasta el próximo cuadro del operador (F2-14); sin él, de a kPasoQuieto (la placa va a 5).
        ba::platform::dormirHasta(receiver.proximaMirada(kPasoQuieto));
    }
}

// --- Subida ------------------------------------------------------------------

void MediaPump::forwardAudio(int session, const float* samples, size_t n, int rate) {
    if (session == pruebaSesion_.load() && samples != nullptr && rate > 0) {
        // Goertzel a 1 kHz sobre el canal izquierdo, en trozos de 20 ms ubicados por
        // la hora de llegada desde que empezó la prueba.
        const std::int64_t desde = pruebaDesdeNs_.load();
        const std::int64_t ahoraNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch()).count();
        const size_t frames = n / 2;
        const double k = 2.0 * std::cos(2.0 * 3.14159265358979 * 1000.0 / rate);
        double s1 = 0, s2 = 0;
        for (size_t i = 0; i < frames; ++i) {
            const double s0 = samples[2 * i] + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double pot = (s1 * s1 + s2 * s2 - k * s1 * s2) / std::max<size_t>(frames * frames, 1);
        const long bin = static_cast<long>((ahoraNs - desde) / 20'000'000LL);
        std::lock_guard<std::mutex> l(pruebaLock_);
        if (bin >= 0 && bin < 1000) {
            if (pruebaBins_.size() <= static_cast<size_t>(bin)) pruebaBins_.resize(static_cast<size_t>(bin) + 1, -1.0);
            pruebaBins_[static_cast<size_t>(bin)] = std::max(pruebaBins_[static_cast<size_t>(bin)], pot);
        }
    }
    const MediaRoute r = route(session);
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index <= kMaxInvitados) {
        if (AudioOut* a = guestAudio_[static_cast<size_t>(r.index)].load(std::memory_order_acquire))
            publishAudio(*a, samples, n, rate);
    } else if (r.kind == MediaRoute::Kind::Lobby && (r.talk || r.atiende) && r.index >= 1 &&
               r.index < static_cast<int>(lobbyAudio_.size())) {
        // El operador solo escucha a quien le esta hablando; y la app, para
        // pasarla a texto, a quien atiende el asistente hablando.
        publishAudio(*lobbyAudio_[static_cast<size_t>(r.index)], samples, n, rate);
    } else if (r.kind == MediaRoute::Kind::Remoto && r.index >= 1 && r.index < static_cast<int>(remotoAudio_.size())) {
        publishAudio(*remotoAudio_[static_cast<size_t>(r.index)], samples, n, rate);
    } else if (r.kind == MediaRoute::Kind::Atencion && atencionAudio_) {
        // El que espera en la sesión remota, atendido hablando: su voz, solo para
        // pasarla a texto (la app la toma con una entrada interna, fuera de la mezcla).
        publishAudio(*atencionAudio_, samples, n, rate);
    } else if (r.kind == MediaRoute::Kind::Registro && r.index >= 1 &&
               r.index < static_cast<int>(registroAudio_.size()) && registroAudio_[static_cast<size_t>(r.index)]) {
        // La persona que se registra frente a un tótem: su voz, solo para pasarla a
        // texto (como la atención: una entrada interna de la app, fuera de la mezcla).
        publishAudio(*registroAudio_[static_cast<size_t>(r.index)], samples, n, rate);
        // Y para quien la atiende por web, si alguien la está mirando: se suma a la voz del
        // asistente en vozRegistroThread (un track, una fuente).
        const int k = r.index;
        if (k <= kMaxTotems && !sessionsWhere([k](const MediaRoute& x) { return x.miraRegistro(k); }).empty()) {
            const std::vector<float> v = colavoz::remuestrear(samples, n, rate, cfg_.audioRate);
            MonitorRegistro& m = monitorRegistro_[static_cast<size_t>(k)];
            std::lock_guard<std::mutex> l(m.lock);
            colavoz::encolar(m.persona, v.data(), v.size(), kTopeColaVoz(cfg_.audioRate));
        }
    } else if (r.kind == MediaRoute::Kind::RegistroAtiende && r.talk && r.index >= 1 && r.index <= kMaxTotems) {
        // Quien atiende el registro por web, mientras atiende: su voz baja SOLO a ese tótem
        // (mezclada con la del asistente). Nunca a la mezcla, ni a la app.
        const std::vector<float> v = colavoz::remuestrear(samples, n, rate, cfg_.audioRate);
        MonitorRegistro& m = monitorRegistro_[static_cast<size_t>(r.index)];
        std::lock_guard<std::mutex> l(m.lock);
        colavoz::encolar(m.habla, v.data(), v.size(), kTopeColaVoz(cfg_.audioRate));
    }
}

bool MediaPump::codigoPrueba(long long tramo) {
    static constexpr unsigned kCodigo = 0b1011000111101010u; // 16 tramos, mitad prendidos
    return tramo >= 0 && ((kCodigo >> (15 - (tramo % 16))) & 1u) != 0;
}

nlohmann::json MediaPump::analizarEco(const std::vector<double>& bins, int dur) {
    // El patrón (1 = tono) en trozos de 20 ms: el código de 16 tramos de 250 ms, durante `dur`.
    const auto tono = [dur](long b) { return b >= 0 && b * 20 < dur && codigoPrueba((b * 20) / 250); };
    double mejor = -1e9, ecoDb = 0;
    int demora = 0;
    std::vector<double> porDemora;
    for (int lag = 0; lag <= 50; ++lag) { // hasta 1 s de ida y vuelta
        double on = 0, off = 0;
        int non = 0, noff = 0;
        for (size_t b = 0; b < bins.size(); ++b) {
            if (bins[b] < 0) continue;
            const long origen = static_cast<long>(b) - lag;
            if (origen < 0 || origen * 20 >= dur) continue;
            if (tono(origen)) on += bins[b], ++non;
            else off += bins[b], ++noff;
        }
        if (non < 3 || noff < 3) continue;
        const double db = 10.0 * std::log10((on / non + 1e-12) / (off / noff + 1e-12));
        porDemora.push_back(db);
        if (db > mejor) mejor = db, ecoDb = db, demora = lag * 20;
    }
    // Un eco de verdad sobresale en UNA demora; si todas dan parecido, es otra cosa
    // (un ruido parejo de 1 kHz): se descuenta la mediana de las demás.
    double mediana = 0;
    if (!porDemora.empty()) {
        std::vector<double> v = porDemora;
        std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
        mediana = v[v.size() / 2];
    }
    const double sobresale = ecoDb - std::max(0.0, mediana);
    return nlohmann::json{{"eco_db", std::round(sobresale * 10) / 10},
                       {"eco_bruto_db", std::round(ecoDb * 10) / 10},
                       {"demora_ms", demora},
                       {"eco", sobresale > 10.0},
                       {"trozos", bins.size()}};
}

std::string MediaPump::pruebaTecnica(int session, int ms) {
    const MediaRoute r = route(session);
    if (r.kind != MediaRoute::Kind::Program && r.kind != MediaRoute::Kind::Backstage)
        return nlohmann::json{{"error", traducir("gateway", "la prueba es para los de adentro (programa o fila): en el "
                                                            "lobby no recibe el retorno")}}
            .dump();
    int libre = 0;
    if (!pruebaSesion_.compare_exchange_strong(libre, session))
        return nlohmann::json{{"error", traducir("gateway", "ya hay una prueba andando")}}.dump();
    {
        std::lock_guard<std::mutex> l(pruebaLock_);
        pruebaBins_.clear();
    }
    pruebaDesdeNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const int dur = std::clamp(ms, 2000, 8000);
    std::this_thread::sleep_for(std::chrono::milliseconds(dur + 600)); // y lo que tarda en volver
    pruebaSesion_ = 0;
    std::vector<double> bins;
    {
        std::lock_guard<std::mutex> l(pruebaLock_);
        bins = pruebaBins_;
    }
    nlohmann::json out = analizarEco(bins, dur);
    if (WebrtcSession* sess = hub_.session(session)) {
        const nlohmann::json red = nlohmann::json::parse(sess->estadisticas(), nullptr, false);
        if (red.is_object()) out["red"] = red;
    }
    if (bins.empty())
        out["aviso"] = traducir("gateway", "no llegó audio de su micrófono durante la prueba (¿lo tiene silenciado?)");
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void MediaPump::forwardVideo(int session, const uint8_t* pixels, size_t bytes, int w, int h) {
    if (pixels == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    const MediaRoute r = route(session);
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index <= kMaxInvitados) {
        if (VideoOut* v = guestVideo_[static_cast<size_t>(r.index)].load(std::memory_order_acquire))
            publishVideo(*v, pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Lobby && r.index >= 1 &&
               r.index < static_cast<int>(lobbyVideo_.size())) {
        publishVideo(*lobbyVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Backstage && r.index >= 1 &&
               r.index < static_cast<int>(backstageVideo_.size())) {
        publishVideo(*backstageVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Remoto && r.index >= 1 && r.index < static_cast<int>(remotoVideo_.size())) {
        publishVideo(*remotoVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Registro && r.index >= 1 &&
               r.index < static_cast<int>(registroVideo_.size()) && registroVideo_[static_cast<size_t>(r.index)]) {
        publishVideo(*registroVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
        // Quien atiende por web ve los mismos cuadros, achicados al tamaño del retorno y a lo
        // sumo a 15 por segundo (es para ver qué pasa, no para el aire).
        const int k = r.index;
        if (k > kMaxTotems) return;
        const auto miran = sessionsWhere([k](const MediaRoute& x) { return x.miraRegistro(k); });
        if (miran.empty()) return;
        MonitorRegistro& m = monitorRegistro_[static_cast<size_t>(k)];
        std::lock_guard<std::mutex> l(m.lock);
        const uint64_t ahora = monoNs();
        if (ahora - m.videoNs < 66'000'000ULL) return;
        m.videoNs = ahora;
        m.video.resize(static_cast<size_t>(cfg_.guestVideoW) * cfg_.guestVideoH * 4u);
        fitBgra(pixels, w, h, m.video.data(), cfg_.guestVideoW, cfg_.guestVideoH);
        const CuadroSaliente c = CuadroSaliente::desdeBgra(m.video.data(), cfg_.guestVideoW, cfg_.guestVideoH);
        for (int s : miran)
            if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalVideo(c);
    }
}

void MediaPump::forwardScreen(int session, const uint8_t* pixels, size_t bytes, int w, int h) {
    if (pixels == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    const MediaRoute r = route(session);
    // Solo los de adentro comparten pantalla hacia el programa (y el remoto con la palabra).
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index <= kMaxInvitados) {
        if (VideoOut* v = shareVideo_[static_cast<size_t>(r.index)].load(std::memory_order_acquire))
            publishVideo(*v, pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Remoto && r.index >= 1 &&
               r.index < static_cast<int>(remotoContenido_.size()) && remotoContenido_[static_cast<size_t>(r.index)]) {
        publishVideo(*remotoContenido_[static_cast<size_t>(r.index)], pixels, w, h, true);
    }
}

void MediaPump::apagarRegistro(int canal) {
    if (canal < 1 || canal >= static_cast<int>(registroVideo_.size()) || !registroVideo_[static_cast<size_t>(canal)]) return;
    VideoOut& out = *registroVideo_[static_cast<size_t>(canal)];
    if (canal <= kMaxTotems) {
        // Lo que esperaba mezclarse era de la conversación que terminó.
        MonitorRegistro& m = monitorRegistro_[static_cast<size_t>(canal)];
        std::lock_guard<std::mutex> l(m.lock);
        m.persona.clear();
        m.habla.clear();
    }
    if (out.w <= 0 || out.h <= 0) return;
    std::vector<uint8_t> px(static_cast<size_t>(out.w) * out.h * 4u, 0);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;
    publishVideo(out, px.data(), out.w, out.h, false);
}

void MediaPump::apagarRemoto(int canal) {
    if (canal < 1 || canal >= static_cast<int>(remotoVideo_.size()) || !remotoVideo_[static_cast<size_t>(canal)]) return;
    const auto negro = [this](VideoOut& out) {
        if (out.w <= 0 || out.h <= 0) return;
        std::vector<uint8_t> px(static_cast<size_t>(out.w) * out.h * 4u, 0);
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;
        publishVideo(out, px.data(), out.w, out.h, false);
    };
    negro(*remotoVideo_[static_cast<size_t>(canal)]);
    if (canal < static_cast<int>(remotoContenido_.size()) && remotoContenido_[static_cast<size_t>(canal)])
        negro(*remotoContenido_[static_cast<size_t>(canal)]);
}

void MediaPump::publishVideo(VideoOut& out, const uint8_t* pixels, int w, int h, bool real) {
    if (!out.sender.published()) return;
    if (out.minIntervalNs != 0) {
        const uint64_t t = monoNs();
        if (t - out.lastNs.load(std::memory_order_relaxed) < out.minIntervalNs) return;
    }
    if (real) out.lastNs.store(monoNs(), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(out.lock);
    const uint8_t* frame = pixels;
    // El canal tiene tamaño fijo; el participante manda lo que su camara/red
    // permita: viaja a su tamaño, en una caja con la forma del canal (cajaDelCuadro).
    int bw = out.w, bh = out.h;
    cajaDelCuadro(w, h, out.w, out.h, bw, bh);
    if (w != bw || h != bh) {
        fitBgra(pixels, w, h, out.scratch.data(), bw, bh);
        frame = out.scratch.data();
    }
    const uint64_t now = HealthBlock::nowQpc();
    FrameMeta meta{now, now};
    if (bw != out.w || bh != out.h) { // el tamaño real (si es el del canal, no hace falta decirlo)
        meta.ancho = static_cast<uint32_t>(bw);
        meta.alto = static_cast<uint32_t>(bh);
    }
    out.sender.send(frame, static_cast<size_t>(bw) * bh * 4u, meta);

    // A que ritmo llega lo que sube este participante. Solo los canales sin
    // tope de fps: una miniatura de backstage va a 10 a proposito y decir que
    // "va a 10" seria ruido.
    if (real && out.minIntervalNs == 0 && !out.nombre.empty()) {
        ++out.cuadros;
        const uint64_t t = monoNs();
        if (out.informeNs == 0) {
            out.informeNs = t;
        } else if (t - out.informeNs >= 10'000'000'000ull) {
            const double seg = static_cast<double>(t - out.informeNs) / 1e9;
            log_.info(llenar(traducir("gateway", "sube %1: %2 fps (%3x%4)"),
                             {out.nombre, unDecimal(static_cast<double>(out.cuadros) / seg), std::to_string(w),
                              std::to_string(h)}));
            out.cuadros = 0;
            out.informeNs = t;
        }
    }
}

void MediaPump::publishAudio(AudioOut& out, const float* samples, size_t n, int rate) {
    if (samples == nullptr || n == 0 || out.ring.header() == nullptr) return;
    const int channels = cfg_.audioChannels > 0 ? cfg_.audioChannels : 2;
    const size_t blockFloats = out.ring.blockFloats(); // 480 * canales
    std::lock_guard<std::mutex> lock(out.lock);
    // Re-muestreo si no llega a la frecuencia del ring (48 kHz).
    if (rate > 0 && rate != cfg_.audioRate) {
        if (!out.resampler || out.resamplerRate != rate) {
            out.resampler = std::make_unique<ba::dsp::Resampler>();
            out.resampler->configure(rate, cfg_.audioRate, channels, ba::dsp::Quality::SincMedium);
            out.resamplerRate = rate;
        }
        const size_t inFrames = n / static_cast<size_t>(channels);
        const size_t maxOut = static_cast<size_t>(static_cast<double>(inFrames) * cfg_.audioRate / rate) + 32;
        if (out.resampleOut.size() < maxOut * static_cast<size_t>(channels)) {
            out.resampleOut.resize(maxOut * static_cast<size_t>(channels));
        }
        size_t outFrames = 0;
        if (inFrames > 0 &&
            out.resampler->resample(samples, inFrames, out.resampleOut.data(), maxOut, &outFrames) &&
            outFrames > 0) {
            out.buf.insert(out.buf.end(), out.resampleOut.begin(),
                           out.resampleOut.begin() + static_cast<std::ptrdiff_t>(outFrames * channels));
        }
    } else {
        out.buf.insert(out.buf.end(), samples, samples + n);
    }
    // Re-bloqueo a 10 ms con indice de lectura (sin memmove por bloque).
    while (out.buf.size() - out.pos >= blockFloats) {
        out.ring.push(out.buf.data() + out.pos, HealthBlock::nowQpc());
        out.pos += blockFloats;
    }
    if (out.pos > 0 && out.pos >= blockFloats * 8) {
        out.buf.erase(out.buf.begin(), out.buf.begin() + static_cast<std::ptrdiff_t>(out.pos));
        out.pos = 0;
    }
}

// --- Placas de los slots del programa ----------------------------------------

void MediaPump::plateThread() {
    const int w = cfg_.programW;
    const int h = cfg_.programH;
    // La placa de cada lugar se dibuja cuando el lugar tiene canal (el cupo puede
    // subir andando: ampliar).
    std::vector<std::vector<uint8_t>> plates(guestVideo_.size());
    const uint64_t kGuestTimeoutNs = cfg_.sinVideoNs; // sin video tanto tiempo -> placa
    while (!stop_.load(std::memory_order_relaxed)) {
        const uint64_t now = monoNs();
        for (size_t s = 1; s < guestVideo_.size(); ++s) {
            VideoOut* v = guestVideo_[s].load(std::memory_order_acquire);
            if (v == nullptr) continue;
            if (plates[s].empty()) {
                plates[s].assign(static_cast<size_t>(w) * h * 4u, 0);
                drawPlate(plates[s].data(), w, h, static_cast<int>(s));
            }
            const uint64_t last = v->lastNs.load(std::memory_order_relaxed);
            if (last == 0 || now - last > kGuestTimeoutNs) publishVideo(*v, plates[s].data(), w, h, false);
        }
        // 5 fps: es una imagen fija y A muestra el ultimo frame recibido.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace ba::gateway
