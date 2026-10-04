// ============================================================================
// src/gst-plugins/superres/coremlsr_mac.mm — superresolución con un modelo CoreML
//
// Proposito:
//   Elemento de GStreamer `bacoremlsr`: agranda con una red convertida a CoreML
//   (.mlmodelc), que corre ENTERA en el Neural Engine o la GPU de la Mac. La
//   misma red por ONNX Runtime + CoreML se partía en decenas de pedazos (CPU y
//   ANE alternados): Real-ESRGAN compacto pasó de 9 s a ~39 ms por cuadro.
//
//   El modelo lo arma el laboratorio (assets/superres/laboratorio.py,
//   `instalar`): entrada y salida como IMAGEN RGB (CoreML convierte el color en
//   el acelerador; acá entra y sale BGRA sin tocar los píxeles), con tamaños de
//   entrada fijos (los de las cámaras comunes: el Neural Engine no acepta
//   tamaños libres). `escala` es lo que agranda la red (2 o 4); una red x4
//   recibe la imagen a la mitad (lo arma basuperres) y sale x2 de la original.
//
//   Mientras el modelo se carga (en otro hilo: la primera vez el Neural Engine
//   lo prepara, unos segundos), o si el tamaño de la cámara no es uno de los del
//   modelo, la imagen sale agrandada común (vImage): nunca se corta.
//
// Modelo de threading:
//   `transform` corre en el hilo de streaming; la predicción es sincrónica.
//   El modelo se carga en un hilo aparte y se entrega por un atómico.
// ============================================================================

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "comun.hpp"

#import <Accelerate/Accelerate.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <cstring>
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
    gboolean procesa; ///< negociado: la salida es escala x la entrada
    gchar* error;
    std::atomic<void*>* listo; ///< MLModel* (retenido) que dejó el hilo de carga
    void* red;                 ///< MLModel* (retenido) en uso
    GThread* cargando;
    gboolean tamanoOk; ///< el tamaño de entrada está entre los del modelo
    ba::sr::Demora* demora;
};

G_DEFINE_TYPE(BaCoreMlSr, ba_coremlsr, GST_TYPE_BASE_TRANSFORM)

enum { PROP_0, PROP_MODELO, PROP_ESCALA, PROP_ACTIVO, PROP_MS, PROP_ERROR };

static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                              GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("BGRA")));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                             GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("BGRA")));

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
        conf.computeUnits = MLComputeUnitsAll;
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

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* s = BA_COREMLSR(bt);
    return ba::sr::transformarCaps(dir, caps, filtro, s->activo, nullptr, s->escala);
}

static GstCaps* fijarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* otras) {
    return ba::sr::fijarCapsPor(dir, caps, otras, BA_COREMLSR(bt)->escala);
}

static gboolean tamanoSalida(GstBaseTransform*, GstPadDirection, GstCaps*, gsize, GstCaps* otras, gsize* otro) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps(&vi, otras)) return FALSE;
    *otro = GST_VIDEO_INFO_SIZE(&vi);
    return TRUE;
}

static gboolean ponerCaps(GstBaseTransform* bt, GstCaps* incaps, GstCaps* outcaps) {
    auto* s = BA_COREMLSR(bt);
    if (!gst_video_info_from_caps(&s->in, incaps) || !gst_video_info_from_caps(&s->out, outcaps)) return FALSE;
    const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
    s->procesa = GST_VIDEO_INFO_WIDTH(&s->out) == w * s->escala && GST_VIDEO_INFO_HEIGHT(&s->out) == h * s->escala;
    gst_base_transform_set_passthrough(bt, !s->procesa);
    s->tamanoOk = FALSE;
    if (s->procesa && s->red == nullptr && s->cargando == nullptr && s->modelo != nullptr && *s->modelo != '\0')
        s->cargando = g_thread_new("bacoremlsr-carga", cargar, s);
    if (s->red != nullptr) s->tamanoOk = aceptaTamano((__bridge MLModel*)s->red, w, h);
    GST_INFO_OBJECT(s, "CoreML x%d %s (%dx%d)", s->escala, s->procesa ? "activo" : "apagado", w, h);
    return TRUE;
}

