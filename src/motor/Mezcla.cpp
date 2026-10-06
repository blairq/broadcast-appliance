// ============================================================================
// src/motor/Mezcla.cpp — ver Mezcla.hpp
// ============================================================================
#include "Mezcla.hpp"

#include "Piezas.hpp"
#include "Reloj.hpp"

#include <algorithm>
#include <cmath>

namespace ba::motor {

using json = nlohmann::json;

namespace {

std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

/// El mayor de los canales de un mensaje de `level` (dBFS).
double mayor(const GstStructure* s, const char* campo) {
    const GValue* v = gst_structure_get_value(s, campo);
    if (v == nullptr) return -120;
    double m = -120;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    if (G_VALUE_HOLDS(v, G_TYPE_VALUE_ARRAY)) {
        auto* a = static_cast<GValueArray*>(g_value_get_boxed(v));
        for (guint i = 0; a != nullptr && i < a->n_values; ++i)
            m = std::max(m, g_value_get_double(g_value_array_get_nth(a, i)));
    } else if (GST_VALUE_HOLDS_ARRAY(v)) {
        for (guint i = 0; i < gst_value_array_get_size(v); ++i)
            m = std::max(m, g_value_get_double(gst_value_array_get_value(v, i)));
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
    return std::isfinite(m) ? std::max(m, -120.0) : -120.0;
}

std::string nombreSeguro(const std::string& id) {
    std::string n = id;
    for (char& c : n)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return n;
}

} // namespace

Mezcla::Mezcla(Reloj& reloj, std::string nombre) : reloj_(reloj), nombre_(std::move(nombre)) {
    const std::string desc =
        "audiomixer name=mx force-live=true start-time-selection=first output-buffer-duration=10000000 ! "
        "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2 ! "
        "level name=nivel interval=50000000 post-messages=true peak-ttl=300000000 ! "
        "intersink name=sale producer-name=" +
        entreComillas(productor()) + " sync=false";
    GError* e = nullptr;
    pipeline_ = gst_parse_launch(desc.c_str(), &e);
    if (e != nullptr) {
        error_ = e->message;
        g_error_free(e);
        if (pipeline_ != nullptr) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        registrar(Nivel::Error, "mezcla " + nombre_ + ": " + error_);
        return;
    }
    mezclador_ = gst_bin_get_by_name(GST_BIN(pipeline_), "mx");
    reloj_.adoptar(pipeline_);
    aplicarLatencia();
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "el mezclador no arrancó";
        registrar(Nivel::Error, "mezcla " + nombre_ + ": " + error_);
    }
}

Mezcla::~Mezcla() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ != nullptr) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        for (auto& [id, r] : ramas_)
            if (r.pad != nullptr) {
                gst_element_release_request_pad(mezclador_, r.pad);
                gst_object_unref(r.pad);
            }
        ramas_.clear();
        gst_object_unref(mezclador_);
        gst_object_unref(pipeline_);
    }
}

void Mezcla::aplicarLatencia() {
    // La del estudio más el margen; sube enseguida, baja solo de a mucho (cada
    // cambio corre el audio).
    const std::int64_t l = reloj_.latencia() + kMargenMezclaNs;
    if (latenciaNs_ >= 0 && l <= latenciaNs_ && l > latenciaNs_ - 60 * static_cast<std::int64_t>(GST_MSECOND)) return;
    latenciaNs_ = l;
    g_object_set(mezclador_, "min-upstream-latency", static_cast<guint64>(l), nullptr);
    gst_pipeline_set_latency(GST_PIPELINE(pipeline_), static_cast<GstClockTime>(l));
    gst_bin_recalculate_latency(GST_BIN(pipeline_));
}

void Mezcla::agregar(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr || ramas_.count(id) > 0) return;
    const std::string n = nombreSeguro(id);
    Rama r;
    r.src = gst_element_factory_make("intersrc", ("src_" + n).c_str());
    r.cola = gst_element_factory_make("queue", ("cola_" + n).c_str());
    r.vol = gst_element_factory_make("volume", ("vol_" + n).c_str());
    r.nivel = gst_element_factory_make("level", ("nivel_" + n).c_str());
    if (r.src == nullptr || r.cola == nullptr || r.vol == nullptr || r.nivel == nullptr) {
        for (GstElement* e : {r.src, r.cola, r.vol, r.nivel})
            if (e != nullptr) gst_object_unref(e);
        error_ = "falta intersrc, queue, volume o level";
        registrar(Nivel::Error, "mezcla " + nombre_ + ": " + error_);
        return;
    }
    g_object_set(r.src, "producer-name", (id + ":audio").c_str(), nullptr);
    // La cola no agrega demora: lo que no entra se tira (una entrada trabada no traba el bus).
    g_object_set(r.cola, "max-size-time", static_cast<guint64>(GST_SECOND), "max-size-buffers", 0u, "max-size-bytes", 0u,
                 nullptr);
    g_object_set(r.nivel, "interval", static_cast<guint64>(50 * GST_MSECOND), "post-messages", TRUE, "peak-ttl",
                 static_cast<guint64>(300 * GST_MSECOND), nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), r.src, r.cola, r.vol, r.nivel, nullptr);
    gst_element_link_many(r.src, r.cola, r.vol, r.nivel, nullptr);
    r.pad = gst_element_request_pad_simple(mezclador_, "sink_%u");
    GstPad* sale = gst_element_get_static_pad(r.nivel, "src");
    gst_pad_link(sale, r.pad);
    gst_object_unref(sale);
    for (GstElement* e : {r.nivel, r.vol, r.cola, r.src}) gst_element_sync_state_with_parent(e);
    ramas_[id] = r;
}

