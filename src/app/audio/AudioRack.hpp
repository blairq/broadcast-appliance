// ============================================================================
// src/app/audio/AudioRack.hpp — rack de audio "tipo JACK" de una fuente
//
// Cada fuente con audio (microfono, placa, video con sonido, invitado...)
// puede tener un rack: un GRAFO de modulos conectados con cables, como un
// patchbay de JACK. Nodos:
//   in         el audio de la fuente (con analizador propio)
//   out        lo que sale al mezclador (con analizador propio)
//   plugin     un modulo CLAP (LSP: gate, compresor, de-esser, EQ...)
//   analyzer   analizador en cualquier punto (pasa el audio tal cual)
//   generator  señal de prueba (seno, ruido, barrido): solo salida
//   monitor    escucha: sumidero que copia lo que le llega para mandarlo a una
//              salida de audio del sistema (auriculares del operador). No toca
//              lo que sale al aire; se patchea desde cualquier salida.
// Puertos: "in" (entrada principal), "sc" (sidechain, si el modulo tiene) y
// "out". Varios cables a una entrada se suman. Sin ciclos.
//
// Hilos: la edicion (agregar, quitar, conectar, parametros) es del hilo
// principal y arma un PLAN nuevo que se intercambia con el del audio sin
// bloquearlo (si justo esta procesando, ese bloque sale sin procesar). El
// audio (OBS) llama process() con los canales de la fuente.
//
// Medicion: cada modulo mide su entrada y su salida (RMS, pico) => la
// reduccion en dB (compuerta, compresor, de-esser). `measure()` clona la
// cadena y la mide FUERA DE LINEA con señales de prueba (respuesta en
// frecuencia, curva de transferencia) sin tocar lo que sale al aire.
// ============================================================================
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/Analyzer.hpp"
#include "audio/ClapHost.hpp"

namespace ba::app::audio {

class AudioRack {
   public:
    using json = nlohmann::json;
    static constexpr uint32_t kMaxFrames = 1024;

    AudioRack(std::shared_ptr<ClapLibrary> lib, HostServices* services, double sampleRate);
    ~AudioRack();
    AudioRack(const AudioRack&) = delete;
    AudioRack& operator=(const AudioRack&) = delete;

    // --- hilo principal --------------------------------------------------------------
    /// Agrega un modulo CLAP. `after`: insertarlo en la cadena despues de ese
    /// nodo ("" = justo antes de la salida). Devuelve el id nuevo o "".
    /// `idBase`: base del id ("gate" -> "gate", "gate-2"...); vacio = de la etiqueta.
    std::string addPlugin(const std::string& pluginId, const std::string& label, const std::string& after,
                          std::string* error, const std::string& idBase = {});
    std::string addAnalyzer(const std::string& label, const std::string& after);
    /// Generador suelto (no se conecta solo: se patchea a mano o se usa para medir).
    std::string addGenerator(const std::string& label);
    /// Nodo de escucha (uno por rack). Devuelve su id (el existente si ya esta).
    std::string addMonitor(const std::string& label = {});
    bool hasMonitor() const;
    bool remove(const std::string& id, std::string* error);
    bool connect(const std::string& from, const std::string& fromPort, const std::string& to,
                 const std::string& toPort, std::string* error);
    bool disconnect(const std::string& from, const std::string& fromPort, const std::string& to,
                    const std::string& toPort);
    bool setBypass(const std::string& id, bool bypass);
    bool rename(const std::string& id, const std::string& label);
    /// Posicion del nodo en el patchbay (se guarda con el rack).
    bool setPosition(const std::string& id, double x, double y);

    ClapInstance* plugin(const std::string& id);
    /// "in", "out" o un nodo analizador.
    Analyzer* analyzer(const std::string& id);
    Generator* generator(const std::string& id);
    bool has(const std::string& id) const;
    std::vector<std::string> nodeIds() const;
    std::string kindOf(const std::string& id) const;

    /// Grafo: nodos (id, tipo, etiqueta, modulo, puertos, bypass, medidores) y cables.
    json describe() const;
    /// Medidores de un nodo: entrada/salida (RMS y pico, dBFS) y diferencia.
    json meters(const std::string& id) const;
    /// Para guardar: grafo + estado de cada modulo (base64).
    json save() const;
    bool load(const json& j, std::string* error);

    /// Medicion fuera de linea sobre clones. kind: "respuesta" (ganancia por
    /// tercio de octava con un seno a `levelDb`) o "curva" (salida vs entrada de
    /// un seno de 1 kHz de -80 a 0 dBFS). `node` vacio = toda la cadena principal
    /// de in a out; si no, solo ese modulo.
    json measure(const std::string& kind, const std::string& node, double levelDb, std::string* error);

    // --- hilo de audio ---------------------------------------------------------------
    /// `planes`: los canales de la fuente (1..8, se procesan los 2 primeros).
    void process(float* const* planes, int channels, uint32_t frames);
    /// Lo que capturo el nodo de escucha en el ULTIMO `process()` (planar, 2
    /// canales). Vale hasta el `process()` siguiente y solo en ese hilo.
    uint32_t monitorFrames() const { return monFrames_; }
    const float* monitorPlane(int ch) const;

    struct Node;

   private:
    struct Conn {
        std::string from;
        int fromPort;
        std::string to;
        int toPort;
    };
    struct Plan;

    std::string newId(const std::string& base) const;
    Node* find(const std::string& id) const;
    int portIndex(const Node& n, const std::string& port, bool input) const;
    std::string portName(const Node& n, int idx, bool input) const;
    bool wouldCycle(const std::string& from, const std::string& to) const;
    void insertInChain(const std::string& id, const std::string& after);
    /// Ubica un nodo nuevo en el patchbay: a la derecha del ultimo (la salida
    /// se corre); los generadores, abajo.
    void place(Node& n);
    void rebuild();
    std::vector<std::string> mainChain() const;

    std::shared_ptr<ClapLibrary> lib_;
    HostServices* services_ = nullptr;
    double sr_ = 48000;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<Conn> conns_;

    // Escucha: buffer propio (no se asigna memoria en el hilo de audio).
    static constexpr uint32_t kMonitorCap = 8192;
    std::vector<float> monL_, monR_;
    uint32_t monFrames_ = 0;

    std::mutex planLock_;
    std::unique_ptr<Plan> plan_;
};

} // namespace ba::app::audio
