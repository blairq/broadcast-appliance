// ============================================================================
// src/gateway/DestinoCamara.hpp — adónde va el cuadro de la cámara de una sesión (F3, #6 y #8)
//
// El hilo que DECODIFICA el video de un invitado (el pozo «dec» de webrtc/ColasRtc)
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
#include "diag/RitmoCuadros.hpp"
#include "diag/SondasCuadros.hpp"

#include <nlohmann/json.hpp>

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
    /// El cuadro entero en BGRA (filas de w*4) a ba-app, con la hora de su contenido
    /// (`contenidoQpc`, codecs::horaDeContenido; 0 = no se sabe: va con la de publicación).
    void entero(const std::uint8_t* bgra, int w, int h, std::uint64_t contenidoQpc = 0);
    /// La celda que quiere la grilla para un cuadro de `w`x`h` (ver Grillas::pedirCelda; `redUs`
    /// y `decoUs`: cuándo llegó de la red y cuándo salió del decodificador, para su ritmo).
    Grillas::PedidoCelda pedirCelda(int w, int h, std::int64_t redUs = 0, std::int64_t decoUs = 0);
    void guardarCelda(Grillas::PedidoCelda&& p);
    void soltarCelda(Grillas::PedidoCelda&& p);
    /// ¿Se puede achicar en la placa? (`gateway.celdaEnPlaca`; apagado: del cuadro bajado).
    static bool celdaEnPlaca();

    /// SONDAS (diag/SondasCuadros.hpp; sin BA_SONDAS_CUADROS no hacen nada): el ritmo de cada
    /// cuadro de esta cámara al llegar de la red (su primer paquete) y al salir del
    /// decodificador (los dos en µs de libwebrtc), y el de lo publicado entero a ba-app.
    void anotarLlegada(std::int64_t redUs, std::int64_t decoUs) {
        BA_SONDA(if (redUs > 0) ritmoRed_.cuadro(redUs); if (decoUs > 0) ritmoDeco_.cuadro(decoUs);)
        (void)redUs;
        (void)decoUs;
    }
    /// {red, deco, entero: {cps, p50_ms, p95_ms, max_ms, huecos (> 1,5 × la mediana), n}}; vacío sin sondas.
    nlohmann::json ritmo() const;

    const std::string& persona() const { return persona_; }
    int sesion() const { return sesion_; }

   private:
    MediaPump* pump_;
    int sesion_;
    Grillas* grillas_;
    std::string persona_;
    BA_SONDA(ba::diag::RitmoCuadros ritmoRed_; ba::diag::RitmoCuadros ritmoDeco_; ba::diag::RitmoCuadros ritmoEntero_;)
};

} // namespace ba::gateway
