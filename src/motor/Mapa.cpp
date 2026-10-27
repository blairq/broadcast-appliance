// ============================================================================
// src/motor/Mapa.cpp — ver Mapa.hpp
// ============================================================================
#include "Mapa.hpp"
#include "i18n/I18n.hpp"

#include <gst/base/gstbasetransform.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ba::motor {

using json = nlohmann::json;

namespace {

std::mutex gMu;
std::vector<GstElement*> gVivos; // sin referencia: se sacan al destruirse (weak ref)

void alMorir(gpointer, GObject* muerto) {
    std::lock_guard<std::mutex> l(gMu);
    gVivos.erase(std::remove(gVivos.begin(), gVivos.end(), reinterpret_cast<GstElement*>(muerto)), gVivos.end());
}

/// Texto en UTF-8 válido (los nombres de elementos pueden traer bytes sueltos).
std::string texto(const gchar* s) {
    if (s == nullptr) return "";
    if (g_utf8_validate(s, -1, nullptr)) return s;
    gchar* v = g_utf8_make_valid(s, -1);
    std::string r = v;
    g_free(v);
    return r;
}

/// La memoria de unas caps: la caps feature de memoria, o «sistema».
std::string memoriaDe(GstCaps* c) {
    if (c == nullptr || gst_caps_get_size(c) == 0) return "";
    GstCapsFeatures* f = gst_caps_get_features(c, 0);
    if (f != nullptr && !gst_caps_features_is_any(f))
        for (guint i = 0; i < gst_caps_features_get_size(f); ++i) {
            const std::string n = texto(gst_caps_features_get_nth(f, i));
            if (n.rfind("memory:", 0) == 0 && n != "memory:SystemMemory") return n.substr(7);
        }
    return "sistema";
}

bool esGpu(const std::string& m) { return !m.empty() && m != "sistema"; }

json describirCaps(GstCaps* c) {
    json o = json::object();
    if (c == nullptr || gst_caps_get_size(c) == 0) return o;
    const GstStructure* s = gst_caps_get_structure(c, 0);
    o["medio"] = texto(gst_structure_get_name(s));
    if (const gchar* f = gst_structure_get_string(s, "format")) o["formato"] = f;
    gint w = 0, h = 0, rate = 0, ch = 0;
    if (gst_structure_get_int(s, "width", &w) && gst_structure_get_int(s, "height", &h))
        o["tam"] = std::to_string(w) + "x" + std::to_string(h);
    if (gst_structure_get_int(s, "rate", &rate)) o["frecuencia"] = rate;
    if (gst_structure_get_int(s, "channels", &ch)) o["canales"] = ch;
    o["memoria"] = memoriaDe(c);
    return o;
}

/// El elemento real al otro lado de un pad (atravesando los ghost pads).
GstElement* dueñoReal(GstPad* pad) {
    if (pad == nullptr) return nullptr;
    GstPad* p = GST_PAD(gst_object_ref(pad));
    // Un ghost pad (proxy): su objetivo adentro del bin.
    for (int i = 0; i < 8 && GST_IS_GHOST_PAD(p); ++i) {
        GstPad* t = gst_ghost_pad_get_target(GST_GHOST_PAD(p));
        gst_object_unref(p);
        if (t == nullptr) return nullptr;
        p = t;
    }
    // Un proxy pad interno de un ghost pad: el pad de afuera.
    GstElement* e = GST_ELEMENT(gst_pad_get_parent(p));
    gst_object_unref(p);
    return e;
}

std::string nombreDe(GstElement* e) {
    if (e == nullptr) return "";
    gchar* n = gst_object_get_path_string(GST_OBJECT(e));
    std::string s = texto(n); // ya válido
    g_free(n);
    // Sin el nombre del pipeline (/pipeline12/...).
    const auto a = s.find('/', 1);
    return a != std::string::npos ? s.substr(a + 1) : s;
}

void recorrer(GstBin* bin, std::vector<GstElement*>& out) {
    GstIterator* it = gst_bin_iterate_elements(bin);
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        out.push_back(GST_ELEMENT(gst_object_ref(e)));
        if (GST_IS_BIN(e)) recorrer(GST_BIN(e), out);
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
}

json describirPipeline(GstElement* p) {
    std::vector<GstElement*> elems;
    recorrer(GST_BIN(p), elems);
    json elementos = json::array(), enlaces = json::array(), cpu = json::array(), transiciones = json::array();
    std::set<std::string> lee, escribe;
    for (GstElement* e : elems) {
        GstElementFactory* fab = gst_element_get_factory(e);
        const std::string fabrica = fab != nullptr ? texto(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(fab))) : "";
        const std::string clase =
            fab != nullptr ? texto(gst_element_factory_get_metadata(fab, GST_ELEMENT_METADATA_KLASS)) : "";
        if (GST_IS_BIN(e) && fabrica != "uridecodebin3" && fabrica != "decodebin3" && fabrica != "decodebin" &&
            fabrica != "uridecodebin")
            ; // los bins genéricos no cuentan como trabajo
        const bool hw = clase.find("Hardware") != std::string::npos;
        const std::string nombre = nombreDe(e);
        if (fabrica == "intersrc" || fabrica == "intersink") {
            gchar* prod = nullptr;
            g_object_get(e, "producer-name", &prod, nullptr);
            (fabrica == "intersrc" ? lee : escribe).insert(texto(prod));
            g_free(prod);
        }
        // Memoria de lo que entra y de lo que sale.
        std::set<std::string> entra, sale;
        std::string medio;
        bool entraCrudo = false, saleCrudo = false; ///< video/audio sin comprimir (con formato)
        GstIterator* pit = gst_element_iterate_pads(e);
        GValue pv = G_VALUE_INIT;
        while (gst_iterator_next(pit, &pv) == GST_ITERATOR_OK) {
            auto* pad = GST_PAD(g_value_get_object(&pv));
            GstCaps* c = gst_pad_get_current_caps(pad);
            if (c != nullptr) {
                const json d = describirCaps(c);
                const std::string m = d.value("memoria", std::string());
                if (medio.empty()) medio = d.value("medio", std::string());
                (GST_PAD_DIRECTION(pad) == GST_PAD_SRC ? sale : entra).insert(m);
                if (!d.value("formato", std::string()).empty()) (GST_PAD_DIRECTION(pad) == GST_PAD_SRC ? saleCrudo : entraCrudo) = true;
                if (GST_PAD_DIRECTION(pad) == GST_PAD_SRC && !GST_IS_GHOST_PAD(pad)) {
                    GstPad* peer = gst_pad_get_peer(pad);
                    GstElement* otro = dueñoReal(peer);
                    json en = d;
                    en["desde"] = nombre;
                    en["hacia"] = nombreDe(otro);
                    enlaces.push_back(en);
                    if (otro != nullptr) gst_object_unref(otro);
                    if (peer != nullptr) gst_object_unref(peer);
                }
                gst_caps_unref(c);
            }
            g_value_reset(&pv);
        }
        g_value_unset(&pv);
        gst_iterator_free(pit);

        json el{{"nombre", nombre}, {"fabrica", fabrica}, {"clase", clase}};
        if (hw) el["hardware"] = true;
        const bool gpuEntra = std::any_of(entra.begin(), entra.end(), esGpu);
        const bool gpuSale = std::any_of(sale.begin(), sale.end(), esGpu);
        const bool sisEntra = entra.count("sistema") > 0, sisSale = sale.count("sistema") > 0;
        // Solo cuadros crudos que cruzan: un decodificador de la placa recibe el
        // comprimido (no es una subida) y un bin repite lo de sus elementos.
        const bool cruza = entraCrudo && saleCrudo && !GST_IS_BIN(e);
        if (cruza && gpuEntra && sisSale && !gpuSale)
            transiciones.push_back({{"elemento", nombre}, {"fabrica", fabrica}, {"tipo", "bajada a la CPU"},
                                    {"de", *std::find_if(entra.begin(), entra.end(), esGpu)}});
        if (cruza && sisEntra && gpuSale && !gpuEntra)
            transiciones.push_back({{"elemento", nombre}, {"fabrica", fabrica}, {"tipo", "subida a la GPU"},
                                    {"a", *std::find_if(sale.begin(), sale.end(), esGpu)}});
        // Trabajo en CPU: procesa video crudo en memoria de sistema.
        // Un transform en passthrough no toca los cuadros (mismo formato y tamaño).
        bool enPassthrough = GST_IS_BASE_TRANSFORM(e) && gst_base_transform_is_passthrough(GST_BASE_TRANSFORM(e));
        // Tampoco tocan píxeles: deinterlace con señal progresiva y videorate
        // (solo repite o saltea cuadros: mueve punteros).
        if (fabrica == "videorate") enPassthrough = true;
        if (fabrica == "deinterlace")
            if (GstPad* sk = gst_element_get_static_pad(e, "sink")) {
                if (GstCaps* c = gst_pad_get_current_caps(sk)) {
                    const gchar* im = gst_structure_get_string(gst_caps_get_structure(c, 0), "interlace-mode");
                    enPassthrough = im == nullptr || std::string(im) == "progressive";
                    gst_caps_unref(c);
                }
                gst_object_unref(sk);
            }
        if (enPassthrough) el["passthrough"] = true;
        const bool pasante = enPassthrough || fabrica == "queue" || fabrica == "capsfilter" || fabrica == "identity" || fabrica == "tee" ||
                             fabrica == "intersrc" || fabrica == "intersink" || fabrica == "appsink" ||
                             fabrica == "appsrc" || fabrica == "fakesink" || fabrica == "fallbackswitch" ||
                             fabrica == "imagefreeze" || fabrica == "splitmuxsink" || GST_IS_BIN(e);
        const bool video = medio.rfind("video/x-raw", 0) == 0;
        const bool trabajaVideo = clase.find("Filter") != std::string::npos || clase.find("Converter") != std::string::npos ||
                                  clase.find("Mixer") != std::string::npos || clase.find("Effect") != std::string::npos ||
                                  clase.find("Compositor") != std::string::npos;
        if (!pasante && video && sisSale && !gpuSale && (sisEntra || entra.empty()) && trabajaVideo)
            cpu.push_back({{"elemento", nombre}, {"fabrica", fabrica}, {"que", ba::i18n::traducir("motor", "procesa video en memoria de sistema")}});
        if ((clase.find("Decoder") != std::string::npos || clase.find("Encoder") != std::string::npos) && !hw &&
            clase.find("Video") != std::string::npos)
            cpu.push_back({{"elemento", nombre},
                           {"fabrica", fabrica},
                           {"que", clase.find("Decoder") != std::string::npos
                                       ? ba::i18n::traducir("motor", "decodifica video en CPU (no es de hardware)")
                                       : ba::i18n::traducir("motor", "codifica video en CPU (no es de hardware)")}});
        elementos.push_back(el);
    }
    for (GstElement* e : elems) gst_object_unref(e);
    std::string etiqueta;
    for (const auto& s : escribe) etiqueta += (etiqueta.empty() ? "→ " : ", ") + s;
    std::string entraDe;
    for (const auto& s : lee) entraDe += (entraDe.empty() ? "" : ", ") + s;
    return {{"pipeline", texto(GST_OBJECT_NAME(p))},
            {"lee", json(std::vector<std::string>(lee.begin(), lee.end()))},
            {"escribe", json(std::vector<std::string>(escribe.begin(), escribe.end()))},
            {"etiqueta", (entraDe.empty() ? std::string("(origen)") : entraDe) + " " + (etiqueta.empty() ? "→ (salida)" : etiqueta)},
            {"elementos", elementos},
            {"enlaces", enlaces},
            {"cpu", cpu},
            {"transiciones", transiciones}};
}

} // namespace

