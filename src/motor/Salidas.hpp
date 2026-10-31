// ============================================================================
// src/motor/Salidas.hpp — las salidas del motor: grabación y transmisión
//
// Un CODIFICADOR por fuente y perfil, y cada salida lee lo ya codificado
// (docs/arquitectura/04-plan-unificado.md, F1-07):
//
//   intersrc video:programa ! queue ! <codificador de la placa> ! h264parse ! intersink cod:N ─┐
//   intersrc bus:programa  ! queue ! audioconvert ! avenc_aac ! aacparse ! intersink cod:M ────┤
//                                                                                              │
//   grabación:   intersrc cod:N ! valve ! colchón ! h264parse ─► splitmuxsink (MKV en tramos) ◄┤
//   copia:       lo mismo, en otra carpeta (el MISMO codificador)                             ◄┤
//   transmisión: intersrc cod:N ! queue ! h264parse ─► flvmux ! rtmp2sink | mpegtsmux ! srtsink◄┤
//   respaldo de ingest y destinos extra: otra rama igual                                      ◄┘
//
// El `tee` entre el codificador y las salidas es el intersink: cada salida es
// su propio pipeline, así un disco que falla o una red que se corta no frena a
// las demás, y cada una se rearma sola sin tocar el codificador. Dos salidas con
// la misma fuente y el mismo perfil (bitrate, GOP, control de tasa) comparten el
// codificador: una sesión de NVENC por salida y perfil, no una por destino. La
// copia con un codificador propio es una opción explícita (`codificadorPropio`).
//
// - El codificador: el primero que haya de la lista de la placa (vtenc_h264_hw,
//   nvh264enc, qsvh264enc, d3d12h264enc, vtenc_h264) y x264enc de respaldo; las
//   propiedades se ponen solo si el elemento las tiene. Si uno da error en el
//   aire, va a la LISTA NEGRA de esa salida (la fuente) por lo que queda de la
//   sesión y se rearma con el siguiente; agotados, x264enc con el speed-preset
//   que permita la CPU libre (F1-06). Las salidas que lo leen se rearman solas.
// - Reloj y base_time del estudio, y la latencia de lo que lee (lo que tardan
//   los buses en salir) más lo que tarda el codificador y un margen por tipo de
//   salida (SRT, RTMP, grabación): nada llega tarde (F2-06).
// - Grabación en TRAMOS (splitmuxsink, 15 min de fábrica) con el archivo en curso
//   bajado al disco cada segundo desde un hilo aparte: un corte de luz pierde a
//   lo sumo ese segundo (F1-09). Si el pipeline da error, se cierra lo que haya y
//   sigue en `<nombre>-parte<N>`, con espera creciente; si el que falla es el
//   disco, prueba la carpeta de respaldo (F1-05). Al parar se manda EOS y se
//   espera que el archivo quede cerrado.
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
#include <set>
#include <string>
#include <thread>
#include <condition_variable>
#include <vector>

