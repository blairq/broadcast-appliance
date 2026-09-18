// ============================================================================
// src/gateway/MediaPump.cpp — implementacion del motor de medios (IPC ↔ WebRTC)
//
// Proposito:
//   Implementa MediaPump (ver el header). Dos threads de RETORNO consumen los
//   flujos IPC de A (ring de audio de retorno + sender de video de programa) y
//   los inyectan en los tracks locales de cada invitado conectado; forwardAudio/
//   forwardVideo publican el uplink del invitado en los canales B→A (ring
//   BA.Audio.Guest.<n> + sender Spout BA.Guest.<n>).
//
// Modelo de threading:
//   - returnAudioThread: abre el ring de retorno (reintento 500 ms si A no
//     esta), pop() bloques de 10 ms y los inyecta en las sesiones conectadas.
//   - returnVideoThread: se suscribe a BA.Program (reintento 500 ms), receive()
//     frames nuevos, escala a la resolucion de retorno y los inyecta.
//   - forwardAudio/forwardVideo: lock por slot para el buffer de re-bloqueo de
//     audio a 480 frames (los callbacks de sesion pueden llegar desde threads
//     distintos de libwebrtc).
//   - Los threads se detienen por stop_ (atomic); no lanzan.
//
// Estrategia de errores:
//   No lanza. Un pop/receive fallido se loguea y se sigue. Si un canal B→A no
//   se puede crear/publicar, se loguea y se salta ese slot (no tumba al pump).
// ============================================================================

#include "MediaPump.hpp"

#include <chrono>
#include <vector>

#include "ipc/HealthBlock.hpp"
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
void scaleBgra(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
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
}

} // namespace

MediaPump::MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log) : hub_(hub), cfg_(cfg), log_(log) {}

MediaPump::~MediaPump() { stop(); }

void MediaPump::start() {
    const int guests = hub_.guests();
    const uint32_t pid = currentPid();

    // --- Canales de salida B→A por slot (1..guests). 0 reservado. ---
    // emplace_back (no resize/assign): resize(n) copia un elemento por defecto
    // a cada slot nuevo, pero SharedAudioRing/SpoutSender son move-only (copy
    // borrado) y std::mutex no es copiable. emplace_back default-construct in
    // place y funciona para copiables, move-only y no-copiables por igual.
    const size_t slots = static_cast<size_t>(guests) + 1;
    outAudio_.clear();
    outVideo_.clear();
    audioBuf_.clear();
    audioBufLock_.clear();
    audioBufPos_.assign(slots, 0);
    audioResampler_.clear();
    audioResamplerRate_.assign(slots, 0);
    audioResampleIn_.clear();
    audioResampleOut_.clear();
    for (size_t i = 0; i < slots; ++i) {
        outAudio_.emplace_back();
        outVideo_.emplace_back();
        audioBuf_.emplace_back();
        audioBufLock_.emplace_back(std::make_unique<std::mutex>());
        audioResampler_.emplace_back();
        audioResampleIn_.emplace_back();
        audioResampleOut_.emplace_back();
    }

    // Los vectores de nombres se indexan por slot, con el [0] reservado, asi
    // que el tamaño esperado es guests+1. Si vienen mas cortos, los slots que
    // falten se quedan sin canal B→A: se avisa una vez en vez de saltarlos en
    // silencio (era el sintoma de un desfase de uno dificil de ver en vivo).
    if (static_cast<int>(cfg_.guestAudioNames.size()) <= guests ||
        static_cast<int>(cfg_.guestVideoNames.size()) <= guests) {
        log_.warn("MediaPump: nombres de invitado insuficientes (audio=" +
                      std::to_string(cfg_.guestAudioNames.size()) +
                      " video=" + std::to_string(cfg_.guestVideoNames.size()) + ", se esperaban " +
                      std::to_string(guests + 1) + "); habra slots sin canal B→A",
                  -1);
    }

    for (int s = 1; s <= guests; ++s) {
        // AUDIO B→A: ring BA.Audio.Guest.<s> (B es el writer).
        if (s < static_cast<int>(cfg_.guestAudioNames.size())) {
            const auto& name = cfg_.guestAudioNames[static_cast<size_t>(s)];
            if (!SharedAudioRing::init(name.c_str(), static_cast<uint32_t>(cfg_.audioChannels),
                                       audio_ring::kDefaultCapacityBlocks, pid, kGeneration,
                                       outAudio_[static_cast<size_t>(s)])
                     .ok) {
                log_.warn("audio out slot " + std::to_string(s) + ": no se pudo crear el ring (se salta)",
                          -1);
            } else {
                log_.info("audio out slot " + std::to_string(s) + ": ring BA.Audio.Guest." +
                          std::to_string(s) + " listo");
            }
        }
        // VIDEO B→A: sender Spout BA.Guest.<s> (resolucion fija del slot).
        if (s < static_cast<int>(cfg_.guestVideoNames.size())) {
            const auto& name = cfg_.guestVideoNames[static_cast<size_t>(s)];
            SlotConfig vcfg{cfg_.programW, cfg_.programH};
            if (!outVideo_[static_cast<size_t>(s)].publish(name.c_str(), vcfg)) {
                log_.warn(
                    "video out slot " + std::to_string(s) + ": no se pudo publicar el sender (se salta)", -1);
            } else {
                log_.info("video out slot " + std::to_string(s) + ": sender BA.Guest." + std::to_string(s) +
                          " listo");
            }
        }
    }

    stop_.store(false);
    audioThread_ = std::thread(&MediaPump::returnAudioThread, this);
    videoThread_ = std::thread(&MediaPump::returnVideoThread, this);
    log_.info("MediaPump: threads de retorno arrancados (guests=" + std::to_string(guests) + ")");
}

