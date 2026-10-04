// ============================================================================
// src/gst-plugins/superres/coremlsr_mac.mm — superresolución x2 con un modelo CoreML
//
// Proposito:
//   Elemento de GStreamer `bacoremlsr`: duplica la resolución con una red
//   convertida a CoreML (.mlmodelc), que corre ENTERA en el Neural Engine o la
//   GPU de la Mac. (La misma red por ONNX Runtime + CoreML se partía en decenas
//   de pedazos: Real-ESRGAN compacto pasó de 9 s a ~39 ms por cuadro.)
//
//   El modelo lo arma el laboratorio (assets/superres/laboratorio.py,
//   `instalar`): entrada y salida como IMAGEN RGB, con tamaños de entrada fijos
//   (los de las cámaras comunes: el Neural Engine no acepta tamaños libres).
//   `escala` es lo que agranda la RED (2 o 4): el elemento siempre sale x2 de lo
//   que entra; una red x4 recibe la imagen a la mitad (el elemento la achica).
//
// SIN COPIAS A LA CPU:
//   - Entrada: si el cuadro viene de vtdec (decodificador de la Mac) trae su
//     CVPixelBuffer (GstCoreVideoMeta, una IOSurface): se usa ESE. Si no, se
//     envuelve la memoria del buffer (sin copiar).
//   - Color y tamaño de entrada de la red (NV12 -> BGRA, mitad para una x4): en
//     la GPU, con VTPixelTransferSession, a una IOSurface de un pool.
//   - Salida: CoreML escribe directo en una IOSurface del pool de salida
//     (outputBackings) y esa IOSurface ES el buffer que sale (GstMemory que la
//     envuelve; en Apple Silicon la memoria es unificada: leerla no copia).
//   Sin GstCoreVideoMeta (otra fuente), igual: la única pasada por la CPU es
//   la que haya hecho quien produjo el cuadro.
//
//   Mientras el modelo se carga (en otro hilo: la primera vez el Neural Engine
//   lo prepara, unos segundos), o si el tamaño de la cámara no es uno de los del
//   modelo, la imagen sale agrandada común (en la GPU): nunca se corta.
//
// GStreamer:
//   - CAPS: entra NV12 o BGRA (memoria de sistema, con o sin GstCoreVideoMeta),
//     sale BGRA al doble (transform_caps duplica/divide; pasante si está apagado).
//   - ALLOCATION: acepta GstVideoMeta (strides con relleno) arriba; abajo
//     entrega sus propios buffers (IOSurface) con GstVideoMeta.
//   - LATENCY: declara lo que tarda la red (y avisa si cambia); QoS activado.
//
// Modelo de threading:
//   La red corre en una cola propia (dispatch serial), con un cuadro en
//   vuelo: el hilo de streaming entrega el cuadro (submit_input_buffer) y saca
//   los que ya están (generate_output), en orden; solo espera si ya hay otro
//   adentro. Al terminar (EOS) se vacía la cola antes de pasar el EOS; con un
//   flush o un formato nuevo se descarta lo pendiente. El modelo se carga en un
//   hilo aparte y se entrega por un atómico.
//
// UNIDADES (`unidades`): auto = el Neural Engine (la GPU la usa el programa
//   para componer: una red ahí tardaba 6 veces más, 238 ms contra 40). Se puede
//   pedir la GPU para una instancia si el Neural Engine no da abasto.
// ============================================================================

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "comun.hpp"
#include "cvmemoria_mac.h"

#import <Accelerate/Accelerate.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <string>

G_BEGIN_DECLS
#define BA_TYPE_COREMLSR (ba_coremlsr_get_type())
G_DECLARE_FINAL_TYPE(BaCoreMlSr, ba_coremlsr, BA, COREMLSR, GstBaseTransform)
G_END_DECLS

struct _BaCoreMlSr {
    GstBaseTransform parent;
    GstVideoInfo in, out;
    gchar* modelo;
    gint escala; ///< lo que agranda la red (2 o 4)
    gboolean activo;
    gboolean procesa; ///< negociado: la salida es el doble de la entrada
    gchar* error;
    std::atomic<void*>* listo; ///< MLModel* (retenido) que dejó el hilo de carga
    void* red;                 ///< MLModel* (retenido) en uso
    GThread* cargando;
    gboolean tamanoOk; ///< el tamaño de entrada de la red está entre los del modelo
    int redW, redH;    ///< lo que recibe la red
    VTPixelTransferSessionRef transfer;
    CVPixelBufferPoolRef poolRed;    ///< BGRA redW x redH (entrada de la red)
    CVPixelBufferPoolRef poolSalida; ///< BGRA 2w x 2h (lo que sale)
    guint64 sinMeta, conMeta;        ///< cuántos cuadros llegaron con su CVPixelBuffer
    gint unidades;                   ///< 0 auto, 1 todas, 2 neural engine, 3 gpu, 4 cpu
    gint unidadesUsadas;             ///< lo que quedó (auto ya resuelto)
    void* cola;                      ///< dispatch_queue_t (retenida) donde corre la red
    std::deque<struct Trabajo*>* pendientes;
    ba::sr::Demora* demora;
    // Modos (ver comun.hpp): lo pide la propiedad o el canal de control.
    std::atomic<int>* modo;
    std::atomic<int>* transicionMs;
    ba::sr::Mezcla mezcla; ///< solo el hilo de streaming
    guint64 cuadros;
};

