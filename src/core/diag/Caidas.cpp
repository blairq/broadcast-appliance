// ============================================================================
// src/core/diag/Caidas.cpp — ver Caidas.hpp
// ============================================================================
#include "diag/Caidas.hpp"
#include "diag/SalidaAsincrona.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// Lo común a los tres sistemas de la captura en vivo (capturarPilas): fuera del
// manejador de caídas, así que acá sí se puede pedir memoria.
namespace ba::diag::captura {

inline std::string horaLocal(const char* formato) {
    const std::time_t ahora = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &ahora);
#else
    localtime_r(&ahora, &tm);
#endif
    char b[40];
    std::strftime(b, sizeof b, formato, &tm);
    return b;
}

/// El .json de al lado: lo de siempre (tipo, hora, volcado…) más `extra` (un objeto JSON).
inline void escribirJson(const std::filesystem::path& ruta, nlohmann::json j, const std::string& extra) {
    const nlohmann::json e = nlohmann::json::parse(extra, nullptr, false);
    if (e.is_object())
        for (auto it = e.begin(); it != e.end(); ++it)
            if (!j.contains(it.key())) j[it.key()] = it.value();
    std::ofstream f(ruta, std::ios::binary | std::ios::trunc);
    f << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
}

} // namespace ba::diag::captura

#if !defined(_WIN32)

#include "diag/CaidaCompartida.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <initializer_list>
#include <pthread.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <sys/ucontext.h>
#else
#include <dirent.h>
#include <link.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#endif

namespace ba::diag {
namespace {

// Todo lo que el manejador usa se prepara al instalar: cayéndose no se pide
// memoria ni se toma un candado (ver Caidas.hpp, «async-signal-safety»).
const char* g_quien = "proceso";
int g_fd = STDERR_FILENO; ///< el archivo de Logs/ (no la cola: el proceso muere antes)
int g_fdPilas = -1;       ///< el que dejó el launcher (BA_CAIDA_CANAL): las pilas de todos los hilos
std::atomic<int> g_cayendo{0};
constexpr int kMarcos = 64;
constexpr int kMaxHilos = 256;
alignas(16) char g_pilaAlterna[64 * 1024]; ///< para que un desborde de pila también deje rastro

// --- escribir sin pedir memoria --------------------------------------------------------------

/// Un búfer chico en la pila de quien escribe; se vuelca con `write` en bucle (en
/// un manejador de señal no hay segunda oportunidad).
struct Esc {
    int fd;
    char b[1024];
    std::size_t n = 0;
    explicit Esc(int f) : fd(f) {}
    ~Esc() { vaciar(); }
    void vaciar() {
        const char* p = b;
        while (n > 0 && fd >= 0) {
            const ssize_t w = write(fd, p, n);
            if (w <= 0) break;
            p += w;
            n -= static_cast<std::size_t>(w);
        }
        n = 0;
    }
    Esc& s(const char* t) {
        for (; t != nullptr && *t != 0; ++t) {
            if (n == sizeof b) vaciar();
            b[n++] = *t;
        }
        return *this;
    }
    Esc& hex(std::uintptr_t v) {
        char t[2 + 16 + 1];
        int i = sizeof t - 1;
        t[i] = 0;
        do {
            t[--i] = "0123456789abcdef"[v & 0xF];
            v >>= 4;
        } while (v != 0 && i > 2);
        t[--i] = 'x';
        t[--i] = '0';
        return s(t + i);
    }
    Esc& dec(long long v) {
        char t[24];
        int i = sizeof t - 1;
        t[i] = 0;
        const bool neg = v < 0;
        unsigned long long u = neg ? 0ULL - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
        do {
            t[--i] = static_cast<char>('0' + u % 10);
            u /= 10;
        } while (u != 0 && i > 1);
        if (neg) t[--i] = '-';
        return s(t + i);
    }
};

const char* nombreDeSenal(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (acceso a memoria inválida)";
        case SIGABRT: return "SIGABRT (abort: una comprobación interna falló)";
        case SIGBUS: return "SIGBUS (acceso mal alineado o archivo mapeado que se corto)";
        case SIGILL: return "SIGILL (instrucción inválida)";
        case SIGFPE: return "SIGFPE (division por cero u overflow)";
        case SIGQUIT: return "SIGQUIT (el launcher pidió las pilas: dejó de latir)";
        default: return "señal desconocida";
    }
}

const char* base(const char* ruta) {
    const char* b = std::strrchr(ruta, '/');
    return b != nullptr ? b + 1 : ruta;
}

/// «#03 0x… libgstbasuperres.dylib+0x1b4c8 simbolo+0x24». `dladdr` es lo mismo
/// que usaba `backtrace_symbols_fd` (el riesgo no cambia): sin memoria nueva.
void marco(Esc& e, int i, std::uintptr_t dir) {
    e.s("#").dec(i < 10 ? 0 : i / 10).dec(i % 10).s(" ").hex(dir);
    Dl_info di{};
    if (dir != 0 && dladdr(reinterpret_cast<void*>(dir), &di) != 0 && di.dli_fname != nullptr) {
        e.s(" ").s(base(di.dli_fname)).s("+").hex(dir - reinterpret_cast<std::uintptr_t>(di.dli_fbase));
        if (di.dli_sname != nullptr)
            e.s(" ").s(di.dli_sname).s("+").hex(dir - reinterpret_cast<std::uintptr_t>(di.dli_saddr));
    } else {
        e.s(" ?");
    }
    e.s("\n");
}

void pila(Esc& e, void* const* marcos, int n) {
    for (int i = 0; i < n; ++i) marco(e, i, reinterpret_cast<std::uintptr_t>(marcos[i]));
}

/// Dónde estaba el hilo que se cayó (el contador de programa del contexto).
std::uintptr_t pcDe(void* ctx) {
    if (ctx == nullptr) return 0;
    auto* uc = static_cast<ucontext_t*>(ctx);
#if defined(__APPLE__) && defined(__aarch64__)
    return static_cast<std::uintptr_t>(arm_thread_state64_get_pc(uc->uc_mcontext->__ss));
#elif defined(__APPLE__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(uc->uc_mcontext->__ss.__rip);
#elif defined(__linux__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__aarch64__)
    return static_cast<std::uintptr_t>(uc->uc_mcontext.pc);
#else
    (void)uc;
    return 0;
#endif
}

// --- los demás hilos ------------------------------------------------------------------------

#if defined(__APPLE__)

// macOS: se los congela con Mach y se recorre la cadena de punteros de marco
// (arm64 y x86_64 la tienen siempre). Leer con vm_read_overwrite no se cae si
// el puntero está roto: devuelve error.
bool leer(std::uintptr_t dir, void* dest, std::size_t n) {
    vm_size_t leido = 0;
    return vm_read_overwrite(mach_task_self(), dir, n, reinterpret_cast<vm_address_t>(dest), &leido) == KERN_SUCCESS &&
           leido == n;
}

void nombreYId(thread_t th, char* nombre, std::size_t n, std::uint64_t* id) {
    nombre[0] = 0;
    thread_extended_info_data_t ei{};
    mach_msg_type_number_t c = THREAD_EXTENDED_INFO_COUNT;
    if (thread_info(th, THREAD_EXTENDED_INFO, reinterpret_cast<thread_info_t>(&ei), &c) == KERN_SUCCESS) {
        std::strncpy(nombre, ei.pth_name, n - 1);
        nombre[n - 1] = 0;
    }
    thread_identifier_info_data_t ii{};
    c = THREAD_IDENTIFIER_INFO_COUNT;
    *id = thread_info(th, THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&ii), &c) == KERN_SUCCESS ? ii.thread_id
                                                                                                            : th;
}

