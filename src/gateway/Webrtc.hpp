// ============================================================================
// src/gateway/Webrtc.hpp — Sesiones WebRTC por invitado (libwebrtc M123 + NULL)
//
// Proposito:
//   Encapsula una PeerConnection libwebrtc por invitado (spec §8.1, §8.2) y el
//   hub que las agrupa (1..4 invitados, estrella con B como hub). B es el
//   ANSWERER del offer del invitado: el invitado (navegador) crea la
//   RTCPeerConnection, agrega sus tracks locales (camara + micro) y manda el
//   offer por la senalizacion; B setea el remote description, agrega sus tracks
//   LOCALES (el retorno de audio/video que le envia a ese invitado) y devuelve
//   el answer. Los candidatos ICE fluyen en ambos sentidos.
//
//   Tracks locales de B (lo que el invitado recibe, spec §8.5):
//     - AUDIO : el retorno de audio (ring BA.Audio.ContentForReturn, A→B), que
//               el MediaPump inyecta con pushLocalAudio().
//     - VIDEO : el video de programa (BA.Program, A→B), que el MediaPump
//               inyecta con pushLocalVideo().
//   Tracks remotos del invitado (lo que B recibe, spec §8.5/§8.6):
//     - AUDIO : la voz del invitado -> onRemoteAudio (el hub la publica en el
//               ring BA.Audio.Guest.<n> para el mix de A).
//     - VIDEO : la camara del invitado -> onRemoteVideo (el hub la publica en
//               el sender Spout BA.Guest.<n> para A).
//
// Espectadores (relay estilo SFU):
//   Las vistas web (consola del operador, tablero de oradores) ven el video de
//   los invitados SIN que el invitado suba una copia por espectador y SIN que B
//   re-codifique: B toma los cuadros CODIFICADOS tal como llegan (antes del
//   decodificador, que sigue alimentando a A) y reenvia esos mismos bytes a la
//   PeerConnection de cada espectador (solo cambia el payload type al que
//   negocio cada uno). El invitado sube una vez; B copia paquetes, no pixeles.
//     - Un espectador nuevo arranca en un keyframe: hasta que pasa uno se le
//       saltean los cuadros delta, y se le pide un keyframe al invitado.
//     - Perdidas: los NACK del espectador los atiende el historial de envio de
//       B (retransmite lo que ya reenvio). Si igual se congela, la vista pide
//       un keyframe (requestKeyFrame) y B lo pide aguas arriba (limitado).
//
// Backend:
//   - BA_GATEWAY_HAVE_WEBRTC=1 (Windows + SDK libwebrtc presente): usa
//     webrtc::PeerConnectionInterface real (AEC3, NetEq, control de congestión).
//   - BA_GATEWAY_HAVE_WEBRTC=0 (Linux / build offline): backend NULL: la
//     sesion es una maquina de estados que completa la senalizacion
//     (offer->answer) para que el flujo sea testeable sin el SDK. No hay RTP
//     real, por lo que onRemoteAudio/onRemoteVideo no se disparan.
//
// Modelo de threading:
//   - La senalizacion (setRemoteOffer/addRemoteIce) se llama desde el thread de
//     la conexion WS. En el backend real, libwebrtc despacha los eventos de
//     ICE/RTP desde SUS threads (signaling/worker), asi que los callbacks
//     (onLocalAnswer/onLocalIce/onRemoteAudio/onRemoteVideo/onStateChange)
//     pueden llamarse desde un thread distinto al que hizo el set: el caller
//     debe ser thread-safe (el servidor guarda un mutex por conexion para el
//     send; el MediaPump y el hub toman sus propios locks al publicar).
//   - pushLocalAudio/pushLocalVideo se llaman desde los threads del MediaPump.
//
// Estrategia de errores:
//   No lanza en el nucleo. configure/setRemoteOffer/addRemoteIce devuelven bool.
//   Un fallo de libwebrtc se loguea (el caller lo hace) y la sesion queda en un
//   estado coherente (no conectada). Ninguna excepcion cruza el borde de un
//   callback de libwebrtc: en el backend real se captura en el borde.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