/// Un cuadro en vuelo.
struct Trabajo {
    GstBuffer* entrada = nullptr;
    CVPixelBufferRef salida = nullptr;
    dispatch_semaphore_t listo = nullptr;
    bool ok = false;
    double mezcla = 1.0;   ///< 0 agrandado común, 1 la red, en el medio: transición
    bool barato = false;   ///< afilar el agrandado común
    bool mantener = false; ///< pasada de mantenimiento (la red caliente; el resultado no se usa)
};

G_DEFINE_TYPE(BaCoreMlSr, ba_coremlsr, GST_TYPE_BASE_TRANSFORM)

GST_DEBUG_CATEGORY_STATIC(ba_coremlsr_debug);
#define GST_CAT_DEFAULT ba_coremlsr_debug

enum {
    PROP_0,
    PROP_MODELO,
    PROP_ESCALA,
    PROP_ACTIVO,
    PROP_MS,
    PROP_ERROR,
    PROP_SIN_COPIAS,
    PROP_UNIDADES,
    PROP_UNIDADES_USADAS,
    PROP_MODO,
    PROP_TRANSICION,
    PROP_MEZCLA
};

static const char* kUnidades[] = {"auto", "todas", "ane", "gpu", "cpu"};
static GType tipoUnidades() {
    static GType t = 0;
    if (g_once_init_enter(&t)) {
        static const GEnumValue v[] = {{0, "Elegir solo (el Neural Engine: la GPU compone la imagen)", "auto"},
                                       {1, "Las que elija CoreML", "todas"},
                                       {2, "Neural Engine (y CPU)", "ane"},
                                       {3, "GPU (y CPU)", "gpu"},
                                       {4, "Solo CPU", "cpu"},
                                       {0, nullptr, nullptr}};
        g_once_init_leave(&t, g_enum_register_static("BaCoreMlUnidades", v));
    }
    return t;
}


static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ NV12, BGRA }")));
static GstStaticPadTemplate srcTpl =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("BGRA")));

static void ponerError(BaCoreMlSr* s, const std::string& e) {
    GST_OBJECT_LOCK(s);
    g_free(s->error);
    s->error = e.empty() ? nullptr : g_strdup(e.c_str());
    GST_OBJECT_UNLOCK(s);
    if (!e.empty()) GST_WARNING_OBJECT(s, "%s", e.c_str());
}

/// La carga del modelo, en otro hilo (el Neural Engine lo prepara la primera vez).
static gpointer cargar(gpointer data) {
    auto* s = static_cast<BaCoreMlSr*>(data);
    @autoreleasepool {
        NSString* ruta = [NSString stringWithUTF8String:s->modelo != nullptr ? s->modelo : ""];
        MLModelConfiguration* conf = [[MLModelConfiguration alloc] init];
        int u = s->unidades;
        if (u == 0) u = 2; // auto: el Neural Engine
        s->unidadesUsadas = u;
        conf.computeUnits = u == 2   ? MLComputeUnitsCPUAndNeuralEngine
                            : u == 3 ? MLComputeUnitsCPUAndGPU
                            : u == 4 ? MLComputeUnitsCPUOnly
                                     : MLComputeUnitsAll;
        NSError* err = nil;
        MLModel* m = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:ruta] configuration:conf error:&err];
        if (m == nil) {
            ponerError(s, std::string("no se pudo cargar el modelo CoreML: ") +
                              (err ? err.localizedDescription.UTF8String : ruta.UTF8String));
        } else {
            s->listo->store((__bridge_retained void*)m);
            GST_INFO_OBJECT(s, "modelo CoreML cargado: %s", ruta.UTF8String);
        }
    }
    return nullptr;
}

static void soltarRed(BaCoreMlSr* s) {
    if (s->cargando != nullptr) {
        g_thread_join(s->cargando);
        s->cargando = nullptr;
    }
    if (void* p = s->listo->exchange(nullptr)) (void)(__bridge_transfer MLModel*)p;
    if (s->red != nullptr) {
        (void)(__bridge_transfer MLModel*)s->red;
        s->red = nullptr;
    }
}

static void soltarPools(BaCoreMlSr* s) {
    if (s->poolRed != nullptr) CVPixelBufferPoolRelease(s->poolRed);
    if (s->poolSalida != nullptr) CVPixelBufferPoolRelease(s->poolSalida);
    s->poolRed = s->poolSalida = nullptr;
}

static CVPixelBufferPoolRef crearPool(int w, int h) {
    NSDictionary* atr = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey : @(w),
        (id)kCVPixelBufferHeightKey : @(h),
        (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (id)kCVPixelBufferMetalCompatibilityKey : @YES,
    };
    CVPixelBufferPoolRef p = nullptr;
    CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)atr, &p);
    return p;
}

