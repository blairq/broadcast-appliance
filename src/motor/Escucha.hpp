// ============================================================================
// src/motor/Escucha.hpp — lo que oye el operador en los auriculares
//
//   intersrc <productor> ! queue ! audioconvert ! volume ! level ! <sink de audio del sistema>
//
// El productor puede ser un bus (bus:programa, bus:directo) o una entrada sola
// (<id>:audio, la escucha previa PFL). Usa el reloj y el base_time del estudio y
// espera la latencia de lo que escucha: así «programa» suena junto con el video
// del programa, y nada llega tarde al sink.
//
// El dispositivo, por su id estable (el `unique-id` de macOS, el `device` de
// Windows y Linux); vacío = el del sistema. Si no está, se usa el del sistema.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <string>

namespace ba::motor {

class Reloj;

class Escucha {
   public:
    /// `sinkPrueba`: un sink en vez del de audio del sistema (las pruebas: «fakesink»).
    explicit Escucha(Reloj& reloj, std::string sinkPrueba = "");
    ~Escucha();
    Escucha(const Escucha&) = delete;
    Escucha& operator=(const Escucha&) = delete;

    /// Escuchar `productor` esperando `latenciaNs` (lo que tarda en salir). "" o el error.
    std::string escuchar(const std::string& productor, std::int64_t latenciaNs);
    void apagar();
    void ponerDispositivo(const std::string& id);
    void ponerNivel(double lineal);
    /// Lo llama el motor: si lo escuchado cambió de latencia, se acomoda.
    void tic(std::int64_t latenciaNs);
    nlohmann::json estado() const;
    /// Las salidas de audio del equipo: [{id, nombre}].
    static nlohmann::json dispositivos();

   private:
    void armar();
    void desarmar();

    Reloj& reloj_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    std::string productor_, dispositivo_, error_, sinkPrueba_;
    double nivel_ = 1.0, picoDb_ = -120;
    std::int64_t latenciaNs_ = 0;
};

} // namespace ba::motor
