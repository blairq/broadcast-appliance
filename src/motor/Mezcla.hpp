// ============================================================================
// src/motor/Mezcla.hpp — un bus de audio del motor (audiomixer)
//
// Un pipeline propio por bus (PROGRAMA, una pista, un retorno…):
//
//   intersrc <id>:audio ! queue ! volume (fader, mute) ! level ─┐
//   intersrc <id>:audio ! queue ! volume ! level ───────────────┼─► audiomixer ! level ! intersink bus:<nombre>
//   …                                                            ┘   (pad: ganancia de escena con fundido)
//
// - Reloj y base_time del estudio: las horas de las entradas valen tal cual.
// - El mezclador es en vivo aunque no tenga entradas (force-live) y arranca con
//   la primera que llega sin rebobinar (start-time-selection=first).
// - Espera la latencia del estudio más un margen (min-upstream-latency y la del
//   pipeline): una entrada lenta que se enchufa andando no llega tarde.
// - Las ramas se agregan y se sacan andando, sin cortar las demás.
//
// La ganancia de ESCENA (el audio de una fuente de escena suena solo si su
// escena está al aire) es aparte del fader del operador: va en el pad del
// mezclador y cambia con un fundido.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace ba::motor {

class Reloj;

class Mezcla {
   public:
    Mezcla(Reloj& reloj, std::string nombre);
    ~Mezcla();
    Mezcla(const Mezcla&) = delete;
    Mezcla& operator=(const Mezcla&) = delete;

    const std::string& nombre() const { return nombre_; }
    /// Lo que entrega el bus (para otros pipelines: `intersrc producer-name=…`).
    std::string productor() const { return "bus:" + nombre_; }
    bool anda() const { return pipeline_ != nullptr; }
    const std::string& error() const { return error_; }

    /// Suma la entrada `id` (lee `<id>:audio`). Si ya está, nada.
    void agregar(const std::string& id);
    void quitar(const std::string& id);
    bool tiene(const std::string& id) const;
    /// El fader (lineal, 0…4) y el mute del operador.
    void ponerNivel(const std::string& id, double lineal);
    void ponerMute(const std::string& id, bool mute);
    /// La ganancia de escena (0…1) con un fundido de `ms`.
    void ponerEscena(const std::string& id, double ganancia, int ms);

    /// Lo llama el motor seguido: medidores, latencia, fundidos.
    void tic();
    /// {latencia_ms, pico_db, rms_db, entradas: {id: {nivel, mute, escena, pico_db, rms_db}}}
    nlohmann::json estado() const;
    /// El pico del bus (dBFS, el mayor de los canales; -120 sin señal).
    double picoDb() const;

   private:
    struct Rama {
        GstElement* src = nullptr;
        GstElement* cola = nullptr;
        GstElement* vol = nullptr;
        GstElement* nivel = nullptr;
        GstPad* pad = nullptr; ///< el del mezclador
        double fader = 1.0;
        bool mute = false;
        double escena = 1.0, escenaDesde = 1.0, escenaHacia = 1.0;
        std::int64_t fundidoDesdeNs = 0, fundidoNs = 0;
        double picoDb = -120, rmsDb = -120;
    };
    void aplicarLatencia();
    void soltar(Rama& r);

    Reloj& reloj_;
    std::string nombre_, error_;
    GstElement* pipeline_ = nullptr;
    GstElement* mezclador_ = nullptr;
    mutable std::mutex mu_;
    std::map<std::string, Rama> ramas_;
    std::int64_t latenciaNs_ = -1;
    double picoDb_ = -120, rmsDb_ = -120;
};

/// El margen sobre la latencia del estudio que esperan los mezcladores.
constexpr std::int64_t kMargenMezclaNs = 30 * GST_MSECOND;

} // namespace ba::motor
