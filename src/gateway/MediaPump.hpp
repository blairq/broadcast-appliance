// ============================================================================
// src/gateway/MediaPump.hpp — Motor de medios de B (IPC <-> WebRTC)
//
// Proposito:
//   Mueve los medios entre las sesiones WebRTC de los participantes y los
//   canales de memoria compartida con A. QUE va a DONDE lo decide la ruta de
//   cada sesion (MediaRoute), que fija la sala; admitir a alguien cambia su
//   ruta sin tocar su conexion.
//
//   Subida (participante -> B -> A):
//     Program(n): camara  -> BA.Guest.<n>        (tamaño del programa)
//                 pantalla-> BA.GuestShare.<n>   (tamaño del programa)
//                 audio   -> BA.Audio.Guest.<n>
//     Lobby(k):   camara  -> BA.Lobby.<k>        (preview chico, solo operador)
//     Backstage(k): camara -> BA.Backstage.<k>   (fila de oradores: preview
//                  chico a ~10 fps, solo operador)
//                 audio   -> BA.Audio.Lobby.<k>  (solo si el operador le habla)
//   Bajada (A -> B -> participante):
//     Program(n): BA.Program + BA.Audio.ContentForReturn
//     Lobby(k):   BA.Operator (o una placa si A no lo publica) + la voz del
//                 operador BA.Audio.Operator solo si talk
//
//   Un sender existe siempre (spec §4.2): mientras un slot del programa no
//   tiene video del invitado, B publica una placa.
//
// Threading:
//   - 4 threads de bajada (audio/video del programa, audio/video del operador)
//     con reintento de apertura cada 500 ms y resuscripcion si A se reinicia.
//   - 1 thread de placas.
//   - forwardAudio/forwardVideo/forwardScreen: desde los threads de libwebrtc;
//     un lock por canal de salida.
//   - setRoute: desde la sala; la tabla de rutas tiene su propio mutex.
// ============================================================================
#pragma once

#include <atomic>
#include <deque>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "dsp/Resampler.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "MediaRoute.hpp"
#include "Webrtc.hpp"

namespace ba::gateway {

class Log;

class MediaPump {
   public:
    struct Cfg {
        std::wstring returnAudioName;   // BA.Audio.ContentForReturn (A->B)
        // Mix-minus por slot (A->B): el programa sin la voz de ese invitado.
        // Indexado por slot, [0] reservado. Si el de un slot no está, ese
        // invitado recibe el retorno común.
        std::vector<std::wstring> returnSlotNames; // BA.Audio.Return.<n>
        std::wstring programVideoName;  // BA.Program (A->B)
        std::wstring operatorVideoName; // BA.Operator (A->B, para el lobby)
        std::wstring operatorAudioName; // BA.Audio.Operator (A->B, para el lobby)
        // Indexados por slot, [0] reservado (tamaño guests+1 / lobbySlots+1).
        std::vector<std::wstring> guestAudioNames;     // BA.Audio.Guest.<n>
        std::vector<std::wstring> guestVideoNames;     // BA.Guest.<n>
        std::vector<std::wstring> shareVideoNames;     // BA.GuestShare.<n>
        std::vector<std::wstring> lobbyAudioNames;     // BA.Audio.Lobby.<k>
        std::vector<std::wstring> lobbyVideoNames;     // BA.Lobby.<k>
        std::vector<std::wstring> backstageVideoNames; // BA.Backstage.<k>
        std::vector<std::wstring> remotoAudioNames;    // BA.Audio.Remoto.<k> (R3)
        std::vector<std::wstring> remotoVideoNames;    // BA.Remoto.<k>
        int audioRate = 48000;
        int audioChannels = 2;
        int programW = 1280; // tamaño de los canales del programa (camara/pantalla)
        int programH = 720;
        int lobbyW = 640; // preview del lobby
        int lobbyH = 360;
        int backstageW = 320; // preview de la fila de oradores
        int backstageH = 180;
        int guestVideoW = 640; // retorno al participante
        int guestVideoH = 360;
        // El retorno del PROGRAMA va aparte y mas grande. 640x360 alcanza para
        // el monitor de confianza de alguien que mira su celular, pero la misma
        // imagen en un televisor de 55" se ve mal, y una pantalla de sala es
        // justamente eso. Nunca se agranda mas alla del original.
        int programVideoW = 1280;
        int programVideoH = 720;
    };

    MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log);
    ~MediaPump();

    MediaPump(const MediaPump&) = delete;
    MediaPump& operator=(const MediaPump&) = delete;

    void start();
    void stop();

    /// Ruta de la sesion `session` (1..hub.sessions()).
    void setRoute(int session, const MediaRoute& route);
    MediaRoute route(int session) const;

