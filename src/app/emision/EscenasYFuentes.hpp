// ============================================================================
// src/app/emision/EscenasYFuentes.hpp — el modelo de escenas y fuentes del motor de la app
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp). Qué escenas hay, qué fuentes tienen,
// dónde, con qué recorte y efectos; la escena al aire y la armada de cada salida con su
// transición; las capas encima del programa de cada salida (DSK 1..4, placa y negro:
// CapasPrograma.cpp); la «Banca con la palabra», el traer, el video en vivo dentro de un
// gráfico y los compositores del grafo. El motor recibe cada escena ya traducida a capas en
// píxeles del lienzo y cada fuente ya traducida a los ajustes de su entrada.
//
// Hilos: EL DUEÑO DEL MOTOR (con el turno, que toma la fachada). Sin candados propios.
// ============================================================================
#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "emision/BroadcastEngine.hpp"
#include "escenas/RefEscena.hpp"

namespace ba::app {

struct SalidaAire;

class EscenasYFuentes {
   public:
    /// Una fuente del modelo (lo que el motor no guarda).
    struct Fuente {
        SourceSpec spec; ///< tipo lógico y ajustes tal como los pidieron
        /// «Banca con la palabra» (ba_banca): no es una entrada; al publicar la escena
        /// muestra la ranura que dice (ver setRanurasBanca).
        bool banca = false;
        bool global = false; ///< suena siempre (micrófonos, invitados, música)
        int syncMs = 0;
        bool monitor = false;
        std::vector<int> pistas{1};
        /// Un compositor del grafo de video (ba_video_compositor): no es una entrada, es una
        /// escena interna del motor; esta es su composición.
        std::string compProductor;
    };
    struct Item {
        std::string fuente;
        CanvasRect rect;
        bool visible = true;
        float recorte[4] = {0, 0, 0, 0};
        EfectosCapa efectos;
    };
    struct EscenaApp {
        std::string id;     ///< estable (escenas/RefEscena.hpp): el motor la conoce por esto
        std::string nombre; ///< el que se muestra y se pide
        std::vector<Item> items; ///< de abajo hacia arriba
    };
    using RanuraBanca = BroadcastEngine::RanuraBanca;

    explicit EscenasYFuentes(BroadcastEngine& m) : m_(m) {}

