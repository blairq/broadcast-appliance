// ============================================================================
// src/app/arranque/RepositorioEscenas.hpp — las escenas y sus fuentes, en el motor y en app.json
//
// Pieza de AppRuntime (ver RuntimeConfig.hpp). Cada cambio de escenas y fuentes va al motor
// Y a la configuración en memoria (cfg_.scenes, cfg_.audioInputs, cfg_.transition,
// cfg_.currentScene): así lo que se arma sobrevive a un reinicio sin que nadie toque JSON
// (lo escribe AppRuntime::save, por la cola de escritura de ba::persist). También la mezcla
// que se guarda con la fuente (el fader y el mute de la por omisión; los de las demás
// salidas van a Config/salidas.json por ServicioSalidas). Cada escena tiene su id
// estable (SceneConfig::id, escenas/RefEscena.hpp): todo lo que recibe una escena
// entiende el nombre o el id.
//
// Hilos: EL DUEÑO (con el turno): cfg_ y el motor son suyos.
// ============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arranque/AltasEnLaFoto.hpp"
#include "arranque/AppSettings.hpp"

namespace ba::app {

class AppRuntime;

class RepositorioEscenas {
   public:
    explicit RepositorioEscenas(AppRuntime& rt);

    /// Las altas en el motor esperan su vuelta antes de responder (arranque/AltasEnLaFoto):
    /// las de acá y las de otras piezas (Invitados), y el lote para varias seguidas.
    const AltasEnLaFoto& altas() const { return altas_; }

    /// Al arrancar: los VCA, la transición, cada escena con sus fuentes (en modo seguro,
    /// vacías) y la escena al aire de la por omisión, en el motor.
    void armarEnElMotor();
    /// Al arrancar: las entradas de audio del equipo (micrófonos, líneas), globales.
    void agregarEntradasDeAudio();
    /// Las ubicaciones son fracciones del lienzo: de nuevo en píxeles (otro lienzo).
    void reubicarFuentes();

    // --- Escenas (ver AppRuntime) --------------------------------------------------------
    bool addScene(const std::string& name);
    bool removeScene(const std::string& name);
    bool copyScene(const std::string& from, const std::string& to);
    bool setSceneProtected(const std::string& name, bool protegida);
    bool sceneProtected(const std::string& name) const;
    bool renameScene(const std::string& from, const std::string& to);
    std::string sceneFolder(const std::string& name) const;
    bool setSceneFolder(const std::string& name, const std::string& folder);
    std::vector<std::string> sceneFolders() const;
    std::vector<std::string> orderedSceneNames() const;
    bool moveScene(const std::string& name, const std::string& before);
    bool setScene(const std::string& name, const std::string& salida);
    bool setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida);
    void anotarEscenaAlAire(const std::string& name, const std::string& salida);
    bool setTransition(const std::string& type, int durationMs);

    // --- Fuentes -------------------------------------------------------------------------
    bool addSource(const std::string& scene, const SourceSpec& spec);
    bool removeSource(const std::string& scene, const std::string& name);
    bool moveSource(const std::string& scene, const std::string& name, int delta);
    bool renameSource(const std::string& from, const std::string& to);
    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible);
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect);
    bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos);
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);
    std::optional<SourceSpec> specDe(const std::string& scene, const std::string& name) const;
    bool addAudioInput(const SourceSpec& spec);
    bool removeAudioInput(const std::string& name);

    // --- La mezcla que se guarda ---------------------------------------------------------
    bool setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida);
    bool setVolume(const std::string& name, float mul, const std::string& salida);
    bool setMuted(const std::string& name, bool muted, const std::string& salida);

   private:
    /// La definición guardada (para cambiarla, en el dueño); nullptr si no está.
    SourceSpec* findSpec(const std::string& scene, const std::string& name);
    /// Lo común de setScene y setSceneCon después de pedirlo al motor.
    bool anotarAire(bool ok, const std::string& name, const std::string& salida);

    AppRuntime& rt_;
    AltasEnLaFoto altas_;
};

} // namespace ba::app
