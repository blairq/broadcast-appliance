// ============================================================================
// src/motor/Composicion.cpp — ver Composicion.hpp
// ============================================================================
#include "Composicion.hpp"

#include "Mezcla.hpp" // kMargenMezclaNs
#include "Reloj.hpp"

#include <algorithm>
#include <cstdlib>
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
        k.contener = c.value("contener", false);
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
                         {"visible", c.visible},
                         {"contener", c.contener}});
    return {{"nombre", nombre}, {"capas", capas}};
}

// --- Composición ---------------------------------------------------------------------

Composicion::Composicion(Reloj& reloj, const Lienzo& lienzo, std::string nombre, Opciones op)
    : reloj_(reloj), lienzo_(lienzo), nombre_(std::move(nombre)), gpu_((op.gpu && hayFabrica("glvideomixer")) || gpuVideo()) {
    const std::string tam = "width=" + std::to_string(lienzo_.ancho) + ",height=" + std::to_string(lienzo_.alto) +
                            ",framerate=" + std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD) +
                            ",pixel-aspect-ratio=1/1";
    const std::string comun = "force-live=true start-time-selection=first background=black ";
    // En GPU todo queda en GLMemory: lo que sale es una textura (el que consume
    // la baja si hace falta, chica o en NV12: docs/MAPA-GPU-CPU.md).
    const std::string salida = gpu_ ? "video/x-raw(memory:GLMemory),format=RGBA," + tam
                                    : capsColorDelLienzo(lienzo_) + "," + tam;
    const std::string mezclador = gpu_ ? "glvideomixer" : "compositor";
    const std::string desc = gpu_ ? mezclador + " name=mx " + comun + "! " + salida
                                  : "compositor name=mx " + comun + "! video/x-raw," + tam + " ! videoconvert ! " + salida;
    std::string completo;
    if (op.dsk) {
        // Dos etapas: la escena (limpia) por un tee, y el DSK encima en otro mezclador.
        completo = desc + " ! tee name=t t. ! queue ! intersink name=limpio producer-name=" + entreComillas(productorLimpio()) +
                   " sync=false t. ! queue ! dsk.sink_0 " + mezclador + " name=dsk " + comun + "! " +
                   (gpu_ ? salida : "video/x-raw," + tam + " ! videoconvert ! " + salida) +
                   " ! intersink name=sale producer-name=" + entreComillas(productor()) + " sync=false";
    } else {
        completo = desc + " ! intersink name=sale producer-name=" + entreComillas(productor()) + " sync=false";
    }
    GError* e = nullptr;
    pipeline_ = gst_parse_launch(completo.c_str(), &e);
    if (e != nullptr) {
        error_ = e->message;
        g_error_free(e);
        if (pipeline_ != nullptr) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return;
    }
    mezclador_ = gst_bin_get_by_name(GST_BIN(pipeline_), "mx");
    dskMx_ = gst_bin_get_by_name(GST_BIN(pipeline_), "dsk");
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
    for (auto* m : {&ramas_, &dsk_}) {
        for (auto& [k, r] : *m) {
            if (r->pad != nullptr) {
                gst_element_release_request_pad(r->mx, r->pad);
                gst_object_unref(r->pad);
            }
            delete r;
        }
        m->clear();
    }
    if (dskMx_ != nullptr) gst_object_unref(dskMx_);
    gst_object_unref(mezclador_);
    gst_object_unref(pipeline_);
}

void Composicion::aplicarLatencia() {
    const std::int64_t l = reloj_.latencia() + kMargenMezclaNs;
    if (latenciaNs_ >= 0 && l <= latenciaNs_ && l > latenciaNs_ - 60 * static_cast<std::int64_t>(GST_MSECOND)) return;
    latenciaNs_ = l;
    g_object_set(mezclador_, "min-upstream-latency", static_cast<guint64>(l), nullptr);
    if (dskMx_ != nullptr) g_object_set(dskMx_, "min-upstream-latency", static_cast<guint64>(l), nullptr);
    gst_bin_recalculate_latency(GST_BIN(pipeline_));
}

