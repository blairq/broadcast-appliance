// test_ipc_cross_process.cpp
//
// Prueba los contratos de IPC (spec §4.3 ring de audio, §4.4 bloque de salud)
// CRUZANDO UN LIMITE DE PROCESO REAL, con fork(): el padre crea y escribe, un
// hijo aparte abre por nombre y lee.
//
// Por que existe: hasta ahora el backend POSIX de SharedMemoryRegion era
// mmap(MAP_PRIVATE|MAP_ANONYMOUS) mas un registro name->puntero dentro del
// proceso, asi que `open()` devolvia el mismo puntero que `create()` y toda la
// suite validaba una semantica intra-proceso que no era la de produccion. Con
// shm_open + MAP_SHARED el contrato ya es el real, y este test es el que lo
// comprueba: si alguien vuelve a un backend que no comparta de verdad, este
// test falla y los otros no.
//
// Casos:
//   1. Ring de audio: el padre empuja N bloques con un patron derivado del
//      indice; el hijo los lee y verifica el patron y el timestamp. Se prueba
//      que los datos cruzan y que los indices atomicos se ven desde ambos
//      lados.
//   2. Bloque de salud: el padre publica identidad y latidos; el hijo lee el
//      snapshot bajo el seqlock y comprueba que ve el pid y el estado.
//
// Fuera de POSIX el test se salta (codigo 77, el SKIP de CTest): fork() no
// existe en Windows y el backend de alla es otro (CreateFileMappingW), que se
// valida en Windows.

#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using ba::ipc::HealthBlock;
using ba::ipc::PoppedBlock;
using ba::ipc::ProcessState;
using ba::ipc::SharedAudioRing;
using ba::ipc::Snapshot;
using ba::ipc::Status;
namespace audio_ring = ba::ipc::audio_ring;

// Los helpers y el contador viven en el camino POSIX: el main de Windows es
// solo el SKIP y no los usa (declararlos alla dispara -Wunused-function).
#if !defined(_WIN32)

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

constexpr const wchar_t* kRingName = L"BA.Test.Xproc.Ring";
constexpr const wchar_t* kHealthName = L"BA.Test.Xproc.Health";
constexpr uint32_t kChannels = 2;
constexpr uint64_t kCapBlocks = 16;
constexpr uint64_t kTotalBlocks = 64;
constexpr uint32_t kChildPid = 4242;
constexpr uint32_t kChildVersion = 7;

/// Valor de la muestra `i` del bloque `blockIndex`. Determinista, para poder
/// verificar desde el otro proceso sin compartir estado.
float sentinel(uint64_t blockIndex, size_t i) {
    return static_cast<float>((blockIndex * 131u + i) % 1000u) / 1000.0f;
}

void fillBlock(std::vector<float>& buf, uint64_t blockIndex) {
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = sentinel(blockIndex, i);
    }
}

} // namespace

#else // _WIN32

int main() {
    std::printf("SKIP: fork() no existe en Windows (el backend de alla es CreateFileMappingW)\n");
    return 77; // SKIP_RETURN_CODE de CTest
}

#endif

#if !defined(_WIN32)

