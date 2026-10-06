// ============================================================================
// src/motor/Composicion.cpp — ver Composicion.hpp
// ============================================================================
#include "Composicion.hpp"

#include "Mezcla.hpp" // kMargenMezclaNs
#include "Reloj.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ba::motor {

using json = nlohmann::json;

namespace {
std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

std::string nombreSeguro(const std::string& s) {
    std::string n = s;
    for (char& c : n)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return n;
}

constexpr std::int64_t kEsperaCorteNs = 700 * GST_MSECOND; ///< corte: lo más que se espera el primer cuadro de la nueva
} // namespace

// --- Escena ---------------------------------------------------------------------------

Escena Escena::desdeJson(const nlohmann::json& j) {
    Escena e;
    e.nombre = j.value("nombre", std::string());
    for (const auto& c : j.value("capas", json::array())) {
        if (!c.is_object()) continue;
        Capa k;
        k.entrada = c.value("entrada", std::string());
        k.productor = c.value("productor", std::string());
        k.x = c.value("x", 0);
        k.y = c.value("y", 0);
        k.ancho = c.value("ancho", 0);
        k.alto = c.value("alto", 0);
        k.alfa = c.value("alfa", 1.0);
        k.orden = c.value("orden", 0);
        const json r = c.value("recorte", json::object());
        k.recIzq = r.value("izq", 0);
        k.recArr = r.value("arr", 0);
        k.recDer = r.value("der", 0);
        k.recAba = r.value("aba", 0);
        k.visible = c.value("visible", true);
        if (!k.entrada.empty()) e.capas.push_back(k);
    }
    return e;
}

nlohmann::json Escena::aJson() const {
    nlohmann::json capas = nlohmann::json::array();
    for (const auto& c : this->capas)
        capas.push_back({{"entrada", c.entrada},
                         {"x", c.x},
                         {"y", c.y},
                         {"ancho", c.ancho},
                         {"alto", c.alto},
                         {"alfa", c.alfa},
                         {"orden", c.orden},
                         {"recorte", {{"izq", c.recIzq}, {"arr", c.recArr}, {"der", c.recDer}, {"aba", c.recAba}}},
                         {"visible", c.visible}});
    return {{"nombre", nombre}, {"capas", capas}};
}

// --- Composición ---------------------------------------------------------------------

Composicion::Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre, Opciones op)
    : reloj_(reloj), lienzo_(lienzo), nombre_(std::move(nombre)), gpu_(op.gpu && hayFabrica("glvideomixer")) {
    const std::string tam = "width=" + std::to_string(lienzo_.ancho) + ",height=" + std::to_string(lienzo_.alto) +
                            ",framerate=" + std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD) +
                            ",pixel-aspect-ratio=1/1";
    const std::string comun = "force-live=true start-time-selection=first background=black ";
    const std::string desc =
        gpu_ ? "glvideomixer name=mx " + comun + "! video/x-raw(memory:GLMemory)," + tam + " ! gldownload ! videoconvert ! " +
                   capsColorDelLienzo(lienzo_) + "," + tam
             : "compositor name=mx " + comun + "! video/x-raw," + tam + " ! videoconvert ! " + capsColorDelLienzo(lienzo_) +
                   "," + tam;
    GError* e = nullptr;
    pipeline_ = gst_parse_launch((desc + " ! intersink name=sale producer-name=" + entreComillas(productor()) +
                                  " sync=false")
                                     .c_str(),
                                 &e);
    if (e != nullptr) {
        error_ = e->message;
        g_error_free(e);
        if (pipeline_ != nullptr) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return;
    }
    mezclador_ = gst_bin_get_by_name(GST_BIN(pipeline_), "mx");
    reloj_.adoptar(pipeline_);
    aplicarLatencia();
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "el compositor no arrancó";
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
    }
}

Composicion::~Composicion() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    for (auto& [k, r] : ramas_) {
        if (r->pad != nullptr) {
            gst_element_release_request_pad(mezclador_, r->pad);
            gst_object_unref(r->pad);
        }
        delete r;
    }
    ramas_.clear();
    gst_object_unref(mezclador_);
    gst_object_unref(pipeline_);
}

void Composicion::aplicarLatencia() {
    const std::int64_t l = reloj_.latencia() + kMargenMezclaNs;
    if (latenciaNs_ >= 0 && l <= latenciaNs_ && l > latenciaNs_ - 60 * static_cast<std::int64_t>(GST_MSECOND)) return;
    latenciaNs_ = l;
    g_object_set(mezclador_, "min-upstream-latency", static_cast<guint64>(l), nullptr);
    gst_bin_recalculate_latency(GST_BIN(pipeline_));
}

