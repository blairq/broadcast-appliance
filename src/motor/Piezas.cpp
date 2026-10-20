// ============================================================================
// src/motor/Piezas.cpp — ver Piezas.hpp
// ============================================================================
#include "Piezas.hpp"
#include "ModosVideo.hpp"
#include "Capturas.hpp" // indiceCamaraMac
#include "diag/Utf8.hpp"

#include <set>
#include <thread>
#include "Reloj.hpp"

#include <gst/pbutils/pbutils.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

namespace ba::motor {

using json = nlohmann::json;

std::string colorimetriaDelLienzo(const Lienzo& l) {
    GstVideoColorimetry c = {};
    gst_video_colorimetry_from_string(&c, l.color.c_str());
    c.range = l.rangoCompleto ? GST_VIDEO_COLOR_RANGE_0_255 : GST_VIDEO_COLOR_RANGE_16_235;
    gchar* col = gst_video_colorimetry_to_string(&c);
    std::string out = col != nullptr ? col : l.color;
    g_free(col);
    return out;
}

std::string capsColorDelLienzo(const Lienzo& l) {
    const bool diez = l.diezBits || l.color == "bt2100-pq" || l.color == "bt2100-hlg";
    return std::string("video/x-raw,format=") + (diez ? "P010_10LE" : "NV12") + ",colorimetry=" + colorimetriaDelLienzo(l);
}

namespace {
std::string& colorSalida() {
    static std::string c = "bt709";
    return c;
}
} // namespace

void fijarColorSalida(const Lienzo& l) { colorSalida() = colorimetriaDelLienzo(l); }

std::string comoTexto(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) {
        char b[64];
        std::snprintf(b, sizeof b, "%.6g", v.get<double>());
        for (char* c = b; *c; ++c)
            if (*c == ',') *c = '.'; // sin locale
        return b;
    }
    return {};
}

std::vector<Filtro> leerFiltros(const json& lista) {
    std::vector<Filtro> out;
    if (!lista.is_array()) return out;
    for (std::size_t i = 0; i < lista.size(); ++i) {
        const json& x = lista[i];
        if (!x.is_object()) continue;
        Filtro f;
        f.id = x.value("id", std::string());
        f.elemento = x.value("elemento", std::string());
        f.memoria = x.value("memoria", std::string());
        f.saleEnSistema = x.contains("memoria_salida") && x.value("memoria_salida", std::string("x")).empty();
        if (f.id.empty()) f.id = std::to_string(i);
        f.nombre = f.id;
        for (char& c : f.nombre)
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        const json pr = x.value("propiedades", json::object());
        for (auto it = pr.begin(); pr.is_object() && it != pr.end(); ++it) {
            const std::string v = comoTexto(it.value());
            if (!it.value().is_string() || !v.empty()) f.props.emplace_back(it.key(), v);
        }
        if (!f.elemento.empty()) out.push_back(std::move(f));
    }
    nombresUnicos(out);
    return out;
}

void nombresUnicos(std::vector<Filtro>& v) {
    // Limpio, «eq-1» y «eq.1» quedan iguales (y dos sin id, en dos listas, «0»): el
    // segundo elemento no entraba al pipeline. «_antes» es de la derivación (cadenaDe).
    std::set<std::string> usados;
    for (Filtro& f : v) {
        std::string n = f.nombre;
        if (n.size() >= 6 && n.compare(n.size() - 6, 6, "_antes") == 0) n += "_f";
        const std::string base = n;
        for (int i = 2; usados.count(n) > 0; ++i) n = base + "_" + std::to_string(i);
        f.nombre = n;
        usados.insert(n);
    }
}

namespace {
/// La especificación de una propiedad del elemento (sin instanciarlo).
GParamSpec* especDe(const std::string& elemento, const std::string& prop) {
    GstElementFactory* fac = gst_element_factory_find(elemento.c_str());
    if (fac == nullptr) return nullptr;
    GType t = gst_element_factory_get_element_type(fac);
    if (t == 0) {
        auto* cargada = GST_ELEMENT_FACTORY(gst_plugin_feature_load(GST_PLUGIN_FEATURE(fac)));
        if (cargada != nullptr) {
            t = gst_element_factory_get_element_type(cargada);
            gst_object_unref(cargada);
        }
    }
    gst_object_unref(fac);
    if (t == 0) return nullptr;
    auto* k = static_cast<GObjectClass*>(g_type_class_ref(t));
    GParamSpec* ps = g_object_class_find_property(k, prop.c_str());
    g_type_class_unref(k);
    return ps;
}
} // namespace

std::string firmaDe(const std::vector<Filtro>& v, const std::vector<Filtro>& a, bool alfa) {
    std::string r = alfa ? "A|" : "|";
    for (const auto* l : {&v, &a}) {
        for (const auto& f : *l) {
            r += f.id + "=" + f.elemento + "@" + f.memoria + "{";
            for (const auto& [k, val] : f.props) {
                GParamSpec* ps = especDe(f.elemento, k);
                if (ps != nullptr && (ps->flags & GST_PARAM_MUTABLE_PLAYING) != 0) continue;
                r += k + ":" + val + ";";
            }
            r += "}";
        }
        r += "#";
    }
    return r;
}

void ponerPropiedades(GstElement* e, const Filtro& f, std::vector<std::string>* avisos) {
    for (const auto& [k, v] : f.props) {
        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), k.c_str());
        if (ps == nullptr || (ps->flags & G_PARAM_WRITABLE) == 0) {
            if (avisos != nullptr) avisos->push_back(f.elemento + ": no tiene la propiedad «" + k + "»");
            continue;
        }
        gst_util_set_object_arg(G_OBJECT(e), k.c_str(), v.c_str());
    }
}

bool ponerEnVivo(GstElement* pipeline, const std::vector<Filtro>& v, const char* prefijo) {
    bool todas = true;
    for (const auto& f : v) {
        const std::string n = prefijo + f.nombre;
        GstElement* e = gst_bin_get_by_name(GST_BIN(pipeline), n.c_str());
        if (e == nullptr) {
            todas = false;
            continue;
        }
        ponerPropiedades(e, f, nullptr);
        gst_object_unref(e);
    }
    return todas;
}