/// ¿El modelo acepta este tamaño de entrada? (sus tamaños fijos)
static bool aceptaTamano(MLModel* m, int w, int h) {
    MLFeatureDescription* d = m.modelDescription.inputDescriptionsByName[@"entrada"];
    MLImageConstraint* c = d.imageConstraint;
    if (c == nil) return false;
    if (c.pixelsWide == static_cast<NSInteger>(w) && c.pixelsHigh == static_cast<NSInteger>(h)) return true;
    MLImageSizeConstraint* sc = c.sizeConstraint;
    if (sc.type == MLImageSizeConstraintTypeEnumerated) {
        for (MLImageSize* t in sc.enumeratedImageSizes)
            if (t.pixelsWide == static_cast<NSInteger>(w) && t.pixelsHigh == static_cast<NSInteger>(h)) return true;
        return false;
    }
    if (sc.type == MLImageSizeConstraintTypeRange)
        return NSLocationInRange(static_cast<NSUInteger>(w), sc.pixelsWideRange) &&
               NSLocationInRange(static_cast<NSUInteger>(h), sc.pixelsHighRange);
    return false;
}

/// El formato cambia (NV12/BGRA -> BGRA): la salida es BGRA al doble.
static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* s = BA_COREMLSR(bt);
    GstCaps* sinFormato = gst_caps_copy(caps);
    for (guint i = 0; i < gst_caps_get_size(sinFormato); ++i)
        gst_structure_remove_fields(gst_caps_get_structure(sinFormato, i), "format", "colorimetry", "chroma-site",
                                    nullptr);
    GstCaps* r = ba::sr::transformarCaps(dir, sinFormato, nullptr, s->activo, nullptr, 2);
    gst_caps_unref(sinFormato);
    GstCaps* plantilla = gst_static_pad_template_get_caps(dir == GST_PAD_SINK ? &srcTpl : &sinkTpl);
    GstCaps* out = gst_caps_intersect_full(r, plantilla, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(r);
    gst_caps_unref(plantilla);
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = f;
    }
    return out;
}

static GstCaps* fijarCaps(GstBaseTransform*, GstPadDirection dir, GstCaps* caps, GstCaps* otras) {
    return ba::sr::fijarCapsPor(dir, caps, otras, 2);
}

static void descartar(BaCoreMlSr* s);

static gboolean ponerCaps(GstBaseTransform* bt, GstCaps* incaps, GstCaps* outcaps) {
    auto* s = BA_COREMLSR(bt);
    if (!gst_video_info_from_caps(&s->in, incaps) || !gst_video_info_from_caps(&s->out, outcaps)) return FALSE;
    const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
    s->procesa = GST_VIDEO_INFO_WIDTH(&s->out) == w * 2 && GST_VIDEO_INFO_HEIGHT(&s->out) == h * 2;
    // Mismo tamaño (apagado): igual puede cambiar el formato; si no cambia, pasante.
    gst_base_transform_set_passthrough(bt, !s->procesa && gst_caps_is_equal(incaps, outcaps));
    descartar(s); // lo que venía con el formato anterior
    s->redW = s->escala == 4 ? w / 2 : w;
    s->redH = s->escala == 4 ? h / 2 : h;
    soltarPools(s);
    if (s->procesa) {
        s->poolRed = crearPool(s->redW, s->redH);
        s->poolSalida = crearPool(w * 2, h * 2);
    } else {
        s->poolSalida = crearPool(w, h);
    }
    s->tamanoOk = FALSE;
    if (s->procesa && s->red == nullptr && s->cargando == nullptr && s->modelo != nullptr && *s->modelo != '\0')
        s->cargando = g_thread_new("bacoremlsr-carga", cargar, s);
    if (s->red != nullptr) s->tamanoOk = aceptaTamano((__bridge MLModel*)s->red, s->redW, s->redH);
    GST_INFO_OBJECT(s, "CoreML red x%d %s (%dx%d -> red %dx%d)", s->escala, s->procesa ? "activo" : "apagado", w,
                    h, s->redW, s->redH);
    return TRUE;
}

/// Arriba: un pool de IOSurfaces (quien escribe el cuadro lo hace directo en
/// memoria que la GPU y el Neural Engine usan sin copiar) y GstVideoMeta.
static gboolean proponer(GstBaseTransform* bt, GstQuery* decidida, GstQuery* q) {
    gst_query_add_allocation_meta(q, GST_VIDEO_META_API_TYPE, nullptr);
    GstCaps* caps = nullptr;
    gboolean necesita = FALSE;
    gst_query_parse_allocation(q, &caps, &necesita);
    GstVideoInfo vi;
    if (necesita && caps != nullptr && gst_video_info_from_caps(&vi, caps) && ba_cv_formato(&vi) != 0) {
        GstBufferPool* pool = ba_cv_pool_nuevo();
        GstStructure* conf = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_set_params(conf, caps, static_cast<guint>(GST_VIDEO_INFO_SIZE(&vi)), 2, 0);
        gst_buffer_pool_config_add_option(conf, GST_BUFFER_POOL_OPTION_VIDEO_META);
        if (gst_buffer_pool_set_config(pool, conf))
            gst_query_add_allocation_pool(q, pool, static_cast<guint>(GST_VIDEO_INFO_SIZE(&vi)), 2, 0);
        else
            GST_WARNING_OBJECT(bt, "no se pudo configurar el pool de IOSurfaces");
        gst_object_unref(pool);
        return TRUE;
    }
    return GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->propose_allocation(bt, decidida, q);
}

