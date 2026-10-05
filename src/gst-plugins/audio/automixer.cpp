// ============================================================================
// src/gst-plugins/audio/automixer.cpp — baautomixer: automixer de mesa redonda por grupo
//
// Cada micrófono es una entrada con su propio pipeline; el automixer necesita
// ver a TODOS los de la mesa. Por eso los `baautomixer` con el mismo `grupo`
// comparten, dentro del proceso, un `ba::dsp::Automixer` (Dugan): cada uno le
// reporta el nivel de su bloque y aplica la ganancia que le toca. El que habla
// pasa al frente y los demás bajan; la suma queda acotada.
//
//   - Canal: cada elemento toma un lugar libre del grupo al arrancar (READY →
//     PAUSED) y lo suelta al parar; `canal` (solo lectura) dice cuál.
//   - Parámetros de la mesa (umbral-db, hold-ms, piso-hold-db, max-abiertos,
//     ataque-ms, liberacion-ms): son DEL GRUPO; ponerlos en cualquier elemento
//     los cambia para todos (con un fundido corto, sin clics).
//   - activo=false: el micrófono sale como entra y no cuenta en la mesa.
//   - La ganancia se interpola a lo largo del bloque (sin escalones).
//   - No agrega latencia (trabaja bloque a bloque, en el lugar).
//   - ganancia-db y nivel-db (solo lectura): para la interfaz y el diagnóstico.
// ============================================================================

#include "dsp/Automixer.hpp"

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/gst.h>

#include <atomic>
#include <bitset>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace {

constexpr int kMaxCanales = 32;

/// Una mesa: el automixer compartido y qué lugares están tomados.
struct Grupo {
    ba::dsp::Automixer am{kMaxCanales};
    std::mutex mu;
    std::bitset<kMaxCanales> ocupados;
    ba::dsp::AutomixerParams params;

    void aplicar(double fundidoMs) { am.setParams(params, fundidoMs); } // con mu tomado
};

std::mutex gMu;
std::map<std::string, std::weak_ptr<Grupo>> gGrupos;

std::shared_ptr<Grupo> grupoDe(const std::string& nombre) {
    std::lock_guard<std::mutex> l(gMu);
    if (auto g = gGrupos[nombre].lock()) return g;
    auto g = std::make_shared<Grupo>();
    g->aplicar(0.0);
    gGrupos[nombre] = g;
    return g;
}

double aDb(double lin) { return lin > 1e-9 ? 20.0 * std::log10(lin) : -180.0; }

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_AUTOMIXER (ba_automixer_get_type())
G_DECLARE_FINAL_TYPE(BaAutomixer, ba_automixer, BA, AUTOMIXER, GstAudioFilter)
G_END_DECLS

struct _BaAutomixer {
    GstAudioFilter parent;
    std::mutex* mu; ///< grupo, nombre y canal (los toca el hilo de control y el del estado)
    std::string* nombre;
    std::shared_ptr<Grupo>* grupo;
    int canal;                ///< -1: sin lugar (parado o la mesa está llena)
    std::atomic<bool> activo;
    float gananciaPrevia;     ///< solo el hilo de streaming
    std::atomic<float> ganancia, nivel;
};

G_DEFINE_TYPE(BaAutomixer, ba_automixer, GST_TYPE_AUDIO_FILTER)

GST_DEBUG_CATEGORY_STATIC(ba_automixer_debug);
#define GST_CAT_DEFAULT ba_automixer_debug

enum {
    PROP_0,
    PROP_GRUPO,
    PROP_ACTIVO,
    PROP_UMBRAL,
    PROP_HOLD,
    PROP_PISO_HOLD,
    PROP_MAX_ABIERTOS,
    PROP_ATAQUE,
    PROP_LIBERACION,
    PROP_CANAL,
    PROP_GANANCIA,
    PROP_NIVEL
};

#define CAPS "audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)[1, MAX], channels=(int)[1, MAX]"