/// Agrandado común (mientras el modelo se carga, o si el tamaño no es del modelo).
static void agrandarComun(GstVideoFrame* fi, GstVideoFrame* fo) {
    vImage_Buffer a{GST_VIDEO_FRAME_PLANE_DATA(fi, 0), static_cast<vImagePixelCount>(GST_VIDEO_FRAME_HEIGHT(fi)),
                    static_cast<vImagePixelCount>(GST_VIDEO_FRAME_WIDTH(fi)),
                    static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(fi, 0))};
    vImage_Buffer b{GST_VIDEO_FRAME_PLANE_DATA(fo, 0), static_cast<vImagePixelCount>(GST_VIDEO_FRAME_HEIGHT(fo)),
                    static_cast<vImagePixelCount>(GST_VIDEO_FRAME_WIDTH(fo)),
                    static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(fo, 0))};
    vImageScale_ARGB8888(&a, &b, nullptr, kvImageHighQualityResampling);
}

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* inbuf, GstBuffer* outbuf) {
    auto* s = BA_COREMLSR(bt);
    if (!s->procesa) return GST_FLOW_OK;
    // ¿Terminó de cargar el modelo?
    if (s->red == nullptr) {
        if (void* p = s->listo->exchange(nullptr)) {
            s->red = p;
            s->tamanoOk = aceptaTamano((__bridge MLModel*)s->red, GST_VIDEO_INFO_WIDTH(&s->in), GST_VIDEO_INFO_HEIGHT(&s->in));
            if (!s->tamanoOk)
                ponerError(s, "el modelo no acepta " + std::to_string(GST_VIDEO_INFO_WIDTH(&s->in)) + "x" +
                                  std::to_string(GST_VIDEO_INFO_HEIGHT(&s->in)) +
                                  " (instalado para los tamaños de cámara comunes): sale agrandado común");
        }
    }
    GstVideoFrame fi, fo;
    if (!gst_video_frame_map(&fi, &s->in, inbuf, GST_MAP_READ)) return GST_FLOW_ERROR;
    if (!gst_video_frame_map(&fo, &s->out, outbuf, GST_MAP_WRITE)) {
        gst_video_frame_unmap(&fi);
        return GST_FLOW_ERROR;
    }
    const gint64 t0 = g_get_monotonic_time();
    bool hecho = false;
    if (s->red != nullptr && s->tamanoOk) {
        @autoreleasepool {
            const int w = GST_VIDEO_FRAME_WIDTH(&fi), h = GST_VIDEO_FRAME_HEIGHT(&fi);
            CVPixelBufferRef pb = nullptr;
            // El cuadro de GStreamer, sin copiar, como CVPixelBuffer BGRA.
            CVPixelBufferCreateWithBytes(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA, GST_VIDEO_FRAME_PLANE_DATA(&fi, 0),
                                         GST_VIDEO_FRAME_PLANE_STRIDE(&fi, 0), nullptr, nullptr, nullptr, &pb);
            if (pb != nullptr) {
                NSError* err = nil;
                MLFeatureValue* fv = [MLFeatureValue featureValueWithPixelBuffer:pb];
                MLDictionaryFeatureProvider* prov = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"entrada": fv}
                                                                                                    error:&err];
                id<MLFeatureProvider> res = prov ? [(__bridge MLModel*)s->red predictionFromFeatures:prov error:&err] : nil;
                CVPixelBufferRef r = res ? [res featureValueForName:@"salida"].imageBufferValue : nullptr;
                if (r != nullptr && static_cast<int>(CVPixelBufferGetWidth(r)) == GST_VIDEO_FRAME_WIDTH(&fo) &&
                    static_cast<int>(CVPixelBufferGetHeight(r)) == GST_VIDEO_FRAME_HEIGHT(&fo)) {
                    CVPixelBufferLockBaseAddress(r, kCVPixelBufferLock_ReadOnly);
                    const OSType fmt = CVPixelBufferGetPixelFormatType(r);
                    vImage_Buffer de{CVPixelBufferGetBaseAddress(r), CVPixelBufferGetHeight(r), CVPixelBufferGetWidth(r),
                                     CVPixelBufferGetBytesPerRow(r)};
                    vImage_Buffer a{GST_VIDEO_FRAME_PLANE_DATA(&fo, 0), static_cast<vImagePixelCount>(GST_VIDEO_FRAME_HEIGHT(&fo)),
                                    static_cast<vImagePixelCount>(GST_VIDEO_FRAME_WIDTH(&fo)),
                                    static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&fo, 0))};
                    if (fmt == kCVPixelFormatType_32BGRA) {
                        vImageCopyBuffer(&de, &a, 4, kvImageNoFlags);
                        hecho = true;
                    } else if (fmt == kCVPixelFormatType_32ARGB) {
                        const uint8_t mapa[4] = {3, 2, 1, 0}; // ARGB -> BGRA
                        vImagePermuteChannels_ARGB8888(&de, &a, mapa, kvImageNoFlags);
                        hecho = true;
                    } else {
                        ponerError(s, "el modelo devuelve un formato de imagen inesperado");
                    }
                    CVPixelBufferUnlockBaseAddress(r, kCVPixelBufferLock_ReadOnly);
                } else if (err != nil) {
                    ponerError(s, std::string("CoreML: ") + err.localizedDescription.UTF8String);
                }
                CVPixelBufferRelease(pb);
            }
        }
        if (hecho) {
            s->demora->anotar(static_cast<double>(g_get_monotonic_time() - t0) / 1000.0);
            ba::sr::quizasAvisarLatencia(bt, *s->demora);
        }
    }
    if (!hecho) agrandarComun(&fi, &fo);
    gst_video_frame_unmap(&fo);
    gst_video_frame_unmap(&fi);
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_COREMLSR(o);
    switch (id) {
        case PROP_MODELO:
            soltarRed(s);
            g_free(s->modelo);
            s->modelo = g_value_dup_string(v);
            ponerError(s, "");
            break;
        case PROP_ESCALA:
            s->escala = g_value_get_int(v);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
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
        case PROP_MS: g_value_set_double(v, s->demora->media()); break;
        case PROP_ERROR:
            GST_OBJECT_LOCK(s);
            g_value_set_string(v, s->error);
            GST_OBJECT_UNLOCK(s);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static gboolean parar(GstBaseTransform* bt) {
    BA_COREMLSR(bt)->demora->reiniciar();
    return TRUE;
}

static gboolean consultar(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    return ba::sr::consultaLatencia(bt, q, *BA_COREMLSR(bt)->demora, GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->query, dir);
}

static gboolean eventoEntrada(GstBaseTransform* bt, GstEvent* ev) {
    if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START)
        BA_COREMLSR(bt)->demora->reiniciar();
    return GST_BASE_TRANSFORM_CLASS(ba_coremlsr_parent_class)->sink_event(bt, ev);
}

