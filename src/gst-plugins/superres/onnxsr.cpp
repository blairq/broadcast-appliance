// ============================================================================
// src/gst-plugins/superres/onnxsr.cpp — superresolución x2 con una red ONNX
//
// Proposito:
//   Elemento de GStreamer `baonnxsr`: duplica la resolución con una red
//   neuronal en formato ONNX, corrida por ONNX Runtime, en la placa que haya:
//
//     proveedor   placa                                   sistema
//     tensorrt    NVIDIA (compila la red PARA ESA placa)  Windows, Linux
//     cuda        NVIDIA                                  Windows, Linux
//     dml         DirectML: AMD (también las integradas    Windows
//                 Ryzen/Vega/RDNA), NVIDIA, Intel
//     migraphx    AMD (compila la red para esa placa)     Linux
//     rocm        AMD                                     Linux
//     openvino    Intel (GPU integrada/NPU)               Windows, Linux
//     coreml      GPU / Neural Engine de la Mac           macOS
//     cpu         el procesador (siempre)
//   `auto` prueba en ese orden lo que haya en esta máquina y se queda con el
//   primero que abre la red. La red COMPILADA para la placa (TensorRT,
//   MIGraphX, OpenVINO) se guarda en `cache` (de fábrica, <modelo>.cache/):
//   la primera vez tarda (minutos en TensorRT), después abre al toque.
//
//   EL ONNX RUNTIME se elige al correr (no al compilar): el que trae el
//   programa (CPU + DirectML en Windows, CoreML en la Mac) o, si la variable
//   BA_ORT_GPU apunta a otra biblioteca (la que el programa baja a
//   Datos/Herramientas/onnxruntime-gpu para NVIDIA o AMD, con CUDA/TensorRT o
//   ROCm/MIGraphX), esa. Así el mismo programa sirve para cualquier placa y
//   las bibliotecas grandes se bajan solo donde hacen falta.
//
//   MODELOS: dos formas.
//     - NV12 (lo que arma el laboratorio, `modelo-nv12.onnx`): entra y sale la
//       imagen tal cual la tiene GStreamer —entradas `y` [1,1,alto,ancho] y
//       `uv` [1,1,alto/2,ancho] uint8, salidas al doble—; el color, la mitad
//       de una red x4 y el agrandado del color van ADENTRO de la red, en la
//       placa. Por la memoria viaja 1,5 byte por píxel (no 12 de float) y la
//       CPU no toca un píxel: los tensores apuntan a los planos del cuadro.
//     - float (cualquier ONNX de superresolución): float 0..1, NCHW [1,C,alto,
//       ancho], C = 1 (luma) o 3 (RGB). Una red x4 recibe la imagen a la mitad
//       (sale x2 de la original).
//   Alto y ancho libres se fijan al tamaño de la cámara al abrir la sesión (las
//   aceleradoras compilan para tamaños fijos).
//
// NVIDIA SIN BAJAR A LA CPU (si GStreamer trae gst-cuda): la entrada puede ser
//   memory:CUDAMemory (lo que entrega nvh264dec/nvh265dec): el cuadro se copia
//   de placa a placa a los tensores de la red (cuMemcpy2D, sin pasar por la
//   CPU), la red corre con IoBinding en CUDA/TensorRT y la salida baja a la
//   CPU una vez (libobs la necesita ahí igual). Comparte el contexto CUDA del
//   pipeline (consultas y mensajes de GstContext, como los elementos de nvcodec).
//
// GStreamer: NV12 de entrada y salida (al doble), LATENCY con lo que tarda
//   la red, QoS, metadatos que no dependen del tamaño. La red corre en un hilo
//   propio con un cuadro en vuelo (el hilo de streaming no espera a la red);
//   EOS vacía en orden, un flush o un formato nuevo descartan lo pendiente.
//   Mientras la red se prepara (o si falla) la imagen sale agrandada simple.
// ============================================================================

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "comun.hpp"

#if defined(BA_HAVE_ORT)
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#if defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
#if defined(BA_HAVE_GST_CUDA)
#include <gst/cuda/gstcuda.h>
#endif
#if defined(BA_HAVE_NCNN)
#include <ncnn/gpu.h>
#include <ncnn/net.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_ONNXSR (ba_onnxsr_get_type())
G_DECLARE_FINAL_TYPE(BaOnnxSr, ba_onnxsr, BA, ONNXSR, GstBaseTransform)
G_END_DECLS

// --- El ONNX Runtime, elegido al correr ---------------------------------------------
#if defined(BA_HAVE_ORT)
namespace ba::ort {

struct Runtime {
    const OrtApi* api = nullptr;
    std::string biblioteca; ///< de dónde salió ("" = la del programa)
    std::string error;
    std::vector<std::string> proveedores; ///< los que trae (GetAvailableProviders)
};

/// Carga una biblioteca de ONNX Runtime que no es la del programa (BA_ORT_GPU).
static const OrtApiBase* baseDe(const std::string& ruta, std::string& error) {
    using Fn = const OrtApiBase*(ORT_API_CALL*)();
#if defined(_WIN32)
    const std::wstring w = std::filesystem::path(ruta).wstring();
    // Sus dependencias (proveedores, CUDA, cuDNN, TensorRT…) en su misma carpeta.
    HMODULE h = LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h == nullptr) {
        error = "no se pudo cargar " + ruta + " (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    auto f = reinterpret_cast<Fn>(GetProcAddress(h, "OrtGetApiBase"));
#else
    // RTLD_DEEPBIND (Linux): sus símbolos internos no se mezclan con los del
    // ONNX Runtime del programa (que ya está cargado para rostros y voz).
#if defined(__linux__)
    // Sus dependencias (CUDA, cuDNN, TensorRT, ROCm, OpenVINO…) están en su
    // carpeta, que no está en la ruta de búsqueda: se cargan antes, globales,
    // y así el núcleo y sus proveedores las encuentran por nombre (lo mismo que
    // hace onnxruntime.preload_dlls en Python). Las que dependen de otras
    // fallan la primera vez: se reintenta hasta que no avanza.
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> pendientes;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::path(ruta).parent_path(), ec)) {
            const std::string n = e.path().filename().string();
            if (n.find(".so") == std::string::npos || n.rfind("libonnxruntime", 0) == 0) continue;
            pendientes.push_back(e.path());
        }
        for (bool avanzo = true; avanzo && !pendientes.empty();) {
            avanzo = false;
            for (auto it = pendientes.begin(); it != pendientes.end();) {
                if (dlopen(it->c_str(), RTLD_NOW | RTLD_GLOBAL) != nullptr) {
                    it = pendientes.erase(it);
                    avanzo = true;
                } else {
                    ++it;
                }
            }
        }
    }
    void* h = dlopen(ruta.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#else
    void* h = dlopen(ruta.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (h == nullptr) {
        const char* e = dlerror();
        error = "no se pudo cargar " + ruta + ": " + (e != nullptr ? e : "?");
        return nullptr;
    }
    auto f = reinterpret_cast<Fn>(dlsym(h, "OrtGetApiBase"));
#endif
    if (f == nullptr) {
        error = ruta + " no es un ONNX Runtime (no tiene OrtGetApiBase)";
        return nullptr;
    }
    return f();
}

static Runtime cargar() {
    Runtime r;
    const OrtApiBase* base = nullptr;
    if (const char* gpu = std::getenv("BA_ORT_GPU"); gpu != nullptr && *gpu != '\0') {
        std::error_code ec;
        if (std::filesystem::exists(gpu, ec)) {
            base = baseDe(gpu, r.error);
            if (base != nullptr) r.biblioteca = gpu;
        } else {
            r.error = std::string("BA_ORT_GPU apunta a ") + gpu + ", que no existe";
        }
    }
    if (base == nullptr) base = ::OrtGetApiBase(); // la del programa (enlazada)
    r.api = base->GetApi(ORT_API_VERSION);
    if (r.api == nullptr && !r.biblioteca.empty()) {
        // Una versión vieja que no conoce esta API: la del programa.
        r.error = r.biblioteca + " es de una versión de ONNX Runtime anterior a la del programa";
        r.biblioteca.clear();
        r.api = ::OrtGetApiBase()->GetApi(ORT_API_VERSION);
    }
    if (r.api == nullptr) {
        r.error = "ONNX Runtime no ofrece la API " + std::to_string(ORT_API_VERSION);
        return r;
    }
    Ort::InitApi(r.api);
    char** lista = nullptr;
    int n = 0;
    if (OrtStatus* st = r.api->GetAvailableProviders(&lista, &n); st == nullptr) {
        for (int i = 0; i < n; ++i)
            r.proveedores.emplace_back(lista[i]);
        r.api->ReleaseAvailableProviders(lista, n);
    } else {
        r.api->ReleaseStatus(st);
    }
    return r;
}

static const Runtime& runtime() {
    static const Runtime r = cargar();
    return r;
}

/// ncnn con Vulkan: ¿hay alguna GPU con Vulkan? (el cargador va en el bundle; el driver es de la placa).
static bool hayVulkan() {
#if defined(BA_HAVE_NCNN)
    static const bool hay = [] {
        ncnn::create_gpu_instance();
        return ncnn::get_gpu_count() > 0;
    }();
    return hay;
#else
    return false;
#endif
}

static bool trae(const std::string& proveedor) {
    if (proveedor == "vulkan") return hayVulkan();
    static const std::pair<const char*, const char*> kNombres[] = {
        {"tensorrt", "TensorrtExecutionProvider"}, {"cuda", "CUDAExecutionProvider"},
        {"dml", "DmlExecutionProvider"},           {"migraphx", "MIGraphXExecutionProvider"},
        {"rocm", "ROCMExecutionProvider"},         {"openvino", "OpenVINOExecutionProvider"},
        {"coreml", "CoreMLExecutionProvider"},     {"cpu", "CPUExecutionProvider"}};
    for (const auto& [corto, largo] : kNombres)
        if (proveedor == corto)
            return std::find(runtime().proveedores.begin(), runtime().proveedores.end(), largo) !=
                   runtime().proveedores.end();
    return false;
}

/// Lo que se prueba con `auto`, en orden (lo que no trae este ONNX Runtime se saltea).
static std::vector<std::string> ordenAuto() {
#if defined(__APPLE__)
    return {"coreml", "cpu"};
#elif defined(_WIN32)
    return {"tensorrt", "cuda", "dml", "vulkan", "openvino", "cpu"};
#else
    return {"tensorrt", "cuda", "migraphx", "rocm", "vulkan", "openvino", "cpu"};
#endif
}

static void chequear(OrtStatus* st) {
    if (st == nullptr) return;
    const std::string m = runtime().api->GetErrorMessage(st);
    runtime().api->ReleaseStatus(st);
    throw std::runtime_error(m);
}

/// Opciones por clave/valor (CUDA y TensorRT V2).
template <typename Opc, typename Crear, typename Poner, typename Soltar, typename Agregar>
static void conClaves(OrtSessionOptions* so, Crear crear, Poner poner, Soltar soltar, Agregar agregar,
                      const std::vector<std::pair<std::string, std::string>>& kv) {
    Opc* o = nullptr;
    chequear(crear(&o));
    std::vector<const char*> k, v;
    for (const auto& [a, b] : kv) {
        k.push_back(a.c_str());
        v.push_back(b.c_str());
    }
    OrtStatus* st = poner(o, k.data(), v.data(), k.size());
    if (st == nullptr) st = agregar(so, o);
    soltar(o);
    chequear(st);
}

/// Agrega el proveedor `p` a las opciones de la sesión (excepción si no se puede).
static void agregarProveedor(Ort::SessionOptions& op, const std::string& p, int dispositivo, bool fp16,
                             const std::string& cache) {
    const OrtApi* api = runtime().api;
    OrtSessionOptions* so = op;
    const std::string dev = std::to_string(dispositivo);
    std::error_code ec;
    if (!cache.empty()) std::filesystem::create_directories(cache, ec);
    if (p == "tensorrt") {
        // La red compilada para ESTA placa queda en la caché (motor .engine y
        // tiempos): la primera vez tarda, después no.
        conClaves<OrtTensorRTProviderOptionsV2>(
            so, api->CreateTensorRTProviderOptions, api->UpdateTensorRTProviderOptions,
            api->ReleaseTensorRTProviderOptions, api->SessionOptionsAppendExecutionProvider_TensorRT_V2,
            {{"device_id", dev},
             {"trt_fp16_enable", fp16 ? "1" : "0"},
             {"trt_engine_cache_enable", "1"},
             {"trt_engine_cache_path", cache},
             {"trt_timing_cache_enable", "1"},
             {"trt_timing_cache_path", cache},
             {"trt_builder_optimization_level", "3"}});
        // Lo que TensorRT no sabe hacer, en CUDA (no en la CPU).
        conClaves<OrtCUDAProviderOptionsV2>(so, api->CreateCUDAProviderOptions, api->UpdateCUDAProviderOptions,
                                            api->ReleaseCUDAProviderOptions,
                                            api->SessionOptionsAppendExecutionProvider_CUDA_V2,
                                            {{"device_id", dev}, {"cudnn_conv_algo_search", "HEURISTIC"}});
    } else if (p == "cuda") {
        conClaves<OrtCUDAProviderOptionsV2>(so, api->CreateCUDAProviderOptions, api->UpdateCUDAProviderOptions,
                                            api->ReleaseCUDAProviderOptions,
                                            api->SessionOptionsAppendExecutionProvider_CUDA_V2,
                                            {{"device_id", dev},
                                             {"cudnn_conv_algo_search", "HEURISTIC"},
                                             {"do_copy_in_default_stream", "1"}});
    } else if (p == "dml") {
        // DirectML pide ejecución secuencial y sin patrones de memoria.
        op.DisableMemPattern();
        op.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        op.AppendExecutionProvider("DML", {{"device_id", dev}});
    } else if (p == "migraphx") {
        // MIGraphX compila para la placa: se guarda y se reusa.
        const std::string archivo = (std::filesystem::path(cache) / "modelo.mxr").string();
        const bool hay = std::filesystem::exists(archivo, ec);
        OrtMIGraphXProviderOptions o{};
        o.device_id = dispositivo;
        o.migraphx_fp16_enable = fp16 ? 1 : 0;
        o.migraphx_save_compiled_model = hay ? 0 : 1;
        o.migraphx_save_model_path = archivo.c_str();
        o.migraphx_load_compiled_model = hay ? 1 : 0;
        o.migraphx_load_model_path = archivo.c_str();
        chequear(api->SessionOptionsAppendExecutionProvider_MIGraphX(so, &o));
    } else if (p == "rocm") {
        OrtROCMProviderOptions o;
        o.device_id = dispositivo;
        chequear(api->SessionOptionsAppendExecutionProvider_ROCM(so, &o));
    } else if (p == "openvino") {
        const char* k[] = {"device_type", "cache_dir", "precision"};
        const char* v[] = {"GPU", cache.c_str(), fp16 ? "FP16" : "FP32"};
        chequear(api->SessionOptionsAppendExecutionProvider_OpenVINO_V2(so, k, v, 3));
    } else if (p == "coreml") {
#if defined(__APPLE__)
        // MLProgram + todas las unidades (CPU, GPU, Neural Engine).
        chequear(OrtSessionOptionsAppendExecutionProvider_CoreML(so, COREML_FLAG_CREATE_MLPROGRAM));
#else
        throw std::runtime_error("CoreML es de la Mac");
#endif
    } else if (p != "cpu") {
        throw std::runtime_error("proveedor desconocido: " + p);
    }
}

} // namespace ba::ort

