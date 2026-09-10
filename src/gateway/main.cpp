// ============================================================================
// src/gateway/main.cpp — `ba-gateway` (Proceso B) — FAKE GATEWAY, Fase 1
//
// Proposito:
//   Doble de prueba headless del Proceso B (spec Fase 1: "un fake-gateway.exe
//   que emite patron de video y tono"). En lugar de libwebrtc + CEF + tunel,
//   emite por cada slot de invitado (1..N):
//     - AUDIO : un ring SPSC `BA.Audio.Guest.<n>` (48 kHz, 2 canales) al que
//               un thread productor emite un tono senoidal (440/550/660/880 Hz
//               segun el slot) en bloques de 480 frames, al ritmo de tiempo
//               real, para que un lector (A / ba-sources) lo consuma en vivo.
//     - VIDEO : un sender Spout `BA.Guest.<n>` (1280x720) al que un thread
//               emite un patron de prueba (gradiente diagonal en movimiento)
//               a ~30 fps, con FrameMeta{frameIndex++, nowQpc()}.
//   Tambien publica su salud en `BA.Health` (slot 0 = B): beat() cada 250 ms,
//   Running al arrancar y Terminated al salir ordenadamente.
//
//   El objetivo es que A (y las fuentes IPC de ba-sources) tengan flujos IPC
//   reales contra los que probar el reenganche (100 kills de A) y la ausencia
//   de deriva en 8 h, sin necesidad del gateway real.
//
// Modelo de threading:
//   - Thread PRINCIPAL = hilo de control: instala el handler de senal, abre el
//     HealthBlock, crea los rings/senders, arranca los threads productores y
//     ejecuta el bucle de vida: beat(0) cada 250 ms y chequeo de parada
//     (duracion transcurrida o SIGINT). Al terminar: senala parada a los
//     productores, los join(), publica Terminated y loguea el cierre.
//   - Un thread PRODUCTOR DE AUDIO por slot: emite bloques de 480 frames con
//     un acumulador de fase continuo (el tono no se corta entre bloques).
//   - Un thread PRODUCTOR DE VIDEO por slot: emite un frame de 1280x720x4 con
//     el gradiente desplazado por frameIndex.
//   - NINGUN thread lanza: los fallos de push/send se loguean y se ignoran
//     (degradacion elegante). Los threads se detienen por un std::atomic<bool>
//     de parada; no hay excepciones cruzando el borde de un thread.
//
// Estrategia de errores:
//   - Si un ring o un sender no se puede crear/publicar, se loguea el fallo
//     (componente "fakegw", codigo de Status) y se CONTINUA con los demas
//     slots: un slot caido no tumba al gateway.
//   - El HealthBlock: primero intenta open() (el launcher ya lo creo); si no
//     existe, create() con 4 slots para poder correr en solitario.
//   - Sin dialogos de sistema: todo el log va a stderr con el componente
//     "fakegw" (formato `nivel | fakegw | codigo | contexto`).
//
// Compila en Windows/MSVC y en Linux; todo lo Windows-only va detras de
// #ifdef _WIN32. No depende de Qt ni de libobs.
// ============================================================================

#ifdef _WIN32
// MinGW/MSVC: M_PI solo se define si _USE_MATH_DEFINES esta definido ANTES
// de incluir <cmath>. En Linux M_PI siempre esta disponible.
#define _USE_MATH_DEFINES
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ba_core (frozen headers).
#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SharedMemoryRegion.hpp"
#include "ipc/SpoutTransport.hpp" // SpoutSender, SlotConfig, FrameMeta (ba_core)

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <csignal>
#    include <unistd.h>
#endif

