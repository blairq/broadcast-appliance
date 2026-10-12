// ============================================================================
// src/motor/Capturas.cpp — ver Capturas.hpp (Windows y Linux; la Mac en Capturas_mac.mm)
// ============================================================================
#include "Capturas.hpp"

#include "Piezas.hpp" // hayFabrica

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#include <unistd.h>
#if __has_include(<xcb/xcb.h>)
#include <xcb/xcb.h>
#define BA_CON_XCB 1
#endif
#endif

namespace ba::motor {

using json = nlohmann::json;

namespace {

[[maybe_unused]] std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

[[maybe_unused]] std::string sinComillas(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
    return s;
}

} // namespace

/// La ventana viva de `app` con `titulo`: el mismo título y la misma aplicación; si
/// no, la primera de esa aplicación; si no, una de otra aplicación con ese título.
bool buscarVentanaCapturable(const std::string& app, const std::string& titulo, VentanaCapturable& out) {
    const auto lista = ventanasCapturables();
    const std::string a = minusculas(app);
    const VentanaCapturable* deLaApp = nullptr;
    const VentanaCapturable* conTitulo = nullptr;
    for (const auto& v : lista) {
        const bool mismaApp = !a.empty() && minusculas(v.app) == a;
        if (mismaApp && v.titulo == titulo) {
            out = v;
            return true;
        }
        if (mismaApp && deLaApp == nullptr) deLaApp = &v;
        if (!titulo.empty() && v.titulo == titulo && conTitulo == nullptr) conTitulo = &v;
    }
    if (deLaApp != nullptr) {
        out = *deLaApp;
        return true;
    }
    if (conTitulo != nullptr) {
        out = *conTitulo;
        return true;
    }
    return false;
}

#if defined(_WIN32)

namespace {
std::wstring aW(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
std::string aU(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::string ejecutableDe(DWORD pid) {
    std::string out;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t ruta[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, ruta, &n)) {
            std::wstring r(ruta, n);
            const auto barra = r.find_last_of(L"\\/");
            out = aU(barra == std::wstring::npos ? r : r.substr(barra + 1));
        }
        CloseHandle(h);
    }
    return out;
}
const char* fabricaPantalla() {
    return hayFabrica("d3d12screencapturesrc") ? "d3d12screencapturesrc" : "d3d11screencapturesrc";
}
} // namespace

std::vector<VentanaCapturable> ventanasCapturables() {
    std::vector<VentanaCapturable> out;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
            if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
            // Las «encubiertas» (apps de la tienda suspendidas, otras pantallas virtuales).
            DWORD encubierta = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &encubierta, sizeof(encubierta))) && encubierta) return TRUE;
            wchar_t t[512];
            const int n = GetWindowTextW(h, t, 512);
            if (n == 0) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            RECT r{};
            GetWindowRect(h, &r);
            if (r.right - r.left < 60 || r.bottom - r.top < 40) return TRUE;
            VentanaCapturable v;
            v.handle = reinterpret_cast<std::uint64_t>(h);
            v.titulo = aU(std::wstring(t, n));
            v.pid = static_cast<int>(pid);
            v.app = ejecutableDe(pid);
            v.ancho = r.right - r.left;
            v.alto = r.bottom - r.top;
            if (v.titulo == "Program Manager") return TRUE;
            reinterpret_cast<std::vector<VentanaCapturable>*>(lp)->push_back(v);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

bool hayCapturaDeVentana(std::string* porque) {
    if (hayFabrica("d3d12screencapturesrc") || hayFabrica("d3d11screencapturesrc")) return true;
    if (porque) *porque = "falta d3d12screencapturesrc";
    return false;
}