    // --- Escenas y transición -------------------------------------------------------------
    // Cada escena se pide por su NOMBRE o por su ID (escenas/RefEscena.hpp): escena()
    // entiende los dos. Al motor va el id, así que renombrar no le toca nada.
    /// `id` vacío: uno nuevo.
    bool createScene(const std::string& name, const std::string& id = {});
    bool removeScene(const std::string& name);
    bool renameScene(const std::string& from, const std::string& to);
    std::vector<std::string> sceneNames() const;
    /// El id y el nombre de una escena pedida por cualquiera de los dos ("" si no está).
    std::string idDe(const std::string& escena) const;
    std::string nombreDe(const std::string& escena) const;
    escenas::TablaEscenas tabla() const;
    bool setCurrentScene(const std::string& name, const std::string& salida = "");
    std::string currentScene(const std::string& salida = "") const;
    std::string currentSceneId(const std::string& salida = "") const;
    std::string armadaId(const std::string& salida = "") const;
    bool setTransition(const std::string& type, int durationMs);
    bool setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida);
    bool transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida);
    std::string manualHacia(const std::string& salida) const;
    bool armar(const std::string& name, const std::string& salida);
    bool buildScene();
    std::string errorDeCompositor(const std::string& nombre) const;

    // --- Las capas encima del programa (CapasPrograma.cpp) --------------------------------
    bool ftb(bool activar, int ms, bool bajarAudio, const std::string& salida);
    bool placa(bool activar, const std::string& texto, const std::string& imagen, int ms, const std::string& salida);
    nlohmann::json estadoPlaca(const std::string& salida) const;
    bool dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro, const std::string& salida);
    bool dskMostrar(int capa, bool visible, int ms, const std::string& salida);
    nlohmann::json dskEstado(const std::string& salida) const;
    std::vector<std::string> fuentesDsk(const std::string& salida) const;
    bool textoGlobal(const std::string& nombre, const std::string& texto);
    /// Vuelve a mandarle al motor todas las capas de la salida (DSK, placa, negro).
    void publicarCapas(SalidaAire& s, int ms);
    /// Las entradas internas (fundido a negro, placa) se piden de nuevo a un motor nuevo.
    void olvidarInternas() { internasPedidas_.clear(); }

    // --- La banca, el traer, lo que se ve en el programa ----------------------------------
    void setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar);
    bool hayFuenteBanca() const;
    std::string escenaDeRanura(int ranura) const;
    std::vector<std::string> fuentesEnPrograma(const std::string& salida) const;
    std::vector<std::string> fuentesEnPrograma() const;
    bool fuenteAlAire(const std::string& fuente) const;
    void setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor);

    // --- Video en vivo dentro de un gráfico -----------------------------------------------
    std::string ponerVideoDeGrafico(const std::string& grafico, const nlohmann::json& capas);
    void empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs);
    void quitarVideoDeGrafico(const std::string& grafico);

    // --- Fuentes --------------------------------------------------------------------------
    bool addSource(const std::string& scene, const SourceSpec& spec);
    bool addSource(const SourceSpec& spec);
    bool removeSource(const std::string& scene, const std::string& name);
    bool moveSource(const std::string& scene, const std::string& name, int delta);
    std::vector<std::string> sceneSources(const std::string& scene) const;
    bool setSourceVisible(const std::string& scene, const std::string& name, bool visible);
    bool sourceVisible(const std::string& scene, const std::string& name) const;
    bool setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect);
    bool setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                       float abajo);
    bool setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos);
    EfectosCapa sourceEfectos(const std::string& scene, const std::string& name) const;
    void aplicarRecortesPendientes();
    bool renameSource(const std::string& from, const std::string& to);
    std::string sourceTypeId(const std::string& name) const;
    bool sourceExists(const std::string& name) const { return sources_.count(name) > 0; }
    nlohmann::json estadoEntrada(const std::string& name) const;
    std::vector<std::pair<std::string, CanvasRect>> itemsVisibles(const std::string& scene) const;
    std::string controlEntrada(const std::string& name, const nlohmann::json& campos);
    std::string sourceSettingsJson(const std::string& name) const;
    bool sourceSize(const std::string& name, int& width, int& height) const;
    bool updateSourceSettings(const std::string& name, const std::string& settingsJson);
    bool sourceTypeExists(const std::string& typeId) const;
    static std::vector<SourceType> tiposDeFuente();
    std::vector<std::string> sourceNames() const { return ordenFuentes_; }

    // --- Entradas globales e invitados ----------------------------------------------------
    bool addGuestAudio(int slot);
    bool addGlobalAudio(const SourceSpec& spec);
    bool removeGlobalAudio(const std::string& name);
    std::vector<std::string> globalAudioInputs() const { return micInputs_; }
    bool addGuestVideo(int slot, const CanvasRect& rect);

    // --- Para las otras piezas (en el dueño) ----------------------------------------------
    const EscenaApp* escena(const std::string& nombre) const;
    EscenaApp* escena(const std::string& nombre);
    /// La fuente del modelo (nullptr si no está).
    Fuente* fuente(const std::string& nombre);
    const Fuente* fuente(const std::string& nombre) const;
    const std::map<std::string, Fuente>& todasLasFuentes() const { return sources_; }
    /// El modelo de las fuentes para la mezcla (MezclaApp: el fader, la escucha, las pistas).
    std::map<std::string, Fuente>& todasLasFuentes() { return sources_; }
    /// Crea la fuente en el motor (si no existe). false + lastError si no se pudo.
    bool crearFuente(const SourceSpec& spec, bool global);
    /// Los ajustes del motor para un tipo lógico (y si se pudo).
    bool ajustesMotor(const SourceSpec& spec, nlohmann::json& ajustes) const;
    /// Lo que había antes de apagar el motor, para armarlo igual en el nuevo (resetVideo).
    struct Copia {
        std::vector<EscenaApp> escenas;
        std::map<std::string, Fuente> fuentes;
        std::vector<std::string> orden, mics;
    };
    Copia copia() const { return {scenes_, sources_, ordenFuentes_, micInputs_}; }
    /// Ya con las fuentes creadas de nuevo: las escenas y los micrófonos como estaban.
    void reponer(const Copia& c);
    /// Al apagar el motor: el modelo queda vacío (las escenas vuelven con reponer).
    void vaciar();

   private:
    /// La escena tal como la entiende el motor (capas en píxeles).
    nlohmann::json escenaMotor(const EscenaApp& e) const;
    /// Suma (o anima) en las capas de una escena el video de sus gráficos.
    void sumarVideoDeGraficos(const EscenaApp& e, nlohmann::json& capas) const;
    /// Vuelve a publicar las escenas que tienen esa fuente.
    void publicarEscenasCon(const std::string& fuente);
    /// La ranura de una fuente ba_banca (1 si no dice).
    int ranuraDe(const Fuente& f) const;
    /// Le manda la escena al motor (si está al aire o armada, cambia andando).
    void publicarEscena(const EscenaApp& e);
    bool ajustesMotorTipo(const SourceSpec& spec, nlohmann::json& ajustes) const;
    /// La escena interna de un compositor (sus capas, de la config del grafo).
    nlohmann::json escenaCompositor(const std::string& nombre, const nlohmann::json& config) const;
    /// ¿Quedó en alguna escena o es global? Si no, se libera.
    void soltarSiHuerfana(const std::string& nombre);
    std::string transicionMotor(const std::string& tipo) const;

    BroadcastEngine& m_;
    std::vector<EscenaApp> scenes_;
    std::map<std::string, Fuente> sources_;
    std::vector<std::string> ordenFuentes_; ///< en orden de creación
    std::string transitionType_ = "fade";
    int transitionMs_ = 300;
    std::vector<std::string> micInputs_; ///< entradas de audio del equipo
    struct RecortePendiente {
        std::string escena, fuente;
    };
    std::vector<RecortePendiente> recortesPendientes_;
    /// Gráfico (nombre de su fuente) → {capas (VideoEnGrafico::aJson), desde_ns}.
    std::map<std::string, nlohmann::json> videoDeGraficos_;
    /// entrada -> {compositor, productor} (ver setTraer).
    std::map<std::string, std::pair<std::string, std::string>> traer_;
    std::vector<RanuraBanca> ranurasBanca_; ///< ver setRanurasBanca
    /// Las entradas internas (fundido a negro, placa) que ya se le pidieron a este motor.
    std::set<std::string> internasPedidas_;
};

} // namespace ba::app
