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
// La interfaz trabada CON aire es una alarma (F1-13): el aviso sale de este
// hilo (ponerAviso), no del de la interfaz, que es justo el que no anda. Lo
// lleva AvisoFueraDeUi a los notificadores y al control web.
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
#include <vector>

namespace ba::app {

class LatidoAire {
   public:
    struct Politica {
        int esperaUiS = 60;                    ///< cuánto se tolera la UI trabada con el motor vivo
        bool matarSiUiTrabadaSinAire = true;   ///< pasada la espera, relanzar si no hay aire
        /// A qué notificadores va la alarma «interfaz trabada con aire»: todos (de
        /// fábrica) o los de `notificar` (vacía = ninguno).
        bool notificarTodos = true;
        std::vector<std::string> notificar;
    };
    /// La alarma «interfaz trabada con aire»: `trabada` al empezar (pasados
    /// supervisor.uiTrabadaMs) y false cuando la interfaz vuelve; `ms`, lo que lleva
    /// (o lo que duró). Se llama DESDE EL HILO DEL LATIDO: no puede tocar nada de la
    /// interfaz ni tardar (encolar y volver).
    using Aviso = std::function<void(bool trabada, std::uint64_t ms, bool motorVivo)>;

    /// `latir`: marca el latido en el bloque de salud (lo serializa la app).
    /// `cuadros`: el contador de cuadros del motor (video_output_get_total_frames).
    void iniciar(const std::filesystem::path& datos, std::function<void()> latir,
                 std::function<std::uint64_t()> cuadros);
    void detener();
    ~LatidoAire() { detener(); }
    /// Quién lleva la alarma afuera (nullptr = nadie). Se puede cambiar andando.
    void ponerAviso(Aviso a);

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
    void avisar(bool trabada, std::uint64_t ms, bool vivo);

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
    /// Tomado MIENTRAS se avisa: así ponerAviso(nullptr) espera al que esté en curso
    /// y el que lleva la alarma se puede destruir después sin carrera.
    std::mutex avisoMu_;
    Aviso aviso_;
};

} // namespace ba::app