struct Red {
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> sesion;
    std::vector<std::string> entradas, salidas;
    bool nv12 = false;  ///< modelo del laboratorio: uint8 y/uv de entrada y salida
    int canales = 1;    ///< float: 1 (luma) o 3 (RGB)
    int factor = 2;     ///< lo que agranda el modelo (2 o 4)
    bool mitad = false; ///< float x4: recibe la imagen a la mitad
    int iw = 0, ih = 0; ///< lo que recibe (float)
    std::string proveedor;
    std::vector<float> in; ///< el tensor de entrada (float), reusado
#if defined(BA_HAVE_NCNN)
    std::unique_ptr<ncnn::Net> net; ///< proveedor «vulkan»: la red en ncnn (la sesión de ORT queda sin usar)
    std::string blobIn, blobOut;
    std::vector<float> out;          ///< la salida de ncnn, contigua (CHW)
#endif
    // Tensores en la placa (entrada CUDAMemory con CUDA/TensorRT).
    std::unique_ptr<Ort::MemoryInfo> memCuda;
    std::unique_ptr<Ort::Allocator> alCuda;
    void* yD = nullptr;
    void* uvD = nullptr;
    void* y2D = nullptr;
    void* uv2D = nullptr;
    int bw = 0, bh = 0;
    ~Red() {
        if (alCuda)
            for (void* p : {yD, uvD, y2D, uv2D})
                if (p != nullptr) alCuda->Free(p);
    }
    bool enPlaca() const { return nv12 && (proveedor == "cuda" || proveedor == "tensorrt"); }
};
#endif

/// Un hilo que corre trabajos en orden (la red).
class Obrero {
   public:
    Obrero() : hilo_([this] { correr(); }) {}
    ~Obrero() {
        {
            std::lock_guard<std::mutex> l(mu_);
            fin_ = true;
        }
        cv_.notify_all();
        hilo_.join();
    }
    std::future<bool> enviar(std::function<bool()> f) {
        auto t = std::make_shared<std::packaged_task<bool()>>(std::move(f));
        std::future<bool> r = t->get_future();
        {
            std::lock_guard<std::mutex> l(mu_);
            cola_.emplace_back([t] { (*t)(); });
        }
        cv_.notify_one();
        return r;
    }

   private:
    void correr() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return fin_ || !cola_.empty(); });
                if (cola_.empty()) return;
                f = std::move(cola_.front());
                cola_.pop_front();
            }
            f();
        }
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> cola_;
    bool fin_ = false;
    std::thread hilo_;
};

struct Trabajo {
    GstBuffer* entrada = nullptr;
    GstBuffer* salida = nullptr;
    std::future<bool> listo;
};

struct _BaOnnxSr {
    GstBaseTransform parent;
    GstVideoInfo in, out;
    gchar* modelo;
    gchar* variantes;     ///< "WxH=ruta;…": la red fija para cada tamaño de cámara (si no, `modelo`)
    std::string* rutaRed; ///< la que se abre (la variante del tamaño o `modelo`)
    gchar* proveedor;
    gchar* cache;
    gint dispositivo;
    gboolean fp16;
    gboolean procesa;
    std::mutex* errorMu;
    gchar* error;
#if defined(BA_HAVE_ORT)
    Red* red;
    GThread* preparando;      ///< la red se arma en otro hilo (CoreML/TensorRT compilan)
    std::atomic<Red*>* lista; ///< lo que dejó el hilo
    int pw, ph;               ///< el tamaño para el que se prepara
#endif
    Obrero* obrero;
    std::deque<Trabajo*>* pendientes;
    gboolean entradaCuda; ///< la entrada es memory:CUDAMemory
    // Modos (ver comun.hpp): lo pide la propiedad o el canal de control.
    std::atomic<int>* modo;
    std::atomic<int>* transicionMs;
    ba::sr::Mezcla mezcla; ///< solo el hilo de streaming
    guint64 cuadros;
#if defined(BA_HAVE_GST_CUDA)
    GstCudaContext* cuda;
#endif
    gboolean activo;
    ba::sr::Demora* demora;
};

G_DEFINE_TYPE(BaOnnxSr, ba_onnxsr, GST_TYPE_BASE_TRANSFORM)

GST_DEBUG_CATEGORY_STATIC(ba_onnxsr_debug);
#define GST_CAT_DEFAULT ba_onnxsr_debug

enum {
    PROP_0,
    PROP_ACTIVO,
    PROP_MODELO,
    PROP_VARIANTES,
    PROP_PROVEEDOR,
    PROP_DISPOSITIVO,
    PROP_FP16,
    PROP_CACHE,
    PROP_MS,
    PROP_ERROR,
    PROP_PROVEEDOR_USADO,
    PROP_PROVEEDORES,
    PROP_BIBLIOTECA,
    PROP_MODO,
    PROP_TRANSICION,
    PROP_MEZCLA
};