namespace ba::motor {

class Reloj;

/// Cómo reparte los bits el codificador (Configuración → Codificación, «encoder.rateControl»).
enum class ControlTasa {
    CBR,     ///< constante: lo que esperan los CDN (RTMP, SRT)
    VBR,     ///< variable con tope (el tope, 1,5 × el objetivo)
    Calidad, ///< calidad constante (CRF) con tope: la grabación «archivo»
};
/// "CBR", "VBR", "CQP"/"CRF"/"calidad" (cualquier otra cosa, CBR).
ControlTasa controlTasaDe(const std::string& nombre);
const char* nombreControlTasa(ControlTasa c);

/// Lo que define un codificador de video. Dos salidas de la misma fuente con el
/// mismo perfil comparten el codificador.
struct PerfilVideo {
    int kbps = 6000; ///< el objetivo (CBR, VBR) o el tope (Calidad)
    int gop = 60;    ///< cuadros entre claves
    ControlTasa control = ControlTasa::CBR;
    int crf = 18;    ///< con Calidad: 14 casi sin pérdida, 18 muy buena, 23 normal, 28 chico
    std::vector<std::string> codificadores; ///< preferidos (vacío = los de la placa)
};

/// La fábrica del codificador H.264 a usar: el primero que haya de `preferidos`
/// (vacío = la lista de la placa) que no esté en `salvo`, y si no, x264enc. "" si
/// no hay ninguno.
std::string elegirCodificador(const std::vector<std::string>& preferidos = {}, const std::set<std::string>& salvo = {});
/// Crea el codificador con el perfil (bitrate en kbps, un cuadro clave cada
/// `gop` cuadros, el control de tasa) en modo tiempo real (las propiedades que
/// tenga). nullptr si no se pudo.
GstElement* crearCodificador(const std::string& fabrica, const PerfilVideo& p);

/// Las fábricas que fallaron en esta sesión con esa fuente de video (la de una
/// salida): no se vuelven a elegir para ella hasta reiniciar.
std::set<std::string> fabricasCaidas(const std::string& fuenteVideo);
/// Las olvida todas (las pruebas).
void olvidarFabricasCaidas();
/// Los codificadores abiertos ahora: [{productor, fuente, tipo, fabrica, kbps,
/// control, consumidores, generacion, perdidos, error}]. Cuántas sesiones abre
/// cada salida (para `sistema_recursos` y el estado de las salidas).
nlohmann::json codificacionesAbiertas();

/// El margen de latencia que suma cada tipo de salida sobre lo que tardan los
/// buses y el codificador (Configuración → avanzado): SRT de contribución corto,
/// RTMP el de siempre, la grabación (que tiene colchón) más.
enum class TipoSalida { Srt, Rtmp, Grabacion };
std::int64_t margenSalidaNs(TipoSalida t);

class Codificacion; // en Salidas.cpp: un codificador compartido

/// Pasa `origen` (MKV) a `destino` (MP4 o MOV, según la extensión) sin recodificar.
/// Bloquea hasta terminar (llamarla en otro hilo); `progreso` recibe 0…1. "" o el error.
std::string remuxar(const std::string& origen, const std::string& destino,
                    const std::function<void(double)>& progreso = nullptr);

/// Pasa `origen` (MKV) al `formato` («mp4», «mov») al lado, con el mismo nombre, y lo
/// verifica (que el nuevo exista y no sea mucho más chico); con `borrarOrigen`, recién
/// entonces borra el MKV. Lo que hacen al terminar una grabación la app y el emisor
/// aparte (F3-01). Bloquea. "" o el error (el MKV queda si algo falló).
std::string remuxarYVerificar(const std::string& origen, const std::string& formato, bool borrarOrigen,
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
        /// llegar al colchón) y la grabación sigue.
        int colchonSeg = 30;
        ControlTasa control = ControlTasa::CBR;
        int crf = 18; ///< con Calidad
        /// Si el disco de `carpeta` falla (no se puede escribir), la parte que
        /// sigue va acá (la carpeta de la copia). Vacío = solo `carpeta`.
        std::string carpetaRespaldo;
        /// Un codificador solo para esta grabación (la copia «independiente»);
        /// si no, comparte el de la salida con el mismo perfil.
        bool codificadorPropio = false;
    };
    Grabacion(Reloj& reloj, Config cfg);
    ~Grabacion();
    Grabacion(const Grabacion&) = delete;
    Grabacion& operator=(const Grabacion&) = delete;

    /// Arranca. "" o el error. Después, si se corta sola, se rearma sola (`tic`).
    std::string iniciar(const Fuentes& f);
    /// Para y cierra el archivo (EOS, hasta `esperaMs`). true si cerró bien.
    bool parar(int esperaMs = 5000);
    /// Escribiendo ahora (con el pipeline armado).
    bool grabando() const;
    /// Pedida pero cortada: esperando para rearmarse en otra parte.
    bool caida() const;
    /// Atiende el pipeline: un error cierra lo que haya y agenda la parte que
    /// sigue (0,5-1-2-5 s); con el codificador cambiado, sigue en otra parte ya.
    void tic();
    /// {grabando, caida, parte, cortes, error, codificador, segundos, archivos, atraso_s
    /// (lo que espera al disco en el colchón), desbordes (veces que se perdieron
    /// cuadros), bytes, pausada, sincronizaciones (veces que bajó el archivo al disco)}
    nlohmann::json estado() const;
    /// Los archivos (tramos de todas las partes) que se escribieron.
    std::vector<std::string> archivos() const;
    /// Lo que espera al disco en el colchón, en segundos (el más atrasado).
    double atrasoS() const;
    /// Pausada no entra nada nuevo (una válvula al principio): el archivo sigue
    /// abierto y no escribe, así el disco queda para otra. Al soltarla sigue
    /// en el mismo archivo (con el salto, desde el próximo cuadro clave). No espera a nadie.
    void pausar(bool si);
    bool pausada() const { return pausada_.load(); }
    /// Para las pruebas: el próximo cuadro que sale del colchón de video espera
    /// `ms` (como un disco trabado: lo que viene se junta en el colchón).
    void simularDiscoTrabado(int ms);

