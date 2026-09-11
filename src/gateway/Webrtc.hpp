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

namespace ba::gateway {

/// Callbacks que una sesion invoca. Ver Webrtc.hpp para el detalle de cada uno.
struct SessionCallbacks {
    /// B genero el answer tras recibir el offer del invitado. `answerSdp` es la
    /// seccion sdp del answer (sin el tipo, que el servidor envia aparte).
    std::function<void(const std::string& answerSdp)> onLocalAnswer;
    /// B genero un candidato ICE local a enviar al invitado (JSON del candidato).
    std::function<void(const std::string& iceCandidate)> onLocalIce;
    /// Llego audio del invitado (track remoto). `samples` es float32 intercalado
    /// a `rate` Hz; `n` es la cantidad de floats.
    std::function<void(const float* samples, size_t n, int rate)> onRemoteAudio;
    /// Llego video del invitado (track remoto). `pixels` es BGRA; `w`x`h`.
    std::function<void(const uint8_t* pixels, size_t bytes, int w, int h)> onRemoteVideo;
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
    /// :param candidate: JSON del candidato (campo "candidate").
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

    /// Diagnosticos: bloques de audio / frames de video de retorno inyectados.
    uint64_t localAudioBlocks() const;
    uint64_t localVideoFrames() const;

   private:
    struct Impl;
    Impl* impl_;
};

/// Hub de sesiones: 1..N invitados en estrella (spec §8.2).
///
/// Posee las sesiones (indice 1..guests; 0 reservado) y, en el backend real,
/// la webrtc::PeerConnectionFactory compartida. El servidor de senalizacion
/// obtiene una sesion por slot y la configura con los callbacks de la conexion.
class WebrtcHub {
   public:
    /// :param guests: cantidad de invitados (1..4).
    /// :param videoW/videoH: resolucion del retorno de video al invitado.
    /// :param audioRate: sample rate del retorno de audio.
    /// :param audioChannels: canales del retorno de audio.
    WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels);
    ~WebrtcHub();

    WebrtcHub(const WebrtcHub&) = delete;
    WebrtcHub& operator=(const WebrtcHub&) = delete;

    /// Devuelve la sesion del slot (1..guests). Fuera de rango: nullptr.
    WebrtcSession* session(int slot);

    /// Cantidad de invitados.
    int guests() const { return guests_; }

    /// Cierra todas las sesiones (shutdown ordenado).
    void closeAll();

    /// webrtc::PeerConnectionFactory* (backend real) o nullptr (NULL).
    void* rtcFactory() const { return rtcFactory_; }

   private:
    int guests_;
    int videoW_, videoH_, audioRate_, audioChannels_;
    std::vector<WebrtcSession> sessions_;
    void* rtcFactory_ = nullptr; // backend real: webrtc::PeerConnectionFactory*
};

} // namespace ba::gateway