#if defined(BA_HAVE_GST_CUDA)
#define CAPS_ENTRADA GST_VIDEO_CAPS_MAKE("NV12") ";" GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, "NV12")
#else
#define CAPS_ENTRADA GST_VIDEO_CAPS_MAKE("NV12")
#endif
static GstStaticPadTemplate sinkTpl =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(CAPS_ENTRADA));
static GstStaticPadTemplate srcTpl =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));

static bool hayModelo(BaOnnxSr* s) { return s->modelo != nullptr && *s->modelo != '\0'; }

static void ponerError(BaOnnxSr* s, const std::string& e) {
    std::lock_guard<std::mutex> l(*s->errorMu);
    g_free(s->error);
    s->error = e.empty() ? nullptr : g_strdup(e.c_str());
    if (!e.empty()) GST_WARNING_OBJECT(s, "%s", e.c_str());
}

static bool hayError(BaOnnxSr* s) {
    std::lock_guard<std::mutex> l(*s->errorMu);
    return s->error != nullptr;
}

static void descartar(BaOnnxSr* s);

static void soltarRed(BaOnnxSr* s) {
    descartar(s); // lo que está en vuelo usa la red
#if defined(BA_HAVE_ORT)
    if (s->preparando != nullptr) {
        g_thread_join(s->preparando);
        s->preparando = nullptr;
    }
    delete s->lista->exchange(nullptr);
    delete s->red;
    s->red = nullptr;
#endif
}

#if defined(BA_HAVE_ORT)
/// Una sesión con el proveedor `p` y los tamaños fijos (excepción si no abre).
static std::unique_ptr<Ort::Session> sesionCon(BaOnnxSr* s, Ort::Env& env, const std::string& p, int w, int h) {
    Ort::SessionOptions op;
    op.SetIntraOpNumThreads(4);
    op.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Alto y ancho libres del modelo, fijos al tamaño de la cámara: así la
    // aceleración compila para ese tamaño y no cae al procesador.
    for (const auto& [nombre, valor] :
         {std::pair{"alto", h}, {"ancho", w}, {"height", h}, {"width", w}, {"alto_c", h / 2}})
        Ort::ThrowOnError(Ort::GetApi().AddFreeDimensionOverrideByName(op, nombre, valor));
    const std::string& modelo = *s->rutaRed;
    std::string cache = s->cache != nullptr && *s->cache != '\0' ? s->cache : modelo + ".cache";
    cache = (std::filesystem::path(cache) / (p + "-" + std::to_string(s->dispositivo) + "-" + std::to_string(w) +
                                            "x" + std::to_string(h)))
                .string();
    ba::ort::agregarProveedor(op, p, s->dispositivo, s->fp16, cache);
#if defined(_WIN32)
    const std::wstring ruta = std::filesystem::path(modelo).wstring();
    return std::make_unique<Ort::Session>(env, ruta.c_str(), op);
#else
    return std::make_unique<Ort::Session>(env, modelo.c_str(), op);
#endif
}

#if defined(BA_HAVE_NCNN)
/// pnnx (convierte ONNX a ncnn): BA_PNNX, o al lado del programa.
static std::string rutaPnnx() {
    if (const char* e = std::getenv("BA_PNNX"); e != nullptr && *e != '\0') return e;
#if defined(_WIN32)
    wchar_t b[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, b, MAX_PATH);
    const auto p = std::filesystem::path(b).parent_path() / "pnnx.exe";
#else
    const auto p = std::filesystem::path("/proc/self/exe").parent_path() / "pnnx";
#endif
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

/// «Compila» la red para Vulkan: ONNX -> ncnn (.param/.bin) con pnnx, para la
/// forma de entrada 1×C×alto×ancho. Queda en la caché (la primera vez tarda).
static bool compilarNcnn(const std::string& onnx, int C, int w, int h, const std::string& param, const std::string& bin,
                         std::string& error) {
    const std::string pnnx = rutaPnnx();
    if (pnnx.empty()) {
        error = "falta pnnx (para pasar la red de ONNX a ncnn)";
        return false;
    }
    std::error_code ec;
    const auto dir = std::filesystem::path(param).parent_path();
    std::filesystem::create_directories(dir, ec);
    const std::string tmp = (dir / "pnnx-tmp").string();
    const std::string forma = "inputshape=[1," + std::to_string(C) + "," + std::to_string(h) + "," + std::to_string(w) + "]";
    std::vector<std::string> args{pnnx, onnx, forma, "ncnnparam=" + param, "ncnnbin=" + bin, "pnnxparam=" + tmp + ".pnnx.param",
                                  "pnnxbin=" + tmp + ".pnnx.bin", "pnnxpy=" + tmp + "_pnnx.py", "pnnxonnx=" + tmp + ".pnnx.onnx",
                                  "ncnnpy=" + tmp + "_ncnn.py", "device=cpu"};
    std::vector<gchar*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    gchar* sale = nullptr;
    gchar* err = nullptr;
    gint estado = 0;
    GError* ge = nullptr;
    const gboolean ok = g_spawn_sync(dir.string().c_str(), argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &sale, &err,
                                     &estado, &ge);
    const std::string texto = std::string(sale != nullptr ? sale : "") + (err != nullptr ? err : "");
    g_free(sale);
    g_free(err);
    for (const char* x : {".pnnx.param", ".pnnx.bin", "_pnnx.py", ".pnnx.onnx", "_ncnn.py"})
        std::filesystem::remove(tmp + x, ec);
    if (!ok) {
        error = std::string("no pude correr pnnx: ") + (ge != nullptr ? ge->message : "?");
        if (ge != nullptr) g_error_free(ge);
        return false;
    }
    if (!std::filesystem::exists(param, ec) || !std::filesystem::exists(bin, ec)) {
        error = "pnnx no pudo convertir la red: " + texto.substr(texto.size() > 400 ? texto.size() - 400 : 0);
        return false;
    }
    return true;
}

/// La red en ncnn con Vulkan para una cámara de w×h (r.canales ya sabido). La red se
/// convierte una vez, con una forma chica: estas redes (convoluciones, PixelShuffle,
/// interpolación por factor) andan en cualquier tamaño. Lo que agranda sale de una
/// pasada de 64×64 en la GPU (antes se corría la red entera en la CPU para saberlo).
static bool abrirNcnn(BaOnnxSr* s, Red& r, int w, int h, std::string& error) {
    const std::string& modelo = *s->rutaRed;
    std::string param, bin;
    if (modelo.size() > 6 && modelo.compare(modelo.size() - 6, 6, ".param") == 0) {
        // Ya convertida (el laboratorio): modelo.param + modelo.bin.
        param = modelo;
        bin = modelo.substr(0, modelo.size() - 6) + ".bin";
    } else {
        const std::string cache = s->cache != nullptr && *s->cache != '\0' ? s->cache : modelo + ".cache";
        const auto base = std::filesystem::path(cache) / ("vulkan-" + std::to_string(r.canales));
        param = base.string() + ".ncnn.param";
        bin = base.string() + ".ncnn.bin";
        std::error_code ec;
        if (!std::filesystem::exists(param, ec) || !std::filesystem::exists(bin, ec)) {
            GST_INFO_OBJECT(s, "pasando %s a ncnn", modelo.c_str());
            if (!compilarNcnn(modelo, r.canales, 64, 64, param, bin, error)) return false;
        }
    }
    const int gpus = ncnn::get_gpu_count();
    if (gpus <= 0) {
        error = "no hay GPU con Vulkan";
        return false;
    }
    auto net = std::make_unique<ncnn::Net>();
    net->opt.use_vulkan_compute = true;
    net->opt.use_fp16_packed = net->opt.use_fp16_storage = s->fp16 != FALSE;
    net->opt.use_fp16_arithmetic = false; // como realesrgan-ncnn-vulkan: guarda en FP16, cuenta en FP32
    net->set_vulkan_device(std::clamp(static_cast<int>(s->dispositivo), 0, gpus - 1));
    if (net->load_param(param.c_str()) != 0 || net->load_model(bin.c_str()) != 0) {
        error = "ncnn no pudo cargar " + param;
        return false;
    }
    if (net->input_names().empty() || net->output_names().empty()) {
        error = "la red de ncnn no dice su entrada y su salida";
        return false;
    }
    r.blobIn = net->input_names()[0];
    r.blobOut = net->output_names()[0];
    const auto pasada = [&](int pw, int ph, ncnn::Mat& out) {
        ncnn::Mat in(pw, ph, r.canales);
        in.fill(0.5f);
        ncnn::Extractor ex = net->create_extractor();
        ex.input(r.blobIn.c_str(), in);
        return ex.extract(r.blobOut.c_str(), out) == 0;
    };
    ncnn::Mat out;
    if (!pasada(64, 64, out) || out.c != r.canales || out.w % 64 != 0) {
        error = "la red de ncnn no devolvió una imagen agrandada";
        return false;
    }
    r.factor = out.w / 64;
    if (r.factor != 2 && r.factor != 4) {
        error = "la red agranda x" + std::to_string(r.factor) + ": se usa x2 o x4";
        return false;
    }
    // Una x4 recibe la cámara a la mitad (sale x2).
    if (r.factor == 4 && !r.mitad && w % 4 == 0 && h % 4 == 0) r.mitad = true;
    r.iw = r.mitad ? w / 2 : w;
    r.ih = r.mitad ? h / 2 : h;
    // Calentar con el tamaño de verdad (Vulkan arma sus pipelines la primera vez).
    const gint64 t0 = g_get_monotonic_time();
    if (!pasada(r.iw, r.ih, out) || out.w != r.iw * r.factor || out.h != r.ih * r.factor) {
        error = "la red de ncnn no anda en " + std::to_string(r.iw) + "x" + std::to_string(r.ih);
        return false;
    }
    GST_INFO_OBJECT(s, "ncnn: x%d%s, %dx%d, primera pasada %.0f ms", r.factor, r.mitad ? " a la mitad" : "", r.iw, r.ih,
                    static_cast<double>(g_get_monotonic_time() - t0) / 1000.0);
    r.net = std::move(net);
    return true;
}
#endif

/// Abre la red para este tamaño de cámara, con el mejor proveedor que ande.
static Red* abrirCon(BaOnnxSr* s, int w, int h, std::string& motivos) {
    const auto& rt = ba::ort::runtime();
    if (rt.api == nullptr) throw std::runtime_error(rt.error);
    const std::string pedido = s->proveedor != nullptr && *s->proveedor ? s->proveedor : "auto";
    std::vector<std::string> probar = pedido == "auto" ? ba::ort::ordenAuto() : std::vector<std::string>{pedido};
    if (pedido != "auto" && pedido != "cpu") probar.push_back("cpu"); // si el pedido no anda, igual hay imagen
    auto r = std::make_unique<Red>();
    r->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "baonnxsr");
    // Primero, qué modelo es (con la CPU, que siempre abre): NV12 o float, y su forma.
    // «vulkan» no es de ONNX Runtime: si viene antes del que abre, la red se pasa a
    // ncnn al final (con la forma ya sabida).
    bool quiereVulkan = false;
    for (int intento = 0; intento < 2; ++intento) {
        for (const auto& p : probar) {
            if (p == "vulkan") {
                if (ba::ort::trae(p))
                    quiereVulkan = true;
                else
                    motivos += "vulkan: no hay GPU con Vulkan (o el programa no trae ncnn); ";
                continue;
            }
            if (!ba::ort::trae(p)) {
                motivos += p + ": este ONNX Runtime no lo trae; ";
                continue;
            }
            try {
                r->sesion = sesionCon(s, *r->env, p, r->mitad ? w / 2 : w, r->mitad ? h / 2 : h);
                r->proveedor = p;
                break;
            } catch (const std::exception& e) {
                motivos += p + ": " + e.what() + "; ";
            }
        }
        if (!r->sesion) throw std::runtime_error("ningún proveedor abrió la red (" + motivos + ")");
        Ort::AllocatorWithDefaultOptions al;
        r->entradas.clear();
        r->salidas.clear();
        for (size_t i = 0; i < r->sesion->GetInputCount(); ++i)
            r->entradas.emplace_back(r->sesion->GetInputNameAllocated(i, al).get());
        for (size_t i = 0; i < r->sesion->GetOutputCount(); ++i)
            r->salidas.emplace_back(r->sesion->GetOutputNameAllocated(i, al).get());
        const Ort::TypeInfo tipo = r->sesion->GetInputTypeInfo(0); // vive mientras se lee su forma
        const auto info = tipo.GetTensorTypeAndShapeInfo();
        r->nv12 = r->entradas.size() == 2 && info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
        if (r->nv12) {
            if (quiereVulkan) motivos += "vulkan: las redes NV12 del laboratorio no van en ncnn; ";
            return r.release(); // el laboratorio ya dejó la red para x2 de lo que entra
        }
        const auto forma = info.GetShape();
        r->canales = forma.size() == 4 && forma[1] > 0 ? static_cast<int>(forma[1]) : 1;
        // Una variante fija para una x4 viene a la mitad de la cámara: ya se sabe.
        if (!r->mitad && forma.size() == 4 && forma[3] > 0 && forma[2] > 0 && forma[3] * 2 == w && forma[2] * 2 == h)
            r->mitad = true;
        if (r->canales != 1 && r->canales != 3)
            throw std::runtime_error("el modelo tiene que tener 1 (luma) o 3 (RGB) canales de entrada");
#if defined(BA_HAVE_NCNN)
        // Vulkan: la red en ncnn, sin la pasada de prueba en la CPU (lenta en 1080p).
        if (quiereVulkan) {
            std::string e;
            if (abrirNcnn(s, *r, w, h, e)) {
                r->proveedor = "vulkan";
                r->sesion.reset();
                return r.release();
            }
            motivos += "vulkan: " + e + "; ";
            quiereVulkan = false; // sigue con ONNX Runtime
        }
#endif
        // Lo que agranda: una pasada de prueba (y de paso calienta la red).
        r->iw = r->mitad ? w / 2 : w;
        r->ih = r->mitad ? h / 2 : h;
        std::vector<float> prueba(static_cast<size_t>(r->canales) * r->iw * r->ih, 0.5f);
        const std::array<int64_t, 4> dims{1, r->canales, r->ih, r->iw};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value t = Ort::Value::CreateTensor<float>(mem, prueba.data(), prueba.size(), dims.data(), dims.size());
        const char* ins[] = {r->entradas[0].c_str()};
        const char* outs[] = {r->salidas[0].c_str()};
        auto res = r->sesion->Run(Ort::RunOptions{nullptr}, ins, &t, 1, outs, 1);
        const auto fo = res[0].GetTensorTypeAndShapeInfo().GetShape();
        r->factor = fo.size() == 4 ? static_cast<int>(fo[3] / r->iw) : 0;
        if (r->factor != 2 && r->factor != 4)
            throw std::runtime_error("el modelo agranda x" + std::to_string(r->factor) + ": se usa x2 o x4");
        // Una x4 con la imagen entera saldría x4: se vuelve a abrir para la mitad.
        if (r->factor == 4 && !r->mitad && w % 4 == 0 && h % 4 == 0) {
            r->mitad = true;
            r->sesion.reset();
            continue;
        }
        return r.release();
    }
    throw std::runtime_error("no se pudo abrir la red a la mitad");
}
#endif

