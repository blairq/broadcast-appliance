// src/gateway/webrtc/CodecsVideo.hpp — Las fábricas de video del gateway, por plataforma. Ver CodecsVideo.cpp.
#pragma once


#include <cstdint>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace ba::gateway {
class DestinoCamara;
}

namespace ba::gateway::codecs {

/// F3: un cuadro decodificado que todavía está en la placa de video (Windows: la superficie
/// D3D11 del decodificador, CodecsWin.cpp). El decodificador lo anuncia EN SU HILO mientras
/// llama a Decoded() (AnuncioPlaca); como sin el suavizado previo al render el cuadro llega al
/// sink en ese mismo hilo y antes de que Decoded() vuelva, entregarCamara lo reconoce (mismo
/// buffer) y le pide solo lo que hace falta: la celda achicada EN LA PLACA (se baja lo chico) o
/// el cuadro entero en BGRA sin pasar por I420. Sin RTTI: por eso el anuncio y no un cast.
class CuadroEnPlaca {
   public:
    /// El buffer que viaja en el VideoFrame (para reconocerlo).
    virtual const webrtc::VideoFrameBuffer* buffer() const = 0;
    /// El cuadro achicado a `tw`x`th` en BGRA (filas de `paso` bytes), achicado en la placa.
    /// false si no se pudo (entonces se hace en el procesador).
    virtual bool celdaBgra(int tw, int th, std::uint8_t* dst, int paso) = 0;
    /// El cuadro entero en BGRA (filas de `paso` bytes), del NV12 bajado directo. false si no se pudo.
    virtual bool enteroBgra(std::uint8_t* dst, int paso) = 0;
    /// Los dos en una sola vuelta de la placa (una espera): el entero como enteroBgra y la
    /// celda `tw`x`th` achicada allá como celdaBgra. `celdaHecha` dice si salió la celda (si
    /// no, se achica del entero en el procesador). false si no salió el entero.
    virtual bool enteroYCelda(std::uint8_t* dst, int paso, int tw, int th, std::uint8_t* celda, int pasoCelda,
                              bool& celdaHecha) = 0;

   protected:
    ~CuadroEnPlaca() = default;
};

/// Mientras vive, `c` es el cuadro en la placa de este hilo (uno por vez).
class AnuncioPlaca {
   public:
    explicit AnuncioPlaca(CuadroEnPlaca* c);
    ~AnuncioPlaca();
    AnuncioPlaca(const AnuncioPlaca&) = delete;
    AnuncioPlaca& operator=(const AnuncioPlaca&) = delete;

   private:
    CuadroEnPlaca* anterior_;
};

/// F3 (#6 y #8 verticales): lo que hace el sink de la cámara de una sesión con cada cuadro,
/// en el hilo que lo decodificó. Pregunta antes de convertir nada: si ba-app no lo publica
/// ahora y la grilla no lo usa, no se toca; la celda de la grilla se achica del cuadro en la
/// placa, del NV12 o del I420 (lo que tenga menos vueltas) y a `grillas` solo le llega lo chico;
/// el BGRA entero (en `bgra`, que se reusa) solo si ba-app lo publica. Lo llama
/// RemoteVideoSink::OnFrame (Webrtc.cpp) con la capa que sale.
void entregarCamara(const webrtc::VideoFrame& frame, DestinoCamara& destino, std::vector<std::uint8_t>& bgra);

/// Codificadores: macOS VideoToolbox (H.264) + VP8/VP9/AV1; Windows Media Foundation
/// (H.264, CodecsWin) + VP8/VP9/AV1; Linux VP8/VP9/AV1 por software.
std::unique_ptr<webrtc::VideoEncoderFactory> codificadores();

/// Decodificadores, los mismos por plataforma. Todos envueltos: los de software con
/// `gateway.hilosPorDecodificador` hilos internos (1 por omisión), y cada uno anota qué
/// implementación usa y si es por hardware (log y `estado()`).
std::unique_ptr<webrtc::VideoDecoderFactory> decodificadores();

/// Lo que elige «auto» en `retorno.codec`: true = H.264 (macOS y Windows con H.264), false = VP8.
bool h264PorOmision();

/// Para el estado del gateway (`uso.codecsVideo` de /api/v1/room → room_status):
/// {plataforma, auto, h264: {decodifica, codifica, decodificador, codificador, placa, caidas},
///  hilos_por_decodificador, decodificadores: [{codec, implementacion, hardware, activos}]}.
nlohmann::json estado();

} // namespace ba::gateway::codecs

