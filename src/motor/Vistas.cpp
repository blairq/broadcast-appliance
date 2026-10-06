// ============================================================================
// src/motor/Vistas.cpp — ver Vistas.hpp
// ============================================================================
#include "Vistas.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <algorithm>
#include <cstring>

namespace ba::motor {

using json = nlohmann::json;

namespace {
constexpr std::int64_t kMargenNs = 40 * GST_MSECOND;
struct Llamada {
    void* vistas;
    std::string clave;
};
} // namespace

Vistas::Vistas(Reloj& reloj, Resolver resolver) : reloj_(reloj), resolver_(std::move(resolver)) {}

Vistas::~Vistas() {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& [k, u] : vistas_) {
        desarmar(*u);
        delete u;
    }
    vistas_.clear();
}

GstFlowReturn Vistas::alCuadro(GstElement* sink, gpointer d) {
    auto* ll = static_cast<Llamada*>(d);
    auto* self = static_cast<Vistas*>(ll->vistas);
    GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (s == nullptr) return GST_FLOW_OK;
    GstVideoInfo info;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo m;
    if (gst_video_info_from_caps(&info, gst_sample_get_caps(s)) && b != nullptr && gst_buffer_map(b, &m, GST_MAP_READ)) {
        std::function<void(const Cuadro&)> aviso;
        Cuadro copia;
        {
            std::lock_guard<std::mutex> l(self->mu_);
            auto it = self->vistas_.find(ll->clave);
            if (it != self->vistas_.end()) {
                Cuadro& c = it->second->cuadro;
                c.ancho = GST_VIDEO_INFO_WIDTH(&info);
                c.alto = GST_VIDEO_INFO_HEIGHT(&info);
                c.stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
                c.bgra.assign(m.data, m.data + std::min<std::size_t>(m.size, static_cast<std::size_t>(c.stride) * c.alto));
                c.serie = ++self->serie_;
                aviso = it->second->aviso;
                if (aviso) copia = c;
            }
        }
        gst_buffer_unmap(b, &m);
        if (aviso) aviso(copia); // fuera del lock
    }
    gst_sample_unref(s);
    return GST_FLOW_OK;
}

namespace {
std::string base(const std::string& clave) {
    const auto a = clave.find('@');
    return a == std::string::npos ? clave : clave.substr(0, a);
}
} // namespace

void Vistas::avisar(const std::string& clave, std::function<void(const Cuadro&)> fn) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = vistas_.find(clave);
    if (it != vistas_.end()) it->second->aviso = std::move(fn);
}

void Vistas::armar(const std::string& clave, Una& u) {
    desarmar(u);
    u.productor = resolver_(base(clave), &u.latencia);
    if (u.productor.empty()) return;
    const std::string d = "intersrc producer-name=" + entreComillas(u.productor) +
                          " ! queue leaky=downstream max-size-buffers=2 max-size-time=0 max-size-bytes=0 ! videoconvert ! "
                          "videoscale ! video/x-raw,format=BGRA,width=" + std::to_string(u.ancho) + ",height=" +
                          std::to_string(u.alto) + ",pixel-aspect-ratio=1/1 ! appsink name=s max-buffers=1 drop=true sync=true";
    GstElement* p = gst_parse_launch(d.c_str(), nullptr);
    if (p == nullptr) return;
    GstElement* s = gst_bin_get_by_name(GST_BIN(p), "s");
    g_object_set(s, "emit-signals", TRUE, nullptr);
    g_signal_connect_data(s, "new-sample", G_CALLBACK(&Vistas::alCuadro), new Llamada{this, clave},
                          [](gpointer d, GClosure*) { delete static_cast<Llamada*>(d); }, static_cast<GConnectFlags>(0));
    gst_object_unref(s);
    reloj_.adoptar(p);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(u.latencia + kMargenNs));
    gst_element_set_state(p, GST_STATE_PLAYING);
    u.pipeline = p;
}

void Vistas::desarmar(Una& u) {
    if (u.pipeline == nullptr) return;
    GstElement* p = u.pipeline;
    u.pipeline = nullptr;
    // Sin el lock: el callback del cuadro lo toma (set_state espera a que termine).
    mu_.unlock();
    gst_element_set_state(p, GST_STATE_NULL);
    mu_.lock();
    gst_object_unref(p);
}

std::string Vistas::pedir(const std::string& clave, int ancho, int alto) {
    std::lock_guard<std::mutex> l(mu_);
    Una*& u = vistas_[clave];
    if (u == nullptr) u = new Una();
    ++u->pedidos;
    ancho = std::clamp(ancho, 16, 3840) & ~1;
    alto = std::clamp(alto, 16, 2160) & ~1;
    if (u->pipeline == nullptr || u->ancho != ancho || u->alto != alto) {
        u->ancho = ancho;
        u->alto = alto;
        armar(clave, *u);
    }
    return u->pipeline != nullptr ? std::string() : "no hay nada para ver en «" + clave + "»";
}

void Vistas::soltar(const std::string& clave) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = vistas_.find(clave);
    if (it == vistas_.end()) return;
    if (--it->second->pedidos > 0) return;
    Una* u = it->second;
    desarmar(*u);
    vistas_.erase(clave);
    delete u;
}

bool Vistas::ultimo(const std::string& clave, Cuadro& out, std::uint64_t& serie) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = vistas_.find(clave);
    if (it == vistas_.end() || it->second->cuadro.serie <= serie) return false;
    out = it->second->cuadro;
    serie = out.serie;
    return true;
}

void Vistas::tic() {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& [clave, u] : vistas_) {
        // Lo que mira cambió de latencia, o todavía no existía (se pidió antes de armarse).
        std::int64_t lat = 0;
        const std::string prod = resolver_(base(clave), &lat);
        if (u->pipeline == nullptr && !prod.empty()) {
            armar(clave, *u);
            continue;
        }
        if (u->pipeline != nullptr && (lat > u->latencia + static_cast<std::int64_t>(5 * GST_MSECOND) ||
                                       lat < u->latencia - static_cast<std::int64_t>(60 * GST_MSECOND))) {
            u->latencia = lat;
            gst_pipeline_set_latency(GST_PIPELINE(u->pipeline), static_cast<GstClockTime>(lat + kMargenNs));
            gst_bin_recalculate_latency(GST_BIN(u->pipeline));
        }
        if (u->pipeline != nullptr) {
            GstBus* bus = gst_element_get_bus(u->pipeline);
            while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
            gst_object_unref(bus);
        }
    }
}

json Vistas::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json o = json::object();
    for (const auto& [k, u] : vistas_)
        o[k] = {{"pedidos", u->pedidos}, {"ancho", u->ancho}, {"alto", u->alto}, {"anda", u->pipeline != nullptr}, {"serie", u->cuadro.serie}};
    return o;
}

} // namespace ba::motor