void cabeceraHilo(Esc& e, const char* que, std::uint64_t id, const char* nombre) {
    e.s("== ").s(que).s(" ").dec(static_cast<long long>(id)).s(" ").s(nombre).s("\n");
}

void hilosRestantes(Esc& e) {
    thread_act_array_t hilos = nullptr;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &hilos, &n) != KERN_SUCCESS) return;
    const thread_t yo = mach_thread_self();
    for (mach_msg_type_number_t i = 0; i < n && i < kMaxHilos; ++i) {
        const thread_t th = hilos[i];
        if (th == yo) continue;
        char nombre[64];
        std::uint64_t id = 0;
        nombreYId(th, nombre, sizeof nombre, &id);
        cabeceraHilo(e, "hilo", id, nombre);
        e.vaciar(); // la cabecera escrita antes de congelarlo (el que escribe no espera a nadie)
        // Congelado SOLO mientras se le lee la pila; los nombres (dladdr) se buscan
        // después, ya despierto: si tenía el candado del cargador, lo suelta.
        thread_suspend(th);
        std::uintptr_t marcos[kMarcos];
        int k = 0;
        std::uintptr_t pc = 0, fp = 0, lr = 0;
        bool ok = false;
#if defined(__aarch64__)
        arm_thread_state64_t st{};
        mach_msg_type_number_t c = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(th, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&st), &c) == KERN_SUCCESS) {
            ok = true;
            pc = static_cast<std::uintptr_t>(arm_thread_state64_get_pc(st));
            fp = static_cast<std::uintptr_t>(arm_thread_state64_get_fp(st));
            // En una función hoja el retorno todavía está en lr (no lo guardó en la pila).
            lr = static_cast<std::uintptr_t>(arm_thread_state64_get_lr(st));
        }
#else
        x86_thread_state64_t st{};
        mach_msg_type_number_t c = x86_THREAD_STATE64_COUNT;
        if (thread_get_state(th, x86_THREAD_STATE64, reinterpret_cast<thread_state_t>(&st), &c) == KERN_SUCCESS) {
            ok = true;
            pc = static_cast<std::uintptr_t>(st.__rip);
            fp = static_cast<std::uintptr_t>(st.__rbp);
        }
#endif
        if (ok) {
            // pc, después lr (si el hilo está en una función hoja, como casi todos los que
            // esperan en el núcleo, su llamador está ahí y no en la cadena), y la cadena.
            constexpr std::uintptr_t kSinFirma = 0x0000FFFFFFFFFFFFULL; // sin la firma del puntero (PAC)
            marcos[k++] = pc;
            std::uintptr_t par[2] = {0, 0}; // [marco anterior, retorno]
            bool hay = fp != 0 && (fp & 0x7) == 0 && leer(fp, par, sizeof par);
            if (lr != 0 && (!hay || (par[1] & kSinFirma) != (lr & kSinFirma))) marcos[k++] = lr & kSinFirma;
            while (hay && k < kMarcos && par[1] != 0) {
                marcos[k++] = par[1] & kSinFirma;
                if (par[0] <= fp || (par[0] & 0x7) != 0) break; // la pila crece hacia abajo: lo demás es basura
                fp = par[0];
                hay = leer(fp, par, sizeof par);
            }
        }
        thread_resume(th);
        for (int m = 0; m < k; ++m) marco(e, m, marcos[m]);
    }
    // Los puertos y la lista se devuelven: la captura en vivo (capturarPilas) se
    // repite con el proceso andando. Son llamadas al núcleo: también valen cayéndose.
    for (mach_msg_type_number_t i = 0; i < n; ++i) mach_port_deallocate(mach_task_self(), hilos[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(hilos), n * sizeof(thread_t));
    mach_port_deallocate(mach_task_self(), yo);
}

