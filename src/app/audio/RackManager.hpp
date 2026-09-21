// ============================================================================
// src/app/audio/RackManager.hpp — racks de audio de todas las fuentes
//
// Cada fuente con audio puede tener su rack (AudioRack) montado como un
// filtro de audio de OBS ("ba_audio_rack"): OBS le pasa el audio de la
// fuente en su hilo de audio y el rack lo devuelve procesado al mezclador.
//
// El manager:
//   - abre los plugins LSP del bundle (plugins/lsp/lsp-plugins.clap);
//   - da los servicios del hilo principal que piden los plugins (timers, fds,
//     "correr en el principal") con Qt;
//   - guarda el grafo y el estado de cada rack en Config/audio-racks.json
//     (por nombre de fuente) y los restaura al arrancar;
//   - lista las fuentes con audio (tambien videos con sonido, invitados...);
//   - la ESCUCHA: el nodo "monitor" de un rack se manda a una salida de audio
//     del sistema con una fuente privada de OBS en modo MONITOR_ONLY (nunca a
//     la mezcla). El dispositivo es uno solo para toda la app (asi lo maneja
//     libobs) y se guarda en Config/audio-escucha.json.
// Todo en el hilo principal salvo el procesamiento (hilo de audio de OBS).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "audio/AudioRack.hpp"

namespace ba::app::audio {

class QtHostServices;

class RackManager : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;

    RackManager(std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~RackManager() override;

    /// Los plugins LSP (se abren la primera vez). nullptr + error si no estan.
    std::shared_ptr<ClapLibrary> library(std::string* error = nullptr);
    /// Fuentes con audio: [{nombre, tipo, rack: bool, modulos: n}].
    json audioSources() const;
    /// El rack de la fuente; con `create` lo monta si no tiene.
    AudioRack* rack(const std::string& source, bool create, std::string* error = nullptr);
    /// Quita el rack de la fuente (el audio vuelve a pasar directo).
    bool removeRack(const std::string& source);

    /// Salidas de audio del equipo agrupadas por dispositivo:
    /// [{dispositivo, salidas: [{id, nombre}]}].
    json outputDevices() const;
    /// Manda el audio del rack de `source` a la salida `device`. Devuelve el
    /// error o "". OJO: el motor reproduce por UNA salida a la vez.
    std::string setOutput(const std::string& source, const std::string& device);
    /// Algo cambio en un rack: se guarda en un momento.
    void markDirty();
    /// Guarda ya (tambien al salir).
    void saveNow();
    /// Restaura los racks guardados de las fuentes que existan.
    void restore();
    double sampleRate() const;

    /// Servicios del hilo principal para los plugins.
    HostServices* services() const;
    /// Uso interno del filtro de OBS.
    struct Filter;
    void attach(Filter* f);
    void detach(Filter* f);

   signals:
    void changed();

   private:
    std::filesystem::path root_;
    QtHostServices* services_ = nullptr; // del proceso (ver el constructor)
    std::shared_ptr<ClapLibrary> lib_;
    std::string libError_;
    bool libTried_ = false;
    std::map<std::string, Filter*> filters_; // por fuente
    /// Prende o apaga la escucha de cada rack segun el dispositivo elegido.
    void syncMonitors();
    void saveMonitor() const;
    void restoreMonitor();

    QTimer saveTimer_;
    std::string monitorId_;   // "" = sin escucha aparte
    std::string monitorName_; // el nombre que espera libobs junto al id
    json pending_;            // racks guardados de fuentes que todavia no existen
};

} // namespace ba::app::audio
