// ============================================================================
// src/gst-plugins/superres/vtsuperres_mac.mm — superresolución x2 con VideoToolbox
//
// Proposito:
//   Elemento de GStreamer `bavtsuperres`: duplica la resolución de un video
//   chico (una cámara PAL de 768x576 pasa a 1536x1152) con la superresolución
//   de baja latencia de Apple (VTLowLatencySuperResolutionScaler, macOS 26):
//   una red neuronal pensada para video en vivo (videollamadas), que corre en
//   la GPU/Neural Engine. Medido en una M1 Pro: ~2,3 ms por cuadro de 768x576.
//   No es «agrandar»: reconstruye bordes y saca los bloques de compresión.
//
//   Uno de los motores de `basuperres` (plugin.cpp); también se puede usar
//   suelto: ... ! videoconvert ! video/x-raw,format=NV12 ! bavtsuperres ! ...
//
//   Limites de Apple: los pregunta al sistema (VTLowLatencySuperResolutionScaler-
//   Configuration: en macOS 26, entre 96x96 y 960x960), factor 2, NV12 de rango
//   limitado (420v). Lo que no entra (una cámara HD) pasa SIN TOCAR (modo
//   passthrough), igual que si el sistema no lo soporta (macOS < 26).
//
// Modelo de threading:
//   `transform` corre en el hilo de streaming de GStreamer; el procesamiento
//   es sincrónico (processWithParameters:error:), con una sesión por tamaño.
// ============================================================================

#include <algorithm>

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "comun.hpp"

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <cstring>

namespace {

constexpr int kFactor = 2;

bool soportado() {
    if (@available(macOS 26.0, *)) return VTLowLatencySuperResolutionScalerConfiguration.isSupported;
    return false;
}

/// ¿Este tamaño de entrada se puede procesar?
bool entra(int w, int h) {
    if (!soportado()) return false;
    if (@available(macOS 26.0, *)) {
        const CMVideoDimensions mx = VTLowLatencySuperResolutionScalerConfiguration.maximumDimensions;
        const CMVideoDimensions mn = VTLowLatencySuperResolutionScalerConfiguration.minimumDimensions;
        if (w < mn.width || h < mn.height || w > mx.width || h > mx.height) return false;
        NSArray<NSNumber*>* f = [VTLowLatencySuperResolutionScalerConfiguration supportedScaleFactorsForFrameWidth:w frameHeight:h];
        for (NSNumber* n in f)
            if (n.floatValue == static_cast<float>(kFactor)) return true;
    }
    return false;
}

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_VTSUPERRES (ba_vtsuperres_get_type())
G_DECLARE_FINAL_TYPE(BaVtSuperRes, ba_vtsuperres, BA, VTSUPERRES, GstBaseTransform)
G_END_DECLS

struct _BaVtSuperRes {
    GstBaseTransform parent;
    GstVideoInfo in, out;
    gboolean activo;   ///< propiedad: false = pasa sin tocar
    gboolean procesa;  ///< lo negociado: tamaño soportado y activo
    void* procesador;  ///< VTFrameProcessor* (retenido)
    void* config;      ///< VTLowLatencySuperResolutionScalerConfiguration* (retenido)
    CVPixelBufferPoolRef poolIn, poolOut;
    guint64 cuadros;
    ba::sr::Demora* demora;
};

G_DEFINE_TYPE(BaVtSuperRes, ba_vtsuperres, GST_TYPE_BASE_TRANSFORM)

GST_DEBUG_CATEGORY_STATIC(ba_vtsuperres_debug);
#define GST_CAT_DEFAULT ba_vtsuperres_debug

enum { PROP_0, PROP_ACTIVO, PROP_MS, PROP_CUADROS_RED, PROP_MS_SUMA };

static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                              GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                             GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));

static void soltarSesion(BaVtSuperRes* s) {
    if (@available(macOS 26.0, *)) {
        if (s->procesador != nullptr) {
            VTFrameProcessor* p = (__bridge_transfer VTFrameProcessor*)s->procesador;
            [p endSession];
            s->procesador = nullptr;
        }
        if (s->config != nullptr) {
            (void)(__bridge_transfer id)s->config;
            s->config = nullptr;
        }
    }
    if (s->poolIn != nullptr) CVPixelBufferPoolRelease(s->poolIn), s->poolIn = nullptr;
    if (s->poolOut != nullptr) CVPixelBufferPoolRelease(s->poolOut), s->poolOut = nullptr;
}

