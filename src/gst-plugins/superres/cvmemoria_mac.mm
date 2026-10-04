// ============================================================================
// src/gst-plugins/superres/cvmemoria_mac.mm — ver cvmemoria_mac.h
// ============================================================================

#include "cvmemoria_mac.h"

#import <Foundation/Foundation.h>

#include <atomic>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC(ba_cv_debug);
#define GST_CAT_DEFAULT ba_cv_debug

// --- El CVPixelBuffer compartido entre los planos, con su estado de bloqueo --------

namespace {

enum class Bloqueo { Libre, Lectura, Escritura };

struct Pixbuf {
    std::atomic<int> refs{1};
    std::mutex mu;
    CVPixelBufferRef pb = nullptr;
    Bloqueo estado = Bloqueo::Libre;
    int veces = 0; ///< cuántos mapeos abiertos

    explicit Pixbuf(CVPixelBufferRef p) : pb(CVPixelBufferRetain(p)) {}
    ~Pixbuf() {
        if (estado != Bloqueo::Libre) {
            GST_ERROR("CVPixelBuffer %p todavía bloqueado (%d mapeos sin cerrar)", pb, veces);
            CVPixelBufferUnlockBaseAddress(pb, estado == Bloqueo::Lectura ? kCVPixelBufferLock_ReadOnly : 0);
        }
        CVPixelBufferRelease(pb);
    }
    Pixbuf* ref() {
        ++refs;
        return this;
    }
    void unref() {
        if (--refs == 0) delete this;
    }

