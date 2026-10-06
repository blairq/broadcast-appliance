// ============================================================================
// src/app/AppRuntime.hpp — El Proceso A en marcha, sin importar la interfaz
//
// Proposito:
//   Todo lo que hace ba-app para estar "al aire", comun a la consola
//   (OperateMode) y al dashboard (QML): leer la configuracion (AppSettings) y
//   la linea de comandos, arrancar el motor, armar escenas y fuentes, publicar
//   el retorno hacia B, latir en BA.Health y atender el canal de control.
//
//   Cada accion del operador actualiza el motor Y la configuracion en memoria;
//   `save()` la persiste. Asi lo que se arma en el dashboard sobrevive a un
//   reinicio sin que la interfaz toque JSON.
//
// Modelo de threading:
//   Todo desde un unico thread (el de la interfaz o el bucle de la consola).
//   `requestStop()` es async-signal-safe (solo escribe un atomic).
//
// Estrategia de errores:
//   No lanza. `start` devuelve el codigo de salida si no se puede arrancar;
//   las acciones devuelven bool y dejan el motivo en `lastError()`.
// ============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "AppSettings.hpp"
#include "LatidoAire.hpp"
#include "BroadcastEngine.hpp"
#include "ipc/ControlService.hpp"
#include "ipc/HealthBlock.hpp"
#include "OperatorFeed.hpp"

namespace ba::app {

class AppRuntime {
   public:
    /// Lee la configuracion y la linea de comandos (sin arrancar nada).
    void configure(int argc, char** argv);

    /// Arranca motor, escenas, salidas, salud y canal de control.
    /// :returns: 0 si quedo en marcha; si no, el codigo de salida del proceso.
    int start();

    /// Una vuelta de trabajo: canal de control y latido. Llamar cada ~100 ms.
    void tick();

    /// Detiene salidas y motor y marca la salud como terminada. Idempotente.
    void stop();

    static void requestStop();
    static bool stopRequested();

    // --- Salidas --------------------------------------------------------------
    bool startRecording();
    void stopRecording();
    /// Graba una entrada a su propio archivo, ademas del programa. Ver
    /// BroadcastEngine::startSourceRecording: cada una es un encode mas.
    bool startSourceRecording(const std::string& source);
    bool stopSourceRecording(const std::string& source);
    void stopAllSourceRecordings();
    /// La señal limpia (sin DSK ni placa) a un destino guardado (`destino` = su id) o
    /// a Recordings/limpia/ si viene vacío. "" o el error.
    std::string iniciarSenalLimpia(const std::string& destino);
    /// Prende o apaga la repetición con cfg_.replaySegundos. Devuelve la nota para el operador.
    std::string aplicarReplay();
    /// Transmite a `server` con `key`; vacios = los de la configuracion/entorno.
    bool startStreaming(const std::string& server = {}, const std::string& key = {}, bool cambiarEnCaliente = false);
    void stopStreaming();

    // --- Destinos adicionales de la transmisión (Config/destinos.json; la
    // clave de cada uno en el almacén cifrado como "stream-<id>") -------------
    /// [{id, nombre, url, respaldo, tiene_clave, activo, reconectando, perdidos, destino}].
    nlohmann::json destinos() const;
    /// Agrega o cambia (`id` existente) un destino: {id?, nombre, url, clave?, respaldo?}. "" o el error.
    std::string guardarDestino(const nlohmann::json& d, std::string* idOut = nullptr);
    std::string borrarDestino(const std::string& id);
    /// Arranca un destino ("todos": todos los guardados). "" o el error.
    std::string iniciarDestino(const std::string& id);
    /// El disco de la carpeta de grabación: {libre_gb, horas, kbps} (horas al
    /// bitrate de video configurado + audio; -1 si no se sabe).
    nlohmann::json discoGrabacion() const;
    void detenerDestino(const std::string& id); ///< "todos": todos

