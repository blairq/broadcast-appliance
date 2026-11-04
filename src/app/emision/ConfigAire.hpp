// ============================================================================
// src/app/emision/ConfigAire.hpp — Los ajustes del aire, sin el motor
//
// Las estructuras de valor que van y vienen entre la configuración (arranque/AppSettings),
// el runtime y el motor (emision/BroadcastEngine): el video, la codificación, la
// grabación, la transmisión, cada fuente con su lugar y sus efectos, y el estado que el
// motor muestra. Antes vivían en BroadcastEngine.hpp: quien solo lee o guarda la
// configuración no necesita el motor (Ola 3 del diseño físico,
// docs/local/PLAN-DISENO-FISICO.md). BroadcastEngine.hpp lo incluye: para el motor no
// cambia nada.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "json/JsonFwd.hpp"

namespace ba::app {

/// Resolucion y cadencia del pipeline de video (spec §7.1).
struct VideoConfig {
    int baseWidth = 1920;   ///< lienzo de composicion
    int baseHeight = 1080;  ///< lienzo de composicion
    int outputWidth = 1920; ///< resolucion de salida (escalada desde el lienzo)
    int outputHeight = 1080;
    int fpsNum = 30;
    int fpsDen = 1;              ///< 1001 para 23,976 / 29,97 / 59,94
    std::string formato = "nv12"; ///< lo que reciben los codificadores: nv12 | i420 | i444 | p010 | i010
    std::string color = "709";    ///< espacio de color: 601 | 709 | srgb | 2100pq | 2100hlg (HDR: 10 bits)
    std::string rango = "limitado"; ///< limitado (broadcast) | completo
};

/// "30", "29.97", "30000/1001" -> num/den. false si no se entiende.
bool fpsDeTexto(const std::string& texto, int& num, int& den);
/// num/den -> "30" o "30000/1001" (lo que se guarda y se muestra en Ajustes).
std::string textoDeFps(int num, int den);
/// num/den -> "29,97" para mostrar.
std::string fpsLegible(int num, int den);
/// El tamaño del lienzo del motor andando (false si no hay motor).
bool lienzoActual(int& ancho, int& alto);
/// Los cuadros por segundo del motor andando (30 si no hay motor).
double fpsActual();

/// Formato del pipeline de audio (spec §7.4: 48 kHz es el reloj del proyecto).
struct AudioConfig {
    int sampleRate = 48000;
    int channels = 2;
};

/// Parametros de codificacion.
struct EncoderConfig {
    int videoBitrateKbps = 6000;
    int audioBitrateKbps = 160;
    int keyintSec = 2;               ///< intervalo de keyframe (2 s es el estandar de RTMP)
    std::string preset = "veryfast"; ///< preset de x264
    std::string rateControl = "CBR"; ///< CBR es lo que esperan los CDN
    bool preferHardware = true;      ///< con "auto": probar hardware antes que x264
    /// Codificador de video: un alias.
    ///   "auto"         -> hardware de la plataforma en orden (Windows: NVENC,
    ///                     QSV, AMF; Linux: NVENC, VAAPI; macOS: VideoToolbox)
    ///   "nvenc" | "qsv" | "amf" | "vaapi" | "videotoolbox" | "x264"
    /// Siempre termina en x264 como red de seguridad: si el elegido no esta o
    /// no arranca (driver roto, GPU ocupada), se cae al siguiente.
    std::string videoEncoder = "auto";
};

/// Rectangulo en FRACCIONES del lienzo (0..1): independiente de la resolucion.
struct CanvasRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 1.0f;
    float h = 1.0f;
};

/// Efectos de un elemento EN LA ESCENA: transparencia, borde difuso y sombra (o
/// resplandor: distancia 0 y un color claro). Medidas en píxeles del lienzo; el
/// ángulo, hacia dónde cae la sombra (0 = derecha, 90 = abajo, 45 = abajo a la derecha).
/// En JSON (guardado y MCP):
///   {"opacidad": 1, "borde": 0, "esquinas": 0,
///    "sombra": {"activa": true, "color": "#000000", "alfa": 0.6, "difuminado": 20,
///               "distancia": 10, "angulo": 45, "tamano": 0}}
struct EfectosCapa {
    float opacidad = 1.0f; ///< de todo el elemento (y su sombra)
    float borde = 0;       ///< borde difuso: cuántos píxeles del borde se funden
    float esquinas = 0;    ///< radio de las esquinas redondeadas
    bool sombra = false;
    std::string color = "#000000"; ///< de la sombra: "#RRGGBB" o "#AARRGGBB"
    float alfa = 0.6f;             ///< opacidad de la sombra
    float difuminado = 20, distancia = 10, angulo = 45, tamano = 0;
    bool vacio() const { return opacidad >= 1.0f && borde <= 0 && esquinas <= 0 && !sombra; }
    /// Cambia lo que traiga `j` (lo demás queda); false + `error` si algo no sirve.
    bool aplicar(const nlohmann::json& j, std::string* error = nullptr);
    nlohmann::json aJson() const;
    /// Lo que entiende el motor en la capa (`sombra`, ver ba::motor::SombraCapa); null si no hay.
    nlohmann::json sombraMotor() const;
};