/// Abre la sesión para este tamaño (en el hilo de preparación).
static void abrirRed(BaOnnxSr* s, int w, int h) {
#if defined(BA_HAVE_ORT)
    std::string motivos;
    try {
        Red* r = abrirCon(s, w, h, motivos);
        r->in.resize(static_cast<size_t>(r->canales) * std::max(r->iw, 1) * std::max(r->ih, 1));
        s->lista->store(r);
        GST_INFO_OBJECT(s, "red %s: %s, x%d%s, %s (%s)", s->rutaRed->c_str(), r->nv12 ? "NV12" : "float", r->factor,
                        r->mitad ? " a la mitad" : "", r->proveedor.c_str(), motivos.c_str());
        // Si se quedó con uno peor que el pedido, que se sepa por qué.
        ponerError(s, motivos.empty() || r->proveedor != "cpu" ? "" : "corre en la CPU: " + motivos);
    } catch (const std::exception& e) {
        ponerError(s, std::string("no se pudo abrir la red: ") + e.what());
    }
#else
    (void)w;
    (void)h;
    ponerError(s, "el programa no trae ONNX Runtime");
#endif
}

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* s = BA_ONNXSR(bt);
    // La memoria: sale siempre en la de sistema; entra en la de sistema o (con
    // gst-cuda) en la de CUDA.
    GstCaps* sinMem = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i)
        gst_caps_append_structure(sinMem, gst_structure_copy(gst_caps_get_structure(caps, i)));
    GstCaps* r = ba::sr::transformarCaps(dir, sinMem, nullptr, s->activo && hayModelo(s));
    gst_caps_unref(sinMem);
#if defined(BA_HAVE_GST_CUDA)
    if (dir == GST_PAD_SRC) {
        GstCaps* conCuda = gst_caps_copy(r);
        for (guint i = 0; i < gst_caps_get_size(conCuda); ++i)
            gst_caps_set_features(conCuda, i, gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr));
        r = gst_caps_merge(conCuda, r); // CUDA primero: si el de arriba puede, sin bajar a la CPU
    }
#endif
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, r, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(r);
        r = f;
    }
    return r;
}

static gboolean tamanoSalida(GstBaseTransform*, GstPadDirection, GstCaps*, gsize, GstCaps* otras, gsize* otro) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps(&vi, otras)) return FALSE;
    *otro = GST_VIDEO_INFO_SIZE(&vi);
    return TRUE;
}

