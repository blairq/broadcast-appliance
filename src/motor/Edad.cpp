// ============================================================================
// src/motor/Edad.cpp — ver Edad.hpp
// ============================================================================
#include "Edad.hpp"

#include "Reloj.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace ba::motor {

using json = nlohmann::json;

struct MedidorEdad::Datos {
    const Reloj* reloj = nullptr;
    std::mutex mu;
    std::deque<double> edades; ///< ms, las últimas
    std::atomic<std::int64_t> plazoNs{0};
    std::deque<std::int64_t> tardes; ///< cuándo (µs monotónicos) pasó uno el plazo, del último minuto
};

namespace {

constexpr std::size_t kMuestras = 600; ///< ~10 s de video a 60, menos de audio en bloques de 10 ms

GstPadProbeReturn alPasar(GstPad* pad, GstPadProbeInfo* info, gpointer u) {
    auto* d = static_cast<std::shared_ptr<MedidorEdad::Datos>*>(u)->get();
    GstBuffer* b = nullptr;
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) b = GST_PAD_PROBE_INFO_BUFFER(info);
    else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList* l = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        if (l != nullptr && gst_buffer_list_length(l) > 0) b = gst_buffer_list_get(l, 0);
    }
    if (b == nullptr) return GST_PAD_PROBE_OK;
    // Lo muxeado puede traer solo DTS.
    const GstClockTime t = GST_BUFFER_PTS_IS_VALID(b) ? GST_BUFFER_PTS(b) : GST_BUFFER_DTS(b);
    if (!GST_CLOCK_TIME_IS_VALID(t)) return GST_PAD_PROBE_OK;
    GstEvent* ev = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
    if (ev == nullptr) return GST_PAD_PROBE_OK;
    const GstSegment* seg = nullptr;
    gst_event_parse_segment(ev, &seg);
    GstClockTime rt = GST_CLOCK_TIME_NONE;
    if (seg != nullptr && seg->format == GST_FORMAT_TIME) rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, t);
    gst_event_unref(ev);
    if (!GST_CLOCK_TIME_IS_VALID(rt) || d->reloj == nullptr) return GST_PAD_PROBE_OK;
    const double edad = (static_cast<double>(d->reloj->ahora()) - static_cast<double>(rt)) / 1e6;
    std::lock_guard<std::mutex> l(d->mu);
    d->edades.push_back(edad);
    while (d->edades.size() > kMuestras) d->edades.pop_front();
    const std::int64_t plazo = d->plazoNs.load(std::memory_order_relaxed);
    if (plazo > 0 && edad * 1e6 > static_cast<double>(plazo)) {
        const std::int64_t ahora = g_get_monotonic_time();
        d->tardes.push_back(ahora);
        while (!d->tardes.empty() && ahora - d->tardes.front() > 60'000'000) d->tardes.pop_front();
    }
    return GST_PAD_PROBE_OK;
}

} // namespace

void MedidorEdad::ponerPlazo(std::int64_t ns) {
    plazoNs_ = ns;
    if (d_) d_->plazoNs = ns;
}

int MedidorEdad::tarde60s() const {
    if (!d_) return 0;
    std::lock_guard<std::mutex> l(d_->mu);
    const std::int64_t ahora = g_get_monotonic_time();
    int n = 0;
    for (const std::int64_t t : d_->tardes)
        if (ahora - t <= 60'000'000) ++n;
    return n;
}

void MedidorEdad::poner(GstPad* pad, const Reloj& reloj) {
    sacar();
    if (pad == nullptr) return;
    d_ = std::make_shared<Datos>();
    d_->plazoNs = plazoNs_;
    d_->reloj = &reloj;
    pad_ = GST_PAD(gst_object_ref(pad));
    id_ = gst_pad_add_probe(pad_, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                            alPasar, new std::shared_ptr<Datos>(d_),
                            [](gpointer u) { delete static_cast<std::shared_ptr<Datos>*>(u); });
}

void MedidorEdad::ponerEnEntrada(GstElement* elemento, const Reloj& reloj) {
    if (elemento == nullptr) return;
    GstPad* elegido = nullptr;
    GstIterator* it = gst_element_iterate_sink_pads(elemento);
    GValue v = G_VALUE_INIT;
    bool listo = false;
    while (!listo) {
        switch (gst_iterator_next(it, &v)) {
            case GST_ITERATOR_OK: {
                auto* p = GST_PAD(g_value_get_object(&v));
                // Uno de video si hay (un mux tiene video y audio); si no, el primero.
                GstCaps* c = gst_pad_get_current_caps(p);
                const bool video = c != nullptr && gst_caps_get_size(c) > 0 &&
                                   g_str_has_prefix(gst_structure_get_name(gst_caps_get_structure(c, 0)), "video/");
                if (c != nullptr) gst_caps_unref(c);
                if (elegido == nullptr || video) {
                    if (elegido != nullptr) gst_object_unref(elegido);
                    elegido = GST_PAD(gst_object_ref(p));
                }
                g_value_reset(&v);
                break;
            }
            case GST_ITERATOR_RESYNC: gst_iterator_resync(it); break;
            default: listo = true; break;
        }
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    if (elegido != nullptr) {
        poner(elegido, reloj);
        gst_object_unref(elegido);
    }
}

void MedidorEdad::sacar() {
    if (pad_ != nullptr) {
        if (id_ != 0) gst_pad_remove_probe(pad_, id_);
        gst_object_unref(pad_);
    }
    pad_ = nullptr;
    id_ = 0;
    // d_ queda: lo medido sigue valiendo hasta que se ponga otro.
}

json MedidorEdad::json() const {
    if (!d_) return nullptr;
    std::vector<double> v;
    {
        std::lock_guard<std::mutex> l(d_->mu);
        v.assign(d_->edades.begin(), d_->edades.end());
    }
    if (v.empty()) return nullptr;
    std::sort(v.begin(), v.end());
    const auto p = [&](double q) { return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))]; };
    const auto r = [](double x) { return static_cast<double>(static_cast<long long>(x * 10 + (x >= 0 ? 0.5 : -0.5))) / 10; };
    return {{"p50_ms", r(p(0.5))}, {"p95_ms", r(p(0.95))}, {"n", v.size()}, {"tarde_60s", tarde60s()}};
}

} // namespace ba::motor
