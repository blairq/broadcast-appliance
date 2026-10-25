// ============================================================================
// src/app/audio/EntradasAudio.cpp — ver EntradasAudio.hpp
// ============================================================================

#include "audio/EntradasAudio.hpp"

#include "audio/UnificarEntradas.hpp"
#include "emision/InputSpecs.hpp"

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

namespace ba::app::audio {

namespace {

#if defined(_WIN32)
constexpr const char* kElemento = "wasapi2src";
constexpr const char* kPropiedad = "device";
constexpr const char* kExtra = " low-latency=true";
#elif defined(__APPLE__)
constexpr const char* kElemento = "osxaudiosrc";
constexpr const char* kPropiedad = "unique-id";
constexpr const char* kExtra = "";
#else
constexpr const char* kElemento = "pulsesrc";
constexpr const char* kPropiedad = "device";
constexpr const char* kExtra = "";
#endif

/// Entre comillas para gst_parse_launch (los id de Windows traen \ y #).
std::string entreComillas(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

std::string idDesdeLibobs(const std::string& id) {
#if defined(_WIN32)
    // libobs: el id del endpoint ({0.0.1.00000000}.{guid}); wasapi2src: la
    // ruta de la interfaz de captura de ese endpoint.
    if (id.rfind("{0.0.", 0) == 0) return "\\\\?\\SWD#MMDEVAPI#" + id + "#{2eef81be-33fa-4800-9670-1cd474972c3f}";
#endif
    return id;
}

std::string pipelineDeMicrofono(const std::string& dispositivo) {
    std::string p = kElemento;
    // «loopback:<id>»: lo que suena en esa salida (ver UnificarEntradas.hpp).
    const bool loopback = dispositivo.rfind(kPrefijoLoopback, 0) == 0;
    const std::string id = loopback ? dispositivo.substr(std::char_traits<char>::length(kPrefijoLoopback)) : dispositivo;
    if (!id.empty() && id != "default") p += std::string(" ") + kPropiedad + "=" + entreComillas(id);
#if defined(_WIN32)
    if (loopback) p += " loopback=true";
#endif
    return p + kExtra;
}

std::vector<BroadcastEngine::ListItem> entradasDeAudio(BroadcastEngine& engine) {
    std::vector<BroadcastEngine::ListItem> out;
    out.push_back({"Entrada de audio predeterminada", "default", false});
#if defined(BA_HAVE_GST)
    if (gst_is_initialized()) {
        GstDeviceMonitor* mon = gst_device_monitor_new();
        gst_device_monitor_add_filter(mon, "Audio/Source", nullptr);
        if (gst_device_monitor_start(mon)) {
            std::vector<EntradaCruda> crudas;
            GList* lista = gst_device_monitor_get_devices(mon);
            for (GList* l = lista; l != nullptr; l = l->next) {
                auto* dev = GST_DEVICE(l->data);
                bool monitor = false;
                gboolean loopback = FALSE;
                if (GstStructure* props = gst_device_get_properties(dev)) {
                    // Linux: «Monitor of …» (lo que suena) no es una entrada.
                    const gchar* clase = gst_structure_get_string(props, "device.class");
                    monitor = clase != nullptr && std::string(clase) == "monitor";
                    // Windows: las salidas también se anuncian como fuente (loopback).
                    if (!gst_structure_get_boolean(props, "wasapi2.device.loopback", &loopback)) loopback = FALSE;
                    gst_structure_free(props);
                }
                GstElement* e = monitor ? nullptr : gst_device_create_element(dev, nullptr);
                if (e != nullptr) {
                    gchar* id = nullptr;
                    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), kPropiedad) != nullptr)
                        g_object_get(e, kPropiedad, &id, nullptr);
                    gchar* nombre = gst_device_get_display_name(dev);
                    GstElementFactory* fab = gst_element_get_factory(e);
                    if (id != nullptr && *id != '\0')
                        crudas.push_back({nombre != nullptr ? nombre : id, id,
                                          fab != nullptr ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(fab)) : "",
                                          loopback != FALSE});
                    g_free(nombre);
                    g_free(id);
                    gst_object_unref(e);
                }
            }
            g_list_free_full(lista, gst_object_unref);
            gst_device_monitor_stop(mon);
            // Una por dispositivo físico, sin las predeterminadas repetidas y con el
            // loopback como lo que es (UnificarEntradas.hpp).
            for (auto& [nombre, valor] : unificarEntradas(crudas, kElemento)) out.push_back({nombre, valor, false});
        }
        gst_object_unref(mon);
        if (out.size() > 1) return out;
    }
#endif
    std::vector<EntradaCruda> crudas;
    for (const auto& d : engine.sourceTypeListItems(inputs::kAudioInLibobs, inputs::kAudioInProp))
        crudas.push_back({d.name, idDesdeLibobs(d.value), "", false});
    for (auto& [nombre, valor] : unificarEntradas(crudas, kElemento)) out.push_back({nombre, valor, false});
    return out;
}

} // namespace ba::app::audio