/// El cuadro de entrada como CVPixelBuffer: el de vtdec si lo trae; si no, la
/// memoria del buffer envuelta (sin copiar; queda mapeada hasta soltarla).
struct Entrada {
    CVPixelBufferRef pb = nullptr;
    GstVideoFrame frame{};
    bool mapeado = false;
};

static bool entradaDe(BaCoreMlSr* s, GstBuffer* b, Entrada& e) {
    if (CVPixelBufferRef pb = ba_cv_pixbuf_de(b)) {
        e.pb = CVPixelBufferRetain(pb);
        ++s->conMeta;
        return true;
    }
    ++s->sinMeta;
    if (!gst_video_frame_map(&e.frame, &s->in, b, GST_MAP_READ)) return false;
    e.mapeado = true;
    const int w = GST_VIDEO_FRAME_WIDTH(&e.frame), h = GST_VIDEO_FRAME_HEIGHT(&e.frame);
    if (GST_VIDEO_FRAME_FORMAT(&e.frame) == GST_VIDEO_FORMAT_BGRA) {
        CVPixelBufferCreateWithBytes(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA,
                                     GST_VIDEO_FRAME_PLANE_DATA(&e.frame, 0), GST_VIDEO_FRAME_PLANE_STRIDE(&e.frame, 0),
                                     nullptr, nullptr, nullptr, &e.pb);
    } else {
        void* planos[2] = {GST_VIDEO_FRAME_PLANE_DATA(&e.frame, 0), GST_VIDEO_FRAME_PLANE_DATA(&e.frame, 1)};
        size_t anchos[2] = {static_cast<size_t>(w), static_cast<size_t>((w + 1) / 2)};
        size_t altos[2] = {static_cast<size_t>(h), static_cast<size_t>((h + 1) / 2)};
        size_t strides[2] = {static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&e.frame, 0)),
                             static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&e.frame, 1))};
        const bool completo = GST_VIDEO_INFO_COLORIMETRY(&s->in).range == GST_VIDEO_COLOR_RANGE_0_255;
        CVPixelBufferCreateWithPlanarBytes(
            kCFAllocatorDefault, w, h,
            completo ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
            nullptr, 0, 2, planos, anchos, altos, strides, nullptr, nullptr, nullptr, &e.pb);
        if (e.pb != nullptr) {
            // El color de la entrada (para convertir bien a RGB).
            const auto matriz = GST_VIDEO_INFO_COLORIMETRY(&s->in).matrix;
            CVBufferSetAttachment(e.pb, kCVImageBufferYCbCrMatrixKey,
                                  matriz == GST_VIDEO_COLOR_MATRIX_BT601 ? kCVImageBufferYCbCrMatrix_ITU_R_601_4
                                                                         : kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                                  kCVAttachmentMode_ShouldPropagate);
        }
    }
    return e.pb != nullptr;
}

static void soltarEntrada(Entrada& e) {
    if (e.pb != nullptr) CVPixelBufferRelease(e.pb);
    if (e.mapeado) gst_video_frame_unmap(&e.frame);
}

/// Una IOSurface como GstBuffer, sin copiar: memoria CoreVideo (se bloquea
/// solo al mapear) + GstVideoMeta + GstCoreVideoMeta (ver cvmemoria_mac.h).
static GstBuffer* bufferDe(CVPixelBufferRef pb) { return ba_cv_buffer_nuevo(pb); }

/// La red: entrada (ya del tamaño de la red) -> `salida` del pool. false si no se pudo.
static bool predecir(BaCoreMlSr* s, CVPixelBufferRef entrada, CVPixelBufferRef salida) {
    @autoreleasepool {
        NSError* err = nil;
        MLFeatureValue* fv = [MLFeatureValue featureValueWithPixelBuffer:entrada];
        MLDictionaryFeatureProvider* prov = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"entrada": fv}
                                                                                            error:&err];
        if (prov == nil) return false;
        MLPredictionOptions* op = [[MLPredictionOptions alloc] init];
        if (@available(macOS 14.0, *)) op.outputBackings = @{@"salida": (__bridge id)salida};
        id<MLFeatureProvider> res = [(__bridge MLModel*)s->red predictionFromFeatures:prov options:op error:&err];
        if (res == nil) {
            ponerError(s, std::string("CoreML: ") + (err ? err.localizedDescription.UTF8String : "falló"));
            return false;
        }
        CVPixelBufferRef r = [res featureValueForName:@"salida"].imageBufferValue;
        if (r == nullptr) return false;
        if (r == salida) return true; // escribió directo en nuestra IOSurface
        // Sin outputBackings (macOS viejo, u otro formato): a nuestra IOSurface, en la GPU.
        return VTPixelTransferSessionTransferImage(s->transfer, r, salida) == noErr;
    }
}