    /// Subida desde la sesion `session` (se rutea segun su MediaRoute).
    void forwardAudio(int session, const float* samples, size_t n, int rate);
    void forwardVideo(int session, const uint8_t* pixels, size_t bytes, int w, int h);
    void forwardScreen(int session, const uint8_t* pixels, size_t bytes, int w, int h);

    /// Prueba técnica de un invitado admitido (T33): durante `ms` le suma al retorno un
    /// tono de 1 kHz cortado (250 ms sí, 250 no) y mira cuánto de ese tono vuelve por su
    /// micrófono (correlación con el patrón, a cualquier demora hasta 1 s). Bloquea `ms`.
    /// {eco_db (cuánto más 1 kHz hay con el tono que sin), demora_ms, eco: bool, red: {…}}.
    std::string pruebaTecnica(int session, int ms);
    /// El código seudoaleatorio de la prueba (tramos de 250 ms): true = tono.
    static bool codigoPrueba(long long tramo);
    /// El análisis de la prueba: energía de 1 kHz del micrófono en trozos de 20 ms
    /// (-1 = sin dato) contra el código, a demoras de 0 a 1 s. {eco_db, eco, demora_ms…}.
    static nlohmann::json analizarEco(const std::vector<double>& bins, int dur);

   private:
    /// Canal de video B->A con tamaño fijo (el frame se ajusta adentro).
    struct VideoOut {
        ba::ipc::SpoutSender sender;
        std::mutex lock;
        std::vector<uint8_t> scratch;
        int w = 0;
        int h = 0;
        std::atomic<uint64_t> lastNs{0}; // ultimo frame REAL (no placa)
        uint64_t minIntervalNs = 0;      // 0 = todos los cuadros; si no, tope de fps
        // A cuantos cuadros por segundo esta LLEGANDO lo que sube el
        // participante. Es la mitad que faltaba para saber donde se va el
        // retardo: si el telefono entrega 30 parejos, lo que se ve tarde se
        // esta tardando mas adelante; si entrega 12 a los tirones, el camino
        // de subida es el problema y no hay nada que apurar de este lado.
        std::string nombre;
        uint64_t informeNs = 0;
        uint64_t cuadros = 0;
    };
    /// Canal de audio B->A: ring + re-bloqueo a 10 ms + re-muestreo.
    struct AudioOut {
        ba::ipc::SharedAudioRing ring;
        std::mutex lock;
        std::vector<float> buf;
        size_t pos = 0;
        std::unique_ptr<ba::dsp::Resampler> resampler;
        int resamplerRate = 0;
        std::vector<float> resampleOut;
    };

    void returnAudioThread();
    void returnVideoThread();
    void operatorAudioThread();
    void operatorVideoThread();
    void plateThread();

    void publishVideo(VideoOut& out, const uint8_t* pixels, int w, int h, bool real);
    void publishAudio(AudioOut& out, const float* samples, size_t n, int rate);
    /// Sesiones cuya ruta cumple `pred` (copia de la tabla bajo lock).
    template <typename Pred> std::vector<int> sessionsWhere(Pred pred) const;

    WebrtcHub& hub_;
    Cfg cfg_;
    Log& log_;
    std::atomic<bool> stop_{false};
    std::vector<std::thread> threads_;

    // [0] reservado en todos.
    std::vector<std::unique_ptr<VideoOut>> guestVideo_;
    std::vector<std::unique_ptr<VideoOut>> shareVideo_;
    std::vector<std::unique_ptr<VideoOut>> lobbyVideo_;
    std::vector<std::unique_ptr<VideoOut>> backstageVideo_;
    std::vector<std::unique_ptr<AudioOut>> guestAudio_;
    std::vector<std::unique_ptr<AudioOut>> lobbyAudio_;
    std::vector<std::unique_ptr<VideoOut>> remotoVideo_;
    std::vector<std::unique_ptr<AudioOut>> remotoAudio_;

    // IFB: la voz del operador para los de adentro que tienen «hablar» prendido; el
    // hilo del retorno la mezcla con el programa (una sola fuente por sesión).
    std::mutex ifbLock_;
    std::deque<float> ifb_;
    // Prueba técnica: la sesión que se prueba, la fase del tono y la energía de 1 kHz
    // que vuelve por su micrófono, en trozos de 20 ms desde que empezó.
    std::atomic<int> pruebaSesion_{0};
    std::atomic<std::int64_t> pruebaDesdeNs_{0};
    double pruebaFase_ = 0;
    std::mutex pruebaLock_;
    std::vector<double> pruebaBins_;

    mutable std::mutex routesLock_;
    std::vector<MediaRoute> routes_; // por sesion, [0] reservado
};

} // namespace ba::gateway
