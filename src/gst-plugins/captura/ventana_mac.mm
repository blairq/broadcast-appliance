// ============================================================================
// src/gst-plugins/captura/ventana_mac.mm — bacapturaventana: la ventana de una
// aplicación en la Mac (ScreenCaptureKit), como fuente de GStreamer
//
// avfvideosrc captura pantallas enteras; una ventana sola (aunque esté tapada o
// en otro escritorio) solo la da ScreenCaptureKit (macOS 12.3+). Es la fuente
// de «Captura de una aplicación» del motor (Capturas_mac.mm).
//
//   ventana   el CGWindowID (kCGWindowNumber de CGWindowListCopyWindowInfo)
//   cursor    dibujar el puntero
//   fps       cuadros por segundo como mucho (lo que entrega SCStream)
//
// Sale BGRA en memoria de la máquina, en vivo, con el tamaño de la ventana en
// píxeles (en una pantalla Retina, el doble de sus puntos) al arrancar: si la
// ventana cambia de tamaño, ScreenCaptureKit la acomoda en ese cuadro. Pide el
// permiso de grabación de pantalla (el mismo que la captura de pantalla); si la
// ventana se cierra, la fuente termina con un error y el motor la vuelve a
// buscar.
//
//   gst-launch-1.0 bacapturaventana ventana=1234 ! videoconvert ! autovideosink
// ============================================================================

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

GST_DEBUG_CATEGORY_STATIC(ba_ventana_debug);
#define GST_CAT_DEFAULT ba_ventana_debug

struct Compartido; // lo que comparten el hilo de ScreenCaptureKit y el de GStreamer

API_AVAILABLE(macos(12.3))
@interface BaSalidaVentana : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, assign) Compartido* compartido;
@end

struct Compartido {
    std::mutex mu;
    std::condition_variable cv;
    GstBuffer* ultimo = nullptr; ///< el cuadro más nuevo, sin entregar
    int ancho = 0, alto = 0;
    bool detenido = false, cortado = false;
    std::string error;
};

G_BEGIN_DECLS
#define BA_TYPE_CAPTURA_VENTANA (ba_captura_ventana_get_type())
G_DECLARE_FINAL_TYPE(BaCapturaVentana, ba_captura_ventana, BA, CAPTURA_VENTANA, GstPushSrc)
G_END_DECLS

struct _BaCapturaVentana {
    GstPushSrc parent;
    guint ventana;
    gboolean cursor;
    gint fps;
    Compartido* c;
    void* stream; ///< SCStream (retenido a mano)
    void* salida; ///< BaSalidaVentana
    gboolean conCaps;
};

G_DEFINE_TYPE(BaCapturaVentana, ba_captura_ventana, GST_TYPE_PUSH_SRC)

enum { P_0, P_VENTANA, P_CURSOR, P_FPS };

@implementation BaSalidaVentana
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)muestra ofType:(SCStreamOutputType)tipo
    API_AVAILABLE(macos(12.3)) {
    if (tipo != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(muestra)) return;
    // Solo los cuadros completos (los otros avisan que no cambió nada).
    CFArrayRef adjuntos = CMSampleBufferGetSampleAttachmentsArray(muestra, false);
    if (adjuntos != nullptr && CFArrayGetCount(adjuntos) > 0) {
        auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(adjuntos, 0));
        auto estado = static_cast<CFNumberRef>(CFDictionaryGetValue(d, (__bridge CFStringRef)SCStreamFrameInfoStatus));
        int s = 0;
        if (estado != nullptr) CFNumberGetValue(estado, kCFNumberIntType, &s);
        if (s != SCFrameStatusComplete) return;
    }
    CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(muestra);
    if (pb == nullptr || self.compartido == nullptr) return;
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    const int w = static_cast<int>(CVPixelBufferGetWidth(pb)), h = static_cast<int>(CVPixelBufferGetHeight(pb));
    const size_t stride = CVPixelBufferGetBytesPerRow(pb);
    const auto* src = static_cast<const guint8*>(CVPixelBufferGetBaseAddress(pb));
    GstBuffer* b = gst_buffer_new_allocate(nullptr, static_cast<gsize>(w) * h * 4, nullptr);
    GstMapInfo m;
    if (src != nullptr && gst_buffer_map(b, &m, GST_MAP_WRITE)) {
        for (int y = 0; y < h; ++y) std::memcpy(m.data + static_cast<size_t>(y) * w * 4, src + y * stride, static_cast<size_t>(w) * 4);
        gst_buffer_unmap(b, &m);
    }
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    Compartido& c = *self.compartido;
    {
        std::lock_guard<std::mutex> l(c.mu);
        if (c.ultimo != nullptr) gst_buffer_unref(c.ultimo); // el de antes no llegó a salir: se tira
        c.ultimo = b;
        c.ancho = w;
        c.alto = h;
    }
    c.cv.notify_all();
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error API_AVAILABLE(macos(12.3)) {
    if (self.compartido == nullptr) return;
    Compartido& c = *self.compartido;
    {
        std::lock_guard<std::mutex> l(c.mu);
        c.cortado = true;
        c.error = error != nil ? std::string(error.localizedDescription.UTF8String ?: "") : "se cortó";
    }
    c.cv.notify_all();
}
@end