/// Las imágenes cargadas con su UUID (lo que pide atos con los .dSYM del almacén).
void imagenes(Esc& e) {
    e.s("== imagenes\n");
    const std::uint32_t n = _dyld_image_count();
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto* h = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        const char* nombre = _dyld_get_image_name(i);
        if (h == nullptr || nombre == nullptr) continue;
        e.s("img ").hex(reinterpret_cast<std::uintptr_t>(h)).s(" ");
        const auto* lc = reinterpret_cast<const load_command*>(h + 1);
        bool uuid = false;
        for (std::uint32_t k = 0; k < h->ncmds && !uuid; ++k) {
            if (lc->cmd == LC_UUID) {
                const auto* u = reinterpret_cast<const uuid_command*>(lc);
                for (int b = 0; b < 16; ++b) {
                    const char d[3] = {"0123456789ABCDEF"[u->uuid[b] >> 4], "0123456789ABCDEF"[u->uuid[b] & 0xF], 0};
                    e.s(d);
                    if (b == 3 || b == 5 || b == 7 || b == 9) e.s("-");
                }
                uuid = true;
            }
            lc = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(lc) + lc->cmdsize);
        }
        if (!uuid) e.s("-");
        e.s(" ").s(nombre).s("\n");
    }
}

void nombreYIdPropio(char* nombre, std::size_t n, std::uint64_t* id) { nombreYId(mach_thread_self(), nombre, n, id); }

#else // Linux

// Linux: no hay cómo leerle los registros a otro hilo sin ptrace. Se le manda
// una señal propia a cada uno (tgkill) y su manejador anota su pila en un lugar
// fijo; el hilo que se cae la espera un rato corto y la escribe.
int senalPila() { return SIGRTMIN + 4; }
void* g_marcosOtro[kMarcos];
std::atomic<int> g_nOtro{-1};
std::atomic<long> g_pedidoA{0};

void alPedirPila(int) {
    if (g_pedidoA.load() != syscall(SYS_gettid)) return;
    const int n = backtrace(g_marcosOtro, kMarcos);
    g_nOtro.store(n);
}

void nombreDe(long tid, char* nombre, std::size_t n) {
    nombre[0] = 0;
    char ruta[64] = "/proc/self/task/";
    std::size_t p = std::strlen(ruta);
    char dig[24];
    int k = 0;
    do {
        dig[k++] = static_cast<char>('0' + tid % 10);
        tid /= 10;
    } while (tid > 0 && k < 20);
    while (k > 0) ruta[p++] = dig[--k];
    std::memcpy(ruta + p, "/comm", 6);
    const int f = open(ruta, O_RDONLY | O_CLOEXEC);
    if (f < 0) return;
    const ssize_t r = read(f, nombre, n - 1);
    close(f);
    nombre[r > 0 ? r : 0] = 0;
    if (char* nl = std::strchr(nombre, '\n')) *nl = 0;
}

void cabeceraHilo(Esc& e, const char* que, long id, const char* nombre) {
    e.s("== ").s(que).s(" ").dec(id).s(" ").s(nombre).s("\n");
}

struct linux_dirent64 {
    std::uint64_t d_ino;
    std::int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

void hilosRestantes(Esc& e) {
    const int dir = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return;
    const long yo = syscall(SYS_gettid);
    const pid_t pid = getpid();
    alignas(8) char buf[4096];
    int hechos = 0;
    for (;;) {
        const long n = syscall(SYS_getdents64, dir, buf, sizeof buf);
        if (n <= 0) break;
        for (long off = 0; off < n && hechos < kMaxHilos;) {
            const auto* d = reinterpret_cast<const linux_dirent64*>(buf + off);
            off += d->d_reclen;
            if (d->d_name[0] < '0' || d->d_name[0] > '9') continue;
            long tid = 0; // a mano: sin locale ni memoria
            for (const char* c = d->d_name; *c >= '0' && *c <= '9'; ++c) tid = tid * 10 + (*c - '0');
            if (tid == yo) continue;
            ++hechos;
            char nombre[32];
            nombreDe(tid, nombre, sizeof nombre);
            cabeceraHilo(e, "hilo", tid, nombre);
            g_nOtro.store(-1);
            g_pedidoA.store(tid);
            if (syscall(SYS_tgkill, pid, tid, senalPila()) != 0) continue;
            // Hasta 300 ms: un hilo con la señal bloqueada no contesta y no frena a los demás.
            for (int i = 0; i < 300 && g_nOtro.load() < 0; ++i) {
                const timespec ms{0, 1000000};
                nanosleep(&ms, nullptr);
            }
            const int m = g_nOtro.load();
            g_pedidoA.store(0);
            if (m < 0) {
                e.s("(no contesto)\n");
                continue;
            }
            pila(e, g_marcosOtro, m);
        }
    }
    close(dir);
}

int unaImagen(dl_phdr_info* info, std::size_t, void* datos) {
    Esc& e = *static_cast<Esc*>(datos);
    e.s("img ").hex(static_cast<std::uintptr_t>(info->dlpi_addr)).s(" ");
    bool id = false;
    // El build-id (lo que usa tools/caidas/simbolos-linux.sh para guardar el .debug).
    for (int i = 0; i < info->dlpi_phnum && !id; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) continue;
        const char* p = reinterpret_cast<const char*>(info->dlpi_addr + ph.p_vaddr);
        const char* fin = p + ph.p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= fin) {
            const auto* nh = reinterpret_cast<const ElfW(Nhdr)*>(p);
            const char* nombre = p + sizeof(ElfW(Nhdr));
            const auto* desc = reinterpret_cast<const unsigned char*>(nombre + ((nh->n_namesz + 3) & ~3U));
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && std::memcmp(nombre, "GNU", 4) == 0) {
                for (unsigned k = 0; k < nh->n_descsz; ++k) {
                    const char d[3] = {"0123456789abcdef"[desc[k] >> 4], "0123456789abcdef"[desc[k] & 0xF], 0};
                    e.s(d);
                }
                id = true;
                break;
            }
            p = reinterpret_cast<const char*>(desc) + ((nh->n_descsz + 3) & ~3U);
        }
    }
    if (!id) e.s("-");
    char exe[512];
    const char* nombre = info->dlpi_name;
    if (nombre == nullptr || nombre[0] == 0) { // el ejecutable
        const ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
        exe[r > 0 ? r : 0] = 0;
        nombre = exe;
    }
    e.s(" ").s(nombre).s("\n");
    return 0;
}