bool hayFabrica(const std::string& elemento) {
    GstElementFactory* fac = gst_element_factory_find(elemento.c_str());
    if (fac == nullptr) return false;
    gst_object_unref(fac);
    return true;
}

namespace {
/// ¿La fábrica tiene todas esas propiedades? (para cambiar un filtro por su par de GL)
bool tieneLasPropiedades(const std::string& fabrica, const std::vector<std::pair<std::string, std::string>>& props) {
    GstElementFactory* f = gst_element_factory_find(fabrica.c_str());
    if (f == nullptr) return false;
    GstPluginFeature* cargada = gst_plugin_feature_load(GST_PLUGIN_FEATURE(f));
    gst_object_unref(f);
    if (cargada == nullptr) return false;
    const GType t = gst_element_factory_get_element_type(GST_ELEMENT_FACTORY(cargada));
    gst_object_unref(cargada);
    if (t == 0) return false;
    auto* k = static_cast<GObjectClass*>(g_type_class_ref(t));
    bool todas = true;
    for (const auto& [n, v] : props)
        if (g_object_class_find_property(k, n.c_str()) == nullptr) todas = false;
    g_type_class_unref(k);
    return todas;
}
/// La memoria de placa que pide un elemento que no acepta la de sistema (glshader:
/// GLMemory; d3d12…: D3D12Memory). "" si acepta la de sistema (o no se sabe).
std::string memoriaPropia(const std::string& fabrica) {
    GstElementFactory* f = gst_element_factory_find(fabrica.c_str());
    if (f == nullptr) return {};
    bool sistema = false;
    std::string placa;
    for (const GList* l = gst_element_factory_get_static_pad_templates(f); l != nullptr; l = l->next) {
        auto* t = static_cast<GstStaticPadTemplate*>(l->data);
        if (t->direction != GST_PAD_SINK) continue;
        GstCaps* c = gst_static_caps_get(&t->static_caps);
        if (gst_caps_is_any(c)) sistema = true;
        for (guint i = 0; i < gst_caps_get_size(c); ++i) {
            GstCapsFeatures* ft = gst_caps_get_features(c, i);
            bool esPlaca = false;
            for (const char* m : {"GLMemory", "D3D12Memory", "D3D11Memory", "CUDAMemory"})
                if (ft != nullptr && gst_caps_features_contains(ft, (std::string("memory:") + m).c_str())) {
                    esPlaca = true;
                    if (placa.empty()) placa = m;
                }
            if (!esPlaca) sistema = true;
        }
        gst_caps_unref(c);
    }
    gst_object_unref(f);
    return sistema ? std::string() : placa;
}
} // namespace

std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos, bool desdeGpu, Derivacion* derivar) {
    std::string r;
    if (derivar != nullptr) derivar->ramas.clear();
    // La memoria del motor (GLMemory o D3D12Memory) y dónde está el video en este punto.
    const std::string memMotor = !desdeGpu ? "" : gpu() == Gpu::D3D12 ? "D3D12Memory" : "GLMemory";
    bool enGpu = desdeGpu;
    for (const auto& f0 : v) {
        if (!hayFabrica(f0.elemento)) {
            avisos.push_back("no está el elemento «" + f0.elemento + "» (filtro " + f0.id + "): pasa sin él");
            continue;
        }
        // En la GPU, el par del elemento para esa memoria si lo tiene (baptz → baptzgl,
        // y los de GStreamer que tienen el suyo) y si entiende todas sus propiedades.
        Filtro f = f0;
        if (desdeGpu && f.memoria.empty()) {
            static const std::map<std::string, std::string> kParesGl{{"videobalance", "glcolorbalance"},
                                                                     {"videoflip", "glvideoflip"}};
            std::string par;
            if (memMotor == "GLMemory") {
                const auto i = kParesGl.find(f.elemento);
                par = i != kParesGl.end() ? i->second : f.elemento + "gl";
            } else {
                // (d3d12convert tiene brillo/contraste/saturación, pero en la PC con AMD dio
                // otro color: videobalance y bacolor siguen en la CPU.)
                par = f.elemento + "d3d12";
            }
            if (hayFabrica(par) && tieneLasPropiedades(par, f.props)) {
                f.elemento = par;
                f.memoria = memMotor;
            }
        }
        // Uno que solo trabaja en la placa (una tarjeta «gst» con glshader): con su memoria.
        if (f.memoria.empty()) f.memoria = memoriaPropia(f.elemento);
        // Un elemento que trabaja en la placa: subir antes y bajar después (si ya
        // viene en esa memoria, la subida pasa de largo: sin copias).
        static const std::map<std::string, std::pair<const char*, const char*>> kMemorias{
            {"CUDAMemory", {"cudaupload ! cudaconvertscale", "cudadownload"}},
            {"GLMemory", {"glupload ! glcolorconvert", "gldownload"}},
            {"D3D11Memory", {"d3d11upload ! d3d11convert", "d3d11download"}},
            {"D3D12Memory", {"d3d12upload ! d3d12convert", "d3d12download"}}};
        const auto m = kMemorias.find(f.memoria);
        bool hayPuente = m != kMemorias.end();
        if (hayPuente) {
            const std::string sube = m->second.first;
            const std::string primero = sube.substr(0, sube.find(' '));
            hayPuente = hayFabrica(primero) && hayFabrica(m->second.second);
            if (!hayPuente)
                avisos.push_back("no hay " + primero + " para darle " + f.memoria + " a «" + f.elemento +
                                 "»: va en memoria de sistema");
        }
        const std::string elem = f.elemento + " name=" + prefijo + f.nombre + " ! ";
        // La toma tal como llega a este filtro, para otro productor (ver Derivacion).
        if (derivar != nullptr && derivar->ramas.empty() && f.nombre == derivar->antesDe && !derivar->productor.empty()) {
            const std::string tee = std::string(prefijo) + f.nombre + "_antes";
            // Una cola (la del último cuadro, sin demora) también en la rama de siempre: sin ella el
            // tee le pasaba arriba el pool de esta rama y la otra se quedaba con su último cuadro;
            // con una fuente de pocos cuadros (el canal de un invitado vacío: la placa a 5 cps) la
            // rama principal esperaba un búfer libre para siempre (pasaba UN cuadro por rearme).
            r += "tee name=" + tee + " allow-not-linked=true ! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! ";
            derivar->ramas = " " + tee + ". ! queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! intersink name=" + tee + "_sale producer-name=" +
                             entreComillas(derivar->productor) + " sync=false event-types=\"<>\"";
        }
        if (desdeGpu) {
            if (hayPuente && f.memoria == memMotor) {
                // En la memoria del motor: si ya está ahí, sin puente.
                r += (enGpu ? std::string() : std::string(m->second.first) + " ! video/x-raw(memory:" + memMotor + "),format=RGBA ! ") + elem;
                enGpu = !f.saleEnSistema;
            } else {
                if (enGpu) r += std::string(memMotor == "D3D12Memory" ? "d3d12download" : "gldownload") + " ! ";
                r += hayPuente ? std::string(m->second.first) + " ! " + elem +
                                     (f.saleEnSistema ? std::string() : std::string(m->second.second) + " ! ")
                               : std::string(conv) + " ! " + elem;
                enGpu = false;
            }
        } else if (hayPuente) {
            r += std::string(m->second.first) + " ! " + elem +
                 (f.saleEnSistema ? std::string() : std::string(m->second.second) + " ! ");
        } else {
            r += std::string(conv) + " ! " + elem;
        }
    }
    return r;
}

