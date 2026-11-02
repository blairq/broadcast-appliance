// ============================================================================
// src/gateway/Fundido.hpp — de la capa baja a la HD con un fundido, sin negro ni salto
//
// REGLA DEL USUARIO (2026-11-01): «cuando se le da la palabra hay que usar el stream de
// alta definición que envía ese participante: un fade in y listo». O sea:
//   1. al darle la palabra (o al pasar a un lugar grande de una grilla) la sala le pide la
//      capa ALTA (Capas.hpp);
//   2. mientras no llega el primer cuadro HD se sigue mostrando la BAJA (nunca negro: el
//      SelectorCapas deja pasar la baja aunque se haya pedido la alta);
//   3. cuando llega, FUNDIDO de la baja a la HD (`ms`, corto: de fábrica 300 ms);
//   4. recién después del fundido se pausa la baja (SelectorCapas la retiene ese tiempo).
//
// Esto es la parte de la IMAGEN: quien pinta (el canal de la app en MediaPump, el retrato
// de la grilla en Grillas) guarda el último cuadro de la capa que se va (el «anterior») y,
// durante el fundido, mezcla cada cuadro nuevo con ese anterior. Mientras dura, los cuadros
// que todavía llegan de la capa vieja refrescan el anterior (no queda una foto congelada).
//
// Sin hilos ni candados (lo protege quien lo tiene) y sin libwebrtc: lo prueba test_fundido.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace ba::gateway {

/// Duración del fundido de fábrica (ms). La cambia la app (ajuste «fundido_ms» de la grilla).
inline constexpr int kFundidoMsDefecto = 300;

/// 0..1: cuánto se ve la imagen NUEVA a los `ahora - t0` ms de un fundido de `durMs`
/// (con la curva suave de las grillas). durMs <= 0: 1 (de golpe).
float alfaFundido(std::int64_t t0, int durMs, std::int64_t ahora);

/// dst = ant·(1-a) + dst·a, píxel a píxel (BGRA, los cuatro canales). Mismo tamaño.
void mezclarBgra(std::uint8_t* dst, int dstStride, const std::uint8_t* ant, int antStride, int w, int h, float a);

/// Escala BGRA (vecino más cercano; para el anterior alcanza: se está yendo).
void escalarCercano(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dw, int dh);

/// El fundido de UNA imagen (un canal, un retrato).
class Fundido {
   public:
    /// Empieza: `px` (w x h BGRA) es lo último que se mostró de la capa que se va.
    void empezar(const std::uint8_t* px, int w, int h, int durMs, std::int64_t ahora);
    /// Un cuadro de la capa vieja que llegó durante el fundido: es el anterior ahora.
    void anterior(const std::uint8_t* px, int w, int h);
    /// ¿Hay un fundido en curso en `ahora`? (cuando termina, suelta el anterior)
    bool activo(std::int64_t ahora);
    /// Mezcla `px` (el cuadro nuevo, w x h, paso `stride`) con el anterior (escalado a ese
    /// tamaño si hace falta). Sin fundido activo no toca nada. true si mezcló.
    bool aplicar(std::uint8_t* px, int stride, int w, int h, std::int64_t ahora);
    /// Lo corta (otra conexión, otro canal).
    void cortar();
    /// 0..1 de la imagen nueva (1 si no hay fundido): para las pruebas y el estado.
    float alfa(std::int64_t ahora) const;

   private:
    std::vector<std::uint8_t> ant_, escalado_;
    int w_ = 0, h_ = 0;
    std::int64_t t0_ = 0;
    int ms_ = 0;
    bool activo_ = false;
};

} // namespace ba::gateway
