// ShadowCopy.cpp
//
// Implementacion real de la copia sombra (spec 3.1.5, decision D3; ver
// ShadowCopy.hpp). Copia bin/, obs-plugins/ y data/ desde la raiz del disco de
// origen a <tempBase>/BroadcastAppliance/<hash>/ y devuelve esa raiz sombra
// para que los procesos hijos se ejecuten desde ahi: asi un movimiento del
// cable USB no mata a los hijos con EXCEPTION_IN_PAGE_ERROR.
//
// Modelo de threading:
//   prepare() se invoca desde el thread principal ANTES de lanzar hijos; no
//   hay estado mutable compartido (solo std::filesystem + Log, que es
//   thread-safe), por lo que es seguro llamarlo desde un solo thread.
//
// Estrategia de errores:
//   NUNCA lanza. Ante cualquier fallo (tempBase, create_directories,
//   copy_file, marker) loguea con BA_LOG_ERROR y prepare() devuelve la raiz
//   ORIGINAL (fail-safe: se ejecuta desde el disco). cleanup() ante un fallo
//   loguea y no hace nada mas. El marker ".shadow_ok" se escribe al FINAL de
//   la copia: un directorio a medio copiar no se trata como valido y se
//   reconstruye en la proxima prepare().
//
// Nota (spec 3.1.5): config/, logs/ y recordings/ se quedan en el disco
// original; solo se copian bin/, obs-plugins/ y data/.

#include "ShadowCopy.hpp"

#include "Log.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ba::launcher {

namespace {

namespace fs = std::filesystem;

// Nombre del archivo marker que marca una copia sombra completa y valida.
constexpr const char* kMarkerName = ".shadow_ok";

// Nombre del subdirectorio base dentro de tempBase.
constexpr const wchar_t* kAppDir = L"BroadcastAppliance";

// Convierte un std::wstring a fs::path de forma portable.
// En Windows value_type es wchar_t y la conversion es nativa; en Linux
// value_type es char y se hace un cast byte a byte (suficiente para las rutas
// ASCII que usa el launcher y el test; para rutas no-ASCII haria falta una
// codificacion UTF-8 explicita).
fs::path toPath(const std::wstring& w) {
#ifdef _WIN32
    return fs::path(w);
#else
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) {
        s.push_back(static_cast<char>(c));
    }
    return fs::path(s);
#endif
}

// Convierte un fs::path a std::wstring (inverso de toPath).
std::wstring toString(const fs::path& p) {
#ifdef _WIN32
    return p.wstring();
#else
    const std::string s = p.string();
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) {
        w.push_back(static_cast<wchar_t>(c));
    }
    return w;
#endif
}

// Devuelve la base temporal: GetTempPathW() en Windows, "/tmp" en el resto.
// Ante un fallo devuelve false y deja out sin modificar.
bool tempBase(std::wstring& out) {
#ifdef _WIN32
    wchar_t buf[4096];
    const DWORD n = ::GetTempPathW(ARRAYSIZE(buf), buf);
    if (n == 0 || n > ARRAYSIZE(buf)) {
        return false;
    }
    out.assign(buf, n);
    return true;
#else
    out = L"/tmp";
    return true;
#endif
}

// Deja solo los caracteres alfanumericos, '-' y '_'. Si el resultado queda
// vacio, devuelve "default" (un hash vacio o con solo simbolos no es un
// subdirectorio util).
std::wstring sanitizeHash(const std::wstring& h) {
    std::wstring out;
    out.reserve(h.size());
    for (wchar_t c : h) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == L'-' || c == L'_') {
            out.push_back(c);
        }
    }
    if (out.empty()) {
        return L"default";
    }
    return out;
}

