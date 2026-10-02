// ============================================================================
// src/obs-module/ba_gst_fuente.cpp — Un pipeline de GStreamer como entrada de video
//
// FUENTE `ba_gst_fuente`: el ajuste `pipeline` es la parte de origen de un
// pipeline de GStreamer, como se escribe en gst-launch:
//   videotestsrc pattern=smpte is-live=true          (barras SMPTE)
//   videotestsrc pattern=black is-live=true          (negro)
//   rtspsrc location=rtsp://… latency=200 ! decodebin
//   srtsrc uri=srt://:9000 ! decodebin
// y la fuente le agrega «! videoconvert ! video/x-raw,format=BGRA ! appsink».
// Cada cuadro va a libobs con obs_source_output_video (fuente asíncrona,
// como una cámara). Si el pipeline se cae (una red que se corta), se vuelve a
// armar a los 2 s, y el motivo queda en el log y en el proc `estado`.
//
// Las entradas virtuales de la app (señal de ajuste, color, negro) son esta
// fuente con un pipeline de videotestsrc.
// ============================================================================

#if defined(BA_HAVE_GST)

#include "ba_gst.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
#include <mutex>
#include <new>
#include <string>

namespace {

struct Fuente {
    obs_source_t* ctx = nullptr;
    std::mutex lock;
    std::string descripcion;
    std::string error;
    GstElement* pipeline = nullptr;
    std::atomic<std::uint64_t> reintento{0}; ///< cuándo volver a armar (os_gettime_ns)
};

GstFlowReturn alCuadro(GstAppSink* sink, gpointer datos) {
    auto* f = static_cast<Fuente*>(datos);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (s == nullptr) return GST_FLOW_OK;
    GstCaps* caps = gst_sample_get_caps(s);
    GstStructure* st = caps != nullptr ? gst_caps_get_structure(caps, 0) : nullptr;
    int w = 0, h = 0;
    if (st != nullptr) {
        gst_structure_get_int(st, "width", &w);
        gst_structure_get_int(st, "height", &h);
    }
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo m;
    if (w > 0 && h > 0 && b != nullptr && gst_buffer_map(b, &m, GST_MAP_READ)) {
        obs_source_frame frame = {};
        frame.data[0] = m.data;
        frame.linesize[0] = static_cast<uint32_t>(w) * 4;
        frame.width = static_cast<uint32_t>(w);
        frame.height = static_cast<uint32_t>(h);
        frame.format = VIDEO_FORMAT_BGRA;
        frame.timestamp = os_gettime_ns();
        frame.full_range = true;
        video_format_get_parameters_for_format(VIDEO_CS_SRGB, VIDEO_RANGE_FULL, VIDEO_FORMAT_BGRA, frame.color_matrix,
                                               frame.color_range_min, frame.color_range_max);
        obs_source_output_video(f->ctx, &frame);
        gst_buffer_unmap(b, &m);
    }
    gst_sample_unref(s);
    return GST_FLOW_OK;
}

void desarmar(Fuente* f) {
    if (f->pipeline != nullptr) {
        gst_element_set_state(f->pipeline, GST_STATE_NULL);
        gst_object_unref(f->pipeline);
        f->pipeline = nullptr;
    }
}

void armar(Fuente* f) {
    desarmar(f);
    f->error.clear();
    if (!ba::gst::iniciar()) {
        f->error = ba::gst::errorInicio();
        return;
    }
    if (f->descripcion.empty()) return;
    const std::string completo = f->descripcion + " ! videoconvert ! video/x-raw,format=BGRA ! appsink name=ba_sink "
                                                  "sync=true max-buffers=2 drop=true emit-signals=false";
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(completo.c_str(), &err);
    if (err != nullptr) {
        f->error = err->message;
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        blog(LOG_WARNING, "[ba-sources] fuente GStreamer «%s»: %s", f->descripcion.c_str(), f->error.c_str());
        return;
    }
    GstElement* sink = gst_bin_get_by_name(GST_BIN(p), "ba_sink");
    GstAppSinkCallbacks cb = {};
    cb.new_sample = alCuadro;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, f, nullptr);
    gst_object_unref(sink);
    f->pipeline = p;
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        f->error = "el pipeline no arrancó";
        desarmar(f);
    }
}

const char* nombre(void*) { return obs_module_text("Fuente GStreamer (BA)"); }

void actualizar(void* data, obs_data_t* s) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    const std::string d = obs_data_get_string(s, "pipeline");
    if (d == f->descripcion && f->pipeline != nullptr) return;
    f->descripcion = d;
    armar(f);
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* f = new (std::nothrow) Fuente();
    if (f == nullptr) return nullptr;
    f->ctx = ctx;
    actualizar(f, settings);
    proc_handler_add(obs_source_get_proc_handler(ctx), "void estado(out string error, out bool andando)",
                     [](void* d, calldata_t* cd) {
                         auto* x = static_cast<Fuente*>(d);
                         std::lock_guard<std::mutex> l(x->lock);
                         calldata_set_string(cd, "error", x->error.c_str());
                         calldata_set_bool(cd, "andando", x->pipeline != nullptr);
                     },
                     f);
    return f;
}

void destruir(void* data) {
    auto* f = static_cast<Fuente*>(data);
    {
        std::lock_guard<std::mutex> l(f->lock);
        desarmar(f);
    }
    delete f;
}

/// Revisa el bus: un error (la red se cortó) se anota y se reintenta a los 2 s.
void tic(void* data, float) {
    auto* f = static_cast<Fuente*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    if (f->pipeline != nullptr) {
        GstBus* bus = gst_element_get_bus(f->pipeline);
        while (GstMessage* m = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                GError* e = nullptr;
                gst_message_parse_error(m, &e, nullptr);
                f->error = e != nullptr ? e->message : "error";
                if (e != nullptr) g_error_free(e);
                blog(LOG_WARNING, "[ba-sources] fuente GStreamer «%s»: %s (reintento en 2 s)", f->descripcion.c_str(),
                     f->error.c_str());
            } else {
                f->error = "terminó (EOS)";
            }
            gst_message_unref(m);
            desarmar(f);
            f->reintento = os_gettime_ns() + 2'000'000'000ULL;
        }
        if (bus != nullptr) gst_object_unref(bus);
    } else if (!f->descripcion.empty() && f->reintento != 0 && os_gettime_ns() >= f->reintento) {
        f->reintento = 0;
        armar(f);
    }
}

obs_properties_t* propiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "pipeline", obs_module_text("Pipeline de GStreamer (origen)"), OBS_TEXT_MULTILINE);
    return p;
}

void porDefecto(obs_data_t* s) { obs_data_set_default_string(s, "pipeline", "videotestsrc pattern=smpte is-live=true"); }

} // namespace

extern "C" {

static struct obs_source_info ba_gst_fuente_info = {};

void ba_register_gst_fuente(void) {
    ba_gst_fuente_info.id = "ba_gst_fuente";
    ba_gst_fuente_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_gst_fuente_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    ba_gst_fuente_info.get_name = nombre;
    ba_gst_fuente_info.create = crear;
    ba_gst_fuente_info.destroy = destruir;
    ba_gst_fuente_info.update = actualizar;
    ba_gst_fuente_info.video_tick = tic;
    ba_gst_fuente_info.get_properties = propiedades;
    ba_gst_fuente_info.get_defaults = porDefecto;
    obs_register_source(&ba_gst_fuente_info);
}

} // extern "C"

#else

extern "C" void ba_register_gst_fuente(void) {}

#endif