GstPadProbeReturn Composicion::alPrimerCuadro(GstPad*, GstPadProbeInfo*, gpointer d) {
    auto* r = static_cast<Rama*>(d);
    r->listo = true;
    return GST_PAD_PROBE_REMOVE;
}

Composicion::Rama* Composicion::crearRama(const Capa& c, int gen, GstElement* mx) {
    if (mx == nullptr) mx = mezclador_;
    const std::string n = nombreSeguro(std::to_string(gen) + "_" + c.entrada);
    auto* r = new Rama();
    r->entrada = c.entrada;
    r->productor = c.productor.empty() ? c.entrada + ":video" : c.productor;
    r->generacion = gen;
    r->src = gst_element_factory_make("intersrc", ("src_" + n).c_str());
    r->cola = gst_element_factory_make("queue", ("cola_" + n).c_str());
    r->recorte = gst_element_factory_make(gpu_ ? "gltransformation" : "videocrop", ("rec_" + n).c_str());
    if (gpu_) {
        r->subir = gst_element_factory_make("glupload", ("gl_" + n).c_str());
        r->conv = gst_element_factory_make("glcolorconvert", ("glc_" + n).c_str());
        // Sin un elemento que dibuje detrás, gltransformation no dibuja: le pasa
        // al mezclador una transformación afín que glvideomixer aplica SIN
        // recortar a la caja de la capa (lo ampliado se sale). glshader, con su
        // shader de fábrica, copia la textura: una pasada barata en la GPU.
        r->fijar = gst_element_factory_make("glshader", ("gls_" + n).c_str());
    }
    if (r->src == nullptr || r->cola == nullptr || r->recorte == nullptr || (gpu_ && (r->subir == nullptr || r->conv == nullptr || r->fijar == nullptr))) {
        for (GstElement* e : {r->src, r->cola, r->recorte, r->subir, r->conv, r->fijar})
            if (e != nullptr) gst_object_unref(e);
        delete r;
        error_ = gpu_ ? "falta intersrc, queue, glupload, glcolorconvert o gltransformation" : "falta intersrc, queue o videocrop";
        registrar(Nivel::Error, "composición " + nombre_ + ": " + error_);
        return nullptr;
    }
    if (gpu_) g_object_set(r->recorte, "ortho", TRUE, nullptr);
    g_object_set(r->src, "producer-name", r->productor.c_str(), nullptr);
    // Sin demora propia: si se atrasa, se tira lo viejo (el compositor espera la latencia).
    g_object_set(r->cola, "max-size-buffers", 3u, "max-size-time", static_cast<guint64>(0), "max-size-bytes", 0u, "leaky", 2,
                 nullptr);
    gst_bin_add_many(GST_BIN(pipeline_), r->src, r->cola, r->recorte, nullptr);
    if (gpu_) {
        // intersrc → cola → subir a GL (si ya es GL, pasa) → RGBA → recorte en GL.
        gst_bin_add_many(GST_BIN(pipeline_), r->subir, r->conv, r->fijar, nullptr);
        gst_element_link_many(r->src, r->cola, r->subir, r->conv, r->recorte, r->fijar, nullptr);
    } else {
        gst_element_link_many(r->src, r->cola, r->recorte, nullptr);
    }
    r->mx = mx;
    r->pad = gst_element_request_pad_simple(mx, "sink_%u");
    GstPad* sale = gst_element_get_static_pad(r->fijar != nullptr ? r->fijar : r->recorte, "src");
    gst_pad_link(sale, r->pad);
    gst_pad_add_probe(sale, GST_PAD_PROBE_TYPE_BUFFER, &Composicion::alPrimerCuadro, r, nullptr);
    gst_object_unref(sale);
    for (GstElement* e : {r->fijar, r->recorte, r->conv, r->subir, r->cola, r->src})
        if (e != nullptr) gst_element_sync_state_with_parent(e);
    if (mx == mezclador_) ramas_[std::to_string(gen) + "/" + c.entrada] = r;
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
    gst_util_set_object_arg(G_OBJECT(r.pad), "sizing-policy", c.contener ? "keep-aspect-ratio" : "none");
    if (gpu_)
        recorteGl(r, c, 0, w, h);
    else
        g_object_set(r.recorte, "left", std::max(0, c.recIzq), "top", std::max(0, c.recArr), "right", std::max(0, c.recDer),
                     "bottom", std::max(0, c.recAba), nullptr);
    r.alfaHacia = c.visible ? c.alfa : 0.0;
    r.orden = c.orden;
    r.capa = c;
}

