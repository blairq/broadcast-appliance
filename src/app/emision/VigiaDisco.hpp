// ============================================================================
// src/app/emision/VigiaDisco.hpp — el disco en apuros: el disco, para la grabación del programa
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp). Si el disco no da abasto (lo que espera
// en el colchón de una grabación, o el registro trabado escribiendo), se avisa y se ofrece
// darle el disco a la grabación del programa: se pausan las ISO, la repetición, la copia si va
// al mismo disco y la señal limpia a disco, y el registro se junta en memoria. Cuando el disco
// anda bien de nuevo (30 s de fábrica), todo sigue solo. También lo que queda de disco (las
// horas que alcanzan, por volumen) y las copias que van al mismo disco que su grabación.
//
// Hilos:
//   - EL HILO DE SALIDAS: vigilar, la prioridad, lo que queda de disco y el estado para la
//     foto (todo lo de abajo que no es atómico).
//   - EL DUEÑO DEL MOTOR: el modo (se guarda en Config/disco.json) y pedir la prioridad
//     (se encargan al hilo de salidas). Los umbrales (Configuración → Grabación) y el modo
//     son atómicos: los pone el dueño y los lee el hilo de salidas.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {

class BroadcastEngine;

class VigiaDisco {
   public:
    explicit VigiaDisco(BroadcastEngine& m) : m_(m) {}

    // --- Del dueño ------------------------------------------------------------------------
    /// El modo guardado (Config/disco.json), antes de arrancar el hilo de salidas.
    void cargarModo();
    /// "preguntar" (avisa y ofrece), "auto" (lo hace solo) o "nunca" (solo avisa).
    std::string modo() const;
    std::string setModo(const std::string& modo); ///< "" o el error
    /// Cuánto espera lo grabado para entrar al disco hasta «apuros», y cuánto todo al día hasta salir.
    void ponerUmbrales(double apurosS, int recuperadoS);
    /// Darle (o devolverle) el disco al programa ahora (en el hilo de salidas).
    void priorizar(bool si);

    // --- Del hilo de salidas --------------------------------------------------------------
    /// Una vuelta: el atraso de lo que graba y del registro, entrar y salir de apuros.
    void vigilar();
    /// Lo que queda de disco (cada pocos segundos: stat y espacio libre de cada carpeta).
    void medirRestante();
    /// {apuros, restante, mismo_volumen, atraso_s, …}: la parte «disco» de la foto de las salidas.
    nlohmann::json estado(bool grabaPrograma) const;
    /// El disco es del programa: lo que arranca ahora (ISO, repetición) arranca en pausa.
    bool prioridad() const { return prioridad_; }

   private:
    void aplicarPrioridad(bool si, bool automatica);

    BroadcastEngine& m_;
    // Los umbrales (ponerUmbrales): de este motor, no del proceso.
    std::atomic<double> apurosS_{3.0};               ///< lo grabado esperando al disco: apuros
    std::atomic<std::int64_t> registroTrabadoMs_{3000}; ///< el registro trabado escribiendo: apuros
    std::atomic<std::int64_t> bienDuranteMs_{30000};    ///< todo al día este tiempo: sale de apuros
    std::atomic<int> modo_{0}; ///< 0 preguntar, 1 auto, 2 nunca
    // Del hilo de salidas.
    bool apuros_ = false, prioridad_ = false, prioridadAuto_ = false;
    std::int64_t apurosDesdeMs_ = 0, bienDesdeMs_ = 0;
    double atraso_ = 0;
    std::int64_t registroTrabado_ = 0;
    std::vector<std::string> pausadas_;
    // Lo que queda de disco (restante, mismo_volumen): se recalcula cada pocos segundos.
    nlohmann::json restante_ = nlohmann::json::object(), mismoVolumen_ = nlohmann::json::array();
    std::int64_t restanteMs_ = 0;
};

} // namespace ba::app