/// Callbacks que una sesion invoca. Ver Webrtc.hpp para el detalle de cada uno.
struct SessionCallbacks {
    /// B genero el answer tras recibir el offer del invitado. `answerSdp` es la
    /// seccion sdp del answer (sin el tipo, que el servidor envia aparte).
    std::function<void(const std::string& answerSdp)> onLocalAnswer;
    /// B genero un candidato ICE local a enviar al invitado: objeto JSON
    /// {candidate, sdpMid, sdpMLineIndex} serializado (lo que espera
    /// RTCPeerConnection.addIceCandidate del navegador).
    std::function<void(const std::string& iceCandidateJson)> onLocalIce;
    /// Llego audio del invitado (track remoto). `samples` es float32 ESTEREO
    /// intercalado a `rate` Hz (el mono del navegador se duplica); `n` es la
    /// cantidad de floats.
    std::function<void(const float* samples, size_t n, int rate)> onRemoteAudio;
    /// Llego video de la CAMARA del invitado (primer track de video). BGRA.
    std::function<void(const uint8_t* pixels, size_t bytes, int w, int h)> onRemoteVideo;
    /// Llego video de su PANTALLA/VENTANA compartida (segundo track de video).
    std::function<void(const uint8_t* pixels, size_t bytes, int w, int h)> onRemoteScreen;
    /// Cambio de estado de la sesion (true = conectada, false = cerrada).
    std::function<void(bool connected)> onStateChange;
};

/// Una PeerConnection por invitado (B es el answerer del offer del invitado).
///
/// Move-only. El backend (real o NULL) se elige en compile-time por
/// BA_GATEWAY_HAVE_WEBRTC. Ver Webrtc.hpp para el modelo de threading y la
/// estrategia de errores.
class WebrtcSession {
   public:
    /// :param slot: indice del invitado (1..maxGuests).
    /// :param videoW/videoH: resolucion del track de video local (retorno).
    /// :param audioRate: sample rate del track de audio local (retorno).
    /// :param audioChannels: canales del track de audio local.
    WebrtcSession(int slot, int videoW, int videoH, int audioRate, int audioChannels);
    /// Ctor por defecto: sesion "vacía" en el slot 0 (reservado, sin invitado).
    /// Existe para que contenedores std (p. ej. el `std::vector<WebrtcSession>`
    /// del hub) puedan default-construct los elementos antes de moverlos; el
    /// hub los sobreescribe con move-assign usando el ctor de 5 args.
    WebrtcSession();
    ~WebrtcSession();

    WebrtcSession(WebrtcSession&& other) noexcept;
    WebrtcSession& operator=(WebrtcSession&& other) noexcept;
    WebrtcSession(const WebrtcSession&) = delete;
    WebrtcSession& operator=(const WebrtcSession&) = delete;

    /// Configura los callbacks y (en el backend real) crea la PeerConnection
    /// sobre la `factory` (webrtc::PeerConnectionFactory*, del hub). En el
    /// backend NULL solo guarda los callbacks.
    ///
    /// :param cb: callbacks a invocar (answer, ICE, remote media, estado).
    /// :param factory: webrtc::PeerConnectionFactory* (backend real) o nullptr
    ///                 (backend NULL).
    /// :returns: true si quedo configurada.
    bool configure(const SessionCallbacks& cb, void* factory);

    /// Procesa el offer SDP del invitado y genera el answer (backend real:
    /// SetRemoteDescription + CreateAnswer; NULL: completa de inmediato).
    ///
    /// :param sdp: seccion sdp del offer.
    /// :returns: true si se genero el answer (disparara onLocalAnswer).
    bool setRemoteOffer(const std::string& sdp);

    /// Agrega un candidato ICE remoto del invitado.
    ///
    /// :param candidate: JSON {candidate, sdpMid, sdpMLineIndex} (o la linea
    ///                   "candidate:..." sola, que se asocia a la m-line 0).
    /// :returns: true si se agrego.
    bool addRemoteIce(const std::string& candidate);

    /// Inyecta un bloque de audio de retorno en el track local de audio.
    ///
    /// :param samples: float32 intercalado.
    /// :param n: cantidad de floats.
    /// :param rate: sample rate.
    void pushLocalAudio(const float* samples, size_t n, int rate);

    /// Inyecta un frame de video de retorno en el track local de video.
    ///
    /// :param pixels: BGRA.
    /// :param bytes: tamano en bytes.
    /// :param w/h: resolucion del frame.
    void pushLocalVideo(const uint8_t* pixels, size_t bytes, int w, int h);

    /// Indica si la sesion esta conectada (offer procesado / PC establecida).
    bool connected() const;

    /// Cierra la sesion (detiene la PC, libera tracks). Idempotente.
    void close();

    int slot() const;

    /// Pide las estadisticas de la conexion (GetStats, asincronico) y devuelve
    /// las ultimas que llegaron: {kbps_in, kbps_out, perdida_pct, jitter_ms,
    /// rtt_ms, cuadros_perdidos, alto, ancho, semaforo: verde|ambar|rojo}
    /// (JSON; "{}" si todavia no hay).
    std::string estadisticas();