void Composicion::recorteGl(Rama& r, const Capa& c, int extraDer, int ancho, int alto) {
    // El tamaño de la fuente (lo que llega al recorte).
    int sw = 0, sh = 0;
    if (GstPad* p = gst_element_get_static_pad(r.recorte, "sink")) {
        if (GstCaps* k = gst_pad_get_current_caps(p)) {
            gst_structure_get_int(gst_caps_get_structure(k, 0), "width", &sw);
            gst_structure_get_int(gst_caps_get_structure(k, 0), "height", &sh);
            gst_caps_unref(k);
        }
        gst_object_unref(p);
    }
    const int izq = std::max(0, c.recIzq), arr = std::max(0, c.recArr), der = std::max(0, c.recDer) + std::max(0, extraDer),
              aba = std::max(0, c.recAba);
    if (sw <= 0 || sh <= 0 || izq + der + arr + aba == 0) {
        g_object_set(r.recorte, "scale-x", 1.0f, "scale-y", 1.0f, "translation-x", 0.0f, "translation-y", 0.0f, nullptr);
        return;
    }
    // La región visible, en fracciones de la fuente; gltransformation la estira a todo el cuadro.
    const double u0 = std::clamp(static_cast<double>(izq) / sw, 0.0, 0.95), u1 = std::clamp(1.0 - static_cast<double>(der) / sw, u0 + 0.01, 1.0);
    const double v0 = std::clamp(static_cast<double>(arr) / sh, 0.0, 0.95), v1 = std::clamp(1.0 - static_cast<double>(aba) / sh, v0 + 0.01, 1.0);
    const double sx = 1.0 / (u1 - u0), sy = 1.0 / (v1 - v0);
    g_object_set(r.recorte, "scale-x", static_cast<gfloat>(sx), "scale-y", static_cast<gfloat>(sy), "translation-x",
                 static_cast<gfloat>((0.5 - (u0 + u1) / 2.0) * sx), "translation-y",
                 static_cast<gfloat>((0.5 - (v0 + v1) / 2.0) * sy), nullptr);
    // Con «contener»: la caja con la forma de lo recortado (el mezclador vería la de la fuente entera).
    if (c.contener) {
        const double aspecto = ((u1 - u0) * sw) / ((v1 - v0) * sh);
        int w = ancho, h = static_cast<int>(std::lround(ancho / aspecto));
        if (h > alto) {
            h = alto;
            w = static_cast<int>(std::lround(alto * aspecto));
        }
        gst_util_set_object_arg(G_OBJECT(r.pad), "sizing-policy", "none");
        g_object_set(r.pad, "xpos", c.x + (ancho - w) / 2, "ypos", c.y + (alto - h) / 2, "width", w, "height", h, nullptr);
    }
}

int Composicion::anchoDeOrigen(const Rama& r) {
    int w = 0;
    if (GstPad* p = gst_element_get_static_pad(r.recorte, "sink")) {
        if (GstCaps* c = gst_pad_get_current_caps(p)) {
            gst_structure_get_int(gst_caps_get_structure(c, 0), "width", &w);
            gst_caps_unref(c);
        }
        gst_object_unref(p);
    }
    return w;
}

