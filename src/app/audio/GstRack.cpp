// ============================================================================
// src/app/audio/GstRack.cpp — ver GstRack.hpp
// ============================================================================

#include "audio/GstRack.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#if defined(BA_HAVE_GST)
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/gst.h>

#include "audio/GstVoz.hpp"
#include "video/CatalogoGst.hpp"
#endif

namespace ba::app::audio::gstrack {

namespace {

std::mutex g_mu;
std::map<std::string, std::shared_ptr<Anfitrion>> g_anfitriones;
std::map<std::string, std::int64_t> g_ultimoBloque; ///< fuente → ms del último bloque procesado
std::atomic<std::uint64_t> g_generacion{1};         ///< cambia con cada publicar/retirar
std::atomic<bool> g_registrado{false};

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void publicar(const std::string& fuente, std::shared_ptr<Anfitrion> a) {
    std::shared_ptr<Anfitrion> viejo; // se suelta fuera del lock
    {
        std::lock_guard<std::mutex> l(g_mu);
        viejo = std::move(g_anfitriones[fuente]);
        g_anfitriones[fuente] = std::move(a);
    }
    ++g_generacion;
}

void retirar(const std::string& fuente) {
    std::shared_ptr<Anfitrion> viejo;
    {
        std::lock_guard<std::mutex> l(g_mu);
        auto it = g_anfitriones.find(fuente);
        if (it == g_anfitriones.end()) return;
        viejo = std::move(it->second);
        g_anfitriones.erase(it);
        g_ultimoBloque.erase(fuente);
    }
    ++g_generacion;
}

bool procesando(const std::string& fuente) {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = g_ultimoBloque.find(fuente);
    return it != g_ultimoBloque.end() && ahoraMs() - it->second < 1000;
}

bool elementoRegistrado() { return g_registrado.load(); }

#if !defined(BA_HAVE_GST)

bool registrarElemento() { return false; }
void registrarEnSegundoPlano() {}

} // namespace ba::app::audio::gstrack

#else

} // namespace ba::app::audio::gstrack

using ba::app::audio::gstrack::Anfitrion;

namespace {
constexpr std::uint32_t kBloque = 1024; // AudioRack::kMaxFrames
}

G_BEGIN_DECLS
#define BA_TYPE_RACK (ba_rack_get_type())
G_DECLARE_FINAL_TYPE(BaRack, ba_rack, BA, RACK, GstAudioFilter)
G_END_DECLS

struct _BaRack {
    GstAudioFilter parent;
    std::mutex* mu;      ///< `fuente` (propiedad) contra el hilo de audio
    std::string* fuente;
    // Solo el hilo de audio:
    std::shared_ptr<Anfitrion>* anfitrion;
    std::uint64_t generacion; ///< la del registro cuando se buscó el anfitrión
    std::int64_t avisadoMs;   ///< último aviso de «procesando» al registro
    float* planos[2];         ///< kBloque muestras cada uno
};

G_DEFINE_TYPE(BaRack, ba_rack, GST_TYPE_AUDIO_FILTER)