void Mezcla::soltar(Rama& r) {
    // Se para la rama de atrás para adelante y se suelta el pad del mezclador.
    for (GstElement* e : {r.src, r.cola, r.vol, r.nivel}) gst_element_set_state(e, GST_STATE_NULL);
    if (r.pad != nullptr) {
        gst_element_release_request_pad(mezclador_, r.pad);
        gst_object_unref(r.pad);
        r.pad = nullptr;
    }
    gst_bin_remove_many(GST_BIN(pipeline_), r.src, r.cola, r.vol, r.nivel, nullptr);
}

void Mezcla::quitar(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ramas_.find(id);
    if (it == ramas_.end()) return;
    soltar(it->second);
    ramas_.erase(it);
}

bool Mezcla::tiene(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    return ramas_.count(id) > 0;
}

void Mezcla::ponerNivel(const std::string& id, double lineal) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ramas_.find(id);
    if (it == ramas_.end()) return;
    it->second.fader = std::clamp(lineal, 0.0, 4.0);
    g_object_set(it->second.vol, "volume", it->second.fader, nullptr);
}

void Mezcla::ponerMute(const std::string& id, bool mute) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ramas_.find(id);
    if (it == ramas_.end()) return;
    it->second.mute = mute;
    g_object_set(it->second.vol, "mute", mute ? TRUE : FALSE, nullptr);
}

void Mezcla::ponerEscena(const std::string& id, double ganancia, int ms) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = ramas_.find(id);
    if (it == ramas_.end()) return;
    Rama& r = it->second;
    r.escenaDesde = r.escena;
    r.escenaHacia = std::clamp(ganancia, 0.0, 1.0);
    r.fundidoDesdeNs = ahoraNs();
    r.fundidoNs = std::max(0, ms) * static_cast<std::int64_t>(1000000);
    if (r.fundidoNs == 0) {
        r.escena = r.escenaHacia;
        g_object_set(r.pad, "volume", r.escena, nullptr);
    }
}

void Mezcla::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    aplicarLatencia();
    // Los fundidos de escena (curva de igual potencia: sin bache en el medio).
    const std::int64_t ahora = ahoraNs();
    for (auto& [id, r] : ramas_) {
        if (r.fundidoNs <= 0 || r.escena == r.escenaHacia) continue;
        const double t = std::clamp(static_cast<double>(ahora - r.fundidoDesdeNs) / static_cast<double>(r.fundidoNs), 0.0, 1.0);
        const double sube = std::sin(t * M_PI / 2), baja = std::cos(t * M_PI / 2);
        r.escena = r.escenaHacia > r.escenaDesde ? r.escenaDesde + (r.escenaHacia - r.escenaDesde) * sube
                                                 : r.escenaHacia + (r.escenaDesde - r.escenaHacia) * baja;
        if (t >= 1.0) r.escena = r.escenaHacia;
        g_object_set(r.pad, "volume", r.escena, nullptr);
    }
    // Los medidores y los avisos del bus.
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(m)) {
            case GST_MESSAGE_ELEMENT: {
                const GstStructure* s = gst_message_get_structure(m);
                if (s != nullptr && gst_structure_has_name(s, "level")) {
                    const std::string src = GST_OBJECT_NAME(GST_MESSAGE_SRC(m));
                    const double pico = mayor(s, "peak"), rms = mayor(s, "rms");
                    if (src == "nivel") {
                        picoDb_ = pico;
                        rmsDb_ = rms;
                    } else {
                        for (auto& [id, r] : ramas_)
                            if (r.nivel != nullptr && GST_OBJECT_NAME(r.nivel) == src) {
                                r.picoDb = pico;
                                r.rmsDb = rms;
                            }
                    }
                }
                break;
            }
            case GST_MESSAGE_LATENCY: gst_bin_recalculate_latency(GST_BIN(pipeline_)); break;
            case GST_MESSAGE_ERROR: {
                GError* e = nullptr;
                gst_message_parse_error(m, &e, nullptr);
                error_ = e != nullptr ? e->message : "error";
                registrar(Nivel::Error, "mezcla " + nombre_ + " (" + GST_OBJECT_NAME(GST_MESSAGE_SRC(m)) + "): " + error_);
                if (e != nullptr) g_error_free(e);
                break;
            }
            case GST_MESSAGE_WARNING: {
                GError* e = nullptr;
                gst_message_parse_warning(m, &e, nullptr);
                if (e != nullptr) {
                    registrar(Nivel::Info, "mezcla " + nombre_ + " (aviso de " + GST_OBJECT_NAME(GST_MESSAGE_SRC(m)) + "): " + e->message);
                    g_error_free(e);
                }
                break;
            }
            default: break;
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

double Mezcla::picoDb() const {
    std::lock_guard<std::mutex> l(mu_);
    return picoDb_;
}

json Mezcla::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json e = json::object();
    for (const auto& [id, r] : ramas_)
        e[id] = {{"nivel", r.fader}, {"mute", r.mute}, {"escena", r.escena}, {"pico_db", r.picoDb}, {"rms_db", r.rmsDb}};
    return {{"bus", nombre_},
            {"anda", pipeline_ != nullptr},
            {"error", error_},
            {"latencia_ms", static_cast<double>(latenciaNs_) / 1e6},
            {"pico_db", picoDb_},
            {"rms_db", rmsDb_},
            {"entradas", e}};
}

} // namespace ba::motor