std::string entreComillas(const std::string& v) {
    std::string r = "\"";
    for (char c : v) {
        if (c == '\\' || c == '"') r += '\\';
        r += c;
    }
    return r + "\"";
}

#if defined(__linux__)
namespace {
/// /dev/videoN cambia al reenchufar: el enlace estable de /dev/v4l/by-id.
std::string idEstableV4l2(const std::string& dev) {
    std::error_code ec;
    const std::filesystem::path dir("/dev/v4l/by-id");
    if (dev.rfind("/dev/video", 0) != 0 || !std::filesystem::is_directory(dir, ec)) return dev;
    const auto real = std::filesystem::canonical(dev, ec);
    if (ec) return dev;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (std::filesystem::canonical(e.path(), ec) == real && e.path().string().find("index0") != std::string::npos)
            return e.path().string();
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (std::filesystem::canonical(e.path(), ec) == real) return e.path().string();
    return dev;
}
} // namespace
#endif

std::string idDeCamara(GstDevice* dev, GstElement* e) {
    std::string id;
#if defined(__APPLE__)
    (void)e;
    if (GstStructure* p = gst_device_get_properties(dev)) {
        if (const gchar* u = gst_structure_get_string(p, "avf.unique_id")) id = u;
        gst_structure_free(p);
    }
#else
    (void)dev;
#if defined(_WIN32)
    const char* prop = "device-path";
#else
    const char* prop = "device";
#endif
    if (e != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) != nullptr) {
        gchar* v = nullptr;
        g_object_get(e, prop, &v, nullptr);
        if (v != nullptr) id = v;
        g_free(v);
    }
#if defined(__linux__)
    id = idEstableV4l2(id);
#endif
#endif
    return id;
}

bool camaraUsable(GstElement* e) {
    if (e == nullptr) return false;
#if defined(_WIN32)
    GstElementFactory* f = gst_element_get_factory(e);
    return f != nullptr && std::string(GST_OBJECT_NAME(f)) == "mfvideosrc";
#else
    return true;
#endif
}

bool mismaCamara(const std::string& a, const std::string& b) {
    if (a == b) return true;
#if defined(_WIN32)
    // Hasta el último «#{» (la categoría), sin distinguir mayúsculas.
    const auto instancia = [](const std::string& s) {
        const auto p = s.rfind("#{");
        std::string r = p == std::string::npos ? s : s.substr(0, p);
        for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    };
    return a.rfind("#{") != std::string::npos && instancia(a) == instancia(b);
#else
    return false;
#endif
}

std::string idDeMicrofono(GstElement* e) {
    if (e == nullptr) return {};
#if defined(__APPLE__)
    const char* prop = "unique-id";
#else
    const char* prop = "device";
#endif
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) == nullptr) return {};
    gchar* v = nullptr;
    g_object_get(e, prop, &v, nullptr);
    std::string id = v != nullptr ? v : "";
    g_free(v);
    return id;
}


