// test_video_cross_process.cpp
//
// Transporte de video (spec §4.2, backend CPU) CRUZANDO un limite de proceso
// real. Hasta ahora el backend fuera de Windows era un registro dentro del
// proceso: sender y receiver tenian que vivir en el mismo proceso, asi que el
// video del invitado nunca llegaba de B a A. Este test es el que lo cubre.
//
// El binario se relanza a si mismo como productor (`--child ...`) con
// ba::platform::Process, igual en Windows, Linux y macOS.
//
// Casos:
//   1. Sin frames cortados: el productor escribe lo mas rapido que puede
//      frames uniformes (todos los bytes = indice del frame) mientras el
//      consumidor lee. Cada frame recibido debe ser uniforme y los indices
//      crecientes: un frame "mezclado" delataria una lectura durante una
//      escritura.
//   2. Productor reiniciado: el productor muere y otro publica el MISMO
//      nombre. `stale()` lo detecta y la resuscripcion ve los frames nuevos
//      (sin esto el consumidor queda mirando la memoria vieja: video
//      congelado).
//   4. Ring de AUDIO con productor reiniciado: mismo problema que el video
//      (el lector quedaba leyendo un ring que ya nadie escribe: invitado
//      mudo). SharedAudioRing::stale() lo detecta por (writerPid, generation).
//   3. Rendimiento del fallback CPU a 1080p: tiempo de send + receive por
//      frame (informativo, con un tope holgado para detectar regresiones
//      groseras).

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace ba::ipc;
using namespace std::chrono_literals;

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BA_UNDER_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define BA_UNDER_SANITIZER 1
#endif
#endif