struct SourceSpec {
    std::string id;                  ///< tipo: "ba_gst_fuente", "ba_gst_medio", "image_source"...
    std::string name;                ///< nombre unico en la escena
    std::string settingsJson = "{}"; ///< ajustes de la fuente (los de su tipo)
    CanvasRect rect;                 ///< ubicacion (ignorado en fuentes solo-audio)
    bool visible = true;
    float volume = 1.0f; ///< ganancia lineal (1 = 0 dB)
    bool muted = false;
    /// Recorte en la escena: izquierda, arriba, derecha, abajo (fracciones de su tamaño).
    float recorte[4] = {0, 0, 0, 0};
    EfectosCapa efectos; ///< en la escena
};

/// Tipo de fuente disponible (para "Agregar fuente").
struct SourceType {
    std::string id;          ///< tipo ("ba_gst_fuente", "image_source"...)
    std::string displayName; ///< nombre legible que da el plugin
    bool video = false;
    bool audio = false;
};

/// Destino de transmision (spec §8).
struct StreamConfig {
    std::string server; ///< p.ej. "rtmp://a.rtmp.youtube.com/live2"
    std::string key;    ///< clave de transmision (sale de la boveda de secretos)
};

/// Destino de grabacion de respaldo (layout §5.1: Recordings/).
struct RecordConfig {
    std::string directory;         ///< carpeta destino
    std::string container = "mkv"; ///< mkv sobrevive a un corte de energia; mp4 no
    /// Con mp4/mov: se graba en MKV (sobrevive a un corte) y al terminar se
    /// pasa al formato pedido sin recodificar (el MKV se borra si salió bien).
    bool remuxAlTerminar = true;
    /// "como_stream": comparte los codificadores de la transmisión (no cuesta otro
    /// encode). "archivo": codificador propio a calidad constante (CRF/CQP), pensado
    /// para editar después; es un encode más.
    std::string calidad = "como_stream";
    int calidadCrf = 18; ///< con "archivo": 14 (casi sin pérdida) a 28 (chico)
    /// Pistas de audio EXTRA, numeradas como en la Matriz (2..6): cada una con lo que
    /// la Matriz le cruce. La 1 (el programa) va siempre.
    std::vector<int> pistas;
    /// Segunda copia simultánea en otro disco ("" = sin copia). Mismos codificadores (el
    /// mismo encode; uno propio solo con «record.copiaCodificadorPropio»). Si el disco de la
    /// grabación falla, la parte que sigue va acá.
    std::string copiaDir;
    /// Segundos de grabación que se juntan en memoria si el disco se traba
    /// (Configuración → Grabación: «Colchón si el disco se traba»).
    int colchonSeg = 60;
};

/// Rutas del layout portable que el motor necesita (spec §5.1/§5.3).
struct EnginePaths {
    std::string configDir; ///< Config/
};

/// Estado observable del motor, para la UI y el bloque de salud.
struct EngineStatus {
    bool videoActive = false;
    bool audioActive = false;
    bool streaming = false;
    bool recording = false;
    /// La grabación de la por omisión está pedida pero cortada: el motor la rearma sola
    /// en otra parte (F1-05). Mientras tanto `recording` es false y NO hay que pararla.
    bool recordingDown = false;
    int droppedFrames = 0;
    int totalFrames = 0;
    double fps = 0.0;
    std::uint64_t bytesSent = 0;
    std::string videoEncoder; ///< el codificador en uso ("" si ninguno)
    std::string lastError;
    /// Se corto la transmision y se esta reintentando. Mientras esto es
    /// true, `streaming` puede seguir en true: la salida sigue activa, pero
    /// no esta llegando nada.
    bool reconnecting = false;
    /// En castellano y con la CAUSA: "Se cortó la conexión con el servidor",
    /// "El servidor rechazó la clave"... Vacio si no hubo ningun problema.
    /// Es lo que el operador necesita para saber si esperar o hacer algo.
    std::string outputProblem;
    /// Cada salida (PLAN-SALIDAS), la por omisión primera. Los campos de arriba
    /// son los de la por omisión, como siempre.
    struct Salida {
        std::string id, nombre;
        bool porOmision = false;
        std::string escena;      ///< al aire en esa salida
        bool streaming = false;  ///< su transmisión principal
        bool reconnecting = false;
        bool recording = false;
        bool recordingDown = false; ///< pedida pero cortada: el motor la está rearmando (alarma record_down)
        int destinos = 0;        ///< transmisiones extra andando
        bool limpia = false, repeticion = false;
    };
    std::vector<Salida> salidas;
};

} // namespace ba::app
