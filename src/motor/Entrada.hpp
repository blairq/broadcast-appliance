// ============================================================================
// src/motor/Entrada.hpp — una entrada del motor: su pipeline de GStreamer
//
// Lo mismo que hacía `ba_gst_fuente` adentro de OBS, sin OBS. Arma la cadena
// con los mismos ajustes (JSON) y la termina en dos `intersink`:
//     <id>:video   el video normalizado al lienzo (formato, color, cuadros, píxel cuadrado)
//     <id>:audio   F32, 48 kHz, estéreo
// Los consumidores (la mezcla de audio, la composición, el monitor de la
// entrada) los leen con `intersrc producer-name=…`, en otros pipelines.
//
// AJUSTES (los de ba_gst_fuente):
//   uri              srt://, rtmp://, rtsp://, udp://, http(s)://, file://
//   pipeline         la parte de origen del video, como en gst-launch
//   pipeline_audio   la parte de origen del audio
//   camara, camara_modo   una cámara del equipo por su id estable («1920x1080@30»)
//   microfono        el id estable del micrófono que usa `pipeline_audio` (para esperarlo)
//   rack             el rack de audio (barack fuente=<rack>), primero en la cadena del audio
//   filtros          {"video":[…], "audio":[…], "alfa":bool} (JSON o texto JSON)
//   filtros_audio    [… ] (los de audio, aparte)
//   normalizar (true), audio (true), estudio (true), latencia, desfase (ms), ntp_sync (true)
//   superres, superres_motor, superres_modelo   (la superresolución vieja: un filtro más)
//   medio (false)    un archivo con controles (play/pausa/ir a/bucle): `local_file`,
//                    `looping`, `clear_on_media_end`
//
// LA HORA: una entrada en vivo usa el reloj y el base_time del estudio: sus
// horas valen tal cual en la mezcla. No espera al reloj: entrega apenas tiene
// (intersink sync=false); los mezcladores esperan la latencia del estudio.
// Un archivo marca su propio ritmo (identity sync=true) con su propio base_time
// (cada salto lo cambia) y el pad que entrega lleva la diferencia (offset).
//
// Se rearma sola: el dispositivo que no está se espera (vigía de dispositivos),
// un error se reintenta con espera creciente (0,2 / 0,5 / 1 / 2 s), y una
// entrada en vivo que deja de dar datos 10 s sin error se rearma.
// ============================================================================
#pragma once

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

class Entrada {
   public:
    Entrada(Reloj& reloj, const Lienzo& lienzo, std::string id, const nlohmann::json& ajustes);
    ~Entrada();
    Entrada(const Entrada&) = delete;
    Entrada& operator=(const Entrada&) = delete;

    const std::string& id() const { return id_; }
    /// Cambia los ajustes: rearma solo si hace falta (las propiedades vivas de los filtros, en vivo).
    void actualizar(const nlohmann::json& ajustes);
    /// Lo llama el motor seguido (~20 veces por segundo): bus, reintentos, vigías, latencia.
    void tic();
    nlohmann::json estado() const;

    bool daVideo() const;
    bool daAudio() const;
    std::string productorVideo() const { return id_ + ":video"; }
    std::string productorAudio() const { return id_ + ":audio"; }
    /// Lo que pide de latencia (ns, 0 si no midió) y si pasa el tope del estudio.
    std::int64_t latenciaPropia() const { return pide_.load(); }
    bool llegaTarde() const { return llegaTarde_.load(); }

    // --- Medios (medio = true) ---
    /// play, pausa, reiniciar, parar. "" o el error.
    std::string controlMedio(const std::string& accion);
    void irA(std::int64_t ms);
    /// none, playing, paused, stopped, ended, error
    std::string estadoMedio() const;
    std::int64_t tiempoMs() const;
    std::int64_t duracionMs() const;

   private:
    void leer(const nlohmann::json& a);
    void armar();
    void desarmar();
    void programarReintento();
    void procesarMensaje(GstMessage* m);
    void medirLatencia();
    void vigilarDatos();
    void irANs(std::int64_t ns);
    void corregirOffset();
    static GstPadProbeReturn alSalir(GstPad* pad, GstPadProbeInfo* info, gpointer d);

    Reloj& reloj_;
    Lienzo lienzo_;
    std::string id_;
    mutable std::mutex mu_;

    // Lo pedido
    std::string uri_, descripcion_, descripcionAudio_, camara_, camaraModo_, microfono_, rack_;
    std::string filtrosJson_, superresMotor_, superresModelo_;
    std::vector<Filtro> filtrosV_, filtrosA_;
    bool alfa_ = false, normalizar_ = true, conAudio_ = true, estudio_ = true, ntpSync_ = true, superres_ = false;
    std::int64_t latenciaMs_ = 0, desfaseMs_ = 0;
    bool medio_ = false, bucle_ = false, limpiarAlTerminar_ = true;
    std::string firma_; ///< lo que obliga a rearmar

    // Lo armado
    GstElement* pipeline_ = nullptr;
    std::string error_, errorAnotado_;
    std::uint64_t errorAnotadoNs_ = 0;
    std::vector<std::string> avisos_;
    bool esperaDispositivo_ = false;
    std::uint64_t genVista_ = 0, esperaDesdeNs_ = 0;
    std::uint64_t reintentoNs_ = 0;
    int reintentos_ = 0;
    std::uint64_t rackFaltaDesde_ = 0;
    GstClockTime baseAplicada_ = GST_CLOCK_TIME_NONE; ///< (medios) el base_time con que se puso el offset
    std::int64_t realineadoNs_ = 0; ///< lo en vivo que trae sus propias horas (no las del estudio): la corrección
    bool ntpApagadoSolo_ = false;

    // Medios
    std::string estadoMedio_ = "none";
    std::int64_t buscarNs_ = -1;
    bool segmentoListo_ = false;

    // Lo que se mide al salir (hilos de GStreamer)
    mutable std::mutex medMu_;
    std::deque<double> edades_; ///< lo viejo que salen los últimos cuadros (ms)
    std::uint64_t cuadros_ = 0, bloques_ = 0;
    std::uint64_t ultimoDatoNs_ = 0;
    nlohmann::json origen_ = nlohmann::json::object(); ///< cómo llega la señal (antes de normalizar)
    nlohmann::json sale_ = nlohmann::json::object();   ///< cómo sale
    double declaradoMs_ = 0, edadP50_ = 0, edadP95_ = 0;
    std::uint64_t consultaNs_ = 0;
    std::uint64_t vistosDatos_ = 0, datosDesdeNs_ = 0;
    std::atomic<std::int64_t> pide_{0};
    std::atomic<bool> llegaTarde_{false};
};

} // namespace ba::motor
