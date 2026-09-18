// ============================================================================
// src/gateway/MediaPump.hpp — Motor de medios del Proceso B (IPC → WebRTC)
//
// Proposito:
//   Conecta los flujos IPC (ba_core::ipc) con los tracks WebRTC de cada
//   invitado (spec §8.5, §8.6). Dos direcciones:
//
//   RETORNO (A → B → invitado):
//     - AUDIO : consume el ring `BA.Audio.ContentForReturn` (A→B, el audio de
//               presentacion/reproduccion) con SharedAudioRing::openReader/pop
//               y lo inyecta en el track local de audio de cada invitado
//               (pushLocalAudio).
//     - VIDEO : consume el sender `BA.Program` (A→B, el video de programa) con
//               SpoutReceiver/subscribe/receive, lo escala a la resolucion del
//               retorno (640x360) y lo inyecta en el track local de video de
//               cada invitado (pushLocalVideo).
//
//   UPLINK (invitado → B → A):
//     - AUDIO : la voz del invitado (onRemoteAudio) se publica en el ring
//               `BA.Audio.Guest.<n>` (B→A) para el mix de YouTube.
//     - VIDEO : la camara del invitado (onRemoteVideo) se publica en el sender
//               Spout `BA.Guest.<n>` (B→A) para que A la compuesta.
//   "Un sender existe siempre" (spec §4.2): B publica BA.Guest.<n> y el ring
//   BA.Audio.Guest.<n> para cada slot, incluso sin invitado (placa/silencio).
//
// Modelo de threading:
//   - Un thread de RETORNO DE AUDIO: abre el ring de retorno, pop() bloques de
//     10 ms y los inyecta en las sesiones conectadas. Pacing de 10 ms (tiempo
//     real) para no adelantar al productor (A).
//   - Un thread de RETORNO DE VIDEO: se suscribe a BA.Program, receive() frames
//     nuevos, escala y los inyecta. Pacing de ~33 ms (30 fps).
//   - forwardAudio/forwardVideo se llaman desde los callbacks de las sesiones
//     (en el backend real, desde los threads de libwebrtc; en el NULL nunca se
//     llaman). Toman un lock por slot para el buffer de re-bloqueo a 480 frames.
//   - Los threads se detienen por un std::atomic<bool>; no lanzan.
//
// Estrategia de errores:
//   No lanza. Si A no esta corriendo (ring/sender ausentes), los threads de
//   retorno reintentan la apertura/suscripcion cada 500 ms y siguen vivos. Un
//   pop/receive fallido se loguea y se sigue (degradacion elegante).
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp/Resampler.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "Webrtc.hpp"

namespace ba::gateway {

class Log;

/// Motor de medios: enruta IPC ↔ WebRTC por invitado. Ver el header.
class MediaPump {
   public:
    /// Configuracion de los nombres de flujos IPC y resolucion.
    struct Cfg {
        std::wstring returnAudioName;  // BA.Audio.ContentForReturn (A→B)
        std::wstring programVideoName; // BA.Program (A→B)
        // Indexados POR SLOT: el elemento [n] es el nombre del invitado del
        // slot n (1..guests) y el [0] es un hueco reservado (no hay slot 0).
        // El tamaño esperado es guests+1; un vector mas corto deja invitados
        // sin canal (start() lo loguea como warning).
        std::vector<std::wstring> guestAudioNames; // [n] = BA.Audio.Guest.<n> (B→A)
        std::vector<std::wstring> guestVideoNames; // [n] = BA.Guest.<n> (B→A)
        int audioRate = 48000;
        int audioChannels = 2;
        int programW = 1280;
        int programH = 720;
        int guestVideoW = 640;
        int guestVideoH = 360;
    };

    /// :param hub: hub de sesiones al que se inyecta el retorno.
    /// :param cfg: nombres de flujos IPC y resolucion.
    /// :param log: logger compartido.
    MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log);
    ~MediaPump();

    MediaPump(const MediaPump&) = delete;
    MediaPump& operator=(const MediaPump&) = delete;

    /// Crea los canales de salida (B→A): ring BA.Audio.Guest.<n> (writer) y
    /// sender Spout BA.Guest.<n> por slot, y arranca los threads de retorno.
    void start();

    /// Detiene los threads de retorno y cierra los canales de salida.
    void stop();

    /// Publica audio de uplink del invitado `slot` en su ring BA.Audio.Guest.
    /// Se llama desde onRemoteAudio de la sesion. Re-bloquea a 480 frames.
    void forwardAudio(int slot, const float* samples, size_t n, int rate);

    /// Publica video de uplink del invitado `slot` en su sender BA.Guest.
    /// Se llama desde onRemoteVideo de la sesion.
    void forwardVideo(int slot, const uint8_t* pixels, size_t bytes, int w, int h);

   private:
    void returnAudioThread();
    void returnVideoThread();

    WebrtcHub& hub_;
    Cfg cfg_;
    Log& log_;
    std::atomic<bool> stop_{false};

    std::thread audioThread_;
    std::thread videoThread_;

    // Canales de salida (B→A), uno por slot (1..guests). 0 reservado.
    std::vector<ba::ipc::SharedAudioRing> outAudio_;
    std::vector<ba::ipc::SpoutSender> outVideo_;

    // Buffer de re-bloqueo de audio de uplink a bloques de 480 frames, por slot.
    std::vector<std::vector<float>> audioBuf_;
    // Posicion de lectura dentro de audioBuf_[slot]. Antes cada bloque
    // publicado hacia un erase() desde el frente, es decir un memmove de todo
    // el resto, dentro del callback de libwebrtc y bajo el lock del slot. Con
    // un indice, publicar es O(1) y el buffer se compacta de vez en cuando.
    std::vector<size_t> audioBufPos_;
    // Re-muestreador por slot: el invitado puede entregar a 44.1 kHz y el ring
    // B→A es de 48 kHz fijos. Antes el `rate` se ignoraba (`(void)rate`) y el
    // audio se publicaba crudo, o sea desafinado y a velocidad equivocada.
    std::vector<std::unique_ptr<ba::dsp::Resampler>> audioResampler_;
    std::vector<int> audioResamplerRate_; // rate de entrada configurado
    std::vector<std::vector<float>> audioResampleIn_;
    std::vector<std::vector<float>> audioResampleOut_;
    // unique_ptr<mutex>: std::mutex no es copiable ni movible, y cualquier op
    // que haga crecer un std::vector<std::mutex> (emplace_back/resize) debe
    // reubicar los elementos existentes (instantia el move/copy borrado). Detras
    // del unique_ptr el vector crece moviendo punteros (no los mutex).
    std::vector<std::unique_ptr<std::mutex>> audioBufLock_;
};

} // namespace ba::gateway