namespace {

void poner(GObject* o, guint id, const GValue* v, GParamSpec*) {
    auto* s = BA_CAPTURA_VENTANA(o);
    switch (id) {
        case P_VENTANA: s->ventana = g_value_get_uint(v); break;
        case P_CURSOR: s->cursor = g_value_get_boolean(v); break;
        case P_FPS: s->fps = g_value_get_int(v); break;
        default: break;
    }
}
void leer(GObject* o, guint id, GValue* v, GParamSpec*) {
    auto* s = BA_CAPTURA_VENTANA(o);
    switch (id) {
        case P_VENTANA: g_value_set_uint(v, s->ventana); break;
        case P_CURSOR: g_value_set_boolean(v, s->cursor); break;
        case P_FPS: g_value_set_int(v, s->fps); break;
        default: break;
    }
}

gboolean empezar(GstBaseSrc* bs) {
    auto* s = BA_CAPTURA_VENTANA(bs);
    if (@available(macOS 12.3, *)) {
        s->c = new Compartido();
        s->conCaps = FALSE;
        // La ventana entre las que se pueden compartir (pide el permiso la primera vez).
        __block SCShareableContent* contenido = nil;
        __block NSError* err = nil;
        dispatch_semaphore_t listo = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                   onScreenWindowsOnly:NO
                                                     completionHandler:^(SCShareableContent* c, NSError* e) {
                                                       contenido = c;
                                                       err = e;
                                                       dispatch_semaphore_signal(listo);
                                                     }];
        dispatch_semaphore_wait(listo, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        if (contenido == nil) {
            GST_ELEMENT_ERROR(s, RESOURCE, OPEN_READ, ("no se pudo ver las ventanas (¿falta el permiso de grabación de pantalla?)"),
                              ("%s", err != nil ? err.localizedDescription.UTF8String : "sin respuesta"));
            return FALSE;
        }
        SCWindow* ventana = nil;
        for (SCWindow* w in contenido.windows)
            if (w.windowID == s->ventana) ventana = w;
        if (ventana == nil) {
            GST_ELEMENT_ERROR(s, RESOURCE, NOT_FOUND, ("la ventana %u no está abierta", s->ventana), (nullptr));
            return FALSE;
        }
        SCContentFilter* filtro = [[SCContentFilter alloc] initWithDesktopIndependentWindow:ventana];
        SCStreamConfiguration* conf = [[SCStreamConfiguration alloc] init];
        // En píxeles: los puntos de la ventana por la escala de la pantalla donde está.
        CGFloat escala = 2;
        if (@available(macOS 14.0, *)) escala = filtro.pointPixelScale;
        conf.width = std::max<size_t>(2, static_cast<size_t>(ventana.frame.size.width * escala) & ~static_cast<size_t>(1));
        conf.height = std::max<size_t>(2, static_cast<size_t>(ventana.frame.size.height * escala) & ~static_cast<size_t>(1));
        conf.pixelFormat = kCVPixelFormatType_32BGRA;
        conf.minimumFrameInterval = CMTimeMake(1, std::max(1, s->fps));
        conf.showsCursor = s->cursor;
        conf.queueDepth = 4;
        BaSalidaVentana* salida = [[BaSalidaVentana alloc] init];
        salida.compartido = s->c;
        SCStream* stream = [[SCStream alloc] initWithFilter:filtro configuration:conf delegate:salida];
        NSError* e2 = nil;
        dispatch_queue_t cola = dispatch_queue_create("ba.captura.ventana", DISPATCH_QUEUE_SERIAL);
        if (![stream addStreamOutput:salida type:SCStreamOutputTypeScreen sampleHandlerQueue:cola error:&e2]) {
            GST_ELEMENT_ERROR(s, RESOURCE, FAILED, ("no se pudo capturar la ventana"), ("%s", e2.localizedDescription.UTF8String));
            return FALSE;
        }
        __block NSError* e3 = nil;
        dispatch_semaphore_t arranco = dispatch_semaphore_create(0);
        [stream startCaptureWithCompletionHandler:^(NSError* e) {
          e3 = e;
          dispatch_semaphore_signal(arranco);
        }];
        dispatch_semaphore_wait(arranco, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        if (e3 != nil) {
            GST_ELEMENT_ERROR(s, RESOURCE, FAILED, ("no arrancó la captura de la ventana"), ("%s", e3.localizedDescription.UTF8String));
            return FALSE;
        }
        s->stream = (__bridge_retained void*)stream;
        s->salida = (__bridge_retained void*)salida;
        return TRUE;
    }
    GST_ELEMENT_ERROR(s, RESOURCE, FAILED, ("capturar una ventana necesita macOS 12.3 o más nuevo"), (nullptr));
    return FALSE;
}

gboolean parar(GstBaseSrc* bs) {
    auto* s = BA_CAPTURA_VENTANA(bs);
    if (@available(macOS 12.3, *)) {
        if (s->stream != nullptr) {
            SCStream* stream = (__bridge_transfer SCStream*)s->stream;
            s->stream = nullptr;
            dispatch_semaphore_t paro = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError*) { dispatch_semaphore_signal(paro); }];
            dispatch_semaphore_wait(paro, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        }
        if (s->salida != nullptr) {
            BaSalidaVentana* salida = (__bridge_transfer BaSalidaVentana*)s->salida;
            salida.compartido = nullptr;
            s->salida = nullptr;
        }
    }
    if (s->c != nullptr) {
        if (s->c->ultimo != nullptr) gst_buffer_unref(s->c->ultimo);
        delete s->c;
        s->c = nullptr;
    }
    return TRUE;
}

gboolean desbloquear(GstBaseSrc* bs) {
    auto* s = BA_CAPTURA_VENTANA(bs);
    if (s->c != nullptr) {
        std::lock_guard<std::mutex> l(s->c->mu);
        s->c->detenido = true;
    }
    if (s->c != nullptr) s->c->cv.notify_all();
    return TRUE;
}

gboolean desbloquearFin(GstBaseSrc* bs) {
    auto* s = BA_CAPTURA_VENTANA(bs);
    if (s->c != nullptr) {
        std::lock_guard<std::mutex> l(s->c->mu);
        s->c->detenido = false;
    }
    return TRUE;
}

GstFlowReturn crear(GstPushSrc* ps, GstBuffer** out) {
    auto* s = BA_CAPTURA_VENTANA(ps);
    Compartido& c = *s->c;
    GstBuffer* b = nullptr;
    int w = 0, h = 0;
    {
        std::unique_lock<std::mutex> l(c.mu);
        c.cv.wait(l, [&]() { return c.ultimo != nullptr || c.detenido || c.cortado; });
        if (c.detenido) return GST_FLOW_FLUSHING;
        if (c.cortado && c.ultimo == nullptr) {
            GST_ELEMENT_ERROR(s, RESOURCE, READ, ("se cortó la captura de la ventana (¿se cerró?)"), ("%s", c.error.c_str()));
            return GST_FLOW_ERROR;
        }
        b = c.ultimo;
        c.ultimo = nullptr;
        w = c.ancho;
        h = c.alto;
    }
    if (!s->conCaps) {
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", "width", G_TYPE_INT, w, "height",
                                            G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION, 0, 1, "pixel-aspect-ratio",
                                            GST_TYPE_FRACTION, 1, 1, nullptr);
        const gboolean ok = gst_base_src_set_caps(GST_BASE_SRC(s), caps);
        gst_caps_unref(caps);
        if (!ok) {
            gst_buffer_unref(b);
            return GST_FLOW_NOT_NEGOTIATED;
        }
        s->conCaps = TRUE;
    }
    *out = b;
    return GST_FLOW_OK;
}