std::string origenDeCamara(const std::string& id, const std::string& modo, const Lienzo& lienzo, nlohmann::json* elegido) {
    std::string o;
    ModoVideo m;
    std::string porque;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    if (gst_device_monitor_start(mon)) {
        resondearSinAviso(); // lo enchufado con la app abierta
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr && o.empty(); l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            GstElement* e = gst_device_create_element(dev, nullptr);
            const bool es = camaraUsable(e) && ((id.empty() || id == "default") || mismaCamara(idDeCamara(dev, e), id));
            if (es && e != nullptr) {
#if defined(__APPLE__)
                // El índice, contado como lo cuenta avfvideosrc (por su uniqueID): el del
                // dispositivo de GStreamer a veces viene en otro orden y abría OTRA cámara
                // (con la Kandao enchufada, «Kandao» mostraba la FaceTime y al revés).
                gint indice = -1;
                if (GstStructure* pr = gst_device_get_properties(dev)) {
                    if (const gchar* uid = gst_structure_get_string(pr, "avf.unique_id")) indice = indiceCamaraMac(uid);
                    gst_structure_free(pr);
                }
                if (indice < 0) g_object_get(e, "device-index", &indice, nullptr);
                if (indice >= 0) o = "avfvideosrc device-index=" + std::to_string(indice);
#elif defined(_WIN32)
                o = "mfvideosrc device-path=" + entreComillas(idDeCamara(dev, e));
#else
                o = "v4l2src device=" + entreComillas(idDeCamara(dev, e));
#endif
                if (!o.empty()) {
                    // El modo: el guardado (si la cámara lo sigue dando) o el que conviene para el lienzo.
                    GstCaps* caps = gst_device_get_caps(dev);
                    const auto modos = modosDe(caps);
                    if (caps != nullptr) gst_caps_unref(caps);
                    if (!modo.empty()) {
                        m = modoGuardado(modos, modo);
                        porque = m.ancho > 0 ? "el elegido a mano" : "el elegido a mano (" + modo + ") ya no está: el que conviene";
                    }
                    if (m.ancho <= 0) {
                        std::string p;
                        m = elegirModo(modos, {lienzo.ancho, lienzo.alto,
                                               lienzo.fpsD > 0 ? static_cast<double>(lienzo.fpsN) / lienzo.fpsD : 30.0},
                                       &p);
                        porque = porque.empty() ? p : porque + ": " + p;
                    }
                }
            }
            if (e != nullptr) gst_object_unref(e);
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
    if (o.empty()) return {};
    o += cadenaDeModo(m); // sin modo (la cámara no dijo nada): que negocie GStreamer
    if (elegido != nullptr) {
        *elegido = m.ancho > 0 ? m.json() : nlohmann::json::object();
        (*elegido)["porque"] = m.ancho > 0 ? porque : "la cámara no anuncia sus modos: negocia GStreamer";
        (*elegido)["automatico"] = modo.empty();
    }
    return o;
}

// --- Vigía de dispositivos ----------------------------------------------------------

namespace {
std::string claveDeDispositivo(GstDevice* d) {
    std::string k;
    if (gchar* c = gst_device_get_device_class(d)) {
        k += c;
        g_free(c);
    }
    k += '|';
    if (gchar* n = gst_device_get_display_name(d)) {
        k += n;
        g_free(n);
    }
    k += '|';
    if (GstStructure* p = gst_device_get_properties(d)) {
        gchar* t = gst_structure_to_string(p);
        k += t;
        g_free(t);
        gst_structure_free(p);
    }
    return k;
}
} // namespace

void resondearSinAviso() {
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    GList* fabricas = gst_device_provider_factory_list_get_device_providers(GST_RANK_NONE);
    for (GList* f = fabricas; f != nullptr; f = f->next) {
        GstDeviceProvider* p = gst_device_provider_factory_get(GST_DEVICE_PROVIDER_FACTORY(f->data));
        if (p == nullptr) continue;
        GstDeviceProviderClass* k = GST_DEVICE_PROVIDER_GET_CLASS(p);
        // Solo los que no vigilan solos y están prendidos (si no, ya sondean al listar).
        if (gst_device_provider_can_monitor(p) || k->probe == nullptr || !gst_device_provider_is_started(p)) {
            gst_object_unref(p);
            continue;
        }
        GList* viejos = gst_device_provider_get_devices(p); // la lista guardada
        GList* nuevos = k->probe(p);
        std::set<std::string> kv, kn;
        for (GList* d = viejos; d != nullptr; d = d->next) kv.insert(claveDeDispositivo(GST_DEVICE(d->data)));
        for (GList* d = nuevos; d != nullptr; d = d->next) kn.insert(claveDeDispositivo(GST_DEVICE(d->data)));
        if (kv != kn) {
            // Se reemplaza entera: el índice (device-index) de las que siguen puede haber corrido.
            for (GList* d = viejos; d != nullptr; d = d->next) gst_device_provider_device_remove(p, GST_DEVICE(d->data));
            for (GList* d = nuevos; d != nullptr; d = d->next) gst_device_provider_device_add(p, GST_DEVICE(d->data));
            g_list_free(nuevos); // device_add se quedó con la referencia (flotante)
        } else {
            for (GList* d = nuevos; d != nullptr; d = d->next) gst_object_ref_sink(d->data);
            g_list_free_full(nuevos, gst_object_unref);
        }
        g_list_free_full(viejos, gst_object_unref);
        gst_object_unref(p);
    }
    gst_plugin_feature_list_free(fabricas);
}

Vigia& Vigia::el() {
    static Vigia* v = new Vigia(); // vive lo que el proceso
    return *v;
}

void Vigia::anotar(GstDevice* dev, bool esta) {
    GstElement* e = gst_device_create_element(dev, nullptr);
    const bool esVideo = gst_device_has_classes(dev, "Video/Source");
    const std::string id = esVideo ? (camaraUsable(e) ? idDeCamara(dev, e) : std::string()) : idDeMicrofono(e);
    if (e != nullptr) gst_object_unref(e);
    if (id.empty()) return;
    std::lock_guard<std::mutex> l(mu_);
    auto& conjunto = esVideo ? video_ : audio_;
    if (esta)
        conjunto.insert(id);
    else
        conjunto.erase(id);
}

void Vigia::revisar() {
    std::call_once(hilo_, [this]() { std::thread([this]() { vigilar(); }).detach(); });
}

void Vigia::vigilar() {
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Audio/Source", nullptr);
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    const bool anda = gst_device_monitor_start(mon);
    if (anda) {
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr; l = l->next) anotar(GST_DEVICE(l->data), true);
        g_list_free_full(lista, gst_object_unref);
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        listo_ = true;
        sinMonitor_ = !anda;
    }
    arranco_.notify_all();
    if (!anda) {
        gst_object_unref(mon);
        return; // sin monitor: hay() dice que sí a todo
    }
    // Vive lo que el proceso. Los avisos, apenas llegan; lo que se enchufó en los
    // proveedores que no avisan, cada 3 s.
    GstBus* bus = gst_device_monitor_get_bus(mon);
    std::int64_t ultimoSondeo = g_get_monotonic_time();
    for (;;) {
        if (g_get_monotonic_time() - ultimoSondeo > 3'000'000) {
            ultimoSondeo = g_get_monotonic_time();
            resondearSinAviso();
        }
        bool cambio = false;
        for (GstMessage* m = gst_bus_timed_pop(bus, 500 * GST_MSECOND); m != nullptr; m = gst_bus_pop(bus)) {
            GstDevice* dev = nullptr;
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_DEVICE_ADDED) {
                gst_message_parse_device_added(m, &dev);
                anotar(dev, true);
                cambio = true;
            } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_DEVICE_REMOVED) {
                gst_message_parse_device_removed(m, &dev);
                anotar(dev, false);
                cambio = true;
            }
            if (dev != nullptr) gst_object_unref(dev);
            gst_message_unref(m);
        }
        if (cambio) ++generacion_;
    }
}

