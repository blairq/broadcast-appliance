// ============================================================================
// src/app/escritorio/control_archivos.cpp — archivos, el registro y avisos de
// Windows (ver control.hpp)
//
//   system_archivos   leer (texto, o una imagen como imagen), escribir,
//                      listar, info, copiar, mover, borrar (SIEMPRE a la
//                      papelera: se puede deshacer), crear una carpeta
//   system_registro   leer y listar (permiso sistema); escribir y borrar un
//                      valor (permiso registro, apagado de fábrica)
//   system_aviso      un aviso de Windows (el globo del área de notificación)
//
// Las rutas aceptan variables (%USERPROFILE%\Desktop\x.txt); las relativas son
// desde la carpeta del usuario.
// ============================================================================
#include "escritorio/control.hpp"
#include "platform/Hilo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <thread>

#if !defined(_WIN32)
#include <fnmatch.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#endif
#if defined(_WIN32)
#include <shellapi.h>
#include <shlwapi.h>
#endif

namespace bactl {

json herramientasArchivos() {
    json t = json::array();
    t.push_back(herramienta(
        "system_archivos",
        ba::i18n::traducir("mcp", "Archivos y carpetas de esta máquina. `accion`:\n"
        "- leer `ruta`: el texto (desde la línea `desde`, `lineas` como mucho, 2000); una imagen (png, jpg, gif, bmp) "
        "vuelve como imagen.\n"
        "- escribir `ruta` `contenido` (`agregar`: al final); crea las carpetas que falten.\n"
        "- listar `ruta` (una carpeta; `patron` *.txt, `recursivo`, `ocultos`, `max` 500).\n"
        "- info `ruta`: tamaño, fechas, si es carpeta.\n"
        "- copiar / mover `ruta` → `destino` (`reemplazar`).\n"
        "- borrar `ruta`: a la PAPELERA (se puede recuperar).\n"
        "- crear_carpeta `ruta`.\n"
        "Rutas con variables (%USERPROFILE%\\Desktop en Windows; ~/Desktop o $HOME en macOS y Linux); las relativas, desde "
        "la carpeta del usuario. Permiso: archivos "
        "(leer, listar, info) y archivos_escribir (lo demás)."),
        esquema({{"accion", {{"type", "string"}, {"enum", {"leer", "escribir", "listar", "info", "copiar", "mover", "borrar", "crear_carpeta"}}}},
                 {"ruta", {{"type", "string"}}},
                 {"destino", {{"type", "string"}}},
                 {"contenido", {{"type", "string"}}},
                 {"agregar", {{"type", "boolean"}}},
                 {"reemplazar", {{"type", "boolean"}}},
                 {"desde", {{"type", "integer"}, {"minimum", 1}}},
                 {"lineas", {{"type", "integer"}, {"minimum", 1}}},
                 {"patron", {{"type", "string"}}},
                 {"recursivo", {{"type", "boolean"}}},
                 {"ocultos", {{"type", "boolean"}}},
                 {"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000}}}},
                {"accion", "ruta"})));
    t.push_back(herramienta(
        "system_registro",
        ba::i18n::traducir("mcp", "El registro de Windows; en macOS, las preferencias (`clave` = el dominio: com.apple.dock, NSGlobalDomain; "
        "`nombre` = la preferencia). En Windows `clave`: «HKCU\\Software\\…» (HKCU, HKLM, HKCR, HKU, HKCC). `accion`: leer (el valor "
        "`nombre`; vacío = el predeterminado), listar (subclaves y valores), escribir (`nombre`, `valor`, `tipo`: sz, "
        "expand_sz, multi_sz —una lista—, dword, qword), borrar (el valor `nombre`; nunca una clave entera). Permiso: "
        "sistema para leer y listar; registro para escribir y borrar (apagado de fábrica)."),
        esquema({{"accion", {{"type", "string"}, {"enum", {"leer", "listar", "escribir", "borrar"}}}},
                 {"clave", {{"type", "string"}}},
                 {"nombre", {{"type", "string"}}},
                 {"valor", json::object()},
                 {"tipo", {{"type", "string"}, {"enum", {"sz", "expand_sz", "multi_sz", "dword", "qword"}}}}},
                {"accion", "clave"})));
    t.push_back(herramienta(
        "system_aviso",
        ba::i18n::traducir("mcp", "Muestra un aviso del sistema (la notificación de Windows o de macOS): `titulo` y `texto`, `tipo`: info, aviso, error. Para "
        "avisarle algo a la persona que está en la máquina. Permiso: avisos."),
        esquema({{"titulo", {{"type", "string"}}}, {"texto", {{"type", "string"}}}, {"tipo", {{"type", "string"}, {"enum", {"info", "aviso", "error"}}}}},
                {"texto"})));
    return t;
}

#if defined(_WIN32)

namespace {

std::wstring ruta(const std::string& r) {
    std::wstring w = aW(r);
    wchar_t ex[4096];
    if (ExpandEnvironmentStringsW(w.c_str(), ex, 4096) > 0) w = ex;
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    if (PathIsRelativeW(w.c_str())) {
        wchar_t perfil[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", perfil, MAX_PATH) > 0) w = std::wstring(perfil) + L"\\" + w;
    }
    return w;
}

std::string fecha(const FILETIME& ft) {
    SYSTEMTIME u{}, l{};
    FileTimeToSystemTime(&ft, &u);
    SystemTimeToTzSpecificLocalTime(nullptr, &u, &l);
    char b[32];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", l.wYear, l.wMonth, l.wDay, l.wHour, l.wMinute, l.wSecond);
    return b;
}

std::string error(DWORD e) {
    wchar_t* m = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0,
                   reinterpret_cast<wchar_t*>(&m), 0, nullptr);
    std::string s = m != nullptr ? aU(m) : ba::i18n::llenar(ba::i18n::traducir("mcp", "error %1"), {std::to_string(e)});
    if (m) LocalFree(m);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s + " (" + std::to_string(e) + ")";
}

bool esUtf8(const std::string& s) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0 || s.empty();
}

std::string desdeAnsi(const std::string& s) {
    const int n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return aU(w);
}

bool leerTodo(const std::wstring& r, std::string& out, size_t max, bool& cortado) {
    HANDLE h = CreateFileW(r.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER t{};
    GetFileSizeEx(h, &t);
    const size_t n = static_cast<size_t>(std::min<long long>(t.QuadPart, static_cast<long long>(max)));
    cortado = static_cast<unsigned long long>(t.QuadPart) > max;
    out.resize(n);
    DWORD leidos = 0;
    const bool ok = n == 0 || ReadFile(h, out.data(), static_cast<DWORD>(n), &leidos, nullptr);
    out.resize(leidos);
    CloseHandle(h);
    return ok;
}

json archivos(const json& id, const json& a, const Permisos& p) {
    const std::string accion = a.value("accion", std::string());
    const bool escribe = accion != "leer" && accion != "listar" && accion != "info";
    if (escribe ? !p.archivos_escribir : !p.archivos) return sinPermiso(id, escribe ? "archivos_escribir" : "archivos");
    const std::wstring r = ruta(a.value("ruta", std::string()));
    const std::string ru = aU(r);
    if (r.empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta `ruta`"), true);

    if (accion == "info") {
        WIN32_FILE_ATTRIBUTE_DATA d{};
        if (!GetFileAttributesExW(r.c_str(), GetFileExInfoStandard, &d)) return resultadoTexto(id, ru + ": " + error(GetLastError()), true);
        const bool carpeta = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        json j{{"ruta", ru}, {"carpeta", carpeta}, {"creado", fecha(d.ftCreationTime)}, {"modificado", fecha(d.ftLastWriteTime)},
               {"oculto", (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0}, {"solo_lectura", (d.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0}};
        if (!carpeta) j["bytes"] = (static_cast<unsigned long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
        return resultadoDatos(id, j);
    }

    if (accion == "leer") {
        std::wstring ext = PathFindExtensionW(r.c_str());
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        // Una imagen: como imagen (hasta 8 MB).
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".bmp" || ext == L".webp") {
            std::string datos;
            bool cortado = false;
            if (!leerTodo(r, datos, 8u << 20, cortado)) return resultadoTexto(id, ru + ": " + error(GetLastError()), true);
            if (cortado) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: imagen de más de 8 MB"), {ru}), true);
            const std::string mime = ext == L".png" ? "image/png" : ext == L".gif" ? "image/gif" : ext == L".bmp" ? "image/bmp"
                                     : ext == L".webp" ? "image/webp" : "image/jpeg";
            json res = resultadoImagen(id, base64(reinterpret_cast<const unsigned char*>(datos.data()), datos.size()), ru);
            res["result"]["content"].back()["mimeType"] = mime;
            return res;
        }
        std::string datos;
        bool cortado = false;
        if (!leerTodo(r, datos, 16u << 20, cortado)) return resultadoTexto(id, ru + ": " + error(GetLastError()), true);
        if (datos.find('\0') != std::string::npos && !(datos.size() >= 2 && static_cast<unsigned char>(datos[0]) == 0xFF))
            return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 es binario (%2 bytes): no se muestra como texto"),
                                                   {ru, std::to_string(datos.size())}), true);
        // UTF-16 con BOM, UTF-8 (con o sin BOM) o la página de Windows.
        if (datos.size() >= 2 && static_cast<unsigned char>(datos[0]) == 0xFF && static_cast<unsigned char>(datos[1]) == 0xFE)
            datos = aU(std::wstring(reinterpret_cast<const wchar_t*>(datos.data() + 2), (datos.size() - 2) / 2));
        else if (datos.size() >= 3 && datos.compare(0, 3, "\xEF\xBB\xBF") == 0)
            datos.erase(0, 3);
        else if (!esUtf8(datos))
            datos = desdeAnsi(datos);
        const int desde = std::max(1, a.value("desde", 1)), lineas = std::max(1, a.value("lineas", 2000));
        std::string out;
        int linea = 1, puestas = 0, total = 1;
        size_t i = 0;
        while (i <= datos.size()) {
            const size_t fin = datos.find('\n', i);
            const size_t f = fin == std::string::npos ? datos.size() : fin;
            if (linea >= desde && puestas < lineas) {
                std::string l = datos.substr(i, f - i);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                out += l + "\n";
                ++puestas;
            }
            if (fin == std::string::npos) break;
            i = fin + 1;
            ++linea;
            ++total;
        }
        json d{{"ruta", ru}, {"texto", out}, {"desde", desde}, {"lineas", puestas}, {"lineas_total", total}};
        if (desde + puestas - 1 < total) d["nota"] = ba::i18n::llenar(ba::i18n::traducir("mcp", "hay más: `desde` %1"), {std::to_string(desde + puestas)});
        if (cortado) d["nota_tamano"] = ba::i18n::traducir("mcp", "el archivo pasa de 16 MB: se leyó el principio");
        return resultadoDatos(id, d);
    }

    if (accion == "listar") {
        const std::wstring patron = aW(a.value("patron", std::string("*")));
        const bool rec = a.value("recursivo", false), ocultos = a.value("ocultos", false);
        const size_t max = static_cast<size_t>(std::clamp(a.value("max", 500), 1, 5000));
        json l = json::array();
        size_t total = 0;
        std::vector<std::wstring> pend{r};
        while (!pend.empty()) {
            const std::wstring dir = pend.back();
            pend.pop_back();
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) {
                if (dir == r) return resultadoTexto(id, ru + ": " + error(GetLastError()), true);
                continue;
            }
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                if (!ocultos && (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
                const bool carpeta = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                const std::wstring completo = dir + L"\\" + fd.cFileName;
                if (rec && carpeta && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pend.push_back(completo);
                if (!PathMatchSpecW(fd.cFileName, patron.c_str())) continue;
                ++total;
                if (l.size() >= max) continue;
                json e{{"nombre", aU(rec ? completo.substr(r.size() + 1) : std::wstring(fd.cFileName))}, {"carpeta", carpeta},
                       {"modificado", fecha(fd.ftLastWriteTime)}};
                if (!carpeta) e["bytes"] = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                l.push_back(e);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        json d{{"carpeta", ru}, {"elementos", l}, {"total", total}};
        if (total > l.size()) d["nota"] = ba::i18n::llenar(ba::i18n::traducir("mcp", "hay %1 más (`max`, `patron`)"), {std::to_string(total - l.size())});
        return resultadoDatos(id, d);
    }

    if (accion == "escribir") {
        // Las carpetas que falten.
        std::wstring dir = r.substr(0, r.find_last_of(L'\\'));
        if (!dir.empty()) {
            for (size_t k = 3; k <= dir.size(); ++k)
                if (k == dir.size() || dir[k] == L'\\') CreateDirectoryW(dir.substr(0, k).c_str(), nullptr);
        }
        const bool agregar = a.value("agregar", false);
        HANDLE h = CreateFileW(r.c_str(), agregar ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               agregar ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return resultadoTexto(id, ru + ": " + error(GetLastError()), true);
        const std::string c = a.value("contenido", std::string());
        DWORD escritos = 0;
        const bool ok = c.empty() || WriteFile(h, c.data(), static_cast<DWORD>(c.size()), &escritos, nullptr);
        const DWORD e = GetLastError();
        CloseHandle(h);
        if (!ok) return resultadoTexto(id, ru + ": " + error(e), true);
        return resultadoTexto(id, ba::i18n::llenar(agregar ? ba::i18n::traducir("mcp", "Agregados %1 bytes en %2.")
                                                     : ba::i18n::traducir("mcp", "Escritos %1 bytes en %2."),
                                             {std::to_string(c.size()), ru}));
    }

    if (accion == "crear_carpeta") {
        for (size_t k = 3; k <= r.size(); ++k)
            if (k == r.size() || r[k] == L'\\') CreateDirectoryW(r.substr(0, k).c_str(), nullptr);
        if (!PathIsDirectoryW(r.c_str())) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo crear %1"), {ru}), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Carpeta %1 lista."), {ru}));
    }

    if (accion == "copiar" || accion == "mover") {
        const std::wstring d = ruta(a.value("destino", std::string()));
        if (a.value("destino", std::string()).empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta `destino`"), true);
        const bool reemplazar = a.value("reemplazar", false);
        // SHFileOperation: archivos y carpetas enteras; sin preguntar ni mostrar nada.
        std::wstring de = r + L'\0', a2 = d + L'\0';
        SHFILEOPSTRUCTW op{};
        op.wFunc = accion == "copiar" ? FO_COPY : FO_MOVE;
        op.pFrom = de.c_str();
        op.pTo = a2.c_str();
        op.fFlags = FOF_NOCONFIRMMKDIR | FOF_SILENT | FOF_NOERRORUI | (reemplazar ? FOF_NOCONFIRMATION : FOF_RENAMEONCOLLISION);
        const int e = SHFileOperationW(&op);
        if (e != 0 || op.fAnyOperationsAborted) return resultadoTexto(id, ba::i18n::llenar(accion == "copiar" ? ba::i18n::traducir("mcp", "no se pudo copiar %1 (código %2)")
                                                                                      : ba::i18n::traducir("mcp", "no se pudo mover %1 (código %2)"),
                                                                    {ru, std::to_string(e)}), true);
        return resultadoTexto(id, ba::i18n::llenar(accion == "copiar" ? ba::i18n::traducir("mcp", "Copiado %1 → %2.")
                                                                 : ba::i18n::traducir("mcp", "Movido %1 → %2."),
                                             {ru, aU(d)}));
    }

    if (accion == "borrar") {
        if (GetFileAttributesW(r.c_str()) == INVALID_FILE_ATTRIBUTES) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no existe"), {ru}), true);
        // A la papelera, siempre (FOF_ALLOWUNDO): nada se borra para siempre desde acá.
        std::wstring de = r + L'\0';
        SHFILEOPSTRUCTW op{};
        op.wFunc = FO_DELETE;
        op.pFrom = de.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        const int e = SHFileOperationW(&op);
        if (e != 0 || op.fAnyOperationsAborted) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo borrar %1 (código %2)"), {ru, std::to_string(e)}), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 fue a la papelera (se puede recuperar)."), {ru}));
    }
    return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "acción desconocida: %1"), {accion}), true);
}

// --- Registro ------------------------------------------------------------------------

bool clave(const std::string& c, HKEY& raiz, std::wstring& sub) {
    const auto barra = c.find('\\');
    std::string r = c.substr(0, barra);
    for (auto& x : r) x = static_cast<char>(toupper(static_cast<unsigned char>(x)));
    sub = barra == std::string::npos ? std::wstring() : aW(c.substr(barra + 1));
    if (r == "HKCU" || r == "HKEY_CURRENT_USER") raiz = HKEY_CURRENT_USER;
    else if (r == "HKLM" || r == "HKEY_LOCAL_MACHINE") raiz = HKEY_LOCAL_MACHINE;
    else if (r == "HKCR" || r == "HKEY_CLASSES_ROOT") raiz = HKEY_CLASSES_ROOT;
    else if (r == "HKU" || r == "HKEY_USERS") raiz = HKEY_USERS;
    else if (r == "HKCC" || r == "HKEY_CURRENT_CONFIG") raiz = HKEY_CURRENT_CONFIG;
    else return false;
    return true;
}

const char* tipoReg(DWORD t) {
    switch (t) {
        case REG_SZ: return "sz";
        case REG_EXPAND_SZ: return "expand_sz";
        case REG_MULTI_SZ: return "multi_sz";
        case REG_DWORD: return "dword";
        case REG_QWORD: return "qword";
        case REG_BINARY: return "binario";
        default: return "otro";
    }
}

json valorReg(DWORD t, const std::vector<BYTE>& d) {
    if (t == REG_SZ || t == REG_EXPAND_SZ) {
        std::wstring w(reinterpret_cast<const wchar_t*>(d.data()), d.size() / sizeof(wchar_t));
        while (!w.empty() && w.back() == L'\0') w.pop_back();
        return aU(w);
    }
    if (t == REG_MULTI_SZ) {
        json l = json::array();
        const auto* w = reinterpret_cast<const wchar_t*>(d.data());
        const size_t n = d.size() / sizeof(wchar_t);
        for (size_t i = 0; i < n && w[i];) {
            std::wstring s(w + i);
            l.push_back(aU(s));
            i += s.size() + 1;
        }
        return l;
    }
    if (t == REG_DWORD && d.size() >= 4) return *reinterpret_cast<const DWORD*>(d.data());
    if (t == REG_QWORD && d.size() >= 8) return *reinterpret_cast<const unsigned long long*>(d.data());
    std::string hex;
    for (size_t i = 0; i < std::min<size_t>(d.size(), 256); ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02x", d[i]);
        hex += b;
    }
    return hex + (d.size() > 256 ? "…" : "");
}

json registro(const json& id, const json& a, const Permisos& p) {
    const std::string accion = a.value("accion", std::string());
    const bool escribe = accion == "escribir" || accion == "borrar";
    if (escribe ? !p.registro : !p.sistema) return sinPermiso(id, escribe ? "registro" : "sistema");
    HKEY raiz;
    std::wstring sub;
    const std::string c = a.value("clave", std::string());
    if (!clave(c, raiz, sub)) return resultadoTexto(id, ba::i18n::traducir("mcp", "la clave empieza con HKCU, HKLM, HKCR, HKU o HKCC"), true);
    const std::wstring nombre = aW(a.value("nombre", std::string()));
    HKEY k = nullptr;
    LSTATUS e = escribe ? RegCreateKeyExW(raiz, sub.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr)
                        : RegOpenKeyExW(raiz, sub.c_str(), 0, KEY_READ, &k);
    if (e != ERROR_SUCCESS) return resultadoTexto(id, c + ": " + error(static_cast<DWORD>(e)), true);
    json out;
    if (accion == "leer") {
        DWORD t = 0, n = 0;
        e = RegQueryValueExW(k, nombre.c_str(), nullptr, &t, nullptr, &n);
        std::vector<BYTE> d(n);
        if (e == ERROR_SUCCESS) e = RegQueryValueExW(k, nombre.c_str(), nullptr, &t, d.data(), &n);
        out = e == ERROR_SUCCESS ? resultadoDatos(id, {{"clave", c}, {"nombre", aU(nombre)}, {"tipo", tipoReg(t)}, {"valor", valorReg(t, d)}})
                                 : resultadoTexto(id, c + " \\ " + aU(nombre) + ": " + error(static_cast<DWORD>(e)), true);
    } else if (accion == "listar") {
        json subs = json::array(), vals = json::array();
        wchar_t nb[256];
        for (DWORD i = 0;; ++i) {
            DWORD n = 256;
            if (RegEnumKeyExW(k, i, nb, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS || i >= 1000) break;
            subs.push_back(aU(std::wstring(nb, n)));
        }
        for (DWORD i = 0;; ++i) {
            wchar_t vn[16384];
            DWORD n = 16384, t = 0, dn = 0;
            if (RegEnumValueW(k, i, vn, &n, nullptr, &t, nullptr, &dn) != ERROR_SUCCESS || i >= 1000) break;
            std::vector<BYTE> d(dn);
            DWORD n2 = 16384;
            RegEnumValueW(k, i, vn, &n2, nullptr, &t, d.data(), &dn);
            vals.push_back({{"nombre", aU(std::wstring(vn, n2))}, {"tipo", tipoReg(t)}, {"valor", valorReg(t, d)}});
        }
        out = resultadoDatos(id, {{"clave", c}, {"subclaves", subs}, {"valores", vals}});
    } else if (accion == "escribir") {
        const std::string tipo = a.value("tipo", std::string("sz"));
        const json v = a.value("valor", json());
        std::vector<BYTE> d;
        DWORD t = REG_SZ;
        if (tipo == "dword" || tipo == "qword") {
            const unsigned long long x = v.is_number() ? v.get<unsigned long long>() : std::stoull(v.is_string() ? v.get<std::string>() : "0");
            t = tipo == "dword" ? REG_DWORD : REG_QWORD;
            d.resize(tipo == "dword" ? 4 : 8);
            if (tipo == "dword") *reinterpret_cast<DWORD*>(d.data()) = static_cast<DWORD>(x);
            else *reinterpret_cast<unsigned long long*>(d.data()) = x;
        } else if (tipo == "multi_sz") {
            t = REG_MULTI_SZ;
            std::wstring w;
            for (const auto& s : v.is_array() ? v : json::array({v})) w += aW(s.is_string() ? s.get<std::string>() : s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) + L'\0';
            w += L'\0';
            d.assign(reinterpret_cast<const BYTE*>(w.data()), reinterpret_cast<const BYTE*>(w.data() + w.size()));
        } else {
            t = tipo == "expand_sz" ? REG_EXPAND_SZ : REG_SZ;
            const std::wstring w = aW(v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) + L'\0';
            d.assign(reinterpret_cast<const BYTE*>(w.data()), reinterpret_cast<const BYTE*>(w.data() + w.size()));
        }
        e = RegSetValueExW(k, nombre.c_str(), 0, t, d.data(), static_cast<DWORD>(d.size()));
        out = e == ERROR_SUCCESS ? resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Escrito %1 \\ %2 (%3)."), {c, aU(nombre), tipo}))
                                 : resultadoTexto(id, error(static_cast<DWORD>(e)), true);
    } else if (accion == "borrar") {
        if (nombre.empty()) {
            out = resultadoTexto(id, ba::i18n::traducir("mcp", "falta `nombre`: se borra un valor, nunca una clave entera"), true);
        } else {
            e = RegDeleteValueW(k, nombre.c_str());
            out = e == ERROR_SUCCESS ? resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Borrado %1 \\ %2."), {c, aU(nombre)}))
                                     : resultadoTexto(id, error(static_cast<DWORD>(e)), true);
        }
    } else {
        out = resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "acción desconocida: %1"), {accion}), true);
    }
    RegCloseKey(k);
    return out;
}

