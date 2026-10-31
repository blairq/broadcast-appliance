// ============================================================================
// src/core/ipc/Timbre.cpp — ver Timbre.hpp (la primitiva de cada sistema)
// ============================================================================
#include "ipc/Timbre.hpp"

#include "ipc/AtomicRef.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <climits>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <os/os_sync_wait_on_address.h>
#include <cerrno>
#elif defined(__linux__)
#include <cerrno>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ba::ipc {

namespace {

using Reloj = std::chrono::steady_clock;

AtomicRef<std::uint32_t> palabra(std::uint32_t& x) { return AtomicRef<std::uint32_t>(x); }

#if defined(__linux__)
long futex(std::uint32_t* p, int op, std::uint32_t val, const timespec* plazo) {
    return syscall(SYS_futex, p, op, val, plazo, nullptr, 0);
}
#endif

} // namespace

bool Timbre::hayPrimitiva() {
#if defined(_WIN32) || defined(__linux__)
    return true;
#elif defined(__APPLE__)
    if (__builtin_available(macOS 14.4, *)) return true;
    return false;
#else
    return false;
#endif
}

Timbre::~Timbre() { soltar(); }

Timbre::Timbre(Timbre&& o) noexcept : t_(std::exchange(o.t_, nullptr)), escritor_(o.escritor_) {
#if defined(_WIN32)
    sem_ = std::exchange(o.sem_, nullptr);
#endif
}

Timbre& Timbre::operator=(Timbre&& o) noexcept {
    if (this != &o) {
        soltar();
        t_ = std::exchange(o.t_, nullptr);
        escritor_ = o.escritor_;
#if defined(_WIN32)
        sem_ = std::exchange(o.sem_, nullptr);
#endif
    }
    return *this;
}

void Timbre::enganchar(TimbreCompartido* t, const std::wstring& canal, bool escritor) {
    soltar();
    if (t == nullptr) return;
#if defined(_WIN32)
    // El mismo nombre para los dos lados: lo crea el primero que llega.
    const std::wstring nombre = L"Local\\" + canal + L".Timbre";
    sem_ = CreateSemaphoreW(nullptr, 0, LONG_MAX, nombre.c_str());
    if (sem_ == nullptr) return; // sin semáforo, sin timbre: el lector sigue con Mirada
#else
    (void)canal;
#endif
    t_ = t;
    escritor_ = escritor;
    if (escritor && hayPrimitiva()) palabra(t_->vivo).store(1, std::memory_order_release);
}

void Timbre::soltar() {
    // El escritor que se va toca una última vez: el que duerme se entera ya (y mira si
    // el canal quedó viejo) en vez de esperar su plazo.
    if (t_ != nullptr && escritor_) tocar();
#if defined(_WIN32)
    if (sem_ != nullptr) CloseHandle(static_cast<HANDLE>(sem_));
    sem_ = nullptr;
#endif
    t_ = nullptr;
}

void Timbre::tocar() {
    if (t_ == nullptr) return;
    palabra(t_->toques).fetch_add(1, std::memory_order_seq_cst);
    // Nadie duerme (lo común con un lector que todavía trabaja el cuadro anterior): no se
    // llama al sistema.
    const std::uint32_t n = palabra(t_->durmientes).load(std::memory_order_seq_cst);
    if (n == 0) return;
#if defined(_WIN32)
    if (sem_ != nullptr) ReleaseSemaphore(static_cast<HANDLE>(sem_), static_cast<LONG>(std::min<std::uint32_t>(n, 64)), nullptr);
#elif defined(__APPLE__)
    if (__builtin_available(macOS 14.4, *))
        os_sync_wake_by_address_all(&t_->toques, sizeof(std::uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
#elif defined(__linux__)
    futex(&t_->toques, FUTEX_WAKE, INT_MAX, nullptr);
#endif
}

std::uint32_t Timbre::visto() const { return t_ != nullptr ? palabra(t_->toques).load(std::memory_order_seq_cst) : 0; }

bool Timbre::vivo() const {
    return t_ != nullptr && hayPrimitiva() && palabra(t_->vivo).load(std::memory_order_acquire) != 0;
}

bool Timbre::esperar(std::uint32_t visto, Reloj::time_point plazo) {
    if (!vivo()) {
        ba::platform::dormirHasta(plazo);
        return false;
    }
    palabra(t_->durmientes).fetch_add(1, std::memory_order_seq_cst);
    bool tocaron = false;
    for (;;) {
        if (palabra(t_->toques).load(std::memory_order_seq_cst) != visto) {
            tocaron = true;
            break;
        }
        const auto ahora = Reloj::now();
        if (ahora >= plazo) break;
        const auto faltaNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(plazo - ahora).count());
#if defined(_WIN32)
        // Redondeado para arriba: un plazo de 0,4 ms no es «no esperar».
        const DWORD ms = static_cast<DWORD>(std::min<std::uint64_t>((faltaNs + 999'999) / 1'000'000, 0xFFFFFFF0u));
        if (WaitForSingleObject(static_cast<HANDLE>(sem_), ms) == WAIT_TIMEOUT) break;
        // Un permiso: tocaron (o es uno que sobró de un vencido): se vuelve a mirar la palabra.
        if (palabra(t_->toques).load(std::memory_order_seq_cst) != visto) tocaron = true;
        break;
#elif defined(__APPLE__)
        if (__builtin_available(macOS 14.4, *)) {
            const int r = os_sync_wait_on_address_with_timeout(&t_->toques, visto, sizeof(std::uint32_t),
                                                               OS_SYNC_WAIT_ON_ADDRESS_SHARED, OS_CLOCK_MACH_ABSOLUTE_TIME,
                                                               std::max<std::uint64_t>(faltaNs, 1));
            if (r < 0 && errno == ETIMEDOUT) break;
            // Despertado, la palabra ya era otra, o EINTR/EFAULT: se vuelve a mirar.
        } else {
            ba::platform::dormirHasta(plazo);
            break;
        }
#elif defined(__linux__)
        const timespec ts{static_cast<time_t>(faltaNs / 1'000'000'000ULL), static_cast<long>(faltaNs % 1'000'000'000ULL)};
        if (futex(&t_->toques, FUTEX_WAIT, visto, &ts) != 0 && errno == ETIMEDOUT) break;
#else
        ba::platform::dormirHasta(plazo);
        break;
#endif
    }
    palabra(t_->durmientes).fetch_sub(1, std::memory_order_seq_cst);
    return tocaron;
}

} // namespace ba::ipc