bool Vigia::hay(bool esVideo, const std::string& id) {
    revisar();
    std::unique_lock<std::mutex> l(mu_);
    if (!arranco_.wait_for(l, std::chrono::seconds(3), [this]() { return listo_; }) || sinMonitor_) return true;
    if (!esVideo) return audio_.count(id) > 0;
    for (const auto& v : video_)
        if (mismaCamara(v, id)) return true;
    return false;
}

// --- Archivos ------------------------------------------------------------------------

bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error) {
    video = audio = false;
    GError* e = nullptr;
    GstDiscoverer* d = gst_discoverer_new(5 * GST_SECOND, &e);
    if (d == nullptr) {
        error = e != nullptr ? ba::diag::utf8Valido(e->message) : "no se pudo mirar el archivo";
        if (e != nullptr) g_error_free(e);
        return false;
    }
    GstDiscovererInfo* info = gst_discoverer_discover_uri(d, uri.c_str(), &e);
    bool ok = info != nullptr && gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK;
    if (ok) {
        GList* v = gst_discoverer_info_get_video_streams(info);
        GList* a = gst_discoverer_info_get_audio_streams(info);
        video = v != nullptr; // una imagen sola cuenta como video (un cuadro)
        audio = a != nullptr;
        gst_discoverer_stream_info_list_free(v);
        gst_discoverer_stream_info_list_free(a);
        ok = video || audio;
        if (!ok) error = "el archivo no tiene video ni audio";
    } else {
        error = e != nullptr ? ba::diag::utf8Valido(e->message) : "no se pudo leer el archivo";
    }
    if (e != nullptr) g_error_free(e);
    if (info != nullptr) gst_discoverer_info_unref(info);
    g_object_unref(d);
    return ok;
}

json describirCaps(GstCaps* c) {
    GstVideoInfo info;
    if (c == nullptr || !gst_video_info_from_caps(&info, c)) return json::object();
    const int fn = GST_VIDEO_INFO_FPS_N(&info), fd = GST_VIDEO_INFO_FPS_D(&info);
    const int pn = GST_VIDEO_INFO_PAR_N(&info), pd = GST_VIDEO_INFO_PAR_D(&info);
    const GstVideoColorimetry& k = GST_VIDEO_INFO_COLORIMETRY(&info);
    const char* matriz = k.matrix == GST_VIDEO_COLOR_MATRIX_BT601    ? "BT.601"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT709  ? "BT.709"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_BT2020 ? "BT.2020"
                         : k.matrix == GST_VIDEO_COLOR_MATRIX_RGB    ? "RGB"
                                                                     : "sin declarar";
    return {{"formato", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&info))},
            {"ancho", GST_VIDEO_INFO_WIDTH(&info)},
            {"alto", GST_VIDEO_INFO_HEIGHT(&info)},
            {"fps", fn > 0 && fd > 0 ? static_cast<double>(fn) / fd : 0.0},
            {"fps_variable", fn == 0},
            {"entrelazado", static_cast<bool>(GST_VIDEO_INFO_IS_INTERLACED(&info))},
            {"pixel", pn == pd ? std::string("cuadrado") : std::to_string(pn) + "/" + std::to_string(pd) + " (no cuadrado)"},
            {"matriz", matriz},
            {"rango", k.range == GST_VIDEO_COLOR_RANGE_0_255    ? "completo"
                      : k.range == GST_VIDEO_COLOR_RANGE_16_235 ? "limitado"
                                                                : "sin declarar"},
            {"hdr", k.transfer == GST_VIDEO_TRANSFER_SMPTE2084 || k.transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67}};
}

void sinRelojPropio(GstElement* pipeline) {
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(pipeline));
    if (it == nullptr) return;
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "provide-clock") != nullptr)
            g_object_set(e, "provide-clock", FALSE, nullptr);
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
}

namespace {
// La hora de captura de una fuente de audio, contada por muestras desde el primer bloque
// (no tiembla) y anclada al reloj del pipeline (el del estudio); si se aleja más de
// kReanclarNs de lo que da el reloj, se reancla (la placa y el reloj derivan un poco).
struct HoraAudio {
    GstClockTime ancla = GST_CLOCK_TIME_NONE; ///< la hora del primer bloque desde el último anclaje
    guint64 muestras = 0;                     ///< las que pasaron desde el anclaje
    static constexpr GstClockTime kReanclarNs = 50 * GST_MSECOND;
};

GstPadProbeReturn ponerHoraAudio(GstPad* pad, GstPadProbeInfo* info, gpointer d) {
    auto* h = static_cast<HoraAudio*>(d);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    GstElement* e = gst_pad_get_parent_element(pad);
    if (b == nullptr || e == nullptr) {
        if (e != nullptr) gst_object_unref(e);
        return GST_PAD_PROBE_OK;
    }
    GstClock* reloj = gst_element_get_clock(e);
    const GstClockTime base = gst_element_get_base_time(e);
    gst_object_unref(e);
    if (reloj == nullptr) return GST_PAD_PROBE_OK;
    const GstClockTime ahora = gst_clock_get_time(reloj);
    gst_object_unref(reloj);
    gint rate = 0, canales = 0, bytesPorMuestra = 0;
    if (GstCaps* c = gst_pad_get_current_caps(pad)) {
        GstAudioInfo ai;
        if (gst_audio_info_from_caps(&ai, c)) {
            rate = GST_AUDIO_INFO_RATE(&ai);
            canales = GST_AUDIO_INFO_CHANNELS(&ai);
            bytesPorMuestra = GST_AUDIO_INFO_BPF(&ai);
        }
        gst_caps_unref(c);
    }
    if (rate <= 0 || bytesPorMuestra <= 0 || ahora < base) return GST_PAD_PROBE_OK;
    (void)canales;
    const guint64 n = gst_buffer_get_size(b) / static_cast<guint64>(bytesPorMuestra);
    const GstClockTime dur = gst_util_uint64_scale(n, GST_SECOND, static_cast<guint64>(rate));
    const GstClockTime real = ahora - base > dur ? ahora - base - dur : 0; // cuándo empezó a capturarse
    GstClockTime pts = GST_CLOCK_TIME_IS_VALID(h->ancla)
                           ? h->ancla + gst_util_uint64_scale(h->muestras, GST_SECOND, static_cast<guint64>(rate))
                           : GST_CLOCK_TIME_NONE;
    if (!GST_CLOCK_TIME_IS_VALID(pts) || (pts > real ? pts - real : real - pts) > HoraAudio::kReanclarNs) {
        h->ancla = real;
        h->muestras = 0;
        pts = real;
    }
    h->muestras += n;
    b = gst_buffer_make_writable(b);
    GST_PAD_PROBE_INFO_DATA(info) = b;
    GST_BUFFER_PTS(b) = pts;
    GST_BUFFER_DTS(b) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(b) = dur;
    return GST_PAD_PROBE_OK;
}
} // namespace

