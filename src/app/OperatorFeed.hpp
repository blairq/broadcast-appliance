// ============================================================================
// src/app/OperatorFeed.hpp — Camara y voz del operador para el LOBBY
//
// Proposito:
//   Quien espera en el lobby ve SOLO al operador (nunca el programa ni a los
//   demas que esperan). A le publica a B:
//     - BA.Operator         video de la fuente que el operador elija (su
//                           camara), a 640x360, desde una vista PROPIA de
//                           libobs (obs_view con su mezcla, escalada en GPU):
//                           no toca el programa ni su lienzo;
//     - BA.Audio.Operator   su microfono, capturado directo de la fuente
//                           (antes de la mezcla del programa).
//   B decide a quien le llega (video a todo el lobby; voz solo a con quien el
//   operador esta hablando).
//
//   Y a la inversa, el MONITOREO: la voz de quien espera (BA.Audio.Lobby.<k>)
//   se escucha por los auriculares del operador (monitoreo, nunca la mezcla
//   del programa) solo mientras le esta hablando.
//
// Threading: configure/stop/setLobbyMonitor desde el thread de la interfaz;
// los callbacks de video/audio llegan desde threads de libobs.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

struct obs_scene;
struct obs_source;
struct obs_view;
struct video_output;
struct video_data;
struct audio_data;

namespace ba::app {

class BroadcastEngine;

class OperatorFeed {
   public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 360;
    static constexpr int kMaxLobby = 8;

    OperatorFeed() = default;
    ~OperatorFeed();
    OperatorFeed(const OperatorFeed&) = delete;
    OperatorFeed& operator=(const OperatorFeed&) = delete;

    /// Arma (o rearma) el feed con fuentes EXISTENTES del motor, por nombre.
    /// Vacio = sin video / sin voz (B muestra una placa de espera).
    /// :returns: false si alguna fuente no existe (el resto queda armado).
    bool configure(BroadcastEngine& engine, const std::string& videoSource, const std::string& audioSource);
    void stop();
    /// stop() + libera el monitoreo. Llamar ANTES de apagar libobs.
    void shutdown();

    /// Crea las fuentes de monitoreo del lobby (1..count), silenciadas.
    void startLobbyMonitor(int count);
    /// Escuchar (o no) a quien ocupa el lugar `slot` del lobby.
    void setLobbyMonitor(int slot, bool on);

    // --- Push-to-talk (docs/AUDIO-TTS.md) -------------------------------------
    /// Mono en float con su frecuencia: el oído del control por voz.
    using Dictado = std::function<void(const float* mono, std::size_t frames, std::uint32_t frecuencia)>;
    /// A dónde va la voz mientras la tecla está apretada (thread-safe).
    void setDictado(Dictado d);
    /// Tecla apretada: la voz va al dictado y el lobby recibe SILENCIO en su
    /// lugar. Por construcción, el invitado no puede oír lo que se le pide al
    /// asistente: no es una casilla que alguien pueda olvidar.
    void setPushToTalk(bool on) { ptt_ = on; }
    bool pushToTalk() const { return ptt_; }
    /// Si hay un micrófono del operador elegido (Sala): sin él no hay dictado.
    bool hayMicrofono() const { return audioSrc_ != nullptr; }

    const std::string& videoSource() const { return videoName_; }
    const std::string& audioSource() const { return audioName_; }

   private:
    static void onVideo(void* param, struct video_data* frame);
    static void onAudio(void* param, struct obs_source* source, const struct audio_data* data, bool muted);
    void stopVideo();
    void stopAudio();

    // Video
    struct obs_scene* scene_ = nullptr;
    struct obs_view* view_ = nullptr;
    struct video_output* video_ = nullptr;
    ba::ipc::SpoutSender sender_;
    std::vector<std::uint8_t> packed_;
    std::uint64_t frameIndex_ = 0;
    std::string videoName_;

    // Audio
    struct obs_source* audioSrc_ = nullptr;
    ba::ipc::SharedAudioRing ring_;
    std::mutex audioLock_;
    std::vector<float> audioBuf_; // estereo intercalado pendiente de bloque
    std::string audioName_;
    std::uint32_t frecuencia_ = 48000;
    std::atomic<bool> ptt_{false};
    std::mutex dictadoLock_;
    Dictado dictado_;

    // Monitoreo del lobby
    std::vector<struct obs_source*> lobbyMonitors_;
};

} // namespace ba::app