   private:
    std::string armar();                   ///< con mu_: arma la parte en curso
    bool cerrar(int esperaMs);             ///< con mu_: EOS (hasta esperaMs) y suelta el pipeline; true si cerró bien
    std::string carpetaParaParte() const;  ///< con mu_: la principal o la de respaldo
    void bucleSincronizar();               ///< el hilo que baja el archivo en curso al disco

    Reloj& reloj_;
    MedidorEdad edadSalida_; ///< la edad de lo que sale (medida)
    Config cfg_;
    Fuentes fuentes_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    std::shared_ptr<Codificacion> codV_;
    std::vector<std::shared_ptr<Codificacion>> codA_;
    std::uint64_t generacionV_ = 0; ///< la del codificador de video con que se armó
    std::vector<GstElement*> colchones_, valvulas_; ///< del pipeline (con ref)
    guint64 atrasoNs() const; ///< con mu_ tomado
    std::atomic<std::uint64_t> desbordes_{0}, bytes_{0}, sincronizaciones_{0};
    std::atomic<std::int64_t> latenciaNs_{0}; ///< la que se le fijó al pipeline (estado: latencia_ms)
    std::atomic<bool> pausada_{false};
    std::atomic<bool> esperarClave_{true}; ///< el video entra recién desde un cuadro clave
    bool pedida_ = false;
    int parte_ = 1, cortes_ = 0, intentos_ = 0;
    std::int64_t reintentoNs_ = 0, caidaDesdeNs_ = 0;
    std::string error_, codificador_;
    mutable std::mutex arMu_; ///< archivos_ y la ruta en curso (las toca el hilo de datos de splitmuxsink)
    std::vector<std::string> archivos_;
    std::string carpetaParte_, nombreParte_; ///< las de la parte en curso (con arMu_)
    std::int64_t desdeNs_ = 0;
    // El hilo que baja el archivo en curso al disco (F1-09).
    std::thread sincronizador_;
    std::mutex sincMu_;
    std::condition_variable sincCv_;
    bool sincSeguir_ = false;
};

class Transmision {
   public:
    struct Config {
        /// rtmp://servidor/app/clave (o rtmps://) | srt://host:puerto[?mode=listener&…]
        std::string url;
        int kbps = 6000, kbpsAudio = 160, gopSeg = 2, fps = 30;
        std::vector<std::string> codificadores; ///< preferidos (vacío = los de la placa)
        /// CBR o VBR con tope (Calidad no sirve para la red: se toma como VBR).
        ControlTasa control = ControlTasa::CBR;
        /// El punto de ingest de respaldo (la misma clave): sale a los dos a la vez,
        /// con el mismo codificador, y cada uno se reconecta por su lado.
        std::string urlRespaldo;
    };
    Transmision(Reloj& reloj, Config cfg);
    ~Transmision();
    Transmision(const Transmision&) = delete;
    Transmision& operator=(const Transmision&) = delete;

    /// Arranca (y queda reintentando si se cae: 1, 2, 4, 8 s, después cada 10 s). "" o el error.
    std::string iniciar(const Fuentes& f);
    void parar();
    bool andando() const;   ///< pedida (aunque esté reconectando)
    bool conectada() const; ///< mandando datos (a alguno de los dos puntos)
    void tic();
    /// {andando, conectada, reconectando, intentos, kbps, cuadros, perdidos, error, codificador,
    /// url (sin la clave), respaldo: {url, conectada, reconectando, intentos, kbps, error} si hay}
    nlohmann::json estado() const;

    struct Envio; ///< un punto de ingest: su pipeline y su reconexión

   private:
    std::string armar(Envio& e);
    void desarmar(Envio& e);
    void atender(Envio& e, std::int64_t ahora);
    nlohmann::json estadoDe(const Envio& e) const;
    static GstPadProbeReturn alSalir(GstPad*, GstPadProbeInfo*, gpointer);
    static GstPadProbeReturn alMandar(GstPad*, GstPadProbeInfo*, gpointer);

    Reloj& reloj_;
    Config cfg_;
    Fuentes fuentes_;
    mutable std::mutex mu_;
    std::vector<std::unique_ptr<Envio>> envios_; ///< el principal y, si hay, el de respaldo
    std::shared_ptr<Codificacion> codV_, codA_;
    bool pedida_ = false;
    std::string codificador_;
    std::uint64_t perdidosBase_ = 0;
    std::atomic<std::uint64_t> perdidos_{0}; ///< lo que tiran las colas (la red o la máquina no dan abasto)
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
