// ============================================================================
// src/app/emision/NivelesMotor.hpp — el canal de niveles: los vúmetros sin tocar el motor (F3-03)
//
// docs/arquitectura/05-motor-del-plano.md. Lo que se mueve a cada rato (los picos de cada
// entrada, de los buses y del programa de cada salida) no va en la foto del motor
// (FotoMotor): cambia en cada vuelta y la compararía siempre distinta. Va por este canal,
// que la interfaz lee a su ritmo (los vúmetros a 10 Hz, el Mezclador a 30 Hz) sin el turno
// ni candados del motor.
//
//   - LOS NIVELES de cada entrada y bus los publica el hilo del motor (Motor::niveles, cada
//     ~40 ms; Motor::alPublicarNiveles): acá queda la última foto. Si el motor se rearma,
//     el nuevo publica en el mismo canal; apagado, queda vacío.
//   - EL PICO DEL PROGRAMA de cada salida lo suma el dueño en cada vuelta de su reloj
//     (AppRuntime::ticMotor, BroadcastEngine::programPeaksDb): el mayor desde que la
//     interfaz lo tomó (tomarPrograma, el vúmetro de arriba con su caída) y el de la
//     última vuelta (programa: el MCP, que no se lo roba a la vista).
//
// Es de BroadcastEngine (vive lo que la app); la interfaz lo pide una vez
// (AppRuntime::niveles). DESDE CUALQUIER HILO.
// ============================================================================
#pragma once

#include "NivelAudio.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ba::app {

class NivelesMotor {
   public:
    using Niveles = std::vector<motor::NivelAudio>;

    /// DESDE EL MOTOR: la foto nueva de los niveles (nullptr: el motor se fue).
    void ponerNiveles(std::shared_ptr<const Niveles> n);
    /// La última (nullptr si no hay motor). Se puede guardar: no cambia.
    std::shared_ptr<const Niveles> niveles() const;
    /// El pico (dBFS, el mayor de los dos canales) de lo que entrega la entrada `nombre`,
    /// después de su rack y antes de su volumen; -100 si no suena o no es de audio.
    float picoEntrada(const std::string& nombre) const;
    /// Lo mismo de un bus (`programa`, `pista2`, `matriz:<id>`…).
    float picoBus(const std::string& id) const;

    /// DESDE EL DUEÑO: cuál es la salida por omisión (su id también se pide como "").
    void setPorOmision(const std::string& id);
    /// DESDE EL DUEÑO: el pico del programa de `salida` en esta vuelta (dBFS, L y R).
    void sumarPrograma(const std::string& salida, float l, float r);
    /// El mayor desde la última vez que se lo tomó (y lo vuelve a -100). "" = la por omisión.
    std::pair<float, float> tomarPrograma(const std::string& salida);
    /// El de la última vuelta del dueño, sin tomarlo.
    std::pair<float, float> programa(const std::string& salida) const;
    /// Una salida que ya no está (deja de sumarse).
    void olvidarSalida(const std::string& salida);

   private:
    std::string clave(const std::string& salida) const; ///< con mu_
    struct Programa {
        float acum[2] = {-100, -100}; ///< el mayor desde que se tomó
        float ultimo[2] = {-100, -100};
    };
    mutable std::mutex mu_;
    std::shared_ptr<const Niveles> niveles_;
    std::string porOmision_;
    std::map<std::string, Programa> programa_; ///< "" = la por omisión
};

} // namespace ba::app