/// Las imágenes con su build-id. Va AL FINAL: `dl_iterate_phdr` toma el candado
/// del cargador, y si se cayó cargando una biblioteca, lo de arriba ya quedó escrito.
void imagenes(Esc& e) {
    e.s("== imagenes\n");
    e.vaciar();
    dl_iterate_phdr(unaImagen, &e);
}

void nombreYIdPropio(char* nombre, std::size_t n, std::uint64_t* id) {
    nombre[0] = 0;
    char t[17] = {};
    prctl(PR_GET_NAME, t, 0, 0, 0);
    std::strncpy(nombre, t, n - 1);
    nombre[n - 1] = 0;
    *id = static_cast<std::uint64_t>(syscall(SYS_gettid));
}

#endif

// --- el manejador -----------------------------------------------------------------------------

void manejar(int sig, siginfo_t* si, void* ctx) {
    int nadie = 0;
    if (!g_cayendo.compare_exchange_strong(nadie, 1)) {
        // Otro hilo ya lo está contando: que termine él (el proceso muere con su señal).
        for (;;) pause();
    }
    const std::uintptr_t pc = pcDe(ctx);
    char nombre[64];
    std::uint64_t id = 0;
    nombreYIdPropio(nombre, sizeof nombre, &id);
    // La pila de verdad empieza donde se cayó (pc): los dos primeros marcos de
    // `backtrace` son este manejador y el trampolín de la señal, que no son de
    // nadie (y confundirían la atribución: fault::atribuir).
    void* crudos[kMarcos];
    const int nc = backtrace(crudos, kMarcos);
    void* marcos[kMarcos];
    int n = 0;
    if (pc != 0) marcos[n++] = reinterpret_cast<void*>(pc);
    for (int i = nc > 2 ? 2 : nc; i < nc && n < kMarcos; ++i)
        if (!(n == 1 && reinterpret_cast<std::uintptr_t>(crudos[i]) == pc)) marcos[n++] = crudos[i];

    // 1) Al registro, como siempre: qué fue y la pila del que se cayó.
    {
        Esc e(g_fd);
        e.s("\n==== SE CAYO ").s(g_quien).s(": ").s(nombreDeSenal(sig)).s(" ====\n");
        e.s("hilo ").dec(static_cast<long long>(id)).s(" ").s(nombre).s(":\n");
        pila(e, marcos, n);
        e.s(g_fdPilas >= 0 ? "==== fin de la pila (las de todos los hilos, en Logs/dumps) ====\n"
                           : "==== fin de la pila ====\n");
    }
    // 2) Las de todos los hilos, con su nombre, para el launcher (Volcados) y
    //    tools/caidas/analizar.py. Sin launcher, al mismo registro.
    {
        Esc e(g_fdPilas >= 0 ? g_fdPilas : g_fd);
        e.s("BA-PILAS 1\n");
        e.s("proceso ").s(g_quien).s("\n");
        e.s("pid ").dec(getpid()).s("\n");
        e.s("senal ").dec(sig).s(" ").s(nombreDeSenal(sig)).s("\n");
        e.s("direccion ").hex(si != nullptr ? reinterpret_cast<std::uintptr_t>(si->si_addr) : 0).s("\n");
        e.s("pc ").hex(pc).s("\n");
        e.s("== cayo ").dec(static_cast<long long>(id)).s(" ").s(nombre).s("\n");
        pila(e, marcos, n);
        e.vaciar();
        hilosRestantes(e);
        e.vaciar();
        imagenes(e);
        e.s("FIN\n");
    }
    if (g_fdPilas >= 0) fsync(g_fdPilas);

    // Se muere como se habría muerto: mismo código de salida, mismo
    // comportamiento del launcher. Esto solo deja la prueba.
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

void instalarManejadorDeCaidas(const char* quien) {
    static bool instalado = false;
    if (instalado) return;
    instalado = true;
    if (quien != nullptr) g_quien = quien;
    g_fd = static_cast<int>(salidaOriginal());

    // El archivo para las pilas lo eligió el launcher (Volcados): se abre ahora,
    // con todo sano, y queda abierto. Que no lo hereden los procesos que lance.
    if (const char* ruta = std::getenv(caida::kVariable); ruta != nullptr && ruta[0] != 0) {
        g_fdPilas = open(ruta, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        unsetenv(caida::kVariable);
    }

    // Se llama UNA VEZ ahora, con todo sano: la primera llamada a `backtrace`
    // carga libgcc y eso sí asigna memoria. Haciéndolo acá, el manejador ya la
    // encuentra cargada y no tiene que asignar nada. Lo mismo `dladdr`.
    void* calentar[4];
    const int n = backtrace(calentar, 4);
    Dl_info di{};
    if (n > 0) dladdr(calentar[0], &di);

    // Una pila aparte para el manejador (la del hilo principal): un desborde de
    // pila también deja rastro.
    stack_t alt{};
    alt.ss_sp = g_pilaAlterna;
    alt.ss_size = sizeof g_pilaAlterna;
    sigaltstack(&alt, nullptr);

    struct sigaction sa {};
    sa.sa_sigaction = manejar;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    // SIGQUIT: el launcher pide las pilas de un proceso que dejó de latir (y después lo termina).
    for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGQUIT}) sigaction(sig, &sa, nullptr);
#if !defined(__APPLE__)
    struct sigaction sp {};
    sp.sa_handler = alPedirPila;
    sp.sa_flags = SA_RESTART;
    sigemptyset(&sp.sa_mask);
    sigaction(senalPila(), &sp, nullptr);
#endif
}