/// Procesa un cuadro (en la cola de la red): `t->salida` queda con el resultado.
/// La entrada al tamaño de la red, como CVPixelBuffer (retenido) o nullptr.
static CVPixelBufferRef entradaDeRed(BaCoreMlSr* s, CVPixelBufferRef pb) {
    const bool directo = CVPixelBufferGetPixelFormatType(pb) == kCVPixelFormatType_32BGRA &&
                         static_cast<int>(CVPixelBufferGetWidth(pb)) == s->redW &&
                         static_cast<int>(CVPixelBufferGetHeight(pb)) == s->redH && CVPixelBufferGetIOSurface(pb) != nullptr;
    if (directo) return CVPixelBufferRetain(pb);
    CVPixelBufferRef red = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s->poolRed, &red) != kCVReturnSuccess) return nullptr;
    if (VTPixelTransferSessionTransferImage(s->transfer, pb, red) != noErr) {
        CVPixelBufferRelease(red);
        return nullptr;
    }
    return red;
}

/// La red sobre la entrada, con el resultado en `destino`. true si anduvo.
static bool correrRed(BaCoreMlSr* s, CVPixelBufferRef pb, CVPixelBufferRef destino) {
    if (s->red == nullptr || !s->tamanoOk) return false;
    const gint64 t0 = g_get_monotonic_time();
    CVPixelBufferRef red = entradaDeRed(s, pb);
    if (red == nullptr) return false;
    const bool ok = predecir(s, red, destino);
    CVPixelBufferRelease(red);
    if (ok) s->demora->anotar(static_cast<double>(g_get_monotonic_time() - t0) / 1000.0);
    return ok;
}

/// Afilado del agrandado común (modo barato): 3x3 con la Aceleración de la CPU (~1 ms).
static void afilar(CVPixelBufferRef src, CVPixelBufferRef dst) {
    CVPixelBufferLockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(dst, 0);
    vImage_Buffer a{CVPixelBufferGetBaseAddress(src), CVPixelBufferGetHeight(src), CVPixelBufferGetWidth(src),
                    CVPixelBufferGetBytesPerRow(src)};
    vImage_Buffer b{CVPixelBufferGetBaseAddress(dst), CVPixelBufferGetHeight(dst), CVPixelBufferGetWidth(dst),
                    CVPixelBufferGetBytesPerRow(dst)};
    static const int16_t k[9] = {0, -1, 0, -1, 8, -1, 0, -1, 0}; // /4: centro 2, cruz -0,25
    vImageConvolve_ARGB8888(&a, &b, nullptr, 0, 0, k, 3, 3, 4, nullptr, kvImageEdgeExtend);
    CVPixelBufferUnlockBaseAddress(dst, 0);
    CVPixelBufferUnlockBaseAddress(src, kCVPixelBufferLock_ReadOnly);
}

/// destino = destino*(1-k) + red*k (en la transición).
static void mezclar(CVPixelBufferRef destino, CVPixelBufferRef red, double k) {
    CVPixelBufferLockBaseAddress(red, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(destino, 0);
    ba::sr::mezclarPlano(static_cast<guint8*>(CVPixelBufferGetBaseAddress(destino)),
                         static_cast<int>(CVPixelBufferGetBytesPerRow(destino)),
                         static_cast<const guint8*>(CVPixelBufferGetBaseAddress(red)),
                         static_cast<int>(CVPixelBufferGetBytesPerRow(red)),
                         static_cast<int>(CVPixelBufferGetWidth(destino)) * 4,
                         static_cast<int>(CVPixelBufferGetHeight(destino)), k);
    CVPixelBufferUnlockBaseAddress(destino, 0);
    CVPixelBufferUnlockBaseAddress(red, kCVPixelBufferLock_ReadOnly);
}

static void procesar(BaCoreMlSr* s, Trabajo* t) {
    Entrada e;
    if (!entradaDe(s, t->entrada, e)) {
        soltarEntrada(e);
        return;
    }
    // Activo del todo: la red directo a la salida.
    if (t->mezcla >= 1.0 && correrRed(s, e.pb, t->salida)) {
        t->ok = true;
        soltarEntrada(e);
        return;
    }
    // Lo de base (siempre sale algo: nunca negro): el agrandado común en la GPU,
    // afilado en modo barato.
    if (t->barato && t->mezcla <= 0.0) {
        CVPixelBufferRef tmp = nullptr;
        if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s->poolSalida, &tmp) == kCVReturnSuccess &&
            VTPixelTransferSessionTransferImage(s->transfer, e.pb, tmp) == noErr) {
            afilar(tmp, t->salida);
            t->ok = true;
        }
        if (tmp != nullptr) CVPixelBufferRelease(tmp);
    }
    if (!t->ok) t->ok = VTPixelTransferSessionTransferImage(s->transfer, e.pb, t->salida) == noErr;
    // La transición (la red mezclada) o la pasada de mantenimiento (la red caliente).
    if (t->ok && (t->mezcla > 0.0 || t->mantener)) {
        CVPixelBufferRef red = nullptr;
        if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s->poolSalida, &red) == kCVReturnSuccess &&
            correrRed(s, e.pb, red) && t->mezcla > 0.0)
            mezclar(t->salida, red, t->mezcla);
        if (red != nullptr) CVPixelBufferRelease(red);
    }
    soltarEntrada(e);
}

