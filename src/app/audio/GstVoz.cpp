// ============================================================================
// src/app/audio/GstVoz.cpp — ver GstVoz.hpp
// ============================================================================

#include "audio/GstVoz.hpp"

#include <algorithm>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#if defined(BA_HAVE_GST)
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#endif

#include <chrono>

namespace ba::app::audio::gstvoz {

namespace {

struct Tramo {
    std::vector<float> muestras; ///< intercaladas
    std::uint32_t frecuencia = 0;
    std::uint64_t ts = 0;
    int canales = 1;
};

struct Cola {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Tramo> tramos;
};

std::mutex g_mu;
std::map<std::string, std::shared_ptr<Cola>> g_colas;
std::atomic<bool> g_registrado{false};

std::uint64_t relojDeFabrica() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::atomic<std::uint64_t (*)()> g_reloj{relojDeFabrica};

std::shared_ptr<Cola> colaDe(const std::string& voz) {
    std::lock_guard<std::mutex> l(g_mu);
    auto& c = g_colas[voz];
    if (!c) c = std::make_shared<Cola>();
    return c;
}

} // namespace

void encolar(const std::string& voz, const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t ts,
             int canales) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    canales = std::clamp(canales, 1, 2);
    auto c = colaDe(voz);
    {
        std::lock_guard<std::mutex> l(c->mu);
        c->tramos.push_back(
            {std::vector<float>(muestras, muestras + n * static_cast<std::size_t>(canales)), frecuencia, ts, canales});
        // Tope: 2 minutos encolados (algo se trabó del otro lado).
        while (c->tramos.size() > 2000) c->tramos.pop_front();
    }
    c->cv.notify_all();
}

void vaciar(const std::string& voz) {
    auto c = colaDe(voz);
    std::lock_guard<std::mutex> l(c->mu);
    c->tramos.clear();
}

bool registrado() { return g_registrado.load(); }

void ponerReloj(std::uint64_t (*reloj)()) { g_reloj = reloj != nullptr ? reloj : relojDeFabrica; }

#if !defined(BA_HAVE_GST)

bool registrarElemento() { return false; }

} // namespace ba::app::audio::gstvoz

#else

} // namespace ba::app::audio::gstvoz

G_BEGIN_DECLS
#define BA_TYPE_VOZ (ba_voz_get_type())
G_DECLARE_FINAL_TYPE(BaVoz, ba_voz, BA, VOZ, GstPushSrc)
G_END_DECLS

struct _BaVoz {
    GstPushSrc parent;
    std::mutex* mu;
    std::string* voz;
    std::atomic<bool> parar;
    std::uint32_t frecuencia; ///< la de las caps negociadas (0 = sin negociar)
    int canales;
};

G_DEFINE_TYPE(BaVoz, ba_voz, GST_TYPE_PUSH_SRC)

enum { PROP_0, PROP_VOZ };

static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)[1, MAX], channels=(int)[1, 2]"));

