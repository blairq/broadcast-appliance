// ============================================================================
// src/app/emision/BuzonSalidas.hpp — lo pedido y el buzón entre el dueño y el hilo de salidas
//
// Ver emision/SalidaMotor.hpp. Lo pedido de cada salida es del dueño del motor y lleva el
// número de la orden que lo cambió (PedidoOrden); el hilo de salidas le cuenta lo que pasó
// por el buzón (BuzonSalidas, en orden y desde cualquier hilo). Sin dependencias: lo prueba
// tests/test_buzon_salidas.cpp.
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ba::app {

/// Algo pedido al hilo de salidas, con el número de la última orden que lo cambió: un «no
/// arrancó» que llega tarde (de una orden ya reemplazada) no baja lo que se pidió después.
struct PedidoOrden {
    bool si = false;
    std::uint64_t orden = 0;
    /// Lo pide (o deja de pedirlo) con la orden que sigue de `ordenes`; devuelve su número.
    std::uint64_t poner(bool pedido, std::uint64_t& ordenes) {
        si = pedido;
        orden = ++ordenes;
        return orden;
    }
    /// La orden `o` no arrancó: deja de estar pedido, si sigue siendo esa orden.
    void noArranco(std::uint64_t o) {
        if (orden == o) si = false;
    }
};

/// Lo que el hilo de salidas le cuenta al dueño (ver emision/SalidaMotor.hpp).
struct MensajeSalida {
    enum class Tipo {
        NoTransmite,     ///< la transmisión principal de la orden `orden` no arrancó
        NoGraba,         ///< la grabación de la orden `orden` no arrancó
        NoLimpia,        ///< la señal limpia de la orden `orden` no arrancó
        ExtraNoArranco,  ///< el destino `id` de la orden `orden` no arrancó
        AparteNoArranco, ///< la grabación aparte de la entrada `id` (orden `orden`) no arrancó
        ExtraAdoptado,   ///< el emisor aparte ya transmitía al destino `id`: queda pedido
        LimpioAdoptado,  ///< el emisor aparte ya tenía la señal limpia: queda pedida
        Aac,             ///< transmite AAC a `kbps` (el más chico hasta ahora)
    };
    Tipo tipo;
    std::string salida, id;
    std::uint64_t orden = 0;
    int kbps = 0;
};

/// El buzón del hilo de salidas al dueño: en orden, desde cualquier hilo.
class BuzonSalidas {
   public:
    void poner(MensajeSalida m) {
        std::lock_guard<std::mutex> l(mu_);
        cola_.push_back(std::move(m));
    }
    /// Lo que llegó, en el orden en que llegó (y el buzón queda vacío).
    std::vector<MensajeSalida> tomar() {
        std::lock_guard<std::mutex> l(mu_);
        std::vector<MensajeSalida> out;
        out.swap(cola_);
        return out;
    }

   private:
    std::mutex mu_;
    std::vector<MensajeSalida> cola_;
};

} // namespace ba::app
