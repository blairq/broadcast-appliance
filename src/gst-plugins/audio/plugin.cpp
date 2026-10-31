// ============================================================================
// src/gst-plugins/audio/plugin.cpp — el plugin de GStreamer «baaudio»
//
// El procesamiento de audio de las entradas como elementos de GStreamer (la
// capa de entrada, no libobs):
//
//     baautomixer   automixer de mesa redonda (Dugan) compartido por grupo    automixer.cpp
//     balimitador   limitador del máster con true peak (T12)                  limitador.cpp
//     baderiva      la placa al reloj del estudio (DLL + libsamplerate)       deriva.cpp
//
//   Ejemplo (dos micrófonos, la misma mesa):
//     gst-launch-1.0 osxaudiosrc device=73 ! audioconvert ! baautomixer grupo=bancas ! autoaudiosink \
//                    osxaudiosrc device=81 ! audioconvert ! baautomixer grupo=bancas ! autoaudiosink
// ============================================================================

#include <gst/gst.h>

extern "C" {
gboolean ba_registrar_automixer(GstPlugin* p);
gboolean ba_registrar_limitador(GstPlugin* p);
gboolean ba_registrar_deriva(GstPlugin* p);
}

static gboolean plugin_init(GstPlugin* p) {
    return ba_registrar_automixer(p) && ba_registrar_limitador(p) && ba_registrar_deriva(p);
}

#define PACKAGE "baaudio"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, baaudio, "Audio de Broadcast Appliance: automixer de mesa redonda, limitador del máster y deriva de las placas",
                  plugin_init, "1.0", "GPL", "Broadcast Appliance", "Broadcast Appliance")
