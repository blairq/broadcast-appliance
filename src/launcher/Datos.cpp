// src/launcher/Datos.cpp — ver Datos.hpp

#include "Datos.hpp"

#include <cctype>

#include <fstream>
#include <system_error>

#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

namespace ba::launcher {

namespace fs = std::filesystem;
using ba::i18n::llenar;
using ba::i18n::traducir;

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
        avisos.push_back(llenar(traducir("datos", "no se pudo crear la carpeta de datos %1 (%2): se usan los datos de "
                                                  "adentro del programa"),
                                {u8(datos), ec.message()}));
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
        avisos.push_back(e2 ? llenar(traducir("datos", "no se pudo mudar %1: %2"), {d, e2.message()})
                            : llenar(traducir("datos", "datos mudados: %1 -> %2"), {d, u8(a)}));
    }
    // Las semillas van en la configuración activa (prepararConfiguraciones).
    return datos;
}

namespace {
/// La carpeta de `dir` que se llama EXACTAMENTE `nombre` ("" si no hay). En Windows
/// (y en el disco de la Mac) el sistema no distingue mayúsculas: fs::exists(datos /
/// "Plugins") encuentra `plugins/`, que es del programa (los módulos de audio LSP).
fs::path exacta(const fs::path& dir, const char* nombre) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().filename().u8string() == std::u8string(reinterpret_cast<const char8_t*>(nombre))) return e.path();
    return {};
}

/// La carpeta de `dir` que se llama como `nombre` con OTRA grafía de mayúsculas
/// ("" si no hay): `plugins` para "Plugins". En Windows y la Mac `dir / nombre`
/// ya la encontraba (no distinguen); en Linux no, y lo del usuario no se mudaba.
fs::path otraGrafia(const fs::path& dir, const char* nombre) {
    const auto bajar = [](std::string t) {
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string buscado = bajar(nombre);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n != nombre && bajar(n) == buscado && e.is_directory(ec)) return e.path();
    }
    return {};
}

/// Lo que es del programa adentro de una carpeta que se llama como una de datos
/// (con los datos en la misma carpeta que el programa, Windows): no es del usuario.
bool esDelPrograma(const fs::path& hijo) {
    const std::u8string n = hijo.filename().u8string();
    return n == u8"lsp"; // plugins/lsp: los módulos de audio (ver BaLsp)
}
} // namespace

fs::path prepararConfiguraciones(const fs::path& bundle, const fs::path& datos, std::vector<std::string>& avisos) {
    std::error_code ec;
    const fs::path confs = datos / "Configuraciones";
    // En Windows los datos viven en la carpeta del programa: sus carpetas de fábrica
    // (las semillas, plugins/lsp) están al lado de las de datos, con los mismos nombres.
    const bool juntos = fs::equivalent(bundle, datos, ec);
    const auto esSemilla = [](const char* d) {
        for (const char* s : kSemillas)
            if (std::string(s) == d) return true;
        return false;
    };
    if (!fs::is_directory(confs, ec)) {
        // La primera vez: lo de la configuración se muda a «Principal» (renombrar: nada se copia).
        const fs::path principal = confs / "Principal";
        fs::create_directories(principal, ec);
        if (ec) {
            avisos.push_back(llenar(traducir("datos", "no se pudo crear %1: %2 (sigue sin configuraciones)"),
                                    {u8(principal), ec.message()}));
            return datos;
        }
        // launcher.json es de la máquina (el que arranca todo): sale de Config/ antes de mudarla.
        if (fs::exists(datos / "Config" / "launcher.json", ec) && !fs::exists(datos / "launcher.json", ec)) {
            fs::rename(datos / "Config" / "launcher.json", datos / "launcher.json", ec);
            if (ec) avisos.push_back(llenar(traducir("datos", "no se pudo mover launcher.json: %1"), {ec.message()}));
        }
        for (const char* d : ba::platform::kCarpetasDeConfiguracion) {
            std::error_code e2;
            const fs::path de = exacta(datos, d);
            if (de.empty()) {
                // Con otra grafía (Windows: `plugins`, la del programa): se muda solo lo del usuario.
                const fs::path otra = otraGrafia(datos, d);
                if (!juntos || otra.empty()) continue;
                for (const auto& h : fs::directory_iterator(otra, ec)) {
                    if (esDelPrograma(h.path())) continue;
                    fs::create_directories(principal / d, e2);
                    fs::rename(h.path(), principal / d / h.path().filename(), e2);
                    if (e2) avisos.push_back(llenar(traducir("datos", "no se pudo mudar %1: %2"), {u8(h.path()), e2.message()}));
                }
                continue;
            }
            if (juntos && esSemilla(d)) {
                // Es también la semilla de fábrica: se copia (lo del usuario incluido) y queda.
                fs::copy(de, principal / d, fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
                avisos.push_back(e2 ? llenar(traducir("datos", "no se pudo copiar %1 a la configuración Principal: %2"),
                                             {d, e2.message()})
                                    : llenar(traducir("datos", "%1 (copia) -> Configuraciones/Principal"), {d}));
                continue;
            }
            fs::rename(de, principal / d, e2);
            avisos.push_back(e2 ? llenar(traducir("datos", "no se pudo mudar %1 a la configuración Principal: %2"),
                                         {d, e2.message()})
                                : llenar(traducir("datos", "%1 -> Configuraciones/Principal"), {d}));
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
    // REPARAR lo que mudó una versión anterior (2.0.0 del 2026-10-08) con los datos
    // junto al programa: se llevó plugins/lsp (los módulos de audio: la integridad
    // los daba por ausentes en cada arranque) y las semillas de fábrica.
    if (juntos) {
        const fs::path lspMudado = activa / "Plugins" / "lsp";
        if (!fs::exists(bundle / "plugins" / "lsp", ec) && fs::is_directory(lspMudado, ec)) {
            fs::create_directories(bundle / "plugins", ec);
            std::error_code e2;
            fs::rename(lspMudado, bundle / "plugins" / "lsp", e2);
            avisos.push_back(e2 ? "no se pudo devolver plugins/lsp: " + e2.message()
                                : "plugins/lsp devuelto al programa (lo había mudado una versión anterior)");
        }
        for (const char* d : kSemillas) {
            if (!vacia(bundle / d) || vacia(activa / d)) continue;
            std::error_code e2;
            fs::copy(activa / d, bundle / d, fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
            if (!e2) avisos.push_back(std::string(d) + " de fábrica devuelto al programa");
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
