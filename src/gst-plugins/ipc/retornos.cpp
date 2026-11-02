// ============================================================================
// src/gst-plugins/ipc/retornos.cpp — baretornos: el mix-minus de todos los invitados
//
// Lo que oye cada invitado es el programa SIN su propia voz. Armar una mezcla por
// invitado con todos los demás cuesta N×N (con 40 lugares: 1600 ramas, cada una con
// su hilo). Este elemento es un mezclador (GstAudioAggregator, como audiomixer) que
// SUMA UNA VEZ todas sus entradas y, para cada invitado, escribe en su anillo la suma
// MENOS lo que aportó su voz, en el mismo bloque y con la misma ganancia: la resta es
// exacta (lo que queda de su voz es el redondeo del float, −140 dB) y no agrega
// demora. El costo crece con las entradas y los invitados, no con su producto.
//
//   - Cada pad (una entrada) tiene `volume` (la ganancia de escena, como el pad de
//     audiomixer) y `oyente`: el invitado cuya voz es (0 = de nadie). Lo que suma un
//     pad con oyente n se le resta al retorno de n.
//   - `anillos` dice qué retornos escribe: «n=canal» separados por «;» (p. ej.
//     «1=BA.Audio.Return.1;2=BA.Audio.Return.2»). Cambia andando: los que siguen no se
//     cortan, los nuevos se abren, los que se van se cierran (el gateway le apaga el
//     retorno a ese invitado).
//   - Cada retorno pasa por su limitador true peak (ba::dsp::LimitadorTP; `techo-db`,
//     `lookahead-ms`): lo que oye el invitado tampoco satura. Lo escribe un
//     EscritorAnillo (escritor.hpp): bloques de 10 ms con la hora del gateway.
//   - Por su salida (src) sale la suma entera (el programa de oído, con todas las
//     voces): sirve para medir; el motor la tira (fakesink).
//
// Cuando pide más datos espera a sus entradas sin despertarse con cada bloque
// (esperarEntradas, comun.hpp); bamezcla (abajo) es lo mismo sin los retornos.
//
// Escribe al anillo cuando el mezclador termina el bloque (en vivo: cuando llegaron
// todas o venció la espera, min-upstream-latency), sin la espera del sink: el retorno
// llega igual o antes que con un baanillosink atrás.
//
//   gst-launch-1.0 baretornos name=r anillos="1=BA.Audio.Return.1" force-live=true ! fakesink \
//       audiotestsrc is-live=true ! r.sink_0   audiotestsrc is-live=true freq=660 ! r.sink_1
//   (y `oyente`=1 en r.sink_0, desde el código: gst-launch no pone propiedades de pads pedidos)
// ============================================================================

#include "comun.hpp"
#include "escritor.hpp"

#include "dsp/LimitadorTP.hpp"

#include <gst/audio/audio.h>
#include <gst/audio/gstaudioaggregator.h>
#include <gst/gst.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// --- El pad: una entrada, con su ganancia de escena y de quién es la voz ---

G_BEGIN_DECLS
#define BA_TYPE_RETORNOS_PAD (ba_retornos_pad_get_type())
G_DECLARE_FINAL_TYPE(BaRetornosPad, ba_retornos_pad, BA, RETORNOS_PAD, GstAudioAggregatorConvertPad)
#define BA_TYPE_RETORNOS (ba_retornos_get_type())
G_DECLARE_FINAL_TYPE(BaRetornos, ba_retornos, BA, RETORNOS, GstAudioAggregator)
G_END_DECLS

struct _BaRetornosPad {
    GstAudioAggregatorConvertPad parent;
    double volumen; ///< con el OBJECT_LOCK del pad
    int oyente;     ///< el invitado cuya voz es (0 = nadie); con el OBJECT_LOCK del pad
};

G_DEFINE_TYPE(BaRetornosPad, ba_retornos_pad, GST_TYPE_AUDIO_AGGREGATOR_CONVERT_PAD)

enum { PROP_PAD_0, PROP_PAD_VOLUME, PROP_PAD_OYENTE };

