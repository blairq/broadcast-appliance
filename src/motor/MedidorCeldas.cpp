// ============================================================================
// src/motor/MedidorCeldas.cpp — ver MedidorCeldas.hpp
// ============================================================================
#include "MedidorCeldas.hpp"

#include <gst/video/video.h>

namespace ba::motor {

struct MedidorCeldas::Datos {
    mutable std::mutex mu;
    AnalisisCeldas a;
    GstCaps* caps = nullptr; ///< las últimas que se leyeron (bajo mu)
    GstVideoInfo info{};
    bool sirve = false; ///< 4 bytes por píxel, un plano
    ~Datos() {
        if (caps != nullptr) gst_caps_unref(caps);
    }
};

namespace {

std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

GstPadProbeReturn alPasar(GstPad* pad, GstPadProbeInfo* info, gpointer u) {
    auto* d = static_cast<std::shared_ptr<MedidorCeldas::Datos>*>(u)->get();
    if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) return GST_PAD_PROBE_OK;
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b == nullptr) return GST_PAD_PROBE_OK;
    const std::int64_t t = ahoraNs();
    std::lock_guard<std::mutex> l(d->mu);
    GstCaps* c = gst_pad_get_current_caps(pad);
    if (c == nullptr) return GST_PAD_PROBE_OK;
    if (d->caps == nullptr || !gst_caps_is_equal(c, d->caps)) {
        if (d->caps != nullptr) gst_caps_unref(d->caps);
        d->caps = gst_caps_ref(c);
        d->sirve = gst_video_info_from_caps(&d->info, c) && GST_VIDEO_INFO_N_PLANES(&d->info) == 1 &&
                   GST_VIDEO_INFO_COMP_PSTRIDE(&d->info, 0) == 4;
        // Solo memoria de sistema: la de la GPU no se puede leer acá.
        const GstCapsFeatures* f = gst_caps_get_features(c, 0);
        if (f != nullptr && !gst_caps_features_is_equal(f, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) d->sirve = false;
    }
    gst_caps_unref(c);
    if (!d->sirve) return GST_PAD_PROBE_OK;
    GstMapInfo m;
    if (!gst_buffer_map(b, &m, GST_MAP_READ)) return GST_PAD_PROBE_OK;
    const int w = GST_VIDEO_INFO_WIDTH(&d->info), h = GST_VIDEO_INFO_HEIGHT(&d->info);
    const int stride = GST_VIDEO_INFO_PLANE_STRIDE(&d->info, 0);
    // Un buffer más chico que lo que dicen las caps (el tamaño real del canal: ver
    // videocomun.cpp) ya trae sus caps propias; por las dudas, no se lee de más.
    if (m.size >= static_cast<gsize>(stride) * static_cast<gsize>(h)) {
        const std::uint64_t indice = GST_BUFFER_OFFSET_IS_VALID(b) ? GST_BUFFER_OFFSET(b) : 0;
        d->a.cuadro(m.data, w, h, stride, t, indice);
    }
    gst_buffer_unmap(b, &m);
    return GST_PAD_PROBE_OK;
}

void soltarDatos(gpointer u) { delete static_cast<std::shared_ptr<MedidorCeldas::Datos>*>(u); }

} // namespace

MedidorCeldas::MedidorCeldas(std::vector<AnalisisCeldas::Celda> celdas) : d_(std::make_shared<Datos>()) {
    d_->a = AnalisisCeldas(std::move(celdas));
}

bool MedidorCeldas::poner(GstPad* pad) {
    if (pad == nullptr || pad_ != nullptr) return false;
    pad_ = GST_PAD(gst_object_ref(pad));
    id_ = gst_pad_add_probe(pad_, GST_PAD_PROBE_TYPE_BUFFER, alPasar, new std::shared_ptr<Datos>(d_), soltarDatos);
    return id_ != 0;
}

void MedidorCeldas::sacar() {
    if (pad_ == nullptr) return;
    if (id_ != 0) gst_pad_remove_probe(pad_, id_);
    gst_object_unref(pad_);
    pad_ = nullptr;
    id_ = 0;
}

void MedidorCeldas::cuadro(const std::uint8_t* px, int w, int h, int stride, std::uint64_t indice) {
    const std::int64_t t = ahoraNs();
    std::lock_guard<std::mutex> l(d_->mu);
    d_->a.cuadro(px, w, h, stride, t, indice);
}

void MedidorCeldas::cambiar(const std::vector<AnalisisCeldas::Celda>& celdas) {
    const std::int64_t t = ahoraNs();
    std::lock_guard<std::mutex> l(d_->mu);
    d_->a.cambiar(celdas, t);
}

nlohmann::json MedidorCeldas::resumen() const {
    std::lock_guard<std::mutex> l(d_->mu);
    return d_->a.resumen();
}

} // namespace ba::motor
