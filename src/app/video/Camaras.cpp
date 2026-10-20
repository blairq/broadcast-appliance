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
    recorrer([&](GstDevice* dev, const std::string& cual) {
        if (!out.empty() || !ba::motor::mismaCamara(cual, id)) return;
        GstCaps* caps = gst_device_get_caps(dev);
        out = ba::motor::modosDe(caps);
        if (caps != nullptr) gst_caps_unref(caps);
    });
#else
    (void)id;
#endif
    return out;
}

namespace {
ba::motor::PedidoVideo pedidoDelLienzo() {
    int w = 0, h = 0;
    if (!lienzoActual(w, h)) w = 1920, h = 1080;
    return {w, h, fpsActual()};
}
} // namespace

ModoCamara modoAutomatico(const std::string& id, std::string* porque) {
    return ba::motor::elegirModo(modosDeCamara(id), pedidoDelLienzo(), porque);
}

nlohmann::json fichasDeVideo(const std::string& id) {
    nlohmann::json out = nlohmann::json::array();
#if defined(BA_HAVE_GST)
    if (!gst_is_initialized()) return out;
    GstDeviceMonitor* mon = gst_device_monitor_new();
    gst_device_monitor_add_filter(mon, "Video/Source", nullptr);
    if (gst_device_monitor_start(mon)) {
        ba::motor::resondearSinAviso();
        GList* lista = gst_device_monitor_get_devices(mon);
        for (GList* l = lista; l != nullptr; l = l->next) {
            auto* dev = GST_DEVICE(l->data);
            const std::string cual = idDe(dev);
            if (!id.empty() && !ba::motor::mismaCamara(cual, id)) continue;
            GstElement* e = gst_device_create_element(dev, nullptr);
            const bool usable = ba::motor::camaraUsable(e);
            GstElementFactory* fab = e != nullptr ? gst_element_get_factory(e) : nullptr;
            nlohmann::json f{{"id", cual}, {"usable", usable}, {"elemento", fab != nullptr ? GST_OBJECT_NAME(fab) : ""}};
            if (e != nullptr) gst_object_unref(e);
            if (gchar* n = gst_device_get_display_name(dev)) f["nombre"] = n, g_free(n);
            if (gchar* c = gst_device_get_device_class(dev)) f["clase"] = c, g_free(c);
            nlohmann::json props = nlohmann::json::object();
            if (GstStructure* p = gst_device_get_properties(dev)) {
                for (int i = 0; i < gst_structure_n_fields(p); ++i) {
                    const char* k = gst_structure_nth_field_name(p, i);
                    gchar* v = gst_value_serialize(gst_structure_get_value(p, k));
                    props[k] = v != nullptr ? v : "";
                    g_free(v);
                }
                if (const gchar* api = gst_structure_get_string(p, "device.api")) f["api"] = api;
                gst_structure_free(p);
            }
            f["propiedades"] = props;
            if (!usable)
                f["por_que_no"] = "en Windows se abre por Media Foundation (mfvideosrc): esta es la misma cámara vista por otra API";
            GstCaps* caps = gst_device_get_caps(dev);
            nlohmann::json crudas = nlohmann::json::array(), modos = nlohmann::json::array();
            if (caps != nullptr) {
                for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
                    GstCaps* una = gst_caps_copy_nth(caps, i);
                    gchar* t = gst_caps_to_string(una);
                    crudas.push_back(t != nullptr ? t : "");
                    g_free(t);
                    gst_caps_unref(una);
                }
                const auto ms = ba::motor::modosDe(caps);
                for (const auto& m : ms) modos.push_back(m.json());
                if (usable) {
                    std::string porque;
                    const auto a = ba::motor::elegirModo(ms, pedidoDelLienzo(), &porque);
                    nlohmann::json j = a.ancho > 0 ? a.json() : nlohmann::json::object();
                    j["porque"] = a.ancho > 0 ? porque : "no anuncia sus modos: negocia GStreamer";
                    f["automatico"] = j;
                }
                gst_caps_unref(caps);
            }
            f["modos"] = modos;
            f["caps"] = crudas;
            out.push_back(std::move(f));
        }
        g_list_free_full(lista, gst_object_unref);
        gst_device_monitor_stop(mon);
    }
    gst_object_unref(mon);
#else
    (void)id;
#endif
    return out;
}

} // namespace ba::app::video