static GstFlowReturn crear(GstPushSrc* ps, GstBuffer** out) {
    using namespace ba::app::audio::gstvoz;
    using ba::app::audio::gstvoz::g_reloj;
    auto* v = BA_VOZ(ps);
    std::string voz;
    {
        std::lock_guard<std::mutex> l(*v->mu);
        voz = *v->voz;
    }
    auto cola = colaDe(voz);
    Tramo t;
    {
        std::unique_lock<std::mutex> l(cola->mu);
        while (cola->tramos.empty()) {
            if (v->parar.load()) return GST_FLOW_FLUSHING;
            cola->cv.wait_for(l, std::chrono::milliseconds(100));
        }
        t = std::move(cola->tramos.front());
        cola->tramos.pop_front();
    }
    if (t.frecuencia != v->frecuencia || t.canales != v->canales) {
        GstCaps* caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE", "layout", G_TYPE_STRING,
                                            "interleaved", "rate", G_TYPE_INT, static_cast<int>(t.frecuencia), "channels",
                                            G_TYPE_INT, t.canales, nullptr);
        const bool ok = gst_base_src_set_caps(GST_BASE_SRC(v), caps);
        gst_caps_unref(caps);
        if (!ok) return GST_FLOW_NOT_NEGOTIATED;
        v->frecuencia = t.frecuencia;
        v->canales = t.canales;
    }
    const gsize bytes = t.muestras.size() * sizeof(float);
    GstBuffer* b = gst_buffer_new_allocate(nullptr, bytes, nullptr);
    gst_buffer_fill(b, 0, t.muestras.data(), bytes);
    // La hora del tramo (ba::app::ahoraNs) en el reloj del pipeline: los dos
    // relojes leídos juntos, sin suponer que tengan la misma base.
    GstClockTime pts = GST_CLOCK_TIME_NONE;
    if (GstClock* reloj = gst_element_get_clock(GST_ELEMENT(v))) {
        const std::int64_t tg = static_cast<std::int64_t>(gst_clock_get_time(reloj));
        const std::int64_t to = static_cast<std::int64_t>(g_reloj.load()());
        gst_object_unref(reloj);
        const std::int64_t base = static_cast<std::int64_t>(gst_element_get_base_time(GST_ELEMENT(v)));
        const std::int64_t r = static_cast<std::int64_t>(t.ts) + (tg - to) - base;
        pts = r > 0 ? static_cast<GstClockTime>(r) : 0;
    }
    GST_BUFFER_PTS(b) = pts;
    GST_BUFFER_DURATION(b) =
        gst_util_uint64_scale(t.muestras.size() / static_cast<std::size_t>(t.canales), GST_SECOND, t.frecuencia);
    *out = b;
    return GST_FLOW_OK;
}

static gboolean desbloquear(GstBaseSrc* bs) {
    auto* v = BA_VOZ(bs);
    v->parar = true;
    std::string voz;
    {
        std::lock_guard<std::mutex> l(*v->mu);
        voz = *v->voz;
    }
    ba::app::audio::gstvoz::colaDe(voz)->cv.notify_all();
    return TRUE;
}

static gboolean desbloquearFin(GstBaseSrc* bs) {
    BA_VOZ(bs)->parar = false;
    return TRUE;
}

static gboolean alParar(GstBaseSrc* bs) {
    BA_VOZ(bs)->frecuencia = 0;
    BA_VOZ(bs)->canales = 0;
    return TRUE;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* val, GParamSpec* ps) {
    auto* v = BA_VOZ(o);
    if (id != PROP_VOZ) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(val);
    std::lock_guard<std::mutex> l(*v->mu);
    *v->voz = x != nullptr ? x : "";
}

static void leerPropiedad(GObject* o, guint id, GValue* val, GParamSpec* ps) {
    auto* v = BA_VOZ(o);
    if (id != PROP_VOZ) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*v->mu);
    g_value_set_string(val, v->voz->c_str());
}

static void finalizar(GObject* o) {
    auto* v = BA_VOZ(o);
    delete v->voz;
    delete v->mu;
    G_OBJECT_CLASS(ba_voz_parent_class)->finalize(o);
}

static void ba_voz_class_init(BaVozClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* bs = GST_BASE_SRC_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(go, PROP_VOZ,
                                    g_param_spec_string("voz", "Voz", "La voz (su nombre: «Voz del sistema»…)", "",
                                                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Voz sintetizada", "Source/Audio",
                                          "Lo que dice una voz de la app (texto a voz), con su hora", "Broadcast Appliance");
    gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(k), &srcTpl);
    bs->unlock = desbloquear;
    bs->unlock_stop = desbloquearFin;
    bs->stop = alParar;
    GST_PUSH_SRC_CLASS(k)->create = crear;
}

static void ba_voz_init(BaVoz* v) {
    v->mu = new std::mutex();
    v->voz = new std::string();
    v->parar.store(false);
    v->frecuencia = 0;
    gst_base_src_set_live(GST_BASE_SRC(v), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(v), GST_FORMAT_TIME);
}

namespace ba::app::audio::gstvoz {

bool registrarElemento() {
    // Reintentable: si GStreamer todavía no arrancó, la próxima vez.
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    if (!g_registrado && gst_is_initialized() && gst_element_register(nullptr, "bavoz", GST_RANK_NONE, BA_TYPE_VOZ))
        g_registrado = true;
    return g_registrado.load();
}

} // namespace ba::app::audio::gstvoz

#endif
