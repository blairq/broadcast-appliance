// ============================================================================
// src/obs-module/ba_gst.cpp — ver ba_gst.hpp
// ============================================================================

#include "ba_gst.hpp"

#if defined(BA_HAVE_GST)

#include "platform/GstEntorno.hpp"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <cstring>
#include <mutex>

namespace ba::gst {

namespace {
std::once_flag g_una;
bool g_listo = false;
std::string g_error;
} // namespace

bool iniciar() {
    std::call_once(g_una, []() {
        if (!ba::platform::prepararGst()) {
            g_error = "GStreamer no está en el programa";
            return;
        }
        GError* err = nullptr;
        g_listo = gst_init_check(nullptr, nullptr, &err);
        if (!g_listo) g_error = err != nullptr ? err->message : "gst_init falló";
        if (err != nullptr) g_error_free(err);
        if (g_listo) blog(LOG_INFO, "[ba-sources] GStreamer %s listo", gst_version_string());
        else blog(LOG_WARNING, "[ba-sources] GStreamer: %s", g_error.c_str());
    });
    return g_listo;
}

const std::string& errorInicio() { return g_error; }

TarjetaGst::TarjetaGst(std::string elemento, std::map<std::string, std::string> propiedades)
    : elemento_(std::move(elemento)), props_(std::move(propiedades)) {}

TarjetaGst::~TarjetaGst() {
    desarmar();
    obs_enter_graphics();
    gs_texrender_destroy(bajada_);
    gs_stagesurface_destroy(stage_);
    gs_texture_destroy(subida_);
    obs_leave_graphics();
}

void TarjetaGst::desarmar() {
    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    pipeline_ = src_ = sink_ = modulo_ = nullptr;
}

bool TarjetaGst::armar(std::uint32_t cx, std::uint32_t cy) {
    desarmar();
    error_.clear();
    if (!iniciar()) {
        error_ = errorInicio();
        return false;
    }
    modulo_ = gst_element_factory_make(elemento_.c_str(), "modulo");
    if (modulo_ == nullptr) {
        error_ = "no existe el módulo «" + elemento_ + "»";
        return false;
    }
    for (const auto& [k, v] : props_) gst_util_set_object_arg(G_OBJECT(modulo_), k.c_str(), v.c_str());
    pipeline_ = gst_pipeline_new("ba-tarjeta");
    src_ = gst_element_factory_make("appsrc", "src");
    GstElement* conv1 = gst_element_factory_make("videoconvert", nullptr);
    GstElement* conv2 = gst_element_factory_make("videoconvert", nullptr);
    GstElement* escala = gst_element_factory_make("videoscale", nullptr);
    GstElement* filtro = gst_element_factory_make("capsfilter", nullptr);
    sink_ = gst_element_factory_make("appsink", "sink");
    if (!pipeline_ || !src_ || !conv1 || !conv2 || !escala || !filtro || !sink_) {
        error_ = "faltan elementos básicos de GStreamer (appsrc, videoconvert…)";
        desarmar();
        return false;
    }
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGBA", "width", G_TYPE_INT,
                                        static_cast<int>(cx), "height", G_TYPE_INT, static_cast<int>(cy), "framerate",
                                        GST_TYPE_FRACTION, 30, 1, nullptr);
    g_object_set(src_, "caps", caps, "format", GST_FORMAT_TIME, "is-live", TRUE, "block", FALSE, nullptr);
    g_object_set(filtro, "caps", caps, nullptr);
    g_object_set(sink_, "sync", FALSE, "max-buffers", 2, "drop", TRUE, nullptr);
    gst_caps_unref(caps);
    gst_bin_add_many(GST_BIN(pipeline_), src_, conv1, modulo_, conv2, escala, filtro, sink_, nullptr);
    if (!gst_element_link_many(src_, conv1, modulo_, conv2, escala, filtro, sink_, nullptr)) {
        error_ = "el módulo «" + elemento_ + "» no acepta video crudo (¿es un filtro de video?)";
        desarmar();
        return false;
    }
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "el pipeline de «" + elemento_ + "» no arrancó";
        desarmar();
        return false;
    }
    cx_ = cx;
    cy_ = cy;
    pts_ = 0;
    haySubida_ = false;
    return true;
}

