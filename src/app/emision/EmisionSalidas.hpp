// ============================================================================
// src/app/emision/EmisionSalidas.hpp — lo que sale de cada salida, en el hilo de salidas
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp; docs/planes/PLAN-MOTOR-SIN-ESPERAS.md,
// S4). Transmisión y destinos extra, grabación (con su copia y sus pistas), repetición,
// grabaciones por entrada (ISO) y señal limpia viven en el HILO DE SALIDAS: arrancarlas,
// pararlas y atenderlas (una transmisión caída se rearma probando codificadores; parar una
// grabación espera el cierre del archivo) son cambios de estado de GStreamer que tardan. El
// dueño encola (enSalidas), lee la foto que este hilo publica cada 100 ms y lee lo que este
// hilo le contesta por el buzón (emision/SalidaMotor.hpp). Lo que no arrancó vuelve además
// como aviso (BroadcastEngine::alFallarMotor).
//
// Hilos (cada dato con UN dueño):
//   - EL DUEÑO DEL MOTOR (con el turno): las órdenes (start*/stop*), lo pedido (en cada
//     SalidaAire y individualesPedidas_) y atender el buzón.
//   - EL HILO DE SALIDAS: emisiones_ (cada SalidaEmision), sourceRecs_ y lo del disco
//     (VigiaDisco). Antes de arrancarlo y después de pararlo, del dueño.
//   - DESDE CUALQUIER HILO: fotoSalidas (fotoSalidasM_), alAire (atómico), el remux (lo lanzan
//     el dueño y el hilo de salidas al parar una grabación: su candado).
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "emision/BroadcastEngine.hpp"
#include "emision/SalidaMotor.hpp"

namespace ba::app {

class EmisionSalidas {
   public:
    using AlResultado = BroadcastEngine::AlResultado;
    using Inicio = BroadcastEngine::Inicio;

    explicit EmisionSalidas(BroadcastEngine& m);
    ~EmisionSalidas();

    // --- El hilo ---------------------------------------------------------------------------
    /// Arranca el hilo de salidas (startup) y lo para corriendo lo encolado (shutdown: lo que
    /// queda andando se para en el hilo, y lo pedido queda en nada).
    void arrancar();
    void detener();
    bool andando() const { return hilo_.joinable(); }
    /// Una salida nueva / que se va: su mitad pasa al hilo de salidas (o se va de él).
    void alta(const std::shared_ptr<SalidaEmision>& s);
    void baja(const std::shared_ptr<SalidaAire>& s);
    /// Al apagar el motor (con el hilo parado): solo queda la por omisión.
    void quedarseConLaPorOmision();
    /// Lo que cuenta el hilo de salidas al dueño (ver SalidaMotor.hpp).
    void contarAlDueno(MensajeSalida m) { buzon_.poner(std::move(m)); }
    /// El dueño atiende lo que contó el hilo de salidas.
    void atenderBuzon();