static gboolean ponerCaps(GstBaseTransform* bt, GstCaps* incaps, GstCaps* outcaps) {
    auto* s = BA_ONNXSR(bt);
    if (!gst_video_info_from_caps(&s->in, incaps) || !gst_video_info_from_caps(&s->out, outcaps)) return FALSE;
    s->procesa = GST_VIDEO_INFO_WIDTH(&s->out) == 2 * GST_VIDEO_INFO_WIDTH(&s->in) &&
                 GST_VIDEO_INFO_HEIGHT(&s->out) == 2 * GST_VIDEO_INFO_HEIGHT(&s->in);
    const GstCapsFeatures* fe = gst_caps_get_size(incaps) > 0 ? gst_caps_get_features(incaps, 0) : nullptr;
    s->entradaCuda = FALSE;
#if defined(BA_HAVE_GST_CUDA)
    if (fe != nullptr && gst_caps_features_contains(fe, GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
        if (!gst_cuda_ensure_element_context(GST_ELEMENT(s), -1, &s->cuda)) {
            GST_ELEMENT_ERROR(s, RESOURCE, NOT_FOUND, ("no hay un contexto CUDA"), (nullptr));
            return FALSE;
        }
        s->entradaCuda = TRUE;
    }
#else
    (void)fe;
#endif
    soltarRed(s); // se abre al primer cuadro, con este tamaño
    // Mismo tamaño pero de CUDA a sistema: no es pasante (hay que bajarlo).
    gst_base_transform_set_passthrough(bt, !s->procesa && !s->entradaCuda);
    GST_INFO_OBJECT(s, "%dx%d %s, %s", GST_VIDEO_INFO_WIDTH(&s->in), GST_VIDEO_INFO_HEIGHT(&s->in),
                    s->entradaCuda ? "en la placa (CUDAMemory)" : "en memoria de sistema",
                    s->procesa ? "agranda" : "no agranda");
    return TRUE;
}

/// El plano de color (UV intercalado) al doble, bilineal.
static void colorX2(const guint8* uv, int paso, int cw, int ch, guint8* dst, int pasoD) {
    for (int y = 0; y < ch * 2; ++y) {
        const float sy = std::max(0.0f, (y + 0.5f) / 2.0f - 0.5f);
        const int y0 = std::min(static_cast<int>(sy), ch - 1), y1 = std::min(y0 + 1, ch - 1);
        const float fy = sy - static_cast<float>(y0);
        const guint8* r0 = uv + static_cast<size_t>(y0) * paso;
        const guint8* r1 = uv + static_cast<size_t>(y1) * paso;
        guint8* o = dst + static_cast<size_t>(y) * pasoD;
        for (int x = 0; x < cw * 2; ++x) {
            const float sx = std::max(0.0f, (x + 0.5f) / 2.0f - 0.5f);
            const int x0 = std::min(static_cast<int>(sx), cw - 1), x1 = std::min(x0 + 1, cw - 1);
            const float fx = sx - static_cast<float>(x0);
            for (int c = 0; c < 2; ++c) {
                const float a = r0[x0 * 2 + c] * (1 - fx) + r0[x1 * 2 + c] * fx;
                const float b = r1[x0 * 2 + c] * (1 - fx) + r1[x1 * 2 + c] * fx;
                o[x * 2 + c] = static_cast<guint8>(a * (1 - fy) + b * fy + 0.5f);
            }
        }
    }
}

static inline guint8 sat(float v) { return static_cast<guint8>(std::clamp(v, 0.0f, 255.0f) + 0.5f); }

/// Sin red (preparándose, o falló): agrandado simple para que la imagen siga.
static void agrandarSimple(GstVideoFrame* fi, GstVideoFrame* fo) {
    const int w = GST_VIDEO_FRAME_WIDTH(fi), h = GST_VIDEO_FRAME_HEIGHT(fi);
    const auto* Y = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fi, 0));
    const int pY = GST_VIDEO_FRAME_PLANE_STRIDE(fi, 0);
    auto* oY = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fo, 0));
    const int poY = GST_VIDEO_FRAME_PLANE_STRIDE(fo, 0);
    // La luma bilineal (como el color): sin escalones.
    for (int y = 0; y < h * 2; ++y) {
        const float sy = std::max(0.0f, (y + 0.5f) / 2.0f - 0.5f);
        const int y0 = std::min(static_cast<int>(sy), h - 1), y1 = std::min(y0 + 1, h - 1);
        const int fy = static_cast<int>((sy - static_cast<float>(y0)) * 256.0f);
        const guint8* r0 = Y + static_cast<size_t>(y0) * pY;
        const guint8* r1 = Y + static_cast<size_t>(y1) * pY;
        guint8* o = oY + static_cast<size_t>(y) * poY;
        for (int x = 0; x < w * 2; ++x) {
            const int x0 = x == 0 ? 0 : std::min((x - 1) / 2, w - 1);
            const int x1 = std::min(x0 + ((x & 1) == 0 ? 1 : 1), w - 1);
            const int fx = x == 0 ? 0 : ((x & 1) != 0 ? 64 : 192); // 1/4 y 3/4 entre muestras
            const int a = r0[x0] * (256 - fx) + r0[x1] * fx;
            const int b = r1[x0] * (256 - fx) + r1[x1] * fx;
            o[x] = static_cast<guint8>((a * (256 - fy) + b * fy + (1 << 15)) >> 16);
        }
    }
    colorX2(static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fi, 1)), GST_VIDEO_FRAME_PLANE_STRIDE(fi, 1),
            w / 2, h / 2, static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fo, 1)), GST_VIDEO_FRAME_PLANE_STRIDE(fo, 1));
}

#if defined(BA_HAVE_ORT)
/// Un plano como tensor uint8 [1,1,alto,ancho] SIN copiar si es contiguo; si
/// tiene relleno, se compacta en `tmp`.
static Ort::Value tensorPlano(guint8* datos, int stride, int ancho, int alto, std::vector<guint8>& tmp,
                              bool copiar) {
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 4> dims{1, 1, alto, ancho};
    guint8* p = datos;
    if (stride != ancho) {
        tmp.resize(static_cast<size_t>(ancho) * alto);
        if (copiar)
            for (int y = 0; y < alto; ++y)
                std::memcpy(tmp.data() + static_cast<size_t>(y) * ancho, datos + static_cast<size_t>(y) * stride,
                            static_cast<size_t>(ancho));
        p = tmp.data();
    }
    return Ort::Value::CreateTensor<guint8>(mem, p, tmp.empty() || p == datos ? static_cast<size_t>(stride) * alto
                                                                               : tmp.size(),
                                            dims.data(), dims.size());
}

/// Modelo NV12: los planos del cuadro van y vuelven tal cual (la placa hace todo).
static void correrNv12(Red* r, GstVideoFrame* fi, GstVideoFrame* fo) {
    const int w = GST_VIDEO_FRAME_WIDTH(fi), h = GST_VIDEO_FRAME_HEIGHT(fi);
    std::vector<guint8> t0, t1, t2, t3;
    const auto plano = [](GstVideoFrame* f, int i) { return static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(f, i)); };
    const auto paso = [](GstVideoFrame* f, int i) { return GST_VIDEO_FRAME_PLANE_STRIDE(f, i); };
    Ort::Value ins[2] = {tensorPlano(plano(fi, 0), paso(fi, 0), w, h, t0, true),
                         tensorPlano(plano(fi, 1), paso(fi, 1), w, h / 2, t1, true)};
    Ort::Value outs[2] = {tensorPlano(plano(fo, 0), paso(fo, 0), 2 * w, 2 * h, t2, false),
                          tensorPlano(plano(fo, 1), paso(fo, 1), 2 * w, h, t3, false)};
    const char* ni[] = {r->entradas[0].c_str(), r->entradas[1].c_str()};
    const char* no[] = {r->salidas[0].c_str(), r->salidas[1].c_str()};
    // Las salidas van directo a los planos del cuadro de salida.
    r->sesion->Run(Ort::RunOptions{nullptr}, ni, ins, 2, no, outs, 2);
    // Si había relleno, se copia al cuadro.
    for (int i = 0; i < 2; ++i) {
        std::vector<guint8>& t = i == 0 ? t2 : t3;
        if (t.empty()) continue;
        const int alto = i == 0 ? 2 * h : h;
        for (int y = 0; y < alto; ++y)
            std::memcpy(plano(fo, i) + static_cast<size_t>(y) * paso(fo, i), t.data() + static_cast<size_t>(y) * 2 * w,
                        static_cast<size_t>(2 * w));
    }
}

/// f(desde, hasta) sobre las filas [0, n), en franjas en varios hilos (filas distintas: sin cruces).
template <class F>
static void enFranjas(int n, F f) {
    const int hilos = n >= 256 ? static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u)) : 1;
    std::vector<std::thread> v;
    for (int k = 1; k < hilos; ++k)
        v.emplace_back(f, n * k / hilos, n * (k + 1) / hilos);
    f(0, n / hilos);
    for (auto& t : v)
        t.join();
}

/// Modelo float: NV12 -> float (a la mitad si es x4) -> red -> NV12 al doble.
static void correrFloat(Red* r, GstVideoFrame* fi, GstVideoFrame* fo) {
    const int w = GST_VIDEO_FRAME_WIDTH(fi), h = GST_VIDEO_FRAME_HEIGHT(fi);
    const auto* Y = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fi, 0));
    const auto* UV = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fi, 1));
    const int pY = GST_VIDEO_FRAME_PLANE_STRIDE(fi, 0), pUV = GST_VIDEO_FRAME_PLANE_STRIDE(fi, 1);
    auto* oY = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fo, 0));
    auto* oUV = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(fo, 1));
    const int poY = GST_VIDEO_FRAME_PLANE_STRIDE(fo, 0), poUV = GST_VIDEO_FRAME_PLANE_STRIDE(fo, 1);
    const int C = r->canales, iw = r->iw, ih = r->ih;
    // El color de la entrada (lo que digan las caps): matriz BT.601/709/2020 y
    // rango limitado o completo. De ida y de vuelta con la misma, así sale igual.
    const GstVideoColorimetry& col = GST_VIDEO_INFO_COLORIMETRY(&fi->info);
    gdouble Kr = 0.2126, Kb = 0.0722;
    if (!gst_video_color_matrix_get_Kr_Kb(col.matrix, &Kr, &Kb)) Kr = 0.2126, Kb = 0.0722;
    const float kr = static_cast<float>(Kr), kb = static_cast<float>(Kb), kg = 1.0f - kr - kb;
    const bool completo = col.range == GST_VIDEO_COLOR_RANGE_0_255;
    const float y0 = completo ? 0.0f : 16.0f, yE = completo ? 255.0f : 219.0f, cE = completo ? 255.0f : 224.0f;
    // YCbCr -> RGB: R = Y + 2(1-Kr)·Cr ; B = Y + 2(1-Kb)·Cb ; G = (Y - Kr·R - Kb·B) / Kg.
    const float aR = 2.0f * (1.0f - kr), aB = 2.0f * (1.0f - kb);
    const float gU = -aB * kb / kg, gV = -aR * kr / kg;
    std::vector<float>& in = r->in;
    in.resize(static_cast<size_t>(C) * iw * ih);
    const size_t planoI = static_cast<size_t>(iw) * ih;
    // En franjas, en varios hilos: en 1080p son millones de píxeles por cuadro (en
    // un solo hilo, la conversión tardaba más que la red en la GPU).
    enFranjas(ih, [&](int fila0, int fila1) {
    for (int y = fila0; y < fila1; ++y)
        for (int x = 0; x < iw; ++x) {
            // La luma: el píxel, o el promedio 2x2 si va a la mitad.
            float yy;
            if (r->mitad) {
                const guint8* a = Y + static_cast<size_t>(2 * y) * pY + 2 * x;
                yy = (a[0] + a[1] + a[pY] + a[pY + 1]) * 0.25f;
            } else {
                yy = Y[static_cast<size_t>(y) * pY + x];
            }
            yy = (yy - y0) / yE;
            const size_t i = static_cast<size_t>(y) * iw + x;
            if (C == 1) {
                in[i] = yy;
                continue;
            }
            // El color: a la mitad, el plano UV coincide justo con la imagen.
            const int cy = r->mitad ? y : y / 2, cx = r->mitad ? x : x / 2;
            const float u = (UV[static_cast<size_t>(cy) * pUV + cx * 2] - 128.0f) / cE;
            const float v = (UV[static_cast<size_t>(cy) * pUV + cx * 2 + 1] - 128.0f) / cE;
            in[i] = std::clamp(yy + aR * v, 0.0f, 1.0f);
            in[planoI + i] = std::clamp(yy + gU * u + gV * v, 0.0f, 1.0f);
            in[2 * planoI + i] = std::clamp(yy + aB * u, 0.0f, 1.0f);
        }
    });
    const int W = iw * r->factor, H = ih * r->factor;
    const float* o = nullptr;
    std::vector<Ort::Value> res; // vive mientras se lee la salida (ONNX Runtime)
