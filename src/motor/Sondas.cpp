// ============================================================================
// src/motor/Sondas.cpp — ver Sondas.hpp
// ============================================================================
#include "Sondas.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

namespace ba::motor {

using json = nlohmann::json;

namespace {

constexpr std::size_t kMuestras = 2000; ///< por pad (a 60 fps, ~30 s)

struct Pad {
    GstPad* pad = nullptr;
    gulong id = 0;
    std::mutex mu;
    std::vector<double> edadesMs;
};

struct Elemento {
    std::string nombre, fabrica;
    GstElement* el = nullptr;
    // Compartido con la sonda: un hilo de GStreamer puede estar adentro de alPasar justo
    // cuando se la saca; la sonda guarda su propia referencia y la suelta GStreamer
    // (GDestroyNotify) cuando ya no la usa.
    std::shared_ptr<Pad> entrada, salida;
    /// Un mezclador tiene varias entradas: las demás (la que manda es la que llega última).
    std::vector<std::shared_ptr<Pad>> otras;
    /// Un videorate: lo que llevaba repetido y tirado al empezar (F2-17: una cámara a 29,97
    /// en un lienzo de 30 repite uno cada ~33 s; una de 60 tira uno de cada dos, a propósito).
    bool videorate = false;
    guint64 duplicados = 0, tirados = 0;
};

/// Lo que llevan repetido y tirado los videorate (sus contadores).
void contadoresVideorate(GstElement* el, guint64& duplicados, guint64& tirados) {
    g_object_get(el, "duplicate", &duplicados, "drop", &tirados, nullptr);
}

double mediana(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

double percentil(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    const std::size_t i = std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(i), v.end());
    return v[i];
}

double redondo(double x) { return static_cast<double>(static_cast<long long>(x * 100.0 + (x >= 0 ? 0.5 : -0.5))) / 100.0; }

/// La edad (ms) de un buffer al pasar por `pad`: la hora del reloj del pipeline menos
/// el running time del buffer. NaN si no se puede saber.
double edadMs(GstPad* pad, GstBuffer* buf) {
    if (buf == nullptr || !GST_BUFFER_PTS_IS_VALID(buf)) return -1e18;
    GstEvent* ev = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0);
    if (ev == nullptr) return -1e18;
    const GstSegment* seg = nullptr;
    gst_event_parse_segment(ev, &seg);
    double edad = -1e18;
    if (seg != nullptr && seg->format == GST_FORMAT_TIME) {
        const guint64 rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(buf));
        GstElement* el = gst_pad_get_parent_element(pad);
        if (el != nullptr && rt != GST_CLOCK_TIME_NONE) {
            GstClock* reloj = gst_element_get_clock(el);
            if (reloj != nullptr) {
                const GstClockTime ahora = gst_clock_get_time(reloj);
                const GstClockTime base = gst_element_get_base_time(el);
                if (ahora != GST_CLOCK_TIME_NONE && ahora >= base)
                    edad = (static_cast<double>(ahora - base) - static_cast<double>(rt)) / 1e6;
                gst_object_unref(reloj);
            }
        }
        if (el != nullptr) gst_object_unref(el);
    }
    gst_event_unref(ev);
    return edad;
}

GstPadProbeReturn alPasar(GstPad* pad, GstPadProbeInfo* info, gpointer datos) {
    Pad* p = static_cast<std::shared_ptr<Pad>*>(datos)->get();
    GstBuffer* buf = nullptr;
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) buf = GST_PAD_PROBE_INFO_BUFFER(info);
    else if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList* l = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        if (l != nullptr && gst_buffer_list_length(l) > 0) buf = gst_buffer_list_get(l, 0);
    }
    const double e = edadMs(pad, buf);
    if (e > -1e17) {
        std::lock_guard<std::mutex> l(p->mu);
        if (p->edadesMs.size() < kMuestras) p->edadesMs.push_back(e);
    }
    return GST_PAD_PROBE_OK;
}