std::uint64_t idDelHilo() {
#if defined(__APPLE__)
    std::uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return id;
#else
    return static_cast<std::uint64_t>(syscall(SYS_gettid));
#endif
}

CapturaPilas capturarPilas(const std::filesystem::path& dir, const std::string& base, const std::string& motivo,
                           std::uint64_t hiloInterfaz, const std::string& extraJson) {
    // De a una: en Linux el pedido a cada hilo usa lugares fijos (g_pedidoA, g_marcosOtro).
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    CapturaPilas r;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#if !defined(__APPLE__)
    // Sin el manejador instalado (un test, una herramienta), la señal propia mataría
    // al hilo que la recibe: se instala el que anota la pila.
    static const bool listo = []() {
        struct sigaction actual {};
        sigaction(senalPila(), nullptr, &actual);
        if (actual.sa_handler != alPedirPila) {
            struct sigaction sp {};
            sp.sa_handler = alPedirPila;
            sp.sa_flags = SA_RESTART;
            sigemptyset(&sp.sa_mask);
            sigaction(senalPila(), &sp, nullptr);
        }
        void* calentar[4];
        backtrace(calentar, 4); // la primera carga libgcc
        return true;
    }();
    (void)listo;
#endif
    const std::filesystem::path pila = dir / (base + ".pila");
    const int fd = open(pila.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        r.error = std::string("no se pudo abrir ") + pila.string() + ": " + std::strerror(errno);
        return r;
    }
    {
        Esc e(fd);
        e.s("BA-PILAS 1\n");
        e.s("proceso ").s(g_quien).s("\n");
        e.s("pid ").dec(getpid()).s("\n");
        e.s("senal 0 captura en vivo (el proceso sigue)\n");
        e.s("motivo ").s(motivo.c_str()).s("\n");
        e.s("interfaz ").dec(static_cast<long long>(hiloInterfaz)).s("\n");
        e.vaciar();
        hilosRestantes(e); // todos menos este (el vigía)
        e.vaciar();
        imagenes(e);
        e.s("FIN\n");
    }
    close(fd);
    r.archivo = pila;
    r.json = dir / (base + ".json");
    captura::escribirJson(r.json,
                          {{"componente", g_quien},
                           {"pid", getpid()},
                           {"tipo", "traba"},
                           {"motivo", motivo},
                           {"hora", captura::horaLocal("%Y-%m-%dT%H:%M:%S")},
                           {"volcado", pila.filename().string()},
                           {"hilo_interfaz", hiloInterfaz}},
                          extraJson);
    return r;
}

} // namespace ba::diag


#else // _WIN32

#include "diag/CaidaCompartida.hpp"
#include "platform/Hilo.hpp"
#include "platform/Paths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdlib.h>

namespace ba::diag {
namespace {

// Todo lo que el manejador usa se prepara al instalar: cayéndose no se pide
// memoria ni se carga nada (con el heap roto, eso puede no volver).
const char* g_quien = "proceso";
caida::Pedido* g_pedido = nullptr;
HANDLE g_pide = nullptr, g_hecho = nullptr;
wchar_t g_dirVolcados[MAX_PATH] = {};
using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpFn g_volcar = nullptr;
std::atomic<DWORD> g_hiloCayendo{0};

void escribir(const char* s) {
    DWORD w = 0;
    WriteFile(reinterpret_cast<HANDLE>(salidaOriginal()), s, static_cast<DWORD>(std::strlen(s)), &w, nullptr);
}

void moduloDe(const void* dir, char* nombre, std::size_t n, unsigned long long* desp) {
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(dir), &m)) {
        char ruta[MAX_PATH] = {};
        GetModuleFileNameA(m, ruta, MAX_PATH);
        const char* b = std::strrchr(ruta, '\\');
        strncpy_s(nombre, n, b != nullptr ? b + 1 : ruta, _TRUNCATE);
        *desp = reinterpret_cast<std::uintptr_t>(dir) - reinterpret_cast<std::uintptr_t>(m);
    } else {
        strncpy_s(nombre, n, "?", _TRUNCATE);
        *desp = reinterpret_cast<std::uintptr_t>(dir);
    }
}