#if defined(BA_HAVE_NCNN)
    if (r->net) {
        // ncnn (Vulkan): sube, corre en la GPU y baja; la salida, contigua.
        ncnn::Mat mi(iw, ih, C);
        for (int c = 0; c < C; ++c)
            std::memcpy(mi.channel(c), in.data() + c * planoI, planoI * sizeof(float));
        ncnn::Extractor ex = r->net->create_extractor();
        ex.input(r->blobIn.c_str(), mi);
        ncnn::Mat mo;
        if (ex.extract(r->blobOut.c_str(), mo) != 0 || mo.w != W || mo.h != H)
            throw std::runtime_error("ncnn no devolvió la imagen agrandada");
        const size_t planoS = static_cast<size_t>(W) * H;
        r->out.resize(static_cast<size_t>(C) * planoS);
        for (int c = 0; c < C; ++c)
            std::memcpy(r->out.data() + c * planoS, mo.channel(c), planoS * sizeof(float));
        o = r->out.data();
    } else
#endif
    {
        const std::array<int64_t, 4> dims{1, C, ih, iw};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value t = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), dims.data(), dims.size());
        const char* ins[] = {r->entradas[0].c_str()};
        const char* outs[] = {r->salidas[0].c_str()};
        res = r->sesion->Run(Ort::RunOptions{nullptr}, ins, &t, 1, outs, 1);
        o = res[0].GetTensorData<float>();
    }
    const int k = std::max(1, W / (2 * w)); // 1, o 2 si una x4 entró entera (se promedia 2x2)
    const float div = 1.0f / static_cast<float>(k * k);
    const size_t planoO = static_cast<size_t>(W) * H;
    enFranjas(h * 2, [&](int fila0, int fila1) {
    for (int y = fila0; y < fila1; ++y)
        for (int x = 0; x < w * 2; ++x) {
            float acc[3] = {0, 0, 0};
            for (int dy = 0; dy < k; ++dy)
                for (int dx = 0; dx < k; ++dx) {
                    const size_t i = static_cast<size_t>(y * k + dy) * W + (x * k + dx);
                    for (int c = 0; c < C; ++c)
                        acc[c] += o[c * planoO + i];
                }
            if (C == 1) {
                oY[y * poY + x] = sat(y0 + yE * acc[0] * div);
            } else {
                const float R = acc[0] * div, G = acc[1] * div, B = acc[2] * div;
                const float L = kr * R + kg * G + kb * B;
                oY[y * poY + x] = sat(y0 + yE * L);
                if ((x & 1) == 0 && (y & 1) == 0) {
                    oUV[(y / 2) * poUV + x] = sat(128.0f + cE * (B - L) / aB);
                    oUV[(y / 2) * poUV + x + 1] = sat(128.0f + cE * (R - L) / aR);
                }
            }
        }
    });
    if (C == 1) colorX2(UV, pUV, w / 2, h / 2, oUV, poUV);
}
#endif

#if defined(BA_HAVE_GST_CUDA) && defined(BA_HAVE_ORT)
/// Copia un plano de 8 bits entre memorias (placa/sistema), con sus pasos.
static bool copiar2D(const void* src, int pasoSrc, bool srcPlaca, void* dst, int pasoDst, bool dstPlaca, int ancho,
                     int alto) {
    CUDA_MEMCPY2D c = {};
    c.srcMemoryType = srcPlaca ? CU_MEMORYTYPE_DEVICE : CU_MEMORYTYPE_HOST;
    if (srcPlaca)
        c.srcDevice = reinterpret_cast<CUdeviceptr>(src);
    else
        c.srcHost = src;
    c.srcPitch = static_cast<size_t>(pasoSrc);
    c.dstMemoryType = dstPlaca ? CU_MEMORYTYPE_DEVICE : CU_MEMORYTYPE_HOST;
    if (dstPlaca)
        c.dstDevice = reinterpret_cast<CUdeviceptr>(dst);
    else
        c.dstHost = dst;
    c.dstPitch = static_cast<size_t>(pasoDst);
    c.WidthInBytes = static_cast<size_t>(ancho);
    c.Height = static_cast<size_t>(alto);
    return CuMemcpy2D(&c) == CUDA_SUCCESS;
}

/// NV12 en la placa: CUDAMemory -> tensores de la red (placa a placa) -> red
/// (IoBinding, CUDA/TensorRT) -> cuadro de salida (baja una vez).
static bool correrEnPlaca(BaOnnxSr* s, Red* r, GstBuffer* inbuf, GstVideoFrame* fo) {
    const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
    if (!r->alCuda || r->bw != w || r->bh != h) {
        r->memCuda = std::make_unique<Ort::MemoryInfo>("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);
        r->alCuda = std::make_unique<Ort::Allocator>(*r->sesion, *r->memCuda);
        r->yD = r->alCuda->Alloc(static_cast<size_t>(w) * h);
        r->uvD = r->alCuda->Alloc(static_cast<size_t>(w) * h / 2);
        r->y2D = r->alCuda->Alloc(static_cast<size_t>(4) * w * h);
        r->uv2D = r->alCuda->Alloc(static_cast<size_t>(2) * w * h);
        r->bw = w;
        r->bh = h;
    }
    GstVideoFrame fi;
    if (!gst_video_frame_map(&fi, &s->in, inbuf, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_CUDA))) return false;
    bool ok = gst_cuda_context_push(s->cuda);
    if (ok) {
        ok = copiar2D(GST_VIDEO_FRAME_PLANE_DATA(&fi, 0), GST_VIDEO_FRAME_PLANE_STRIDE(&fi, 0), true, r->yD, w, true,
                      w, h) &&
             copiar2D(GST_VIDEO_FRAME_PLANE_DATA(&fi, 1), GST_VIDEO_FRAME_PLANE_STRIDE(&fi, 1), true, r->uvD, w, true,
                      w, h / 2);
        gst_cuda_context_pop(nullptr);
    }
    gst_video_frame_unmap(&fi);
    if (!ok) return false;
    const auto tensor = [&](void* p, int64_t alto, int64_t ancho) {
        const std::array<int64_t, 4> d{1, 1, alto, ancho};
        return Ort::Value::CreateTensor(*r->memCuda, p, static_cast<size_t>(alto * ancho), d.data(), d.size(),
                                        ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8);
    };
    Ort::IoBinding io(*r->sesion);
    io.BindInput(r->entradas[0].c_str(), tensor(r->yD, h, w));
    io.BindInput(r->entradas[1].c_str(), tensor(r->uvD, h / 2, w));
    io.BindOutput(r->salidas[0].c_str(), tensor(r->y2D, 2 * h, 2 * w));
    io.BindOutput(r->salidas[1].c_str(), tensor(r->uv2D, h, 2 * w));
    r->sesion->Run(Ort::RunOptions{nullptr}, io);
    io.SynchronizeOutputs();
    // Baja una vez, directo a los planos del cuadro de salida.
    if (!gst_cuda_context_push(s->cuda)) return false;
    ok = copiar2D(r->y2D, 2 * w, true, GST_VIDEO_FRAME_PLANE_DATA(fo, 0), GST_VIDEO_FRAME_PLANE_STRIDE(fo, 0), false,
                  2 * w, 2 * h) &&
         copiar2D(r->uv2D, 2 * w, true, GST_VIDEO_FRAME_PLANE_DATA(fo, 1), GST_VIDEO_FRAME_PLANE_STRIDE(fo, 1), false,
                  2 * w, h);
    gst_cuda_context_pop(nullptr);
    return ok;
}
#endif

/// Afilado del agrandado común (modo barato): la luma, 3x3 en cruz.
static void afilarLuma(GstVideoFrame* f) {
    auto* Y = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(f, 0));
    const int p = GST_VIDEO_FRAME_PLANE_STRIDE(f, 0), w = GST_VIDEO_FRAME_WIDTH(f), h = GST_VIDEO_FRAME_HEIGHT(f);
    std::vector<guint8> arriba(static_cast<size_t>(w)), actual(static_cast<size_t>(w));
    std::memcpy(arriba.data(), Y, static_cast<size_t>(w));
    for (int y = 0; y < h; ++y) {
        guint8* fila = Y + static_cast<size_t>(y) * p;
        std::memcpy(actual.data(), fila, static_cast<size_t>(w));
        const guint8* abajo = Y + static_cast<size_t>(std::min(y + 1, h - 1)) * p;
        for (int x = 0; x < w; ++x) {
            const int c = actual[x];
            const int cruz = arriba[x] + abajo[x] + actual[std::max(x - 1, 0)] + actual[std::min(x + 1, w - 1)];
            fila[x] = sat(static_cast<float>(c) + 0.25f * (4 * c - cruz));
        }
        std::swap(arriba, actual);
    }
}

/// La red sobre `inbuf`, con el resultado en `fo` (CUDA si la entrada está en la
/// placa y la red también; si no, en la CPU). true si anduvo.
static bool correrRedEn(BaOnnxSr* s, GstBuffer* inbuf, GstVideoFrame* fi, GstVideoFrame* fo) {
#if defined(BA_HAVE_ORT)
    Red* r = s->red;
    if (r == nullptr) return false;
    const gint64 t0 = g_get_monotonic_time();
    try {
        bool ok = false;
#if defined(BA_HAVE_GST_CUDA)
        if (s->entradaCuda && r->enPlaca()) ok = correrEnPlaca(s, r, inbuf, fo);
#endif
        if (!ok && fi != nullptr) {
            if (r->nv12)
                correrNv12(r, fi, fo);
            else
                correrFloat(r, fi, fo);
            ok = true;
        }
        if (ok) s->demora->anotar(static_cast<double>(g_get_monotonic_time() - t0) / 1000.0);
        return ok;
    } catch (const std::exception& e) {
        ponerError(s, std::string("la red falló: ") + e.what());
    }
#else
    (void)s;
    (void)inbuf;
    (void)fi;
    (void)fo;
#endif
    return false;
}

