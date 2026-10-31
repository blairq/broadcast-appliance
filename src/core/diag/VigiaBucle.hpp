// ============================================================================
// src/core/diag/VigiaBucle.hpp — cuánto se demora el bucle de eventos de la interfaz
//
// Todo lo que corre en el hilo de la interfaz (un evento de QML, un timer, una
// herramienta MCP que se despacha ahí, la E/S de un archivo de estado) hace
// esperar a lo demás. El vigía del candado (motor/Candado.hpp) solo ve las
// esperas por el motor y LatidoAire solo las trabas de segundos; esto mide
// TODAS, chicas y grandes: un temporizador de 50 ms que mira cuánto tarde le
// llegó su turno. Ese retraso es lo que esperó cualquier cosa encolada en ese
// momento (un clic, un cuadro de la vista previa, la respuesta a un MCP).
//
//   - Histograma desde el arranque y los retrasos del último minuto (p50/p99/máx).
//   - Quién estaba corriendo: lo que anota el que despacha (`anotar`: el evento
//     de Qt y su destino, la herramienta MCP) con su hora; cuando el turno llega
//     tarde, el culpable es lo último que EMPEZÓ antes de la hora en que le
//     tocaba al temporizador (lo que lo tapó).
//   - Los peores culpables (veces, total y máximo) y un aviso en el registro por
//     culpable cada 10 s, igual que el vigía del candado.
//
// Sin Qt: el temporizador y el filtro de eventos los pone la app
// (ui/VigiaInterfaz) y llaman a `tic` y a `anotar` desde el hilo de la interfaz.
// El reloj se inyecta (los tests no esperan).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ba::diag {

class VigiaBucle {
   public:
    /// Microsegundos monotónicos (de fábrica, steady_clock).
    using Reloj = std::function<std::int64_t()>;
    /// Lo que se dice cuando el turno llega tarde (de fábrica, al registro).
    using Avisar = std::function<void(const std::string& texto)>;

    explicit VigiaBucle(Reloj reloj = {}, int periodoMs = 50, int umbralMs = 50);

    /// El turno del temporizador (cada `periodoMs`). La primera vez solo toma la hora.
    void tic();
    /// Lo que empieza a correr en el bucle. `que` vive para siempre (el nombre de una
    /// clase de Qt, un literal): no se copia, para que anotar cada evento no cueste.
    /// `detalle`: el tipo de evento (o 0).
    void anotar(const char* que, int detalle = 0);
    /// Igual, con un texto que se copia (la herramienta MCP: «mcp scene_switch»).
    void anotar(const std::string& que);

    /// {periodo_ms, umbral_ms, vueltas, tarde, histograma, ultimo_minuto: {p50_ms,
    /// p99_ms, max_ms, muestras}, culpables: [{que, veces, total_ms, max_ms}] (los 10
    /// peores por total), ultimo_tarde: {ms, que, hace_s}}.
    nlohmann::json informe() const;

    void alAvisar(Avisar a);
    /// El de la interfaz (uno por proceso).
    static VigiaBucle& interfaz();

    /// Los bordes del histograma (ms): [0,1), [1,2), [2,5)… y el último, «más».
    static constexpr std::array<int, 11> kBordesMs{1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000};

   private:
    struct Anotado {
        std::int64_t us = 0;
        const char* que = nullptr;
        int detalle = 0;
        std::string texto; ///< si no está vacío, en vez de `que`
    };
    struct Culpable {
        std::uint64_t veces = 0;
        std::int64_t totalUs = 0;
        std::int64_t maxUs = 0;
        std::int64_t avisadoUs = -1; ///< el último aviso
    };
    void anotarEn(Anotado a);
    /// Lo último que empezó en o antes de `hasta` (lo que tapó el turno).
    std::string quien(std::int64_t hasta) const;

    Reloj reloj_;
    std::int64_t periodoUs_, umbralUs_;
    Avisar avisar_;

    mutable std::mutex mu_;
    std::int64_t anteriorUs_ = -1;
    std::uint64_t vueltas_ = 0, tarde_ = 0;
    std::array<std::uint64_t, kBordesMs.size() + 1> histo_{};
    std::vector<std::int64_t> minuto_; ///< los retrasos del último minuto (anillo)
    std::size_t minutoMax_ = 0;
    std::size_t minutoPos_ = 0;
    std::array<Anotado, 64> anotados_{}; ///< anillo: lo último que se anotó
    std::size_t anotadoPos_ = 0;
    std::map<std::string, Culpable> culpables_;
    std::int64_t ultimoTardeUs_ = 0, ultimoTardeCuandoUs_ = -1;
    std::string ultimoTardeQue_;
};

} // namespace ba::diag
