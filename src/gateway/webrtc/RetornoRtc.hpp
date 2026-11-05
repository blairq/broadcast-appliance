// ============================================================================
// src/gateway/webrtc/RetornoRtc.hpp — el retorno compartido por dentro (solo con libwebrtc)
//
// Lo usan Webrtc.cpp (la fábrica de codificadores, la fuente de cada sesión) y
// webrtc/RetornoRtc.cpp (flujos, capas y el codificador de paso). Ver RetornoCompartido.hpp.
// ============================================================================
#pragma once


#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace ba::gateway::retorno {

class Flujo;
struct Emision;

/// Lo que una sesión le dice a su codificador de paso (el tope del operador) y lo que él le
/// cuenta (para sus estadísticas). Lo comparten la sesión y cada cuadro que manda.
struct EstadoSesion {
    std::atomic<int> topeAlto{0};             ///< 0 = sin tope (calidad-max)
    std::atomic<int> capaAlto{0};             ///< el alto de la capa que sale (0 = nada todavía)
    std::atomic<std::int64_t> objetivoBps{0}; ///< lo que le da su control de congestión
    std::atomic<int> fuente{0};               ///< 0 nada, 1 compartida, 2 propia (sin flujo)
    std::atomic<std::uint64_t> cambiosCapa{0};
    std::atomic<std::uint64_t> cuadrosCompartidos{0};
    std::atomic<std::uint64_t> cuadrosPropios{0};
    std::atomic<std::uint64_t> sondeos{0};

    /// Sondear la red de la conexión: que el control de congestión pruebe si entra `hastaBps`
    /// (sube el tope de su estimación de `desdeBps` a `hastaBps`, y libwebrtc sondea al subir
    /// el tope). Lo pone la sesión con su PeerConnection; lo llama el codificador de paso
    /// cuando quiere subir de capa y su objetivo no alcanza (una conexión que manda una capa
    /// de bitrate fijo no hace crecer su estimación sola).
    void sondear(std::int64_t desdeBps, std::int64_t hastaBps) {
        std::function<void(std::int64_t, std::int64_t)> f;
        {
            std::lock_guard<std::mutex> l(m);
            f = sondeo;
        }
        if (f) {
            sondeos.fetch_add(1, std::memory_order_relaxed);
            f(desdeBps, hastaBps);
        }
    }
    void ponerSondeo(std::function<void(std::int64_t, std::int64_t)> f) {
        std::lock_guard<std::mutex> l(m);
        sondeo = std::move(f);
    }

   private:
    std::mutex m;
    std::function<void(std::int64_t, std::int64_t)> sondeo;
};

/// El cuadro que cada sesión le pasa a su fuente de video: NATIVO, para que libwebrtc lo
/// entregue tal cual al codificador de paso, que reconoce la emisión que lleva. Lleva el I420
/// (el mismo para todas las sesiones) por si alguien lo pide crudo y para el codificador
/// propio de un cuadro sin flujo (pushLocalVideo de a una sesión).
///
/// Es el ÚNICO buffer nativo que llega a un codificador del gateway: las fuentes de las
/// sesiones solo emiten estos y las de los espectadores no emiten nada.
class BufferRetorno : public webrtc::VideoFrameBuffer {
   public:
    BufferRetorno(rtc::scoped_refptr<webrtc::I420BufferInterface> i420, std::shared_ptr<Emision> emision,
                  std::shared_ptr<EstadoSesion> estado)
        : i420_(std::move(i420)), emision_(std::move(emision)), estado_(std::move(estado)) {}
    Type type() const override { return Type::kNative; }
    int width() const override { return i420_ ? i420_->width() : 0; }
    int height() const override { return i420_ ? i420_->height() : 0; }
    rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override { return i420_; }
    const rtc::scoped_refptr<webrtc::I420BufferInterface>& i420() const { return i420_; }
    const std::shared_ptr<Emision>& emision() const { return emision_; }
    const std::shared_ptr<EstadoSesion>& estado() const { return estado_; }

   private:
    rtc::scoped_refptr<webrtc::I420BufferInterface> i420_;
    std::shared_ptr<Emision> emision_;
    std::shared_ptr<EstadoSesion> estado_;
};

/// Un flujo nuevo: codifica con los codificadores de `interna` (VP8 de libvpx, H.264 de
/// VideoToolbox en la Mac).
std::shared_ptr<Flujo> crearFlujo(std::string nombre, std::shared_ptr<webrtc::VideoEncoderFactory> interna);
/// Una emisión del flujo: este cuadro, que se codifica en las capas que alguien usa.
std::shared_ptr<Emision> emitir(const std::shared_ptr<Flujo>& flujo, rtc::scoped_refptr<webrtc::I420BufferInterface> i420);
/// Para los hilos de sus capas (al cerrar el hub, antes de soltar la fábrica).
void parar(Flujo& flujo);
/// {nombre, emisiones, capas: [{codec, alto, ancho, kbps, en_uso, cuadros, claves, pedidos_clave,
/// descartados, kbps_medio (desde que se creó)}]}
nlohmann::json estado(const Flujo& flujo);

/// La fábrica de las PeerConnection: los formatos de `interna`, pero cada codificador que
/// pide una sesión es de PASO (entrega lo que codificó su flujo).
std::unique_ptr<webrtc::VideoEncoderFactory> fabricaDePaso(std::shared_ptr<webrtc::VideoEncoderFactory> interna);

} // namespace ba::gateway::retorno