namespace {

int g_failures = 0;
std::atomic<bool> g_stopChild{false};

extern "C" void onTerm(int) { g_stopChild.store(true); }

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            std::fflush(stdout);                                                                             \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

int currentPid() {
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

ba::platform::ProcessOptions childOpts(const std::vector<std::string>& args) {
    ba::platform::ProcessOptions o;
    o.exe = ba::platform::executablePath();
    o.args = {"--child"};
    o.args.insert(o.args.end(), args.begin(), args.end());
    return o;
}

/// Suscribe con reintentos (el productor tarda en publicar).
bool subscribeWithRetry(SpoutReceiver& rcv, const std::wstring& name, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (rcv.subscribe(name.c_str())) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Productor (proceso hijo)
// ---------------------------------------------------------------------------

/// --child produce <nombre> <frames> <valorBase>: publica 320x180 y manda
/// `frames` frames uniformes (valor = base + i) sin pausa; despues se queda
/// vivo hasta que lo maten (el slot desaparece cuando el proceso muere).
/// Con frames < 0 emite sin fin, un frame cada 20 ms.
int produce(const std::string& name, int frames, int base) {
    SpoutSender snd;
    if (!snd.publish(widen(name).c_str(), SlotConfig{320, 180})) {
        std::printf("productor: publish fallo\n");
        return 2;
    }
    std::vector<std::uint8_t> px(320u * 180u * 4u);
    for (int i = 1; frames < 0 || i <= frames; ++i) {
        std::memset(px.data(), (base + i) & 0xFF, px.size());
        snd.send(px.data(), px.size(),
                 FrameMeta{static_cast<std::uint64_t>(i), static_cast<std::uint64_t>(i) * 10});
        if (frames < 0) {
            std::this_thread::sleep_for(20ms);
            if (g_stopChild.load()) {
                break;
            }
        }
    }
    // SIGTERM (terminate()) sale limpio: el destructor del sender retira el
    // nombre. Un kill() simula un crash y deja el objeto huerfano, que el
    // proximo publish del mismo nombre reemplaza.
    while (!g_stopChild.load()) {
        std::this_thread::sleep_for(20ms);
    }
    return 0;
}

/// --child ring <nombre>: crea el ring de audio (PID real como identidad) y
/// empuja un bloque de 10 ms cada 10 ms hasta SIGTERM.
int produceRing(const std::string& name) {
    SharedAudioRing ring;
    if (!SharedAudioRing::init(widen(name).c_str(), 2, audio_ring::kDefaultCapacityBlocks,
                               static_cast<std::uint32_t>(currentPid()), 1, ring)
             .ok) {
        return 2;
    }
    std::vector<float> block(ring.blockFloats(), 0.25f);
    std::uint64_t qpc = 0;
    while (!g_stopChild.load()) {
        ring.push(block.data(), ++qpc);
        std::this_thread::sleep_for(10ms);
    }
    return 0;
}

/// Espera un bloque con datos reales (no silencio) del ring.
bool popData(SharedAudioRing& ring, int ms) {
    std::vector<float> out(ring.blockFloats());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        PoppedBlock b;
        if (ring.pop(out.data(), b) && !b.isSilence) {
            return out[0] == 0.25f;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

bool openRingWithRetry(SharedAudioRing& ring, const std::wstring& name, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (SharedAudioRing::openReader(name.c_str(), ring).ok) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

// ---------------------------------------------------------------------------
// 1) Sin frames cortados
// ---------------------------------------------------------------------------

void testNoTearing() {
    const std::string name = "BA.Test.Video." + std::to_string(currentPid()) + ".a";
    const int frames = 20000;
    ba::platform::Process producer;
    CHECK(ba::platform::Process::spawn(childOpts({"produce", name, std::to_string(frames), "0"}), producer));

    SpoutReceiver rcv;
    CHECK(subscribeWithRetry(rcv, widen(name), 5000));
    CHECK(rcv.width() == 320 && rcv.height() == 180);

    std::vector<std::uint8_t> px(rcv.frameBytes());
    std::uint64_t lastIndex = 0;
    int received = 0;
    int torn = 0;
    bool ordered = true;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (lastIndex < static_cast<std::uint64_t>(frames) && std::chrono::steady_clock::now() < deadline) {
        FrameMeta m{};
        if (!rcv.receive(px.data(), px.size(), m)) {
            std::this_thread::yield();
            continue;
        }
        ++received;
        const std::uint8_t v = static_cast<std::uint8_t>(m.frameIndex & 0xFF);
        for (std::uint8_t b : px) {
            if (b != v) {
                ++torn;
                break;
            }
        }
        ordered = ordered && m.frameIndex > lastIndex && m.qpc == m.frameIndex * 10;
        lastIndex = m.frameIndex;
    }
    std::printf("  recibidos %d de %d frames (el lector toma el mas nuevo), cortados: %d\n", received, frames,
                torn);
    CHECK(received > 0);
    CHECK(torn == 0);
    CHECK(ordered);
    CHECK(lastIndex == static_cast<std::uint64_t>(frames)); // el ultimo siempre llega
    producer.terminate();
    producer.wait(5000);
}

// ---------------------------------------------------------------------------
// 2) Productor reiniciado
// ---------------------------------------------------------------------------

void testProducerRestart() {
    const std::string name = "BA.Test.Video." + std::to_string(currentPid()) + ".b";
    ba::platform::Process first;
    CHECK(ba::platform::Process::spawn(childOpts({"produce", name, "1", "10"}), first));
    SpoutReceiver rcv;
    CHECK(subscribeWithRetry(rcv, widen(name), 5000));
    CHECK(!rcv.stale());

    // Muere el productor y otro publica el mismo nombre.
    first.kill();
    first.wait(5000);
    ba::platform::Process second;
    CHECK(ba::platform::Process::spawn(childOpts({"produce", name, "-1", "100"}), second));

    // stale() tiene que notarlo (mira como mucho una vez por segundo).
    bool detected = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!detected && std::chrono::steady_clock::now() < deadline) {
        detected = rcv.stale();
        std::this_thread::sleep_for(50ms);
    }
    CHECK(detected);

    // Resuscripcion: llegan los frames del productor NUEVO (valor = 100 + i).
    rcv = SpoutReceiver{};
    CHECK(subscribeWithRetry(rcv, widen(name), 5000));
    std::vector<std::uint8_t> px(rcv.frameBytes());
    FrameMeta m{};
    bool got = false;
    const auto until = std::chrono::steady_clock::now() + 3s;
    while (!got && std::chrono::steady_clock::now() < until) {
        got = rcv.receive(px.data(), px.size(), m);
        std::this_thread::sleep_for(5ms);
    }
    CHECK(got);
    CHECK(got && px[0] == static_cast<std::uint8_t>((100 + m.frameIndex) & 0xFF));
    CHECK(!rcv.stale());
    second.terminate();
    second.wait(5000);
}

// ---------------------------------------------------------------------------
// 3) Rendimiento del fallback CPU a 1080p (mismo proceso: mide la copia)
// ---------------------------------------------------------------------------

void testThroughput1080p() {
    const std::wstring name = widen("BA.Test.Video." + std::to_string(currentPid()) + ".c");
    SpoutSender snd;
    CHECK(snd.publish(name.c_str(), SlotConfig{1920, 1080}));
    SpoutReceiver rcv;
    CHECK(rcv.subscribe(name.c_str()));
    std::vector<std::uint8_t> src(rcv.frameBytes(), 0x42);
    std::vector<std::uint8_t> dst(rcv.frameBytes());
    const int n = 120;
    const auto t0 = std::chrono::steady_clock::now();
    int got = 0;
    for (int i = 1; i <= n; ++i) {
        snd.send(src.data(), src.size(), FrameMeta{static_cast<std::uint64_t>(i), 0});
        FrameMeta m{};
        got += rcv.receive(dst.data(), dst.size(), m) ? 1 : 0;
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    std::printf("  1080p BGRA: %.2f ms por frame (send + receive, fallback CPU)\n", ms);
    CHECK(got == n);
    // Presupuesto holgado: a 60 fps hay 16,6 ms por frame en total. Con
    // sanitizers (ASan/TSan) todo corre varias veces mas lento: no se mide.
#if !defined(BA_UNDER_SANITIZER)
    CHECK(ms < 12.0);
#endif
}

// ---------------------------------------------------------------------------
// 4) Ring de audio con productor reiniciado
// ---------------------------------------------------------------------------

void testRingProducerRestart() {
    const std::string name = "BA.Test.Ring." + std::to_string(currentPid());
    ba::platform::Process first;
    CHECK(ba::platform::Process::spawn(childOpts({"ring", name}), first));
    SharedAudioRing ring;
    CHECK(openRingWithRetry(ring, widen(name), 5000));
    CHECK(popData(ring, 3000));
    CHECK(!ring.stale());

    first.kill(); // crash: el ring viejo queda sin escritor
    first.wait(5000);
    ba::platform::Process second;
    CHECK(ba::platform::Process::spawn(childOpts({"ring", name}), second));

#ifdef _WIN32
    // Windows: el mapeo con nombre sigue vivo mientras el lector lo tenga
    // abierto, asi que el productor nuevo escribe en EL MISMO ring (pop()
    // resincroniza por el cambio de pid/generacion). No hace falta reabrir:
    // el audio nuevo llega por el mismo ring.
    CHECK(popData(ring, 5000));
    second.terminate();
    second.wait(5000);
    return;
#endif

    bool detected = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    std::vector<float> out(ring.blockFloats());
    while (!detected && std::chrono::steady_clock::now() < deadline) {
        PoppedBlock b;
        ring.pop(out.data(), b); // el consumidor sigue leyendo (silencio)
        detected = ring.stale();
        std::this_thread::sleep_for(20ms);
    }
    CHECK(detected);

    ring = SharedAudioRing{};
    CHECK(openRingWithRetry(ring, widen(name), 5000));
    CHECK(popData(ring, 3000)); // audio del productor nuevo
    second.terminate();
    second.wait(5000);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "--child") == 0 && std::strcmp(argv[2], "ring") == 0) {
        std::signal(SIGTERM, onTerm);
        return produceRing(argv[3]);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0 && std::strcmp(argv[2], "produce") == 0 &&
        argc >= 6) {
        std::signal(SIGTERM, onTerm);
        return produce(argv[3], std::atoi(argv[4]), std::atoi(argv[5]));
    }
    testNoTearing();
    testProducerRestart();
    testRingProducerRestart();
    testThroughput1080p();
    if (g_failures == 0) {
        std::printf("test_video_cross_process: OK\n");
        return 0;
    }
    std::printf("test_video_cross_process: %d fallo(s)\n", g_failures);
    return 1;
}
