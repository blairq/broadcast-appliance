// ============================================================================
// src/app/emision/OperatorFeed.hpp — Camara y voz del operador para el LOBBY
//
// Proposito:
//   Quien espera en el lobby ve SOLO al operador (nunca el programa ni a los
//   demas que esperan). A le publica a B:
//     - BA.Operator         video de la fuente que el operador elija (su
//                           camara), a 640x360 (con bandas), desde una vista
//                           PROPIA del motor: no toca el programa ni su lienzo;
//     - BA.Audio.Operator   su microfono, tomado de su entrada (despues de su
//                           cadena, antes de la mezcla del programa).
//   B decide a quien le llega (video a todo el lobby; voz solo a con quien el
//   operador esta hablando).
//
//   Y la voz de quien se registra en un tótem (BA.Audio.Registro.<k>, k = 1..4),
//   solo para pasarla a texto (ui/RegistroViewModel).
//
//   Y a la inversa, el MONITOREO: la voz de quien espera (BA.Audio.Lobby.<k>)
//   se escucha por los auriculares del operador (la escucha «seleccion» del
//   motor, nunca la mezcla del programa) solo mientras le esta hablando.
//
// Threading: configure/stop/setLobbyMonitor desde el thread de la interfaz;
// los cuadros y el audio llegan desde threads de GStreamer.
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

namespace ba::app {

class BroadcastEngine;

class OperatorFeed {
   public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 360;
    static constexpr int kMaxLobby = 8;
    /// Los tótems de registro que conversan a la vez (canales 1..4, como el gateway).
    static constexpr int kMaxRegistro = 4;

    OperatorFeed() = default;
    ~OperatorFeed();
    OperatorFeed(const OperatorFeed&) = delete;
    OperatorFeed& operator=(const OperatorFeed&) = delete;

    /// Arma (o rearma) el feed con fuentes EXISTENTES del motor, por nombre.
    /// Vacio = sin video / sin voz (B muestra una placa de espera).
    /// :returns: false si alguna fuente no existe (el resto queda armado).
    bool configure(BroadcastEngine& engine, const std::string& videoSource, const std::string& audioSource);
    void stop();
    /// stop() + libera el monitoreo. Llamar ANTES de apagar el motor.
    void shutdown();

    /// Crea las fuentes de monitoreo del lobby (1..count), silenciadas, y la de la
    /// atención hablada de la sesión remota.
    void startLobbyMonitor(int count);
    /// Escuchar (o no) a quien ocupa el lugar `slot` del lobby.
    void setLobbyMonitor(int slot, bool on);
    /// La voz de quien ocupa el lugar `slot` del lobby, para pasarla a texto (la
    /// atención hablada, ui/AtencionViewModel): estéreo intercalado a 48 kHz, en
    /// el hilo de GStreamer. Llega solo mientras la sala la sube (`talk` o
    /// `atiende` en su ruta). Devuelve la toma (0 = no se pudo) para soltarla.
    int escucharLobby(int slot, std::function<void(const float* estereo, std::size_t cuadros)> fn);
    void dejarDeEscucharLobby(int toma);
    /// Lo mismo para el integrante que espera en la sesión remota de la asamblea y
    /// lo atiende el asistente hablando (BA.Audio.Atencion, un canal: de a uno). Se
    /// suelta con dejarDeEscucharLobby. 0 = no se pudo.
    int escucharAtencionRemota(std::function<void(const float* estereo, std::size_t cuadros)> fn);
    /// La voz de quien se registra por el tótem que tiene el canal `canal` (1..4,
    /// BA.Audio.Registro.<k>; ui/RegistroViewModel). Se suelta con
    /// dejarDeEscucharLobby. 0 = no se pudo.
    int escucharRegistro(int canal, std::function<void(const float* estereo, std::size_t cuadros)> fn);

    // --- Push-to-talk (docs/funciones/AUDIO-TTS.md) -------------------------------------
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
    bool hayMicrofono() const { return tomaAudio_ != 0; }

    const std::string& videoSource() const { return videoName_; }
    const std::string& audioSource() const { return audioName_; }

   private:
    void onAudio(const float* estereo, std::size_t frames);
    void stopVideo();
    void stopAudio();

    // Video
    std::string vista_; ///< la clave de la vista del motor ("" = ninguna)
    std::mutex videoLock_;
    ba::ipc::SpoutSender sender_;
    std::vector<std::uint8_t> packed_;
    std::uint64_t frameIndex_ = 0;
    std::string videoName_;

    // Audio
    int tomaAudio_ = 0; ///< la toma del motor (escucharEntrada)
    ba::ipc::SharedAudioRing ring_;
    std::mutex audioLock_;
    std::vector<float> audioBuf_; // estereo intercalado pendiente de bloque
    std::string audioName_;
    std::uint32_t frecuencia_ = 48000;
    std::atomic<bool> ptt_{false};
    std::mutex dictadoLock_;
    Dictado dictado_;

    // Monitoreo del lobby: entradas internas del motor (no van a la mezcla ni a las listas)
    std::vector<std::string> lobbyMonitors_;
    std::string atencionMonitor_; ///< la de BA.Audio.Atencion ("" = no se armó)
    std::vector<std::string> registroMonitors_; ///< las de BA.Audio.Registro.<k>
};

} // namespace ba::app
