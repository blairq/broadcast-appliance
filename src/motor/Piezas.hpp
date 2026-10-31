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
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

// --- Los cuadros por segundo y la red eléctrica (F2-17) ----------------------------------
// Las luces LED y fluorescentes titilan al doble de la frecuencia de la red (100 Hz en una
// red de 50, 120 en una de 60). Un cuadro que no dura un número entero de esos ciclos los
// agarra a distinta altura: parpadeo o franjas que suben en cámara. En Argentina (50 Hz)
// el lienzo de fábrica de 30 cps lo hace; 25 o 50 no. Y una cámara a 29,97 en un lienzo
// de 30/1 hace que videorate repita un cuadro cada ~33 s: un salto en los paneos. Nada de
// esto cambia el lienzo solo (cambiar el de fábrica cambiaría plantillas y eventos que ya
// existen sin que nadie lo decida): se avisa y se dicen las cadencias de la red.

/// La frecuencia de la red de un país (ISO 3166 alfa-2, «AR», «US»): 50 o 60; 0 si no se sabe
/// o si el país tiene las dos (Japón).
int redElectricaHz(const std::string& pais);
/// La del país del sistema (la región del idioma preferido: «es-AR» → AR); 0 si no se sabe.
int redElectricaDelSistema();
/// ¿Un cuadro de `cps` dura un número entero de ciclos de luz de una red de `redHz`? (con
/// la tolerancia de las cadencias NTSC: 29,97 con 60 Hz, sí)
bool cadenciaSinParpadeo(double cps, int redHz);
/// Las cadencias del lienzo para una red (las «plantillas por región»): 50 Hz → 25, 50;
/// 60 Hz → 30, 60, 29,97, 59,94, 24. Como num/den. Vacía si no se sabe la red.
std::vector<std::pair<int, int>> cadenciasDeLaRed(int redHz);
/// Los avisos del lienzo con esa red (vacío si va bien o si no se sabe la red).
std::vector<std::string> avisosDeLaRed(const Lienzo& l, int redHz);
/// El aviso de una cámara que da `cpsCamara` en un lienzo que va a otra cadencia casi igual
/// (29,97 contra 30): cada cuánto repite o tira un cuadro videorate. "" si van iguales o si
/// la diferencia es grande (eso es otra cosa: tira o repite seguido, a propósito).
std::string avisoDeCadenciaDeCamara(double cpsCamara, const Lienzo& l);

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
/// Que cada filtro tenga su nombre de elemento (dos ids distintos pueden limpiarse igual;
/// al juntar dos listas, también). Ya lo hace leerFiltros con cada lista.
void nombresUnicos(std::vector<Filtro>& v);
/// Lo que obliga a rearmar: qué elementos, en qué orden, y las propiedades que no cambian andando.
std::string firmaDe(const std::vector<Filtro>& v, const std::vector<Filtro>& a, bool alfa);
/// Las propiedades de un filtro sobre su elemento (las que no tiene, a `avisos`).
void ponerPropiedades(GstElement* e, const Filtro& f, std::vector<std::string>* avisos);
/// Pone en vivo las propiedades que se pueden cambiar andando. true si todas.
bool ponerEnVivo(GstElement* pipeline, const std::vector<Filtro>& v, const char* prefijo);
bool hayFabrica(const std::string& elemento);
/// ¿La fábrica tiene esa propiedad? (sin crear el elemento)
bool tienePropiedad(const std::string& fabrica, const std::string& prop);
/// La memoria de placa que piden los pads de esa dirección de una fábrica que no acepta la
/// de sistema (glshader: GLMemory; d3d12…: D3D12Memory). "" si acepta la de sistema.
std::string memoriaPropia(const std::string& fabrica, GstPadDirection dir = GST_PAD_SINK);

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
/// Enlaza los elementos en cadena («src» → «sink») SIN CONSULTAR CAPS: la negociación se
/// hace igual con los eventos de caps, en el hilo de streaming. Con la consulta, enlazar una
/// capa nueva llegaba hasta el productor de su intersrc, y si esa entrada se estaba armando
/// en su trabajador, el bucle del motor esperaba (en la PC, 1348 ms con su candado).
bool enlazarSinConsultar(std::initializer_list<GstElement*> elementos);
/// ¿La entrada de la fábrica (o su salida, con `dir`) acepta esa memoria («D3D12Memory», «GLMemory»…)?
bool aceptaMemoria(const std::string& fabrica, const std::string& memoria, GstPadDirection dir = GST_PAD_SINK);
/// La memoria en la que lleva el video el motor: «GLMemory», «D3D12Memory» o "" (sistema).
std::string memoriaDelMotor();
/// En qué memoria conviene darle (o recibir de) un elemento cualquiera: la del motor si la
/// acepta (sin copias), si no la de placa que pida, si no la de sistema ("").
std::string memoriaDeTrabajo(const std::string& fabrica, GstPadDirection dir = GST_PAD_SINK);
/// Lleva el video a `memoria` ("" = sistema) venga de donde venga (sistema o la memoria
/// del motor), para gst_parse_launch. Con ancho×alto, además lo escala a ese tamaño.
/// En la del motor sale RGBA; en sistema, el formato lo negocia el que sigue.
std::string llevarA(const std::string& memoria, int ancho = 0, int alto = 0);
/// Lo que baja de `memoria` antes de llevarA si no es la del motor ni la de sistema
/// («d3d11download ! »); vacío si no hace falta.
std::string bajarDe(const std::string& memoria);
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
///
/// DERIVACIÓN (`derivar`): antes del filtro de ese nombre, un `tee` cuya otra salida
/// publica lo que llega hasta ahí en el productor `derivar->productor` (la toma
/// entera, antes del recorte del PTZ digital). La rama se agrega en
/// `derivar->ramas` (va al final de la descripción, como otro segmento); sin ese
/// filtro en la cadena, queda vacía. La rama lleva una cola que descarta lo viejo:
/// sin ella, el intersink frenaba al tee y la cadena principal dejaba de dar
/// cuadros (lo prueba test_motor_app).
struct Derivacion {
    std::string antesDe;   ///< el nombre del filtro («ptz»)
    std::string productor; ///< el intersink de la rama
    std::string ramas;     ///< (salida) lo que hay que sumar a la descripción
};
std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos, bool desdeGpu = false, Derivacion* derivar = nullptr);
/// Entre comillas para gst_parse_launch (los id de Windows traen \\ y #).
std::string entreComillas(const std::string& v);

