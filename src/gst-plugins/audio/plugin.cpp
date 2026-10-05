// ============================================================================
// src/gst-plugins/audio/plugin.cpp — el plugin de GStreamer «baaudio»
//
// El procesamiento de audio de las entradas como elementos de GStreamer (la
// capa de entrada, no libobs):
//
//     baautomixer   automixer de mesa redonda (Dugan) compartido por grupo    automixer.cpp
//
//   Ejemplo (dos micrófonos, la misma mesa):
//     gst-launch-1.0 osxaudiosrc device=73 ! audioconvert ! baautomixer grupo=bancas ! autoaudiosink \
//                    osxaudiosrc device=81 ! audioconvert ! baautomixer grupo=bancas ! autoaudiosink
// ============================================================================

#include <gst/gst.h>

extern "C" {
gboolean ba_registrar_automixer(GstPlugin* p);
}

static gboolean plugin_init(GstPlugin* p) { return ba_registrar_automixer(p); }

#define PACKAGE "baaudio"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, baaudio, "Audio de Broadcast Appliance: automixer de mesa redonda",
                  plugin_init, "1.0", "GPL", "Broadcast Appliance", "Broadcast Appliance")
