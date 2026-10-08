// src/launcher/Datos.cpp — ver Datos.hpp

#include "Datos.hpp"

#include <fstream>
#include <system_error>

#include "platform/Paths.hpp"

namespace ba::launcher {

namespace fs = std::filesystem;

namespace {
/// Datos del usuario que versiones anteriores dejaban adentro del bundle.
constexpr const char* kDatos[] = {"Config",  "Credenciales", "Logs",   "Recordings", "Media",     "Eventos",
                                  "Modelos", "Agentes",      "Shares", "Respaldos",  "Exportado", "Runtime"};
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
    // Las semillas van en la configuración activa (prepararConfiguraciones).
    return datos;
}

fs::path prepararConfiguraciones(const fs::path& bundle, const fs::path& datos, std::vector<std::string>& avisos) {
    std::error_code ec;
    const fs::path confs = datos / "Configuraciones";
    if (!fs::is_directory(confs, ec)) {
        // La primera vez: lo de la configuración se muda a «Principal» (renombrar: nada se copia).
        const fs::path principal = confs / "Principal";
        fs::create_directories(principal, ec);
        if (ec) {
            avisos.push_back("no se pudo crear " + u8(principal) + ": " + ec.message() + " (sigue sin configuraciones)");
            return datos;
        }
        // launcher.json es de la máquina (el que arranca todo): sale de Config/ antes de mudarla.
        if (fs::exists(datos / "Config" / "launcher.json", ec) && !fs::exists(datos / "launcher.json", ec)) {
            fs::rename(datos / "Config" / "launcher.json", datos / "launcher.json", ec);
            if (ec) avisos.push_back("no se pudo mover launcher.json: " + ec.message());
        }
        for (const char* d : ba::platform::kCarpetasDeConfiguracion) {
            const fs::path de = datos / d;
            if (!fs::exists(de, ec)) continue;
            std::error_code e2;
            fs::rename(de, principal / d, e2);
            avisos.push_back(e2 ? "no se pudo mudar " + std::string(d) + " a la configuración Principal: " + e2.message()
                                : std::string(d) + " -> Configuraciones/Principal");
        }
        std::ofstream(datos / "configuraciones.json") << "{\n \"activa\": \"Principal\"\n}\n";
    }
    fs::path activa = ba::platform::configRootFor(datos);
    if (activa == datos) {
        // configuraciones.json roto o apuntando a una que no está: la primera que haya.
        for (const auto& e : fs::directory_iterator(confs, ec))
            if (e.is_directory()) {
                const std::u8string n = e.path().filename().u8string();
                std::ofstream(datos / "configuraciones.json")
                    << "{\n \"activa\": \"" << std::string(n.begin(), n.end()) << "\"\n}\n";
                avisos.push_back("la configuración activa no estaba: se usa " + u8(e.path().filename()));
                activa = e.path();
                break;
            }
        if (activa == datos) {
            fs::create_directories(confs / "Principal", ec);
            std::ofstream(datos / "configuraciones.json") << "{\n \"activa\": \"Principal\"\n}\n";
            activa = confs / "Principal";
        }
    }
    // Las semillas: lo de fábrica que falta (sin pisar lo que el usuario cambió).
    for (const char* d : kSemillas) {
        const fs::path de = bundle / d;
        if (vacia(de)) continue;
        std::error_code e2;
        fs::copy(de, activa / d, fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
        if (e2) avisos.push_back("no se pudo copiar " + std::string(d) + " de fábrica: " + e2.message());
    }
    return activa;
}

} // namespace ba::launcher
