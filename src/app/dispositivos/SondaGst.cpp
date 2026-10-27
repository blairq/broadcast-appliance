// src/app/dispositivos/SondaGst.cpp — los proveedores de dispositivos de
// GStreamer como drivers (ver SondaReal.hpp)

#include "dispositivos/SondaReal.hpp"

#include "i18n/I18n.hpp"

#include "platform/GstEntorno.hpp"

#include <QByteArray>
#include <QtGlobal>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include <cstdio>
#include <filesystem>
#include <system_error>

namespace ba::app::dispositivos::sonda {

using json = nlohmann::json;

#if defined(BA_HAVE_GST)
namespace {

/// gst_init en el hijo, con el mismo entorno que la app pero viendo los rangos
/// VERDADEROS: la app le saca GST_PLUGIN_FEATURE_RANK (ahí van los que están en
/// cuarentena con NONE) y prepararGst lo vuelve a poner; se lo deja como vino.
bool iniciar() {
    const bool habia = qEnvironmentVariableIsSet("GST_PLUGIN_FEATURE_RANK");
    const QByteArray previo = qgetenv("GST_PLUGIN_FEATURE_RANK");
    if (!ba::platform::prepararGst()) return false;
    if (habia)
        qputenv("GST_PLUGIN_FEATURE_RANK", previo);
    else
        qunsetenv("GST_PLUGIN_FEATURE_RANK");
    GError* e = nullptr;
    const bool ok = gst_init_check(nullptr, nullptr, &e);
    if (e != nullptr) {
        std::fprintf(stderr, "gst_init: %s\n", e->message);
        g_error_free(e);
    }
    // Lo mismo que hace la app (video/CatalogoGst): el directshow del sistema, afuera.
    if (ok)
        if (GstPlugin* p = gst_registry_find_plugin(gst_registry_get(), "directshow")) {
            gst_registry_remove_plugin(gst_registry_get(), p);
            gst_object_unref(p);
        }
    return ok;
}

bool g_gst = false;

std::string metadato(GstDeviceProviderFactory* f, const char* k) {
    const gchar* v = gst_device_provider_factory_get_metadata(f, k);
    return v != nullptr ? v : "";
}

/// El plugin del proveedor (archivo, versión y tamaño), leído del registro: sin cargarlo.
std::string firmaDe(GstPluginFeature* f) {
    std::string firma;
    if (GstPlugin* p = gst_plugin_feature_get_plugin(f)) {
        const gchar* v = gst_plugin_get_version(p);
        const gchar* a = gst_plugin_get_filename(p);
        firma = std::string(gst_plugin_get_name(p)) + "@" + (v != nullptr ? v : "?");
        if (a != nullptr) {
            std::error_code ec;
            // La ruta viene en UTF-8 (también en Windows): como char8_t, para que path no la lea en ANSI.
            const std::string a8 = a;
            const std::filesystem::path ruta(std::u8string(a8.begin(), a8.end()));
            const auto tam = std::filesystem::file_size(ruta, ec);
            const std::string nombre = a8.substr(a8.find_last_of("/\\") == std::string::npos ? 0 : a8.find_last_of("/\\") + 1);
            firma += ":" + nombre + ":" + (ec ? std::string("?") : std::to_string(tam));
        }
        gst_object_unref(p);
    }
    return firma;
}

json probarProveedor(const std::string& nombre) {
    GstDeviceProviderFactory* f = gst_device_provider_factory_find(nombre.c_str());
    if (f == nullptr) return {{"ausente", true}};
    GstDeviceProvider* p = gst_device_provider_factory_get(f);
    gst_object_unref(f);
    if (p == nullptr) return {{"ok", false}, {"motivo", ba::i18n::traducir("dispositivos", "no se pudo cargar su plugin")}};
    GstDeviceProviderClass* k = GST_DEVICE_PROVIDER_GET_CLASS(p);
    GList* devs = nullptr;
    bool arrancado = false;
    if (k->probe != nullptr) {
        devs = gst_device_provider_get_devices(p); // sin arrancar: llama a probe
    } else {
        // Los que solo vigilan (no tienen probe): arrancar, pedir la lista y parar.
        if (!gst_device_provider_start(p)) {
            gst_object_unref(p);
            return {{"ok", false}, {"motivo", ba::i18n::traducir("dispositivos", "no arrancó")}};
        }
        arrancado = true;
        devs = gst_device_provider_get_devices(p);
    }
    json lista = json::array();
    int audio = 0, video = 0, n = 0;
    for (GList* l = devs; l != nullptr; l = l->next) {
        auto* d = GST_DEVICE(l->data);
        ++n;
        audio += gst_device_has_classes(d, "Audio") ? 1 : 0;
        video += gst_device_has_classes(d, "Video") ? 1 : 0;
        if (lista.size() < 40) {
            gchar* nom = gst_device_get_display_name(d);
            gchar* cl = gst_device_get_device_class(d);
            lista.push_back({{"nombre", nom != nullptr ? nom : ""}, {"clase", cl != nullptr ? cl : ""}});
            g_free(nom);
            g_free(cl);
        }
    }
    g_list_free_full(devs, gst_object_unref);
    if (arrancado) gst_device_provider_stop(p);
    gst_object_unref(p);
    std::string detalle = ba::i18n::traducir("dispositivos", "%n dispositivo", nullptr, n);
    if (audio > 0 && video > 0)
        detalle += " " + ba::i18n::llenar(ba::i18n::traducir("dispositivos", "(%1 de audio, %2 de video)"),
                                          {std::to_string(audio), std::to_string(video)});
    return {{"ok", true}, {"detalle", detalle}, {"dispositivos", lista}};
}

} // namespace
#endif

Fuente fuenteGStreamer() {
    Fuente f;
#if defined(BA_HAVE_GST)
    f.preparar = []() { g_gst = iniciar(); };
    f.listar = []() {
        std::vector<json> out;
        if (!g_gst) return out;
        // Solo los que eligen los monitores de dispositivos (rango MARGINAL o más): uno
        // con NONE no lo carga nadie solo, no hace falta cuidarse de él.
        GList* fs = gst_device_provider_factory_list_get_device_providers(GST_RANK_MARGINAL);
        for (GList* l = fs; l != nullptr; l = l->next) {
            auto* fab = GST_DEVICE_PROVIDER_FACTORY(l->data);
            const std::string nombre = GST_OBJECT_NAME(fab);
            if (nombre == "asiodeviceprovider") continue; // cada driver ASIO va aparte (SondaAsio)
            std::string largo = metadato(fab, GST_ELEMENT_METADATA_LONGNAME);
            out.push_back({{"id", "gst:" + nombre},
                           {"nombre", largo.empty() ? nombre : largo},
                           {"tipo", "gstreamer"},
                           {"firma", firmaDe(GST_PLUGIN_FEATURE(fab))},
                           {"rango", static_cast<int>(gst_plugin_feature_get_rank(GST_PLUGIN_FEATURE(fab)))}});
        }
        gst_plugin_feature_list_free(fs);
        return out;
    };
    f.esMia = [](const std::string& id) { return id.rfind("gst:", 0) == 0; };
    f.probar = [](const std::string& id) -> json {
        if (!g_gst) return {{"ok", false}, {"motivo", ba::i18n::traducir("dispositivos", "GStreamer no arrancó en la sonda")}};
        return probarProveedor(id.substr(4));
    };
#endif
    return f;
}

void ponerRangoDeProveedor(const std::string& nombre, int rango) {
#if defined(BA_HAVE_GST)
    if (!gst_is_initialized()) return;
    GstDeviceProviderFactory* f = gst_device_provider_factory_find(nombre.c_str());
    if (f == nullptr) return;
    gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(f), static_cast<guint>(rango < 0 ? 0 : rango));
    gst_object_unref(f);
#else
    (void)nombre;
    (void)rango;
#endif
}

int principalDeLaApp(int argc, char** argv) {
    return principal(argc, argv, {fuenteGStreamer(), fuenteAsio(), fuenteDePrueba()});
}

} // namespace ba::app::dispositivos::sonda
