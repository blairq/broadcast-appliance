// ============================================================================
// src/app/emision/VistasYMedicion.hpp — lo que la interfaz mira y lo que se mide del programa
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp). Las vistas previas (el programa, las
// escenas y fuentes por canal, los buses de cada salida), las capturas para las miniaturas,
// lo que se mide del programa de cada salida (loudness y picos, brillo y quietud para las
// alarmas), la sincronía labios/voz con un aplauso y la latencia del estudio.
//
// Hilos:
//   - Pedir y soltar vistas, medir, la latencia: EL DUEÑO DEL MOTOR (con el turno).
//   - DESDE CUALQUIER HILO: leer las vistas (latestPreview, renderScenePreview,
//     scenePreviewName) con vistasMu_, que también toma el núcleo al cambiar el motor
//     (candado()); las capturas (capturaMu_, capturas en su hilo); los picos y programaVideo
//     (atómicos de MedidasSalida).
//   - Los hilos del motor (audio y cuadros) alimentan las medidas y la sincronía: la
//     medición en curso se lee y se cambia de forma atómica (medSincro_).
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "emision/BroadcastEngine.hpp"

namespace ba::app {

struct SalidaAire;

class VistasYMedicion {
   public:
    explicit VistasYMedicion(BroadcastEngine& m) : m_(m) {}

    // --- Vistas previas -------------------------------------------------------------------
    bool enablePreview(int width, int height);
    void disablePreview();
    bool previewActiva() const { return previewActive_; }
    bool latestPreview(motor::Cuadro& out, std::uint64_t& serial) const;
    bool enableScenePreview(const std::string& canal, const std::string& name, int maxWidth);
    bool enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida, int maxWidth);
    void disableScenePreview(const std::string& canal);
    void medirVista(const std::string& clave);
    std::string scenePreviewName(const std::string& canal) const;
    /// Lo que muestran las vistas pedidas ahora, tal como se pidió (una escena por nombre o
    /// id, «mini:X», «entera:X», una fuente, «programa:<salida>»…). DESDE CUALQUIER HILO.
    std::vector<std::string> nombresEnVistas() const;
    bool renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial);
    void soltarVistasSinMirar(bool on);
    bool vistasSinMirar() const { return vistasSinMirar_; }

    // --- Capturas -------------------------------------------------------------------------
    void capturarAparte(const std::string& name, int maxWidth, BroadcastEngine::Captura listo);
    std::string prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                bool& esEscena) const;
    std::string capturarVista(const std::string& clave, int w, int h, bool esEscena, std::vector<std::uint8_t>& rgba,
                              int& width, int& height);

    // --- Lo que se mide del programa ------------------------------------------------------
    void programPeaksDb(float& left, float& right, const std::string& salida);
    float picoProgramaVigiaDb();
    BroadcastEngine::ProgramaVideo programaVideo(const std::string& salida) const;
    /// Empieza a medir el programa de `s`: el audio (loudness, picos) y el video (brillo y
    /// quietud, para las alarmas), y la sincronía si se mide en ella.
    void medirSalida(SalidaAire& s);
    void dejarDeMedir(SalidaAire& s);
    bool empezarMedicionSincro(const std::string& salida);
    nlohmann::json terminarMedicionSincro();
    bool midiendoSincro() const;

    // --- La latencia del estudio ----------------------------------------------------------
    nlohmann::json latenciaEstudio() const;
    bool bajarLatenciaEstudio();
    void ponerTopeEstudio(long long ms);

    // --- Para el núcleo -------------------------------------------------------------------
    /// El candado de las vistas: quien cambia el motor (startup, shutdown) lo toma.
    std::mutex& candado() { return vistasMu_; }
    /// Al apagar el motor: suelta las capturas vivas y espera las que están esperando un
    /// cuadro (usan el motor), y suelta las vistas de escena.
    void soltarCapturas();
    void esperarCapturas();
    void soltarVistasDeEscena();

   private:
    /// La vista chica del programa de `s` para medirlo.
    static std::string claveAlarmas(const SalidaAire& s);
    struct MedicionSincro; ///< en el .cpp

    BroadcastEngine& m_;
    /// Las vistas que lee la interfaz sin el turno: quien las cambia (con el turno) y quien
    /// cambia el motor las toma.
    mutable std::mutex vistasMu_;
    bool previewActive_ = false;
    bool vistasSinMirar_ = false; ///< soltarVistasSinMirar (vale también para el motor que se rearme)
    struct VistaPrevia {
        std::string clave, name;
        std::uint64_t serie = 0;
    };
    std::map<std::string, VistaPrevia> scenePreviews_;
    /// Las vistas de captura vivas (clave → tamaño y último uso): las miniaturas se piden
    /// seguido y armar una vista por pedido congelaba la interfaz. Se sueltan a los 10 s sin
    /// uso. Desde varios hilos (capturaMu_).
    struct VistaCaptura {
        int ancho = 0, alto = 0;
        std::chrono::steady_clock::time_point uso;
    };
    std::mutex capturaMu_;
    std::map<std::string, VistaCaptura> capturas_;
    std::atomic<int> capturasEnCurso_{0};
    /// La sincronía labios/voz (una medición a la vez, en la salida que se pidió): la cambia
    /// el dueño y la leen los hilos del motor, siempre con std::atomic_load/atomic_store.
    std::shared_ptr<MedicionSincro> medSincro_;
};

} // namespace ba::app