/// Un cuadro (en el hilo de la red), con su modo. false si no se usó la red.
static bool procesar(BaOnnxSr* s, GstBuffer* inbuf, GstBuffer* outbuf, double mezcla, bool barato, bool mantener) {
    GstVideoFrame fi, fo;
    if (!gst_video_frame_map(&fo, &s->out, outbuf, GST_MAP_WRITE)) return false;
    bool usoRed = false;
    // Activo del todo con la red y el cuadro en la placa (CUDA): sin mapear la
    // entrada en la CPU (eso la bajaría).
#if defined(BA_HAVE_ORT)
    if (mezcla >= 1.0 && s->entradaCuda && s->red != nullptr && s->red->enPlaca())
        usoRed = correrRedEn(s, inbuf, nullptr, &fo);
#endif
    if (usoRed) {
        gst_video_frame_unmap(&fo);
        return true;
    }
    // La entrada en la CPU (si está en la placa, el mapeo la baja): para el
    // agrandado común o si la red corre en la CPU.
    const bool hayFi = gst_video_frame_map(&fi, &s->in, inbuf, GST_MAP_READ);
    // Activo del todo: la red directo a la salida.
    if (mezcla >= 1.0) usoRed = correrRedEn(s, inbuf, hayFi ? &fi : nullptr, &fo);
    if (!usoRed && hayFi) {
        // Lo de base (nunca negro): el agrandado común; afilado en modo barato.
        agrandarSimple(&fi, &fo);
        if (barato && mezcla <= 0.0) afilarLuma(&fo);
        // La transición (mezcla con la red) o la pasada de mantenimiento.
        if (mezcla > 0.0 || mantener) {
            GstBuffer* tmp = gst_buffer_new_allocate(nullptr, GST_VIDEO_INFO_SIZE(&s->out), nullptr);
            GstVideoFrame ft;
            if (tmp != nullptr && gst_video_frame_map(&ft, &s->out, tmp, GST_MAP_READWRITE)) {
                if (correrRedEn(s, inbuf, &fi, &ft) && mezcla > 0.0) {
                    for (int pl = 0; pl < 2; ++pl)
                        ba::sr::mezclarPlano(static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fo, pl)),
                                             GST_VIDEO_FRAME_PLANE_STRIDE(&fo, pl),
                                             static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&ft, pl)),
                                             GST_VIDEO_FRAME_PLANE_STRIDE(&ft, pl), GST_VIDEO_FRAME_WIDTH(&fo),
                                             pl == 0 ? GST_VIDEO_FRAME_HEIGHT(&fo) : GST_VIDEO_FRAME_HEIGHT(&fo) / 2,
                                             mezcla);
                    usoRed = true;
                }
                gst_video_frame_unmap(&ft);
            }
            if (tmp != nullptr) gst_buffer_unref(tmp);
        }
    }
    if (hayFi) gst_video_frame_unmap(&fi);
    gst_video_frame_unmap(&fo);
    return usoRed;
}

/// ¿Terminó de prepararse la red? Si no hay, empieza a prepararla.
static void quizasPreparar(BaOnnxSr* s) {
#if defined(BA_HAVE_ORT)
    if (s->red != nullptr) return;
    if (Red* r = s->lista->exchange(nullptr)) {
        s->red = r;
        if (s->preparando != nullptr) g_thread_join(s->preparando), s->preparando = nullptr;
    } else if (s->preparando == nullptr && !hayError(s) && hayModelo(s)) {
        s->pw = GST_VIDEO_INFO_WIDTH(&s->in);
        s->ph = GST_VIDEO_INFO_HEIGHT(&s->in);
        *s->rutaRed = ba::sr::rutaVariante(s->modelo, s->variantes, s->pw, s->ph);
        s->preparando = g_thread_new(
            "baonnxsr-preparar",
            [](gpointer d) -> gpointer {
                auto* x = static_cast<BaOnnxSr*>(d);
                abrirRed(x, x->pw, x->ph);
                return nullptr;
            },
            s);
    }
#endif
}

static GstBuffer* sacar(BaOnnxSr* s) {
    Trabajo* t = s->pendientes->front();
    s->pendientes->pop_front();
    t->listo.wait();
    GstBuffer* out = t->salida;
    gst_buffer_unref(t->entrada);
    delete t;
    return out;
}

static void descartar(BaOnnxSr* s) {
    while (!s->pendientes->empty())
        gst_buffer_unref(sacar(s));
}

static constexpr size_t kEnVuelo = 1;

