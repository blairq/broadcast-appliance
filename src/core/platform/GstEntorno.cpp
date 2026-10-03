// src/core/platform/GstEntorno.cpp — ver GstEntorno.hpp

#include "platform/GstEntorno.hpp"

#include "platform/Paths.hpp"

#include <cstdlib>
#include <string>
#include <system_error>

namespace ba::platform {

namespace fs = std::filesystem;

namespace {
void poner(const char* k, const fs::path& v) {
    const std::u8string s = v.u8string();
    const std::string t(s.begin(), s.end());
#ifdef _WIN32
    _putenv_s(k, t.c_str());
#else
    setenv(k, t.c_str(), 1);
#endif
}
void ponerTexto(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}
} // namespace

fs::path gstRaiz() {
    std::error_code ec;
    if (const char* e = std::getenv("BA_GST_RAIZ"); e != nullptr && *e != '\0') {
        const fs::path p(e);
        if (fs::is_directory(p / "lib" / "gstreamer-1.0", ec)) return p;
    }
    const fs::path bin = bundleRoot() / "bin" / "64bit";
    for (const fs::path& p : {bin / "GStreamer.framework" / "Versions" / "1.0", bin / "gstreamer"})
        if (fs::is_directory(p / "lib" / "gstreamer-1.0", ec)) return p;
    return {};
}

bool prepararGst() {
    const fs::path raiz = gstRaiz();
    if (raiz.empty()) return false;
    // Solo los plugins del bundle: la ruta del sistema apunta a la nuestra y la
    // del usuario queda vacía.
    poner("GST_PLUGIN_SYSTEM_PATH_1_0", raiz / "lib" / "gstreamer-1.0");
    poner("GST_PLUGIN_SYSTEM_PATH", raiz / "lib" / "gstreamer-1.0");
    ponerTexto("GST_PLUGIN_PATH_1_0", "");
    ponerTexto("GST_PLUGIN_PATH", "");
#ifdef _WIN32
    const char* escaner = "gst-plugin-scanner.exe";
#else
    const char* escaner = "gst-plugin-scanner";
#endif
    poner("GST_PLUGIN_SCANNER_1_0", raiz / "libexec" / "gstreamer-1.0" / escaner);
    poner("GST_PLUGIN_SCANNER", raiz / "libexec" / "gstreamer-1.0" / escaner);
    // El registro (la lista de plugins ya escaneados), en la caché del bundle.
    const fs::path cache = cacheDir();
    if (!cache.empty()) poner("GST_REGISTRY_1_0", cache / "gstreamer-1.0-registry.bin");
    poner("GIO_EXTRA_MODULES", raiz / "lib" / "gio" / "modules");
    ponerTexto("GIO_USE_VFS", "local");
    // Sin resolución de proxy: el de GIO (libproxy) busca la configuración
    // automática de la red (WPAD) antes de cada conexión, y una cámara RTSP de
    // la red local tardaba MINUTOS en conectar (y avisaba «pxbackend: Could not
    // parse ignore mask 169.254/16»). Las fuentes de red de un estudio no van
    // por proxy; y así tampoco se lee la configuración del sistema.
    ponerTexto("GIO_USE_PROXY_RESOLVER", "dummy");
    return true;
}

} // namespace ba::platform