GstPadProbeReturn Composicion::alPrimerCuadro(GstPad*, GstPadProbeInfo*, gpointer d) {
    auto* r = static_cast<Rama*>(d);
    r->listo = true;
    return GST_PAD_PROBE_REMOVE;
}

Composicion::Rama* Composicion::crearRama(const Capa& c, int gen) {
    const std::string n = nombreSeguro(std::to_string(gen) + "_" + c.entrada);
    auto* r = new Rama();
    r->entrada = c.entrada;
    r->productor = c.productor.empty() ? c.entrada + ":video" : c.productor;
    r->generacion = gen;
    r->src = gst_element_factory_make("intersrc", ("src_" + n).c_str());
    r->cola = gst_element_factory_make("queue", ("cola_" + n).c_str());
    r->recorte = gst_element_factory_make("videocrop", ("rec_" + n).c_str());
    if (gpu_) r->subir = gst_element_factory_make("glupload", ("gl_" + n).c_str());
    if (r->src == nullptr || r->cola == nullptr || r->recorte == nullptr || (gpu_ && r->subir == nullptr)) {
        for (GstElement* e : {r->src, r->cola, r->recorte, r->subir})
            if (e != nullptr) gst_object_unref(e);
        delete r;
        error_ = "falta intersrc, queue, videocrop o glupload";
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return nullptr;
    }
    g_object_set(r->src, "producer-name", r->productor.c_str(), nullptr);
    // Sin demora propia: si se atrasa, se tira lo viejo (el compositor espera la latencia).
    g_object_set(r->cola, "max-size-buffers", 3u, "max-size-time", static_cast<guint64>(0), "max-size-bytes", 0u, "leaky", 2,
                 nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), r->src, r->cola, r->recorte, nullptr);
    if (r->subir != nullptr) gst_bin_add(GST_BIN(pipeline_), r->subir);
    if (r->subir != nullptr)
        gst_element_link_many(r->src, r->cola, r->recorte, r->subir, nullptr);
    else
        gst_element_link_many(r->src, r->cola, r->recorte, nullptr);
    r->pad = gst_element_request_pad_simple(mezclador_, "sink_%u");
    GstPad* sale = gst_element_get_static_pad(r->subir != nullptr ? r->subir : r->recorte, "src");
    gst_pad_link(sale, r->pad);
    gst_pad_add_probe(sale, GST_PAD_PROBE_TYPE_BUFFER, &Composicion::alPrimerCuadro, r, nullptr);
    gst_object_unref(sale);
    for (GstElement* e : {r->subir, r->recorte, r->cola, r->src})
        if (e != nullptr) gst_element_sync_state_with_parent(e);
    ramas_[std::to_string(gen) + "/" + c.entrada] = r;
    return r;
}

void Composicion::ponerCapa(Rama& r, const Capa& c, double alfa) {
    const int w = c.ancho > 0 ? c.ancho : lienzo_.ancho;
    const int h = c.alto > 0 ? c.alto : lienzo_.alto;
    // Las nuevas (generación más alta) van arriba de las viejas: así la mezcla
    // pasa por encima.
    const guint zorder = static_cast<guint>(std::clamp(r.generacion % 1000, 0, 999) * 1000 + std::clamp(c.orden, 0, 999));
    g_object_set(r.pad, "xpos", c.x, "ypos", c.y, "width", w, "height", h, "alpha", c.visible ? alfa : 0.0, "zorder",
                 zorder, nullptr);
    g_object_set(r.recorte, "left", std::max(0, c.recIzq), "top", std::max(0, c.recArr), "right", std::max(0, c.recDer),
                 "bottom", std::max(0, c.recAba), nullptr);
    r.alfaHacia = c.visible ? c.alfa : 0.0;
    r.orden = c.orden;
}

void Composicion::soltar(Rama& r) {
    for (GstElement* e : {r.src, r.cola, r.recorte, r.subir})
        if (e != nullptr) gst_element_set_state(e, GST_STATE_NULL);
    if (r.pad != nullptr) {
        gst_element_release_request_pad(mezclador_, r.pad);
        gst_object_unref(r.pad);
        r.pad = nullptr;
    }
    gst_bin_remove_many(GST_BIN(pipeline_), r.src, r.cola, r.recorte, nullptr);
    if (r.subir != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.subir);
}

void Composicion::sacarGeneracion(int gen) {
    for (auto it = ramas_.begin(); it != ramas_.end();) {
        if (it->second->generacion == gen) {
            soltar(*it->second);
            delete it->second;
            it = ramas_.erase(it);
        } else {
            ++it;
        }
    }
}