// Copia recursivamente el arbol src hacia dst creando los directorios
// intermedios. Sobrescribe archivos existentes (overwrite_existing). Nunca
// lanza: ante un error de std::filesystem devuelve false. Si src no existe se
// considera "no hay nada que copiar" y devuelve true (un layout parcial es
// aceptable: por ejemplo un disco sin data/).
bool copyTree(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        return true; // sin fuente: no es un error de copia.
    }
    if (!fs::is_directory(src, ec) || ec) {
        return false;
    }
    if (!fs::create_directories(dst, ec) && ec) {
        return false;
    }
    fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const fs::directory_entry& entry = *it;
        std::error_code ec2;
        const fs::path rel = fs::relative(entry.path(), src, ec2);
        if (ec2) {
            return false;
        }
        const fs::path target = dst / rel;
        if (entry.is_directory()) {
            if (!fs::create_directories(target, ec2) && ec2) {
                return false;
            }
        } else if (entry.is_regular_file()) {
            if (!fs::create_directories(target.parent_path(), ec2) && ec2) {
                return false;
            }
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec2);
            if (ec2) {
                return false;
            }
        }
        it.increment(ec);
    }
    return true;
}

// Escribe el marker ".shadow_ok" en shadowRoot. Solo se llama al FINAL de una
// copia exitosa: asi un directorio a medio copiar nunca se marca como valido.
bool writeMarker(const fs::path& shadowRoot, const std::wstring& versionHash) {
    std::ofstream f(shadowRoot / kMarkerName, std::ios::trunc);
    if (!f) {
        return false;
    }
    f << "shadow_ok\nhash=" << std::string(versionHash.begin(), versionHash.end()) << "\n";
    f.flush();
    return static_cast<bool>(f);
}

} // namespace

std::wstring ShadowCopy::prepare(const std::wstring& root, const std::wstring& versionHash) {
    std::wstring tempBaseStr;
    if (!tempBase(tempBaseStr)) {
        BA_LOG_ERROR("shadow", 1,
                     "no se pudo obtener la carpeta temporal; ejecutar desde "
                     "la raiz (fail-safe)");
        return root;
    }

    const fs::path shadowRoot = toPath(tempBaseStr) / kAppDir / sanitizeHash(versionHash);

    // Reuso: si ya existe y tiene el marker, la copia es completa y valida.
    std::error_code ec;
    if (fs::exists(shadowRoot, ec) && !ec && fs::exists(shadowRoot / kMarkerName, ec) && !ec) {
        BA_LOG_INFO("shadow", 0, "copia sombra reutilizada (reused): " + shadowRoot.string());
        return toString(shadowRoot);
    }

    // Si queda un directorio sin marker (copia a medias de un crash anterior)
    // se reconstruye limpio para no arrastrar archivos obsoletos.
    if (fs::exists(shadowRoot, ec) && !ec) {
        fs::remove_all(shadowRoot, ec);
        if (ec) {
            BA_LOG_ERROR("shadow", 2,
                         "no se pudo limpiar la copia sombra a medias: " + shadowRoot.string() +
                             "; ejecutar desde la raiz");
            return root;
        }
    }

    // Copia los tres arboles que viajan con la version (spec 3.1.5).
    const fs::path rootPath = toPath(root);
    const char* subdirs[] = {"bin", "obs-plugins", "data"};
    for (const char* sub : subdirs) {
        if (!copyTree(rootPath / sub, shadowRoot / sub)) {
            BA_LOG_ERROR("shadow", 3,
                         "fallo al copiar " + std::string(sub) + " hacia " + shadowRoot.string() +
                             "; ejecutar desde la raiz");
            return root;
        }
    }

    // El marker va al FINAL: marca la copia como completa y valida.
    if (!writeMarker(shadowRoot, versionHash)) {
        BA_LOG_ERROR("shadow", 4,
                     "no se pudo escribir el marker .shadow_ok en " + shadowRoot.string() +
                         "; ejecutar desde la raiz");
        return root;
    }

    BA_LOG_INFO("shadow", 0, "copia sombra creada: " + shadowRoot.string());
    return toString(shadowRoot);
}

void ShadowCopy::cleanup(const std::wstring& shadowRoot) {
    if (shadowRoot.empty()) {
        return;
    }
    std::error_code ec;
    const fs::path p = toPath(shadowRoot);
    if (!fs::exists(p, ec) || ec) {
        return; // no hay nada que borrar.
    }
    fs::remove_all(p, ec);
    if (ec) {
        BA_LOG_ERROR("shadow", 5, "no se pudo borrar la copia sombra: " + p.string());
        return;
    }
    BA_LOG_INFO("shadow", 0, "copia sombra eliminada: " + p.string());
}

} // namespace ba::launcher