/// El primer pad de una dirección (los de pedido ya creados también cuentan).
GstPad* primerPad(GstElement* el, GstPadDirection dir) {
    GstPad* elegido = nullptr;
    GstIterator* it = dir == GST_PAD_SINK ? gst_element_iterate_sink_pads(el) : gst_element_iterate_src_pads(el);
    GValue v = G_VALUE_INIT;
    bool listo = false;
    while (!listo) {
        switch (gst_iterator_next(it, &v)) {
            case GST_ITERATOR_OK: {
                auto* pad = GST_PAD(g_value_get_object(&v));
                // Preferir uno enlazado (un tee tiene varios; un pad libre no ve nada).
                if (elegido == nullptr || (gst_pad_is_linked(pad) && !gst_pad_is_linked(elegido))) {
                    if (elegido != nullptr) gst_object_unref(elegido);
                    elegido = GST_PAD(gst_object_ref(pad));
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
    return elegido;
}

/// Pone la sonda en `pad` (se queda con la referencia).
std::shared_ptr<Pad> sondearPad(GstPad* pad) {
    if (pad == nullptr) return nullptr;
    auto p = std::make_shared<Pad>();
    p->pad = pad;
    p->id = gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                              alPasar, new std::shared_ptr<Pad>(p),
                              [](gpointer d) { delete static_cast<std::shared_ptr<Pad>*>(d); });
    return p;
}

std::shared_ptr<Pad> sondear(GstElement* el, GstPadDirection dir) { return sondearPad(primerPad(el, dir)); }

/// Las entradas enlazadas de un elemento (con referencia; hasta 32).
std::vector<GstPad*> entradasEnlazadas(GstElement* el) {
    std::vector<GstPad*> out;
    GstIterator* it = gst_element_iterate_sink_pads(el);
    GValue v = G_VALUE_INIT;
    bool listo = false;
    while (!listo) {
        switch (gst_iterator_next(it, &v)) {
            case GST_ITERATOR_OK: {
                auto* pad = GST_PAD(g_value_get_object(&v));
                if (gst_pad_is_linked(pad) && out.size() < 32) out.push_back(GST_PAD(gst_object_ref(pad)));
                g_value_reset(&v);
                break;
            }
            case GST_ITERATOR_RESYNC:
                for (GstPad* p : out) gst_object_unref(p);
                out.clear();
                gst_iterator_resync(it);
                break;
            default: listo = true; break;
        }
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    return out;
}

std::string capsCortas(GstCaps* caps) {
    if (caps == nullptr || gst_caps_get_size(caps) == 0) return {};
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    std::string out = gst_structure_get_name(s);
    if (const char* f = gst_structure_get_string(s, "format")) out += std::string(" ") + f;
    int w = 0, h = 0;
    if (gst_structure_get_int(s, "width", &w) && gst_structure_get_int(s, "height", &h))
        out += " " + std::to_string(w) + "x" + std::to_string(h);
    int num = 0, den = 0;
    if (gst_structure_get_fraction(s, "framerate", &num, &den) && num > 0 && den > 0)
        out += " " + std::to_string(static_cast<int>(static_cast<double>(num) / den + 0.5)) + " fps";
    int rate = 0, ch = 0;
    if (gst_structure_get_int(s, "rate", &rate)) out += " " + std::to_string(rate) + " Hz";
    if (gst_structure_get_int(s, "channels", &ch)) out += " " + std::to_string(ch) + " ch";
    return out;
}

} // namespace

namespace {
std::string memoriaDeFeatures(GstCapsFeatures* f) {
    if (f == nullptr) return "sistema";
    if (gst_caps_features_is_any(f)) return "cualquiera";
    const auto tiene = [f](const char* m) { return gst_caps_features_contains(f, m) != FALSE; };
    if (tiene("memory:GLMemory")) return "gl";
    if (tiene("memory:D3D12Memory")) return "d3d12";
    if (tiene("memory:D3D11Memory")) return "d3d11";
    if (tiene("memory:CUDAMemory")) return "cuda";
    if (tiene("memory:VAMemory") || tiene("memory:VASurface")) return "va";
    if (tiene("memory:DMABuf")) return "dmabuf";
    return "sistema";
}

/// La latencia mínima (ms) que contesta una consulta de latencia a `pad` (peer: al de enfrente). <0 si no contesta.
double latenciaMs(GstPad* pad, bool peer) {
    if (pad == nullptr) return -1;
    GstQuery* q = gst_query_new_latency();
    double ms = -1;
    if ((peer ? gst_pad_peer_query(pad, q) : gst_pad_query(pad, q)) != FALSE) {
        gboolean vivo = FALSE;
        GstClockTime min = 0, max = 0;
        gst_query_parse_latency(q, &vivo, &min, &max);
        if (min != GST_CLOCK_TIME_NONE) ms = static_cast<double>(min) / 1e6;
    }
    gst_query_unref(q);
    return ms;
}
} // namespace

std::string memoriaDe(GstCaps* caps) {
    if (caps == nullptr || gst_caps_get_size(caps) == 0) return {};
    return memoriaDeFeatures(gst_caps_get_features(caps, 0));
}

std::vector<std::string> memoriasPosibles(GstElementFactory* fabrica, GstPadDirection dir) {
    std::vector<std::string> out;
    if (fabrica == nullptr) return out;
    const auto sumar = [&out](const std::string& m) {
        if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    };
    for (const GList* l = gst_element_factory_get_static_pad_templates(fabrica); l != nullptr; l = l->next) {
        auto* t = static_cast<GstStaticPadTemplate*>(l->data);
        if (t->direction != dir) continue;
        GstCaps* c = gst_static_caps_get(&t->static_caps);
        if (c == nullptr) continue;
        if (gst_caps_is_any(c)) sumar("cualquiera");
        else
            for (guint i = 0; i < gst_caps_get_size(c); ++i) sumar(memoriaDeFeatures(gst_caps_get_features(c, i)));
        gst_caps_unref(c);
    }
    return out;
}

bool esGpu(const std::string& m) { return m == "gl" || m == "d3d12" || m == "d3d11" || m == "cuda" || m == "va"; }

struct Sondas::Estado {
    std::vector<Elemento> elementos;
    std::chrono::steady_clock::time_point desde = std::chrono::steady_clock::now();
    bool terminado = false;
};

Sondas::Sondas(GstElement* pipeline) : st_(std::make_shared<Estado>()) {
    if (pipeline == nullptr || !GST_IS_BIN(pipeline)) return;
    // De la salida a la fuente (orden topológico); se da vuelta para leer de la fuente a la salida.
    GstIterator* it = gst_bin_iterate_sorted(GST_BIN(pipeline));
    GValue v = G_VALUE_INIT;
    std::vector<GstElement*> orden;
    bool listo = false;
    while (!listo) {
        switch (gst_iterator_next(it, &v)) {
            case GST_ITERATOR_OK:
                orden.push_back(GST_ELEMENT(gst_object_ref(g_value_get_object(&v))));
                g_value_reset(&v);
                break;
            case GST_ITERATOR_RESYNC:
                for (GstElement* e : orden) gst_object_unref(e);
                orden.clear();
                gst_iterator_resync(it);
                break;
            default: listo = true; break;
        }
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    std::reverse(orden.begin(), orden.end());
    for (GstElement* el : orden) {
        Elemento e;
        e.el = el; // la referencia queda hasta terminar
        e.nombre = GST_OBJECT_NAME(el) != nullptr ? GST_OBJECT_NAME(el) : "";
        GstElementFactory* fab = gst_element_get_factory(el);
        e.fabrica = fab != nullptr ? GST_OBJECT_NAME(fab) : (GST_IS_BIN(el) ? "bin" : "");
        if (e.fabrica == "videorate") {
            e.videorate = true;
            contadoresVideorate(el, e.duplicados, e.tirados);
        }
        const std::vector<GstPad*> ents = entradasEnlazadas(el);
        if (ents.size() > 1) {
            e.entrada = sondearPad(ents[0]);
            for (size_t i = 1; i < ents.size(); ++i) e.otras.push_back(sondearPad(ents[i]));
        } else {
            for (GstPad* p : ents) gst_object_unref(p);
            e.entrada = sondear(el, GST_PAD_SINK);
        }
        e.salida = sondear(el, GST_PAD_SRC);
        st_->elementos.push_back(std::move(e));
    }
}

Sondas::~Sondas() {
    if (!st_->terminado) terminar();
}

json Sondas::terminar() {
    json lista = json::array(), cruces = json::array();
    const double segundos =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - st_->desde).count();
    for (Elemento& e : st_->elementos) {
        // Primero se saca la sonda (después nadie más escribe en las muestras).
        std::vector<double> ent, sal;
        std::string memEnt, memSal, capsEnt, capsSal, medio;
        for (Pad* p : {e.entrada.get(), e.salida.get()}) {
            if (p == nullptr) continue;
            if (p->id != 0) gst_pad_remove_probe(p->pad, p->id);
            p->id = 0;
            GstCaps* caps = gst_pad_get_current_caps(p->pad);
            const std::string mem = memoriaDe(caps), cortas = capsCortas(caps);
            if (caps != nullptr) {
                const char* n = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                if (medio.empty() && n != nullptr)
                    medio = g_str_has_prefix(n, "video/") ? "video" : g_str_has_prefix(n, "audio/") ? "audio" : "";
                gst_caps_unref(caps);
            }
            std::lock_guard<std::mutex> l(p->mu);
            if (p == e.entrada.get()) {
                ent = p->edadesMs;
                memEnt = mem;
                capsEnt = cortas;
            } else {
                sal = p->edadesMs;
                memSal = mem;
                capsSal = cortas;
            }
        }
        // Un mezclador: lo que agrega se cuenta desde la entrada que llega última (la que espera);
        // la primera de sus entradas puede ser un fondo que llega enseguida.
        double entMax = ent.empty() ? -1e18 : mediana(ent), entMin = entMax;
        for (auto& o : e.otras) {
            if (!o) continue;
            if (o->id != 0) gst_pad_remove_probe(o->pad, o->id);
            o->id = 0;
            std::vector<double> v;
            {
                std::lock_guard<std::mutex> l(o->mu);
                v = o->edadesMs;
            }
            if (v.empty()) continue;
            const double m = mediana(v);
            if (entMax < -1e17) entMax = entMin = m;
            entMax = std::max(entMax, m);
            entMin = std::min(entMin, m);
            if (ent.empty()) ent = v;
        }
        json f{{"nombre", e.nombre}, {"fabrica", e.fabrica}, {"medio", medio}, {"cuadros", sal.size()},
               {"memoria_entrada", memEnt}, {"memoria_salida", memSal}, {"caps_entrada", capsEnt},
               {"caps_salida", capsSal}};
        if (!sal.empty()) f["edad_salida_ms"] = redondo(mediana(sal));
        if (!ent.empty()) f["edad_entrada_ms"] = redondo(e.otras.empty() ? mediana(ent) : entMax);
        if (!e.otras.empty()) {
            f["entradas"] = static_cast<int>(e.otras.size()) + 1;
            f["edad_entrada_primera_ms"] = redondo(entMin);
        }
        // Una fuente (sin entrada): lo que agrega es la edad con que ya sale el cuadro, desde su
        // hora de captura (una cámara: el sensor, el USB, el driver y el sistema; un intersrc: lo que
        // esperó antes). Sin esto la suma de los elementos no llegaba a la edad de la entrada.
        if (ent.empty() && e.otras.empty() && !sal.empty() && !e.entrada) {
            f["agrega_ms"] = redondo(std::max(0.0, mediana(sal)));
            f["agrega_p95_ms"] = redondo(std::max(0.0, percentil(sal, 0.95)));
            f["fuente"] = true;
        }
        if (!ent.empty() && !sal.empty()) {
            const double desde = e.otras.empty() ? mediana(ent) : entMax;
            const double dif = mediana(sal) - desde;
            f["agrega_ms"] = redondo(std::max(0.0, dif));
            f["agrega_p95_ms"] = redondo(std::max(0.0, percentil(sal, 0.95) - (e.otras.empty() ? percentil(ent, 0.95) : entMax)));
            // Sale «más joven» que lo que entró: el elemento publica los cuadros con otra hora
            // (lo de después no se puede comparar con lo de antes).
            if (dif < -5) f["cambia_la_hora_ms"] = redondo(-dif);
        }
        // Lo que el elemento sabe usar (sus templates) y la latencia que declara.
        if (GstElementFactory* fab = e.el != nullptr ? gst_element_get_factory(e.el) : nullptr) {
            f["puede_entrada"] = memoriasPosibles(fab, GST_PAD_SINK);
            f["puede_salida"] = memoriasPosibles(fab, GST_PAD_SRC);
        }
        if (e.salida && e.salida->pad != nullptr) {
            const double hasta = latenciaMs(e.salida->pad, false);
            const double antes = e.entrada && e.entrada->pad != nullptr ? latenciaMs(e.entrada->pad, true) : 0.0;
            if (hasta >= 0 && antes >= 0) f["declara_ms"] = redondo(std::max(0.0, hasta - antes));
        }
        // Un videorate: lo que repitió y tiró mientras se medía (y por minuto, para ver el ritmo
        // de un 29,97 contra 30: uno cada ~33 s son ~2 por minuto).
        if (e.videorate && e.el != nullptr) {
            guint64 dup = 0, tir = 0;
            contadoresVideorate(e.el, dup, tir);
            const double min = std::max(segundos, 1e-3) / 60.0;
            f["videorate"] = {{"duplicados", dup - e.duplicados},
                              {"tirados", tir - e.tirados},
                              {"duplicados_por_minuto", redondo(static_cast<double>(dup - e.duplicados) / min)},
                              {"tirados_por_minuto", redondo(static_cast<double>(tir - e.tirados) / min)},
                              {"duplicados_total", dup},
                              {"tirados_total", tir}};
        }
        // Las colas: cuánto tienen guardado ahora.
        if (e.el != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(e.el), "current-level-time") != nullptr) {
            guint64 ns = 0;
            g_object_get(e.el, "current-level-time", &ns, nullptr);
            f["nivel_cola_ms"] = redondo(static_cast<double>(ns) / 1e6);
        }
        // Un cruce: la memoria cambia entre la entrada y la salida del elemento.
        if (!memEnt.empty() && !memSal.empty() && esGpu(memEnt) != esGpu(memSal)) {
            const std::string que = esGpu(memEnt) ? "baja" : "sube";
            f["cruce"] = que;
            cruces.push_back({{"elemento", e.nombre}, {"que", que}, {"de", memEnt}, {"a", memSal}});
        }
        lista.push_back(std::move(f));
        for (Pad* p : {e.entrada.get(), e.salida.get()})
            if (p != nullptr && p->pad != nullptr) {
                gst_object_unref(p->pad);
                p->pad = nullptr;
            }
        for (auto& o : e.otras)
            if (o && o->pad != nullptr) {
                gst_object_unref(o->pad);
                o->pad = nullptr;
            }
        if (e.el != nullptr) {
            gst_object_unref(e.el);
            e.el = nullptr;
        }
    }
    st_->elementos.clear();
    st_->terminado = true;
    return {{"elementos", lista}, {"cruces", cruces}, {"segundos", redondo(segundos)}};
}

} // namespace ba::motor