    // --- Escenas y fuentes (motor + configuracion) ----------------------------
    bool addScene(const std::string& name);
    bool removeScene(const std::string& name);
    /// Copia una escena entera con sus fuentes. La copia NO queda protegida
    /// aunque el original lo este: para eso se copia.
    bool copyScene(const std::string& from, const std::string& to);
    /// Una escena protegida no se borra ni se renombra (copiarla si).
    bool setSceneProtected(const std::string& name, bool protegida);
    bool sceneProtected(const std::string& name) const;
    bool renameScene(const std::string& from, const std::string& to);
    /// Carpeta de una escena ("" si no tiene).
    std::string sceneFolder(const std::string& name) const;
    /// Mueve la escena a una carpeta ("" la saca). Devuelve false si no existe.
    bool setSceneFolder(const std::string& name, const std::string& folder);
    /// Carpetas que hay, en el orden en que aparecen las escenas.
    std::vector<std::string> sceneFolders() const;
    /// Escenas en el orden en que las ve el operador (el de la configuracion;
    /// las que solo existan en el motor van al final).
    std::vector<std::string> orderedSceneNames() const;
    /// Mueve `name` para que quede JUSTO ANTES de `before` (vacio = al final) y
    /// le pone la carpeta de `before`. Devuelve false si no existe.
    bool moveScene(const std::string& name, const std::string& before);
    bool setScene(const std::string& name);
    /// Al aire con otra transición solo esta vez (ver BroadcastEngine::setCurrentSceneCon).
    bool setSceneCon(const std::string& name, const std::string& tipo, int ms);
    bool setTransition(const std::string& type, int durationMs);
    bool addSource(const std::string& scene, const SourceSpec& spec);
    bool removeSource(const std::string& scene, const std::string& name);
    bool moveSource(const std::string& scene, const std::string& name, int delta);
    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible);
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect);
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);
    bool setVolume(const std::string& name, float mul);
    bool setMuted(const std::string& name, bool muted);

    // --- Configuracion ----------------------------------------------------------
    /// Aplica video/codificacion en caliente (sin salidas activas).
    bool applyVideo(const VideoConfig& v);
    bool applyEncoder(const EncoderConfig& e);
    /// Guarda app.json, gateway.json y launcher.json.
    bool save();
    /// Reinicia B (el launcher lo relanza con gateway.json nuevo).
    bool restartGateway();

    AppConfig& config() { return cfg_; }
    const AppConfig& config() const { return cfg_; }
    GatewaySettings& gateway() { return gateway_; }
    LauncherSettings& launcher() { return launcher_; }
    BroadcastEngine& engine() { return engine_; }
    /// El latido del supervisor que mira el motor (la UI trabada no corta el aire).
    LatidoAire& latido() { return latido_; }
    const std::string& lastError() const { return lastError_; }
    /// true si el ultimo corte de la transmision / grabacion lo PIDIO alguien
    /// (operador, plataforma, MCP). Si la salida se detiene con esto en false,
    /// se cayo sola (red, servidor, disco lleno) y hay que avisar fuerte.
    /// Hay una clave guardada (cifrada en Credenciales/) de la ultima salida al
    /// aire: sirve para no tipearla de nuevo y para retomar tras una caida.
    bool streamKeySaved() const;
    bool streamStopWanted() const { return streamStopWanted_; }
    bool recordStopWanted() const { return recordStopWanted_; }
    /// La corrida anterior terminó sin cerrarse (corte de luz, cuelgue): hay que
    /// volver como estaba (la escena, la playlist, las salidas).
    bool corteAnterior() const { return corteAnterior_; }
    const std::filesystem::path& dataRoot() const { return root_; }
    /// El feed del operador (lobby): de ahí sale el micrófono del push-to-talk.
    OperatorFeed& operatorFeed() { return operator_; }
    std::uint32_t pid() const;

    /// Salud del gateway (B): true si su latido esta fresco.
    bool gatewayAlive() const;

    // --- Sala: lo que el LOBBY ve/oye del operador --------------------------------
    /// Fuentes (por nombre) con las que el operador aparece ante el lobby;
    /// vacio = placa / sin voz. Se guarda en la configuracion.
    bool setOperatorFeed(const std::string& videoSource, const std::string& audioSource);
    /// Escuchar por monitoreo a quien ocupa el lugar `slot` del lobby.
    void setLobbyMonitor(int slot, bool on) { operator_.setLobbyMonitor(slot, on); }

    /// La definicion guardada de una fuente en una escena (nullptr si no esta).
    SourceSpec* findSpec(const std::string& scene, const std::string& name);

    /// Entrada de audio del equipo (microfono, linea) como fuente global.
    bool addAudioInput(const SourceSpec& spec);
    bool removeAudioInput(const std::string& name);

   private:
    void addGuests();
   public:
    /// El cupo de invitados cambió en caliente (T33): agrega el audio de los lugares
    /// nuevos (los que sobran quedan, mudos hasta que alguien entre).
    void ajustarInvitados(int n);

   private:
    void addAudioInputs();
    /// Runtime/al-aire.json: que salidas estaban activas. Lo lee un arranque
    /// con --restore para retomar la emision tras una caida.
    /// También la escena al aire y si el cierre fue ordenado. Escritura atómica.
    void writeOnAirState() const;
    void restoreOutputs();

    AppConfig cfg_;
    GatewaySettings gateway_;
    LauncherSettings launcher_;
    std::filesystem::path root_; // datos (BA_DATA_ROOT)
    std::filesystem::path bin_;  // binarios
    BroadcastEngine engine_;
    ba::ipc::HealthBlock health_;
    std::mutex healthMu_; ///< latidos desde la UI y desde LatidoAire
    LatidoAire latido_;
    ba::ipc::ControlService control_;
    OperatorFeed operator_; // vive mientras libobs este arriba (ver stop)
    std::string lastError_;
    bool streamStopWanted_ = true;
    bool recordStopWanted_ = true;
    bool corteAnterior_ = false;
    bool cerrando_ = false;           ///< stop(): al-aire.json dice «cerrado»
    mutable std::string escenaEscrita_; ///< la que dice al-aire.json
    mutable int keySaved_ = -1;   // cache de streamKeySaved (-1 = sin consultar)
    bool safeBackupDone_ = false; // app.json resguardado antes de guardar en modo seguro
    bool started_ = false;
};

/// Ubicacion por defecto del invitado `slot` de `count` (fila inferior).
CanvasRect guestRect(int slot, int count);

} // namespace ba::app