void Composicion::mostrar(const Escena& e, const std::string& transicion, int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    // La misma escena (y sin transición en curso): se actualiza en el lugar.
    if (e.nombre == escena_.nombre && genVieja_ < 0 && generacion_ > 0) {
        std::map<std::string, const Capa*> nuevas;
        for (const auto& c : e.capas) nuevas[c.entrada] = &c;
        for (auto it = ramas_.begin(); it != ramas_.end();) {
            auto n = nuevas.find(it->second->entrada);
            const std::string prodNuevo =
                n == nuevas.end() ? std::string() : (n->second->productor.empty() ? n->second->entrada + ":video" : n->second->productor);
            if (n == nuevas.end() || prodNuevo != it->second->productor) { // se fue, o ahora lee otra cosa
                soltar(*it->second);
                delete it->second;
                it = ramas_.erase(it);
                continue;
            }
            ponerCapa(*it->second, *n->second, n->second->alfa);
            nuevas.erase(n);
            ++it;
        }
        for (const auto& [ent, c] : nuevas)
            if (Rama* r = crearRama(*c, generacion_)) ponerCapa(*r, *c, c->alfa);
        escena_ = e;
        return;
    }
    // Una transición que no terminó: lo viejo se va ya.
    if (genVieja_ >= 0) {
        sacarGeneracion(genVieja_);
        genVieja_ = -1;
    }
    const int vieja = generacion_;
    ++generacion_;
    const bool mezcla = transicion == "mezcla" && ms > 0 && vieja > 0;
    for (const auto& c : e.capas)
        if (Rama* r = crearRama(c, generacion_)) ponerCapa(*r, c, mezcla ? 0.0 : c.alfa);
    escena_ = e;
    if (vieja > 0) {
        genVieja_ = vieja;
        transicion_ = mezcla ? "mezcla" : "corte";
        transDesdeNs_ = ahoraNs();
        transNs_ = mezcla ? static_cast<std::int64_t>(ms) * 1000000 : kEsperaCorteNs;
    }
}

bool Composicion::enTransicion() const {
    std::lock_guard<std::mutex> l(mu_);
    return genVieja_ >= 0;
}

void Composicion::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return;
    aplicarLatencia();
    if (ahoraNs() - consultaNs_ > 1'000'000'000LL) {
        consultaNs_ = ahoraNs();
        GstQuery* q = gst_query_new_latency();
        if (gst_element_query(pipeline_, q)) {
            gboolean vivo = FALSE;
            GstClockTime min = 0, max = 0;
            gst_query_parse_latency(q, &vivo, &min, &max);
            if (GST_CLOCK_TIME_IS_VALID(min)) salidaNs_ = static_cast<std::int64_t>(min);
        }
        gst_query_unref(q);
    }
    // La transición.
    if (genVieja_ >= 0) {
        const double t = std::clamp(static_cast<double>(ahoraNs() - transDesdeNs_) / static_cast<double>(std::max<std::int64_t>(1, transNs_)), 0.0, 1.0);
        bool nuevasListas = true;
        for (auto& [k, r] : ramas_)
            if (r->generacion == generacion_) {
                const bool listo = r->listo.load();
                nuevasListas = nuevasListas && listo;
                if (transicion_ == "mezcla") g_object_set(r->pad, "alpha", r->alfaHacia * t, nullptr);
            }
        const bool termino = transicion_ == "mezcla" ? t >= 1.0 : (nuevasListas || t >= 1.0);
        if (termino) {
            for (auto& [k, r] : ramas_)
                if (r->generacion == generacion_) g_object_set(r->pad, "alpha", r->alfaHacia, nullptr);
            sacarGeneracion(genVieja_);
            genVieja_ = -1;
        }
    }
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? e->message : "error";
            registrar(Nivel::Error, "composición " + nombre_ + " (" + GST_OBJECT_NAME(GST_MESSAGE_SRC(m)) + "): " + error_);
            if (e != nullptr) g_error_free(e);
        } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_LATENCY) {
            gst_bin_recalculate_latency(GST_BIN(pipeline_));
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

std::int64_t Composicion::latenciaSalidaNs() const {
    std::lock_guard<std::mutex> l(mu_);
    return std::max(salidaNs_, latenciaNs_);
}

json Composicion::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json capas = json::array();
    for (const auto& [k, r] : ramas_)
        capas.push_back({{"entrada", r->entrada},
                         {"generacion", r->generacion},
                         {"llega", r->listo.load()}});
    return {{"bus", nombre_},
            {"anda", pipeline_ != nullptr},
            {"gpu", gpu_},
            {"error", error_},
            {"escena", escena_.nombre},
            {"en_transicion", genVieja_ >= 0},
            {"latencia_ms", static_cast<double>(latenciaNs_) / 1e6},
            {"latencia_salida_ms", static_cast<double>(std::max(salidaNs_, latenciaNs_)) / 1e6},
            {"capas", capas}};
}

} // namespace ba::motor
