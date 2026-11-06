// ============================================================================
// src/motor/Cuadro.hpp — un cuadro de una vista del motor (Motor::verUltimo, verAvisar)
//
// Lo que la app lee de las vistas (Vistas.hpp): los bytes (Pixeles: el buffer de GStreamer
// mapeado, sin copiar) y su forma. Aparte de Vistas.hpp para que quien solo lee cuadros
// (la app, el emisor) no traiga GStreamer ni las vistas. Pixeles está en Vistas.cpp.
// ============================================================================
#pragma once

#include "GstAdelantado.hpp"
#include "diag/SondasCuadros.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ba::motor {


/// Los bytes de un cuadro: el buffer de GStreamer mismo, mapeado para leer y con su
/// referencia (copiar un Pixeles es contar una referencia más, no copiar bytes). El buffer
/// se desmapea y se suelta con el último que lo tiene.
class Pixeles {
   public:
    Pixeles() = default;
    /// Una referencia a `b`, mapeado; `desde`/`bytes`: la parte que es el cuadro (0 = todo).
    Pixeles(GstBuffer* b, std::size_t desde, std::size_t bytes);
    const std::uint8_t* data() const { return datos_; }
    std::size_t size() const { return bytes_; }
    bool empty() const { return bytes_ == 0; }

   private:
    struct Mapa;
    std::shared_ptr<const Mapa> mapa_;
    const std::uint8_t* datos_ = nullptr;
    std::size_t bytes_ = 0;
};

struct Cuadro {
    Pixeles bgra; ///< BGRA, filas de `stride` bytes (en NV12: desde el plano Y hasta el final del UV)
    int ancho = 0, alto = 0, stride = 0;
    /// Una vista pedida en NV12: el plano UV empieza `desdeUv` bytes después del Y, con
    /// filas de `strideUv` bytes (alto/2 filas).
    bool nv12 = false;
    std::size_t desdeUv = 0;
    int strideUv = 0;
    std::uint64_t serie = 0;
    /// La hora de lo que muestra, en el reloj de los sellos (HealthBlock::nowQpc); 0 = no se sabe.
    std::uint64_t contenidoQpc = 0;
    /// SONDA: el número de cuadro de ESTA vista (1, 2, 3…): quien lee ve cuántos se perdió.
    BA_SONDA(std::uint64_t numero = 0;)
};

} // namespace ba::motor