void TarjetaGst::ajustar(const std::map<std::string, std::string>& propiedades) {
    for (const auto& [k, v] : propiedades) {
        const auto it = props_.find(k);
        if (it != props_.end() && it->second == v) continue;
        props_[k] = v;
        if (modulo_ != nullptr) gst_util_set_object_arg(G_OBJECT(modulo_), k.c_str(), v.c_str());
    }
}

bool TarjetaGst::procesar(gs_texture_t* entrada, std::uint32_t cx, std::uint32_t cy, gs_effect_t* fx) {
    if (pipeline_ == nullptr || cx != cx_ || cy != cy_) {
        if (!armar(cx, cy)) return false;
    }
    if (bajada_ == nullptr) bajada_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (stage_ == nullptr || gs_stagesurface_get_width(stage_) != cx || gs_stagesurface_get_height(stage_) != cy) {
        gs_stagesurface_destroy(stage_);
        stage_ = gs_stagesurface_create(cx, cy, GS_RGBA);
    }
    gs_eparam_t* pImage = gs_effect_get_param_by_name(fx, "image");

    // 1) La entrada, con alfa recto (GStreamer no premultiplica), a la RAM.
    gs_texrender_reset(bajada_);
    if (!gs_texrender_begin(bajada_, cx, cy)) return false;
    gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
    gs_effect_set_texture(pImage, entrada);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    while (gs_effect_loop(fx, "Despremultiplicar")) gs_draw_sprite(entrada, 0, cx, cy);
    gs_blend_state_pop();
    gs_texrender_end(bajada_);
    gs_stage_texture(stage_, gs_texrender_get_texture(bajada_));
    std::uint8_t* datos = nullptr;
    std::uint32_t paso = 0;
    if (!gs_stagesurface_map(stage_, &datos, &paso)) return false;
    const gsize tam = static_cast<gsize>(cx) * cy * 4;
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, tam, nullptr);
    GstMapInfo m;
    if (gst_buffer_map(buf, &m, GST_MAP_WRITE)) {
        for (std::uint32_t y = 0; y < cy; ++y)
            std::memcpy(m.data + static_cast<gsize>(y) * cx * 4, datos + static_cast<gsize>(y) * paso, cx * 4);
        gst_buffer_unmap(buf, &m);
    }
    gs_stagesurface_unmap(stage_);
    GST_BUFFER_PTS(buf) = pts_;
    GST_BUFFER_DURATION(buf) = GST_SECOND / 30;
    pts_ += GST_SECOND / 30;
    gst_app_src_push_buffer(GST_APP_SRC(src_), buf); // se queda con el buffer

    // 2) Lo que devuelve (espera un poco: los filtros comunes devuelven en el
    //    acto; los que tienen retardo propio entregan el cuadro anterior).
    if (GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(sink_), 40 * GST_MSECOND)) {
        GstBuffer* b = gst_sample_get_buffer(s);
        GstMapInfo r;
        if (b != nullptr && gst_buffer_map(b, &r, GST_MAP_READ)) {
            if (subida_ == nullptr || gs_texture_get_width(subida_) != cx || gs_texture_get_height(subida_) != cy) {
                gs_texture_destroy(subida_);
                subida_ = gs_texture_create(cx, cy, GS_RGBA, 1, nullptr, GS_DYNAMIC);
            }
            if (r.size >= tam) {
                gs_texture_set_image(subida_, r.data, cx * 4, false);
                haySubida_ = true;
            }
            gst_buffer_unmap(b, &r);
        }
        gst_sample_unref(s);
    }
    // Errores del pipeline (un módulo que se cae) quedan a la vista.
    if (GstBus* bus = gst_element_get_bus(pipeline_)) {
        while (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            error_ = e != nullptr ? e->message : "error de GStreamer";
            if (e != nullptr) g_error_free(e);
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    }
    if (!haySubida_) return false;

    // 3) De vuelta a la GPU, con alfa premultiplicado.
    gs_effect_set_texture(pImage, subida_);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    while (gs_effect_loop(fx, "Premultiplicar")) gs_draw_sprite(subida_, 0, cx, cy);
    gs_blend_state_pop();
    return true;
}

} // namespace ba::gst

#endif
