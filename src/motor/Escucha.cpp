// ============================================================================
// src/motor/Escucha.cpp — ver Escucha.hpp
// ============================================================================
#include "Escucha.hpp"
#include "diag/Utf8.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <algorithm>
#include <cmath>

namespace ba::motor {

using json = nlohmann::json;

namespace {

/// El sink de audio del sistema, con el dispositivo pedido.
std::string sinkDe(const std::string& id) {
#if defined(__APPLE__)
    return "osxaudiosink name=sale" + (id.empty() ? std::string() : " unique-id=" + entreComillas(id));
#elif defined(_WIN32)
    return "wasapi2sink name=sale" + (id.empty() ? std::string() : " device=" + entreComillas(id));
#else
    return "pulsesink name=sale" + (id.empty() ? std::string() : " device=" + entreComillas(id));
#endif
}

constexpr std::int64_t kMargenNs = 40 * GST_MSECOND; // lo que tarda el propio sink en arrancar a escribir

} // namespace

Escucha::Escucha(Reloj& reloj, std::string sinkPrueba) : reloj_(reloj), sinkPrueba_(std::move(sinkPrueba)) {}

Escucha::~Escucha() {
    std::lock_guard<std::mutex> l(mu_);
    desarmar();
}

void Escucha::desarmar() {
    if (pipeline_ == nullptr) return;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
}

void Escucha::armar() {
    desarmar();
    error_.clear();
    if (productor_.empty()) return;
    const std::string cola = "intersrc producer-name=" + entreComillas(productor_) +
                             " ! queue max-size-time=2000000000 max-size-buffers=0 max-size-bytes=0 ! audioconvert ! "
                             "audioresample ! volume name=vol ! level name=nivel interval=50000000 peak-ttl=300000000 ! ";
    for (const std::string& disp : {dispositivo_, std::string()}) {
        GError* e = nullptr;
        const std::string sink = sinkPrueba_.empty() ? sinkDe(disp) : sinkPrueba_ + " name=sale";
        GstElement* p = gst_parse_launch((cola + sink).c_str(), &e);
        if (e != nullptr) {
            error_ = ba::diag::utf8Valido(e->message);
            g_error_free(e);
            if (p != nullptr) gst_object_unref(p);
            continue;
        }
        if (GstElement* v = gst_bin_get_by_name(GST_BIN(p), "vol")) {
            g_object_set(v, "volume", nivel_, nullptr);
            gst_object_unref(v);
        }
        sinRelojPropio(p); // el reloj es el del estudio, no el de la placa de audio
        reloj_.adoptar(p);
        gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(latenciaNs_ + kMargenNs));
        if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            error_ = disp.empty() ? "la salida de audio no arrancó" : "el dispositivo «" + disp + "» no arrancó";
            gst_element_set_state(p, GST_STATE_NULL);
            gst_object_unref(p);
            continue;
        }
        if (!disp.empty() || dispositivo_.empty()) error_.clear();
        else error_ = "no está «" + dispositivo_ + "»: va por la salida del sistema";
        pipeline_ = p;
        break;
    }
    if (!error_.empty()) registrar(Nivel::Aviso, "escucha: " + error_);
}

std::string Escucha::escuchar(const std::string& productor, std::int64_t latenciaNs) {
    std::lock_guard<std::mutex> l(mu_);
    productor_ = productor;
    latenciaNs_ = std::max<std::int64_t>(0, latenciaNs);
    armar();
    return pipeline_ != nullptr ? std::string() : error_;
}

void Escucha::apagar() {
    std::lock_guard<std::mutex> l(mu_);
    productor_.clear();
    desarmar();
}

void Escucha::ponerDispositivo(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    if (id == dispositivo_) return;
    dispositivo_ = id;
    armar();
}

void Escucha::ponerNivel(double lineal) {
    std::lock_guard<std::mutex> l(mu_);
    nivel_ = std::clamp(lineal, 0.0, 4.0);
    if (pipeline_ == nullptr) return;
    if (GstElement* v = gst_bin_get_by_name(GST_BIN(pipeline_), "vol")) {
        g_object_set(v, "volume", nivel_, nullptr);
        gst_object_unref(v);
    }
}

void Escucha::tic(std::int64_t latenciaNs) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    // Lo escuchado tarda más (o bastante menos): se acomoda la espera.
    if (latenciaNs > latenciaNs_ + static_cast<std::int64_t>(5 * GST_MSECOND) ||
        latenciaNs < latenciaNs_ - static_cast<std::int64_t>(60 * GST_MSECOND)) {
        latenciaNs_ = latenciaNs;
        gst_pipeline_set_latency(GST_PIPELINE(pipeline_), static_cast<GstClockTime>(latenciaNs_ + kMargenNs));
        recalcularLatenciaAparte(pipeline_);
    }
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ELEMENT) {
            const GstStructure* s = gst_message_get_structure(m);
            if (s != nullptr && gst_structure_has_name(s, "level")) {
                const GValue* v = gst_structure_get_value(s, "peak");
                double p = -120;
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                if (v != nullptr && G_VALUE_HOLDS(v, G_TYPE_VALUE_ARRAY)) {
                    auto* a = static_cast<GValueArray*>(g_value_get_boxed(v));
                    for (guint i = 0; a != nullptr && i < a->n_values; ++i)
                        p = std::max(p, g_value_get_double(g_value_array_get_nth(a, i)));
                }
                G_GNUC_END_IGNORE_DEPRECATIONS
                picoDb_ = std::isfinite(p) ? std::max(p, -120.0) : -120.0;
            }
        } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : "error";
            if (e != nullptr) g_error_free(e);
            registrar(Nivel::Aviso, "escucha: " + error_);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

json Escucha::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    return {{"escucha", productor_},
            {"anda", pipeline_ != nullptr},
            {"dispositivo", dispositivo_},
            {"nivel", nivel_},
            {"pico_db", picoDb_},
            {"latencia_ms", static_cast<double>(latenciaNs_ + kMargenNs) / 1e6},
            {"error", error_}};
}

json Escucha::dispositivos() {
    json out = json::array();
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Audio/Sink", nullptr);
    if (gst_device_monitor_start(mon)) {
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr; l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            GstElement* e = gst_device_create_element(dev, nullptr);
            const std::string id = idDeMicrofono(e); // la misma propiedad: unique-id / device
            gchar* n = gst_device_get_display_name(dev);
            if (!id.empty()) out.push_back({{"id", id}, {"nombre", n != nullptr ? n : id}});
            g_free(n);
            if (e != nullptr) gst_object_unref(e);
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
    return out;
}

} // namespace ba::motor