void Composicion::animar(double t) {
    const int W = lienzo_.ancho;
    for (auto& [k, r] : ramas_) {
        const bool nueva = r->generacion == generacion_;
        const Capa& c = r->capa;
        const int w = c.ancho > 0 ? c.ancho : lienzo_.ancho;
        if (transicion_ == "mezcla") {
            if (nueva) g_object_set(r->pad, "alpha", r->alfaHacia * t, nullptr);
        } else if (transicion_ == "empuje") {
            const int dx = static_cast<int>(std::lround((nueva ? (1.0 - t) : -t) * W));
            g_object_set(r->pad, "xpos", c.x + dx, nullptr);
        } else if (transicion_ == "cortina" && nueva && c.contener) {
            // Una capa que entra sin deformarse no se puede recortar a medida: se funde.
            g_object_set(r->pad, "alpha", r->alfaHacia * t, nullptr);
        } else if (transicion_ == "cortina" && nueva) {
            const double borde = t * W;
            const int vis = static_cast<int>(std::clamp(borde - c.x, 0.0, static_cast<double>(w)));
            const int origen = anchoDeOrigen(*r);
            if (vis <= 1 || origen <= 0) {
                g_object_set(r->pad, "alpha", 0.0, nullptr);
                continue;
            }
            const int util = std::max(1, origen - c.recIzq - c.recDer);
            const int sacar = static_cast<int>(std::lround(util * (1.0 - static_cast<double>(vis) / w)));
            if (gpu_)
                recorteGl(*r, c, sacar, w, c.alto > 0 ? c.alto : lienzo_.alto);
            else
                g_object_set(r->recorte, "right", std::max(0, c.recDer) + sacar, nullptr);
            g_object_set(r->pad, "width", vis, "alpha", r->alfaHacia, nullptr);
        }
    }
}

void Composicion::soltar(Rama& r) {
    for (GstElement* e : {r.src, r.cola, r.recorte, r.subir, r.conv, r.fijar})
        if (e != nullptr) gst_element_set_state(e, GST_STATE_NULL);
    if (r.pad != nullptr) {
        gst_element_release_request_pad(r.mx != nullptr ? r.mx : mezclador_, r.pad);
        gst_object_unref(r.pad);
        r.pad = nullptr;
    }
    gst_bin_remove_many(GST_BIN(pipeline_), r.src, r.cola, r.recorte, nullptr);
    if (r.subir != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.subir);
    if (r.conv != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.conv);
    if (r.fijar != nullptr) gst_bin_remove(GST_BIN(pipeline_), r.fijar);
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
    const bool animada = (transicion == "mezcla" || transicion == "empuje" || transicion == "cortina") && ms > 0 && vieja > 0;
    for (const auto& c : e.capas)
        if (Rama* r = crearRama(c, generacion_)) ponerCapa(*r, c, animada && transicion != "empuje" ? 0.0 : c.alfa);
    escena_ = e;
    if (vieja > 0) {
        genVieja_ = vieja;
        transicion_ = animada ? transicion : "corte";
        transDesdeNs_ = ahoraNs();
        transNs_ = animada ? static_cast<std::int64_t>(ms) * 1000000 : kEsperaCorteNs;
        if (animada) animar(0.0);
    }
}

bool Composicion::manual(const Escena& e, double t, bool soltar) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return false;
    t = std::clamp(t, 0.0, 1.0);
    if (!manual_ || escena_.nombre != e.nombre) {
        // Arranca: lo que esté en transición termina ya, y la nueva entra con alfa 0.
        if (genVieja_ >= 0) {
            sacarGeneracion(genVieja_);
            genVieja_ = -1;
        }
        escenaAntes_ = escena_;
        const int vieja = generacion_;
        ++generacion_;
        for (const auto& c : e.capas)
            if (Rama* r = crearRama(c, generacion_)) ponerCapa(*r, c, 0.0);
        escena_ = e;
        genVieja_ = vieja > 0 ? vieja : -1;
        transicion_ = "mezcla";
        manual_ = true;
    }
    manualT_ = t;
    animar(t);
    if (t < 1.0 && !soltar) return false;
    manual_ = false;
    if (t >= 0.5) {
        for (auto& [k, r] : ramas_)
            if (r->generacion == generacion_) ponerCapa(*r, r->capa, r->alfaHacia);
        if (genVieja_ >= 0) sacarGeneracion(genVieja_);
        genVieja_ = -1;
        return true;
    }
    // Antes de la mitad: vuelve lo de antes (se saca lo nuevo).
    sacarGeneracion(generacion_);
    if (genVieja_ >= 0) generacion_ = genVieja_;
    genVieja_ = -1;
    escena_ = escenaAntes_;
    return false;
}