namespace {

using namespace ba::ipc;

// --- Constantes del fake-gateway --------------------------------------------
inline constexpr const char* kComponent = "fakegw";
inline constexpr int kHealthSlot = 0; // B ocupa el slot 0 (segun la tarea)
inline constexpr std::size_t kHealthSlotCount = 4;
inline constexpr uint32_t kAudioChannels = 2;
inline constexpr uint64_t kAudioCapacityBlocks = 16; // 16*480 = 7680 frames (>= 4800)
inline constexpr uint32_t kVideoWidth = 1280;
inline constexpr uint32_t kVideoHeight = 720;
inline constexpr int kVideoFps = 30;
inline constexpr int kMaxGuests = 4;
inline constexpr uint32_t kProcessVersion = 1;

// Frecuencias de tono por slot (spec Fase 1: 440/550/660/880).
inline constexpr double kToneHz[kMaxGuests] = {440.0, 550.0, 660.0, 880.0};

// --- Logger minimo (componente "fakegw") ------------------------------------
// Sin dialogos de sistema: todo a stderr. Formato estructurado
// `nivel | fakegw | codigo | contexto`. Thread-safe por el mutex.
struct Logger {
    mutable std::mutex m;
    void info(const std::string& ctx) const { line("info", 0, ctx); }
    void warn(const std::string& ctx, int code) const { line("warn", code, ctx); }
    void error(const std::string& ctx, int code) const { line("error", code, ctx); }

private:
    void line(const char* level, int code, const std::string& ctx) const {
        std::lock_guard<std::mutex> lock(m);
        std::fprintf(stderr, "%s | %s | %d | %s\n", level, kComponent, code, ctx.c_str());
        std::fflush(stderr);
    }
};

// --- Argumentos -------------------------------------------------------------
struct Args {
    int guests = 2;        // 1..4 (default 2)
    long long durationMs = 3000; // 0 = correr hasta senal
    int rate = 48000;      // sample rate del tono
};

/// Parsea --guests N, --duration-ms M y --rate R (todos opcionales).
///
/// :param argc: cantidad de argumentos.
/// :param argv: argumentos.
/// :returns: Args con los valores (guests acotado a 1..kMaxGuests, durationMs
///           >= 0, rate > 0).
Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto nextInt = [&]() -> long long {
            if (i + 1 >= argc) return 0;
            return std::strtol(argv[++i], nullptr, 10);
        };
        if (arg == "--guests") {
            a.guests = static_cast<int>(nextInt());
        } else if (arg == "--duration-ms") {
            a.durationMs = nextInt();
        } else if (arg == "--rate") {
            a.rate = static_cast<int>(nextInt());
        }
        // Desconocidos: se ignoran (el fake es tolerante).
    }
    if (a.guests < 1) a.guests = 1;
    if (a.guests > kMaxGuests) a.guests = kMaxGuests;
    if (a.durationMs < 0) a.durationMs = 0;
    if (a.rate <= 0) a.rate = 48000;
    return a;
}

// --- Senal de parada (SIGINT en POSIX, CtrlHandler en Windows) -------------
std::atomic<bool> g_stop{false};

#ifdef _WIN32
BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        g_stop.store(true);
    }
    return TRUE;
}
#else
void sigHandler(int) { g_stop.store(true); }
#endif

// --- Productor de AUDIO por slot -------------------------------------------
/// Emite un tono senoidal continuo en bloques de 480 frames al ritmo de tiempo
/// real. El acumulador de fase persiste entre bloques para que el tono no se
/// corte. No lanza: un push fallido (overrun) se cuenta y se sigue.
void audioProducer(SharedAudioRing ring, int slot, int rate,
                   const std::atomic<bool>& stop, Logger& log) {
    const double freq = kToneHz[slot - 1];
    const size_t blockFloats = ring.blockFloats(); // 480 * channels
    std::vector<float> buf(blockFloats);

    // Duracion real de un bloque a `rate` Hz (10 ms a 48 kHz).
    const auto blockPeriod = std::chrono::microseconds(
        static_cast<long long>(480.0 * 1e6 / static_cast<double>(rate)));

    double phase = 0.0;
    const double phaseStep = 2.0 * M_PI * static_cast<double>(freq) /
                             static_cast<double>(rate);
    uint64_t blocks = 0;

    while (!stop.load(std::memory_order_relaxed)) {
        // Genera el bloque: seno por canal (identico en ambos canales).
        for (size_t f = 0; f < blockFloats; ++f) {
            const float s = static_cast<float>(std::sin(phase));
            phase += phaseStep;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
            buf[f] = s;
        }
        const uint64_t qpc = HealthBlock::nowQpc();
        if (!ring.push(buf.data(), qpc)) {
            // Overrun: el lector no llego; se descarto el bloque.
            continue;
        }
        ++blocks;
        std::this_thread::sleep_for(blockPeriod);
    }
    log.info("audio slot " + std::to_string(slot) + ": parado (" +
             std::to_string(blocks) + " bloques, " + std::to_string(freq) + " Hz)");
}

// --- Productor de VIDEO por slot -------------------------------------------
/// Emite un patron de prueba (gradiente diagonal en movimiento) a ~30 fps.
/// El desplazamiento depende de frameIndex, de modo que el gradiente se mueve.
/// No lanza: un send fallido se loguea y se sigue.
void videoProducer(SpoutSender sender, int slot, const std::atomic<bool>& stop,
                   Logger& log) {
    const size_t frameBytes = static_cast<size_t>(kVideoWidth) * kVideoHeight * 4u;
    std::vector<uint8_t> frame(frameBytes);
    const auto framePeriod = std::chrono::microseconds(1'000'000 / kVideoFps);
    uint64_t frameIndex = 0;

    while (!stop.load(std::memory_order_relaxed)) {
        // Gradiente diagonal: v = (x + y + desplaz) % 256, BGRA (B=G=R=v, A=255).
        const int shift = static_cast<int>(frameIndex % 256);
        uint8_t* p = frame.data();
        for (uint32_t y = 0; y < kVideoHeight; ++y) {
            for (uint32_t x = 0; x < kVideoWidth; ++x) {
                const uint8_t v = static_cast<uint8_t>((x + y + shift) & 0xFF);
                p[0] = v; // B
                p[1] = v; // G
                p[2] = v; // R
                p[3] = 255; // A
                p += 4;
            }
        }
        FrameMeta meta{frameIndex, HealthBlock::nowQpc()};
        if (!sender.send(frame.data(), frameBytes, meta)) {
            log.warn("video slot " + std::to_string(slot) + ": send fallo", -1);
        }
        ++frameIndex;
        std::this_thread::sleep_for(framePeriod);
    }
    log.info("video slot " + std::to_string(slot) + ": parado (" +
             std::to_string(frameIndex) + " frames)");
}