static void padPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* p = BA_RETORNOS_PAD(o);
    GST_OBJECT_LOCK(p);
    switch (id) {
        case PROP_PAD_VOLUME: p->volumen = g_value_get_double(v); break;
        case PROP_PAD_OYENTE: p->oyente = g_value_get_int(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
    GST_OBJECT_UNLOCK(p);
}

static void padLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* p = BA_RETORNOS_PAD(o);
    GST_OBJECT_LOCK(p);
    switch (id) {
        case PROP_PAD_VOLUME: g_value_set_double(v, p->volumen); break;
        case PROP_PAD_OYENTE: g_value_set_int(v, p->oyente); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
    GST_OBJECT_UNLOCK(p);
}

static void ba_retornos_pad_class_init(BaRetornosPadClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = padPoner;
    go->get_property = padLeer;
    const auto f = static_cast<GParamFlags>(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_PAD_VOLUME, g_param_spec_double("volume", "Volumen", "La ganancia de esta entrada (escena)", 0.0, 10.0, 1.0, f));
    g_object_class_install_property(go, PROP_PAD_OYENTE,
                                    g_param_spec_int("oyente", "Oyente", "El invitado cuya voz es esta entrada: no la oye en su retorno (0 = nadie)",
                                                     0, G_MAXINT, 0, f));
}

static void ba_retornos_pad_init(BaRetornosPad* p) {
    p->volumen = 1.0;
    p->oyente = 0;
}

// --- El elemento ---

namespace {
/// Un retorno: su anillo, su limitador y el bloque donde se arma.
struct Retorno {
    std::string canal;
    ba::ipc_gst::EscritorAnillo escritor;
    ba::dsp::LimitadorTP limitador;
    std::vector<float> bloque;
};
} // namespace

struct _BaRetornos {
    GstAudioAggregator parent;
    std::mutex* mu; ///< los retornos y sus ajustes (el hilo del mezclador contra el que los cambia)
    std::map<int, std::unique_ptr<Retorno>>* retornos;
    std::string* anillos;
    double techo, anticipo;
    // Del hilo del mezclador (sin candado): lo que aportó la voz de cada oyente al bloque en curso.
    std::map<int, std::vector<float>>* propias;
    std::set<int>* tocadas; ///< los oyentes que aportaron algo al bloque en curso
    guint cuadros;          ///< los del bloque en curso
};

G_DEFINE_TYPE(BaRetornos, ba_retornos, GST_TYPE_AUDIO_AGGREGATOR)

enum { PROP_0, PROP_ANILLOS, PROP_TECHO, PROP_LOOKAHEAD };

namespace {
constexpr std::size_t kCanales = ba::ipc_gst::EscritorAnillo::kCanales;

/// «1=BA.Audio.Return.1;2=…» → {1: canal, 2: …}. Lo que no se entiende, afuera.
std::map<int, std::string> leerAnillos(const std::string& s) {
    std::map<int, std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const auto igual = item.find('=');
        if (igual == std::string::npos || igual + 1 >= item.size()) continue;
        const int n = std::atoi(item.substr(0, igual).c_str());
        if (n > 0) out[n] = item.substr(igual + 1);
    }
    return out;
}

void prepararLimitador(ba::dsp::LimitadorTP& l, double techo, double anticipo) {
    l.ponerTecho(techo);
    l.ponerLookahead(anticipo);
    l.preparar(static_cast<int>(kCanales), 48000);
}

/// Pone los retornos que pide `anillos`: los que siguen igual no se tocan.
void aplicarAnillos(BaRetornos* r) {
    const std::map<int, std::string> pedidos = leerAnillos(*r->anillos);
    for (auto it = r->retornos->begin(); it != r->retornos->end();) {
        auto p = pedidos.find(it->first);
        if (p == pedidos.end() || p->second != it->second->canal)
            it = r->retornos->erase(it); // cierra su anillo: el gateway lo nota
        else
            ++it;
    }
    for (const auto& [n, canal] : pedidos) {
        if (r->retornos->count(n) > 0) continue;
        auto ret = std::make_unique<Retorno>();
        ret->canal = canal;
        prepararLimitador(ret->limitador, r->techo, r->anticipo);
        if (!ret->escritor.abrir(canal))
            GST_ELEMENT_WARNING(r, RESOURCE, OPEN_WRITE, ("no se pudo crear el anillo %s", canal.c_str()), (nullptr));
        (*r->retornos)[n] = std::move(ret);
    }
}
} // namespace