std::string Composicion::manualHacia() const {
    std::lock_guard<std::mutex> l(mu_);
    return manual_ ? escena_.nombre : std::string();
}

bool Composicion::enTransicion() const {
    std::lock_guard<std::mutex> l(mu_);
    return genVieja_ >= 0;
}

bool Composicion::ponerDsk(const std::vector<Capa>& capas, int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (dskMx_ == nullptr) return false;
    const std::int64_t ahora = ahoraNs(), dur = static_cast<std::int64_t>(std::max(0, ms)) * 1000000;
    std::map<std::string, const Capa*> nuevas;
    for (const auto& c : capas) nuevas[c.entrada] = &c;
    // Las que se van: salen con su fundido.
    for (auto& [k, r] : dsk_)
        if (nuevas.count(k) == 0 && !r->saliendo) {
            r->saliendo = true;
            r->alfaDesde = r->alfa;
            r->alfaHacia = 0;
            r->fundeDesdeNs = ahora;
            r->fundeNs = dur;
        }
    for (const auto& [k, c] : nuevas) {
        Rama* r = nullptr;
        if (auto it = dsk_.find(k); it != dsk_.end()) {
            r = it->second;
        } else {
            r = crearRama(*c, 0, dskMx_);
            if (r == nullptr) continue;
            dsk_[k] = r;
            r->alfa = 0;
        }
        Capa arriba = *c;
        arriba.orden = 100 + std::clamp(c->orden, 0, 899); // siempre encima de la escena (sink_0)
        ponerCapa(*r, arriba, r->alfa);
        r->saliendo = false;
        r->alfaDesde = r->alfa;
        r->alfaHacia = c->visible ? c->alfa : 0.0;
        r->fundeDesdeNs = ahora;
        r->fundeNs = dur;
    }
    return true;
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
        const double t = manual_ ? manualT_
                                 : std::clamp(static_cast<double>(ahoraNs() - transDesdeNs_) /
                                                  static_cast<double>(std::max<std::int64_t>(1, transNs_)),
                                              0.0, 1.0);
        bool nuevasListas = true;
        for (auto& [k, r] : ramas_)
            if (r->generacion == generacion_) nuevasListas = nuevasListas && r->listo.load();
        if (transicion_ != "corte") animar(t);
        const bool termino = manual_ ? false : transicion_ != "corte" ? t >= 1.0 : (nuevasListas || t >= 1.0);
        if (termino) {
            // Lo nuevo, como tiene que quedar (posición, ancho, recorte y alfa).
            for (auto& [k, r] : ramas_)
                if (r->generacion == generacion_) ponerCapa(*r, r->capa, r->alfaHacia);
            sacarGeneracion(genVieja_);
            genVieja_ = -1;
        }
    }
    // Los fundidos del DSK.
    for (auto it = dsk_.begin(); it != dsk_.end();) {
        Rama* r = it->second;
        const double t = r->fundeNs <= 0 ? 1.0
                                         : std::clamp(static_cast<double>(ahoraNs() - r->fundeDesdeNs) / static_cast<double>(r->fundeNs), 0.0, 1.0);
        r->alfa = r->alfaDesde + (r->alfaHacia - r->alfaDesde) * t;
        g_object_set(r->pad, "alpha", r->alfa, nullptr);
        if (r->saliendo && t >= 1.0) {
            soltar(*r);
            delete r;
            it = dsk_.erase(it);
            continue;
        }
        ++it;
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
            {"dsk", [&]() {
                 json d = json::array();
                 for (const auto& [k, r] : dsk_) d.push_back({{"capa", k}, {"alfa", r->alfa}, {"saliendo", r->saliendo}});
                 return d;
             }()},
            {"latencia_ms", static_cast<double>(latenciaNs_) / 1e6},
            {"latencia_salida_ms", static_cast<double>(std::max(salidaNs_, latenciaNs_)) / 1e6},
            {"capas", capas}};
}

} // namespace ba::motor