    /// El primer mapeo decide el modo; uno de escritura sobre uno de lectura falla.
    bool bloquear(GstMapFlags flags) {
        std::lock_guard<std::mutex> l(mu);
        const bool escribe = (flags & GST_MAP_WRITE) != 0;
        if (estado == Bloqueo::Libre) {
            if (CVPixelBufferLockBaseAddress(pb, escribe ? 0 : kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
                GST_ERROR("no se pudo bloquear el CVPixelBuffer %p", pb);
                return false;
            }
            estado = escribe ? Bloqueo::Escritura : Bloqueo::Lectura;
        } else if (estado == Bloqueo::Lectura && escribe) {
            GST_ERROR("CVPixelBuffer %p bloqueado de solo lectura: no se puede escribir", pb);
            return false;
        }
        ++veces;
        return true;
    }
    void desbloquear() {
        std::lock_guard<std::mutex> l(mu);
        if (estado == Bloqueo::Libre || --veces > 0) return;
        CVPixelBufferUnlockBaseAddress(pb, estado == Bloqueo::Lectura ? kCVPixelBufferLock_ReadOnly : 0);
        estado = Bloqueo::Libre;
    }
};

struct Memoria {
    GstMemory mem;
    Pixbuf* pixbuf;
    size_t plano;
};

} // namespace

// --- El allocator ---------------------------------------------------------------------

G_BEGIN_DECLS
#define BA_TYPE_CV_ALLOCATOR (ba_cv_allocator_get_type())
G_DECLARE_FINAL_TYPE(BaCvAllocator, ba_cv_allocator, BA, CV_ALLOCATOR, GstAllocator)
G_END_DECLS

struct _BaCvAllocator {
    GstAllocator parent;
};
G_DEFINE_TYPE(BaCvAllocator, ba_cv_allocator, GST_TYPE_ALLOCATOR)

static GstAllocator* g_allocator = nullptr;

static Memoria* memoriaNueva(GstMemory* padre, Pixbuf* p, size_t plano, gsize maxsize, gsize offset, gsize size) {
    auto* m = g_slice_new0(Memoria);
    gst_memory_init(GST_MEMORY_CAST(m), static_cast<GstMemoryFlags>(padre != nullptr ? GST_MINI_OBJECT_FLAGS(padre) : 0),
                    g_allocator, padre, maxsize, 0, offset, size);
    m->pixbuf = p->ref();
    m->plano = plano;
    return m;
}

static gpointer mapear(GstMemory* gm, gsize, GstMapFlags flags) {
    auto* m = reinterpret_cast<Memoria*>(gm);
    if (!m->pixbuf->bloquear(flags)) return nullptr;
    CVPixelBufferRef pb = m->pixbuf->pb;
    return CVPixelBufferIsPlanar(pb) ? CVPixelBufferGetBaseAddressOfPlane(pb, m->plano) : CVPixelBufferGetBaseAddress(pb);
}

static void desmapear(GstMemory* gm) { reinterpret_cast<Memoria*>(gm)->pixbuf->desbloquear(); }

static GstMemory* compartir(GstMemory* gm, gssize offset, gssize size) {
    auto* m = reinterpret_cast<Memoria*>(gm);
    GstMemory* padre = gm->parent != nullptr ? gm->parent : gm;
    if (size == -1) size = static_cast<gssize>(gm->size) - offset;
    auto* sub = memoriaNueva(padre, m->pixbuf, m->plano, gm->maxsize, gm->offset + offset, static_cast<gsize>(size));
    GST_MINI_OBJECT_FLAG_SET(sub, GST_MEMORY_FLAG_READONLY);
    return GST_MEMORY_CAST(sub);
}

static gboolean esTramo(GstMemory* a, GstMemory* b, gsize*) {
    // Planos distintos de la misma IOSurface no son contiguos en general.
    (void)a;
    (void)b;
    return FALSE;
}

static GstMemory* asignar(GstAllocator*, gsize, GstAllocationParams*) {
    return nullptr; // solo envuelve CVPixelBuffers (los crea el pool)
}

static void liberar(GstAllocator*, GstMemory* gm) {
    auto* m = reinterpret_cast<Memoria*>(gm);
    m->pixbuf->unref();
    g_slice_free(Memoria, m);
}

static void ba_cv_allocator_class_init(BaCvAllocatorClass* k) {
    auto* a = GST_ALLOCATOR_CLASS(k);
    a->alloc = asignar;
    a->free = liberar;
}

static void ba_cv_allocator_init(BaCvAllocator* a) {
    auto* al = GST_ALLOCATOR_CAST(a);
    al->mem_type = "BaCoreVideoMemory";
    al->mem_map = mapear;
    al->mem_unmap = desmapear;
    al->mem_share = compartir;
    al->mem_is_span = esTramo;
    GST_OBJECT_FLAG_SET(a, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void iniciar() {
    static gsize listo = 0;
    if (g_once_init_enter(&listo)) {
        GST_DEBUG_CATEGORY_INIT(ba_cv_debug, "bacvmemoria", 0, "IOSurfaces como memoria de GStreamer");
        g_allocator = GST_ALLOCATOR(g_object_new(BA_TYPE_CV_ALLOCATOR, nullptr));
        gst_object_ref_sink(g_allocator);
        gst_allocator_register("BaCoreVideoMemory", GST_ALLOCATOR(gst_object_ref(g_allocator)));
        g_once_init_leave(&listo, 1);
    }
}

// --- GstCoreVideoMeta (el de applemedia) ----------------------------------------------

/// Marca un metadato (en C++ las macros de GStreamer no compilan con enums).
static void marcar(GstMeta* m, int f) { m->flags = static_cast<GstMetaFlags>(m->flags | f); }

namespace {
struct MetaCoreVideo { // la forma de GstCoreVideoMeta (sys/applemedia/corevideobuffer.h)
    GstMeta meta;
    CVBufferRef cvbuf;
    CVPixelBufferRef pixbuf;
};
} // namespace

/// La info registrada por applemedia. Se registra cuando applemedia crea su
/// primer buffer (vtdec decodificando): hasta entonces no hay, y NO se
/// registra acá con el mismo nombre (rompería la de applemedia). Se vuelve a
/// buscar (una tabla) hasta que aparece: con vtdec arriba, aparece enseguida.
static const GstMetaInfo* infoMetaCoreVideo() {
    static std::atomic<const GstMetaInfo*> info{nullptr};
    const GstMetaInfo* i = info.load();
    if (i == nullptr) {
        i = gst_meta_get_info("GstCoreVideoMeta");
        if (i != nullptr) info.store(i);
    }
    return i;
}

// --- Buffers ---------------------------------------------------------------------------

static GstVideoFormat formatoGst(OSType f) {
    switch (f) {
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange: return GST_VIDEO_FORMAT_NV12;
        case kCVPixelFormatType_422YpCbCr8_yuvs: return GST_VIDEO_FORMAT_YUY2;
        case kCVPixelFormatType_422YpCbCr8: return GST_VIDEO_FORMAT_UYVY;
        case kCVPixelFormatType_32BGRA: return GST_VIDEO_FORMAT_BGRA;
        case kCVPixelFormatType_32RGBA: return GST_VIDEO_FORMAT_RGBA;
        default: return GST_VIDEO_FORMAT_UNKNOWN;
    }
}

OSType ba_cv_formato(const GstVideoInfo* info) {
    const bool completo = GST_VIDEO_INFO_COLORIMETRY(info).range == GST_VIDEO_COLOR_RANGE_0_255;
    switch (GST_VIDEO_INFO_FORMAT(info)) {
        case GST_VIDEO_FORMAT_NV12:
            return completo ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                            : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        case GST_VIDEO_FORMAT_YUY2: return kCVPixelFormatType_422YpCbCr8_yuvs;
        case GST_VIDEO_FORMAT_UYVY: return kCVPixelFormatType_422YpCbCr8;
        case GST_VIDEO_FORMAT_BGRA: return kCVPixelFormatType_32BGRA;
        case GST_VIDEO_FORMAT_RGBA: return kCVPixelFormatType_32RGBA;
        default: return 0;
    }
}

static void agregarMeta(GstBuffer* b, CVPixelBufferRef pb) {
    const GstMetaInfo* info = infoMetaCoreVideo();
    if (info == nullptr) return;
    auto* m = reinterpret_cast<MetaCoreVideo*>(gst_buffer_add_meta(b, info, nullptr));
    if (m == nullptr) return;
    m->cvbuf = CVBufferRetain(pb); // lo suelta el free del metadato (applemedia)
    m->pixbuf = pb;
    // Del pool: el metadato queda con el buffer al volver (si no, se borraría al reciclarlo).
    marcar(&m->meta, GST_META_FLAG_POOLED | GST_META_FLAG_LOCKED);
}

GstBuffer* ba_cv_buffer_nuevo(CVPixelBufferRef pb) {
    iniciar();
    GstBuffer* b = gst_buffer_new();
    auto* p = new Pixbuf(pb);
    gsize offset[GST_VIDEO_MAX_PLANES] = {0};
    gint stride[GST_VIDEO_MAX_PLANES] = {0};
    guint planos = 1;
    gsize acumulado = 0;
    if (CVPixelBufferIsPlanar(pb)) {
        planos = static_cast<guint>(CVPixelBufferGetPlaneCount(pb));
        for (guint i = 0; i < planos; ++i) {
            stride[i] = static_cast<gint>(CVPixelBufferGetBytesPerRowOfPlane(pb, i));
            const gsize tam = static_cast<gsize>(stride[i]) * CVPixelBufferGetHeightOfPlane(pb, i);
            offset[i] = acumulado;
            acumulado += tam;
            gst_buffer_append_memory(b, GST_MEMORY_CAST(memoriaNueva(nullptr, p, i, tam, 0, tam)));
        }
    } else {
        stride[0] = static_cast<gint>(CVPixelBufferGetBytesPerRow(pb));
        const gsize tam = static_cast<gsize>(stride[0]) * CVPixelBufferGetHeight(pb);
        gst_buffer_append_memory(b, GST_MEMORY_CAST(memoriaNueva(nullptr, p, 0, tam, 0, tam)));
    }
    p->unref(); // lo retienen las memorias
    const GstVideoFormat f = formatoGst(CVPixelBufferGetPixelFormatType(pb));
    if (f != GST_VIDEO_FORMAT_UNKNOWN) {
        auto* vm = gst_buffer_add_video_meta_full(b, GST_VIDEO_FRAME_FLAG_NONE, f,
                                                  static_cast<guint>(CVPixelBufferGetWidth(pb)),
                                                  static_cast<guint>(CVPixelBufferGetHeight(pb)), planos, offset, stride);
        if (vm != nullptr) {
            marcar(&vm->meta, GST_META_FLAG_POOLED | GST_META_FLAG_LOCKED);
        }
    }
    agregarMeta(b, pb);
    return b;
}

CVPixelBufferRef ba_cv_pixbuf_de(GstBuffer* b) {
    if (gst_buffer_n_memory(b) > 0) {
        GstMemory* m = gst_buffer_peek_memory(b, 0);
        if (g_allocator != nullptr && m->allocator == g_allocator) return reinterpret_cast<Memoria*>(m)->pixbuf->pb;
    }
    static GType api = 0;
    if (api == 0) api = g_type_from_name("GstCoreVideoMetaAPI");
    if (api == 0) return nullptr;
    auto* m = reinterpret_cast<MetaCoreVideo*>(gst_buffer_get_meta(b, api));
    return m != nullptr ? m->pixbuf : nullptr;
}

// --- El pool ----------------------------------------------------------------------------

G_BEGIN_DECLS
#define BA_TYPE_CV_POOL (ba_cv_pool_get_type())
G_DECLARE_FINAL_TYPE(BaCvPool, ba_cv_pool, BA, CV_POOL, GstBufferPool)
G_END_DECLS

struct _BaCvPool {
    GstBufferPool parent;
    CVPixelBufferPoolRef cv;
    GstVideoInfo info;
};
G_DEFINE_TYPE(BaCvPool, ba_cv_pool, GST_TYPE_BUFFER_POOL)

static const gchar** opciones(GstBufferPool*) {
    static const gchar* o[] = {GST_BUFFER_POOL_OPTION_VIDEO_META, nullptr};
    return o;
}

static gboolean configurar(GstBufferPool* bp, GstStructure* config) {
    auto* p = BA_CV_POOL(bp);
    GstCaps* caps = nullptr;
    guint tam = 0, min = 0, max = 0;
    if (!gst_buffer_pool_config_get_params(config, &caps, &tam, &min, &max) || caps == nullptr ||
        !gst_video_info_from_caps(&p->info, caps))
        return FALSE;
    const OSType f = ba_cv_formato(&p->info);
    if (f == 0) {
        GST_WARNING_OBJECT(p, "formato sin IOSurface: %s", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&p->info)));
        return FALSE;
    }
    if (p->cv != nullptr) CVPixelBufferPoolRelease(p->cv);
    p->cv = nullptr;
    NSDictionary* atr = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(f),
        (id)kCVPixelBufferWidthKey : @(GST_VIDEO_INFO_WIDTH(&p->info)),
        (id)kCVPixelBufferHeightKey : @(GST_VIDEO_INFO_HEIGHT(&p->info)),
        (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (id)kCVPixelBufferMetalCompatibilityKey : @YES,
    };
    CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)atr, &p->cv);
    if (p->cv == nullptr) return FALSE;
    // El tamaño real (con el relleno de la IOSurface): lo dice el GstVideoMeta.
    gst_buffer_pool_config_set_params(config, caps, static_cast<guint>(GST_VIDEO_INFO_SIZE(&p->info)), min, max);
    return GST_BUFFER_POOL_CLASS(ba_cv_pool_parent_class)->set_config(bp, config);
}