// --- Aviso -----------------------------------------------------------------------------

json aviso(const json& id, const json& a, const Permisos& p) {
    if (!p.avisos) return sinPermiso(id, "avisos");
    // El globo del área de notificación (en Windows 10 y 11 sale como notificación):
    // una ventana propia invisible para el ícono, que se saca cuando se fue el aviso.
    // (Una sola, para siempre: este proceso no tiene bucle de mensajes que la cierre.)
    static HWND w = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BaControlAviso";
        RegisterClassW(&wc);
        return CreateWindowExW(0, L"BaControlAviso", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    }();
    static UINT siguiente = 1;
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = w;
    n.uID = siguiente++;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    n.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516)); // IDI_INFORMATION
    wcsncpy_s(n.szTip, L"Broadcast Appliance", _TRUNCATE);
    wcsncpy_s(n.szInfoTitle, aW(a.value("titulo", std::string("Broadcast Appliance"))).c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, aW(a.value("texto", std::string())).c_str(), _TRUNCATE);
    const std::string tipo = a.value("tipo", std::string("info"));
    n.dwInfoFlags = tipo == "error" ? NIIF_ERROR : tipo == "aviso" ? NIIF_WARNING : NIIF_INFO;
    const bool ok = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    // Se deja unos segundos y se saca el ícono (el aviso ya quedó en el centro de notificaciones).
    std::thread([n]() mutable {
        ba::platform::nombrarHilo("control-aviso");
        Sleep(8000);
        Shell_NotifyIconW(NIM_DELETE, &n);
    }).detach();
    return ok ? resultadoTexto(id, ba::i18n::traducir("mcp", "Aviso mostrado.")) : resultadoTexto(id, ba::i18n::traducir("mcp", "Windows no dejó mostrar el aviso"), true);
}

} // namespace