static CVPixelBufferPoolRef crearPool(NSDictionary* attrs, int w, int h) {
    NSMutableDictionary* a = [attrs mutableCopy];
    a[(id)kCVPixelBufferWidthKey] = @(w);
    a[(id)kCVPixelBufferHeightKey] = @(h);
    if (a[(id)kCVPixelBufferIOSurfacePropertiesKey] == nil) a[(id)kCVPixelBufferIOSurfacePropertiesKey] = @{};
    CVPixelBufferPoolRef pool = nullptr;
    CVPixelBufferPoolCreate(nullptr, nullptr, (__bridge CFDictionaryRef)a, &pool);
    return pool;
}

static bool abrirSesion(BaVtSuperRes* s, int w, int h) {
    soltarSesion(s);
    if (@available(macOS 26.0, *)) {
        VTLowLatencySuperResolutionScalerConfiguration* c =
            [[VTLowLatencySuperResolutionScalerConfiguration alloc] initWithFrameWidth:w frameHeight:h scaleFactor:kFactor];
        if (c == nil) return false;
        VTFrameProcessor* p = [[VTFrameProcessor alloc] init];
        NSError* err = nil;
        if (![p startSessionWithConfiguration:c error:&err]) {
            GST_WARNING_OBJECT(s, "VideoToolbox no abrió la superresolución: %s", err.localizedDescription.UTF8String);
            return false;
        }
        s->poolIn = crearPool(c.sourcePixelBufferAttributes, w, h);
        s->poolOut = crearPool(c.destinationPixelBufferAttributes, w * kFactor, h * kFactor);
        if (s->poolIn == nullptr || s->poolOut == nullptr) {
            [p endSession];
            return false;
        }
        s->procesador = (__bridge_retained void*)p;
        s->config = (__bridge_retained void*)c;
        return true;
    }
    return false;
}

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    return ba::sr::transformarCaps(dir, caps, filtro, BA_VTSUPERRES(bt)->activo, [](int w, int h) { return entra(w, h); });
}

static gboolean tamanoSalida(GstBaseTransform*, GstPadDirection, GstCaps*, gsize, GstCaps* otras, gsize* otro) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps(&vi, otras)) return FALSE;
    *otro = GST_VIDEO_INFO_SIZE(&vi);
    return TRUE;
}

static gboolean ponerCaps(GstBaseTransform* bt, GstCaps* incaps, GstCaps* outcaps) {
    auto* s = BA_VTSUPERRES(bt);
    if (!gst_video_info_from_caps(&s->in, incaps) || !gst_video_info_from_caps(&s->out, outcaps)) return FALSE;
    const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
    s->procesa = GST_VIDEO_INFO_WIDTH(&s->out) == w * kFactor && GST_VIDEO_INFO_HEIGHT(&s->out) == h * kFactor;
    if (s->procesa && !abrirSesion(s, w, h)) return FALSE;
    if (!s->procesa) soltarSesion(s);
    gst_base_transform_set_passthrough(bt, !s->procesa);
    GST_INFO_OBJECT(s, "superresolución %s (%dx%d)", s->procesa ? "activa" : "apagada", w, h);
    return TRUE;
}

