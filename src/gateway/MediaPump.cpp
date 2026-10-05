// ============================================================================
// src/gateway/MediaPump.cpp — implementacion del motor de medios (ver header)
//
// Estrategia de errores: no lanza. Un canal que no se puede crear se loguea y
// se salta (no tumba al pump); un pop/receive fallido se reintenta.
// ============================================================================

#include "MediaPump.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "ipc/HealthBlock.hpp"

#if BA_GATEWAY_HAVE_WEBRTC
#include "third_party/libyuv/include/libyuv/scale_argb.h"
#endif
#include "Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::gateway {

namespace {

using namespace ba::ipc;

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

uint64_t monoNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

MediaPump::MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log) : hub_(hub), cfg_(cfg), log_(log) {}

MediaPump::~MediaPump() { stop(); }

void MediaPump::start() {
    const uint32_t pid = currentPid();
    const auto name = [](const std::vector<std::wstring>& v, size_t i) {
        return i < v.size() ? v[i] : std::wstring();
    };
    const auto narrow = [](const std::wstring& w) { return std::string(w.begin(), w.end()); };

    const auto makeVideo = [&](const std::wstring& n, int w, int h) {
        auto out = std::make_unique<VideoOut>();
        out->w = w;
        out->h = h;
        out->nombre = narrow(n);
        out->scratch.resize(static_cast<size_t>(w) * h * 4u);
        if (!n.empty()) {
            if (out->sender.publish(n.c_str(), SlotConfig{w, h})) {
                log_.info("video out: " + narrow(n) + " listo (" + std::to_string(w) + "x" +
                          std::to_string(h) + ")");
            } else {
                log_.warn("video out: no se pudo publicar " + narrow(n) + " (se salta)", -1);
            }
        }
        return out;
    };
    const auto makeAudio = [&](const std::wstring& n) {
        auto out = std::make_unique<AudioOut>();
        if (!n.empty()) {
            if (SharedAudioRing::init(n.c_str(), static_cast<uint32_t>(cfg_.audioChannels),
                                      audio_ring::kDefaultCapacityBlocks, pid, kGeneration, out->ring)
                    .ok) {
                log_.info("audio out: " + narrow(n) + " listo");
            } else {
                log_.warn("audio out: no se pudo crear " + narrow(n) + " (se salta)", -1);
            }
        }
        return out;
    };

    const size_t guests = cfg_.guestVideoNames.empty() ? 0 : cfg_.guestVideoNames.size() - 1;
    const size_t lobby = cfg_.lobbyVideoNames.empty() ? 0 : cfg_.lobbyVideoNames.size() - 1;
    guestVideo_.clear();
    shareVideo_.clear();
    lobbyVideo_.clear();
    backstageVideo_.clear();
    guestAudio_.clear();
    lobbyAudio_.clear();
    guestVideo_.emplace_back();
    shareVideo_.emplace_back();
    guestAudio_.emplace_back();
    lobbyVideo_.emplace_back();
    lobbyAudio_.emplace_back();
    backstageVideo_.emplace_back();
    const size_t backstage = cfg_.backstageVideoNames.empty() ? 0 : cfg_.backstageVideoNames.size() - 1;
    for (size_t k = 1; k <= backstage; ++k) {
        backstageVideo_.push_back(
            makeVideo(name(cfg_.backstageVideoNames, k), cfg_.backstageW, cfg_.backstageH));
        backstageVideo_.back()->minIntervalNs = 100'000'000; // ~10 fps: es una miniatura
    }
    for (size_t s = 1; s <= guests; ++s) {
        guestVideo_.push_back(makeVideo(name(cfg_.guestVideoNames, s), cfg_.programW, cfg_.programH));
        shareVideo_.push_back(makeVideo(name(cfg_.shareVideoNames, s), cfg_.programW, cfg_.programH));
        guestAudio_.push_back(makeAudio(name(cfg_.guestAudioNames, s)));
    }
    for (size_t k = 1; k <= lobby; ++k) {
        lobbyVideo_.push_back(makeVideo(name(cfg_.lobbyVideoNames, k), cfg_.lobbyW, cfg_.lobbyH));
        lobbyAudio_.push_back(makeAudio(name(cfg_.lobbyAudioNames, k)));
    }
    {
        std::lock_guard<std::mutex> lock(routesLock_);
        routes_.assign(static_cast<size_t>(hub_.sessions()) + 1, MediaRoute{});
    }

    stop_.store(false);
    threads_.emplace_back(&MediaPump::returnAudioThread, this);
    threads_.emplace_back(&MediaPump::returnVideoThread, this);
    threads_.emplace_back(&MediaPump::operatorAudioThread, this);
    threads_.emplace_back(&MediaPump::operatorVideoThread, this);
    threads_.emplace_back(&MediaPump::plateThread, this);
    log_.info("MediaPump: en marcha (slots=" + std::to_string(guests) + ", lobby=" + std::to_string(lobby) +
              ", fila=" + std::to_string(backstage) + ", sesiones=" + std::to_string(hub_.sessions()) + ")");
}