static GstFlowReturn crearBuffer(GstBufferPool* bp, GstBuffer** out, GstBufferPoolAcquireParams*) {
    auto* p = BA_CV_POOL(bp);
    CVPixelBufferRef pb = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, p->cv, &pb) != kCVReturnSuccess || pb == nullptr)
        return GST_FLOW_ERROR;
    // El color, para que quien convierta en la GPU sepa qué es.
    const auto matriz = GST_VIDEO_INFO_COLORIMETRY(&p->info).matrix;
    if (!GST_VIDEO_INFO_IS_RGB(&p->info))
        CVBufferSetAttachment(pb, kCVImageBufferYCbCrMatrixKey,
                              matriz == GST_VIDEO_COLOR_MATRIX_BT601 ? kCVImageBufferYCbCrMatrix_ITU_R_601_4
                                                                     : kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                              kCVAttachmentMode_ShouldPropagate);
    *out = ba_cv_buffer_nuevo(pb);
    CVPixelBufferRelease(pb);
    return GST_FLOW_OK;
}

static void finalizarPool(GObject* o) {
    auto* p = BA_CV_POOL(o);
    if (p->cv != nullptr) CVPixelBufferPoolRelease(p->cv);
    G_OBJECT_CLASS(ba_cv_pool_parent_class)->finalize(o);
}

static void ba_cv_pool_class_init(BaCvPoolClass* k) {
    G_OBJECT_CLASS(k)->finalize = finalizarPool;
    auto* b = GST_BUFFER_POOL_CLASS(k);
    b->get_options = opciones;
    b->set_config = configurar;
    b->alloc_buffer = crearBuffer;
}

static void ba_cv_pool_init(BaCvPool* p) {
    p->cv = nullptr;
    gst_video_info_init(&p->info);
}

GstBufferPool* ba_cv_pool_nuevo(void) {
    iniciar();
    return GST_BUFFER_POOL(g_object_new(BA_TYPE_CV_POOL, nullptr));
}
