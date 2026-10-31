// ============================================================================
// src/core/diag/Metricas.hpp — las métricas del proceso, con nombre y tipo
//
// Hasta acá cada cosa medía a su manera (diag::Recursos, los contadores del
// transporte, el vigía del bucle, el del candado del motor) y la salud se leía
// en el registro: «¿cuántas veces se reconectó?» era contar líneas a ojo. Esto
// es el lugar común (docs/arquitectura/04-plan-unificado.md, F4-01):
//
//   - CONTADOR: sube y nunca baja (reconexiones, cuadros perdidos, órdenes);
//   - MEDIDOR: un valor de ahora (edad p99 de un anillo, colchón de grabación);
//   - HISTOGRAMA: cuánto tardó algo, en casilleros fijos (latencia de una
//     herramienta MCP, espera del candado del motor), con su p50/p99.
//
// Registrar una serie toma un candado (una vez: después se guarda la referencia,
// que vive lo que el proceso). Sumar, poner y observar NO bloquean (atómicos):
// se puede llamar desde un hilo con plazo.
//
// Lo que ya se mide en otro lado NO se mide de nuevo: un COLECTOR copia lo que
// tiene (VigiaBucle::informe, MediaPump::transporte, Recursos::ultima…) a sus
// series justo antes de exportar. Así hay un solo número por cosa.
//
// Se exporta en el formato de texto de Prometheus (`GET /metrics` de la app y
// del gateway, con su Bearer; el launcher lo escribe en Runtime/) y en JSON
// (sistema_recursos, la página de diagnóstico, las alarmas `metrica`). Los
// nombres y las etiquetas son claves: no se traducen; la ayuda sí.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ba::diag {

/// {clave, valor}… en el orden en que se dan (es parte de la identidad de la serie).
using Etiquetas = std::vector<std::pair<std::string, std::string>>;

class Contador {
   public:
    void sumar(std::uint64_t n = 1) { v_.fetch_add(n, std::memory_order_relaxed); }
    /// Copia un total que ya cuenta otro (el overrun acumulado de un anillo): el
    /// contador queda en ese valor. Uno que vuelve para atrás es un reinicio de
    /// quien contaba, y se toma igual (Prometheus entiende el reinicio).
    void igualar(std::uint64_t total) { v_.store(total, std::memory_order_relaxed); }
    std::uint64_t valor() const { return v_.load(std::memory_order_relaxed); }

   private:
    std::atomic<std::uint64_t> v_{0};
};

class Medidor {
   public:
    void poner(double v) { v_.store(v, std::memory_order_relaxed); }
    /// No se sabe (el sistema no lo dice, nada medido todavía): nunca un 0 que parezca medido.
    void desconocido();
    double valor() const { return v_.load(std::memory_order_relaxed); }
    bool conocido() const;

   private:
    std::atomic<double> v_{0};
};

class Histograma {
   public:
    /// `bordes`: los topes de cada casillero, crecientes (el último, «más», va solo).
    explicit Histograma(std::vector<double> bordes);
    void observar(double v);
    std::uint64_t cuenta() const { return cuenta_.load(std::memory_order_relaxed); }
    double suma() const { return suma_.load(std::memory_order_relaxed); }
    /// El percentil `p` (0…1) interpolado dentro del casillero; -1 sin observaciones.
    double percentil(double p) const;
    const std::vector<double>& bordes() const { return bordes_; }
    /// Lo acumulado de cada casillero (n+1: el último es «más»).
    std::vector<std::uint64_t> casilleros() const;

    /// Los de siempre para tiempos en ms: de 1 ms a 10 s.
    static const std::vector<double>& bordesMs();

   private:
    std::vector<double> bordes_;
    std::unique_ptr<std::atomic<std::uint64_t>[]> casilleros_;
    std::atomic<std::uint64_t> cuenta_{0};
    std::atomic<double> suma_{0};
};

class Metricas {
   public:
    enum class Tipo { Contador, Medidor, Histograma };

    /// La serie `nombre`+`etiquetas` (la crea la primera vez). La referencia vale
    /// para siempre: guardarla y usarla sin volver a buscar. `ayuda`: una frase en
    /// castellano marcada con QT_TRANSLATE_NOOP("metricas", …) (se traduce al exportar).
    Contador& contador(const std::string& nombre, const char* ayuda, const Etiquetas& e = {});
    Medidor& medidor(const std::string& nombre, const char* ayuda, const Etiquetas& e = {});
    Histograma& histograma(const std::string& nombre, const char* ayuda, const Etiquetas& e = {},
                           const std::vector<double>& bordes = Histograma::bordesMs());

    /// Lo que copia a las series lo que ya mide otro, justo antes de exportar.
    /// Corre en el hilo que exporta; puede crear series. Devuelve un id para sacarlo.
    int agregarColector(std::function<void(Metricas&)> c);
    void quitarColector(int id);
    /// Corre los colectores (lo hacen texto() y json(); suelto, para las alarmas).
    void refrescar();

    /// Formato de texto de Prometheus 0.0.4.
    std::string texto();
    /// {nombre: {tipo, ayuda, series: [{etiquetas: {k: v}, valor} | {etiquetas, cuenta,
    /// suma, p50, p90, p99}]}}. Un medidor desconocido va como null.
    nlohmann::json json();
    /// El valor de una serie (sin refrescar): un contador o un medidor; de un
    /// histograma, con sufijo «:p50», «:p90», «:p99», «:cuenta» o «:suma». nullopt si
    /// no existe o no se sabe.
    std::optional<double> valor(const std::string& nombre, const Etiquetas& e = {}) const;
    /// Las series de un nombre (para las alarmas con «*»): [(etiquetas, valor)].
    std::vector<std::pair<Etiquetas, double>> valores(const std::string& nombre) const;

    /// La clave de una serie en el texto: «a="b",c="d"» (con las comillas escapadas).
    static std::string etiquetasATexto(const Etiquetas& e);

    /// Las del proceso (una sola: la app, el gateway y el launcher tienen la suya).
    static Metricas& proceso();

    Metricas() = default;
    Metricas(const Metricas&) = delete;
    Metricas& operator=(const Metricas&) = delete;

   private:
    struct Serie {
        Etiquetas etiquetas;
        std::unique_ptr<Contador> c;
        std::unique_ptr<Medidor> m;
        std::unique_ptr<Histograma> h;
    };
    struct Familia {
        Tipo tipo = Tipo::Contador;
        const char* ayuda = "";
        std::map<std::string, Serie> series; ///< por etiquetasATexto
    };
    Serie& serie(const std::string& nombre, Tipo t, const char* ayuda, const Etiquetas& e,
                 const std::vector<double>* bordes);

    mutable std::mutex mu_;
    std::map<std::string, Familia> familias_;
    std::mutex colMu_;
    std::vector<std::pair<int, std::function<void(Metricas&)>>> colectores_;
    int proximoColector_ = 1;
};

} // namespace ba::diag