/// ¿Terminó de cargar el modelo? (en el hilo de streaming, antes de mandar trabajo)
static void quizasTomarRed(BaCoreMlSr* s) {
    if (s->red != nullptr) return;
    void* p = s->listo->exchange(nullptr);
    if (p == nullptr) return;
    s->red = p;
    s->tamanoOk = aceptaTamano((__bridge MLModel*)s->red, s->redW, s->redH);
    if (!s->tamanoOk)
        ponerError(s, "el modelo no acepta " + std::to_string(s->redW) + "x" + std::to_string(s->redH) +
                          " (instalado para los tamaños de cámara comunes): sale agrandado común");
}

/// Espera el primero de la cola y lo convierte en el buffer que sale (nullptr si falló).
static GstBuffer* sacar(BaCoreMlSr* s) {
    Trabajo* t = s->pendientes->front();
    s->pendientes->pop_front();
    dispatch_semaphore_wait(t->listo, DISPATCH_TIME_FOREVER);
    GstBuffer* out = nullptr;
    if (t->ok) {
        out = bufferDe(t->salida);
        GST_BASE_TRANSFORM_GET_CLASS(s)->copy_metadata(GST_BASE_TRANSFORM(s), t->entrada, out);
    }
    gst_buffer_unref(t->entrada);
    CVPixelBufferRelease(t->salida);
    delete t;
    return out;
}

/// Descarta lo que esté en vuelo (flush, formato nuevo, parar).
static void descartar(BaCoreMlSr* s) {
    while (!s->pendientes->empty())
        if (GstBuffer* b = sacar(s)) gst_buffer_unref(b);
}

static constexpr size_t kEnVuelo = 1; // uno en la red mientras llega el siguiente: menos demora (el acelerador es uno)

static GstFlowReturn generar(GstBaseTransform* bt, GstBuffer** outbuf) {
    auto* s = BA_COREMLSR(bt);
    *outbuf = nullptr;
    // Apagado / mismo tamaño: lo de siempre (copia o pasante).
    if (!s->procesa && s->pendientes->empty())
        return GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->generate_output(bt, outbuf);
    if (GstBuffer* in = bt->queued_buf) {
        bt->queued_buf = nullptr;
        if (s->poolSalida == nullptr) {
            gst_buffer_unref(in);
            return GST_FLOW_NOT_NEGOTIATED;
        }
        quizasTomarRed(s);
        auto* t = new Trabajo();
        t->entrada = in;
        // El modo de este cuadro: la mezcla avanza un paso hacia lo pedido.
        const int modo = s->modo->load();
        const double fps = GST_VIDEO_INFO_FPS_D(&s->in) > 0 && GST_VIDEO_INFO_FPS_N(&s->in) > 0
                               ? static_cast<double>(GST_VIDEO_INFO_FPS_N(&s->in)) / GST_VIDEO_INFO_FPS_D(&s->in)
                               : 30.0;
        t->mezcla = s->mezcla.avanzar(modo, s->transicionMs->load(), fps);
        t->barato = modo == ba::sr::kBarato;
        // En reposo, una pasada de la red cada ~5 s: queda caliente y su demora medida.
        t->mantener = t->mezcla <= 0.0 && (++s->cuadros % 150) == 0;
        CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s->poolSalida, &t->salida);
        t->listo = dispatch_semaphore_create(0);
        if (t->salida == nullptr) {
            gst_buffer_unref(in);
            delete t;
            return GST_FLOW_ERROR;
        }
        s->pendientes->push_back(t);
        auto* cola = (__bridge dispatch_queue_t)s->cola;
        dispatch_async(cola, ^{
          @autoreleasepool {
              procesar(s, t);
          }
          dispatch_semaphore_signal(t->listo);
        });
    }
    // Sale el primero si ya está listo, o si hay demasiados en vuelo (espera).
    while (!s->pendientes->empty()) {
        Trabajo* f = s->pendientes->front();
        const bool listo = dispatch_semaphore_wait(f->listo, DISPATCH_TIME_NOW) == 0;
        if (listo) dispatch_semaphore_signal(f->listo); // lo vuelve a dejar para sacar()
        if (!listo && s->pendientes->size() <= kEnVuelo) break;
        GstBuffer* b = sacar(s);
        if (b == nullptr) continue; // falló ese cuadro: se saltea
        ba::sr::quizasAvisarLatencia(bt, *s->demora);
        *outbuf = b; // de a uno: GstBaseTransform vuelve a llamar mientras haya
        return GST_FLOW_OK;
    }
    return GST_FLOW_OK;
}

/// Sin red (copia común) lo resuelve prepararSalida; con red, generar.
static GstFlowReturn prepararSalida(GstBaseTransform* bt, GstBuffer* inbuf, GstBuffer** outbuf) {
    auto* s = BA_COREMLSR(bt);
    if (gst_base_transform_is_passthrough(bt)) {
        *outbuf = inbuf;
        return GST_FLOW_OK;
    }
    // Mismo tamaño, otro formato (apagado): NV12 -> BGRA en la GPU.
    if (s->poolSalida == nullptr) return GST_FLOW_NOT_NEGOTIATED;
    Entrada e;
    CVPixelBufferRef salida = nullptr;
    if (!entradaDe(s, inbuf, e) ||
        CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s->poolSalida, &salida) != kCVReturnSuccess ||
        VTPixelTransferSessionTransferImage(s->transfer, e.pb, salida) != noErr) {
        soltarEntrada(e);
        if (salida != nullptr) CVPixelBufferRelease(salida);
        GST_ELEMENT_ERROR(s, STREAM, FAILED, ("no se pudo convertir el cuadro"), (nullptr));
        return GST_FLOW_ERROR;
    }
    soltarEntrada(e);
    *outbuf = bufferDe(salida);
    CVPixelBufferRelease(salida);
    GST_BASE_TRANSFORM_GET_CLASS(bt)->copy_metadata(bt, inbuf, *outbuf);
    return GST_FLOW_OK;
}