void volcarmeSolo(EXCEPTION_POINTERS* ep) {
    if (g_volcar == nullptr || g_dirVolcados[0] == 0) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ruta[MAX_PATH + 96];
    swprintf_s(ruta, L"%s\\%S-%04u%02u%02u-%02u%02u%02u-caida.dmp", g_dirVolcados, g_quien, t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(ruta, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    const BOOL ok = g_volcar(GetCurrentProcess(), GetCurrentProcessId(), f, static_cast<MINIDUMP_TYPE>(caida::kTipoVolcado),
                             &mei, nullptr, nullptr);
    CloseHandle(f);
    escribir(ok ? "(volcado propio en Logs/dumps)\n" : "(no se pudo escribir el volcado)\n");
}

void reportar(EXCEPTION_POINTERS* ep, const char* tipo) {
    const DWORD yo = GetCurrentThreadId();
    DWORD nadie = 0;
    if (!g_hiloCayendo.compare_exchange_strong(nadie, yo)) {
        if (nadie == yo) return; // el mismo hilo otra vez (abort después de reportar): ya está
        Sleep(60000);            // otro hilo ya lo está reportando: que termine él
        return;
    }
    const DWORD codigo = ep->ExceptionRecord->ExceptionCode;
    void* dir = ep->ExceptionRecord->ExceptionAddress;
    char mod[160];
    unsigned long long desp = 0;
    moduloDe(dir, mod, sizeof mod, &desp);
    char linea[640];
    std::snprintf(linea, sizeof linea, "\n==== SE CAYO %s: %s (%s, 0x%08lX) en %s+0x%llX, hilo %lu ====\n", g_quien, tipo,
                  caida::nombreCodigo(codigo), codigo, mod, desp, yo);
    escribir(linea);
    bool hecho = false;
    if (g_pedido != nullptr && g_pide != nullptr && g_hecho != nullptr) {
        g_pedido->hilo = yo;
        g_pedido->codigo = codigo;
        g_pedido->punteros = reinterpret_cast<std::uint64_t>(ep);
        g_pedido->direccion = reinterpret_cast<std::uint64_t>(dir);
        strncpy_s(g_pedido->tipo, sizeof g_pedido->tipo, tipo, _TRUNCATE);
        g_pedido->version = caida::kVersion;
        ResetEvent(g_hecho); // pudo quedar prendido de una captura en vivo (capturarPilas)
        MemoryBarrier();
        g_pedido->magic = caida::kMagic;
        SetEvent(g_pide);
        hecho = WaitForSingleObject(g_hecho, 60000) == WAIT_OBJECT_0;
        escribir(hecho ? "(el launcher escribió el volcado en Logs/dumps)\n" : "(el launcher no respondió: se vuelca solo)\n");
    }
    if (!hecho) volcarmeSolo(ep);
}

void reportarAca(const char* tipo) {
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    EXCEPTION_RECORD er{};
    er.ExceptionCode = caida::kCodigoAbort;
    er.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
    EXCEPTION_POINTERS ep{&er, &ctx};
    reportar(&ep, tipo);
}

// La corrupción del heap no llega al filtro de «sin atrapar» (Windows la
// manda directo a WER): se la ve antes, como excepción vectorizada.
LONG WINAPI vectorizada(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == 0xC0000374) reportar(ep, "corrupción del heap");
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI sinAtrapar(EXCEPTION_POINTERS* ep) {
    reportar(ep, "excepción sin atrapar");
    return EXCEPTION_CONTINUE_SEARCH; // y sigue como siempre (WER, código de salida)
}

void alAbortar(int) { reportarAca("abort"); }

void llamadaPura() {
    reportarAca("llamada a una función virtual pura");
    std::abort();
}

void parametroInvalido(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {
    reportarAca("parámetro inválido a la biblioteca de C");
    std::abort();
}

} // namespace

void instalarManejadorDeCaidas(const char* quien) {
    static bool instalado = false;
    if (instalado) return;
    instalado = true;
    if (quien != nullptr) g_quien = quien;

    // El canal que dejó el launcher (si no hay launcher, no está).
    wchar_t canal[256] = {};
    wchar_t var[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, caida::kVariable, -1, var, 64);
    if (GetEnvironmentVariableW(var, canal, 256) > 0) {
        if (HANDLE m = OpenFileMappingW(FILE_MAP_WRITE, FALSE, canal)) {
            g_pedido = static_cast<caida::Pedido*>(MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, sizeof(caida::Pedido)));
            g_pide = OpenEventW(EVENT_MODIFY_STATE, FALSE, caida::nombrePide(canal).c_str());
            // Modificarlo también: antes de pedir una captura en vivo (capturarPilas) se apaga.
            g_hecho = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, caida::nombreHecho(canal).c_str());
        }
        // Que no lo hereden los procesos que lance la app (plugins, herramientas).
        SetEnvironmentVariableW(var, nullptr);
    }
    // Para volcarse solo: dbghelp (del sistema) cargada ya, y la carpeta.
    if (HMODULE d = LoadLibraryW(L"dbghelp.dll"))
        g_volcar = reinterpret_cast<MiniDumpFn>(reinterpret_cast<void*>(GetProcAddress(d, "MiniDumpWriteDump")));
    std::error_code ec;
    const std::filesystem::path dir = ba::platform::dataRoot() / "Logs" / "dumps";
    std::filesystem::create_directories(dir, ec);
    wcsncpy_s(g_dirVolcados, dir.wstring().c_str(), _TRUNCATE);

    AddVectoredExceptionHandler(1, vectorizada);
    SetUnhandledExceptionFilter(sinAtrapar);
    signal(SIGABRT, alAbortar);
    _set_abort_behavior(0, _WRITE_ABORT_MSG); // sin el cuadro de diálogo; el informe de fallas sigue
    _set_purecall_handler(llamadaPura);
    _set_invalid_parameter_handler(parametroInvalido);
}