int horaDeCapturaEnAudio(GstElement* pipeline) {
    // osxaudiosrc (y otras) con el reloj del pipeline que no es el de su placa no le ponen
    // hora a lo que capturan (todo con PTS 0: la «edad» crecía un segundo por segundo y la
    // mezcla lo veía cada vez más atrasado). Ni slave-method ni do-timestamp lo arreglan.
    const GType fuenteDeAudio = g_type_from_name("GstAudioBaseSrc");
    if (fuenteDeAudio == 0) return 0;
    int n = 0;
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(pipeline));
    if (it == nullptr) return 0;
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        if (g_type_is_a(G_OBJECT_TYPE(e), fuenteDeAudio))
            if (GstPad* p = gst_element_get_static_pad(e, "src")) {
                gst_pad_add_probe(p, GST_PAD_PROBE_TYPE_BUFFER, &ponerHoraAudio, new HoraAudio(),
                                  [](gpointer d) { delete static_cast<HoraAudio*>(d); });
                gst_object_unref(p);
                ++n;
            }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    return n;
}

void recalcularLatenciaAparte(GstElement* pipeline, std::function<void()> antes) {
    if (pipeline == nullptr) return;
    struct Pedido {
        bool otraVez = false;
        std::function<void()> antes;
    };
    // Nunca se destruyen (un estático que se destruye al salir con un hilo usándolo mata el proceso).
    static auto* mu = new std::mutex;
    static auto* pedidos = new std::map<GstElement*, Pedido>; // los que tienen un hilo en curso
    {
        std::lock_guard<std::mutex> l(*mu);
        auto it = pedidos->find(pipeline);
        if (it != pedidos->end()) {
            it->second.otraVez = true;
            if (antes) it->second.antes = std::move(antes);
            return;
        }
        (*pedidos)[pipeline] = Pedido{false, {}};
    }
    // La referencia mientras está en el mapa: el puntero no se puede reusar.
    gst_object_ref(pipeline);
    std::thread([pipeline, antes = std::move(antes)]() mutable {
        for (;;) {
            if (antes) antes();
            gst_bin_recalculate_latency(GST_BIN(pipeline));
            std::lock_guard<std::mutex> l(*mu);
            auto it = pedidos->find(pipeline);
            if (it->second.otraVez) {
                it->second.otraVez = false;
                antes = std::move(it->second.antes);
                it->second.antes = {};
                continue;
            }
            pedidos->erase(it);
            break;
        }
        gst_object_unref(pipeline);
    }).detach();
}

int soltarTareasTerminadas(GstElement* bin) {
    if (bin == nullptr || !GST_IS_BIN(bin)) return 0;
    int n = 0;
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(bin));
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        GST_OBJECT_LOCK(e);
        std::vector<GstPad*> pads;
        for (GList* l = e->srcpads; l != nullptr; l = l->next) pads.push_back(GST_PAD(gst_object_ref(l->data)));
        GST_OBJECT_UNLOCK(e);
        for (GstPad* pad : pads) {
            GST_OBJECT_LOCK(pad);
            GstTask* t = GST_PAD_TASK(pad);
            const bool terminada = t != nullptr && GST_PAD_IS_EOS(pad) && gst_task_get_state(t) == GST_TASK_PAUSED;
            GST_OBJECT_UNLOCK(pad);
            if (terminada && gst_pad_stop_task(pad)) ++n;
            gst_object_unref(pad);
        }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    return n;
}

std::shared_ptr<GstObject> refCompartida(gpointer o) {
    if (o == nullptr) return {};
    return std::shared_ptr<GstObject>(GST_OBJECT(gst_object_ref(o)), [](GstObject* x) { gst_object_unref(x); });
}

std::int64_t LatenciaAparte::consultar(GstElement* pipeline) {
    if (pipeline != nullptr && !e_->enCurso.exchange(true)) {
        auto* p = GST_ELEMENT(gst_object_ref(pipeline));
        std::thread([p, e = e_]() {
            GstQuery* q = gst_query_new_latency();
            if (gst_element_query(p, q)) {
                gboolean vivo = FALSE;
                GstClockTime min = 0, max = 0;
                gst_query_parse_latency(q, &vivo, &min, &max);
                e->ns = GST_CLOCK_TIME_IS_VALID(min) ? static_cast<std::int64_t>(min) : 0;
            }
            gst_query_unref(q);
            gst_object_unref(p);
            e->enCurso = false;
        }).detach();
    }
    return e_->ns.load();
}

void fuentesDePruebaEnHora(GstElement* pipeline, GstClockTime ahora) {
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(pipeline));
    if (it == nullptr) return;
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        GstElementFactory* f = gst_element_get_factory(e);
        const std::string n = f != nullptr ? GST_OBJECT_NAME(f) : "";
        if ((n == "videotestsrc" || n == "audiotestsrc") &&
            g_object_class_find_property(G_OBJECT_GET_CLASS(e), "timestamp-offset") != nullptr) {
            gboolean vivo = FALSE;
            g_object_get(e, "is-live", &vivo, nullptr);
            if (vivo) g_object_set(e, "timestamp-offset", static_cast<gint64>(ahora), nullptr);
        }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
}

} // namespace ba::motor