static void ba_coremlsr_finalize(GObject* o) {
    auto* s = BA_COREMLSR(o);
    soltarRed(s);
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
    const auto listo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    const auto vivo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_MODELO, g_param_spec_string("modelo", "Modelo", "Ruta del .mlmodelc", "", listo));
    g_object_class_install_property(
        go, PROP_ESCALA, g_param_spec_int("escala", "Escala", "Lo que agranda la red (2 o 4)", 2, 4, 2, listo));
    g_object_class_install_property(go, PROP_ACTIVO, g_param_spec_boolean("activo", "Activo", "Agranda", TRUE, vivo));
    g_object_class_install_property(go, PROP_MS,
                                    g_param_spec_double("ms-por-cuadro", "ms por cuadro", "Lo que tarda en promedio", 0, 100000, 0, ro));
    g_object_class_install_property(go, PROP_ERROR, g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    gst_element_class_set_static_metadata(ge, "Superresolución (CoreML)", "Filter/Effect/Video/Scaler",
                                          "Agranda con una red convertida a CoreML, en el Neural Engine o la GPU de la Mac",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bt->transform_caps = transformarCaps;
    bt->fixate_caps = fijarCaps;
    bt->set_caps = ponerCaps;
    bt->transform_size = tamanoSalida;
    bt->transform = transformar;
    bt->stop = parar;
    bt->query = consultar;
    bt->sink_event = eventoEntrada;
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
    s->demora = new ba::sr::Demora();
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(s), TRUE);
}

extern "C" gboolean ba_registrar_coremlsr(GstPlugin* plugin) {
    return gst_element_register(plugin, "bacoremlsr", GST_RANK_NONE, BA_TYPE_COREMLSR);
}