/// El id estable de un dispositivo de video (el que guarda `camara`).
std::string idDeCamara(GstDevice* dev, GstElement* e);
/// ¿Se abre esta cámara con lo que usa el programa? En Windows, solo las de Media
/// Foundation (mfvideosrc): el monitor también da la MISMA cámara por Kernel
/// Streaming (ksvideosrc, otra ruta), y esa abierta con mfvideosrc no negocia.
/// La lista la mostraba dos veces con el mismo nombre y una no andaba.
bool camaraUsable(GstElement* e);
/// ¿Son la misma cámara? En Windows la ruta es «\\?\usb#<instancia>#{categoría}\…»:
/// la misma cámara tiene una ruta por categoría (KS y Media Foundation); se
/// compara la instancia. Así una guardada con la ruta de KS abre por MF.
bool mismaCamara(const std::string& a, const std::string& b);
/// La propiedad con el id estable de un dispositivo de audio en su elemento: `unique-id`
/// en macOS, `device` en Windows y Linux (micrófonos y salidas).
const char* propiedadDeDispositivo();
/// El id estable de un micrófono (el que guarda `microfono`) o de una salida de audio.
std::string idDeMicrofono(GstElement* e);
/// La parte de origen de la cámara `id` como está enchufada AHORA ("" si no está).
/// Sin `modo`: el de la cámara que mejor va con el lienzo.
/// `elegido` (si viene): el modo que quedó (ModoVideo::json) con `porque` y `automatico`.
std::string origenDeCamara(const std::string& id, const std::string& modo, const Lienzo& lienzo,
                           nlohmann::json* elegido = nullptr);