std::string origenPantalla(const json& a) {
    const std::string cursor = a.value("cursor", true) ? "true" : "false";
    std::string sel;
    // El HMONITOR de ahora: por el nombre de Windows («\\.\DISPLAY2») si es ese; si no
    // (Qt da el modelo: «DELL U2913WM»), por su recuadro en píxeles; si no, por el tamaño.
    struct Monitor {
        HMONITOR h;
        std::wstring nombre;
        RECT r;
    };
    std::vector<Monitor> ms;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hm, &mi)) reinterpret_cast<std::vector<Monitor>*>(lp)->push_back({hm, mi.szDevice, mi.rcMonitor});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ms));
    const std::wstring nombre = aW(a.value("monitor", std::string()));
    const json px = a.value("px", json());
    const bool conRect = px.is_array() && px.size() == 4;
    const auto cerca = [](long x, int y) { return std::abs(static_cast<int>(x) - y) <= 2; };
    HMONITOR elegido = nullptr;
    for (const auto& m : ms)
        if (!nombre.empty() && _wcsicmp(m.nombre.c_str(), nombre.c_str()) == 0) elegido = m.h;
    if (elegido == nullptr && conRect)
        for (const auto& m : ms)
            if (cerca(m.r.left, px[0].get<int>()) && cerca(m.r.top, px[1].get<int>()) &&
                cerca(m.r.right - m.r.left, px[2].get<int>()) && cerca(m.r.bottom - m.r.top, px[3].get<int>()))
                elegido = m.h;
    if (elegido == nullptr && conRect) {
        int iguales = 0;
        for (const auto& m : ms)
            if (cerca(m.r.right - m.r.left, px[2].get<int>()) && cerca(m.r.bottom - m.r.top, px[3].get<int>())) elegido = m.h, ++iguales;
        if (iguales > 1) elegido = nullptr; // dos del mismo tamaño: no se adivina
    }
    if (elegido != nullptr) sel = " monitor-handle=" + std::to_string(reinterpret_cast<std::uint64_t>(elegido));
    if (sel.empty()) sel = " monitor-index=" + std::to_string(a.value("indice", -1)); // -1: el principal
    return std::string(fabricaPantalla()) + sel + " show-cursor=" + cursor;
}

std::string origenVentana(const std::string& app, const std::string& titulo, bool cursor) {
    VentanaCapturable v;
    if (!buscarVentanaCapturable(app, titulo, v)) return {};
    // Windows Graphics Capture (la única que captura una ventana sola, aunque esté tapada).
    return std::string(fabricaPantalla()) + " capture-api=wgc window-handle=" + std::to_string(v.handle) +
           " show-border=false show-cursor=" + (cursor ? "true" : "false");
}

#elif !defined(__APPLE__)

// --- Linux (X11) ---------------------------------------------------------------------