enum { PROP_0, PROP_FUENTE, PROP_CON_RACK };

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* buf) {
    using namespace ba::app::audio::gstrack;
    auto* r = BA_RACK(bt);
    // ¿Cambió el registro? Se busca el anfitrión de nuevo, sin esperar: si
    // justo está ocupado, en el bloque siguiente.
    const std::uint64_t gen = g_generacion.load();
    if (gen != r->generacion) {
        std::unique_lock<std::mutex> l(g_mu, std::try_to_lock);
        if (l.owns_lock()) {
            std::string fuente;
            {
                std::lock_guard<std::mutex> lf(*r->mu);
                fuente = *r->fuente;
            }
            auto it = g_anfitriones.find(fuente);
            std::shared_ptr<Anfitrion> nuevo = it != g_anfitriones.end() ? it->second : nullptr;
            l.unlock();
            r->anfitrion->swap(nuevo); // el viejo se suelta acá, fuera del lock
            r->generacion = gen;
        }
    }
    const std::shared_ptr<Anfitrion>& a = *r->anfitrion;
    if (!a || GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_GAP)) return GST_FLOW_OK;
    const int canales = GST_AUDIO_INFO_CHANNELS(&GST_AUDIO_FILTER(r)->info);
    if (canales != 2) return GST_FLOW_OK;
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) return GST_FLOW_ERROR;
    auto* s = reinterpret_cast<float*>(map.data);
    const auto total = static_cast<std::uint32_t>(map.size / (sizeof(float) * 2));
    for (std::uint32_t hecho = 0; hecho < total;) {
        const std::uint32_t n = std::min(kBloque, total - hecho);
        float* p = s + static_cast<size_t>(hecho) * 2;
        for (std::uint32_t i = 0; i < n; ++i) {
            r->planos[0][i] = p[2 * i];
            r->planos[1][i] = p[2 * i + 1];
        }
        a->procesar(r->planos, 2, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            p[2 * i] = r->planos[0][i];
            p[2 * i + 1] = r->planos[1][i];
        }
        hecho += n;
    }
    gst_buffer_unmap(buf, &map);
    // Para el diagnóstico: que se sepa que el rack de esta fuente corre acá.
    const std::int64_t ms = ahoraMs();
    if (ms - r->avisadoMs > 250) {
        std::unique_lock<std::mutex> l(g_mu, std::try_to_lock);
        if (l.owns_lock()) {
            std::lock_guard<std::mutex> lf(*r->mu);
            g_ultimoBloque[*r->fuente] = ms;
            r->avisadoMs = ms;
        }
    }
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* r = BA_RACK(o);
    if (id != PROP_FUENTE) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    {
        std::lock_guard<std::mutex> l(*r->mu);
        const gchar* x = g_value_get_string(v);
        *r->fuente = x != nullptr ? x : "";
    }
    r->generacion = 0; // que se busque de nuevo
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* r = BA_RACK(o);
    std::string fuente;
    {
        std::lock_guard<std::mutex> l(*r->mu);
        fuente = *r->fuente;
    }
    // Sin r->mu tomado: el hilo de audio toma el registro y después r->mu.
    switch (id) {
        case PROP_FUENTE: g_value_set_string(v, fuente.c_str()); break;
        case PROP_CON_RACK: g_value_set_boolean(v, ba::app::audio::gstrack::procesando(fuente)); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* r = BA_RACK(o);
    delete r->anfitrion;
    delete r->fuente;
    delete r->mu;
    delete[] r->planos[0];
    delete[] r->planos[1];
    G_OBJECT_CLASS(ba_rack_parent_class)->finalize(o);
}

static void ba_rack_class_init(BaRackClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(
        go, PROP_FUENTE,
        g_param_spec_string("fuente", "Fuente", "El nombre de la fuente cuyo rack procesa el audio", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(go, PROP_CON_RACK,
                                    g_param_spec_boolean("con-rack", "Con rack", "El rack está procesando (último segundo)",
                                                         FALSE, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Rack de audio", "Filter/Effect/Audio",
                                          "El rack de audio de la app (módulos LSP/CLAP) en la cadena de la entrada",
                                          "Broadcast Appliance");
    GstCaps* caps =
        gst_caps_from_string("audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)48000, channels=(int)2");
    gst_audio_filter_class_add_pad_templates(GST_AUDIO_FILTER_CLASS(k), caps);
    gst_caps_unref(caps);
    bt->transform_ip = transformar;
    bt->transform_ip_on_passthrough = FALSE;
}

static void ba_rack_init(BaRack* r) {
    r->mu = new std::mutex();
    r->fuente = new std::string();
    r->anfitrion = new std::shared_ptr<Anfitrion>();
    r->generacion = 0;
    r->avisadoMs = 0;
    r->planos[0] = new float[kBloque]();
    r->planos[1] = new float[kBloque]();
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(r), TRUE);
}

namespace ba::app::audio::gstrack {

bool registrarElemento() {
    static std::once_flag una;
    std::call_once(una, []() {
        if (gst_is_initialized() && gst_element_register(nullptr, "barack", GST_RANK_NONE, BA_TYPE_RACK))
            g_registrado = true;
    });
    return g_registrado.load();
}

void registrarEnSegundoPlano() {
    std::thread([]() {
        if (video::iniciarGStreamer()) {
            registrarElemento();
            gstvoz::registrarElemento(); // las voces (bavoz), de paso
        }
    }).detach();
}

} // namespace ba::app::audio::gstrack

#endif
