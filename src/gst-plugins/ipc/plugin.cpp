// ============================================================================
// src/gst-plugins/ipc/plugin.cpp — el plugin de GStreamer «baipc»
//
// Los canales de memoria compartida entre procesos del aparato, como elementos
// de GStreamer (para que lo que trae el gateway entre por la capa de entrada,
// no por libobs):
//
//     baanillo   fuente de audio: lee un anillo (BA.Audio.Guest.<n>, el     anillo.cpp
//                remoto con la palabra…) que escribe el gateway
//
//   Ejemplo:  gst-launch-1.0 baanillo canal=BA.Audio.Guest.1 ! audioconvert ! autoaudiosink
// ============================================================================

#include <gst/gst.h>

extern "C" {
gboolean ba_registrar_anillo(GstPlugin* p);
}

static gboolean plugin_init(GstPlugin* p) { return ba_registrar_anillo(p); }

#define PACKAGE "baipc"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, baipc,
                  "Canales entre procesos de Broadcast Appliance: el audio de los invitados", plugin_init, "1.0", "GPL",
                  "Broadcast Appliance", "Broadcast Appliance")