std::uint64_t idDelHilo() { return GetCurrentThreadId(); }

namespace {
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 1 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring ancho(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
} // namespace

CapturaPilas capturarPilas(const std::filesystem::path& dir, const std::string& base, const std::string& motivo,
                           std::uint64_t hiloInterfaz, const std::string& extraJson) {
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    CapturaPilas r;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // 1) Con launcher: que lo escriba él, desde afuera (como el de un colgado, sin matar a nadie).
    if (g_pedido != nullptr && g_pide != nullptr && g_hecho != nullptr && g_hiloCayendo.load() == 0) {
        caida::Pedido& p = *g_pedido;
        p.magic = 0;
        p.hilo = static_cast<std::uint32_t>(hiloInterfaz);
        p.codigo = caida::kCodigoTraba;
        p.punteros = 0;
        p.direccion = 0;
        strncpy_s(p.tipo, sizeof p.tipo, motivo.c_str(), _TRUNCATE);
        strncpy_s(p.base, sizeof p.base, base.c_str(), _TRUNCATE);
        strncpy_s(p.extra, sizeof p.extra, extraJson.size() < sizeof p.extra ? extraJson.c_str() : "{}", _TRUNCATE);
        p.respuesta[0] = 0;
        p.version = caida::kVersion;
        ResetEvent(g_hecho);
        MemoryBarrier();
        p.magic = caida::kMagic;
        SetEvent(g_pide);
        if (WaitForSingleObject(g_hecho, 20000) != WAIT_OBJECT_0) {
            r.error = "el launcher no contestó en 20 s";
            return r;
        }
        MemoryBarrier();
        const std::string resp(p.respuesta, strnlen(p.respuesta, sizeof p.respuesta));
        if (resp.empty() || resp.rfind("error:", 0) == 0) {
            r.error = resp.empty() ? std::string("el launcher no escribió el volcado") : resp.substr(6);
            return r;
        }
        r.archivo = std::filesystem::path(ancho(resp));
        r.json = std::filesystem::path(r.archivo).replace_extension(".json");
        r.porLauncher = true;
        return r;
    }
    // 2) Sin launcher (la app suelta): se vuelca solo, desde este hilo (el vigía).
    if (g_volcar == nullptr)
        if (HMODULE d = LoadLibraryW(L"dbghelp.dll"))
            g_volcar = reinterpret_cast<MiniDumpFn>(reinterpret_cast<void*>(GetProcAddress(d, "MiniDumpWriteDump")));
    if (g_volcar == nullptr) {
        r.error = "sin dbghelp";
        return r;
    }
    const std::filesystem::path dmp = dir / (base + ".dmp");
    HANDLE f = CreateFileW(dmp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        r.error = "no se pudo crear " + utf8(dmp.wstring());
        return r;
    }
    const auto t0 = GetTickCount64();
    const BOOL ok = g_volcar(GetCurrentProcess(), GetCurrentProcessId(), f, static_cast<MINIDUMP_TYPE>(caida::kTipoVolcadoTraba),
                             nullptr, nullptr, nullptr);
    const auto ms = GetTickCount64() - t0;
    CloseHandle(f);
    if (!ok) {
        r.error = "MiniDumpWriteDump falló (" + std::to_string(GetLastError()) + ")";
        std::filesystem::remove(dmp, ec);
        return r;
    }
    nlohmann::json hilos = nlohmann::json::array();
    for (const auto& h : ba::platform::hilosDelProceso(GetCurrentProcessId())) hilos.push_back({{"id", h.id}, {"nombre", h.nombre}});
    r.archivo = dmp;
    r.json = dir / (base + ".json");
    captura::escribirJson(r.json,
                          {{"componente", g_quien},
                           {"pid", GetCurrentProcessId()},
                           {"tipo", "traba"},
                           {"motivo", motivo},
                           {"hora", captura::horaLocal("%Y-%m-%dT%H:%M:%S")},
                           {"volcado", utf8(dmp.filename().wstring())},
                           {"volcado_ms", ms},
                           {"hilo_interfaz", hiloInterfaz},
                           {"hilos", std::move(hilos)}},
                          extraJson);
    return r;
}

} // namespace ba::diag

#endif

// --- qué hacía un hilo, en una línea (de las pilas de una captura) ------------------------------

#if !defined(_WIN32)
#include <cxxabi.h>
#endif

