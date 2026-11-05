// ============================================================================
// src/motor/GstAdelantado.hpp — los tipos de GStreamer que una cabecera nombra sin usarlos
//
// Una cabecera del motor que solo pasa o guarda punteros (GstElement*, GstPad*…) no
// necesita <gst/gst.h> (glib y todo GStreamer en cada .o que la incluya): le alcanza con
// declararlos como los declara GStreamer (`typedef struct _GstX GstX;`; repetir el mismo
// typedef es válido en C++). Los .cpp que hablan con GStreamer incluyen <gst/gst.h>.
// Los enum (GstPadProbeReturn, GstFlowReturn…) y los tipos de glib que cambian con la
// plataforma (guint64 y GstClockTime: unsigned long o unsigned long long) NO van acá: quien los nombre, incluye <gst/gst.h>.
// ============================================================================
#pragma once

typedef void* gpointer;
typedef unsigned long gulong; ///< igual en todas las plataformas (glib/gtypes.h): el id de una sonda o señal
typedef struct _GstBuffer GstBuffer;
typedef struct _GstBus GstBus;
typedef struct _GstCaps GstCaps;
typedef struct _GstClock GstClock;
typedef struct _GstContext GstContext;
typedef struct _GstDevice GstDevice;
typedef struct _GstDeviceMonitor GstDeviceMonitor;
typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstObject GstObject;
typedef struct _GstPad GstPad;
typedef struct _GstPadProbeInfo GstPadProbeInfo;
typedef struct _GstSample GstSample;
