// test_shadow_copy.cpp
//
// Pruebas unitarias de la copia sombra (spec 3.1.5, decision D3). Sin framework
// externo: es un main() que devuelve 0 si todo pasa y 1 si algo falla (lo
// registra CTest). Compila y corre en Linux y Windows.
//
// Casos (spec de la tarea):
//   1. Se arma una raiz falsa en un directorio temporal con bin/x.dll y
//      data/gst/z.bin (archivos pequenos).
//   2. prepare(root, "abc123") -> los archivos existen bajo la raiz sombra
//      devuelta y el marker ".shadow_ok" existe.
//   3. prepare de nuevo con el mismo hash -> devuelve la MISMA ruta (reuso) y
//      sigue siendo valida.
//   4. cleanup(shadowRoot) -> shadowRoot ya no existe.

#include "ShadowCopy.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using ba::launcher::ShadowCopy;

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

// Convierte std::wstring <-> fs::path de forma portable (mismo criterio que
// ShadowCopy.cpp: cast byte a byte en Linux, nativo en Windows).
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

// Crea un archivo pequeno con contenido determinista.
void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

// Raiz temporal unica para no pisar otras copias sombra ni tests paralelos.
fs::path makeFakeRoot() {
    const fs::path base = fs::temp_directory_path() / "ba_shadow_test";
    fs::remove_all(base);
    writeFile(base / "bin" / "x.dll", "fake-x-dll");
    writeFile(base / "data" / "gst" / "z.bin", "fake-z-bin");
#if !defined(_WIN32)
    // Cadena de biblioteca versionada (Linux/macOS): libq.so.6 -> libq.so.6.6.3.
    // La copia tiene que llevarse el symlink: sin el, el enlazador del hijo
    // cae a la biblioteca del SISTEMA.
    writeFile(base / "bin" / "qt" / "lib" / "libq.so.6.6.3", "fake-q-lib");
    fs::create_symlink("libq.so.6.6.3", base / "bin" / "qt" / "lib" / "libq.so.6");
#endif
    // Un arbol que NO debe copiarse (spec 3.1.5): config se queda en el disco.
    writeFile(base / "Config" / "user.ini", "fake-config");
    return base;
}

} // namespace

int main() {
    const fs::path rootPath = makeFakeRoot();
    const std::wstring root = toString(rootPath);

    // 2) prepare() -> los archivos viajan y el marker existe.
    const std::wstring shadow = ShadowCopy::prepare(root, L"abc123");
    const fs::path shadowPath = toPath(shadow);
    CHECK(fs::exists(shadowPath / "bin" / "x.dll"));
    CHECK(fs::exists(shadowPath / "data" / "gst" / "z.bin"));
    CHECK(fs::exists(shadowPath / ".shadow_ok"));
#if !defined(_WIN32)
    const fs::path link = shadowPath / "bin" / "qt" / "lib" / "libq.so.6";
    CHECK(fs::is_symlink(link));
    CHECK(fs::read_symlink(link) == fs::path("libq.so.6.6.3")); // relativo: apunta DENTRO de la copia
    CHECK(fs::exists(link));                                    // y resuelve
#endif
    // config/ no viaja (spec 3.1.5).
    CHECK(!fs::exists(shadowPath / "Config" / "user.ini"));

    // 3) prepare() de nuevo con el mismo hash -> MISMA ruta (reuso) y valida.
    const std::wstring shadow2 = ShadowCopy::prepare(root, L"abc123");
    CHECK(shadow2 == shadow);
    CHECK(fs::exists(shadowPath / "bin" / "x.dll"));
    CHECK(fs::exists(shadowPath / ".shadow_ok"));

    // 4) cleanup() -> shadowRoot desaparece.
    ShadowCopy::cleanup(shadow);
    CHECK(!fs::exists(shadowPath));

    // Limpieza de la raiz falsa del test.
    fs::remove_all(rootPath);

    if (g_failures == 0) {
        std::printf("OK: test_shadow_copy\n");
        return 0;
    }
    std::printf("FALLOS: %d\n", g_failures);
    return 1;
}
