// ============================================================================
// src/app/video/Camaras.cpp — ver Camaras.hpp
// ============================================================================

#include "video/Camaras.hpp"
#include "Piezas.hpp" // motor::resondearSinAviso

#include "emision/InputSpecs.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <tuple>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

namespace ba::app::video {

#if defined(__linux__)
/// /dev/videoN cambia de número al reenchufar o reiniciar: el enlace estable de
/// /dev/v4l/by-id que apunta a él (si no hay, el mismo).
static std::string idEstableV4l2(const std::string& dev) {
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
#endif

std::string ModoCamara::valor() const {
    return std::to_string(ancho) + "x" + std::to_string(alto) + (fps > 0 ? "@" + std::to_string(fps) : std::string());
}

std::string ModoCamara::etiqueta() const {
    return std::to_string(ancho) + "×" + std::to_string(alto) + (fps > 0 ? "  ·  " + std::to_string(fps) + " cps" : std::string());
}

std::string idCamaraDesdeLibobs(const std::string& id) {
#if defined(_WIN32)
    // libobs (DirectShow): «Nombre:\\?\usb#…»; mfvideosrc: la ruta sola.
    const auto p = id.find(":\\\\?\\");
    if (p != std::string::npos) return id.substr(p + 1);
#elif defined(__linux__)
    return idEstableV4l2(id); // /dev/videoN → /dev/v4l/by-id/…
#endif
    return id;
}

#if defined(BA_HAVE_GST)
namespace {

/// El id estable de un dispositivo de video.
std::string idDe(GstDevice* dev) {
    std::string id;
#if defined(__APPLE__)
    if (GstStructure* p = gst_device_get_properties(dev)) {
        if (const gchar* u = gst_structure_get_string(p, "avf.unique_id")) id = u;
        gst_structure_free(p);
    }
#else
    if (GstElement* e = gst_device_create_element(dev, nullptr)) {
#if defined(_WIN32)
        const char* prop = "device-path";
#else
        const char* prop = "device";
#endif
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) != nullptr) {
            gchar* v = nullptr;
            g_object_get(e, prop, &v, nullptr);
            if (v != nullptr) id = v;
            g_free(v);
        }
        gst_object_unref(e);
    }
#if defined(__linux__)
    id = idEstableV4l2(id);
#endif
#endif
    return id;
}

/// Recorre las cámaras del monitor: fn(dispositivo, id).
template <typename F>
void recorrer(F&& fn) {
    if (!gst_is_initialized()) return;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    if (gst_device_monitor_start(mon)) {
        ba::motor::resondearSinAviso(); // lo enchufado con la app abierta (macOS no avisa)
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr; l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            // En Windows, solo las de Media Foundation (ver motor::camaraUsable).
            GstElement* e = gst_device_create_element(dev, nullptr);
            const bool usable = ba::motor::camaraUsable(e);
            if (e != nullptr) gst_object_unref(e);
            if (!usable) continue;
            const std::string id = idDe(dev);
            if (!id.empty()) fn(dev, id);
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
}

/// El mayor numerador/denominador de un framerate (fijo, lista o rango).
int maxFps(const GValue* v) {
    if (v == nullptr) return 0;
    if (GST_VALUE_HOLDS_FRACTION(v)) {
        const int d = gst_value_get_fraction_denominator(v);
        return d > 0 ? gst_value_get_fraction_numerator(v) / d : 0;
    }
    if (GST_VALUE_HOLDS_FRACTION_RANGE(v)) return maxFps(gst_value_get_fraction_range_max(v));
    if (GST_VALUE_HOLDS_LIST(v)) {
        int m = 0;
        for (guint i = 0; i < gst_value_list_get_size(v); ++i) m = std::max(m, maxFps(gst_value_list_get_value(v, i)));
        return m;
    }
    return 0;
}

} // namespace
#endif

std::vector<BroadcastEngine::ListItem> camarasDelEquipo(BroadcastEngine& engine) {
    std::vector<BroadcastEngine::ListItem> out;
#if defined(BA_HAVE_GST)
    recorrer([&](GstDevice* dev, const std::string& id) {
        gchar* n = gst_device_get_display_name(dev);
        out.push_back({n != nullptr ? n : id, id, false});
        g_free(n);
    });
    if (!out.empty()) return out;
#endif
    for (const auto& d : engine.sourceTypeListItems(inputs::kVideoType, inputs::kVideoProp))
        out.push_back({d.name, idCamaraDesdeLibobs(d.value), d.disabled});
    return out;
}

std::vector<ModoCamara> modosDeCamara(const std::string& id) {
    std::vector<ModoCamara> out;
#if defined(BA_HAVE_GST)
    std::set<std::tuple<int, int, int>> vistos;
    recorrer([&](GstDevice* dev, const std::string& cual) {
        if (!ba::motor::mismaCamara(cual, id)) return;
        GstCaps* caps = gst_device_get_caps(dev);
        if (caps == nullptr) return;
        for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
            // Solo memoria de sistema (la de la placa repite los mismos modos).
            GstCapsFeatures* ft = gst_caps_get_features(caps, i);
            if (ft != nullptr && !gst_caps_features_is_equal(ft, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) continue;
            const GstStructure* st = gst_caps_get_structure(caps, i);
            // Solo video crudo: es lo que el motor le pide a la cámara (motor::origenDeCamara).
            // Un modo que la cámara da solo en MJPEG (1080p a 30 en una USB 2) no negociaba.
            if (!gst_structure_has_name(st, "video/x-raw")) continue;
            int w = 0, h = 0;
            if (!gst_structure_get_int(st, "width", &w) || !gst_structure_get_int(st, "height", &h)) continue;
            const int fps = maxFps(gst_structure_get_value(st, "framerate"));
            if (vistos.insert({w, h, fps}).second) out.push_back({w, h, fps});
        }
        gst_caps_unref(caps);
    });
    std::sort(out.begin(), out.end(), [](const ModoCamara& a, const ModoCamara& b) {
        return std::tie(a.ancho, a.alto, a.fps) > std::tie(b.ancho, b.alto, b.fps);
    });
#else
    (void)id;
#endif
    return out;
}

} // namespace ba::app::video
