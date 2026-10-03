// ============================================================================
// src/obs-module/ba_gst.hpp — GStreamer adentro del grafo de video
//
// Dos usos:
//   - TarjetaGst: una tarjeta del grafo que pasa la imagen por un módulo de
//     GStreamer (videobalance, coloreffects, gaussianblur, frei0r-…). La
//     textura se baja de la GPU, entra por `appsrc`, pasa por
//       videoconvert ! <módulo> ! videoconvert ! videoscale ! RGBA WxH
//     y sale por `appsink` para volver a la GPU. Las propiedades del módulo
//     se cambian EN VIVO (sin rearmar el pipeline).
//   - La fuente `ba_gst_fuente` (ba_gst_fuente.cpp): un pipeline de GStreamer
//     como entrada de video (señal de ajuste, RTSP, SRT, NDI…).
//
// GStreamer viene en el bundle y usa SOLO sus plugins
// (ba::platform::prepararGst). Sin GStreamer en el bundle, las tarjetas pasan
// la imagen sin tocar y la fuente queda en negro (con el motivo en el log).
//
// Hilos: la tarjeta se usa en el hilo gráfico de libobs (subir/bajar
// texturas); el pipeline corre en sus propios hilos (GStreamer).
// ============================================================================
#pragma once

#if defined(BA_HAVE_GST)

#include <obs-module.h>

#include <cstdint>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

typedef struct _GstElement GstElement;

namespace ba::gst {

/// Inicia GStreamer una sola vez (con el entorno del bundle). false si no hay.
bool iniciar();
/// Sin esperar: true si GStreamer ya está listo. La primera llamada lo
/// inicia en un hilo aparte (la primera vez revisa todos sus plugins).
bool listo();
/// true si ya se intentó y no hay GStreamer.
bool fallo();
/// Publica el reloj de este aparato en la red (GstNetTimeProvider) para que
/// otros se sincronicen con él (`reloj: red`). puerto 0 = apagar.
bool publicarReloj(int puerto, std::string& error);
/// El puerto en el que se publica (0: no se publica).
int relojPublicado();
/// Los plugins de Python (ba_plugin_gst.cpp): registra `baplugin` y
/// `bapluginsrc` (lo hace iniciar()) y anota el puerto de medios de un plugin
/// (0 = no está corriendo).
void registrarElementosPlugin();
void puertoPlugin(const std::string& plugin, int puerto);
/// El filtro de audio de OBS `ba_plugin_audio` (una tarjeta de audio de un plugin).
void registrarFiltroAudioPlugin();
/// El motivo si iniciar() falló.
const std::string& errorInicio();

/// Una tarjeta del grafo que corre un módulo de GStreamer.
class TarjetaGst {
   public:
    TarjetaGst(std::string elemento, std::map<std::string, std::string> propiedades);
    ~TarjetaGst();
    TarjetaGst(const TarjetaGst&) = delete;
    TarjetaGst& operator=(const TarjetaGst&) = delete;

    const std::string& elemento() const { return elemento_; }
    /// Cambia propiedades en vivo (las que no cambiaron, no se tocan). Si se
    /// sacó alguna (vuelve a fábrica), el pipeline se rearma en el próximo cuadro.
    void ajustar(const std::map<std::string, std::string>& propiedades);
    /// Pasa `entrada` (cx x cy, alfa premultiplicado) por el módulo y dibuja el
    /// resultado en el render target actual (ya preparado con gs_ortho).
    /// `fx` es el efecto del grafo (técnicas Despremultiplicar/Premultiplicar).
    /// false si no se pudo (entonces el llamador copia la entrada).
    bool procesar(gs_texture_t* entrada, std::uint32_t cx, std::uint32_t cy, gs_effect_t* fx);
    /// Lo último que pasó (para el log y la interfaz).
    const std::string& error() const { return error_; }
    /// Algo a saber que no es un error (p. ej. que trabaja en 4:2:0).
    const std::string& aviso() const { return aviso_; }
    /// En qué formato le llega la imagen al módulo (tras negociar).
    const std::string& formato() const { return formato_; }
    /// Cuánto esperar por el cuadro de ahora: 0 (de fábrica) entrega el
    /// anterior y nunca frena el video; más, sin latencia pero puede frenar.
    void setEspera(int ms) { esperaMs_ = ms < 0 ? 0 : (ms > 100 ? 100 : ms); }
    /// La latencia que agrega, en cuadros.
    int latenciaCuadros() const { return esperaMs_ > 0 ? 0 : 1; }

   private:
    bool armar(std::uint32_t cx, std::uint32_t cy);
    void avisar();
    void revisarFormato();
    void desarmar();

    std::string elemento_;
    std::map<std::string, std::string> props_;
    std::string error_;
    std::string avisado_; ///< el último error que fue al log
    GstElement* pipeline_ = nullptr;
    GstElement* src_ = nullptr;
    GstElement* sink_ = nullptr;
    GstElement* modulo_ = nullptr;
    std::uint32_t cx_ = 0, cy_ = 0;
    std::uint64_t t0_ = 0;         ///< la hora de libobs del primer cuadro
    int fpsNum_ = 30, fpsDen_ = 1; ///< los del lienzo
    int esperaMs_ = 0;             ///< cuánto esperar el cuadro de ahora (0: usar el anterior)
    bool revisado_ = false;        ///< ya se miró en qué formato trabaja el módulo
    std::string formato_, aviso_;
    gs_texrender_t* bajada_ = nullptr; ///< la entrada despremultiplicada, para bajarla
    gs_stagesurf_t* stage_ = nullptr;
    gs_texture_t* subida_ = nullptr; ///< lo que devolvió GStreamer
    bool haySubida_ = false;
    std::mutex propsLock_; ///< ajustar (el hilo del plan) contra armar (el de video)
    std::atomic<bool> rearmar_{false};
};

} // namespace ba::gst

#endif