static GstFlowReturn transformar(GstBaseTransform*, GstBuffer*, GstBuffer*) { return GST_FLOW_OK; }

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_COREMLSR(o);
    switch (id) {
        case PROP_MODELO:
            soltarRed(s);
            g_free(s->modelo);
            s->modelo = g_value_dup_string(v);
            ponerError(s, "");
            break;
        case PROP_ESCALA: s->escala = g_value_get_int(v) >= 4 ? 4 : 2; break;
        case PROP_UNIDADES:
            s->unidades = std::clamp(g_value_get_enum(v), 0, 4);
            soltarRed(s); // se vuelve a cargar con las unidades nuevas
            break;
        case PROP_MODO: s->modo->store(g_value_get_enum(v)); break;
        case PROP_TRANSICION: s->transicionMs->store(g_value_get_int(v)); break;
        case PROP_ACTIVO:
            s->activo = g_value_get_boolean(v);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_COREMLSR(o);
    switch (id) {
        case PROP_MODELO: g_value_set_string(v, s->modelo); break;
        case PROP_ESCALA: g_value_set_int(v, s->escala); break;
        case PROP_ACTIVO: g_value_set_boolean(v, s->activo); break;
        case PROP_UNIDADES: g_value_set_enum(v, s->unidades); break;
        case PROP_MODO: g_value_set_enum(v, s->modo->load()); break;
        case PROP_TRANSICION: g_value_set_int(v, s->transicionMs->load()); break;
        case PROP_MEZCLA: g_value_set_double(v, s->mezcla.valor); break;
        case PROP_UNIDADES_USADAS: g_value_set_string(v, kUnidades[std::clamp(s->unidadesUsadas, 0, 4)]); break;
        case PROP_MS: g_value_set_double(v, s->demora->media()); break;
        case PROP_SIN_COPIAS:
            g_value_set_double(v, s->conMeta + s->sinMeta > 0
                                      ? static_cast<double>(s->conMeta) / static_cast<double>(s->conMeta + s->sinMeta)
                                      : 0.0);
            break;
        case PROP_ERROR:
            GST_OBJECT_LOCK(s);
            g_value_set_string(v, s->error);
            GST_OBJECT_UNLOCK(s);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static gboolean arrancar(GstBaseTransform* bt) {
    auto* s = BA_COREMLSR(bt);
    if (s->transfer == nullptr) VTPixelTransferSessionCreate(kCFAllocatorDefault, &s->transfer);
    if (s->cola == nullptr)
        s->cola = (__bridge_retained void*)dispatch_queue_create("bacoremlsr", DISPATCH_QUEUE_SERIAL);
    return s->transfer != nullptr;
}

static gboolean parar(GstBaseTransform* bt) {
    auto* s = BA_COREMLSR(bt);
    descartar(s);
    GST_INFO_OBJECT(s, "cuadros con IOSurface: %" G_GUINT64_FORMAT ", copiados/envueltos: %" G_GUINT64_FORMAT
                       ", la red tardó %.1f ms",
                    s->conMeta, s->sinMeta, s->demora->media());
    s->demora->reiniciar();
    soltarPools(s);
    if (s->transfer != nullptr) {
        VTPixelTransferSessionInvalidate(s->transfer);
        CFRelease(s->transfer);
        s->transfer = nullptr;
    }
    return TRUE;
}

static gboolean consultar(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    return ba::sr::consultaLatencia(bt, q, *BA_COREMLSR(bt)->demora,
                                    GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->query, dir);
}

static gboolean eventoEntrada(GstBaseTransform* bt, GstEvent* ev) {
    auto* s = BA_COREMLSR(bt);
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
        // Lo que está en vuelo sale antes del evento (en orden).
        while (!s->pendientes->empty())
            if (GstBuffer* b = sacar(s)) gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(bt), b);
    }
    return GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->sink_event(bt, ev);
}

static void ba_coremlsr_finalize(GObject* o) {
    auto* s = BA_COREMLSR(o);
    soltarRed(s);
    descartar(s);
    soltarPools(s);
    if (s->transfer != nullptr) CFRelease(s->transfer);
    if (s->cola != nullptr) (void)(__bridge_transfer dispatch_queue_t)s->cola;
    delete s->pendientes;
    delete s->modo;
    delete s->transicionMs;
    delete s->listo;
    delete s->demora;
    g_free(s->modelo);
    g_free(s->error);
    G_OBJECT_CLASS(ba_coremlsr_parent_class)->finalize(o);
}

