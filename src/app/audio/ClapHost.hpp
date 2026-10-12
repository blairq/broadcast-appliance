// ============================================================================
// src/app/audio/ClapHost.hpp — Host de plugins CLAP (LSP y cualquier otro)
//
// CLAP es un ABI de C simple y abierto (MIT): un .clap exporta `clap_entry`,
// su fabrica crea instancias, y cada instancia expone extensiones (parametros,
// estado, puertos de audio, interfaz grafica). LSP lo soporta completo en
// Linux y Windows.
//
// Hilos (lo exige CLAP):
//   - HILO PRINCIPAL (Qt): crear, activar/desactivar, leer parametros, estado,
//     editor. `HostServices` le da al host timers, descriptores de archivo y
//     "correr esto en el hilo principal" (en la app, Qt; en los tests, directo).
//   - HILO DE AUDIO (OBS): `process()`. Nunca reserva memoria ni bloquea: los
//     cambios de parametros llegan por una cola SPSC y se entregan como eventos.
//
// Medidores: LSP NO publica sus medidores por CLAP (solo a su interfaz). Los
// niveles de entrada/salida y la reduccion de cada modulo los mide el rack
// (AudioRack) con el analizador propio.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct clap_plugin;
struct clap_plugin_factory;
struct clap_plugin_entry;
struct clap_host;

namespace ba::app::audio {

/// Servicios del hilo principal que necesita un plugin (timers y fds los
/// piden las interfaces graficas en Linux).
class HostServices {
   public:
    virtual ~HostServices() = default;
    /// Correr `fn` en el hilo principal (pronto, no ahora mismo).
    virtual void runOnMain(std::function<void()> fn) = 0;
    virtual int addTimer(int periodMs, std::function<void()> fn) = 0;
    virtual void removeTimer(int id) = 0;
    /// flags: 1 = leer, 2 = escribir, 4 = error (los de CLAP).
    virtual bool addFd(int fd, uint32_t flags, std::function<void(uint32_t)> fn) = 0;
    virtual void removeFd(int fd) = 0;
    /// El plugin cambio su estado (p. ej. desde su editor): hay que guardarlo.
    virtual void stateDirty() {}
    /// Un plugin avisó que cambió su latencia (CLAP_EXT_LATENCY): hay que recalcular
    /// la compensación de los racks.
    virtual void latencyChanged() {}
};

struct PluginDesc {
    std::string id, name, vendor, version, description;
    std::vector<std::string> features;
};

struct ParamInfo {
    uint32_t id = 0;
    std::string name, module;
    double min = 0, max = 1, def = 0;
    uint32_t flags = 0;
    bool readOnly() const;
    bool stepped() const;
    bool hidden() const;
    bool isEnum() const;
};

struct PortInfo {
    std::string name;
    uint32_t channels = 2;
    bool main = false;
};

/// Un archivo .clap abierto (puede traer muchos plugins: el de LSP trae ~200).
class ClapLibrary {
   public:
    static std::shared_ptr<ClapLibrary> open(const std::string& path, std::string* error);
    ~ClapLibrary();
    ClapLibrary(const ClapLibrary&) = delete;
    ClapLibrary& operator=(const ClapLibrary&) = delete;

    const std::string& path() const { return path_; }
    const std::vector<PluginDesc>& plugins() const { return plugins_; }
    const PluginDesc* find(const std::string& id) const;
    const clap_plugin_factory* factory() const { return factory_; }

   private:
    ClapLibrary() = default;
    std::string path_;
    void* handle_ = nullptr;
    const clap_plugin_entry* entry_ = nullptr;
    const clap_plugin_factory* factory_ = nullptr;
    std::vector<PluginDesc> plugins_;
};

class ClapEditor;

/// Una instancia de plugin.
class ClapInstance {
   public:
    static std::unique_ptr<ClapInstance> create(std::shared_ptr<ClapLibrary> lib, const std::string& pluginId,
                                                HostServices* services, std::string* error);
    ~ClapInstance();
    ClapInstance(const ClapInstance&) = delete;
    ClapInstance& operator=(const ClapInstance&) = delete;

    const PluginDesc& desc() const { return desc_; }

    // --- hilo principal ----------------------------------------------------------
    bool activate(double sampleRate, uint32_t maxFrames);
    /// Desactiva. Si el audio lo estaba procesando, primero le pide al hilo de
    /// audio que lo detenga, o lo detiene aca si el audio no esta corriendo.
    /// `audioDetached`: el host ya garantiza que el hilo de audio no lo usa mas
    /// (lo saco del grafo): se detiene aca sin esperar.
    void deactivate(bool audioDetached = false);
    bool active() const { return active_; }
    /// La latencia que agrega, en muestras (CLAP_EXT_LATENCY; 0 si no la declara o
    /// no está activo). Hilo principal.
    uint32_t latencia() const;
    double sampleRate() const { return sampleRate_; }
    const std::vector<PortInfo>& inputs() const { return inputs_; }
    const std::vector<PortInfo>& outputs() const { return outputs_; }
    std::vector<ParamInfo> params() const;
    bool getValue(uint32_t id, double& value) const;
    std::string valueText(uint32_t id, double value) const;
    bool textToValue(uint32_t id, const std::string& text, double& value) const;
    /// Cambia un parametro (hilo principal). Se entrega al plugin en el
    /// proximo bloque de audio, o con flush si no esta procesando.
    bool setValue(uint32_t id, double value);
    std::vector<uint8_t> saveState() const;
    bool loadState(const std::vector<uint8_t>& data);

    bool hasEditor() const;
    bool openEditor(const std::string& title, std::string* error);
    void closeEditor();
    bool editorOpen() const;

    // --- hilo de audio -----------------------------------------------------------
    /// Procesa `frames` muestras. `in[p][c]` / `out[p][c]`: puerto p, canal c
    /// (los canales de cada puerto son los de inputs()/outputs()). Si el
    /// plugin no esta listo, copia la entrada principal a la salida.
    void process(float* const* const* in, float* const* const* out, uint32_t frames);
    /// Llamar desde el hilo de audio si hace falta detener (ver deactivate).
    void audioThreadIdle();

    // --- interno (callbacks del host) ---------------------------------------------
    struct Host;

   private:
    ClapInstance() = default;
    void flushParamsOnMain();
    void deliverGuiClosed();

    std::shared_ptr<ClapLibrary> lib_;
    PluginDesc desc_;
    std::unique_ptr<Host> host_;
    const clap_plugin* plugin_ = nullptr;
    HostServices* services_ = nullptr;
    std::vector<PortInfo> inputs_, outputs_;
    bool active_ = false;
    double sampleRate_ = 48000;
    uint32_t maxFrames_ = 1024;
    // 0 = quieto, 1 = procesando (hilo de audio), 2 = flush en el principal.
    std::atomic<int> state_{0};
    std::atomic<bool> stopRequested_{false};
    int64_t steadyTime_ = 0;

    // Cola SPSC de cambios de parametros (principal -> audio).
    struct ParamChange {
        uint32_t id;
        double value;
    };
    static constexpr uint32_t kQueue = 512;
    ParamChange queue_[kQueue]{};
    std::atomic<uint32_t> qHead_{0}, qTail_{0};

    std::unique_ptr<ClapEditor> editor_;
    friend class ClapEditor;
};

} // namespace ba::app::audio