// --- Nombre de ring/sender por slot ----------------------------------------
std::wstring audioRingName(int slot) { return L"BA.Audio.Guest." + std::to_wstring(slot); }
std::wstring videoSenderName(int slot) { return L"BA.Guest." + std::to_wstring(slot); }

} // namespace

/// Punto de entrada del fake-gateway (Proceso B, Fase 1).
///
/// :param argc: cantidad de argumentos de linea de comandos.
/// :param argv: argumentos (--guests, --duration-ms, --rate).
/// :returns: 0 en cierre ordenado; 1 si no se pudo abrir el HealthBlock.
int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    Logger log;

    log.info("arranque: guests=" + std::to_string(args.guests) +
             " duration_ms=" + std::to_string(args.durationMs) +
             " rate=" + std::to_string(args.rate) + " spout=ba_core");

    // Handler de senal para parada ordenada (SIGINT / Ctrl+C).
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
#else
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);
#endif

    // --- HealthBlock (BA.Health): open si el launcher lo creo, si no create.
    HealthBlock health;
    {
        Status s = HealthBlock::open(HealthBlock::kName, health);
        if (!s.ok) {
            s = HealthBlock::create(HealthBlock::kName, kHealthSlotCount, health);
            if (!s.ok) {
                log.error("no se pudo crear BA.Health", s.code);
                return 1;
            }
        }
    }
    const uint32_t pid = static_cast<uint32_t>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        static_cast<long>(getpid())
#endif
    );
    health.setIdentity(kHealthSlot, pid, kProcessVersion);
    health.setState(kHealthSlot, ProcessState::Running);
    log.info("BA.Health slot 0 = Running (pid=" + std::to_string(pid) + ")");

    // --- Crea rings de audio y senders de video, arranca productores.
    std::vector<std::thread> producers;
    producers.reserve(2 * args.guests);

    for (int n = 1; n <= args.guests; ++n) {
        // AUDIO: ring BA.Audio.Guest.<n> + productor de tono.
        {
            SharedAudioRing ring;
            Status s = SharedAudioRing::init(audioRingName(n).c_str(), kAudioChannels,
                                             kAudioCapacityBlocks, pid, 1, ring);
            if (!s.ok) {
                log.warn("audio slot " + std::to_string(n) +
                         ": no se pudo crear el ring (se saltea)", s.code);
            } else {
                log.info("audio slot " + std::to_string(n) + ": ring BA.Audio.Guest." +
                         std::to_string(n) + " listo, tono " +
                         std::to_string(kToneHz[n - 1]) + " Hz");
                producers.emplace_back(audioProducer, std::move(ring), n, args.rate,
                                       std::ref(g_stop), std::ref(log));
            }
        }
        // VIDEO: sender BA.Guest.<n> + productor de patron.
        {
            SpoutSender sender;
            SlotConfig cfg{kVideoWidth, kVideoHeight};
            if (!sender.publish(videoSenderName(n).c_str(), cfg)) {
                log.warn("video slot " + std::to_string(n) +
                         ": no se pudo publicar el sender (se saltea)", -1);
            } else {
                log.info("video slot " + std::to_string(n) + ": sender BA.Guest." +
                         std::to_string(n) + " listo, patron " +
                         std::to_string(kVideoWidth) + "x" + std::to_string(kVideoHeight) +
                         " @ " + std::to_string(kVideoFps) + " fps");
                producers.emplace_back(videoProducer, std::move(sender), n,
                                       std::ref(g_stop), std::ref(log));
            }
        }
    }

    if (producers.empty()) {
        log.warn("ningun slot activo; el gateway no emite nada", 0);
    }

    // --- Bucle de vida: beat(0) cada 250 ms hasta duracion o senal.
    const auto start = std::chrono::steady_clock::now();
    const auto beatPeriod = std::chrono::milliseconds(250);
    health.beat(kHealthSlot); // primer latido inmediato

    while (!g_stop.load(std::memory_order_relaxed)) {
        if (args.durationMs > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (elapsed >= args.durationMs) {
                break;
            }
        }
        std::this_thread::sleep_for(beatPeriod);
        health.beat(kHealthSlot);
    }

    log.info("parando: deteniendo productores...");

    // --- Shutdown ordenado: 1) senalar parada, 2) join, 3) Terminated, 4) log.
    g_stop.store(true);
    for (auto& t : producers) {
        if (t.joinable()) t.join();
    }
    health.setState(kHealthSlot, ProcessState::Terminated);
    log.info("cierre ordenado: BA.Health slot 0 = Terminated; exito");
    return 0;
}
