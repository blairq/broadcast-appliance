// ============================================================================
// src/core/diag/Frontera.hpp — contadores de cada frontera entre hilos (F0)
//
// docs/planes/PLAN-GATEWAY-FRAGMENTOS.md: «costo de una frontera ≈ pases/s × (costo fijo +
// datos tocados × distancia)». Para decidir qué va vertical (mismo hilo) y qué horizontal
// (otra capa) hay que MEDIR cada frontera: cuántos pases por segundo, cuántas despertadas
// (un pase en lote despierta una vez para muchos), cuántos bytes, cuánto trabajo hay en
// cada pase y cuánto espera algo en la cola antes de que el otro hilo lo tome.
//
// Casi gratis: atómicos relajados, sin candados en el camino caliente. La medición del
// tiempo (trabajo, espera) la hace el que llama con `relojNs()` (steady_clock) solo donde
// importa. El registro es global y las fronteras no se borran nunca (las referencias que
// guarda el código valen toda la vida del proceso):
//
//     static ba::diag::Frontera& f = ba::diag::frontera("turn.entra.udp", 1, "pinggy UDP → TURN");
//     f.despertada();
//     f.lote(paquetes, bytes, trabajoNs);
//
// `fronterasJson()` da las tasas desde la lectura anterior (ventana de 1 s como mínimo; si
// se pide antes, repite la última) para gateway_estado y medir-sala.
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "json/JsonFwd.hpp"

namespace ba::diag {

class Frontera {
   public:
    Frontera(std::string id, int numero, std::string descripcion)
        : id_(std::move(id)), numero_(numero), descripcion_(std::move(descripcion)) {}
    Frontera(const Frontera&) = delete;
    Frontera& operator=(const Frontera&) = delete;

    /// El hilo que recibe se despertó para atender esta frontera (una vez por tanda).
    void despertada() { despertadas_.fetch_add(1, std::memory_order_relaxed); }
    /// Un pase: `bytes` movidos, `trabajoNs` lo que costó atenderlo, `esperaNs` lo que esperó
    /// en la cola (0 = no se mide).
    void pase(std::uint64_t bytes, std::uint64_t trabajoNs = 0, std::uint64_t esperaNs = 0) {
        lote(1, bytes, trabajoNs, esperaNs);
    }
    /// `n` pases juntos (una tanda): el trabajo es el de toda la tanda; la espera, la del más
    /// viejo de la tanda.
    void lote(std::uint64_t n, std::uint64_t bytes, std::uint64_t trabajoNs = 0, std::uint64_t esperaNs = 0) {
        if (n == 0) return;
        pases_.fetch_add(n, std::memory_order_relaxed);
        bytes_.fetch_add(bytes, std::memory_order_relaxed);
        if (trabajoNs != 0) {
            trabajoNs_.fetch_add(trabajoNs, std::memory_order_relaxed);
            trabajoN_.fetch_add(n, std::memory_order_relaxed);
            maximo(trabajoMaxNs_, trabajoNs / n);
        }
        if (esperaNs != 0) {
            esperaNs_.fetch_add(esperaNs, std::memory_order_relaxed);
            esperaN_.fetch_add(1, std::memory_order_relaxed);
            maximo(esperaMaxNs_, esperaNs);
        }
    }

    const std::string& id() const { return id_; }
    int numero() const { return numero_; }
    const std::string& descripcion() const { return descripcion_; }

    struct Lectura {
        std::uint64_t pases = 0, despertadas = 0, bytes = 0, trabajoNs = 0, trabajoN = 0, esperaNs = 0, esperaN = 0;
        std::uint64_t trabajoMaxNs = 0, esperaMaxNs = 0; ///< desde la lectura anterior (se ponen en 0 al leer)
    };
    /// Los acumulados (y los máximos, que se vuelven a 0).
    Lectura leer() {
        Lectura l;
        l.pases = pases_.load(std::memory_order_relaxed);
        l.despertadas = despertadas_.load(std::memory_order_relaxed);
        l.bytes = bytes_.load(std::memory_order_relaxed);
        l.trabajoNs = trabajoNs_.load(std::memory_order_relaxed);
        l.trabajoN = trabajoN_.load(std::memory_order_relaxed);
        l.esperaNs = esperaNs_.load(std::memory_order_relaxed);
        l.esperaN = esperaN_.load(std::memory_order_relaxed);
        l.trabajoMaxNs = trabajoMaxNs_.exchange(0, std::memory_order_relaxed);
        l.esperaMaxNs = esperaMaxNs_.exchange(0, std::memory_order_relaxed);
        return l;
    }

   private:
    static void maximo(std::atomic<std::uint64_t>& a, std::uint64_t v) {
        std::uint64_t x = a.load(std::memory_order_relaxed);
        while (v > x && !a.compare_exchange_weak(x, v, std::memory_order_relaxed)) {
        }
    }

    const std::string id_;
    const int numero_;
    const std::string descripcion_;
    std::atomic<std::uint64_t> pases_{0}, despertadas_{0}, bytes_{0}, trabajoNs_{0}, trabajoN_{0}, esperaNs_{0}, esperaN_{0};
    std::atomic<std::uint64_t> trabajoMaxNs_{0}, esperaMaxNs_{0};
};

/// La frontera `id` (la crea la primera vez; la misma referencia siempre). `numero` es el de
/// la tabla del plan (#1-#20; 0 = fuera de la tabla).
Frontera& frontera(const std::string& id, int numero = 0, const std::string& descripcion = {});

/// Reloj monótono en ns (el de las mediciones de trabajo y espera).
inline std::uint64_t relojNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

/// Mide el trabajo de un bloque: al salir del ámbito suma un pase (o `n`) con lo que tardó.
class MedirPase {
   public:
    explicit MedirPase(Frontera& f, std::uint64_t bytes = 0) : f_(f), bytes_(bytes), t0_(relojNs()) {}
    ~MedirPase() { f_.lote(n_, bytes_, relojNs() - t0_ + 1, esperaNs_); }
    void bytes(std::uint64_t b) { bytes_ = b; }
    void pases(std::uint64_t n) { n_ = n; }
    void espera(std::uint64_t ns) { esperaNs_ = ns; }
    MedirPase(const MedirPase&) = delete;
    MedirPase& operator=(const MedirPase&) = delete;

   private:
    Frontera& f_;
    std::uint64_t bytes_;
    std::uint64_t t0_;
    std::uint64_t n_ = 1;
    std::uint64_t esperaNs_ = 0;
};

/// Todas las fronteras con sus tasas desde la lectura anterior:
///   {"ventana_s": 1.0, "fronteras": [{id, numero, descripcion, pases_s, despertadas_s,
///     pases_por_despertada, kbytes_s, trabajo_us_pase, trabajo_max_us, trabajo_pct_nucleo,
///     espera_us, espera_max_us, pases}]}
/// Con menos de `ventanaMinMs` desde la anterior devuelve la misma de antes.
nlohmann::json fronterasJson(int ventanaMinMs = 1000);

} // namespace ba::diag
