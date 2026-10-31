// ============================================================================
// src/motor/Salidas.hpp — las salidas del motor: grabación y transmisión
//
// Cada salida es un pipeline propio que lee los buses del motor:
//
//   intersrc video:programa ! valve ! queue (cruda) ! <codificador> ! h264parse ! queue (colchón) ─┐
//   intersrc bus:programa  ! valve ! queue ! audioconvert ! avenc_aac ! aacparse ! queue (colchón) ┼─► splitmuxsink
//   intersrc bus:pista2… (las pistas pedidas) ! … avenc_aac ! aacparse ! queue (colchón) ──────────┘  (matroskamux)
//
//   (transmisión: intersrc ! queue ! <codificador> ! h264parse ! queue ─► flvmux ! rtmp2sink
//                 ó … ─► mpegtsmux ! srtsink; el audio, solo la primera pista)
//
// - El codificador: el primero que haya de la lista de la placa, que es por sistema
//   (codificadoresDeLaPlaca en Salidas.cpp: VideoToolbox en la Mac; NVENC, QSV,
//   D3D12, AMF y Media Foundation en Windows; NVENC, QSV y VA en Linux; con el video
//   en D3D12, primero los que reciben D3D12Memory) y x264enc de respaldo; las
//   propiedades se ponen solo si el elemento las tiene. Una transmisión que se cae
//   se rearma con espera creciente eligiendo de la MISMA lista: un codificador que
//   falla se vuelve a elegir (no hay lista de los que fallaron).
// - Reloj y base_time del estudio, y la latencia de lo que lee (lo que tardan
//   los buses en salir) más un margen: nada llega tarde.
// - Grabación: de fábrica un solo MKV por grabación (`record.tramoMin` = 0). Matroska
//   se puede abrir aunque se corte la luz, pero se pierde lo que no llegó al disco
//   (las colas y lo que el sistema no bajó todavía). Con `record.tramoMin` > 0,
//   tramos de esos minutos. Al parar se manda EOS y se espera que el archivo quede
//   cerrado. Una grabación que da error queda parada (no se rearma sola).
// ============================================================================
#pragma once

#include "Edad.hpp"

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

/// Configuración → Transmisión: la espera más larga entre reconexiones (1, 2, 4,
/// 8… s hasta este tope).
void ponerReintentoTransmision(int maxS);

class Grabacion {
   public:
    struct Config {
        std::string carpeta;     ///< dónde
        std::string nombre;      ///< sin extensión; los tramos se numeran: <nombre>-00000.mkv
        int tramoSeg = 600;      ///< 0 = un solo archivo
        int maxTramos = 0;       ///< > 0: se guardan solo los últimos N (los viejos se borran: la repetición)
        int kbps = 8000, kbpsAudio = 192, gopSeg = 2, fps = 30;
        std::vector<std::string> codificadores; ///< preferidos (vacío = los de la placa)
        /// El colchón en memoria entre el codificador y el archivo (ya comprimido:
        /// un minuto a 8 Mb/s son 60 MB). Si el disco se traba, se junta acá y no
        /// se pierde nada mientras entre; pasado eso se pierden cuadros (antes de
        /// codificar) y la grabación sigue.
        int colchonSeg = 30;
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
    /// {grabando, error, codificador, segundos, archivos, atraso_s (lo que espera
    /// al disco en el colchón), desbordes (veces que se perdieron cuadros), bytes, pausada}
    nlohmann::json estado() const;
    /// Los archivos (tramos) que se escribieron.
    std::vector<std::string> archivos() const;
    /// Lo que espera al disco en el colchón, en segundos (el más atrasado).
    double atrasoS() const;
    /// Pausada no entra nada nuevo (una válvula al principio): el archivo sigue
    /// abierto y no escribe, así el disco queda para otra. Al soltarla sigue
    /// en el mismo archivo (con el salto). No espera a nadie.
    void pausar(bool si);
    bool pausada() const { return pausada_.load(); }
    /// Para las pruebas: el próximo cuadro que sale del colchón de video espera
    /// `ms` (como un disco trabado: lo que viene se junta en el colchón).
    void simularDiscoTrabado(int ms);

   private:
    Reloj& reloj_;
    MedidorEdad edadSalida_; ///< la edad de lo que sale (medida)
    Config cfg_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    std::vector<GstElement*> colchones_, valvulas_; ///< del pipeline (con ref)
    guint64 atrasoNs() const; ///< con mu_ tomado
    std::atomic<std::uint64_t> desbordes_{0}, bytes_{0};
    std::atomic<std::int64_t> latenciaNs_{0}; ///< la que se le fijó al pipeline (estado: latencia_ms)
    std::atomic<bool> pausada_{false};
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
    MedidorEdad edadSalida_; ///< la edad de lo que sale (medida)
    Config cfg_;
    Fuentes fuentes_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    bool pedida_ = false;
    std::string error_, codificador_;
    int intentos_ = 0;
    std::int64_t reintentoNs_ = 0;
    std::atomic<std::uint64_t> bytes_{0}, cuadros_{0}, perdidos_{0};
    std::atomic<std::int64_t> latenciaNs_{0}; ///< la que se le fijó al pipeline (estado: latencia_ms)
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
    void pausar(bool si) {
        if (grab_) grab_->pausar(si);
    }
    double atrasoS() const { return grab_ ? grab_->atrasoS() : 0.0; }

   private:
    std::string carpeta_;
    int segundos_ = 30;
    std::unique_ptr<Grabacion> grab_;
};

} // namespace ba::motor