/// Copia un plano de NV12 de GStreamer a un CVPixelBuffer (o al revés).
static void copiarPlano(const guint8* de, int pasoDe, guint8* a, int pasoA, int bytesFila, int filas) {
    for (int y = 0; y < filas; ++y) std::memcpy(a + static_cast<size_t>(y) * pasoA, de + static_cast<size_t>(y) * pasoDe, bytesFila);
}

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* inbuf, GstBuffer* outbuf) {
    auto* s = BA_VTSUPERRES(bt);
    if (!s->procesa || s->procesador == nullptr) return GST_FLOW_OK;
    if (@available(macOS 26.0, *)) {
        @autoreleasepool {
            const gint64 t0 = g_get_monotonic_time();
            GstVideoFrame fi, fo;
            if (!gst_video_frame_map(&fi, &s->in, inbuf, GST_MAP_READ)) return GST_FLOW_ERROR;
            if (!gst_video_frame_map(&fo, &s->out, outbuf, GST_MAP_WRITE)) {
                gst_video_frame_unmap(&fi);
                return GST_FLOW_ERROR;
            }
            const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
            CVPixelBufferRef pi = nullptr, po = nullptr;
            CVPixelBufferPoolCreatePixelBuffer(nullptr, s->poolIn, &pi);
            CVPixelBufferPoolCreatePixelBuffer(nullptr, s->poolOut, &po);
            GstFlowReturn ret = GST_FLOW_OK;
            if (pi == nullptr || po == nullptr) {
                ret = GST_FLOW_ERROR;
            } else {
                CVPixelBufferLockBaseAddress(pi, 0);
                for (int p = 0; p < 2; ++p)
                    copiarPlano(static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fi, p)), GST_VIDEO_FRAME_PLANE_STRIDE(&fi, p),
                                static_cast<guint8*>(CVPixelBufferGetBaseAddressOfPlane(pi, p)),
                                static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(pi, p)), w, p == 0 ? h : h / 2);
                CVPixelBufferUnlockBaseAddress(pi, 0);
                const CMTime pts = GST_BUFFER_PTS_IS_VALID(inbuf) ? CMTimeMake(static_cast<int64_t>(GST_BUFFER_PTS(inbuf)), 1000000000)
                                                                  : CMTimeMake(static_cast<int64_t>(s->cuadros) *
                                                                                   std::max(GST_VIDEO_INFO_FPS_D(&s->in), 1),
                                                                               std::max(GST_VIDEO_INFO_FPS_N(&s->in), 1));
                VTFrameProcessorFrame* a = [[VTFrameProcessorFrame alloc] initWithBuffer:pi presentationTimeStamp:pts];
                VTFrameProcessorFrame* b = [[VTFrameProcessorFrame alloc] initWithBuffer:po presentationTimeStamp:pts];
                VTLowLatencySuperResolutionScalerParameters* par =
                    [[VTLowLatencySuperResolutionScalerParameters alloc] initWithSourceFrame:a destinationFrame:b];
                NSError* err = nil;
                VTFrameProcessor* proc = (__bridge VTFrameProcessor*)s->procesador;
                if (a == nil || b == nil || par == nil || ![proc processWithParameters:par error:&err]) {
                    // Un cuadro que falla sale agrandado sin más (no se corta la imagen).
                    GST_WARNING_OBJECT(s, "superresolución: %s", err ? err.localizedDescription.UTF8String : "no se pudo armar el cuadro");
                    for (int p = 0; p < 2; ++p) {
                        const int filas = p == 0 ? h * kFactor : h;
                        const guint8* de = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fi, p));
                        guint8* aa = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fo, p));
                        const int pd = GST_VIDEO_FRAME_PLANE_STRIDE(&fi, p), pa = GST_VIDEO_FRAME_PLANE_STRIDE(&fo, p);
                        for (int y = 0; y < filas; ++y) {
                            const guint8* fila = de + static_cast<size_t>(y / kFactor) * pd;
                            guint8* dst = aa + static_cast<size_t>(y) * pa;
                            if (p == 0)
                                for (int x = 0; x < w * kFactor; ++x) dst[x] = fila[x / kFactor];
                            else
                                for (int x = 0; x < w; x += 2) {
                                    dst[x * 2] = dst[x * 2 + 2] = fila[x];
                                    dst[x * 2 + 1] = dst[x * 2 + 3] = fila[x + 1];
                                }
                        }
                    }
                } else {
                    CVPixelBufferLockBaseAddress(po, kCVPixelBufferLock_ReadOnly);
                    for (int p = 0; p < 2; ++p)
                        copiarPlano(static_cast<const guint8*>(CVPixelBufferGetBaseAddressOfPlane(po, p)),
                                    static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(po, p)),
                                    static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fo, p)), GST_VIDEO_FRAME_PLANE_STRIDE(&fo, p),
                                    w * kFactor, p == 0 ? h * kFactor : h);
                    CVPixelBufferUnlockBaseAddress(po, kCVPixelBufferLock_ReadOnly);
                }
            }
            if (pi != nullptr) CVPixelBufferRelease(pi);
            if (po != nullptr) CVPixelBufferRelease(po);
            gst_video_frame_unmap(&fo);
            gst_video_frame_unmap(&fi);
            ++s->cuadros;
            s->demora->anotar(static_cast<double>(g_get_monotonic_time() - t0) / 1000.0);
            ba::sr::quizasAvisarLatencia(bt, *s->demora);
            return ret;
        }
    }
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_VTSUPERRES(o);
    if (id == PROP_ACTIVO) {
        s->activo = g_value_get_boolean(v);
        gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
    } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_VTSUPERRES(o);
    if (id == PROP_ACTIVO) g_value_set_boolean(v, s->activo);
    else if (id == PROP_MS) g_value_set_double(v, s->demora->media());
    else if (id == PROP_CUADROS_RED) g_value_set_uint64(v, s->demora->totales().first);
    else if (id == PROP_MS_SUMA) g_value_set_double(v, s->demora->totales().second);
    else G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
}