void MediaPump::stop() {
    stop_.store(true);
    if (audioThread_.joinable()) audioThread_.join();
    if (videoThread_.joinable()) videoThread_.join();
    log_.info("MediaPump: threads de retorno detenidos");
}

// --- Retorno de AUDIO (A→B→invitado) ---------------------------------------
void MediaPump::returnAudioThread() {
    SharedAudioRing reader;
    bool open = false;
    auto lastRetry = std::chrono::steady_clock::now();
    // Buffer de destino de pop(), propiedad de ESTE thread. Se dimensiona una
    // vez (al primer pop, cuando ya se conoce blockFloats()) y se reutiliza:
    // el bucle corre cada 10 ms y no debe asignar.
    std::vector<float> block;

    while (!stop_.load(std::memory_order_relaxed)) {
        if (!open) {
            // Abre el ring de retorno de A. Si A no esta corriendo, reintenta
            // cada 500 ms (degradacion elegante: el invitado oye silencio).
            auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (SharedAudioRing::openReader(cfg_.returnAudioName.c_str(), reader).ok) {
                    open = true;
                    log_.info("retorno audio: ring BA.Audio.ContentForReturn abierto");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Pop un bloque de 10 ms (480 frames) e inyecta en las sesiones
        // conectadas. pop() copia a `block` (buffer de este thread), asi que el
        // bloque sigue siendo valido mientras se reparte entre las sesiones.
        const size_t n = reader.blockFloats();
        if (block.size() < n) {
            block.resize(n);
        }
        PoppedBlock blk;
        if (reader.pop(block.data(), blk)) {
            for (int s = 1; s <= hub_.guests(); ++s) {
                if (WebrtcSession* sess = hub_.session(s)) {
                    if (sess->connected()) {
                        sess->pushLocalAudio(blk.samples, n, cfg_.audioRate);
                    }
                }
            }
        }
        // Pacing de tiempo real: 10 ms por bloque (480 frames a 48 kHz).
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// --- Retorno de VIDEO (A→B→invitado) ---------------------------------------
void MediaPump::returnVideoThread() {
    SpoutReceiver receiver;
    bool sub = false;
    auto lastRetry = std::chrono::steady_clock::now();

    const size_t programBytes = static_cast<size_t>(cfg_.programW) * cfg_.programH * 4u;
    std::vector<uint8_t> programBuf(programBytes);
    const size_t guestBytes = static_cast<size_t>(cfg_.guestVideoW) * cfg_.guestVideoH * 4u;
    std::vector<uint8_t> guestBuf(guestBytes);

    while (!stop_.load(std::memory_order_relaxed)) {
        if (!sub) {
            // Se suscribe a BA.Program. Si A no esta corriendo, reintenta cada
            // 500 ms (el invitado ve la placa "Reanudando transmision").
            auto now = std::chrono::steady_clock::now();
            if (now - lastRetry >= std::chrono::milliseconds(500)) {
                lastRetry = now;
                if (receiver.subscribe(cfg_.programVideoName.c_str())) {
                    sub = true;
                    log_.info("retorno video: suscrito a BA.Program");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (receiver.hasData()) {
            FrameMeta meta{};
            if (receiver.receive(programBuf.data(), programBytes, meta)) {
                // Escala a la resolucion del retorno al invitado (640x360).
                scaleBgra(programBuf.data(), cfg_.programW, cfg_.programH, guestBuf.data(), cfg_.guestVideoW,
                          cfg_.guestVideoH);
                for (int s = 1; s <= hub_.guests(); ++s) {
                    if (WebrtcSession* sess = hub_.session(s)) {
                        if (sess->connected()) {
                            sess->pushLocalVideo(guestBuf.data(), guestBytes, cfg_.guestVideoW,
                                                 cfg_.guestVideoH);
                        }
                    }
                }
            }
        }
        // Pacing de ~30 fps.
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

// --- Uplink de AUDIO (invitado→B→A) ----------------------------------------
void MediaPump::forwardAudio(int slot, const float* samples, size_t n, int rate) {
    if (slot < 1 || slot >= static_cast<int>(outAudio_.size())) return;
    if (!samples || n == 0) return;
    auto& ring = outAudio_[static_cast<size_t>(slot)];
    if (!ring.header()) return; // ring no creado

    const size_t idx = static_cast<size_t>(slot);
    const size_t blockFloats = ring.blockFloats(); // 480 * canales
    const int channels = cfg_.audioChannels > 0 ? cfg_.audioChannels : 2;
    std::lock_guard<std::mutex> lock(*audioBufLock_[idx]);
    auto& buf = audioBuf_[idx];

    // --- Conversion de frecuencia si el invitado no entrega a la del ring ---
    // El ring B→A es de 48 kHz; un navegador puede entregar a 44.1 kHz. Antes
    // el `rate` se descartaba y esas muestras se publicaban tal cual, lo que
    // suena desafinado y corrido en el tiempo.
    if (rate > 0 && rate != cfg_.audioRate) {
        if (!audioResampler_[idx] || audioResamplerRate_[idx] != rate) {
            audioResampler_[idx] = std::make_unique<ba::dsp::Resampler>();
            audioResampler_[idx]->configure(rate, cfg_.audioRate, channels, ba::dsp::Quality::SincMedium);
            audioResamplerRate_[idx] = rate;
        }
        const size_t inFrames = n / static_cast<size_t>(channels);
        if (inFrames > 0) {
            // Margen holgado sobre el ratio para no quedarse corto por redondeo.
            const size_t maxOut =
                static_cast<size_t>(static_cast<double>(inFrames) * cfg_.audioRate / rate) + 32;
            auto& outBuf = audioResampleOut_[idx];
            if (outBuf.size() < maxOut * static_cast<size_t>(channels)) {
                outBuf.resize(maxOut * static_cast<size_t>(channels));
            }
            size_t outFrames = 0;
            if (audioResampler_[idx]->resample(samples, inFrames, outBuf.data(), maxOut, &outFrames) &&
                outFrames > 0) {
                buf.insert(buf.end(), outBuf.begin(),
                           outBuf.begin() +
                               static_cast<std::ptrdiff_t>(outFrames * static_cast<size_t>(channels)));
            }
        }
    } else {
        buf.insert(buf.end(), samples, samples + n);
    }

    // --- Re-bloqueo a bloques de 480 frames ---
    // Se avanza un INDICE de lectura en vez de hacer erase() desde el frente:
    // el erase movia todo el resto del buffer en cada bloque, dentro del
    // callback de libwebrtc y con el lock tomado.
    size_t& pos = audioBufPos_[idx];
    while (buf.size() - pos >= blockFloats) {
        const uint64_t qpc = HealthBlock::nowQpc();
        // push() copia el bloque; si hay overrun se descarta igual y se avanza.
        ring.push(buf.data() + pos, qpc);
        pos += blockFloats;
    }
    // Compactacion diferida: solo cuando lo consumido ya pesa.
    if (pos > 0 && pos >= blockFloats * 8) {
        buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos));
        pos = 0;
    }
}

// --- Uplink de VIDEO (invitado→B→A) ----------------------------------------
void MediaPump::forwardVideo(int slot, const uint8_t* pixels, size_t bytes, int w, int h) {
    (void)w;
    (void)h;
    if (slot < 1 || slot >= static_cast<int>(outVideo_.size())) return;
    if (!pixels || bytes == 0) return;
    auto& sender = outVideo_[static_cast<size_t>(slot)];
    if (!sender.published()) return;
    // frameIndex monotono: usa el reloj QPC como proxy (el sender no lleva
    // contador propio expuesto; el receiver detecta "frame nuevo" por el
    // frameIndex publicado en el meta block).
    FrameMeta meta{HealthBlock::nowQpc(), HealthBlock::nowQpc()};
    if (!sender.send(pixels, bytes, meta)) {
        // Send fallido (A no esta / buffer excedido): se loguea y se sigue.
        log_.warn("video out slot " + std::to_string(slot) + ": send fallo", -1);
    }
}

} // namespace ba::gateway