namespace {

/// Cuerpo del proceso HIJO: abre por nombre y verifica. Devuelve 0 si todo ok.
int childMain() {
    // El padre ya creo las regiones antes del fork, asi que open() debe
    // encontrarlas de inmediato. Se reintenta igual por si el planificador
    // ordena distinto.
    SharedAudioRing reader;
    bool opened = false;
    for (int attempt = 0; attempt < 200 && !opened; ++attempt) {
        if (SharedAudioRing::openReader(kRingName, reader).ok) {
            opened = true;
            break;
        }
        usleep(5000);
    }
    if (!opened) {
        std::printf("hijo: no pudo abrir el ring\n");
        return 2;
    }

    const size_t floats = reader.blockFloats();
    if (floats != static_cast<size_t>(audio_ring::kBlockFrames) * kChannels) {
        std::printf("hijo: blockFloats inesperado (%zu)\n", floats);
        return 3;
    }

    std::vector<float> out(floats);
    uint64_t got = 0;
    const auto childDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (got < kTotalBlocks && std::chrono::steady_clock::now() < childDeadline) {
        PoppedBlock meta;
        if (!reader.pop(out.data(), meta)) {
            return 4;
        }
        if (meta.isSilence) {
            usleep(200);
            continue;
        }
        // Verifica el patron del bloque esperado y su timestamp.
        for (size_t i = 0; i < floats; ++i) {
            if (out[i] != sentinel(got, i)) {
                std::printf("hijo: patron incorrecto en bloque %llu, muestra %zu\n",
                            static_cast<unsigned long long>(got), i);
                return 5;
            }
        }
        if (meta.qpc != got + 1000) {
            std::printf("hijo: qpc incorrecto en bloque %llu\n", static_cast<unsigned long long>(got));
            return 6;
        }
        ++got;
    }
    if (got != kTotalBlocks) {
        std::printf("hijo: solo leyo %llu de %llu bloques\n", static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(kTotalBlocks));
        return 7;
    }

    // --- Bloque de salud: leer el snapshot publicado por el padre ---
    HealthBlock health;
    if (!HealthBlock::open(kHealthName, health).ok) {
        std::printf("hijo: no pudo abrir el bloque de salud\n");
        return 8;
    }
    Snapshot snap{};
    bool sawRunning = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        snap = health.read(0);
        if (snap.pid == kChildPid && snap.state == ProcessState::Running) {
            sawRunning = true;
            break;
        }
        usleep(5000);
    }
    if (!sawRunning) {
        std::printf("hijo: no vio la identidad/estado publicados (pid=%u state=%d)\n", snap.pid,
                    static_cast<int>(snap.state));
        return 9;
    }
    if (snap.version != kChildVersion) {
        std::printf("hijo: version inesperada (%u)\n", snap.version);
        return 10;
    }
    if (snap.lastBeatQpc == 0) {
        std::printf("hijo: sin latido\n");
        return 11;
    }
    return 0;
}

} // namespace

int main() {
    // --- El PADRE crea las dos regiones antes de bifurcar ---
    SharedAudioRing ring;
    Status st = SharedAudioRing::init(kRingName, kChannels, kCapBlocks, /*writerPid=*/1234,
                                      /*generation=*/1, ring);
    CHECK(st.ok, "init del ring en el padre");
    if (!st.ok) {
        return 1;
    }

    HealthBlock health;
    st = HealthBlock::create(kHealthName, /*slotCount=*/2, health);
    CHECK(st.ok, "create del bloque de salud en el padre");
    if (!st.ok) {
        return 1;
    }
    health.setIdentity(0, kChildPid, kChildVersion);
    health.setState(0, ProcessState::Running);
    health.beat(0);

    const pid_t pid = fork();
    CHECK(pid >= 0, "fork()");
    if (pid < 0) {
        return 1;
    }

    if (pid == 0) {
        // HIJO: proceso separado de verdad; solo comparte lo que el nombre
        // publique a traves del sistema operativo.
        const int rc = childMain();
        _exit(rc);
    }

    // --- PADRE: escribe los bloques y sigue latiendo ---
    const size_t floats = ring.blockFloats();
    std::vector<float> buf(floats);
    uint64_t pushed = 0;
    // Plazo de pared, no un contador de vueltas: si el hijo no esta leyendo (el
    // caso de un backend que no comparte de verdad) el padre se queda en
    // overrun permanente. Con un plazo el test FALLA en segundos en vez de
    // colgarse hasta el timeout de CTest, que en CI es un diagnostico opaco.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (pushed < kTotalBlocks && std::chrono::steady_clock::now() < deadline) {
        fillBlock(buf, pushed);
        if (ring.push(buf.data(), pushed + 1000)) {
            ++pushed;
        } else {
            // Overrun: el lector todavia no consumio. Ceder y reintentar.
            usleep(200);
        }
        health.beat(0);
    }
    CHECK(pushed == kTotalBlocks, "el padre empujo todos los bloques (el lector avanza)");
    if (pushed != kTotalBlocks) {
        // Sin lector real no tiene sentido esperar al hijo: se lo termina para
        // no dejar un proceso colgado y se reporta el fallo.
        kill(pid, SIGKILL);
    }

    int status = 0;
    const pid_t waited = waitpid(pid, &status, 0);
    CHECK(waited == pid, "waitpid del hijo");
    CHECK(WIFEXITED(status), "el hijo termino normalmente");
    const int childRc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    CHECK(childRc == 0, "el hijo leyo y verifico todo el trafico IPC");
    if (childRc != 0) {
        std::printf("  (codigo de salida del hijo: %d)\n", childRc);
    }

    if (g_failures == 0) {
        std::printf("test_ipc_cross_process: OK (%llu bloques entre 2 procesos)\n",
                    static_cast<unsigned long long>(kTotalBlocks));
        return 0;
    }
    std::printf("test_ipc_cross_process: %d fallo(s)\n", g_failures);
    return 1;
}

#endif // _WIN32
