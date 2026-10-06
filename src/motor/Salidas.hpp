// ============================================================================
// src/motor/Salidas.hpp — las salidas del motor: grabación y transmisión
//
// Cada salida es un pipeline propio que lee los buses del motor:
//
//   intersrc video:programa ! queue ! <codificador de la placa> ! h264parse ─┐
//   intersrc bus:programa  ! queue ! audioconvert ! avenc_aac ! aacparse ────┼─► splitmuxsink (MKV en tramos)
//   intersrc bus:pista2… (las pistas pedidas) ! … avenc_aac ! aacparse ───────┘
//
//   (transmisión: … ─► flvmux ! rtmp2sink   ó   … ─► mpegtsmux ! srtsink)
//
// - El codificador: el primero que haya de la lista de la placa (vtenc_h264_hw,
//   nvh264enc, qsvh264enc, d3d12h264enc, vtenc_h264) y x264enc de respaldo; las
//   propiedades se ponen solo si el elemento las tiene.
// - Reloj y base_time del estudio, y la latencia de lo que lee (lo que tardan
//   los buses en salir) más un margen: nada llega tarde.
// - Grabación en TRAMOS (splitmuxsink): un corte de luz pierde a lo sumo el
//   último. Al parar se manda EOS y se espera que el archivo quede cerrado.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ba::motor {

class Reloj;

/// La fábrica del codificador H.264 a usar: el primero que haya de `preferidos`
/// (vacío = la lista de la placa) y si no, x264enc. "" si no hay ninguno.
std::string elegirCodificador(const std::vector<std::string>& preferidos = {});
/// Crea el codificador con bitrate (kbps), un cuadro clave cada `gop` cuadros y
/// en modo tiempo real (las propiedades que tenga). nullptr si no se pudo.
GstElement* crearCodificador(const std::string& fabrica, int kbps, int gop);

/// Pasa `origen` (MKV) a `destino` (MP4 o MOV, según la extensión) sin recodificar.
/// Bloquea hasta terminar (llamarla en otro hilo); `progreso` recibe 0…1. "" o el error.
std::string remuxar(const std::string& origen, const std::string& destino,
                    const std::function<void(double)>& progreso = nullptr);

/// Junta tramos (MKV seguidos de una misma grabación) en un solo archivo, sin
/// recodificar. Bloquea. "" o el error.
std::string juntar(const std::vector<std::string>& tramos, const std::string& destino);

/// Lo que lee una salida: el video y los audios (productores de intersink) y
/// cuánto tardan en salir (ns).
struct Fuentes {
    std::string video;               ///< "video:programa" ("" = sin video)
    std::int64_t latVideoNs = 0;
    std::vector<std::string> audios; ///< "bus:programa", "bus:pista2"… (la primera es la pista 1)
    std::int64_t latAudioNs = 0;
};

class Grabacion {
   public:
    struct Config {
        std::string carpeta;     ///< dónde
        std::string nombre;      ///< sin extensión; los tramos se numeran: <nombre>-00000.mkv
        int tramoSeg = 600;      ///< 0 = un solo archivo
        int maxTramos = 0;       ///< > 0: se guardan solo los últimos N (los viejos se borran: la repetición)
        int kbps = 8000, kbpsAudio = 192, gopSeg = 2, fps = 30;
        std::vector<std::string> codificadores; ///< preferidos (vacío = los de la placa)
    };
    Grabacion(Reloj& reloj, Config cfg);
    ~Grabacion();
    Grabacion(const Grabacion&) = delete;
    Grabacion& operator=(const Grabacion&) = delete;

    /// Arranca. "" o el error.
    std::string iniciar(const Fuentes& f);
    /// Para y cierra el archivo (EOS, hasta `esperaMs`). true si cerró bien.
    bool parar(int esperaMs = 5000);
    bool grabando() const;
    void tic();
    nlohmann::json estado() const;
    /// Los archivos (tramos) que se escribieron.
    std::vector<std::string> archivos() const;

   private:
    Reloj& reloj_;
    Config cfg_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    std::string error_, codificador_;
    mutable std::mutex arMu_; ///< archivos_ (lo toca el hilo de datos de splitmuxsink)
    std::vector<std::string> archivos_;
    std::int64_t desdeNs_ = 0;
};

class Transmision {
   public:
    struct Config {
        /// rtmp://servidor/app/clave (o rtmps://) | srt://host:puerto[?mode=listener&…]
        std::string url;
        int kbps = 6000, kbpsAudio = 160, gopSeg = 2, fps = 30;
        std::vector<std::string> codificadores; ///< preferidos (vacío = los de la placa)
    };
    Transmision(Reloj& reloj, Config cfg);
    ~Transmision();
    Transmision(const Transmision&) = delete;
    Transmision& operator=(const Transmision&) = delete;

    /// Arranca (y queda reintentando si se cae: 1, 2, 4, 8 s, después cada 10 s). "" o el error.
    std::string iniciar(const Fuentes& f);
    void parar();
    bool andando() const;   ///< pedida (aunque esté reconectando)
    bool conectada() const; ///< mandando datos
    void tic();
    /// {andando, conectada, reconectando, intentos, kbps, cuadros, perdidos, error, codificador, url (sin la clave)}
    nlohmann::json estado() const;

   private:
    std::string armar();
    void desarmar();
    static GstPadProbeReturn alSalir(GstPad*, GstPadProbeInfo*, gpointer);
    static GstPadProbeReturn alCodificar(GstPad*, GstPadProbeInfo*, gpointer);

    Reloj& reloj_;
    Config cfg_;
    Fuentes fuentes_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    bool pedida_ = false;
    std::string error_, codificador_;
    int intentos_ = 0;
    std::int64_t reintentoNs_ = 0;
    std::atomic<std::uint64_t> bytes_{0}, cuadros_{0}, perdidos_{0};
    std::uint64_t bytesAntes_ = 0;
    std::int64_t medidoNs_ = 0, ultimoDatoNs_ = 0;
    double kbps_ = 0;
};

/// La repetición: graba sin parar en tramos de 2 s y se queda con los últimos
/// `segundos`; `guardar` junta esos tramos en un clip (sin recodificar).
class Repeticion {
   public:
    Repeticion(Reloj& reloj, std::string carpetaTramos, int segundos, int kbps);
    ~Repeticion();
    std::string iniciar(const Fuentes& f);
    /// Junta lo que hay (hasta `segundos`) en `destino`. Bloquea. "" o el error.
    std::string guardar(const std::string& destino);
    bool andando() const;
    void tic();
    int segundos() const { return segundos_; }

   private:
    std::string carpeta_;
    int segundos_ = 30;
    std::unique_ptr<Grabacion> grab_;
};

} // namespace ba::motor