/// Un GstDeviceMonitor de todo el proceso: qué micrófonos, cámaras y salidas de audio
/// hay, y una generación que cambia cuando se conecta o se va uno.
/// Los proveedores de dispositivos que no avisan cuando se enchufa o se
/// desenchufa algo (el de macOS, avfdeviceprovider, en 1.28) se quedan con la
/// lista de cuando arrancaron mientras alguien los tenga prendidos (el Vigía, siempre):
/// una capturadora enchufada con la app abierta no aparecía nunca. Esto los
/// vuelve a sondear y, si cambió algo, les carga la lista nueva (avisa
/// DEVICE_REMOVED/ADDED a todos los monitores). Barato; se llama antes de listar.
void resondearSinAviso();

class Vigia {
   public:
    static Vigia& el();
    /// Arranca su hilo si no estaba (no bloquea). Ahí vive todo lo que tarda: arrancar el
    /// monitor, leer sus avisos y resondear cada 3 s. Antes lo hacía el tic de la primera
    /// entrada de dispositivo, con el candado del motor: ~250 ms cada 3 s en macOS
    /// (AVFoundation enumera las cámaras y se crea un elemento por cada una).
    void revisar();
    /// ¿Está? Lo último que supo el hilo. Si todavía no arrancó: con `esperar`, lo espera un
    /// poco (el trabajador de una entrada al armarse); sin, no (el bucle del motor: enumerar
    /// con Media Foundation tarda ~240 ms). Sin saberlo, sí.
    bool hay(bool esVideo, const std::string& id, bool esperar = true);
    /// ¿Está la salida de audio `id` (la escucha)? Como hay(), sin esperar.
    bool haySalida(const std::string& id);
    std::uint64_t generacion() const { return generacion_.load(); }

   private:
    void vigilar(); ///< el hilo
    void parar();   ///< al salir del proceso (atexit): que el hilo termine y se lo espere
    std::thread hiloVigia_;
    std::atomic<bool> salir_{false};
    void anotar(GstDevice* dev, bool esta);
    std::once_flag hilo_;
    std::mutex mu_; ///< los conjuntos y `listo_`
    std::condition_variable arranco_;
    bool listo_ = false;
    bool sinMonitor_ = false; ///< no arrancó: hay() dice que sí a todo
    std::set<std::string> audio_, video_, salidas_;
    std::atomic<std::uint64_t> generacion_{1};
};

/// Qué tiene un archivo (video, audio), con GstDiscoverer. false + error si no se puede leer.
bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error);

/// Lo que llega (caps de video) en castellano, por campo, para el diagnóstico.
nlohmann::json describirCaps(GstCaps* c);

/// Ningún elemento del pipeline ofrece su reloj (un micrófono que se desenchufa
/// se llevaría el reloj: CLOCK_LOST y todo trabado).
void sinRelojPropio(GstElement* pipeline);
/// Las fuentes de audio del pipeline (las de GstAudioBaseSrc: micrófonos, placas) salen
/// con la hora de captura en el reloj del pipeline: sin su reloj propio no la ponen (ver
/// Piezas.cpp). Antes de PLAYING. Cuántas encontró. Detrás de cada una pone `baderiva`
/// (si está el plugin baaudio): sigue el reloj de la placa y remuestrea al del estudio,
/// sin los saltos de 50 ms del reanclaje.
int horaDeCapturaEnAudio(GstElement* pipeline);
/// La deriva de cada placa abierta: ppm, reanclajes (los de la placa cruda y los que
/// llegan al programa), si está corregida y enganchada. Para `sistema_recursos`.
nlohmann::json derivaDePlacas();

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
