// ============================================================================
// src/gst-plugins/filtros/comun.hpp — lo que comparten los filtros de imagen
//
// Cada filtro es un GstVideoFilter como corresponde:
//   - CAPS: plantillas con los formatos que sabe tratar; videoconvert (que el
//     que arma la cadena pone delante) convierte solo si hace falta.
//   - PASANTE: si los ajustes no cambian nada, el buffer pasa sin tocarse
//     (gst_base_transform_set_passthrough), sin copia.
//   - EN EL LUGAR cuando el tamaño no cambia (transform_frame_ip).
//   - QoS activado: si la cadena viene atrasada, se descartan cuadros antes.
//   - PROPIEDADES CONTROLABLES (GST_PARAM_CONTROLLABLE): se pueden animar con
//     un GstControlSource; los valores se sincronizan antes de cada cuadro.
//   - LATENCIA: no agregan (trabajan cuadro a cuadro sin esperar).
// ============================================================================
#pragma once

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ba::filtros {

/// Antes de cada cuadro: los valores de las propiedades animadas (GstController).
inline void sincronizarValores(GstBaseTransform* bt, GstBuffer* b) {
    const GstClockTime t = GST_BUFFER_PTS(b);
    if (!GST_CLOCK_TIME_IS_VALID(t)) return;
    const GstClockTime st = gst_segment_to_stream_time(&bt->segment, GST_FORMAT_TIME, t);
    if (GST_CLOCK_TIME_IS_VALID(st)) gst_object_sync_values(GST_OBJECT(bt), st);
}

inline std::uint8_t a8(float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

/// Dónde está cada canal en un píxel de 4 bytes (R, G, B, A; -1 = sin alfa).
struct Orden {
    int r, g, b, a;
};
inline bool ordenDe(GstVideoFormat f, Orden& o) {
    switch (f) {
        case GST_VIDEO_FORMAT_BGRA: o = {2, 1, 0, 3}; return true;
        case GST_VIDEO_FORMAT_RGBA: o = {0, 1, 2, 3}; return true;
        case GST_VIDEO_FORMAT_ARGB: o = {1, 2, 3, 0}; return true;
        case GST_VIDEO_FORMAT_ABGR: o = {3, 2, 1, 0}; return true;
        case GST_VIDEO_FORMAT_BGRx: o = {2, 1, 0, -1}; return true;
        case GST_VIDEO_FORMAT_RGBx: o = {0, 1, 2, -1}; return true;
        case GST_VIDEO_FORMAT_xRGB: o = {1, 2, 3, -1}; return true;
        case GST_VIDEO_FORMAT_xBGR: o = {3, 2, 1, -1}; return true;
        default: return false;
    }
}

constexpr GParamFlags kVivo = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                       GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING);

} // namespace ba::filtros
