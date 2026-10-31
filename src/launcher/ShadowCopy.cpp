// ShadowCopy.cpp
//
// Implementacion real de la copia sombra (spec 3.1.5, decision D3; ver
// ShadowCopy.hpp). Copia bin/ y data/ desde la raiz del disco de
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
// original; solo se copian bin/ y data/ (kArboles).

#include "ShadowCopy.hpp"
#include "i18n/I18n.hpp"

#include "Log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

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
    // Portable: si el launcher redirigio las caches al bundle, ahi va.
    if (const char* own = std::getenv("BA_CACHE_DIR"); own != nullptr && *own != '\0') {
        const std::string s(own);
        const fs::path p(std::u8string(s.begin(), s.end()));
        std::error_code ec;
        fs::create_directories(p, ec);
        if (!ec) {
            out = p.wstring();
            return true;
        }
    }
#ifdef _WIN32
    wchar_t buf[4096];
    const DWORD n = ::GetTempPathW(ARRAYSIZE(buf), buf);
    if (n == 0 || n > ARRAYSIZE(buf)) {
        return false;
    }
    out.assign(buf, n);
    return true;
#else
    // Cache POR USUARIO en disco (no /tmp, que es compartido entre usuarios,
    // ni XDG_RUNTIME_DIR, que es RAM): Linux $XDG_CACHE_HOME o ~/.cache;
    // macOS ~/Library/Caches.
    fs::path base;
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        base = fs::path(home) / "Library" / "Caches";
    }
#else
    if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache != nullptr && *cache != '\0') {
        base = cache;
    } else if (const char* home = std::getenv("HOME")) {
        base = fs::path(home) / ".cache";
    }
#endif
    if (base.empty()) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(base, ec);
    out = base.wstring();
    return !ec;
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
/// Los arboles que viajan con la version (los binarios y sus datos).
constexpr const char* kArboles[] = {"bin", "data"};

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
        // lexically_relative y no fs::relative: el segundo RESUELVE symlinks
        // y un libX.so.6 terminaba copiado con el nombre de su destino.
        const fs::path rel = entry.path().lexically_relative(src);
        if (rel.empty()) {
            return false;
        }
        const fs::path target = dst / rel;
        // Symlinks primero y como symlinks: las bibliotecas versionadas de
        // Linux/macOS son cadenas libX.so.6 -> libX.so.6.6.3. Antes se
        // saltaban y la copia quedaba sin libQt6Core.so.6 (o las de GStreamer): el
        // enlazador caia a las del SISTEMA (otra version) y A no arrancaba.
        if (entry.is_symlink(ec2)) {
            if (!fs::create_directories(target.parent_path(), ec2) && ec2) {
                return false;
            }
            fs::remove(target, ec2);
            fs::copy_symlink(entry.path(), target, ec2);
            if (ec2) {
                return false;
            }
            it.increment(ec);
            continue;
        }
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
                     ba::i18n::traducir("shadow", "no se pudo obtener la carpeta temporal; ejecutar desde "
                                                  "la raiz (fail-safe)"));
        return root;
    }

    const fs::path shadowRoot = toPath(tempBaseStr) / kAppDir / sanitizeHash(versionHash);

    // Reuso: si ya existe y tiene el marker, la copia es completa y valida.
    std::error_code ec;
    if (fs::exists(shadowRoot, ec) && !ec && fs::exists(shadowRoot / kMarkerName, ec) && !ec) {
        BA_LOG_INFO("shadow", 0, ba::i18n::llenar(ba::i18n::traducir("shadow", "copia sombra reutilizada (reused): %1"),
                                                  {shadowRoot.string()}));
        return toString(shadowRoot);
    }

    // Si queda un directorio sin marker (copia a medias de un crash anterior)
    // se reconstruye limpio para no arrastrar archivos obsoletos.
    if (fs::exists(shadowRoot, ec) && !ec) {
        fs::remove_all(shadowRoot, ec);
        if (ec) {
            BA_LOG_ERROR("shadow", 2,
                         ba::i18n::llenar(ba::i18n::traducir("shadow", "no se pudo limpiar la copia sombra a medias: %1; "
                                                                       "ejecutar desde la raiz"),
                                          {shadowRoot.string()}));
            return root;
        }
    }

    // Copia los tres arboles que viajan con la version (spec 3.1.5).
    const fs::path rootPath = toPath(root);
    for (const char* sub : kArboles) {
        if (!copyTree(rootPath / sub, shadowRoot / sub)) {
            BA_LOG_ERROR("shadow", 3,
                         ba::i18n::llenar(ba::i18n::traducir("shadow", "fallo al copiar %1 hacia %2; ejecutar desde la raiz"),
                                          {std::string(sub), shadowRoot.string()}));
            return root;
        }
    }

    // El marker va al FINAL: marca la copia como completa y valida.
    if (!writeMarker(shadowRoot, versionHash)) {
        BA_LOG_ERROR("shadow", 4,
                     ba::i18n::llenar(ba::i18n::traducir("shadow", "no se pudo escribir el marker .shadow_ok en %1; "
                                                                   "ejecutar desde la raiz"),
                                      {shadowRoot.string()}));
        return root;
    }

    BA_LOG_INFO("shadow", 0, ba::i18n::llenar(ba::i18n::traducir("shadow", "copia sombra creada: %1"), {shadowRoot.string()}));
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
        BA_LOG_ERROR("shadow", 5, ba::i18n::llenar(ba::i18n::traducir("shadow", "no se pudo borrar la copia sombra: %1"), {p.string()}));
        return;
    }
    BA_LOG_INFO("shadow", 0, ba::i18n::llenar(ba::i18n::traducir("shadow", "copia sombra eliminada: %1"), {p.string()}));
}

std::wstring ShadowCopy::versionKey(const std::wstring& root) {
    // FNV-1a de 64 bits sobre (ruta relativa, tamaño, mtime) en orden estable.
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    };
    const fs::path rootPath = toPath(root);
    std::vector<fs::path> files;
    std::error_code ec;
    for (const char* sub : kArboles) {
        for (fs::recursive_directory_iterator it(rootPath / sub, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                files.push_back(it->path());
            }
        }
        ec.clear();
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        const std::string rel = f.lexically_relative(rootPath).generic_string();
        mix(rel.data(), rel.size());
        const std::uintmax_t size = fs::file_size(f, ec);
        mix(&size, sizeof(size));
        const auto mtime = fs::last_write_time(f, ec).time_since_epoch().count();
        mix(&mtime, sizeof(mtime));
    }
    wchar_t buf[17];
    std::swprintf(buf, 17, L"%016llx", static_cast<unsigned long long>(h));
    return std::wstring(L"v-") + buf;
}

} // namespace ba::launcher