void MediaPump::stop() {
    if (threads_.empty()) return; // idempotente (main y el destructor)
    stop_.store(true);
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    log_.info("MediaPump: detenido");
}

// --- Rutas -------------------------------------------------------------------

void MediaPump::setRoute(int session, const MediaRoute& route) {
    std::lock_guard<std::mutex> lock(routesLock_);
    if (session < 1 || session >= static_cast<int>(routes_.size())) return;
    routes_[static_cast<size_t>(session)] = route;
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

void MediaPump::returnAudioThread() {
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block;
    // Mix-minus: un lector por slot (se abren cuando A los publica).
    const size_t nSlots = cfg_.returnSlotNames.size();
    std::vector<SharedAudioRing> slotReaders(nSlots);
    std::vector<bool> slotOpen(nSlots, false);
    std::vector<std::vector<float>> slotBlock(nSlots);
    auto lastSlotRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (!stop_.load(std::memory_order_relaxed)) {
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastSlotRetry >= std::chrono::milliseconds(1000)) {
                lastSlotRetry = now;
                for (size_t s = 1; s < nSlots; ++s) {
                    if (slotOpen[s] && slotReaders[s].stale()) {
                        slotReaders[s] = SharedAudioRing{};
                        slotOpen[s] = false;
                    }
                    if (!slotOpen[s] && !cfg_.returnSlotNames[s].empty() &&
                        SharedAudioRing::openReader(cfg_.returnSlotNames[s].c_str(), slotReaders[s]).ok) {
                        slotOpen[s] = true;
                        log_.info("retorno mix-minus del slot " + std::to_string(s) + " abierto");
                    }
                }
            }
        }
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(cfg_.returnAudioName.c_str(), reader).ok) {
                    open = true;
                    log_.info("retorno audio: BA.Audio.ContentForReturn abierto");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (reader.stale()) {
            reader = SharedAudioRing{};
            open = false;
            log_.info("retorno audio: A cambio o se cerro; reabriendo");
            continue;
        }
        const size_t n = reader.blockFloats();
        if (block.size() < n) block.resize(n);
        PoppedBlock blk;
        if (reader.pop(block.data(), blk)) {
            // El bloque de cada slot con mix-minus (uno por vuelta, al mismo ritmo).
            std::vector<const float*> deSlot(nSlots, nullptr);
            for (size_t s = 1; s < nSlots; ++s) {
                if (!slotOpen[s]) continue;
                const size_t ns = slotReaders[s].blockFloats();
                if (ns != n) continue;
                if (slotBlock[s].size() < ns) slotBlock[s].resize(ns);
                PoppedBlock b2;
                // Un hueco del mix-minus (silencio de relleno): mejor el programa entero.
                if (slotReaders[s].pop(slotBlock[s].data(), b2) && !b2.isSilence) deSlot[s] = b2.samples;
            }
            for (int s : sessionsWhere([](const MediaRoute& r) {
                     return r.kind == MediaRoute::Kind::Program || r.kind == MediaRoute::Kind::Backstage;
                 })) {
                const MediaRoute r = route(s);
                const float* fuente = blk.samples;
                if (r.kind == MediaRoute::Kind::Program && r.index > 0 && static_cast<size_t>(r.index) < nSlots &&
                    deSlot[static_cast<size_t>(r.index)] != nullptr)
                    fuente = deSlot[static_cast<size_t>(r.index)];
                if (WebrtcSession* sess = hub_.session(s)) sess->pushLocalAudio(fuente, n, cfg_.audioRate);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/// Cada cuanto se mira si A publico un cuadro nuevo.
///
/// MUY por debajo del periodo de un cuadro, a proposito. Mirando cada 33 ms
/// contra alguien que publica cada 33 ms, las dos cadencias baten entre si y
/// se pierde casi la mitad de los cuadros: medido, 13,6 fps de 30. Y lo peor
/// no es la mitad que falta, es que lo que llega llega parejo pero tarde y a
/// destiempo, asi que el navegador del otro lado ensancha su colchon y la
/// pantalla se va un segundo atras de la sala. Mirando cada 4 ms el cuadro
/// sale apenas se publica. Cuesta poco: preparar uno son ~8 ms de un solo
/// nucleo, y mirar y que no haya nada no cuesta nada.
constexpr int kMiradaMs = 4;

void MediaPump::returnVideoThread() {
    SpoutReceiver receiver;
    bool sub = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    int srcW = 0;
    int srcH = 0;
    std::vector<uint8_t> src;
    // El tamaño del retorno se decide recien al saber cuanto mide el programa:
    // agrandarlo no agrega detalle, solo gasta banda y CPU.
    int outW = 0;
    int outH = 0;
    std::vector<uint8_t> scaled;
    std::int64_t trabajoUs = 0;
    std::int64_t edadUs = 0;
    std::int64_t cuadros = 0;
    auto ultimoInforme = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!sub) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                // El tamaño lo fija A al publicar (su salida), no la config de B.
                if (receiver.subscribe(cfg_.programVideoName.c_str())) {
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
                    scaled.assign(static_cast<size_t>(outW) * outH * 4u, 0);
                    log_.info("retorno video: suscrito a BA.Program (" + std::to_string(srcW) + "x" +
                              std::to_string(srcH) + ") -> " + std::to_string(outW) + "x" +
                              std::to_string(outH));
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
            log_.info("retorno video: A cambio o se cerro; resuscribiendo");
            continue;
        }
        if (receiver.hasData()) {
            FrameMeta meta{};
            if (receiver.receive(src.data(), src.size(), meta)) {
                const auto targets = sessionsWhere([](const MediaRoute& r) {
                    return r.kind == MediaRoute::Kind::Program || r.kind == MediaRoute::Kind::Backstage;
                });
                if (!targets.empty() && outW > 0 && outH > 0) {
                    const auto t0 = std::chrono::steady_clock::now();
                    scaleBgra(src.data(), srcW, srcH, scaled.data(), outW, outH);
                    for (int s : targets) {
                        if (WebrtcSession* sess = hub_.session(s)) {
                            sess->pushLocalVideo(scaled.data(), scaled.size(), outW, outH);
                        }
                    }
                    // Cuanto cuesta preparar un cuadro. Importa para el RETARDO:
                    // si esto pasa del presupuesto (33 ms a 30 fps) la cola se
                    // va llenando y la pantalla se atrasa sola, sin que nada
                    // parezca roto. Se avisa cada 10 s, no por cuadro.
                    trabajoUs += std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - t0)
                                     .count();
                    // Cuanto hace que A publico este cuadro. `meta.qpc` lo pone
                    // A al publicar y los dos procesos miden con el mismo reloj
                    // monotonico, asi que la resta es el SALTO A->B solo: el
                    // sondeo mas el IPC. Es el unico tramo del camino que no
                    // tenia numero, y sin el no habia forma de saber si el
                    // retardo estaba en libobs o en nosotros.
                    const std::uint64_t ahoraQpc = HealthBlock::nowQpc();
                    if (meta.qpc != 0 && ahoraQpc > meta.qpc) edadUs += (ahoraQpc - meta.qpc) / 1000;
                    ++cuadros;
                }
            }
        }
        const auto ahora = std::chrono::steady_clock::now();
        if (ahora - ultimoInforme >= std::chrono::seconds(10)) {
            if (cuadros > 0) {
                const double seg = std::chrono::duration<double>(ahora - ultimoInforme).count();
                const double medioMs = static_cast<double>(trabajoUs) / cuadros / 1000.0;
                const double fps = cuadros / seg;
                const double edadMs = static_cast<double>(edadUs) / cuadros / 1000.0;
                log_.fmt(medioMs > 25.0 ? "warn" : "info", 0,
                         "retorno video: %.1f fps, %.1f ms por cuadro, %.1f ms de A a B%s", fps, medioMs,
                         edadMs, medioMs > 25.0 ? " (no llega: la pantalla se va a atrasar)" : "");
            }
            trabajoUs = 0;
            edadUs = 0;
            cuadros = 0;
            ultimoInforme = ahora;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kMiradaMs));
    }
}