// --- El video en la GPU ----------------------------------------------------------------

#include <gst/app/gstappsink.h>
#include <gst/gl/gl.h>

#include <atomic>
#include <cstdlib>
#include <mutex>

namespace ba::motor {

namespace {
std::atomic<int> gGpu{-1}; // -1 sin decidir; si no, un Gpu
std::mutex gGlMu;
GstGLDisplay* gDisplay = nullptr;
GstGLContext* gContexto = nullptr;
std::vector<GstContext*> gContextos; ///< D3D12: los que publicaron los elementos en la autoprueba

bool glListo() {
    std::lock_guard<std::mutex> l(gGlMu);
    if (gContexto != nullptr) return true;
    if (gDisplay == nullptr) gDisplay = gst_gl_display_new();
    if (gDisplay == nullptr) return false;
    GError* e = nullptr;
    if (!gst_gl_display_create_context(gDisplay, nullptr, &gContexto, &e)) {
        registrar(Nivel::Aviso, std::string("sin contexto GL para el video: ") + (e != nullptr ? ba::diag::utf8Valido(e->message) : "?") +
                                    " (se compone en la CPU)");
        if (e != nullptr) g_error_free(e);
        return false;
    }
    gst_gl_display_add_context(gDisplay, gContexto);
    return true;
}

/// La autoprueba de D3D12: subir, convertir, componer y bajar unos cuadros. Si
/// entrega, guarda el contexto del dispositivo que publicaron los elementos
/// (el mismo para todos los pipelines del motor).
bool d3d12Listo() {
    for (const char* f : {"d3d12upload", "d3d12download", "d3d12convert", "d3d12compositor"})
        if (!hayFabrica(f)) return false;
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(
        "videotestsrc num-buffers=4 ! video/x-raw,format=RGBA,width=64,height=36 ! d3d12upload ! d3d12convert ! "
        "d3d12compositor name=mx ! video/x-raw(memory:D3D12Memory),format=RGBA ! d3d12download ! appsink name=s sync=false",
        &e);
    if (e != nullptr) {
        registrar(Nivel::Aviso, std::string("D3D12: la autoprueba no se arma: ") + ba::diag::utf8Valido(e->message));
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return false;
    }
    GstBus* bus = gst_element_get_bus(p);
    gst_element_set_state(p, GST_STATE_PLAYING);
    int cuadros = 0;
    std::string error;
    std::vector<GstContext*> contextos;
    GstElement* s = gst_bin_get_by_name(GST_BIN(p), "s");
    const gint64 hasta = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < hasta) {
        if (GstSample* m = gst_app_sink_try_pull_sample(GST_APP_SINK(s), 20 * GST_MSECOND)) {
            ++cuadros;
            gst_sample_unref(m);
        }
        while (GstMessage* m = gst_bus_pop(bus)) {
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_HAVE_CONTEXT) {
                GstContext* c = nullptr;
                gst_message_parse_have_context(m, &c);
                if (c != nullptr) contextos.push_back(c);
            } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR && error.empty()) {
                GError* ge = nullptr;
                gst_message_parse_error(m, &ge, nullptr);
                error = ge != nullptr ? ba::diag::utf8Valido(ge->message) : "error";
                if (ge != nullptr) g_error_free(ge);
            }
            gst_message_unref(m);
        }
        if (cuadros >= 4 || !error.empty() || gst_app_sink_is_eos(GST_APP_SINK(s))) break;
    }
    gst_object_unref(s);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(p);
    if (cuadros < 4) {
        registrar(Nivel::Aviso, "D3D12: la autoprueba no entregó cuadros (" + std::to_string(cuadros) + ")" +
                                    (error.empty() ? std::string() : ": " + error) + " (se compone en la CPU)");
        for (GstContext* c : contextos) gst_context_unref(c);
        return false;
    }
    std::string tipos;
    for (GstContext* c : contextos) tipos += std::string(tipos.empty() ? "" : ", ") + gst_context_get_context_type(c);
    registrar(Nivel::Info, "D3D12: la autoprueba entregó " + std::to_string(cuadros) + " cuadros; contextos: " +
                               (tipos.empty() ? std::string("ninguno") : tipos));
    std::lock_guard<std::mutex> l(gGlMu);
    for (GstContext* c : gContextos) gst_context_unref(c);
    gContextos = std::move(contextos);
    return true;
}

Gpu pedida() {
    const char* cpu = std::getenv("BA_MOTOR_CPU");
    if (cpu != nullptr && *cpu != '\0' && *cpu != '0') return Gpu::Ninguna;
    const char* g = std::getenv("BA_MOTOR_GPU");
    const std::string v = g != nullptr ? g : "";
    if (v == "cpu") return Gpu::Ninguna;
    if (v == "gl") return Gpu::Gl;
    if (v == "d3d12") return Gpu::D3D12;
#if defined(__APPLE__)
    return Gpu::Gl;
#elif defined(_WIN32)
    return Gpu::D3D12;
#else
    return Gpu::Ninguna;
#endif
}
} // namespace

void ponerGpuVideo(bool quiere) {
    Gpu g = quiere ? pedida() : Gpu::Ninguna;
    if (g == Gpu::Gl && !(hayFabrica("glvideomixer") && hayFabrica("gltransformation") && hayFabrica("glcolorconvert") && glListo()))
        g = Gpu::Ninguna;
    if (g == Gpu::D3D12 && !d3d12Listo()) g = Gpu::Ninguna;
    gGpu = static_cast<int>(g);
    registrar(Nivel::Info, std::string("video del motor: ") + nombreGpu());
}

Gpu gpu() {
    if (gGpu.load() < 0) ponerGpuVideo(true);
    return static_cast<Gpu>(gGpu.load());
}

bool gpuVideo() { return gpu() != Gpu::Ninguna; }

const char* nombreGpu() {
    switch (static_cast<Gpu>(gGpu.load())) {
        case Gpu::Gl: return "gl";
        case Gpu::D3D12: return "d3d12";
        default: return "cpu";
    }
}

std::string texturaGpu(const std::string& extra) {
    return std::string("video/x-raw(memory:") + (gpu() == Gpu::D3D12 ? "D3D12Memory" : "GLMemory") + "),format=RGBA" + extra;
}

