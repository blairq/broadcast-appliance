// ============================================================================
// src/gst-plugins/superres/cvmemoria_mac.h — cuadros de GStreamer que SON IOSurfaces
//
// Como applemedia (sys/applemedia/corevideomemory.c y corevideobuffer.c, de
// gst-plugins-bad), que no exporta esto como biblioteca:
//
//   - MEMORIA «BaCoreVideoMemory»: un GstMemory por plano sobre un
//     CVPixelBuffer compartido. Se bloquea (CVPixelBufferLockBaseAddress)
//     solo mientras alguien lo mapea, de solo lectura o de lectura y escritura
//     según el primer mapeo; desmapeado, la GPU / el Neural Engine pueden
//     escribirlo sin que la CPU lo tenga tomado.
//   - GstCoreVideoMeta: el buffer lleva su CVPixelBuffer con el metadato de
//     applemedia (el mismo registro), así vtenc, otro elemento de la Mac o la
//     siguiente red lo toman sin copiar.
//   - POOL «BaCVPool»: GstBufferPool que entrega IOSurfaces (de un
//     CVPixelBufferPool). Un elemento lo ofrece arriba (propose_allocation)
//     para que quien le escribe (videoconvert, un decodificador por software)
//     lo haga directo en memoria que la GPU usa sin copiar.
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

#include <CoreVideo/CoreVideo.h>

G_BEGIN_DECLS

/// Un GstBuffer cuyas memorias son los planos de `pb` (lo retiene), con
/// GstVideoMeta (los strides de la IOSurface) y GstCoreVideoMeta.
GstBuffer* ba_cv_buffer_nuevo(CVPixelBufferRef pb);

/// El CVPixelBuffer de un buffer (de nuestra memoria o del GstCoreVideoMeta de
/// applemedia), sin retener; NULL si es memoria común.
CVPixelBufferRef ba_cv_pixbuf_de(GstBuffer* b);

/// El formato de CoreVideo para un formato de GStreamer (0 si no hay).
OSType ba_cv_formato(const GstVideoInfo* info);

/// Un pool de IOSurfaces para estas caps (NV12, BGRA, RGBA, UYVY, YUY2).
GstBufferPool* ba_cv_pool_nuevo(void);

G_END_DECLS
