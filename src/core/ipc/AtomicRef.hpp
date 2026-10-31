// ============================================================================
// src/core/ipc/AtomicRef.hpp — std::atomic_ref portable
//
// Proposito:
//   La memoria compartida entre procesos (HealthBlock, SharedAudioRing,
//   SpoutTransport) guarda enteros PLANOS en el layout y los accede de forma
//   atomica con std::atomic_ref. Pero atomic_ref llego tarde a libc++ (LLVM
//   19): la libc++ de Chromium con la que se compila el gateway WebRTC (M123)
//   y la de llvm-mingw no la tienen.
//
//   `ba::ipc::AtomicRef<T>` es std::atomic_ref cuando la biblioteca la trae y,
//   si no, una implementacion minima sobre los builtins __atomic_* de GCC/Clang
//   (los mismos que usa la propia libc++ por dentro): misma semantica de
//   memoria, mismo codigo generado. Solo cubre lo que el IPC usa: load, store,
//   fetch_add e is_always_lock_free.
// ============================================================================
#pragma once

#include <atomic>
#include <type_traits>

namespace ba::ipc {

#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L

template <typename T> using AtomicRef = std::atomic_ref<T>;

#else

template <typename T> class AtomicRef {
    static_assert(std::is_integral_v<T>, "AtomicRef: solo enteros");

   public:
    static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), 0);

    explicit AtomicRef(T& obj) noexcept : p_(&obj) {}
    AtomicRef(const AtomicRef&) noexcept = default;
    AtomicRef& operator=(const AtomicRef&) = delete;

    T load(std::memory_order o = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(p_, order(o));
    }
    void store(T v, std::memory_order o = std::memory_order_seq_cst) const noexcept {
        __atomic_store_n(p_, v, order(o));
    }
    T fetch_add(T v, std::memory_order o = std::memory_order_seq_cst) const noexcept {
        return __atomic_fetch_add(p_, v, order(o));
    }
    T fetch_sub(T v, std::memory_order o = std::memory_order_seq_cst) const noexcept {
        return __atomic_fetch_sub(p_, v, order(o));
    }

   private:
    static constexpr int order(std::memory_order o) noexcept {
        switch (o) {
            case std::memory_order_relaxed: return __ATOMIC_RELAXED;
            case std::memory_order_consume: return __ATOMIC_CONSUME;
            case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
            case std::memory_order_release: return __ATOMIC_RELEASE;
            case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
            default: return __ATOMIC_SEQ_CST;
        }
    }
    T* p_;
};

#endif

} // namespace ba::ipc
