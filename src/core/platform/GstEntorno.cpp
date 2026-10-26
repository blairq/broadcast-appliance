// src/core/platform/GstEntorno.cpp — ver GstEntorno.hpp

#include "platform/GstEntorno.hpp"

#include "platform/Paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

namespace {
/// Dónde busca plugins: los de GStreamer y los propios (gst-plugins/:
/// superresolución…), más BA_GST_PLUGINS_EXTRA (pruebas).
std::string rutaPlugins(const fs::path& raiz) {
    std::string ruta = texto(raiz / "lib" / "gstreamer-1.0");
    std::error_code ec;
    if (fs::is_directory(bundleRoot() / "gst-plugins", ec))
        ruta += std::string(1, kSepRuta) + texto(bundleRoot() / "gst-plugins");
    if (const char* extra = std::getenv("BA_GST_PLUGINS_EXTRA"); extra != nullptr && *extra != '\0')
        ruta += std::string(1, kSepRuta) + extra;
    return ruta;
}
} // namespace

bool prepararGst() {
    const fs::path raiz = gstRaiz();
    if (raiz.empty()) return false;
    // Solo los plugins del bundle: la ruta del sistema apunta a la nuestra y la
    // del usuario queda vacía.
    const std::string ruta = rutaPlugins(raiz);
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
    // ASIO (Windows) nunca por su cuenta. Enumerar dispositivos con el proveedor de ASIO
    // CARGA cada driver ASIO instalado (ASIO4ALL, JackRouter, SAR…) para preguntarle los
    // canales: ASIO4ALL se adueña de la placa (WDM-KS) y la suelta, Windows avisa que
    // cambiaron los dispositivos, el registro vuelve a enumerar y el driver «entra y
    // sale» todo el tiempo (y JackRouter sin JACK abre su error en cada vuelta). Probado
    // en una notebook con los tres. El audio va por WASAPI; con rango NONE ni los
    // monitores de dispositivos ni autoaudiosrc/sink los eligen (asiosrc/asiosink siguen
    // existiendo para quien los pida por nombre). Lo que el operador haya puesto en
    // GST_PLUGIN_FEATURE_RANK se respeta (va adelante; GStreamer toma el último).
    {
        const char* previo = std::getenv("GST_PLUGIN_FEATURE_RANK");
        const std::string rangos = "asiodeviceprovider:NONE,asiosrc:NONE,asiosink:NONE";
        ponerTexto("GST_PLUGIN_FEATURE_RANK",
                   (previo != nullptr && *previo != '\0' ? rangos + "," + previo : rangos).c_str());
    }
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

// --- El registro de plugins ---------------------------------------------------------

namespace {
fs::path archivoRegistro() {
    const fs::path cache = cacheDir();
    return cache.empty() ? fs::path() : cache / "gstreamer-1.0-registry.bin";
}
/// Al lado del registro: la ruta de plugins con la que se hizo. GStreamer guarda
/// la ruta completa de cada plugin y, si cambia, los vuelve a revisar todos.
fs::path archivoRutaRegistro() {
    const fs::path r = archivoRegistro();
    return r.empty() ? fs::path() : fs::path(r).replace_extension(".ruta");
}
constexpr const char* kPrefijoDiag = "gst-plugins-";
bool g_diagnostico = false;

void quitarVariable(const char* k) {
#ifdef _WIN32
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}
} // namespace

bool registroPorRehacer() {
    const fs::path raiz = gstRaiz();
    const fs::path reg = archivoRegistro();
    if (raiz.empty() || reg.empty()) return false;
    std::error_code ec;
    if (!fs::is_regular_file(reg, ec)) return true;
    std::ifstream f(archivoRutaRegistro(), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return !f || ss.str() != rutaPlugins(raiz);
}

int contarPlugins() {
    const fs::path raiz = gstRaiz();
    if (raiz.empty()) return 0;
    // Lo que GStreamer va a mirar: los módulos de cada carpeta de la ruta (y sus
    // subcarpetas), con la extensión de módulo del sistema.
#if defined(_WIN32)
    const std::vector<std::string> ext = {".dll"};
#elif defined(__APPLE__)
    const std::vector<std::string> ext = {".dylib", ".so"};
#else
    const std::vector<std::string> ext = {".so"};
#endif
    int n = 0;
    std::stringstream ruta(rutaPlugins(raiz));
    std::string dir;
    while (std::getline(ruta, dir, kSepRuta)) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(fs::path(std::u8string(dir.begin(), dir.end())), ec), fin; !ec && it != fin; it.increment(ec))
            if (it->is_regular_file(ec) &&
                std::find(ext.begin(), ext.end(), texto(it->path().extension())) != ext.end())
                ++n;
    }
    return n;
}

bool empezarRegistro() {
    g_diagnostico = false;
    if (!registroPorRehacer()) return false;
    // Lo que pida quien corre la app manda (GST_DEBUG a mano para depurar).
    if (const char* d = std::getenv("GST_DEBUG"); d != nullptr && *d != '\0') return true;
    std::error_code ec;
    const fs::path logs = dataRoot() / "Logs";
    fs::create_directories(logs, ec);
    // Los de la vez anterior: solo quedan los de la última revisión.
    for (const auto& e : fs::directory_iterator(logs, ec)) {
        const std::string n = texto(e.path().filename());
        if (n.rfind(kPrefijoDiag, 0) == 0 && e.path().extension() == ".log") fs::remove(e.path(), ec);
    }
    // DEBUG (5) y no más: el GStreamer del bundle viene sin los mensajes de nivel
    // LOG. En el escáner, qué plugin empieza a cargar; en la app, cuáles fallaron.
    // Uno por proceso (%p: la app y cada escáner, que hereda el entorno).
    ponerTexto("GST_DEBUG", "GST_PLUGIN_LOADING:5");
    ponerTexto("GST_DEBUG_NO_COLOR", "1");
    poner("GST_DEBUG_FILE", logs / (std::string(kPrefijoDiag) + "%p.log"));
    g_diagnostico = true;
    return true;
}

std::vector<fs::path> archivosDeEscaneres() {
    std::vector<fs::path> v;
    if (!g_diagnostico) return v;
#ifdef _WIN32
    const std::string propio = std::string(kPrefijoDiag) + std::to_string(_getpid()) + ".log";
#else
    const std::string propio = std::string(kPrefijoDiag) + std::to_string(getpid()) + ".log";
#endif
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dataRoot() / "Logs", ec)) {
        const std::string n = texto(e.path().filename());
        if (n.rfind(kPrefijoDiag, 0) == 0 && e.path().extension() == ".log" && n != propio) v.push_back(e.path());
    }
    std::sort(v.begin(), v.end());
    return v;
}

void registroTerminado(bool ok) {
    if (ok && !archivoRutaRegistro().empty()) {
        std::ofstream f(archivoRutaRegistro(), std::ios::binary | std::ios::trunc);
        f << rutaPlugins(gstRaiz());
    }
    if (!g_diagnostico) return;
    g_diagnostico = false;
    // Que no lo hereden los procesos que la app lance después.
    quitarVariable("GST_DEBUG");
    quitarVariable("GST_DEBUG_NO_COLOR");
    quitarVariable("GST_DEBUG_FILE");
}

} // namespace ba::platform
