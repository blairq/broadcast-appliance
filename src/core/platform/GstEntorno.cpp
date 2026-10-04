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
std::string texto(const fs::path& v) {
    const std::u8string s = v.u8string();
    return std::string(s.begin(), s.end());
}
#ifdef _WIN32
constexpr char kSepRuta = ';';
#else
constexpr char kSepRuta = ':';
#endif
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
    // Los de GStreamer y los propios (gst-plugins/: superresolución…).
    std::string ruta = texto(raiz / "lib" / "gstreamer-1.0");
    std::error_code ec2;
    if (fs::is_directory(bundleRoot() / "gst-plugins", ec2))
        ruta += std::string(1, kSepRuta) + texto(bundleRoot() / "gst-plugins");
    if (const char* extra = std::getenv("BA_GST_PLUGINS_EXTRA"); extra != nullptr && *extra != '\0')
        ruta += std::string(1, kSepRuta) + extra;
    ponerTexto("GST_PLUGIN_SYSTEM_PATH_1_0", ruta.c_str());
    ponerTexto("GST_PLUGIN_SYSTEM_PATH", ruta.c_str());
    ponerTexto("GST_PLUGIN_PATH_1_0", "");
    ponerTexto("GST_PLUGIN_PATH", "");
#ifdef _WIN32
    // Las DLL de GStreamer están junto al .exe (bin/64bit): el escáner de
    // plugins (otro proceso, en libexec/) y los plugins que carga las buscan
    // por el PATH.
    {
        const char* p = std::getenv("PATH");
        const std::string bin = texto(bundleRoot() / "bin" / "64bit");
        ponerTexto("PATH", (bin + ";" + (p != nullptr ? p : "")).c_str());
        // Python 3.8+ no busca las DLL de las extensiones por el PATH: gi
        // (PyGObject, con el parche de GStreamer) las agrega de acá.
        ponerTexto("PYGI_DLL_DIRS", bin.c_str());
    }
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
    // Python con GStreamer (los plugins de Python del usuario y libgstpython):
    // la introspección del bundle, y el Python del bundle para el que se
    // compilaron los bindings (libgstpython lo inicializa adentro de este
    // proceso: sin PYTHONHOME buscaría la biblioteca estándar al lado del .exe).
    // Los Python hijos corren con -I o sin las PYTHON* (plugins/Plugins), así
    // que PYTHONHOME no los toca; GI_TYPELIB_PATH y la ruta de bibliotecas sí
    // les llegan (las heredan).
    poner("GI_TYPELIB_PATH", raiz / "lib" / "girepository-1.0");
    {
        std::error_code ec3;
        const fs::path py = bundleRoot() / "python";
        if (fs::is_directory(py, ec3)) poner("PYTHONHOME", py);
    }
#if defined(__linux__)
    {
        // gi abre las bibliotecas de los .typelib por nombre (dlopen): en un
        // proceso de Python hijo, solo las encuentra por LD_LIBRARY_PATH.
        const char* l = std::getenv("LD_LIBRARY_PATH");
        std::string v = texto(raiz / "lib") + ":" + texto(bundleRoot() / "bin" / "64bit");
        if (l != nullptr && *l != '\0') v += std::string(":") + l;
        ponerTexto("LD_LIBRARY_PATH", v.c_str());
    }
#endif
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
