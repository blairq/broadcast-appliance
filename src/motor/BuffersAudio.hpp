// ============================================================================
// src/motor/BuffersAudio.hpp — los buffers de los dispositivos de audio, explícitos
//
// docs/arquitectura/04-plan-unificado.md, F2-04. Las fuentes y los sinks de audio del
// sistema (osxaudiosrc/sink, wasapi2src/sink, pulsesrc/sink: todos GstAudioBaseSrc o
// GstAudioBaseSink) traen de fábrica un segmento de 10 ms y un buffer de 200 ms. En la
// fuente el buffer es capacidad (no demora); en el sink es lo que se adelanta lo
// escrito al parlante, y en Windows sin `low-latency` el motor de audio agrega su
// período largo. Acá se fijan los dos de forma explícita, iguales en las tres
// plataformas, y se informan (el estado de la entrada y de la escucha, y de ahí
// `tiempos_pipeline` y la vista de tiempos):
//
//   latency-time = audio.dispositivoMs (10 ms): el bloque con que entrega o pide
//   buffer-time  = audio.dispositivoBufferMs (40 ms): el colchón del dispositivo
//
// Si la descripción del pipeline ya trae `latency-time` o `buffer-time` (alguien lo
// pidió a mano para una placa rara), se respeta: solo se informa.
// ============================================================================
#pragma once

#include "ajustes/Ajustes.hpp"

#include <gst/audio/audio.h>
#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>

namespace ba::motor {

/// El segmento y el colchón de los dispositivos de audio (µs, como las propiedades).
inline gint64 latenciaDispositivoUs() { return static_cast<gint64>(ajustes::entero("audio.dispositivoMs", 10)) * 1000; }
inline gint64 bufferDispositivoUs() {
    return std::max<gint64>(static_cast<gint64>(ajustes::entero("audio.dispositivoBufferMs", 40)) * 1000,
                            2 * latenciaDispositivoUs()); // menos de dos segmentos no anda en ningún sistema
}

/// Fija los buffers de cada fuente o sink de audio del sistema que haya en `bin` (los que
/// `descripcion` no fija a mano) y devuelve lo que quedó: [{elemento, fabrica, latencia_ms,
/// buffer_ms, low_latency (si la tiene)}]. Antes de arrancar el pipeline.
inline nlohmann::json fijarBuffersDeAudio(GstElement* bin, const std::string& descripcion) {
    nlohmann::json out = nlohmann::json::array();
    if (bin == nullptr) return out;
    const bool aMano = descripcion.find("latency-time") != std::string::npos || descripcion.find("buffer-time") != std::string::npos;
    const auto uno = [&](GstElement* e) {
        if (!GST_IS_AUDIO_BASE_SRC(e) && !GST_IS_AUDIO_BASE_SINK(e)) return;
        if (!aMano)
            g_object_set(e, "latency-time", latenciaDispositivoUs(), "buffer-time", bufferDispositivoUs(), nullptr);
        gint64 lat = 0, buf = 0;
        g_object_get(e, "latency-time", &lat, "buffer-time", &buf, nullptr);
        GstElementFactory* f = gst_element_get_factory(e);
        nlohmann::json j{{"elemento", GST_OBJECT_NAME(e)},
                         {"fabrica", f != nullptr ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)) : ""},
                         {"latencia_ms", static_cast<double>(lat) / 1000.0},
                         {"buffer_ms", static_cast<double>(buf) / 1000.0},
                         {"a_mano", aMano}};
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "low-latency") != nullptr) {
            gboolean ll = FALSE;
            g_object_get(e, "low-latency", &ll, nullptr);
            j["low_latency"] = ll != FALSE;
        }
        out.push_back(std::move(j));
    };
    if (!GST_IS_BIN(bin)) {
        uno(bin);
        return out;
    }
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(bin));
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        uno(GST_ELEMENT(g_value_get_object(&v)));
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    return out;
}

} // namespace ba::motor
