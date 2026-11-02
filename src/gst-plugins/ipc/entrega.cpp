// ============================================================================
// src/gst-plugins/ipc/entrega.cpp — baentrega / batoma: el audio de una entrada a los
// mezcladores, SIN un hilo por rama
//
// intersink/intersrc pasan cada buffer por una cola (el appsrc del intersrc) con SU
// hilo: una entrada en dos mezcladores (el programa y los retornos) son dos hilos más y
// tres despertadas por bloque de 10 ms. Con 40 invitados, la mayor parte de los cambios
// de contexto de la app. Acá el buffer sigue en el MISMO hilo que lo trajo (el lector
// común de los anillos, el de la placa…) hasta el pad del mezclador:
//
//   baentrega producer-name=X   (sink) guarda las caps y el segmento, y empuja cada
//                               buffer por cada batoma de X que esté andando
//   batoma producer-name=X      (fuente, sin hilo) lo que entrega X; antes del primero,
//                               stream-start, caps y el segmento del productor (con su
//                               desplazamiento: la hora de un archivo), y de nuevo si cambian
//
// SOLO para entregar a un agregador en vivo (audiomixer, baretornos): toma el buffer
// sin esperar al reloj. Algo que espera (un sink con sync) trabaría al que empuja: para
// eso, intersink/intersrc. batoma empuja solo con SU pipeline en PLAYING (un mezclador
// parado no consume: llenaría el pad y trabaría al productor).
//
// La latencia que declara batoma: la del pipeline del productor (la que le reparte su
// pipeline a baentrega con el evento LATENCY), como un intersrc.
// ============================================================================

#include <gst/base/gstbasesink.h>
#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Toma {
    std::mutex mu;
    GstElement* e = nullptr;    ///< bajo mu: nullptr = ya no se le empuja
    GstElement* raiz = nullptr; ///< bajo mu: el pipeline de la batoma (mientras está enganchada)
    GstPad* pad = nullptr;
    bool eventos = true;
    std::uint64_t segmentoVisto = 0;
    GstCaps* caps = nullptr;
    ~Toma() {
        if (caps != nullptr) gst_caps_unref(caps);
    }
};

struct Productor {
    std::mutex mu;
    std::vector<std::shared_ptr<Toma>> tomas; ///< bajo mu
    GstCaps* caps = nullptr;                  ///< bajo mu
    GstSegment segmento{};                    ///< bajo mu
    std::uint64_t serieSegmento = 0;          ///< bajo mu: crece con cada segmento (nunca vuelve atrás)
    bool conSegmento = false;                 ///< bajo mu: el pipeline de la entrega ya mandó uno
    std::atomic<GstClockTime> latencia{0};
};

std::mutex g_mu;
std::map<std::string, std::weak_ptr<Productor>> g_productores;

std::shared_ptr<Productor> productor(const std::string& nombre) {
    std::lock_guard<std::mutex> l(g_mu);
    auto& w = g_productores[nombre];
    std::shared_ptr<Productor> p = w.lock();
    if (!p) {
        p = std::make_shared<Productor>();
        gst_segment_init(&p->segmento, GST_FORMAT_TIME);
        w = p;
    }
    return p;
}

/// Empuja `b` por `t` (lo que falte de eventos antes). Con el candado de la toma.
void empujarA(Toma& t, GstBuffer* b, GstCaps* caps, const GstSegment& seg, std::uint64_t serie) {
    std::lock_guard<std::mutex> l(t.mu);
    if (t.e == nullptr || t.raiz == nullptr) return;
    // Solo con el pipeline del mezclador andando y sin que lo estén parando (ver la
    // cabecera): al bajar, el pipeline cambia su destino ANTES de parar al mezclador (y
    // a la batoma, la última: es fuente), así no se le empuja a un mezclador en pausa.
    GST_OBJECT_LOCK(t.raiz);
    const bool anda = GST_STATE(t.raiz) == GST_STATE_PLAYING && GST_STATE_TARGET(t.raiz) == GST_STATE_PLAYING;
    GST_OBJECT_UNLOCK(t.raiz);
    if (!anda) return;
    if (t.eventos) {
        gchar* id = gst_pad_create_stream_id(t.pad, t.e, nullptr);
        GstEvent* ss = gst_event_new_stream_start(id);
        g_free(id);
        gst_event_set_group_id(ss, gst_util_group_id_next());
        gst_pad_push_event(t.pad, ss);
    }
    if (caps != nullptr && (t.eventos || t.caps == nullptr || !gst_caps_is_equal(t.caps, caps))) {
        if (t.caps != nullptr) gst_caps_unref(t.caps);
        t.caps = gst_caps_ref(caps);
        gst_pad_push_event(t.pad, gst_event_new_caps(caps));
    }
    if (t.eventos || t.segmentoVisto != serie) {
        gst_pad_push_event(t.pad, gst_event_new_segment(&seg));
        t.segmentoVisto = serie;
    }
    t.eventos = false;
    gst_pad_push(t.pad, gst_buffer_ref(b));
}

} // namespace

