// ============================================================================
// src/gateway/DestinoCamara.hpp — adónde va el cuadro de la cámara de una sesión (F3, #6 y #8)
//
// El hilo que DECODIFICA el video de un invitado (el pozo «dec» de rtc/ColasCompartidas)
// entrega el cuadro sin pasar a otro hilo (vertical, docs/planes/PLAN-GATEWAY-FRAGMENTOS.md):
//
//   - a la grilla, solo la CELDA ya achicada al lugar que ocupa (Grillas::pedirCelda): en
//     Windows con la placa se achica en la GPU y se baja solo lo chico (CodecsWin.cpp); en
//     la Mac y en lo demás, del NV12/I420 con libyuv;
//   - a ba-app (memoria compartida, MediaPump::forwardVideo), el cuadro entero en BGRA, y
//     SOLO si su ruta lo publica ahora (quiereEntero): con 40 en la grilla casi ninguno va.
//
// Antes, cada cuadro se convertía entero a BGRA (3,5 ms un 720p en la .177) aunque nadie
// lo fuera a usar, y la grilla lo achicaba después. Lo arma SignalingServer por sesión y lo
// usa codecs::entregarCamara (CodecsVideo.cpp) desde el sink de video de Webrtc.cpp.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "Grillas.hpp"

namespace ba::gateway {

class MediaPump;

class DestinoCamara {
   public:
    /// `pump` y `grillas` pueden ser nullptr (no hay a quién); `persona`: la clave de la grilla
    /// ("sala:<id>", "asamblea:<id>").
    DestinoCamara(MediaPump* pump, int sesion, Grillas* grillas, std::string persona)
        : pump_(pump), sesion_(sesion), grillas_(grillas), persona_(std::move(persona)) {}

    /// ¿ba-app quiere el cuadro entero de este momento? (si no, no se convierte a BGRA). Es
    /// «quiere» de verdad: su canal tiene quién lo lea ahora (SpoutSender::demanda; una
    /// tarjeta que no se ve, uno por segundo). Se llama con cada cuadro que llega: anota la
    /// llegada para la placa del lugar (MediaPump::llegoCamara).
    bool quiereEntero() const;
    /// El cuadro entero en BGRA (filas de w*4) a ba-app.
    void entero(const std::uint8_t* bgra, int w, int h);
    /// La celda que quiere la grilla para un cuadro de `w`x`h` (ver Grillas::pedirCelda).
    Grillas::PedidoCelda pedirCelda(int w, int h);
    void guardarCelda(Grillas::PedidoCelda&& p);
    void soltarCelda(Grillas::PedidoCelda&& p);
    /// ¿Se puede achicar en la placa? (`gateway.celdaEnPlaca`; apagado: del cuadro bajado).
    static bool celdaEnPlaca();

    const std::string& persona() const { return persona_; }
    int sesion() const { return sesion_; }

   private:
    MediaPump* pump_;
    int sesion_;
    Grillas* grillas_;
    std::string persona_;
};

} // namespace ba::gateway
