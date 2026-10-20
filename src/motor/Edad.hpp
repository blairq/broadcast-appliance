// ============================================================================
// src/motor/Edad.hpp — la edad de lo que pasa por un pad, siempre medida
//
// Como la edad de una entrada (Entrada: «ahora − hora del cuadro» con el reloj y el
// base_time del estudio, que comparten todos los pipelines), pero para cualquier
// pad: la salida de la composición del programa, la de la mezcla, la de una salida.
// Así la página de Latencias deja de mostrar la latencia FIJADA en esos tramos y
// muestra la que pasa de verdad (una sonda en un solo pad: cuesta nada).
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <nlohmann/json.hpp>

#include <memory>

namespace ba::motor {

class Reloj;

class MedidorEdad {
   public:
    MedidorEdad() = default;
    ~MedidorEdad() { sacar(); }
    MedidorEdad(const MedidorEdad&) = delete;
    MedidorEdad& operator=(const MedidorEdad&) = delete;

    /// Empieza a medir lo que pasa por `pad` (se queda una referencia). Saca la anterior.
    void poner(GstPad* pad, const Reloj& reloj);
    /// En el primer pad de entrada (sink) de `elemento` (los de pedido también: un mux).
    void ponerEnEntrada(GstElement* elemento, const Reloj& reloj);
    void sacar();
    /// {p50_ms, p95_ms, n} de los últimos ~10 s; null si no pasó nada todavía.
    nlohmann::json json() const;

    struct Datos;

   private:
    std::shared_ptr<Datos> d_;
    GstPad* pad_ = nullptr;
    gulong id_ = 0;
};

} // namespace ba::motor