// ============================================================================ baentrega

G_BEGIN_DECLS
#define BA_TYPE_ENTREGA (ba_entrega_get_type())
G_DECLARE_FINAL_TYPE(BaEntrega, ba_entrega, BA, ENTREGA, GstBaseSink)
G_END_DECLS

struct _BaEntrega {
    GstBaseSink parent;
    std::mutex* mu;               ///< el nombre y `p`
    std::string* nombre;
    std::shared_ptr<Productor>* p; ///< el registrado (nullptr fuera de READY..PLAYING)
};

G_DEFINE_TYPE(BaEntrega, ba_entrega, GST_TYPE_BASE_SINK)

enum { PROP_E_0, PROP_E_PRODUCTOR };

static GstStaticPadTemplate entregaTpl = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static std::shared_ptr<Productor> deEntrega(BaEntrega* e) {
    std::lock_guard<std::mutex> l(*e->mu);
    return *e->p;
}

static gboolean entregaArrancar(GstBaseSink* bs) {
    auto* e = BA_ENTREGA(bs);
    std::lock_guard<std::mutex> l(*e->mu);
    *e->p = productor(*e->nombre);
    return TRUE;
}

static gboolean entregaParar(GstBaseSink* bs) {
    auto* e = BA_ENTREGA(bs);
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*e->mu);
        p = std::move(*e->p);
        *e->p = nullptr;
    }
    if (p) {
        std::lock_guard<std::mutex> l(p->mu);
        if (p->caps != nullptr) gst_caps_unref(p->caps);
        p->caps = nullptr;
        p->conSegmento = false; // al volver a andar manda uno nuevo (y la serie sigue: las tomas lo reenvían)
    }
    return TRUE;
}

static gboolean entregaEvento(GstBaseSink* bs, GstEvent* ev) {
    if (std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(bs))) {
        if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(ev, &c);
            std::lock_guard<std::mutex> l(p->mu);
            if (p->caps != nullptr) gst_caps_unref(p->caps);
            p->caps = c != nullptr ? gst_caps_ref(c) : nullptr;
        } else if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT) {
            // Ya trae el desplazamiento del pad (gst_pad_set_offset: el de un archivo).
            const GstSegment* s = nullptr;
            gst_event_parse_segment(ev, &s);
            std::lock_guard<std::mutex> l(p->mu);
            gst_segment_copy_into(s, &p->segmento);
            ++p->serieSegmento;
            p->conSegmento = true;
        }
    }
    // EOS, flush y lo demás no pasan a los mezcladores (como intersink event-types="<>").
    return GST_BASE_SINK_CLASS(ba_entrega_parent_class)->event(bs, ev);
}

static GstFlowReturn entregaRender(GstBaseSink* bs, GstBuffer* b) {
    std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(bs));
    if (!p) return GST_FLOW_OK;
    std::vector<std::shared_ptr<Toma>> tomas;
    GstCaps* caps = nullptr;
    GstSegment seg;
    std::uint64_t serie = 0;
    {
        std::lock_guard<std::mutex> l(p->mu);
        if (p->tomas.empty()) return GST_FLOW_OK;
        tomas = p->tomas;
        caps = p->caps != nullptr ? gst_caps_ref(p->caps) : nullptr;
        gst_segment_copy_into(&p->segmento, &seg);
        serie = p->conSegmento ? p->serieSegmento : 0;
    }
    if (serie != 0)
        for (const auto& t : tomas) empujarA(*t, b, caps, seg, serie);
    if (caps != nullptr) gst_caps_unref(caps);
    return GST_FLOW_OK;
}

