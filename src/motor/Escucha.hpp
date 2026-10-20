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
#include <functional>
#include <future>
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

    /// Escuchar `productor` esperando `latenciaNs` (lo que tarda en salir). Como apagar,
    /// ponerDispositivo y ponerNivel: NO ESPERA, lo hace su trabajador (abrir el dispositivo
    /// de audio puede tardar segundos y lo pide el bucle del motor, con su candado). Un
    /// error queda en el registro y en estado().
    void escuchar(const std::string& productor, std::int64_t latenciaNs);
    void apagar();
    void ponerDispositivo(const std::string& id);
    void ponerNivel(double lineal);
    /// Lo llama el motor: si lo escuchado cambió de latencia, se acomoda (con el
    /// trabajador ocupado, la saltea).
    void tic(std::int64_t latenciaNs);
    /// Con el trabajador ocupado, lo último que se supo.
    nlohmann::json estado() const;
    /// Espera lo encargado (las pruebas).
    void esperar();
    /// Las salidas de audio del equipo: [{id, nombre}].
    static nlohmann::json dispositivos();

   private:
    void armar();
    void desarmar();
    void encargar(std::function<void()> f); ///< al trabajador, en orden, con mu_
    nlohmann::json estadoYa() const;         ///< con mu_ tomado

    Reloj& reloj_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    std::string productor_, dispositivo_, error_, sinkPrueba_;
    double nivel_ = 1.0, picoDb_ = -120;
    std::int64_t latenciaNs_ = 0;
    std::mutex trabajoM_;
    std::shared_future<void> trabajo_;
    mutable std::mutex guardadoM_;
    mutable nlohmann::json guardado_ = nlohmann::json::object();
};

} // namespace ba::motor
