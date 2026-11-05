// ============================================================================
// src/motor/MedidorCeldas.hpp — AnalisisCeldas en un pad (una sonda), o a mano
//
// Pone una sonda en un pad de video en memoria de sistema (4 bytes por píxel: BGRA,
// RGBA…) y le pasa cada cuadro a un AnalisisCeldas, con la hora de llegada y el número
// de cuadro del emisor (GST_BUFFER_OFFSET, si lo trae). Se usa para la grilla de
// personas (escenas::ControlGrillas::medir): en la entrada de la grilla, apenas sale del
// lector común (ba_origen), y en la vista del programa (que no es un pad: `cuadro`).
// Mide lo que dura la medición; sacar() suelta la sonda.
// ============================================================================
#pragma once

#include "AnalisisCeldas.hpp"

#include "GstAdelantado.hpp"

#include <memory>
#include <mutex>

namespace ba::motor {

class MedidorCeldas {
   public:
    explicit MedidorCeldas(std::vector<AnalisisCeldas::Celda> celdas);
    ~MedidorCeldas() { sacar(); }
    MedidorCeldas(const MedidorCeldas&) = delete;
    MedidorCeldas& operator=(const MedidorCeldas&) = delete;

    /// Mide lo que pasa por `pad` (se queda una referencia). false si ya medía otro.
    bool poner(GstPad* pad);
    void sacar();
    /// Un cuadro a mano (la vista del programa), desde cualquier hilo.
    void cuadro(const std::uint8_t* px, int w, int h, int stride, std::uint64_t indice = 0);
    /// Cambió la disposición (AnalisisCeldas::cambiar), ahora; desde cualquier hilo.
    void cambiar(const std::vector<AnalisisCeldas::Celda>& celdas);
    /// AnalisisCeldas::resumen.
    nlohmann::json resumen() const;

    struct Datos;

   private:
    std::shared_ptr<Datos> d_;
    GstPad* pad_ = nullptr;
    gulong id_ = 0;
};

} // namespace ba::motor
