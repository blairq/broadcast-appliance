// src/core/platform/Herramientas.cpp — ver Herramientas.hpp

#include "platform/Herramientas.hpp"

#include "platform/GstEntorno.hpp"
#include "platform/Paths.hpp"

#include <cstdlib>
#include <string>
#include <system_error>

namespace ba::platform {

namespace fs = std::filesystem;

namespace {
#ifdef _WIN32
constexpr char kSep = ';';
constexpr const char* kExe = ".exe";
#else
constexpr char kSep = ':';
constexpr const char* kExe = "";
#endif

std::string texto(const fs::path& p) {
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}
void poner(const char* k, const std::string& v) {
#ifdef _WIN32
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}
} // namespace

fs::path herramientasDir() {
    std::error_code ec;
    if (const char* e = std::getenv("BA_HERRAMIENTAS_RAIZ"); e != nullptr && *e != '\0')
        if (fs::is_directory(e, ec)) return fs::path(e);
    const fs::path p = bundleRoot() / "herramientas";
    return fs::is_directory(p, ec) ? p : fs::path{};
}

fs::path herramienta(const char* nombre) {
    std::error_code ec;
    const std::string archivo = std::string(nombre) + kExe;
    for (const fs::path& d : {herramientasDir(), gstRaiz().empty() ? fs::path{} : gstRaiz() / "bin"}) {
        if (d.empty()) continue;
        const fs::path p = d / archivo;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return {};
}

bool prepararHerramientas(const fs::path& extra) {
    std::error_code ec;
    const fs::path dir = herramientasDir();
    // El de GStreamer también: gst-launch-1.0 de la terminal tiene que ver los
    // plugins del bundle, no los de la máquina.
    const bool gst = prepararGst();
    std::string antes;
    if (!extra.empty() && fs::is_directory(extra, ec)) antes += texto(extra) + kSep;
    if (!dir.empty()) antes += texto(dir) + kSep;
    if (gst) antes += texto(gstRaiz() / "bin") + kSep;
    if (antes.empty()) return false;
    const char* path = std::getenv("PATH");
    std::string actual = path != nullptr ? path : "";
    // Una sola vez (si el proceso se relanza a sí mismo, no se acumula).
    if (actual.rfind(antes, 0) != 0) poner("PATH", antes + actual);
    if (!dir.empty()) poner("BA_HERRAMIENTAS", texto(dir));
    for (const auto& [var, nombre] : {std::pair{"BA_FFMPEG", "ffmpeg"},
                                      {"BA_FFPROBE", "ffprobe"},
                                      {"BA_YTDLP", "yt-dlp"},
                                      {"BA_GST_LAUNCH", "gst-launch-1.0"}})
        if (const fs::path p = herramienta(nombre); !p.empty()) poner(var, texto(p));
    return !dir.empty();
}

} // namespace ba::platform