/// Suelta el lugar en la mesa (con m->mu tomado). La envolvente del canal se
/// lleva a cero para que no siga pesando en las ganancias de los demás.
static void soltar(BaAutomixer* m) {
    if (m->canal < 0 || !*m->grupo) return;
    Grupo& g = **m->grupo;
    g.am.setLevelBlock(m->canal, 0.0f, 48000 * 60);
    std::lock_guard<std::mutex> l(g.mu);
    g.ocupados.reset(static_cast<size_t>(m->canal));
    m->canal = -1;
}

/// Toma un lugar libre en la mesa (con m->mu tomado).
static void tomar(BaAutomixer* m) {
    if (m->canal >= 0) return;
    if (!*m->grupo) *m->grupo = grupoDe(*m->nombre);
    Grupo& g = **m->grupo;
    std::lock_guard<std::mutex> l(g.mu);
    for (int i = 0; i < kMaxCanales; ++i)
        if (!g.ocupados.test(static_cast<size_t>(i))) {
            g.ocupados.set(static_cast<size_t>(i));
            m->canal = i;
            return;
        }
    GST_ELEMENT_WARNING(m, RESOURCE, BUSY, ("la mesa «%s» está llena (%d micrófonos)", m->nombre->c_str(), kMaxCanales),
                        (nullptr));
}

static gboolean alArrancar(GstBaseTransform* bt) {
    auto* m = BA_AUTOMIXER(bt);
    std::lock_guard<std::mutex> l(*m->mu);
    tomar(m);
    m->gananciaPrevia = 1.0f;
    return TRUE;
}

static gboolean alParar(GstBaseTransform* bt) {
    auto* m = BA_AUTOMIXER(bt);
    std::lock_guard<std::mutex> l(*m->mu);
    soltar(m);
    return TRUE;
}

static gboolean alNegociar(GstAudioFilter* af, const GstAudioInfo* info) {
    auto* m = BA_AUTOMIXER(af);
    std::lock_guard<std::mutex> l(*m->mu);
    if (!*m->grupo) *m->grupo = grupoDe(*m->nombre);
    Grupo& g = **m->grupo;
    std::lock_guard<std::mutex> lg(g.mu);
    // La mesa trabaja a la frecuencia del último que negoció (de fábrica todas
    // las entradas van a 48 kHz); los tiempos siguen en ms.
    if (std::abs(g.params.fs - GST_AUDIO_INFO_RATE(info)) > 0.5) {
        g.params.fs = GST_AUDIO_INFO_RATE(info);
        g.aplicar(0.0);
    }
    return TRUE;
}

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* buf) {
    auto* m = BA_AUTOMIXER(bt);
    const GstAudioInfo& info = GST_AUDIO_FILTER(m)->info;
    const int canales = GST_AUDIO_INFO_CHANNELS(&info);
    if (canales <= 0) return GST_FLOW_NOT_NEGOTIATED;
    std::shared_ptr<Grupo> g;
    int canal;
    {
        std::lock_guard<std::mutex> l(*m->mu);
        g = *m->grupo;
        canal = m->canal;
    }
    if (!g || canal < 0) return GST_FLOW_OK; // sin lugar: pasa como entra
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) return GST_FLOW_ERROR;
    auto* s = reinterpret_cast<float*>(map.data);
    const int frames = static_cast<int>(map.size / (sizeof(float) * static_cast<size_t>(canales)));
    const size_t n = static_cast<size_t>(frames) * static_cast<size_t>(canales);
    const bool silencio = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_GAP);
    double acc = 0.0;
    if (!silencio)
        for (size_t i = 0; i < n; ++i)
            acc += static_cast<double>(s[i]) * s[i];
    const float rms = n > 0 && !silencio ? static_cast<float>(std::sqrt(acc / static_cast<double>(n))) : 0.0f;
    m->nivel.store(rms, std::memory_order_relaxed);
    if (!m->activo.load(std::memory_order_relaxed)) {
        g->am.setLevelBlock(canal, 0.0f, frames); // no cuenta en la mesa
        m->gananciaPrevia = 1.0f;
        m->ganancia.store(1.0f, std::memory_order_relaxed);
        gst_buffer_unmap(buf, &map);
        return GST_FLOW_OK;
    }
    g->am.setLevelBlock(canal, rms, frames);
    const float objetivo = g->am.gainFor(canal);
    const float desde = m->gananciaPrevia;
    if (!silencio && frames > 0) {
        const float paso = (objetivo - desde) / static_cast<float>(frames);
        float k = desde;
        for (int f = 0; f < frames; ++f) {
            k += paso;
            float* p = s + static_cast<size_t>(f) * static_cast<size_t>(canales);
            for (int c = 0; c < canales; ++c)
                p[c] *= k;
        }
    }
    m->gananciaPrevia = objetivo;
    m->ganancia.store(objetivo, std::memory_order_relaxed);
    gst_buffer_unmap(buf, &map);
    return GST_FLOW_OK;
}

