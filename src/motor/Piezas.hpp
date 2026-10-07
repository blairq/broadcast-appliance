// ============================================================================
// src/motor/Piezas.hpp — lo que comparten las cadenas del motor
//
// Sacado de la capa de entrada vieja (ba_gst_fuente, que vivía dentro de OBS)
// sin nada de OBS: los filtros como elementos de GStreamer, las cámaras y
// micrófonos del equipo por su id estable, el vigía de dispositivos, qué tiene
// un archivo y las caps del lienzo.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ba::motor {

/// El lienzo del estudio: a qué se normaliza cada entrada y a qué sale el programa.
struct Lienzo {
    int ancho = 1920, alto = 1080;
    int fpsN = 30, fpsD = 1;
    std::string color = "bt709";  ///< bt601, bt709, bt2100-pq, bt2100-hlg
    bool rangoCompleto = false;
    bool diezBits = false;        ///< P010 en vez de NV12 (también con HDR)
};

/// Las caps de color del lienzo («video/x-raw,format=NV12,colorimetry=…»).
std::string capsColorDelLienzo(const Lienzo& l);
/// La colorimetría del lienzo («bt709», «1:3:5:1»…).
std::string colorimetriaDelLienzo(const Lienzo& l);
/// La que se le pide a lo que entra al codificador (la del lienzo; la anota el
/// motor al crearse). Sin esto, pasar de RGBA a NV12 elige solo: en D3D12 salía
/// matriz BT.601 con transferencia sRGB en una salida HD («2:4:7:1»).
void fijarColorSalida(const Lienzo& l);

/// Un filtro de la cadena: un elemento de GStreamer con sus propiedades (como texto).
struct Filtro {
    std::string id, elemento;
    std::string nombre;  ///< el id sin caracteres raros: el elemento se llama <prefijo><nombre>
    std::string memoria; ///< la que pide (CUDAMemory, GLMemory, D3D11Memory, D3D12Memory; "" = sistema)
    bool saleEnSistema = false; ///< entrega en memoria de sistema (no hace falta bajar después)
    std::vector<std::pair<std::string, std::string>> props;
};

/// [{"id","elemento","memoria","memoria_salida","propiedades":{…}}] -> filtros.
std::vector<Filtro> leerFiltros(const nlohmann::json& lista);
/// Lo que obliga a rearmar: qué elementos, en qué orden, y las propiedades que no cambian andando.
std::string firmaDe(const std::vector<Filtro>& v, const std::vector<Filtro>& a, bool alfa);
/// Las propiedades de un filtro sobre su elemento (las que no tiene, a `avisos`).
void ponerPropiedades(GstElement* e, const Filtro& f, std::vector<std::string>* avisos);
/// Pone en vivo las propiedades que se pueden cambiar andando. true si todas.
bool ponerEnVivo(GstElement* pipeline, const std::vector<Filtro>& v, const char* prefijo);
bool hayFabrica(const std::string& elemento);

// --- El video en la GPU (docs/desarrollo/MAPA-GPU-CPU.md) ---------------------------------------
/// Con qué GPU lleva y compone el video el motor:
///   Gl     macOS (glvideomixer, gltransformation; GLMemory)
///   D3D12  Windows, cualquier marca (d3d12compositor, d3d12convert; D3D12Memory)
///   Ninguna  todo en la CPU (BA_MOTOR_CPU=1, BA_MOTOR_GPU=cpu, o si la GPU no pasa la autoprueba)
/// BA_MOTOR_GPU=gl|d3d12|cpu fuerza uno. Se decide una vez, con una autoprueba
/// (un pipeline chico que tiene que entregar cuadros): bajo Wine, o en una PC
/// sin D3D12, queda en la CPU.
enum class Gpu { Ninguna, Gl, D3D12 };
Gpu gpu();
bool gpuVideo(); ///< gpu() != Ninguna
void ponerGpuVideo(bool quiere);
/// "gl", "d3d12" o "cpu" (para el estado y el mapa).
const char* nombreGpu();
/// Las caps de una textura del motor: «video/x-raw(memory:…),format=RGBA» + `extra`.
std::string texturaGpu(const std::string& extra = "");
/// Subir a la GPU y convertir (si ya está en la GPU, pasa): «glupload ! glcolorconvert» o
/// «d3d12upload ! d3d12convert». Sin caps detrás.
std::string subirGpu();
/// Bajar a la CPU: «gldownload» o «d3d12download».
std::string bajarGpu();
/// Le da al pipeline el contexto de GPU del motor (el mismo para todos: el
/// display y el contexto GL, o el dispositivo D3D12): así un buffer de GPU
/// cruza los `inter` y se usa del otro lado sin copiarlo.
void darContextoGpu(GstElement* pipeline);
/// ¿La entrada de la fábrica acepta esa memoria («D3D12Memory», «GLMemory»…)?
bool aceptaMemoria(const std::string& fabrica, const std::string& memoria);
/// Lo que va delante de un codificador para darle NV12: en la memoria del motor
/// si el codificador la acepta (sin bajar: d3d12h264enc, qsvh264enc), si no en sistema.
std::string aCodificador(const std::string& codificador);
/// Lo que un consumidor pone para recibir video en memoria de sistema en
/// `formato` (sirve llegue GL o sistema): en GPU convierte y baja en GL; si no,
/// videoconvert. Termina con las caps «video/x-raw,format=<formato>».
std::string aSistema(const std::string& formato);
/// Lo mismo, escalado a ancho×alto (en GPU escala en GL y baja CHICO).
std::string aSistemaEscalado(const std::string& formato, int ancho, int alto);
/// La cadena de un tipo de filtros para gst_parse_launch ("" si no hay ninguno).
/// `desdeGpu`: el video llega en la memoria del motor (GLMemory o D3D12Memory, RGBA);
/// los filtros de esa memoria (o los que tienen su par «<elemento>gl» / «<elemento>d3d12»,
/// como baptz) trabajan ahí sin bajar, y solo se baja para los de CPU. Puede
/// terminar en sistema: el que sigue sube (la subida deja pasar lo que ya está en la GPU).
std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos, bool desdeGpu = false);
/// Entre comillas para gst_parse_launch (los id de Windows traen \\ y #).
std::string entreComillas(const std::string& v);