/// La latencia del pipeline (la que reparte a sus sinks): la que declara cada batoma.
static gboolean entregaMandarEvento(GstElement* el, GstEvent* ev) {
    if (GST_EVENT_TYPE(ev) == GST_EVENT_LATENCY) {
        GstClockTime lat = 0;
        gst_event_parse_latency(ev, &lat);
        if (std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(el))) p->latencia = lat;
    }
    return GST_ELEMENT_CLASS(ba_entrega_parent_class)->send_event(el, ev);
}

static void entregaPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* e = BA_ENTREGA(o);
    if (id != PROP_E_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> l(*e->mu);
    *e->nombre = x != nullptr ? x : "";
}

static void entregaLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* e = BA_ENTREGA(o);
    if (id != PROP_E_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*e->mu);
    g_value_set_string(v, e->nombre->c_str());
}

static void entregaFinalizar(GObject* o) {
    auto* e = BA_ENTREGA(o);
    delete e->p;
    delete e->nombre;
    delete e->mu;
    G_OBJECT_CLASS(ba_entrega_parent_class)->finalize(o);
}

static void ba_entrega_class_init(BaEntregaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    auto* bs = GST_BASE_SINK_CLASS(k);
    go->set_property = entregaPoner;
    go->get_property = entregaLeer;
    go->finalize = entregaFinalizar;
    g_object_class_install_property(
        go, PROP_E_PRODUCTOR,
        g_param_spec_string("producer-name", "Productor", "El nombre con que lo encuentran las batoma", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    gst_element_class_set_static_metadata(ge, "Entrega sin hilo", "Sink/Generic",
                                          "Entrega cada buffer, en el mismo hilo, a las batoma del mismo nombre (hacia un "
                                          "agregador en vivo)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &entregaTpl);
    ge->send_event = entregaMandarEvento;
    bs->start = entregaArrancar;
    bs->stop = entregaParar;
    bs->event = entregaEvento;
    bs->render = entregaRender;
}

static void ba_entrega_init(BaEntrega* e) {
    e->mu = new std::mutex();
    e->nombre = new std::string();
    e->p = new std::shared_ptr<Productor>();
    gst_base_sink_set_sync(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_async_enabled(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_last_sample_enabled(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_qos_enabled(GST_BASE_SINK(e), FALSE);
}

// ============================================================================ batoma

G_BEGIN_DECLS
#define BA_TYPE_TOMA (ba_toma_get_type())
G_DECLARE_FINAL_TYPE(BaToma, ba_toma, BA, TOMA, GstElement)
G_END_DECLS

struct _BaToma {
    GstElement parent;
    std::mutex* mu; ///< el nombre y `p`
    std::string* nombre;
    std::shared_ptr<Productor>* p; ///< al que está enganchada (en PLAYING)
    std::shared_ptr<Toma>* t;
};

G_DEFINE_TYPE(BaToma, ba_toma, GST_TYPE_ELEMENT)

enum { PROP_T_0, PROP_T_PRODUCTOR };

static GstStaticPadTemplate tomaTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void enganchar(BaToma* t) {
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*t->mu);
        p = productor(*t->nombre);
        *t->p = p;
    }
    // El pipeline de arriba de todo (no cambia mientras está en PLAYING): se mira su estado.
    GstObject* raiz = GST_OBJECT(t);
    while (GST_OBJECT_PARENT(raiz) != nullptr) raiz = GST_OBJECT_PARENT(raiz);
    {
        std::lock_guard<std::mutex> l((*t->t)->mu);
        (*t->t)->e = GST_ELEMENT(t);
        (*t->t)->raiz = GST_ELEMENT(raiz);
        (*t->t)->eventos = true;
    }
    std::lock_guard<std::mutex> l(p->mu);
    p->tomas.push_back(*t->t);
}

static void desenganchar(BaToma* t) {
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*t->mu);
        p = std::move(*t->p);
        *t->p = nullptr;
    }
    if (p) {
        std::lock_guard<std::mutex> l(p->mu);
        p->tomas.erase(std::remove(p->tomas.begin(), p->tomas.end(), *t->t), p->tomas.end());
    }
}

/// Al bajar a READY y al destruirla: si el productor le está empujando, se espera; después
/// no la toca. (Al salir de PLAYING no se espera: el que empuja puede estar trabado en un
/// mezclador que ya está en pausa, y eso se suelta cuando el mezclador baja a READY, antes
/// que la batoma, que es fuente.)
static void soltarToma(BaToma* t) {
    std::lock_guard<std::mutex> l((*t->t)->mu);
    (*t->t)->e = nullptr;
    (*t->t)->raiz = nullptr;
}

static gboolean tomaConsulta(GstPad* pad, GstObject* padre, GstQuery* q) {
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        auto* t = BA_TOMA(padre);
        std::shared_ptr<Productor> p;
        {
            std::lock_guard<std::mutex> l(*t->mu);
            p = *t->p;
        }
        gst_query_set_latency(q, TRUE, p ? p->latencia.load() : 0, GST_CLOCK_TIME_NONE);
        return TRUE;
    }
    return gst_pad_query_default(pad, padre, q);
}

static gboolean tomaEvento(GstPad*, GstObject*, GstEvent* ev) {
    gst_event_unref(ev); // lo que sube (reconfigurar, QoS) no vuelve al productor
    return TRUE;
}

static GstStateChangeReturn tomaCambiar(GstElement* e, GstStateChange c) {
    auto* t = BA_TOMA(e);
    if (c == GST_STATE_CHANGE_PLAYING_TO_PAUSED) desenganchar(t);
    GstStateChangeReturn r = GST_ELEMENT_CLASS(ba_toma_parent_class)->change_state(e, c);
    if (r == GST_STATE_CHANGE_FAILURE) return r;
    switch (c) {
        case GST_STATE_CHANGE_READY_TO_PAUSED:
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED: r = GST_STATE_CHANGE_NO_PREROLL; break; // en vivo
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: enganchar(t); break;
        case GST_STATE_CHANGE_PAUSED_TO_READY: soltarToma(t); break;
        default: break;
    }
    return r;
}

static void tomaPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* t = BA_TOMA(o);
    if (id != PROP_T_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> l(*t->mu);
    *t->nombre = x != nullptr ? x : "";
}

static void tomaLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* t = BA_TOMA(o);
    if (id != PROP_T_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*t->mu);
    g_value_set_string(v, t->nombre->c_str());
}

