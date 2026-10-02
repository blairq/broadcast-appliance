// src/launcher/Datos.cpp — ver Datos.hpp

#include "Datos.hpp"

#include <system_error>

namespace ba::launcher {

namespace fs = std::filesystem;

namespace {
/// Datos del usuario que versiones anteriores dejaban adentro del bundle.
constexpr const char* kDatos[] = {"Config", "Credenciales", "Logs", "Recordings", "Media",   "Eventos",
                                  "Modelos", "Agentes",     "Shares", "Respaldos", "Exportado", "Runtime"};
/// De fábrica, ampliables por el usuario: se siembran (se copia lo que falta).
constexpr const char* kSemillas[] = {"Graficos", "Extensiones"};

bool vacia(const fs::path& p) {
    std::error_code ec;
    return !fs::exists(p, ec) || (fs::is_directory(p, ec) && fs::is_empty(p, ec));
}

std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
} // namespace

fs::path prepararDatos(const fs::path& bundle, const fs::path& datos, std::vector<std::string>& avisos) {
    if (datos.empty() || datos == bundle) return bundle;
    std::error_code ec;
    fs::create_directories(datos, ec);
    if (ec || !fs::is_directory(datos, ec)) {
        avisos.push_back("no se pudo crear la carpeta de datos " + u8(datos) + " (" + ec.message() +
                         "): se usan los datos de adentro del programa");
        return bundle;
    }
    // La mudanza: lo de adentro del bundle va a la carpeta nueva si allá no hay nada.
    for (const char* d : kDatos) {
        const fs::path de = bundle / d, a = datos / d;
        if (vacia(de) || !vacia(a)) continue;
        std::error_code e2;
        fs::remove(a, e2); // una carpeta vacía que estorba
        fs::rename(de, a, e2);
        if (e2) { // otro disco: copiar y borrar
            e2.clear();
            fs::copy(de, a, fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
            if (!e2) fs::remove_all(de, e2);
        }
        avisos.push_back(e2 ? "no se pudo mudar " + std::string(d) + ": " + e2.message()
                            : "datos mudados: " + std::string(d) + " -> " + u8(a));
    }
    // Las semillas: lo de fábrica que falta (sin pisar lo que el usuario cambió).
    for (const char* d : kSemillas) {
        const fs::path de = bundle / d;
        if (vacia(de)) continue;
        std::error_code e2;
        fs::copy(de, datos / d, fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
        if (e2) avisos.push_back("no se pudo copiar " + std::string(d) + " de fábrica: " + e2.message());
    }
    return datos;
}

} // namespace ba::launcher