std::string subirGpu() { return gpu() == Gpu::D3D12 ? "d3d12upload ! d3d12convert" : "glupload ! glcolorconvert"; }

std::string bajarGpu() { return gpu() == Gpu::D3D12 ? "d3d12download" : "gldownload"; }

void darContextoGpu(GstElement* pipeline) {
    const Gpu g = gpu();
    if (g == Gpu::D3D12) {
        std::lock_guard<std::mutex> l(gGlMu);
        for (GstContext* c : gContextos) gst_element_set_context(pipeline, c);
        return;
    }
    if (g != Gpu::Gl) return;
    GstGLDisplay* d = nullptr;
    GstGLContext* c = nullptr;
    {
        std::lock_guard<std::mutex> l(gGlMu);
        d = gDisplay;
        c = gContexto;
    }
    if (d == nullptr || c == nullptr) return;
    // En el bin: se los pasa a todos los elementos, también a los que se agreguen después.
    GstContext* cd = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
    gst_context_set_gl_display(cd, d);
    gst_element_set_context(pipeline, cd);
    gst_context_unref(cd);
    GstContext* ca = gst_context_new("gst.gl.app_context", TRUE);
    gst_structure_set(gst_context_writable_structure(ca), "context", GST_TYPE_GL_CONTEXT, c, nullptr);
    gst_element_set_context(pipeline, ca);
    gst_context_unref(ca);
    // EL MISMO CONTEXTO PARA TODOS: con el de la app solo como «compartido»,
    // cada pipeline armaba su contexto, y cada textura que pasaba de un pipeline
    // a otro pagaba una barrera de sincronización (glFenceSync: ~25 % del hilo
    // de GL). Con uno solo no hacen falta. Medido (8 cámaras, programa +
    // preview): 181 % → 163 % de CPU, las vistas parejas a 30 cuadros/s.
    // BA_GL_UN_CONTEXTO=0 vuelve a uno por pipeline.
    static const bool unContexto = [] {
        const char* v = std::getenv("BA_GL_UN_CONTEXTO");
        return v == nullptr || std::string(v) != "0";
    }();
    if (unContexto) {
        GstContext* cl = gst_context_new("gst.gl.local_context", TRUE);
        gst_structure_set(gst_context_writable_structure(cl), "context", GST_TYPE_GL_CONTEXT, c, nullptr);
        gst_element_set_context(pipeline, cl);
        gst_context_unref(cl);
    }
}

std::string aSistema(const std::string& formato) {
    if (!gpuVideo()) return "videoconvert ! video/x-raw,format=" + formato;
    const std::string mem = gpu() == Gpu::D3D12 ? "D3D12Memory" : "GLMemory";
    return subirGpu() + " ! video/x-raw(memory:" + mem + "),format=" + formato + " ! " + bajarGpu() +
           " ! video/x-raw,format=" + formato;
}

bool aceptaMemoria(const std::string& fabrica, const std::string& memoria) {
    GstElementFactory* f = gst_element_factory_find(fabrica.c_str());
    if (f == nullptr) return false;
    bool acepta = false;
    for (const GList* l = gst_element_factory_get_static_pad_templates(f); l != nullptr && !acepta; l = l->next) {
        auto* t = static_cast<GstStaticPadTemplate*>(l->data);
        if (t->direction != GST_PAD_SINK) continue;
        GstCaps* c = gst_static_caps_get(&t->static_caps);
        for (guint i = 0; i < gst_caps_get_size(c) && !acepta; ++i) {
            GstCapsFeatures* ft = gst_caps_get_features(c, i);
            acepta = ft != nullptr && gst_caps_features_contains(ft, ("memory:" + memoria).c_str());
        }
        gst_caps_unref(c);
    }
    gst_object_unref(f);
    return acepta;
}

std::string aCodificador(const std::string& codificador) {
    // Con la colorimetría del lienzo dicha: el que convierte de RGBA a NV12 usa
    // esa matriz y el codificador la escribe en el archivo (ver fijarColorSalida).
    const std::string col = ",colorimetry=" + colorSalida();
    if (gpu() == Gpu::D3D12 && aceptaMemoria(codificador, "D3D12Memory"))
        return "d3d12upload ! d3d12convert ! video/x-raw(memory:D3D12Memory),format=NV12" + col;
    if (!gpuVideo()) return "videoconvert ! video/x-raw,format=NV12" + col;
    const std::string mem = gpu() == Gpu::D3D12 ? "D3D12Memory" : "GLMemory";
    return subirGpu() + " ! video/x-raw(memory:" + mem + "),format=NV12" + col + " ! " + bajarGpu() +
           " ! video/x-raw,format=NV12" + col;
}

std::string aSistemaEscalado(const std::string& formato, int ancho, int alto) {
    const std::string tam = ",width=" + std::to_string(ancho) + ",height=" + std::to_string(alto);
    if (!gpuVideo()) return "videoconvert ! videoscale ! video/x-raw,format=" + formato + tam + ",pixel-aspect-ratio=1/1";
    if (gpu() == Gpu::D3D12)
        // d3d12convert convierte y escala en una pasada; baja chico.
        return "d3d12upload ! d3d12convert ! video/x-raw(memory:D3D12Memory),format=" + formato + tam +
               ",pixel-aspect-ratio=1/1 ! d3d12download ! video/x-raw,format=" + formato + tam + ",pixel-aspect-ratio=1/1";
    // En GL: achicar en la placa y bajar CHICO en RGBA; el formato final, en la
    // CPU (a este tamaño es casi gratis): un paso de GL menos por cuadro. Sin colas
    // entre etapas: cada cola es un hilo y no cambiaban nada medible.
    return "glupload ! glcolorconvert ! video/x-raw(memory:GLMemory),format=RGBA ! glcolorscale ! video/x-raw(memory:GLMemory),format=RGBA" +
           tam + " ! gldownload ! video/x-raw,format=RGBA" + tam + " ! videoconvert ! video/x-raw,format=" + formato + tam +
           ",pixel-aspect-ratio=1/1";
}

} // namespace ba::motor