static void tomaFinalizar(GObject* o) {
    auto* t = BA_TOMA(o);
    desenganchar(t); // por si se soltó sin pararla
    soltarToma(t);
    delete t->t;
    delete t->p;
    delete t->nombre;
    delete t->mu;
    G_OBJECT_CLASS(ba_toma_parent_class)->finalize(o);
}

static void ba_toma_class_init(BaTomaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = tomaPoner;
    go->get_property = tomaLeer;
    go->finalize = tomaFinalizar;
    g_object_class_install_property(
        go, PROP_T_PRODUCTOR,
        g_param_spec_string("producer-name", "Productor", "La baentrega de la que toma", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    gst_element_class_set_static_metadata(ge, "Toma sin hilo", "Source/Generic",
                                          "Lo que entrega una baentrega, empujado por el hilo de ella (hacia un agregador en vivo)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &tomaTpl);
    ge->change_state = tomaCambiar;
}

static void ba_toma_init(BaToma* t) {
    t->mu = new std::mutex();
    t->nombre = new std::string();
    t->p = new std::shared_ptr<Productor>();
    t->t = new std::shared_ptr<Toma>(std::make_shared<Toma>());
    GstPad* pad = gst_pad_new_from_static_template(&tomaTpl, "src");
    gst_pad_set_query_function(pad, tomaConsulta);
    gst_pad_set_event_function(pad, tomaEvento);
    gst_pad_use_fixed_caps(pad);
    gst_element_add_pad(GST_ELEMENT(t), pad);
    (*t->t)->pad = pad;
    GST_OBJECT_FLAG_SET(t, GST_ELEMENT_FLAG_SOURCE);
}

extern "C" gboolean ba_registrar_entrega(GstPlugin* p) {
    return gst_element_register(p, "baentrega", GST_RANK_NONE, BA_TYPE_ENTREGA) &&
           gst_element_register(p, "batoma", GST_RANK_NONE, BA_TYPE_TOMA);
}