namespace ba::diag {
namespace {

std::string desenredar(const std::string& s) {
#if !defined(_WIN32)
    int st = 0;
    if (char* d = abi::__cxa_demangle(s.c_str(), nullptr, nullptr, &st); d != nullptr && st == 0) {
        std::string out = d;
        std::free(d);
        return out;
    }
#endif
    return s;
}

bool contiene(const std::string& s, const char* que) { return s.find(que) != std::string::npos; }

bool delSistema(const std::string& m) {
    return m.rfind("libsystem_", 0) == 0 || m.rfind("libc.", 0) == 0 || m.rfind("libc-", 0) == 0 ||
           m.rfind("libpthread", 0) == 0 || m.rfind("ld-linux", 0) == 0 || m == "dyld" || m == "libdyld.dylib" ||
           m == "?" || m.rfind("libobjc", 0) == 0;
}

bool propio(const std::string& modulo, const std::string& simbolo) {
    return simbolo.rfind("ba::", 0) == 0 || contiene(simbolo, " ba::") || contiene(simbolo, "(ba::") ||
           modulo.rfind("ba-", 0) == 0 || modulo.rfind("libba", 0) == 0 || modulo.rfind("BroadcastAppliance", 0) == 0;
}

/// La función sin argumentos (lo que va antes del primer «(»), para que la línea sea corta.
std::string corto(const std::string& simbolo) {
    const std::size_t p = simbolo.find('(');
    return p == std::string::npos ? simbolo : simbolo.substr(0, p);
}

} // namespace

std::string resumirPila(const std::string& texto, std::uint64_t id) {
    struct Marco {
        std::string modulo, simbolo;
    };
    std::vector<Marco> marcos;
    bool enHilo = false, hallado = false;
    std::istringstream in(texto);
    std::string l;
    const std::string buscado = std::to_string(id);
    while (std::getline(in, l)) {
        if (l.rfind("== ", 0) == 0) {
            if (enHilo) break; // el que se buscaba terminó
            // «== hilo <id> <nombre>» o «== cayo <id> <nombre>»
            std::istringstream c(l.substr(3));
            std::string que, num;
            c >> que >> num;
            enHilo = (que == "hilo" || que == "cayo") && num == buscado;
            hallado = hallado || enHilo;
            continue;
        }
        if (!enHilo || l.empty() || l[0] != '#') continue;
        // «#03 0x1 modulo+0x2 simbolo+0x3»
        std::istringstream m(l);
        std::string num, dir, mod, sim;
        m >> num >> dir >> mod >> sim;
        Marco mc;
        mc.modulo = mod.substr(0, mod.rfind('+'));
        mc.simbolo = desenredar(sim.substr(0, sim.rfind('+')));
        marcos.push_back(std::move(mc));
    }
    if (!hallado) return {};
    if (marcos.empty()) return "sin pila (no contestó)";
    // Lo que importa es lo de ARRIBA del bucle de eventos (main, exec y el despacho
    // de abajo están siempre). Dos «QEventLoop::exec»: un bucle anidado (un diálogo).
    std::size_t bucle = marcos.size();
    int bucles = 0;
    for (std::size_t i = 0; i < marcos.size(); ++i) {
        const std::string& s = marcos[i].simbolo;
        const bool esBucle = contiene(s, "QEventLoop::exec") || contiene(s, "QEventLoop::processEvents") ||
                             contiene(s, "CFRunLoopRun") || contiene(s, "QEventDispatcher");
        if (contiene(s, "QEventLoop::exec")) ++bucles;
        if (esBucle && bucle == marcos.size()) bucle = i;
    }
    if (bucle == 0) return "en el bucle de eventos (sin nada de Qt ni propio arriba: ¿el sistema?) — " + marcos.front().modulo;
    bool render = false, espera = false, modal = bucles >= 2;
    for (std::size_t i = 0; i < bucle; ++i) {
        const std::string& s = marcos[i].simbolo;
        render = render || contiene(s, "QSGThreadedRenderLoop") || contiene(s, "QSGRenderThread") ||
                 contiene(s, "QSGRenderLoop") || contiene(s, "QSGGuiThreadRenderLoop") ||
                 contiene(s, "QQuickWindowPrivate::polishItems") || contiene(s, "QQuickWindowPrivate::sync") ||
                 contiene(s, "QQuickWindowPrivate::renderSceneGraph") || contiene(s, "QRhi");
        espera = espera || contiene(s, "QWaitCondition") || contiene(s, "cond_wait") || contiene(s, "cvwait") ||
                 contiene(s, "futex") || contiene(s, "semaphore_wait");
        modal = modal || contiene(s, "runModal") || contiene(s, "NSMenu") || contiene(s, "trackMouse") ||
                contiene(s, "performWindowDrag") || contiene(s, "_NSTrack") || contiene(s, "QDialog::exec");
    }
    // La de más arriba que no es del sistema, y la primera propia (arriba del bucle).
    const Marco* arriba = nullptr;
    const Marco* nuestra = nullptr;
    for (std::size_t i = 0; i < bucle; ++i) {
        const Marco& m = marcos[i];
        if (arriba == nullptr && !delSistema(m.modulo)) arriba = &m;
        if (nuestra == nullptr && propio(m.modulo, m.simbolo)) nuestra = &m;
    }
    std::string donde;
    if (nuestra != nullptr) donde = corto(nuestra->simbolo) + " (" + nuestra->modulo + ")";
    else if (arriba != nullptr) donde = arriba->modulo + ": " + corto(arriba->simbolo);
    else donde = marcos.front().modulo + ": " + corto(marcos.front().simbolo);
    if (render && espera) return "esperando al hilo de render de Qt (sincronización del scene graph) — " + donde;
    if (render) return "en el scene graph de Qt (polish, sync o dibujo en la interfaz) — " + donde;
    if (modal) return "en un bucle anidado o modal (diálogo, menú, arrastre) — " + donde;
    if (nuestra != nullptr) return "en código propio: " + donde;
    return "en " + donde;
}

} // namespace ba::diag
