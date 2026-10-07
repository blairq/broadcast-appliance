// ============================================================================
// src/gst-plugins/filtros/plugin.cpp — el plugin de GStreamer «bafiltros»
//
// Las tarjetas de imagen del grafo de video como elementos de GStreamer, para
// correr en la capa de entrada (la cadena de GStreamer de cada entrada):
//
//     bacolor     brillo, contraste, saturación y gamma     color.cpp
//     bacroma     saca un fondo de color (transparente)     croma.cpp
//     barecorte   recorte en fracciones y espejo            recorte.cpp
//     balegal     legalizador broadcast-safe (luma, croma,   legal.cpp
//                 gama compuesta)
//     baptz       PTZ digital: pan, tilt y zoom recortando   ptz.cpp
//     bamejora    mejorar imagen: ruido, color y nitidez     mejora.cpp
//     basalida    publica un punto de la cadena con nombre   salida.cpp
//                 (la tarjeta Salida en el medio)
//     bacolorgl, barecortegl, baptzgl: lo mismo en la GPU     gl.cpp, ptz.cpp
//
//   Ejemplo:  gst-launch-1.0 videotestsrc ! videoconvert ! bacolor saturacion=0 !
//             barecorte izquierda=0.1 modo=recortar ! videoconvert ! autovideosink
// ============================================================================

#include <gst/gst.h>

extern "C" {
gboolean ba_registrar_color(GstPlugin* p);
gboolean ba_registrar_croma(GstPlugin* p);
gboolean ba_registrar_recorte(GstPlugin* p);
gboolean ba_registrar_legal(GstPlugin* p);
gboolean ba_registrar_ptz(GstPlugin* p);
gboolean ba_registrar_mejora(GstPlugin* p);
gboolean ba_registrar_gl(GstPlugin* p);
gboolean ba_registrar_salida(GstPlugin* p);
}

static gboolean plugin_init(GstPlugin* p) {
    return ba_registrar_color(p) && ba_registrar_croma(p) && ba_registrar_recorte(p) && ba_registrar_legal(p) && ba_registrar_ptz(p) && ba_registrar_mejora(p) && ba_registrar_gl(p) && ba_registrar_salida(p);
}

#define PACKAGE "bafiltros"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, bafiltros,
                  "Filtros de imagen de Broadcast Appliance: color, croma, recorte, espejo, legalizador, PTZ digital y mejorar imagen", plugin_init, "1.0",
                  "GPL", "Broadcast Appliance", "Broadcast Appliance")
