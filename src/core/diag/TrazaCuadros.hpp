// ============================================================================
// src/core/diag/TrazaCuadros.hpp — cada cuadro en cada tramo, con su hora de origen (SONDA)
//
// RitmoCuadros dice cada cuánto pasa un cuadro por un punto; esto guarda CADA paso, con la
// hora en que pasó (`t`), la hora de lo que muestra (`origen`, la del contenido en el reloj
// de los sellos: la misma en todos los tramos de un cuadro) y un dato del tramo (`dato`: un
// número de cuadro, lo que tardó, cuánto esperó…). Con eso se alinean los tramos de un mismo
// cuadro (docs/local/vista-programa.md): dónde se demoró, dónde se lo tiró, quién se trabó.
//
// SOLO con la macro de las sondas (diag/SondasCuadros.hpp): se usa siempre dentro de
// BA_SONDA(…), así que en producción no se crea ninguna ni se anota nada.
//
// Un anillo fijo por tramo (kTam pasos: a 75 por segundo, ~110 s), anotar es tomar un lugar y
// escribir tres enteros (sin candados ni memoria nueva); leer es de cualquier hilo (puede ver un paso a
// medio escribir de la vuelta anterior: para diagnosticar alcanza). Los tramos tienen nombre y
// viven en un registro del proceso (`traza(nombre)`: se busca una vez y se guarda el puntero,
// nunca por cuadro).
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ba::diag {

class TrazaCuadros {
   public:
    static constexpr std::uint32_t kTam = 8192;

    /// µs del reloj monotónico (steady_clock: el mismo que HealthBlock::nowQpc / 1000).
    static std::int64_t ahoraUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Paso {
        std::int64_t t = 0, origen = 0, dato = 0;
    };

    /// De cualquier hilo (el timbre lo tocan varios): cada uno toma su lugar del anillo.
    void anotar(std::int64_t t, std::int64_t origen = 0, std::int64_t dato = 0) {
        const std::uint32_t i = pos_.fetch_add(1, std::memory_order_acq_rel);
        Celda& c = pasos_[i % kTam];
        c.t.store(t, std::memory_order_relaxed);
        c.origen.store(origen, std::memory_order_relaxed);
        c.dato.store(dato, std::memory_order_relaxed);
    }

    /// Los pasos con t >= desdeUs, el más viejo primero.
    std::vector<Paso> desde(std::int64_t desdeUs) const {
        std::vector<Paso> out;
        const std::uint32_t pos = pos_.load(std::memory_order_acquire);
        const std::uint32_t n = pos < kTam ? pos : kTam;
        out.reserve(n);
        for (std::uint32_t k = pos - n; k != pos; ++k) {
            const Celda& c = pasos_[k % kTam];
            Paso p{c.t.load(std::memory_order_relaxed), c.origen.load(std::memory_order_relaxed), c.dato.load(std::memory_order_relaxed)};
            if (p.t >= desdeUs) out.push_back(p);
        }
        return out;
    }

   private:
    struct Celda {
        std::atomic<std::int64_t> t{0}, origen{0}, dato{0};
    };
    std::array<Celda, kTam> pasos_{};
    std::atomic<std::uint32_t> pos_{0};
};

namespace detalle {
inline std::mutex& trazasMu() {
    static std::mutex m;
    return m;
}
inline std::map<std::string, std::shared_ptr<TrazaCuadros>>& trazasMapa() {
    static std::map<std::string, std::shared_ptr<TrazaCuadros>> m;
    return m;
}
} // namespace detalle

/// El tramo `nombre` (lo crea la primera vez). Guardar el puntero: no llamarla por cuadro.
inline std::shared_ptr<TrazaCuadros> traza(const std::string& nombre) {
    std::lock_guard<std::mutex> l(detalle::trazasMu());
    auto& t = detalle::trazasMapa()[nombre];
    if (!t) t = std::make_shared<TrazaCuadros>();
    return t;
}

/// Todos los tramos.
inline std::map<std::string, std::shared_ptr<TrazaCuadros>> trazas() {
    std::lock_guard<std::mutex> l(detalle::trazasMu());
    return detalle::trazasMapa();
}

} // namespace ba::diag