/// Un parámetro de la mesa: se cambia en el grupo (para todos sus elementos).
template <typename F>
static void conParams(BaAutomixer* m, bool escribir, F&& f) {
    std::shared_ptr<Grupo> g;
    {
        std::lock_guard<std::mutex> l(*m->mu);
        if (!*m->grupo) *m->grupo = grupoDe(*m->nombre);
        g = *m->grupo;
    }
    std::lock_guard<std::mutex> lg(g->mu);
    f(g->params);
    if (escribir) g->aplicar(50.0);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* m = BA_AUTOMIXER(o);
    switch (id) {
        case PROP_GRUPO: {
            const gchar* x = g_value_get_string(v);
            const std::string nuevo = x != nullptr && *x != '\0' ? x : "mesa";
            std::lock_guard<std::mutex> l(*m->mu);
            if (nuevo == *m->nombre) return;
            const bool tenia = m->canal >= 0;
            soltar(m);
            *m->nombre = nuevo;
            *m->grupo = grupoDe(nuevo);
            if (tenia) tomar(m);
            return;
        }
        case PROP_ACTIVO: m->activo.store(g_value_get_boolean(v)); return;
        case PROP_UMBRAL: conParams(m, true, [v](auto& p) { p.openThresholdDb = g_value_get_double(v); }); return;
        case PROP_HOLD: conParams(m, true, [v](auto& p) { p.holdMs = g_value_get_double(v); }); return;
        case PROP_PISO_HOLD: conParams(m, true, [v](auto& p) { p.holdFloorDb = g_value_get_double(v); }); return;
        case PROP_MAX_ABIERTOS: conParams(m, true, [v](auto& p) { p.maxOpenMics = g_value_get_int(v); }); return;
        case PROP_ATAQUE: conParams(m, true, [v](auto& p) { p.attackMs = g_value_get_double(v); }); return;
        case PROP_LIBERACION: conParams(m, true, [v](auto& p) { p.releaseMs = g_value_get_double(v); }); return;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* m = BA_AUTOMIXER(o);
    switch (id) {
        case PROP_GRUPO: {
            std::lock_guard<std::mutex> l(*m->mu);
            g_value_set_string(v, m->nombre->c_str());
            return;
        }
        case PROP_ACTIVO: g_value_set_boolean(v, m->activo.load()); return;
        case PROP_UMBRAL: conParams(m, false, [v](auto& p) { g_value_set_double(v, p.openThresholdDb); }); return;
        case PROP_HOLD: conParams(m, false, [v](auto& p) { g_value_set_double(v, p.holdMs); }); return;
        case PROP_PISO_HOLD: conParams(m, false, [v](auto& p) { g_value_set_double(v, p.holdFloorDb); }); return;
        case PROP_MAX_ABIERTOS: conParams(m, false, [v](auto& p) { g_value_set_int(v, p.maxOpenMics); }); return;
        case PROP_ATAQUE: conParams(m, false, [v](auto& p) { g_value_set_double(v, p.attackMs); }); return;
        case PROP_LIBERACION: conParams(m, false, [v](auto& p) { g_value_set_double(v, p.releaseMs); }); return;
        case PROP_CANAL: {
            std::lock_guard<std::mutex> l(*m->mu);
            g_value_set_int(v, m->canal);
            return;
        }
        case PROP_GANANCIA: g_value_set_double(v, aDb(m->ganancia.load())); return;
        case PROP_NIVEL: g_value_set_double(v, aDb(m->nivel.load())); return;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* m = BA_AUTOMIXER(o);
    {
        std::lock_guard<std::mutex> l(*m->mu);
        soltar(m);
    }
    delete m->grupo;
    delete m->nombre;
    delete m->mu;
    G_OBJECT_CLASS(ba_automixer_parent_class)->finalize(o);
}

static void ba_automixer_class_init(BaAutomixerClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_automixer_debug, "baautomixer", 0, "Automixer de mesa redonda");
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    auto* af = GST_AUDIO_FILTER_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    constexpr auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    constexpr auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(
        go, PROP_GRUPO,
        g_param_spec_string("grupo", "Grupo", "La mesa: los baautomixer con el mismo grupo se reparten la ganancia", "mesa", rw));
    g_object_class_install_property(
        go, PROP_ACTIVO,
        g_param_spec_boolean("activo", "Activo", "false: el micrófono pasa como entra y no cuenta en la mesa", TRUE, rw));
    g_object_class_install_property(go, PROP_UMBRAL,
                                    g_param_spec_double("umbral-db", "Umbral", "Desde qué nivel un micrófono cuenta como abierto (dBFS, de la mesa)",
                                                        -90.0, 0.0, -40.0, rw));
    g_object_class_install_property(
        go, PROP_HOLD,
        g_param_spec_double("hold-ms", "Hold", "Cuánto sostiene al último que habló después de callarse (ms, de la mesa)", 0.0,
                            5000.0, 300.0, rw));
    g_object_class_install_property(go, PROP_PISO_HOLD,
                                    g_param_spec_double("piso-hold-db", "Piso del hold",
                                                        "A cuánto queda el que acaba de hablar durante el hold (dB, de la mesa)",
                                                        -60.0, 0.0, -20.0, rw));
    g_object_class_install_property(
        go, PROP_MAX_ABIERTOS,
        g_param_spec_int("max-abiertos", "Máximo abiertos", "Desde cuántos abiertos baja cada uno para que la suma no crezca (de la mesa)",
                         1, kMaxCanales, 4, rw));
    g_object_class_install_property(go, PROP_ATAQUE,
                                    g_param_spec_double("ataque-ms", "Ataque", "Envolvente: subida (ms, de la mesa)", 0.0, 500.0, 5.0, rw));
    g_object_class_install_property(
        go, PROP_LIBERACION,
        g_param_spec_double("liberacion-ms", "Liberación", "Envolvente: bajada (ms, de la mesa)", 0.0, 5000.0, 200.0, rw));
    g_object_class_install_property(
        go, PROP_CANAL, g_param_spec_int("canal", "Canal", "El lugar en la mesa (-1: sin lugar)", -1, kMaxCanales - 1, -1, ro));
    g_object_class_install_property(go, PROP_GANANCIA,
                                    g_param_spec_double("ganancia-db", "Ganancia", "La que aplica ahora (dB)", -180.0, 0.0, 0.0, ro));
    g_object_class_install_property(go, PROP_NIVEL,
                                    g_param_spec_double("nivel-db", "Nivel", "El nivel RMS que entra (dBFS)", -180.0, 20.0, -180.0, ro));
    gst_element_class_set_static_metadata(ge, "Automixer de mesa redonda", "Filter/Effect/Audio",
                                          "Reparte la ganancia entre los micrófonos de una mesa (Dugan): el que habla pasa al frente",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string(CAPS);
    gst_audio_filter_class_add_pad_templates(af, caps);
    gst_caps_unref(caps);
    bt->transform_ip = transformar;
    bt->transform_ip_on_passthrough = FALSE;
    bt->start = alArrancar;
    bt->stop = alParar;
    af->setup = alNegociar;
}

static void ba_automixer_init(BaAutomixer* m) {
    m->mu = new std::mutex();
    m->nombre = new std::string("mesa");
    m->grupo = new std::shared_ptr<Grupo>();
    m->canal = -1;
    m->activo.store(true);
    m->gananciaPrevia = 1.0f;
    m->ganancia.store(1.0f);
    m->nivel.store(0.0f);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(m), TRUE);
}

extern "C" gboolean ba_registrar_automixer(GstPlugin* p) {
    return gst_element_register(p, "baautomixer", GST_RANK_NONE, BA_TYPE_AUTOMIXER);
}