    // --- Transmisión y destinos ------------------------------------------------------------
    bool startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente, const std::string& salida,
                        AlResultado alTerminar);
    Inicio ultimoInicio() const;
    std::string destinoActivo(const std::string& salida) const;
    bool startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                    const std::string& salida, const std::string& serverRespaldo);
    void stopExtra(const std::string& id);
    void stopExtras();
    nlohmann::json extrasEstado() const;
    nlohmann::json saludSalidas() const;
    void stopStreaming(const std::string& salida);

    // --- Grabación -------------------------------------------------------------------------
    bool startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida, AlResultado alTerminar);
    void stopRecording(const std::string& salida);
    nlohmann::json grabacionEstado(const std::string& salida) const;
    std::string archivoGrabacion(const std::string& salida) const;
    static int kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool conPistas);
    bool remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen);
    nlohmann::json remuxEstado() const;
    bool encoderRespaldoActivo() const;
    void simularDiscoTrabado(int ms);

    // --- Repetición ------------------------------------------------------------------------
    bool setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida);
    bool guardarReplay(const std::string& salida);
    std::string ultimoReplay(const std::string& salida) const;
    nlohmann::json replayEstado(const std::string& salida) const;

    // --- Grabación por entrada y señal limpia ----------------------------------------------
    bool startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    bool stopSourceRecording(const std::string& source);
    void stopAllSourceRecordings();
    std::vector<BroadcastEngine::GrabacionIndividual> sourceRecordings() const;
    bool startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                        const EncoderConfig& enc, const std::string& salida);
    void stopCleanFeed(const std::string& salida);
    nlohmann::json cleanFeedEstado(const std::string& salida) const;

    // --- Lo que sale ------------------------------------------------------------------------
    nlohmann::json fotoSalidas() const;
    bool haySalida(bool contarGrabacion) const;
    bool haySalidaEn(const std::string& salida, bool contarGrabacion = true) const;
    /// ¿Hay algo pedido (transmisión, grabación o destinos) en alguna salida? En el dueño.
    bool outputsActive();
    /// Transmitiendo o grabando (lo anota el hilo de salidas). Desde cualquier hilo.
    bool alAire() const { return alAire_.load(); }

    // --- Para VigiaDisco (en el hilo de salidas) --------------------------------------------
    const std::vector<std::shared_ptr<SalidaEmision>>& emisiones() const { return emisiones_; }
    std::map<std::string, GrabacionAparte>& grabacionesAparte() { return sourceRecs_; }
    /// Encola en el hilo de salidas (las órdenes del dueño y lo de VigiaDisco).
    void enSalidas(const std::string& que, std::function<std::string()> f);

   private:
    void bucle();
    void tic();
    void publicarFoto();
    /// Un pedido de lo que YA está pedido (`que`: "grabar" o "transmitir"): su respuesta es
    /// la de la orden que está en camino, así que se encola detrás de ella en el hilo de
    /// salidas y contesta lo que pasó con esa (no «ok» en el acto: si fallaba, quien pidió
    /// la segunda vez se quedaba con un ok que nunca fue).
    void contestarComoLaAnterior(const std::shared_ptr<SalidaEmision>& s, const std::string& que, AlResultado alTerminar);
    /// Lo que hay en el hilo de salidas de una salida, parado (al borrarla o al apagar).
    void pararTodoYa(SalidaEmision& s);
    /// {principal, extras, grabacion, replay, limpio} de una salida (en el hilo de salidas).
    nlohmann::json fotoDe(const SalidaEmision& s) const;
    /// La foto de las salidas de una ("" = la por omisión).
    nlohmann::json fotoDeSalida(const std::string& salida) const;
    /// Una transmisión de `s` con AAC a `kbps`: si es la más chica, se le cuenta al dueño (el techo).
    void anotarAac(SalidaEmision& s, int kbps);
    std::string startStreamingYa(SalidaEmision& s, const StreamConfig& stream, const EncoderConfig& enc);
    std::string startExtraYa(SalidaEmision& s, const std::string& id, const std::string& server, const std::string& key,
                             const EncoderConfig& enc, const std::string& serverRespaldo);
    std::string startRecordingYa(SalidaEmision& s, const RecordConfig& rec, const EncoderConfig& enc,
                                 const std::vector<int>& pistas, const std::string& nombre);
    void stopRecordingYa(SalidaEmision& s);
    std::string setReplayYa(SalidaEmision& s, int segundos, const std::string& dir, const EncoderConfig& enc);
    std::string startSourceRecordingYa(const std::string& source, const std::string& dir, const EncoderConfig& enc);
    std::string startCleanFeedYa(SalidaEmision& s, const std::string& destino, const std::string& clave,
                                 const std::string& dir, const EncoderConfig& enc);

    BroadcastEngine& m_;
    // --- El hilo y su cola ---
    std::thread hilo_;
    std::mutex colaM_;
    std::condition_variable colaCv_;
    std::deque<std::pair<std::string, std::function<std::string()>>> cola_;
    bool seguir_ = false; ///< bajo colaM_
    // --- Del hilo de salidas ---
    std::vector<std::shared_ptr<SalidaEmision>> emisiones_; ///< la por omisión primera, en el orden en que se crearon
    std::map<std::string, GrabacionAparte> sourceRecs_;     ///< las grabaciones por entrada
    // --- Del dueño ---
    std::map<std::string, std::uint64_t> individualesPedidas_; ///< entrada -> orden que la pidió
    std::uint64_t ordenes_ = 0; ///< el número de la última orden (ver PedidoOrden)
    // --- El buzón y lo publicado ---
    BuzonSalidas buzon_;
    std::atomic<bool> alAire_{false};
    mutable std::mutex fotoSalidasM_;
    nlohmann::json fotoSalidas_ = nlohmann::json::object();
    struct EstadoRemux;
    std::shared_ptr<EstadoRemux> remux_; ///< bajo remuxM_ (lo lanza también el hilo de salidas al parar)
    mutable std::mutex remuxM_;
};

} // namespace ba::app