// --- Bajada: operador (solo al lobby) -----------------------------------------

void MediaPump::operatorAudioThread() {
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<float> block;
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!open) {
            const auto now = std::chrono::steady_clock::now();
            if (!cfg_.operatorAudioName.empty() && now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(cfg_.operatorAudioName.c_str(), reader).ok) {
                    open = true;
                    log_.info("operador: microfono (BA.Audio.Operator) abierto");
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
        const size_t n = reader.blockFloats();
        if (block.size() < n) block.resize(n);
        PoppedBlock blk;
        if (reader.pop(block.data(), blk)) {
            // La voz del operador SOLO a quien le esta hablando: el resto del
            // lobby no escucha conversaciones ajenas.
            for (int s : sessionsWhere(
                     [](const MediaRoute& r) { return r.kind == MediaRoute::Kind::Lobby && r.talk; })) {
                if (WebrtcSession* sess = hub_.session(s))
                    sess->pushLocalAudio(blk.samples, n, cfg_.audioRate);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    const auto lobbySessions = [this]() {
        return sessionsWhere([](const MediaRoute& r) { return r.kind == MediaRoute::Kind::Lobby; });
    };
    while (!stop_.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (!sub && !cfg_.operatorVideoName.empty() && now - lastRetry >= std::chrono::milliseconds(500)) {
            lastRetry = now;
            if (receiver.subscribe(cfg_.operatorVideoName.c_str())) {
                sub = true;
                srcW = receiver.width();
                srcH = receiver.height();
                src.assign(receiver.frameBytes(), 0);
                log_.info("operador: camara (BA.Operator) suscrita (" + std::to_string(srcW) + "x" +
                          std::to_string(srcH) + ")");
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
                const auto targets = lobbySessions();
                if (!targets.empty()) {
                    fitBgra(src.data(), srcW, srcH, scaled.data(), cfg_.guestVideoW, cfg_.guestVideoH);
                    for (int s : targets) {
                        if (WebrtcSession* sess = hub_.session(s)) {
                            sess->pushLocalVideo(scaled.data(), scaled.size(), cfg_.guestVideoW,
                                                 cfg_.guestVideoH);
                        }
                    }
                }
            }
        }
        // Sin camara del operador (o caida hace mas de 1 s): placa de espera a
        // 5 fps, para que el invitado vea que esta conectado.
        if (now - lastFrame > std::chrono::seconds(1) && now - lastPlate >= std::chrono::milliseconds(200)) {
            lastPlate = now;
            for (int s : lobbySessions()) {
                if (WebrtcSession* sess = hub_.session(s)) {
                    sess->pushLocalVideo(plate.data(), plate.size(), cfg_.guestVideoW, cfg_.guestVideoH);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kMiradaMs));
    }
}

// --- Subida ------------------------------------------------------------------

void MediaPump::forwardAudio(int session, const float* samples, size_t n, int rate) {
    const MediaRoute r = route(session);
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index < static_cast<int>(guestAudio_.size())) {
        publishAudio(*guestAudio_[static_cast<size_t>(r.index)], samples, n, rate);
    } else if (r.kind == MediaRoute::Kind::Lobby && r.talk && r.index >= 1 &&
               r.index < static_cast<int>(lobbyAudio_.size())) {
        // El operador solo escucha a quien le esta hablando.
        publishAudio(*lobbyAudio_[static_cast<size_t>(r.index)], samples, n, rate);
    }
}

void MediaPump::forwardVideo(int session, const uint8_t* pixels, size_t bytes, int w, int h) {
    if (pixels == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    const MediaRoute r = route(session);
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index < static_cast<int>(guestVideo_.size())) {
        publishVideo(*guestVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Lobby && r.index >= 1 &&
               r.index < static_cast<int>(lobbyVideo_.size())) {
        publishVideo(*lobbyVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    } else if (r.kind == MediaRoute::Kind::Backstage && r.index >= 1 &&
               r.index < static_cast<int>(backstageVideo_.size())) {
        publishVideo(*backstageVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    }
}

void MediaPump::forwardScreen(int session, const uint8_t* pixels, size_t bytes, int w, int h) {
    if (pixels == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    const MediaRoute r = route(session);
    // Solo los de adentro comparten pantalla hacia el programa.
    if (r.kind == MediaRoute::Kind::Program && r.index >= 1 &&
        r.index < static_cast<int>(shareVideo_.size())) {
        publishVideo(*shareVideo_[static_cast<size_t>(r.index)], pixels, w, h, true);
    }
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
    // permita: se ajusta adentro con barras (letterbox/pillarbox).
    if (w != out.w || h != out.h) {
        fitBgra(pixels, w, h, out.scratch.data(), out.w, out.h);
        frame = out.scratch.data();
    }
    const uint64_t now = HealthBlock::nowQpc();
    out.sender.send(frame, static_cast<size_t>(out.w) * out.h * 4u, FrameMeta{now, now});

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
            log_.fmt("info", 0, "sube %s: %.1f fps (%dx%d)", out.nombre.c_str(),
                     static_cast<double>(out.cuadros) / seg, w, h);
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
    std::vector<std::vector<uint8_t>> plates(guestVideo_.size());
    for (size_t s = 1; s < plates.size(); ++s) {
        plates[s].assign(static_cast<size_t>(w) * h * 4u, 0);
        drawPlate(plates[s].data(), w, h, static_cast<int>(s));
    }
    constexpr uint64_t kGuestTimeoutNs = 1000000000ULL; // sin video 1 s -> placa
    while (!stop_.load(std::memory_order_relaxed)) {
        const uint64_t now = monoNs();
        for (size_t s = 1; s < guestVideo_.size(); ++s) {
            const uint64_t last = guestVideo_[s]->lastNs.load(std::memory_order_relaxed);
            if (last == 0 || now - last > kGuestTimeoutNs)
                publishVideo(*guestVideo_[s], plates[s].data(), w, h, false);
        }
        // 5 fps: es una imagen fija y A muestra el ultimo frame recibido.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace ba::gateway
