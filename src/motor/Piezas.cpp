// ============================================================================
// src/motor/Piezas.cpp — ver Piezas.hpp
// ============================================================================
#include "Piezas.hpp"

#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

namespace ba::motor {

using json = nlohmann::json;

std::string capsColorDelLienzo(const Lienzo& l) {
    const bool diez = l.diezBits || l.color == "bt2100-pq" || l.color == "bt2100-hlg";
    GstVideoColorimetry c = {};
    gst_video_colorimetry_from_string(&c, l.color.c_str());
    c.range = l.rangoCompleto ? GST_VIDEO_COLOR_RANGE_0_255 : GST_VIDEO_COLOR_RANGE_16_235;
    gchar* col = gst_video_colorimetry_to_string(&c);
    std::string out = std::string("video/x-raw,format=") + (diez ? "P010_10LE" : "NV12") +
                      ",colorimetry=" + (col != nullptr ? col : l.color);
    g_free(col);
    return out;
}

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
    return out;
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

std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos) {
    std::string r;
    for (const auto& f : v) {
        if (!hayFabrica(f.elemento)) {
            avisos.push_back("no está el elemento «" + f.elemento + "» (filtro " + f.id + "): pasa sin él");
            continue;
        }
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
        if (hayPuente)
            r += std::string(m->second.first) + " ! " + f.elemento + " name=" + prefijo + f.nombre + " ! " +
                 (f.saleEnSistema ? std::string() : std::string(m->second.second) + " ! ");
        else
            r += std::string(conv) + " ! " + f.elemento + " name=" + prefijo + f.nombre + " ! ";
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

namespace {
/// Sin modo pedido: el de la cámara que mejor va con el lienzo (su tamaño si lo
/// tiene; si no, el más grande con su proporción; si no, que negocie GStreamer).
std::string modoParaElLienzo(GstCaps* caps, const Lienzo& l) {
    if (caps == nullptr || l.ancho <= 0 || l.alto <= 0) return {};
    const double prop = static_cast<double>(l.ancho) / l.alto;
    int mejorW = 0, mejorH = 0;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        GstCapsFeatures* ft = gst_caps_get_features(caps, i);
        if (ft != nullptr && !gst_caps_features_is_equal(ft, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) continue;
        int w = 0, h = 0;
        const GstStructure* st = gst_caps_get_structure(caps, i);
        if (!gst_structure_get_int(st, "width", &w) || !gst_structure_get_int(st, "height", &h) || h == 0) continue;
        if (w == l.ancho && h == l.alto) return std::to_string(w) + "x" + std::to_string(h);
        if (std::abs(static_cast<double>(w) / h - prop) < 0.02 && w * h > mejorW * mejorH) {
            mejorW = w;
            mejorH = h;
        }
    }
    return mejorW > 0 ? std::to_string(mejorW) + "x" + std::to_string(mejorH) : std::string();
}
} // namespace

std::string origenDeCamara(const std::string& id, std::string modo, const Lienzo& lienzo) {
    std::string o;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    if (gst_device_monitor_start(mon)) {
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr && o.empty(); l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            GstElement* e = gst_device_create_element(dev, nullptr);
            const bool es = (id.empty() || id == "default") || idDeCamara(dev, e) == id;
            if (es && e != nullptr) {
#if defined(__APPLE__)
                gint indice = -1;
                g_object_get(e, "device-index", &indice, nullptr);
                if (indice >= 0) o = "avfvideosrc device-index=" + std::to_string(indice);
#elif defined(_WIN32)
                o = "mfvideosrc device-path=" + entreComillas(idDeCamara(dev, e));
#else
                o = "v4l2src device=" + entreComillas(idDeCamara(dev, e));
#endif
                if (!o.empty() && modo.empty()) {
                    GstCaps* caps = gst_device_get_caps(dev);
                    modo = modoParaElLienzo(caps, lienzo);
                    if (caps != nullptr) gst_caps_unref(caps);
                }
            }
            if (e != nullptr) gst_object_unref(e);
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
    if (o.empty()) return {};
    int w = 0, h = 0, fps = 0;
    if (!modo.empty() && std::sscanf(modo.c_str(), "%dx%d@%d", &w, &h, &fps) >= 2 && w > 0 && h > 0)
        o += " ! video/x-raw,width=" + std::to_string(w) + ",height=" + std::to_string(h) +
             (fps > 0 ? ",framerate=" + std::to_string(fps) + "/1" : std::string());
    return o;
}

// --- Vigía de dispositivos ----------------------------------------------------------

Vigia& Vigia::el() {
    static Vigia* v = new Vigia(); // vive lo que el proceso
    return *v;
}

void Vigia::anotar(GstDevice* dev, bool esta) {
    GstElement* e = gst_device_create_element(dev, nullptr);
    const bool esVideo = gst_device_has_classes(dev, "Video/Source");
    const std::string id = esVideo ? idDeCamara(dev, e) : idDeMicrofono(e);
    if (e != nullptr) gst_object_unref(e);
    if (id.empty()) return;
    auto& conjunto = esVideo ? video_ : audio_;
    if (esta)
        conjunto.insert(id);
    else
        conjunto.erase(id);
}

void Vigia::arrancar() {
    if (mon_ != nullptr) return;
    mon_ = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon_, "Audio/Source", nullptr);
    gst_device_monitor_add_filter(mon_, "Video/Source", nullptr);
    if (!gst_device_monitor_start(mon_)) return;
    GList* lista = gst_device_monitor_get_devices(mon_);
    for (GList* l = lista; l != nullptr; l = l->next) anotar(GST_DEVICE(l->data), true);
    g_list_free_full(lista, gst_object_unref);
}

void Vigia::revisar() {
    std::unique_lock<std::mutex> l(mu_, std::try_to_lock);
    if (!l.owns_lock()) return;
    arrancar();
    if (mon_ == nullptr) return;
    GstBus* bus = gst_device_monitor_get_bus(mon_);
    bool cambio = false;
    while (GstMessage* m = gst_bus_pop(bus)) {
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
    gst_object_unref(bus);
    if (cambio) ++generacion_;
}

bool Vigia::hay(bool esVideo, const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    arrancar();
    if (mon_ == nullptr) return true;
    return (esVideo ? video_ : audio_).count(id) > 0;
}

// --- Archivos ------------------------------------------------------------------------

bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error) {
    video = audio = false;
    GError* e = nullptr;
    GstDiscoverer* d = gst_discoverer_new(5 * GST_SECOND, &e);
    if (d == nullptr) {
        error = e != nullptr ? e->message : "no se pudo mirar el archivo";
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
        error = e != nullptr ? e->message : "no se pudo leer el archivo";
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

} // namespace ba::motor