    /// Diagnosticos: bloques de audio / frames de video de retorno inyectados.
    uint64_t localAudioBlocks() const;
    uint64_t localVideoFrames() const;

   private:
    struct Impl;
    Impl* impl_;
};

/// Hub de sesiones en estrella (spec §8.2): un POOL de sesiones (1..N; 0
/// reservado). El servidor asigna una sesion a cada participante que publica
/// medios, este en el lobby o adentro; A DONDE van sus medios (slot del
/// programa o preview del lobby) lo decide la tabla de rutas del MediaPump,
/// no el numero de sesion. En el backend real posee ademas la
/// webrtc::PeerConnectionFactory compartida.
class WebrtcHub {
   public:
    /// :param guests: tamaño del pool de sesiones (invitados + lobby).
    /// :param videoW/videoH: resolucion del retorno de video al invitado.
    /// :param audioRate: sample rate del retorno de audio.
    /// :param audioChannels: canales del retorno de audio.
    WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels);
    ~WebrtcHub();

    WebrtcHub(const WebrtcHub&) = delete;
    WebrtcHub& operator=(const WebrtcHub&) = delete;

    /// Devuelve la sesion `idx` (1..sessions()). Fuera de rango: nullptr.
    WebrtcSession* session(int idx);

    /// Tamaño del pool de sesiones.
    int sessions() const { return guests_; }
    /// Compatibilidad: igual a sessions().
    int guests() const { return guests_; }

    /// Cierra todas las sesiones (shutdown ordenado).
    void closeAll();

    /// webrtc::PeerConnectionFactory* (backend real) o nullptr (NULL).
    void* rtcFactory() const { return rtcFactory_; }

    /// Red de las sesiones NUEVAS: rango UDP fijo (firewall / redireccion del
    /// tunel) y si se atiende tambien en 127.0.0.1 (lo usa el puente del tunel
    /// WireGuard, que entrega los medios del invitado en loopback).
    void setNetwork(uint16_t minPort, uint16_t maxPort, bool allowLoopback);

    // --- Espectadores (relay de video codificado, ver arriba) ------------------
    /// Abre un espectador del video de la sesion `session` (camara, o pantalla
    /// si `screen`). `offerSdp` es el offer del navegador (recvonly); bloquea
    /// hasta tener el answer COMPLETO (candidatos incluidos, sin goteo) o
    /// ~3 s. :returns: id del espectador (> 0), o 0 si no se pudo (sin backend
    /// real, tope de espectadores, SDP invalido).
    int openViewer(int session, bool screen, const std::string& offerSdp, std::string& answerSdp);
    void closeViewer(int viewerId);
    /// Cierra todos los espectadores de una sesion (el participante se fue).
    void closeViewersOf(int session);
    /// La vista se congelo: pedir un keyframe al invitado (limitado a uno cada
    /// ~500 ms por video).
    void requestKeyFrame(int viewerId);
    /// Espectadores caidos (desconectados > 10 s o que nunca conectaron en
    /// 20 s) se cierran. Llamar seguido (barato).
    void reapViewers();
    int viewerCount() const;

    /// Bytes de video acumulados desde que arranco el gateway: los que mandan
    /// los invitados y los que se reenvian a los espectadores.
    ///
    /// Es el unico numero que dice si el limite de la sala es la RED y no la
    /// maquina. No incluye el audio ni el overhead de SRTP/RTP (que es poco):
    /// es el video codificado, que es lo que pesa.
    ///
    /// OJO CON DONDE SE MIDE. Se cuenta en el transformador que va entre el
    /// depaquetizador y el decodificador, o sea que cuenta el video que el
    /// gateway PROCESA. Con el sistema entero corriendo eso es todo lo que
    /// entra (A consume todos los slots), pero contra un gateway suelto -sin
    /// A del otro lado- nadie consume el video, libwebrtc no desempaqueta y
    /// esto da CERO aunque los invitados esten mandando. Es el mismo efecto
    /// que hace que la CPU de casi cero en esa situacion.
    struct Trafico {
        uint64_t entrada = 0;
        uint64_t salida = 0;
    };
    Trafico trafico() const;
    static int kMaxViewers() { return ba::ajustes::entero("room.maxVisores", 48); }

   private:
    int guests_;
    std::vector<WebrtcSession> sessions_;
    void* rtcFactory_ = nullptr; // backend real: webrtc::PeerConnectionFactory*
};

} // namespace ba::gateway
