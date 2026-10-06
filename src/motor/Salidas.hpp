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

#include <cstdint>
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

} // namespace ba::motor
