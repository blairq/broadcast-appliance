// ============================================================================
// src/motor/Mezcla.hpp — un bus de audio del motor (audiomixer)
//
// Un pipeline propio por bus (PROGRAMA, una pista, un retorno…):
//
//   intersrc <id>:audio ! queue ! volume (fader, mute) ! level ─┐
//   intersrc <id>:audio ! queue ! volume ! level ───────────────┼─► audiomixer ! [balimitador] ! [ebur128level]
//   …                                                            ┘     ! level ! intersink bus:<nombre>
//                                                     (pad: ganancia de escena con fundido)
//
// El PROGRAMA lleva el limitador del máster (balimitador, −1 dBTP de fábrica) y
// el medidor de loudness (ebur128level: momentáneo, corto, integrado, LRA y true
// peak) a la salida, después del limitador: lo que se mide es lo que sale.
//
// - Reloj y base_time del estudio: las horas de las entradas valen tal cual.
// - El mezclador es en vivo aunque no tenga entradas (force-live) y arranca con
//   la primera que llega sin rebobinar (start-time-selection=first).
// - Espera la latencia del estudio más un margen (min-upstream-latency): una
//   entrada lenta que se enchufa andando no llega tarde. El pipeline suma lo que
//   declaran sus elementos (el bloque de salida, el limitador).
// - Las ramas se agregan y se sacan andando, sin cortar las demás.
//
// La ganancia de ESCENA (el audio de una fuente de escena suena solo si su
// escena está al aire) es aparte del fader del operador: va en el pad del
// mezclador y cambia con un fundido.
// ============================================================================
#pragma once

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "ajustes/Ajustes.hpp"

namespace ba::motor {

class Reloj;

class Mezcla {
   public:
    struct Opciones {
        bool limitador = false; ///< balimitador a la salida (el máster)
        bool loudness = false;  ///< ebur128level a la salida
        bool medidores = true;  ///< un `level` por entrada (los retornos no lo necesitan)
        /// Si no está vacío, el bus sale a ese anillo del gateway (baanillosink) en vez
        /// de a un intersink (p. ej. BA.Audio.Return.1).
        std::string anillo;
        /// ≥ 0: espera esto fijo y no la latencia del estudio (la escucha «directo»:
        /// la sala sin demora; lo que llega más tarde, no entra).
        std::int64_t latenciaFijaNs = -1;
    };
    Mezcla(Reloj& reloj, std::string nombre, Opciones op);
    Mezcla(Reloj& reloj, std::string nombre) : Mezcla(reloj, std::move(nombre), Opciones{}) {}
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

    /// El volumen de todo el bus (lineal, 0…1) con un fundido de `ms` (el FTB baja el audio).
    void ponerMaster(double lineal, int ms);
    /// El limitador del máster: prendido y techo (dBTP). false si este bus no lo tiene.
    bool ponerLimitador(bool activo, double techoDb);
    /// Lo llama el motor seguido: medidores, latencia, fundidos.
    void tic();
    /// {latencia_ms, pico_db, rms_db, limitador: {activo, techo_db, reduccion_db},
    ///  loudness: {momentaneo, corto, integrado, lra, true_peak_db}, entradas: {id: {…}}}
    nlohmann::json estado() const;
    /// El pico del bus (dBFS, el mayor de los canales; -120 sin señal).
    double picoDb() const;
    /// Cuánto después de su hora sale lo que entrega el bus (ns): lo que tiene
    /// que esperar quien lo consume (la escucha, la grabación, el gateway), o
    /// todo le llega tarde.
    std::int64_t latenciaSalidaNs() const;

   private:
    struct Rama {
        GstElement* src = nullptr;
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
    GstElement* limitador_ = nullptr;
    GstElement* master_ = nullptr;
    double masterDesde_ = 1, masterHacia_ = 1, master_v_ = 1;
    std::int64_t masterDesdeNs_ = 0, masterNs_ = 0;
    nlohmann::json loudness_ = nlohmann::json::object();
    mutable std::mutex mu_;
    std::map<std::string, Rama> ramas_;
    std::int64_t latenciaNs_ = -1;
    std::int64_t salidaNs_ = 0;
    std::int64_t consultaNs_ = 0;
    LatenciaAparte latencia_;
    double picoDb_ = -120, rmsDb_ = -120;
    bool medidores_ = true;
    std::int64_t latenciaFijaNs_ = -1;
};

/// El margen sobre la latencia del estudio que esperan los mezcladores
/// (Configuración → avanzado: «audio.margenMezclaMs», 30 ms de siempre).
inline std::int64_t margenMezclaNs() { return ajustes::msANs("audio.margenMezclaMs", 30); }

} // namespace ba::motor