void registrarPipeline(GstElement* pipeline) {
    std::lock_guard<std::mutex> l(gMu);
    if (std::find(gVivos.begin(), gVivos.end(), pipeline) != gVivos.end()) return;
    gVivos.push_back(pipeline);
    g_object_weak_ref(G_OBJECT(pipeline), alMorir, nullptr);
}

json mapa() {
    std::vector<GstElement*> vivos;
    {
        std::lock_guard<std::mutex> l(gMu);
        for (GstElement* p : gVivos) vivos.push_back(GST_ELEMENT(gst_object_ref(p)));
    }
    json ps = json::array();
    int nCpu = 0, nTrans = 0, nGpu = 0;
    std::map<std::string, int> memorias;
    for (GstElement* p : vivos) {
        json d = describirPipeline(p);
        nCpu += static_cast<int>(d["cpu"].size());
        nTrans += static_cast<int>(d["transiciones"].size());
        for (const auto& en : d["enlaces"]) {
            const std::string m = en.value("memoria", std::string());
            if (!m.empty() && en.value("medio", std::string()).rfind("video/x-raw", 0) == 0) {
                ++memorias[m];
                nGpu += esGpu(m) ? 1 : 0;
            }
        }
        ps.push_back(std::move(d));
        gst_object_unref(p);
    }
    return {{"pipelines", ps},
            {"resumen",
             {{"pipelines", ps.size()},
              {"enlaces_de_video_por_memoria", memorias},
              {"trabajos_en_cpu", nCpu},
              {"transiciones", nTrans}}}};
}

} // namespace ba::motor