static gboolean parar(GstBaseTransform* bt) {
    soltarSesion(BA_VTSUPERRES(bt));
    BA_VTSUPERRES(bt)->demora->reiniciar();
    return TRUE;
}

static gboolean consultar(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    return ba::sr::consultaLatencia(bt, q, *BA_VTSUPERRES(bt)->demora, GST_BASE_TRANSFORM_CLASS(ba_vtsuperres_parent_class)->query, dir);
}

static gboolean eventoEntrada(GstBaseTransform* bt, GstEvent* ev) {
    if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(ev) == GST_EVENT_STREAM_START) BA_VTSUPERRES(bt)->demora->reiniciar();
    return GST_BASE_TRANSFORM_CLASS(ba_vtsuperres_parent_class)->sink_event(bt, ev);
}

static void ba_vtsuperres_finalize(GObject* o) {
    soltarSesion(BA_VTSUPERRES(o));
    delete BA_VTSUPERRES(o)->demora;
    G_OBJECT_CLASS(ba_vtsuperres_parent_class)->finalize(o);
}

static void ba_vtsuperres_class_init(BaVtSuperResClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_vtsuperres_debug, "bavtsuperres", 0, "Superresolución VideoToolbox");
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    GstBaseTransformClass* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_vtsuperres_finalize;
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo", "Duplica la resolución (si el tamaño entra)", TRUE,
                                                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(go, PROP_MS,
                                    g_param_spec_double("ms-por-cuadro", "ms por cuadro", "Lo que tarda en promedio", 0, 1000, 0,
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(go, PROP_CUADROS_RED,
                                    g_param_spec_uint64("cuadros-red", "Cuadros por la red",
                                                        "Cuántos cuadros pasaron por la red (para medir una ventana)", 0,
                                                        G_MAXUINT64, 0, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(go, PROP_MS_SUMA,
                                    g_param_spec_double("ms-suma", "ms en total",
                                                        "Lo que tardó la red, sumado (con cuadros-red: el promedio de una ventana)",
                                                        0, G_MAXDOUBLE, 0, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(ge, "Superresolución x2 (VideoToolbox)", "Filter/Effect/Video/Scaler",
                                          "Duplica la resolución de un video chico con la superresolución de baja latencia de Apple",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bt->transform_caps = GST_DEBUG_FUNCPTR(transformarCaps);
    bt->set_caps = GST_DEBUG_FUNCPTR(ponerCaps);
    bt->transform_size = GST_DEBUG_FUNCPTR(tamanoSalida);
    bt->transform = GST_DEBUG_FUNCPTR(transformar);
    bt->stop = GST_DEBUG_FUNCPTR(parar);
    bt->query = GST_DEBUG_FUNCPTR(consultar);
    bt->sink_event = GST_DEBUG_FUNCPTR(eventoEntrada);
    bt->fixate_caps = GST_DEBUG_FUNCPTR(ba::sr::fijarCaps);
    bt->transform_meta = GST_DEBUG_FUNCPTR(ba::sr::transformarMeta);
    bt->passthrough_on_same_caps = GST_DEBUG_FUNCPTR(TRUE);
}

static void ba_vtsuperres_init(BaVtSuperRes* s) {
    s->activo = TRUE;
    s->procesa = FALSE;
    s->procesador = nullptr;
    s->config = nullptr;
    s->poolIn = s->poolOut = nullptr;
    s->cuadros = 0;
    s->demora = new ba::sr::Demora();
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(s), TRUE);
}

/// Registra `bavtsuperres` en el plugin. Sin macOS 26 no registra nada.
extern "C" gboolean ba_registrar_vtsuperres(GstPlugin* plugin) {
    if (!soportado()) return TRUE;
    return gst_element_register(plugin, "bavtsuperres", GST_RANK_NONE, BA_TYPE_VTSUPERRES);
}
extern "C" gboolean ba_vtsuperres_soportado() { return soportado() ? TRUE : FALSE; }