static GstFlowReturn generar(GstBaseTransform* bt, GstBuffer** outbuf) {
    auto* s = BA_ONNXSR(bt);
    *outbuf = nullptr;
    if (!s->procesa && s->pendientes->empty())
        return GST_BASE_TRANSFORM_CLASS(ba_onnxsr_parent_class)->generate_output(bt, outbuf);
    if (GstBuffer* in = bt->queued_buf) {
        bt->queued_buf = GST_DEBUG_FUNCPTR(nullptr);
        GstBuffer* out = nullptr;
        // El buffer de salida, del pool negociado (con marcas de tiempo y metadatos).
        const GstFlowReturn fr =
            GST_BASE_TRANSFORM_CLASS(ba_onnxsr_parent_class)->prepare_output_buffer(bt, in, &out);
        if (fr != GST_FLOW_OK || out == nullptr) {
            gst_buffer_unref(in);
            return fr != GST_FLOW_OK ? fr : GST_FLOW_ERROR;
        }
        quizasPreparar(s);
        auto* t = new Trabajo();
        t->entrada = in;
        t->salida = out;
        // El modo de este cuadro: la mezcla avanza un paso hacia lo pedido.
        const int modo = s->modo->load();
        const double fps = GST_VIDEO_INFO_FPS_D(&s->in) > 0 && GST_VIDEO_INFO_FPS_N(&s->in) > 0
                               ? static_cast<double>(GST_VIDEO_INFO_FPS_N(&s->in)) / GST_VIDEO_INFO_FPS_D(&s->in)
                               : 30.0;
        const double mezcla = s->mezcla.avanzar(modo, s->transicionMs->load(), fps);
        const bool barato = modo == ba::sr::kBarato;
        const bool mantener = mezcla <= 0.0 && (++s->cuadros % 150) == 0; // la red caliente
        t->listo = s->obrero->enviar(
            [s, in, out, mezcla, barato, mantener] { return procesar(s, in, out, mezcla, barato, mantener); });
        s->pendientes->push_back(t);
    }
    while (!s->pendientes->empty()) {
        Trabajo* f = s->pendientes->front();
        const bool listo = f->listo.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (!listo && s->pendientes->size() <= kEnVuelo) break;
        *outbuf = sacar(s);
        ba::sr::quizasAvisarLatencia(bt, *s->demora);
        return GST_FLOW_OK;
    }
    return GST_FLOW_OK;
}

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* inbuf, GstBuffer* outbuf) {
    // Solo sin red en marcha (generar manda todo al hilo de la red).
    auto* s = BA_ONNXSR(bt);
    if (s->procesa) {
        procesar(s, inbuf, outbuf, 1.0, false, false);
    } else if (s->entradaCuda) {
        // Mismo tamaño (apagado): de la placa a la memoria de sistema, tal cual.
        GstVideoFrame fi, fo;
        if (!gst_video_frame_map(&fi, &s->in, inbuf, GST_MAP_READ)) return GST_FLOW_ERROR;
        if (!gst_video_frame_map(&fo, &s->out, outbuf, GST_MAP_WRITE)) {
            gst_video_frame_unmap(&fi);
            return GST_FLOW_ERROR;
        }
        gst_video_frame_copy(&fo, &fi);
        gst_video_frame_unmap(&fo);
        gst_video_frame_unmap(&fi);
    }
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_ONNXSR(o);
    switch (id) {
        case PROP_MODELO:
            g_free(s->modelo);
            s->modelo = g_value_dup_string(v);
            ponerError(s, "");
            soltarRed(s);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        case PROP_VARIANTES:
            g_free(s->variantes);
            s->variantes = g_value_dup_string(v);
            soltarRed(s); // se vuelve a abrir con la del tamaño
            break;
        case PROP_ACTIVO:
            s->activo = g_value_get_boolean(v);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        case PROP_PROVEEDOR:
            g_free(s->proveedor);
            s->proveedor = g_value_dup_string(v);
            ponerError(s, "");
            soltarRed(s);
            break;
        case PROP_DISPOSITIVO:
            s->dispositivo = g_value_get_int(v);
            soltarRed(s);
            break;
        case PROP_FP16:
            s->fp16 = g_value_get_boolean(v);
            soltarRed(s);
            break;
        case PROP_CACHE:
            g_free(s->cache);
            s->cache = g_value_dup_string(v);
            break;
        case PROP_MODO: s->modo->store(g_value_get_enum(v)); break;
        case PROP_TRANSICION: s->transicionMs->store(g_value_get_int(v)); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_ONNXSR(o);
    switch (id) {
        case PROP_MODELO: g_value_set_string(v, s->modelo); break;
        case PROP_VARIANTES: g_value_set_string(v, s->variantes); break;
        case PROP_PROVEEDOR: g_value_set_string(v, s->proveedor); break;
        case PROP_DISPOSITIVO: g_value_set_int(v, s->dispositivo); break;
        case PROP_FP16: g_value_set_boolean(v, s->fp16); break;
        case PROP_CACHE: g_value_set_string(v, s->cache); break;
        case PROP_MODO: g_value_set_enum(v, s->modo->load()); break;
        case PROP_TRANSICION: g_value_set_int(v, s->transicionMs->load()); break;
        case PROP_MEZCLA: g_value_set_double(v, s->mezcla.valor); break;
        case PROP_MS: g_value_set_double(v, s->demora->media()); break;
        case PROP_ACTIVO: g_value_set_boolean(v, s->activo); break;
        case PROP_ERROR: {
            std::lock_guard<std::mutex> l(*s->errorMu);
            g_value_set_string(v, s->error);
            break;
        }
        case PROP_PROVEEDOR_USADO:
#if defined(BA_HAVE_ORT)
            g_value_set_string(v, s->red != nullptr ? s->red->proveedor.c_str() : "");
#else
            g_value_set_string(v, "");
#endif
            break;
        case PROP_PROVEEDORES: {
            std::string l;
#if defined(BA_HAVE_ORT)
            for (const char* p : {"tensorrt", "cuda", "dml", "migraphx", "rocm", "openvino", "coreml", "cpu"})
                if (ba::ort::trae(p)) l += std::string(l.empty() ? "" : ",") + p;
#endif
            g_value_set_string(v, l.c_str());
            break;
        }
        case PROP_BIBLIOTECA:
#if defined(BA_HAVE_ORT)
            g_value_set_string(v, ba::ort::runtime().biblioteca.empty() ? "programa"
                                                                         : ba::ort::runtime().biblioteca.c_str());
#else
            g_value_set_string(v, "");
#endif
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static gboolean parar(GstBaseTransform* bt) {
    soltarRed(BA_ONNXSR(bt));
    BA_ONNXSR(bt)->demora->reiniciar();
    return TRUE;
}

static gboolean consultar(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
#if defined(BA_HAVE_GST_CUDA)
    if (GST_QUERY_TYPE(q) == GST_QUERY_CONTEXT &&
        gst_cuda_handle_context_query(GST_ELEMENT(bt), q, BA_ONNXSR(bt)->cuda))
        return TRUE;
#endif
    return ba::sr::consultaLatencia(bt, q, *BA_ONNXSR(bt)->demora,
                                    GST_BASE_TRANSFORM_CLASS(ba_onnxsr_parent_class)->query, dir);
}

#if defined(BA_HAVE_GST_CUDA)
/// El contexto CUDA del pipeline (el de nvcodec): el mismo para todos.
static void ponerContexto(GstElement* e, GstContext* c) {
    gst_cuda_handle_set_context(e, c, -1, &BA_ONNXSR(e)->cuda);
    GST_ELEMENT_CLASS(ba_onnxsr_parent_class)->set_context(e, c);
}
#endif

static gboolean eventoEntrada(GstBaseTransform* bt, GstEvent* ev) {
    auto* s = BA_ONNXSR(bt);
    // El canal de control de la entrada: el modo (y lo deja pasar).
    int modo = s->modo->load(), tr = s->transicionMs->load();
    if (ba::sr::leerControlSr(ev, modo, tr)) {
        s->modo->store(modo);
        s->transicionMs->store(tr);
        GST_INFO_OBJECT(s, "control: modo %d, transición %d ms", modo, tr);
    }
    if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START) {
        descartar(s);
        s->demora->reiniciar();
    } else if (GST_EVENT_TYPE(ev) == GST_EVENT_EOS || GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT ||
               GST_EVENT_TYPE(ev) == GST_EVENT_GAP) {
        while (!s->pendientes->empty())
            gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(bt), sacar(s));
    }
    return GST_BASE_TRANSFORM_CLASS(ba_onnxsr_parent_class)->sink_event(bt, ev);
}

static void ba_onnxsr_finalize(GObject* o) {
    auto* s = BA_ONNXSR(o);
    soltarRed(s);
    delete s->obrero;
    delete s->pendientes;
    delete s->modo;
    delete s->transicionMs;
#if defined(BA_HAVE_ORT)
    delete s->lista;
#endif
    g_free(s->modelo);
    g_free(s->variantes);
    delete s->rutaRed;
    g_free(s->proveedor);
    g_free(s->cache);
    g_free(s->error);
    delete s->errorMu;
    delete s->demora;
#if defined(BA_HAVE_GST_CUDA)
    if (s->cuda != nullptr) gst_object_unref(s->cuda);
#endif
    G_OBJECT_CLASS(ba_onnxsr_parent_class)->finalize(o);
}

static void ba_onnxsr_class_init(BaOnnxSrClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_onnxsr_debug, "baonnxsr", 0, "Superresolución ONNX");
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    GstBaseTransformClass* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_onnxsr_finalize;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto listo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(
        go, PROP_ACTIVO, g_param_spec_boolean("activo", "Activo", "Agranda (FALSE: pasa sin tocar)", TRUE, rw));
    g_object_class_install_property(
        go, PROP_MODELO,
        g_param_spec_string("modelo", "Modelo", "Ruta del .onnx (vacío: pasa sin tocar)", nullptr, rw));
    g_object_class_install_property(
        go, PROP_VARIANTES,
        g_param_spec_string("variantes", "Variantes",
                            "La red con tamaño fijo para cada tamaño de cámara: «1024x576=/ruta/a.onnx;…» "
                            "(TensorRT y DirectML van mejor con tamaños fijos). Si no hay, `modelo`",
                            nullptr, rw));
    g_object_class_install_property(
        go, PROP_PROVEEDOR,
        g_param_spec_string("proveedor", "Proveedor",
                            "auto, cpu, tensorrt, cuda, dml, migraphx, rocm, openvino o coreml", "auto", rw));
    g_object_class_install_property(go, PROP_DISPOSITIVO,
                                    g_param_spec_int("dispositivo", "Dispositivo",
                                                     "Qué placa (0 = la primera)", 0, 16, 0, listo));
    g_object_class_install_property(
        go, PROP_FP16,
        g_param_spec_boolean("fp16", "FP16", "Media precisión en la placa (más rápido, casi igual)", TRUE, listo));
    g_object_class_install_property(
        go, PROP_CACHE,
        g_param_spec_string("cache", "Caché",
                            "Dónde queda la red compilada para la placa (vacío: <modelo>.cache)", nullptr, listo));
    g_object_class_install_property(go, PROP_MS,
                                    g_param_spec_double("ms-por-cuadro", "ms por cuadro",
                                                        "Lo que tarda (promedio móvil)", 0, 100000, 0, ro));
    g_object_class_install_property(
        go, PROP_ERROR, g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    g_object_class_install_property(
        go, PROP_PROVEEDOR_USADO,
        g_param_spec_string("proveedor-usado", "Proveedor usado", "Con qué corre la red", nullptr, ro));
    g_object_class_install_property(
        go, PROP_PROVEEDORES,
        g_param_spec_string("proveedores", "Proveedores", "Los que trae este ONNX Runtime", nullptr, ro));
    g_object_class_install_property(go, PROP_MODO,
                                    g_param_spec_enum("modo", "Modo",
                                                      "activo (la red), reposo (cargada; agrandado común) o barato "
                                                      "(cargada; agrandado común afilado). También por el canal de "
                                                      "control (evento ba/control, campo superres)",
                                                      ba::sr::tipoModoSr(), ba::sr::kActivo, rw));
    g_object_class_install_property(go, PROP_TRANSICION,
                                    g_param_spec_int("transicion-ms", "Transición",
                                                     "Cuánto tarda en entrar o salir la red (mezcla gradual)", 0,
                                                     10000, 400, rw));
    g_object_class_install_property(go, PROP_MEZCLA,
                                    g_param_spec_double("mezcla", "Mezcla",
                                                        "Cuánto de la red sale ahora (0 agrandado común, 1 la red)",
                                                        0, 1, 1, ro));
    g_object_class_install_property(
        go, PROP_BIBLIOTECA,
        g_param_spec_string("biblioteca", "Biblioteca", "Qué ONNX Runtime se usa (programa o BA_ORT_GPU)", nullptr,
                            ro));
    gst_element_class_set_static_metadata(
        ge, "Superresolución x2 con una red ONNX", "Filter/Effect/Video/Scaler",
        "Duplica la resolución con un modelo ONNX en la placa que haya (TensorRT, CUDA, DirectML, MIGraphX, "
        "ROCm, OpenVINO, CoreML o CPU)",
        "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bt->transform_caps = GST_DEBUG_FUNCPTR(transformarCaps);
    bt->transform_size = GST_DEBUG_FUNCPTR(tamanoSalida);
    bt->set_caps = GST_DEBUG_FUNCPTR(ponerCaps);
    bt->transform = GST_DEBUG_FUNCPTR(transformar);
    bt->generate_output = GST_DEBUG_FUNCPTR(generar);
    bt->stop = GST_DEBUG_FUNCPTR(parar);
    bt->query = GST_DEBUG_FUNCPTR(consultar);
    bt->sink_event = GST_DEBUG_FUNCPTR(eventoEntrada);
    bt->fixate_caps = GST_DEBUG_FUNCPTR(ba::sr::fijarCaps);
    bt->transform_meta = GST_DEBUG_FUNCPTR(ba::sr::transformarMeta);
#if defined(BA_HAVE_GST_CUDA)
    ge->set_context = GST_DEBUG_FUNCPTR(ponerContexto);
#endif
    bt->passthrough_on_same_caps = GST_DEBUG_FUNCPTR(TRUE);
}

static void ba_onnxsr_init(BaOnnxSr* s) {
    s->modelo = nullptr;
    s->variantes = nullptr;
    s->rutaRed = new std::string();
    s->proveedor = g_strdup("auto");
    s->cache = nullptr;
    s->dispositivo = 0;
    s->fp16 = TRUE;
    s->errorMu = new std::mutex;
    s->error = nullptr;
    s->procesa = FALSE;
#if defined(BA_HAVE_ORT)
    s->red = nullptr;
    s->preparando = nullptr;
    s->lista = new std::atomic<Red*>(nullptr);
    s->pw = s->ph = 0;
#endif
    s->obrero = new Obrero();
    s->pendientes = new std::deque<Trabajo*>();
    s->entradaCuda = FALSE;
#if defined(BA_HAVE_GST_CUDA)
    s->cuda = nullptr;
#endif
    s->modo = new std::atomic<int>(ba::sr::kActivo);
    s->transicionMs = new std::atomic<int>(400);
    s->mezcla = ba::sr::Mezcla{};
    s->cuadros = 0;
    s->activo = TRUE;
    s->demora = new ba::sr::Demora();
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(s), TRUE);
}

/// Registra `baonnxsr` en el plugin.
extern "C" gboolean ba_registrar_onnxsr(GstPlugin* plugin) {
    return gst_element_register(plugin, "baonnxsr", GST_RANK_NONE, BA_TYPE_ONNXSR);
}