static void poner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* r = BA_RETORNOS(o);
    std::lock_guard<std::mutex> l(*r->mu);
    switch (id) {
        case PROP_ANILLOS: {
            const gchar* x = g_value_get_string(v);
            *r->anillos = x != nullptr ? x : "";
            aplicarAnillos(r);
            break;
        }
        case PROP_TECHO:
            r->techo = g_value_get_double(v);
            for (auto& [n, ret] : *r->retornos) ret->limitador.ponerTecho(r->techo);
            break;
        case PROP_LOOKAHEAD:
            r->anticipo = g_value_get_double(v);
            for (auto& [n, ret] : *r->retornos) prepararLimitador(ret->limitador, r->techo, r->anticipo);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* r = BA_RETORNOS(o);
    std::lock_guard<std::mutex> l(*r->mu);
    switch (id) {
        case PROP_ANILLOS: g_value_set_string(v, r->anillos->c_str()); break;
        case PROP_TECHO: g_value_set_double(v, r->techo); break;
        case PROP_LOOKAHEAD: g_value_set_double(v, r->anticipo); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

/// Un bloque nuevo: lo de cada voz empieza en cero.
static GstBuffer* crearSalida(GstAudioAggregator* aagg, guint cuadros) {
    auto* r = BA_RETORNOS(aagg);
    r->cuadros = cuadros;
    r->tocadas->clear(); // lo de cada una se pone en cero cuando aporta (sumarUna)
    return GST_AUDIO_AGGREGATOR_CLASS(ba_retornos_parent_class)->create_output_buffer(aagg, cuadros);
}

/// Suma una entrada al bloque (y, si es la voz de alguien, a lo suyo).
static gboolean sumarUna(GstAudioAggregator* aagg, GstAudioAggregatorPad* apad, GstBuffer* entra, guint desde, GstBuffer* sale,
                         guint hasta, guint cuadros) {
    auto* r = BA_RETORNOS(aagg);
    auto* p = BA_RETORNOS_PAD(apad);
    GST_OBJECT_LOCK(p);
    const float vol = static_cast<float>(p->volumen);
    const int oyente = p->oyente;
    GST_OBJECT_UNLOCK(p);
    if (vol <= 0.0f) return FALSE;
    GstMapInfo mi, mo;
    if (!gst_buffer_map(entra, &mi, GST_MAP_READ)) return FALSE;
    if (!gst_buffer_map(sale, &mo, GST_MAP_READWRITE)) {
        gst_buffer_unmap(entra, &mi);
        return FALSE;
    }
    const float* x = reinterpret_cast<const float*>(mi.data) + static_cast<std::size_t>(desde) * kCanales;
    float* s = reinterpret_cast<float*>(mo.data) + static_cast<std::size_t>(hasta) * kCanales;
    const std::size_t n = static_cast<std::size_t>(cuadros) * kCanales;
    if (oyente > 0) {
        std::vector<float>& propia = (*r->propias)[oyente];
        if (r->tocadas->insert(oyente).second) propia.assign(static_cast<std::size_t>(r->cuadros) * kCanales, 0.0f);
        float* q = propia.data() + static_cast<std::size_t>(hasta) * kCanales;
        for (std::size_t i = 0; i < n; ++i) {
            const float v = vol * x[i];
            s[i] += v;
            q[i] += v;
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) s[i] += vol * x[i];
    }
    gst_buffer_unmap(sale, &mo);
    gst_buffer_unmap(entra, &mi);
    return TRUE;
}

/// El bloque está: a cada retorno, la suma menos su voz, limitada, a su anillo.
static GstFlowReturn terminar(GstAggregator* agg, GstBuffer* buf) {
    auto* r = BA_RETORNOS(agg);
    GstMapInfo m;
    if (gst_buffer_map(buf, &m, GST_MAP_READ)) {
        const auto* suma = reinterpret_cast<const float*>(m.data);
        const std::size_t cuadros = m.size / (sizeof(float) * kCanales), n = cuadros * kCanales;
        GstClockTime rt = GST_CLOCK_TIME_NONE;
        if (GST_BUFFER_PTS_IS_VALID(buf))
            rt = gst_segment_to_running_time(&GST_AGGREGATOR_PAD(agg->srcpad)->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buf));
        std::lock_guard<std::mutex> l(*r->mu);
        for (auto& [oyente, ret] : *r->retornos) {
            if (!ret->escritor.abierto()) continue;
            ret->bloque.assign(suma, suma + n);
            if (r->tocadas->count(oyente) > 0) {
                const std::vector<float>& propia = (*r->propias)[oyente];
                for (std::size_t i = 0; i < n && i < propia.size(); ++i) ret->bloque[i] -= propia[i];
            }
            ret->limitador.procesar(ret->bloque.data(), cuadros);
            ret->escritor.escribir(GST_ELEMENT(agg), ret->bloque.data(), cuadros, rt, false);
        }
        gst_buffer_unmap(buf, &m);
    }
    return GST_AGGREGATOR_CLASS(ba_retornos_parent_class)->finish_buffer(agg, buf);
}

/// Lo del agregador; si pide más datos (sin que venza su espera), espera a sus entradas
/// sin que cada push lo despierte (comun.hpp: esperarEntradas).
static GstFlowReturn agregar(GstAggregator* agg, gboolean vencio) {
    const GstFlowReturn r = GST_AGGREGATOR_CLASS(ba_retornos_parent_class)->aggregate(agg, vencio);
    if (r == GST_AGGREGATOR_FLOW_NEED_DATA && !vencio) ba::ipc_gst::esperarEntradas(agg);
    return r;
}

static void finalizar(GObject* o) {
    auto* r = BA_RETORNOS(o);
    delete r->retornos;
    delete r->propias;
    delete r->tocadas;
    delete r->anillos;
    delete r->mu;
    G_OBJECT_CLASS(ba_retornos_parent_class)->finalize(o);
}

static GstStaticPadTemplate kSale = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)48000, channels=(int)2"));
static GstStaticPadTemplate kEntra = GST_STATIC_PAD_TEMPLATE("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS("audio/x-raw"));

static void ba_retornos_class_init(BaRetornosClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ag = GST_AGGREGATOR_CLASS(k);
    auto* aa = GST_AUDIO_AGGREGATOR_CLASS(k);
    go->set_property = poner;
    go->get_property = leer;
    go->finalize = finalizar;
    g_object_class_install_property(go, PROP_ANILLOS,
                                    g_param_spec_string("anillos", "Anillos", "Los retornos que escribe: «n=canal;…» (p. ej. 1=BA.Audio.Return.1)", "",
                                                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(go, PROP_TECHO,
                                    g_param_spec_double("techo-db", "Techo", "El techo del limitador de cada retorno (dBTP)", -20.0, 0.0, -1.0,
                                                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(go, PROP_LOOKAHEAD,
                                    g_param_spec_double("lookahead-ms", "Anticipación", "La anticipación del limitador de cada retorno (ms)", 0.5, 20.0, 1.0,
                                                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_add_static_pad_template_with_gtype(GST_ELEMENT_CLASS(k), &kSale, GST_TYPE_AUDIO_AGGREGATOR_PAD);
    gst_element_class_add_static_pad_template_with_gtype(GST_ELEMENT_CLASS(k), &kEntra, BA_TYPE_RETORNOS_PAD);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Retornos de los invitados (mix-minus)", "Generic/Audio",
                                          "Suma una vez y escribe a cada invitado la suma sin su voz en su anillo del gateway",
                                          "Broadcast Appliance");
    ag->finish_buffer = terminar;
    ag->aggregate = agregar;
    aa->create_output_buffer = crearSalida;
    aa->aggregate_one_buffer = sumarUna;
}

static void ba_retornos_init(BaRetornos* r) {
    r->mu = new std::mutex();
    r->retornos = new std::map<int, std::unique_ptr<Retorno>>();
    r->propias = new std::map<int, std::vector<float>>();
    r->tocadas = new std::set<int>();
    r->anillos = new std::string();
    r->techo = -1.0;
    r->anticipo = 1.0;
    r->cuadros = 0;
}

// --- bamezcla: el mismo mezclador sin los retornos (el programa y los demás buses) ---
//
// Suma sus entradas con la ganancia de cada pad (`volume`, como el pad de audiomixer) y
// espera a sus entradas como baretornos (esperarEntradas): con las batomas de muchos
// invitados, el audiomixer se despertaba con cada bloque de cada uno.

G_BEGIN_DECLS
#define BA_TYPE_MEZCLA (ba_mezcla_get_type())
G_DECLARE_FINAL_TYPE(BaMezcla, ba_mezcla, BA, MEZCLA, GstAudioAggregator)
G_END_DECLS

struct _BaMezcla {
    GstAudioAggregator parent;
};

G_DEFINE_TYPE(BaMezcla, ba_mezcla, GST_TYPE_AUDIO_AGGREGATOR)

static gboolean mezclaSumar(GstAudioAggregator*, GstAudioAggregatorPad* apad, GstBuffer* entra, guint desde, GstBuffer* sale,
                            guint hasta, guint cuadros) {
    auto* p = BA_RETORNOS_PAD(apad);
    GST_OBJECT_LOCK(p);
    const float vol = static_cast<float>(p->volumen);
    GST_OBJECT_UNLOCK(p);
    if (vol <= 0.0f) return FALSE;
    GstMapInfo mi, mo;
    if (!gst_buffer_map(entra, &mi, GST_MAP_READ)) return FALSE;
    if (!gst_buffer_map(sale, &mo, GST_MAP_READWRITE)) {
        gst_buffer_unmap(entra, &mi);
        return FALSE;
    }
    const float* x = reinterpret_cast<const float*>(mi.data) + static_cast<std::size_t>(desde) * kCanales;
    float* s = reinterpret_cast<float*>(mo.data) + static_cast<std::size_t>(hasta) * kCanales;
    const std::size_t n = static_cast<std::size_t>(cuadros) * kCanales;
    if (vol == 1.0f)
        for (std::size_t i = 0; i < n; ++i) s[i] += x[i];
    else
        for (std::size_t i = 0; i < n; ++i) s[i] += vol * x[i];
    gst_buffer_unmap(sale, &mo);
    gst_buffer_unmap(entra, &mi);
    return TRUE;
}

static GstFlowReturn mezclaAgregar(GstAggregator* agg, gboolean vencio) {
    const GstFlowReturn r = GST_AGGREGATOR_CLASS(ba_mezcla_parent_class)->aggregate(agg, vencio);
    if (r == GST_AGGREGATOR_FLOW_NEED_DATA && !vencio) ba::ipc_gst::esperarEntradas(agg);
    return r;
}

static void ba_mezcla_class_init(BaMezclaClass* k) {
    auto* ag = GST_AGGREGATOR_CLASS(k);
    auto* aa = GST_AUDIO_AGGREGATOR_CLASS(k);
    gst_element_class_add_static_pad_template_with_gtype(GST_ELEMENT_CLASS(k), &kSale, GST_TYPE_AUDIO_AGGREGATOR_PAD);
    gst_element_class_add_static_pad_template_with_gtype(GST_ELEMENT_CLASS(k), &kEntra, BA_TYPE_RETORNOS_PAD);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Mezclador de los buses", "Generic/Audio",
                                          "Suma sus entradas (con la ganancia de cada pad) y espera a las batomas sin despertarse "
                                          "con cada bloque",
                                          "Broadcast Appliance");
    ag->aggregate = mezclaAgregar;
    aa->aggregate_one_buffer = mezclaSumar;
}

static void ba_mezcla_init(BaMezcla*) {}

extern "C" gboolean ba_registrar_retornos(GstPlugin* p) {
    return gst_element_register(p, "baretornos", GST_RANK_NONE, BA_TYPE_RETORNOS) &&
           gst_element_register(p, "bamezcla", GST_RANK_NONE, BA_TYPE_MEZCLA);
}