static void ba_coremlsr_class_init(BaCoreMlSrClass* k) {
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    GstBaseTransformClass* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_coremlsr_finalize;
    GST_DEBUG_CATEGORY_INIT(ba_coremlsr_debug, "bacoremlsr", 0, "Superresolución CoreML");
    gst_type_mark_as_plugin_api(tipoUnidades(), static_cast<GstPluginAPIFlags>(0));
    gst_type_mark_as_plugin_api(ba::sr::tipoModoSr(), static_cast<GstPluginAPIFlags>(0));
    const auto listo =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    const auto vivo =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_MODELO,
                                    g_param_spec_string("modelo", "Modelo", "Ruta del .mlmodelc", "", listo));
    g_object_class_install_property(go, PROP_ESCALA,
                                    g_param_spec_int("escala", "Escala de la red",
                                                     "Lo que agranda la red (2; 4 = recibe la mitad): sale x2", 2,
                                                     4, 2, listo));
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo", "Agranda", TRUE, vivo));
    g_object_class_install_property(go, PROP_MS,
                                    g_param_spec_double("ms-por-cuadro", "ms por cuadro",
                                                        "Lo que tarda la red en promedio", 0, 100000, 0, ro));
    g_object_class_install_property(
        go, PROP_SIN_COPIAS,
        g_param_spec_double("sin-copias", "Sin copias",
                            "Fracción de cuadros que llegaron con su IOSurface (de vtdec)", 0, 1, 0, ro));
    g_object_class_install_property(go, PROP_ERROR,
                                    g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    g_object_class_install_property(go, PROP_UNIDADES,
                                    g_param_spec_enum("unidades", "Unidades", "Dónde corre la red", tipoUnidades(), 0,
                                                      listo));
    g_object_class_install_property(go, PROP_MODO,
                                    g_param_spec_enum("modo", "Modo",
                                                      "activo (la red), reposo (cargada; agrandado común) o barato "
                                                      "(cargada; agrandado común afilado). También por el canal de "
                                                      "control (evento ba/control, campo superres)",
                                                      ba::sr::tipoModoSr(), ba::sr::kActivo, vivo));
    g_object_class_install_property(go, PROP_TRANSICION,
                                    g_param_spec_int("transicion-ms", "Transición",
                                                     "Cuánto tarda en entrar o salir la red (mezcla gradual)", 0,
                                                     10000, 400, vivo));
    g_object_class_install_property(go, PROP_MEZCLA,
                                    g_param_spec_double("mezcla", "Mezcla",
                                                        "Cuánto de la red sale ahora (0 agrandado común, 1 la red)",
                                                        0, 1, 1, ro));
    g_object_class_install_property(
        go, PROP_UNIDADES_USADAS,
        g_param_spec_string("unidades-usadas", "Unidades usadas", "Dónde quedó la red", nullptr, ro));
    gst_element_class_set_static_metadata(
        ge, "Superresolución (CoreML)", "Filter/Effect/Video/Scaler",
        "Duplica la resolución con una red CoreML en el Neural Engine o la GPU, sin copias (IOSurface)",
        "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bt->transform_caps = GST_DEBUG_FUNCPTR(transformarCaps);
    bt->fixate_caps = GST_DEBUG_FUNCPTR(fijarCaps);
    bt->set_caps = GST_DEBUG_FUNCPTR(ponerCaps);
    bt->propose_allocation = GST_DEBUG_FUNCPTR(proponer);
    bt->prepare_output_buffer = GST_DEBUG_FUNCPTR(prepararSalida);
    bt->transform = GST_DEBUG_FUNCPTR(transformar);
    bt->generate_output = GST_DEBUG_FUNCPTR(generar);
    bt->start = GST_DEBUG_FUNCPTR(arrancar);
    bt->stop = GST_DEBUG_FUNCPTR(parar);
    bt->query = GST_DEBUG_FUNCPTR(consultar);
    bt->sink_event = GST_DEBUG_FUNCPTR(eventoEntrada);
    bt->transform_meta = ba::sr::transformarMeta;
    bt->passthrough_on_same_caps = TRUE;
}

static void ba_coremlsr_init(BaCoreMlSr* s) {
    s->modelo = g_strdup("");
    s->escala = 2;
    s->activo = TRUE;
    s->procesa = FALSE;
    s->error = nullptr;
    s->listo = new std::atomic<void*>(nullptr);
    s->red = nullptr;
    s->cargando = nullptr;
    s->tamanoOk = FALSE;
    s->redW = s->redH = 0;
    s->transfer = nullptr;
    s->poolRed = s->poolSalida = nullptr;
    s->conMeta = s->sinMeta = 0;
    s->unidades = 0;
    s->unidadesUsadas = 0;
    s->cola = nullptr;
    s->pendientes = new std::deque<Trabajo*>();
    s->modo = new std::atomic<int>(ba::sr::kActivo);
    s->transicionMs = new std::atomic<int>(400);
    s->mezcla = ba::sr::Mezcla{};
    s->cuadros = 0;
    s->demora = new ba::sr::Demora();
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(s), TRUE);
}

extern "C" gboolean ba_registrar_coremlsr(GstPlugin* plugin) {
    return gst_element_register(plugin, "bacoremlsr", GST_RANK_NONE, BA_TYPE_COREMLSR);
}