void finalizar(GObject* o) {
    parar(GST_BASE_SRC(o));
    G_OBJECT_CLASS(ba_captura_ventana_parent_class)->finalize(o);
}

} // namespace

static void ba_captura_ventana_init(BaCapturaVentana* s) {
    s->ventana = 0;
    s->cursor = TRUE;
    s->fps = 30;
    s->c = nullptr;
    s->stream = nullptr;
    s->salida = nullptr;
    s->conCaps = FALSE;
    gst_base_src_set_live(GST_BASE_SRC(s), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(s), GST_FORMAT_TIME);
    gst_base_src_set_do_timestamp(GST_BASE_SRC(s), TRUE);
}

static void ba_captura_ventana_class_init(BaCapturaVentanaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = poner;
    go->get_property = leer;
    go->finalize = finalizar;
    const auto flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, P_VENTANA, g_param_spec_uint("ventana", "ventana", "CGWindowID de la ventana", 0, G_MAXUINT, 0, flags));
    g_object_class_install_property(go, P_CURSOR, g_param_spec_boolean("cursor", "cursor", "Dibujar el puntero", TRUE, flags));
    g_object_class_install_property(go, P_FPS, g_param_spec_int("fps", "fps", "Cuadros por segundo como mucho", 1, 120, 30, flags));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Captura de una ventana", "Source/Video",
                                          "La ventana de una aplicación (ScreenCaptureKit)", "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw, format=BGRA, width=[1,16384], height=[1,16384], framerate=[0/1,120/1]");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    auto* bs = GST_BASE_SRC_CLASS(k);
    bs->start = empezar;
    bs->stop = parar;
    bs->unlock = desbloquear;
    bs->unlock_stop = desbloquearFin;
    GST_PUSH_SRC_CLASS(k)->create = crear;
}

static gboolean plugin_init(GstPlugin* p) {
    GST_DEBUG_CATEGORY_INIT(ba_ventana_debug, "bacapturaventana", 0, "captura de una ventana");
    return gst_element_register(p, "bacapturaventana", GST_RANK_NONE, BA_TYPE_CAPTURA_VENTANA);
}

#define PACKAGE "bacaptura"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, bacaptura, "Captura de la ventana de una aplicación (Mac)", plugin_init, "1.0",
                  "GPL", "Broadcast Appliance", "Broadcast Appliance")