bool atenderArchivos(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out) {
    if (nombre == "system_archivos") return out = archivos(id, a, p), true;
    if (nombre == "system_registro") return out = registro(id, a, p), true;
    if (nombre == "system_aviso") return out = aviso(id, a, p), true;
    return false;
}

#else // macOS y Linux: lo mismo con std::filesystem (papelera, registro y aviso: del sistema)

namespace {

namespace fs = std::filesystem;

/// «~/x», «$HOME/x», relativas desde la carpeta del usuario (como %USERPROFILE% en Windows).
fs::path ruta(const std::string& r) {
    std::string s = r;
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "/";
    if (s.rfind("~/", 0) == 0 || s == "~") s = h + s.substr(1);
    for (const char* var : {"$HOME", "${HOME}", "%USERPROFILE%"})
        if (const auto i = s.find(var); i != std::string::npos) s.replace(i, std::string(var).size(), h);
    fs::path p(s);
    if (p.is_relative()) p = fs::path(h) / p;
    return p;
}

std::string fecha(fs::file_time_type t) {
    // Del reloj de los archivos al del sistema (clock_cast no está en todas las bibliotecas).
    const auto s = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(s);
    std::tm l{};
    localtime_r(&tt, &l);
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &l);
    return b;
}

bool coincide(const std::string& patron, const std::string& nombre) {
    return fnmatch(patron.c_str(), nombre.c_str(), FNM_CASEFOLD) == 0;
}