#if defined(BA_CON_XCB)
namespace {
/// libxcb a pedido (dlopen): es del servidor gráfico del sistema, no viaja con el programa.
struct Xcb {
    void* lib = nullptr;
    decltype(&xcb_connect) connect = nullptr;
    decltype(&xcb_disconnect) disconnect = nullptr;
    decltype(&xcb_connection_has_error) error = nullptr;
    decltype(&xcb_get_setup) setup = nullptr;
    decltype(&xcb_setup_roots_iterator) raices = nullptr;
    decltype(&xcb_intern_atom) atomo = nullptr;
    decltype(&xcb_intern_atom_reply) atomoR = nullptr;
    decltype(&xcb_get_property) prop = nullptr;
    decltype(&xcb_get_property_reply) propR = nullptr;
    decltype(&xcb_get_property_value) valor = nullptr;
    decltype(&xcb_get_property_value_length) largo = nullptr;
    decltype(&xcb_get_geometry) geo = nullptr;
    decltype(&xcb_get_geometry_reply) geoR = nullptr;
    bool ok = false;
    Xcb() {
        lib = dlopen("libxcb.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (lib == nullptr) return;
        const auto f = [this](auto& p, const char* n) { p = reinterpret_cast<std::remove_reference_t<decltype(p)>>(dlsym(lib, n)); return p != nullptr; };
        ok = f(connect, "xcb_connect") && f(disconnect, "xcb_disconnect") && f(error, "xcb_connection_has_error") &&
             f(setup, "xcb_get_setup") && f(raices, "xcb_setup_roots_iterator") && f(atomo, "xcb_intern_atom") &&
             f(atomoR, "xcb_intern_atom_reply") && f(prop, "xcb_get_property") && f(propR, "xcb_get_property_reply") &&
             f(valor, "xcb_get_property_value") && f(largo, "xcb_get_property_value_length") && f(geo, "xcb_get_geometry") &&
             f(geoR, "xcb_get_geometry_reply");
    }
    static Xcb& la() {
        static Xcb x;
        return x;
    }
};
} // namespace
#endif

std::vector<VentanaCapturable> ventanasCapturables() {
    std::vector<VentanaCapturable> out;
#if defined(BA_CON_XCB)
    if (std::getenv("DISPLAY") == nullptr) return out;
    Xcb& x = Xcb::la();
    if (!x.ok) return out;
    xcb_connection_t* c = x.connect(nullptr, nullptr);
    if (c == nullptr || x.error(c)) {
        if (c) x.disconnect(c);
        return out;
    }
    const xcb_window_t raiz = x.raices(x.setup(c)).data->root;
    const auto atomo = [&](const char* n) {
        xcb_intern_atom_reply_t* r = x.atomoR(c, x.atomo(c, 0, static_cast<uint16_t>(strlen(n)), n), nullptr);
        const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
        free(r);
        return a;
    };
    const xcb_atom_t lista = atomo("_NET_CLIENT_LIST"), nombre = atomo("_NET_WM_NAME"), utf8 = atomo("UTF8_STRING"),
                     pidA = atomo("_NET_WM_PID");
    const auto texto = [&](xcb_window_t w, xcb_atom_t prop, xcb_atom_t tipo) {
        std::string s;
        if (xcb_get_property_reply_t* r = x.propR(c, x.prop(c, 0, w, prop, tipo, 0, 1024), nullptr)) {
            s.assign(static_cast<const char*>(x.valor(r)), static_cast<size_t>(x.largo(r)));
            free(r);
        }
        return s;
    };
    if (xcb_get_property_reply_t* r = x.propR(c, x.prop(c, 0, raiz, lista, XCB_ATOM_WINDOW, 0, 4096), nullptr)) {
        const auto* ws = static_cast<const xcb_window_t*>(x.valor(r));
        const int n = x.largo(r) / static_cast<int>(sizeof(xcb_window_t));
        for (int i = 0; i < n; ++i) {
            VentanaCapturable v;
            v.handle = ws[i];
            v.titulo = texto(ws[i], nombre, utf8);
            if (v.titulo.empty()) v.titulo = texto(ws[i], XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
            if (v.titulo.empty()) continue;
            // WM_CLASS: «instancia\0Clase\0»: la clase es la aplicación.
            const std::string clase = texto(ws[i], XCB_ATOM_WM_CLASS, XCB_ATOM_STRING);
            const auto cero = clase.find('\0');
            v.app = cero != std::string::npos ? std::string(clase.c_str() + cero + 1) : clase;
            if (xcb_get_property_reply_t* p = x.propR(c, x.prop(c, 0, ws[i], pidA, XCB_ATOM_CARDINAL, 0, 1), nullptr)) {
                if (x.largo(p) >= 4) v.pid = static_cast<int>(*static_cast<const uint32_t*>(x.valor(p)));
                free(p);
            }
            if (v.pid == static_cast<int>(getpid())) continue;
            if (xcb_get_geometry_reply_t* g = x.geoR(c, x.geo(c, ws[i]), nullptr)) {
                v.ancho = g->width;
                v.alto = g->height;
                free(g);
            }
            out.push_back(v);
        }
        free(r);
    }
    x.disconnect(c);
#endif
    return out;
}

bool hayCapturaDeVentana(std::string* porque) {
#if defined(BA_CON_XCB)
    if (std::getenv("DISPLAY") != nullptr && hayFabrica("ximagesrc") && Xcb::la().ok) return true;
#endif
    if (porque) *porque = std::getenv("WAYLAND_DISPLAY") != nullptr && std::getenv("DISPLAY") == nullptr
                              ? "en Wayland la ventana se elige al capturar la pantalla (el portal del sistema)"
                              : "hace falta X11 (ximagesrc)";
    return false;
}

std::string origenPantalla(const json& a) {
    const std::string cursor = a.value("cursor", true) ? "true" : "false";
    const bool x11 = std::getenv("DISPLAY") != nullptr && hayFabrica("ximagesrc");
    if (!x11 && hayFabrica("pipewiresrc")) return "pipewiresrc"; // Wayland: elige el portal
    std::string d = "ximagesrc use-damage=false show-pointer=" + cursor;
    // X11: todos los monitores son una sola pantalla; el monitor es su recuadro.
    const json px = a.value("px", json());
    if (px.is_array() && px.size() == 4) {
        const int x = px[0].get<int>(), y = px[1].get<int>(), w = px[2].get<int>(), h = px[3].get<int>();
        if (w > 0 && h > 0)
            d += " startx=" + std::to_string(std::max(0, x)) + " starty=" + std::to_string(std::max(0, y)) +
                 " endx=" + std::to_string(std::max(0, x) + w - 1) + " endy=" + std::to_string(std::max(0, y) + h - 1);
    }
    return d;
}

std::string origenVentana(const std::string& app, const std::string& titulo, bool cursor) {
    VentanaCapturable v;
    if (!buscarVentanaCapturable(app, titulo, v)) return {};
    return "ximagesrc use-damage=false xid=" + std::to_string(v.handle) + " show-pointer=" + (cursor ? "true" : "false");
}

#endif

} // namespace ba::motor
