// SPDX-License-Identifier: GPL-2.0-or-later
//
// LatidoAire: el latido del supervisor desde un hilo propio, que mira si el
// MOTOR sigue sacando cuadros, y no solo si la interfaz está viva.
//
// Antes el único latido salía del QTimer de la interfaz (100 ms): una traba de
// 5 s de la UI (un diálogo, un QML pesado, un disco lento) hacía que el
// supervisor matara el proceso con la transmisión andando. Ahora:
//
//   - la interfaz sigue latiendo en cada tick (uiTick) y
//   - este hilo late cada 250 ms MIENTRAS los cuadros del motor avancen.
//
// El supervisor solo mata si ninguno de los dos late (motor trabado). Si la
// interfaz se traba y el motor sigue, el aire no se corta: se avisa (registro y
// Runtime/estado-ui.json) y, pasada la espera configurada, se deja de latir
// solo si NO hay transmisión ni grabación (y así el supervisor relanza una app
// que quedó inservible sin cortar nada que esté saliendo).
//
// La política se guarda en Config/supervisor.json.
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ba::app {

class LatidoAire {
   public:
    struct Politica {
        int esperaUiS = 60;                    ///< cuánto se tolera la UI trabada con el motor vivo
        bool matarSiUiTrabadaSinAire = true;   ///< pasada la espera, relanzar si no hay aire
    };

    /// `latir`: marca el latido en el bloque de salud (lo serializa la app).
    /// `cuadros`: el contador de cuadros del motor (video_output_get_total_frames).
    void iniciar(const std::filesystem::path& datos, std::function<void()> latir,
                 std::function<std::uint64_t()> cuadros);
    void detener();
    ~LatidoAire() { detener(); }

    /// Lo llama el tick de la interfaz: está viva, y si hay aire.
    void uiTick(bool hayAire);

    Politica politica() const;
    /// Cambia y guarda la política. "" o el error.
    std::string ponerPolitica(const nlohmann::json& cambios);

    /// {motor_vivo, ui_viva, ui_trabada_ms, ultima_traba_ms, politica}.
    nlohmann::json estado() const;
    /// La última traba de la UI ya terminada (ms), para avisar una vez; 0 si no hay.
    std::uint64_t tomarTrabaTerminada();

   private:
    void bucle();
    void escribirEstadoUi(bool trabada, std::uint64_t ms) const;
    void guardar() const;

    std::filesystem::path datos_;
    std::function<void()> latir_;
    std::function<std::uint64_t()> cuadros_;
    std::thread hilo_;
    std::atomic<bool> corriendo_{false};
    std::atomic<std::uint64_t> ultimoUiNs_{0};
    std::atomic<bool> hayAire_{false};
    std::atomic<bool> motorVivo_{false};
    std::atomic<std::uint64_t> trabaTerminadaMs_{0};
    std::atomic<std::uint64_t> ultimaTrabaMs_{0};
    mutable std::mutex mu_; // la política
    Politica pol_;
};

} // namespace ba::app