json archivos(const json& id, const json& a, const Permisos& p) {
    const std::string accion = a.value("accion", std::string());
    const bool escribe = accion != "leer" && accion != "listar" && accion != "info";
    if (escribe ? !p.archivos_escribir : !p.archivos) return sinPermiso(id, escribe ? "archivos_escribir" : "archivos");
    if (a.value("ruta", std::string()).empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta `ruta`"), true);
    const fs::path r = ruta(a.value("ruta", std::string()));
    const std::string ru = r.string();
    std::error_code ec;

    if (accion == "info") {
        const auto st = fs::status(r, ec);
        if (ec || !fs::exists(st)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no existe"), {ru}), true);
        const bool carpeta = fs::is_directory(st);
        json j{{"ruta", ru}, {"carpeta", carpeta}, {"modificado", fecha(fs::last_write_time(r, ec))},
               {"oculto", !r.filename().string().empty() && r.filename().string()[0] == '.'},
               {"solo_lectura", (st.permissions() & fs::perms::owner_write) == fs::perms::none}};
        if (!carpeta) j["bytes"] = static_cast<unsigned long long>(fs::file_size(r, ec));
        return resultadoDatos(id, j);
    }

    if (accion == "leer") {
        std::string ext = r.extension().string();
        for (char& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        std::ifstream f(r, std::ios::binary);
        if (!f) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no se pudo abrir"), {ru}), true);
        const auto tam = fs::file_size(r, ec);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp") {
            if (tam > (8u << 20)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: imagen de más de 8 MB"), {ru}), true);
            std::string datos((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const std::string mime = ext == ".png" ? "image/png" : ext == ".gif" ? "image/gif" : ext == ".bmp" ? "image/bmp"
                                     : ext == ".webp" ? "image/webp" : "image/jpeg";
            json res = resultadoImagen(id, base64(reinterpret_cast<const unsigned char*>(datos.data()), datos.size()), ru);
            res["result"]["content"].back()["mimeType"] = mime;
            return res;
        }
        const size_t max = 16u << 20;
        std::string datos(static_cast<size_t>(std::min<std::uintmax_t>(tam, max)), '\0');
        f.read(datos.data(), static_cast<std::streamsize>(datos.size()));
        datos.resize(static_cast<size_t>(f.gcount()));
        const bool cortado = tam > max;
        if (datos.find('\0') != std::string::npos) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 es binario (%2 bytes): no se muestra como texto"),
                                                   {ru, std::to_string(tam)}), true);
        if (datos.size() >= 3 && datos.compare(0, 3, "\xEF\xBB\xBF") == 0) datos.erase(0, 3);
        const int desde = std::max(1, a.value("desde", 1)), lineas = std::max(1, a.value("lineas", 2000));
        std::string out;
        int linea = 1, puestas = 0, total = 1;
        size_t i = 0;
        while (i <= datos.size()) {
            const size_t fin = datos.find('\n', i);
            const size_t ff = fin == std::string::npos ? datos.size() : fin;
            if (linea >= desde && puestas < lineas) {
                std::string l = datos.substr(i, ff - i);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                out += l + "\n";
                ++puestas;
            }
            if (fin == std::string::npos) break;
            i = fin + 1;
            ++linea;
            ++total;
        }
        json d{{"ruta", ru}, {"texto", out}, {"desde", desde}, {"lineas", puestas}, {"lineas_total", total}};
        if (desde + puestas - 1 < total) d["nota"] = ba::i18n::llenar(ba::i18n::traducir("mcp", "hay más: `desde` %1"), {std::to_string(desde + puestas)});
        if (cortado) d["nota_tamano"] = ba::i18n::traducir("mcp", "el archivo pasa de 16 MB: se leyó el principio");
        return resultadoDatos(id, d);
    }

    if (accion == "listar") {
        const std::string patron = a.value("patron", std::string("*"));
        const bool rec = a.value("recursivo", false), ocultos = a.value("ocultos", false);
        const size_t max = static_cast<size_t>(std::clamp(a.value("max", 500), 1, 5000));
        if (!fs::is_directory(r, ec)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no es una carpeta"), {ru}), true);
        json l = json::array();
        size_t total = 0;
        const auto ver = [&](const fs::directory_entry& e, const std::string& relativo) {
            const std::string n = e.path().filename().string();
            if (!coincide(patron, n)) return;
            ++total;
            if (l.size() >= max) return;
            const bool carpeta = e.is_directory(ec);
            json x{{"nombre", relativo}, {"carpeta", carpeta}, {"modificado", fecha(e.last_write_time(ec))}};
            if (!carpeta) x["bytes"] = static_cast<unsigned long long>(e.file_size(ec));
            l.push_back(x);
        };
        if (rec) {
            for (auto it = fs::recursive_directory_iterator(r, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                const std::string n = it->path().filename().string();
                if (!ocultos && !n.empty() && n[0] == '.') {
                    if (it->is_directory(ec)) it.disable_recursion_pending();
                    continue;
                }
                ver(*it, fs::relative(it->path(), r, ec).string());
            }
        } else {
            for (const auto& e : fs::directory_iterator(r, fs::directory_options::skip_permission_denied, ec)) {
                const std::string n = e.path().filename().string();
                if (!ocultos && !n.empty() && n[0] == '.') continue;
                ver(e, n);
            }
        }
        json d{{"carpeta", ru}, {"elementos", l}, {"total", total}};
        if (total > l.size()) d["nota"] = ba::i18n::llenar(ba::i18n::traducir("mcp", "hay %1 más (`max`, `patron`)"), {std::to_string(total - l.size())});
        return resultadoDatos(id, d);
    }

    if (accion == "escribir") {
        fs::create_directories(r.parent_path(), ec);
        const bool agregar = a.value("agregar", false);
        std::ofstream f(r, std::ios::binary | (agregar ? std::ios::app : std::ios::trunc));
        if (!f) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no se pudo escribir"), {ru}), true);
        const std::string c = a.value("contenido", std::string());
        f << c;
        if (!f) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no se pudo escribir"), {ru}), true);
        return resultadoTexto(id, ba::i18n::llenar(agregar ? ba::i18n::traducir("mcp", "Agregados %1 bytes en %2.")
                                                     : ba::i18n::traducir("mcp", "Escritos %1 bytes en %2."),
                                             {std::to_string(c.size()), ru}));
    }

    if (accion == "crear_carpeta") {
        fs::create_directories(r, ec);
        if (!fs::is_directory(r, ec)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo crear %1"), {ru}), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Carpeta %1 lista."), {ru}));
    }

    if (accion == "copiar" || accion == "mover") {
        if (a.value("destino", std::string()).empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta `destino`"), true);
        fs::path d = ruta(a.value("destino", std::string()));
        if (!fs::exists(r, ec)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no existe"), {ru}), true);
        if (fs::is_directory(d, ec)) d /= r.filename();
        const bool reemplazar = a.value("reemplazar", false);
        if (fs::exists(d, ec) && !reemplazar) {
            // Como Windows: con otro nombre al lado en vez de pisar.
            const std::string base = d.stem().string(), ext = d.extension().string();
            for (int i = 2; fs::exists(d, ec); ++i) d = d.parent_path() / (base + " (" + std::to_string(i) + ")" + ext);
        }
        fs::create_directories(d.parent_path(), ec);
        if (accion == "mover") {
            fs::rename(r, d, ec);
            if (ec) { // otro disco: copiar y borrar
                ec.clear();
                fs::copy(r, d, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (!ec) fs::remove_all(r, ec);
            }
        } else
            fs::copy(r, d, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) return resultadoTexto(id, ba::i18n::llenar(accion == "copiar" ? ba::i18n::traducir("mcp", "no se pudo copiar %1: %2")
                                                                   : ba::i18n::traducir("mcp", "no se pudo mover %1: %2"),
                                                   {ru, ec.message()}), true);
        return resultadoTexto(id, ba::i18n::llenar(accion == "copiar" ? ba::i18n::traducir("mcp", "Copiado %1 → %2.")
                                                                 : ba::i18n::traducir("mcp", "Movido %1 → %2."),
                                             {ru, d.string()}));
    }

    if (accion == "borrar") {
        if (!fs::exists(r, ec)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: no existe"), {ru}), true);
        // A la papelera, siempre: nada se borra para siempre desde acá.
        std::string e;
        if (!aLaPapelera(ru, e)) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo mandar a la papelera %1: %2"), {ru, e}), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 fue a la papelera (se puede recuperar)."), {ru}));
    }
    return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "acción desconocida: %1"), {accion}), true);
}

} // namespace

bool atenderArchivos(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out) {
    if (nombre == "system_archivos") return out = archivos(id, a, p), true;
    if (nombre == "system_registro") return out = registroSistema(id, a, p), true;
    if (nombre == "system_aviso") return out = avisoSistema(id, a, p), true;
    return false;
}

#endif

} // namespace bactl
