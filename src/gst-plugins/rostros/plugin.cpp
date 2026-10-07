// ============================================================================
// src/gst-plugins/rostros/plugin.cpp — el plugin de GStreamer «barostros»
//
//     barostros, barostrosgl, barostrosd3d12   caras en la cadena de una entrada
//                                              (barostros.cpp)
//     baseguir (+ baseguirgl, baseguird3d12)   el seguimiento de una cara: elige,
//                                              decide el encuadre (seguir.cpp)
//
//   Ejemplo:  gst-launch-1.0 -m avfvideosrc ! videoconvert ! barostros marcar=true !
//             videoconvert ! autovideosink        (-m: los mensajes «ba-rostros»)
// ============================================================================
#include <gst/gst.h>

extern "C" gboolean ba_registrar_rostros(GstPlugin* p);
extern "C" gboolean ba_registrar_seguir(GstPlugin* p);

static gboolean plugin_init(GstPlugin* p) { return ba_registrar_rostros(p) && ba_registrar_seguir(p); }

#define PACKAGE "barostros"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, barostros,
                  "Rostros de Broadcast Appliance: encontrar, reconocer y marcar caras sin demorar la imagen",
                  plugin_init, "1.0", "GPL", "Broadcast Appliance", "Broadcast Appliance")