/// El id estable de un dispositivo de video (el que guarda `camara`).
std::string idDeCamara(GstDevice* dev, GstElement* e);
/// El id estable de un micrófono (el que guarda `microfono`).
std::string idDeMicrofono(GstElement* e);
/// La parte de origen de la cámara `id` como está enchufada AHORA ("" si no está).
/// Sin `modo`: el de la cámara que mejor va con el lienzo.
std::string origenDeCamara(const std::string& id, std::string modo, const Lienzo& lienzo);

/// Un GstDeviceMonitor de todo el proceso: qué micrófonos y cámaras hay, y una
/// generación que cambia cuando se conecta o se va uno.
class Vigia {
   public:
    static Vigia& el();
    /// Lee los avisos del monitor (no bloquea).
    void revisar();
    /// ¿Está? (sin saberlo todavía: sí, para no trabar el arranque).
    bool hay(bool esVideo, const std::string& id);
    std::uint64_t generacion() const { return generacion_.load(); }

   private:
    void anotar(GstDevice* dev, bool esta);
    void arrancar();
    std::mutex mu_;
    GstDeviceMonitor* mon_ = nullptr;
    std::set<std::string> audio_, video_;
    std::atomic<std::uint64_t> generacion_{1};
};

/// Qué tiene un archivo (video, audio), con GstDiscoverer. false + error si no se puede leer.
bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error);

/// Lo que llega (caps de video) en castellano, por campo, para el diagnóstico.
nlohmann::json describirCaps(GstCaps* c);

/// Ningún elemento del pipeline ofrece su reloj (un micrófono que se desenchufa
/// se llevaría el reloj: CLOCK_LOST y todo trabado).
void sinRelojPropio(GstElement* pipeline);

/// Las fuentes de prueba en vivo (videotestsrc, audiotestsrc) cuentan su tiempo
/// desde 0: en un pipeline con el base_time del estudio (de hace rato) salen a
/// las apuradas hasta alcanzar la hora, y ahogan a quien las consume. Arrancan
/// en `ahora` (la hora de ejecución del estudio).
void fuentesDePruebaEnHora(GstElement* pipeline, GstClockTime ahora);

/// gst_bin_recalculate_latency EN OTRO HILO. Por dentro hace la misma consulta
/// que recorre todo el pipeline (ver LatenciaAparte) y se llamaba con los
/// candados del motor tomados. Los pedidos se juntan: si llega uno con otro en
/// curso, se recalcula una vez más al terminar (con lo último).
/// `antes` corre en ese hilo justo antes de recalcular: poner la latencia en
/// el mezclador (min-upstream-latency) toma su candado, que el recálculo tiene
/// mientras dura; hecho desde el bucle del motor, lo trababa igual. Un pedido
/// pendiente se queda con el último `antes`.
void recalcularLatenciaAparte(GstElement* pipeline, std::function<void()> antes = {});

/// Suelta los hilos de las fuentes que ya terminaron (pasó el EOS y su tarea
/// quedó en pausa): una fuente de un cuadro (el rótulo, un color, la placa)
/// tenía su hilo dormido para siempre. Devuelve cuántas soltó. No es para lo
/// que puede volver a andar sin rearmarse (un archivo que se rebobina).
int soltarTareasTerminadas(GstElement* bin);

/// Una referencia de GStreamer que se suelta sola (para llevar elementos a otro hilo).
std::shared_ptr<GstObject> refCompartida(gpointer o);

/// La latencia de un pipeline, preguntada EN OTRO HILO. El bucle del motor la
/// pedía con su candado tomado y la consulta recorre todo el pipeline: una
/// fuente que se está conectando, o un elemento con su candado ocupado, la tenía
/// segundos sin responder, y mientras tanto todo lo que pasa por el motor
/// esperaba (la interfaz incluida: la vista previa salteaba cuadros).
class LatenciaAparte {
   public:
    /// Lanza una consulta si no hay otra en curso y devuelve la última respuesta
    /// (ns del mínimo; -1 si todavía no respondió ninguna).
    std::int64_t consultar(GstElement* pipeline);
    /// Lo que respondía la cadena anterior ya no vale (se rearmó).
    void olvidar() { e_ = std::make_shared<Estado>(); }

   private:
    /// Compartido con el hilo: si el dueño se va con la consulta en curso, el
    /// hilo termina sobre esto y no sobre algo que ya no existe.
    struct Estado {
        std::atomic<bool> enCurso{false};
        std::atomic<std::int64_t> ns{-1};
    };
    std::shared_ptr<Estado> e_ = std::make_shared<Estado>();
};

/// Un valor JSON como texto para gst_util_set_object_arg.
std::string comoTexto(const nlohmann::json& v);

} // namespace ba::motor
