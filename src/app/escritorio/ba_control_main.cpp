// ============================================================================
// src/app/escritorio/ba_control_main.cpp — ba-control: control del escritorio de
// Windows como servidor MCP, para los asistentes del aparato.
//
// QUÉ ES. Un servidor MCP (stdio, JSON-RPC 2.0) aparte del de la emisión
// (`broadcast`): le da al asistente del operador manejo de ESTA máquina —mouse,
// teclado, captura de pantalla, abrir programas (y como administrador), buscar
// archivos, listar y activar ventanas, portapapeles—. Lo lanzan los mismos
// clientes que el puente MCP (Claude, Gemini, opencode) como un segundo
// servidor; sus herramientas quedan bajo `mcp__control`.
//
// PARA QUÉ. El puesto es una PC Windows que el operador no siempre atiende: el
// asistente tiene que poder abrir una app que faltó, acomodar una ventana,
// sacar una captura para ver qué pasa, correr algo como administrador. Es la
// idea de «computer use», acá nativa y sobre la propia máquina del aparato.
//
// PERMISOS. Nada se hace sin permiso. Lee en CADA llamada
// <datos>/Config/control-windows.json: un interruptor general `habilitado` y
// uno por capacidad (`pantalla`, `mouse`, `teclado`, `ejecutar`,
// `ejecutar_admin`, `buscar`, `ventanas`, `portapapeles`). El operador los
// cambia en Configuración → Control de Windows, o por el MCP
// interno (`control_estado` / `control_configurar`). De fábrica está APAGADO;
// prendido, «ejecutar como administrador» sigue apagado hasta que lo prendan.
// Un cambio vale en la llamada siguiente, sin relanzar.
//
// APROBACIÓN. Además, el cliente (Claude, Gemini, opencode) le pregunta al
// operador antes de cada herramienta de este servidor: la app no las pone
// entre las permitidas de antemano salvo que en Configuración se apague
// «preguntar antes de cada acción».
//
// SIN Qt, runtime de C++ estático: lo lanzan clientes externos, sin pasar por
// el launcher. En la Mac y Linux compila pero cada herramienta contesta que es
// solo para Windows (el control nativo es Win32).
// ============================================================================

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

using json = nlohmann::json;

// --- Configuración de permisos --------------------------------------------------------

/// Las capacidades, con su valor de fábrica: APAGADO todo (se prende en
/// Configuración). Prendido el interruptor general, cada capacidad viene
/// habilitada salvo `ejecutar_admin`, que hay que prender aparte.
struct Permisos {
    bool habilitado = false;
    bool pantalla = true, mouse = true, teclado = true, ejecutar = true, ejecutar_admin = false, buscar = true,
         ventanas = true, portapapeles = true;
};

std::string dataRoot() {
    const char* r = std::getenv("BA_DATA_ROOT");
    return r != nullptr ? std::string(r) : std::string();
}

/// Lee los permisos del archivo (en cada llamada: un cambio vale enseguida).
Permisos permisos() {
    Permisos p;
    const std::string root = dataRoot();
    if (root.empty()) return p; // sin datos: de fábrica (apagado)
    std::ifstream f(root + "/Config/control-windows.json");
    if (!f) return p;
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return p;
    p.habilitado = j.value("habilitado", false);
    const json m = j.value("permisos", json::object());
    const auto leer = [&](const char* k, bool& v) { if (m.contains(k)) v = m.value(k, v); };
    leer("pantalla", p.pantalla);
    leer("mouse", p.mouse);
    leer("teclado", p.teclado);
    leer("ejecutar", p.ejecutar);
    leer("ejecutar_admin", p.ejecutar_admin);
    leer("buscar", p.buscar);
    leer("ventanas", p.ventanas);
    leer("portapapeles", p.portapapeles);
    return p;
}

// --- Salida MCP -----------------------------------------------------------------------

std::mutex gSalida;
void escribir(const json& j) {
    const std::string s = j.dump();
    std::lock_guard<std::mutex> lk(gSalida);
    std::cout << s << "\n";
    std::cout.flush();
}

json resultadoTexto(const json& id, const std::string& texto, bool error = false) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"result", {{"content", json::array({{{"type", "text"}, {"text", texto}}})}, {"isError", error}}}};
}

[[maybe_unused]] json resultadoImagen(const json& id, const std::string& base64, const std::string& texto) {
    json content = json::array();
    if (!texto.empty()) content.push_back({{"type", "text"}, {"text", texto}});
    content.push_back({{"type", "image"}, {"data", base64}, {"mimeType", "image/png"}});
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"content", content}, {"isError", false}}}};
}

[[maybe_unused]] json resultadoDatos(const json& id, const json& datos) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"result",
                 {{"content", json::array({{{"type", "text"}, {"text", datos.dump(2)}}})},
                  {"structuredContent", datos},
                  {"isError", false}}}};
}

[[maybe_unused]] std::string base64(const unsigned char* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (d[i] << 16) | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o.push_back(t[(v >> 18) & 63]);
        o.push_back(t[(v >> 12) & 63]);
        o.push_back(i + 1 < n ? t[(v >> 6) & 63] : '=');
        o.push_back(i + 2 < n ? t[v & 63] : '=');
    }
    return o;
}

// --- El catálogo de herramientas ------------------------------------------------------

json catalogo() {
    const auto obj = [](json props, std::vector<std::string> req = {}) {
        json j{{"type", "object"}, {"properties", props}};
        if (!req.empty()) j["required"] = req;
        return j;
    };
    json tools = json::array();
    const auto tool = [&](const char* name, const char* desc, json schema) {
        tools.push_back({{"name", name}, {"description", desc}, {"inputSchema", schema}});
    };
    tool("windows_pantalla",
         "Saca una captura de pantalla de ESTA máquina (PNG). Sin argumentos: todo el escritorio (todos los "
         "monitores). `monitor`: el índice de windows_monitores. `x,y,ancho,alto`: una región. Permiso: pantalla.",
         obj({{"monitor", {{"type", "integer"}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"ancho", {{"type", "integer"}}},
              {"alto", {{"type", "integer"}}}}));
    tool("windows_monitores", "Lista los monitores: índice, posición, tamaño y cuál es el principal. Permiso: pantalla.",
         obj({}));
    tool("windows_mouse",
         "Mueve o acciona el mouse. `accion`: mover, click, doble, arrastrar, rueda. `x,y` el destino (en "
         "píxeles del escritorio virtual); `hasta_x,hasta_y` para arrastrar; `boton`: izq, der, medio; "
         "`pasos` para la rueda (positivo arriba). Permiso: mouse.",
         obj({{"accion", {{"type", "string"}, {"enum", {"mover", "click", "doble", "arrastrar", "rueda"}}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"hasta_x", {{"type", "integer"}}},
              {"hasta_y", {{"type", "integer"}}},
              {"boton", {{"type", "string"}, {"enum", {"izq", "der", "medio"}}}},
              {"pasos", {{"type", "integer"}}}},
             {"accion"}));
    tool("windows_teclado",
         "Escribe texto o manda una combinación de teclas. `texto`: lo escribe tal cual (Unicode). `teclas`: "
         "una combinación como \"ctrl+c\", \"win+r\", \"alt+tab\", \"enter\", \"f5\". Permiso: teclado.",
         obj({{"texto", {{"type", "string"}}}, {"teclas", {{"type", "string"}}}}));
    tool("windows_ejecutar",
         "Abre un programa, un archivo o una URL (como lo haría doble clic). `programa`: ruta del .exe, de un "
         "archivo o una URL. `argumentos`: opcional. `carpeta`: carpeta de trabajo. `admin`: true lo corre "
         "como administrador (pide UAC). `oculto`: sin ventana. Permiso: ejecutar (y ejecutar_admin si admin).",
         obj({{"programa", {{"type", "string"}}},
              {"argumentos", {{"type", "string"}}},
              {"carpeta", {{"type", "string"}}},
              {"admin", {{"type", "boolean"}}},
              {"oculto", {{"type", "boolean"}}}},
             {"programa"}));
    tool("windows_buscar",
         "Busca archivos por nombre. `patron`: parte del nombre (acepta * y ?). `carpeta`: dónde (por defecto "
         "el perfil del usuario). `max`: cuántos como mucho (por defecto 100). Permiso: buscar.",
         obj({{"patron", {{"type", "string"}}},
              {"carpeta", {{"type", "string"}}},
              {"max", {{"type", "integer"}}}},
             {"patron"}));
    tool("windows_ventanas", "Lista las ventanas visibles: título, id, proceso y posición. Permiso: ventanas.",
         obj({}));
    tool("windows_ventana",
         "Acciona una ventana. `id` (de windows_ventanas) o `titulo` (parte del título). `accion`: activar, "
         "minimizar, maximizar, restaurar, cerrar. Permiso: ventanas.",
         obj({{"id", {{"type", "integer"}}},
              {"titulo", {{"type", "string"}}},
              {"accion",
               {{"type", "string"}, {"enum", {"activar", "minimizar", "maximizar", "restaurar", "cerrar"}}}}},
             {"accion"}));
    tool("windows_portapapeles",
         "Lee o escribe el portapapeles de texto. Sin `texto`: lo lee. Con `texto`: lo pone. Permiso: portapapeles.",
         obj({{"texto", {{"type", "string"}}}}));
    return tools;
}

#if defined(_WIN32)
// --- Implementación en Windows --------------------------------------------------------

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

struct Virtual {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
};

// -- Captura de pantalla (GDI + GDI+ para PNG) --
int pngEncoder(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<unsigned char> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    return -1;
}

bool capturar(int x, int y, int w, int h, std::string& png, std::string& error) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ viejo = SelectObject(mem, bmp);
    const bool ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT) != 0;
    SelectObject(mem, viejo);
    bool listo = false;
    if (ok) {
        Gdiplus::Bitmap gbmp(bmp, nullptr);
        CLSID clsid;
        IStream* stream = nullptr;
        if (pngEncoder(&clsid) >= 0 && CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
            if (gbmp.Save(stream, &clsid, nullptr) == Gdiplus::Ok) {
                HGLOBAL hg = nullptr;
                GetHGlobalFromStream(stream, &hg);
                const SIZE_T n = GlobalSize(hg);
                void* p = GlobalLock(hg);
                if (p != nullptr) {
                    png = base64(static_cast<unsigned char*>(p), n);
                    listo = true;
                    GlobalUnlock(hg);
                }
            }
            stream->Release();
        }
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!listo) error = "no se pudo capturar la pantalla";
    return listo;
}

struct Monitor {
    int x, y, w, h;
    bool principal;
};
std::vector<Monitor> monitores() {
    std::vector<Monitor> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(hm, &mi)) {
                auto* v = reinterpret_cast<std::vector<Monitor>*>(lp);
                v->push_back({mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                              mi.rcMonitor.bottom - mi.rcMonitor.top, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

// -- Mouse --
void mouseAbsoluto(int px, int py) {
    // SetCursorPos: exacto en coordenadas del escritorio virtual (con la
    // normalización a 0..65535 de SendInput quedaba un píxel corrido).
    SetCursorPos(px, py);
}
void mouseBoton(const std::string& boton, bool abajo) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    if (boton == "der") in.mi.dwFlags = abajo ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    else if (boton == "medio") in.mi.dwFlags = abajo ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    else in.mi.dwFlags = abajo ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

// -- Teclado --
WORD teclaVk(const std::string& n, bool& ok) {
    ok = true;
    if (n.size() == 1) {
        const SHORT s = VkKeyScanA(n[0]);
        if (s != -1) return static_cast<WORD>(s & 0xFF);
    }
    std::string k;
    for (char c : n) k.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
    static const std::map<std::string, WORD> m{
        {"ctrl", VK_CONTROL},   {"control", VK_CONTROL}, {"alt", VK_MENU},    {"shift", VK_SHIFT},
        {"win", VK_LWIN},       {"enter", VK_RETURN},    {"return", VK_RETURN}, {"tab", VK_TAB},
        {"esc", VK_ESCAPE},     {"escape", VK_ESCAPE},   {"space", VK_SPACE}, {"espacio", VK_SPACE},
        {"backspace", VK_BACK}, {"supr", VK_DELETE},     {"delete", VK_DELETE}, {"del", VK_DELETE},
        {"ins", VK_INSERT},     {"home", VK_HOME},       {"inicio", VK_HOME}, {"end", VK_END},
        {"fin", VK_END},        {"pageup", VK_PRIOR},    {"pagedown", VK_NEXT}, {"arriba", VK_UP},
        {"abajo", VK_DOWN},     {"izquierda", VK_LEFT},  {"derecha", VK_RIGHT}, {"up", VK_UP},
        {"down", VK_DOWN},      {"left", VK_LEFT},       {"right", VK_RIGHT}};
    if (const auto it = m.find(k); it != m.end()) return it->second;
    if ((k.size() == 2 || k.size() == 3) && k[0] == 'f') { // F1..F24
        const int num = atoi(k.c_str() + 1);
        if (num >= 1 && num <= 24) return static_cast<WORD>(VK_F1 + num - 1);
    }
    ok = false;
    return 0;
}
void tecla(WORD vk, bool abajo) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = abajo ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}
void escribirTexto(const std::wstring& w) {
    for (wchar_t c : w) {
        INPUT in[2]{};
        for (int i = 0; i < 2; ++i) {
            in[i].type = INPUT_KEYBOARD;
            in[i].ki.wScan = c;
            in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
        }
        SendInput(2, in, sizeof(INPUT));
    }
}

// -- Ventanas --
struct Ventana {
    HWND hwnd;
    std::string titulo;
    DWORD pid;
    RECT rect;
};
std::vector<Ventana> ventanas() {
    std::vector<Ventana> out;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(h)) return TRUE;
            wchar_t t[512];
            const int n = GetWindowTextW(h, t, 512);
            if (n == 0) return TRUE;
            auto* v = reinterpret_cast<std::vector<Ventana>*>(lp);
            Ventana w;
            w.hwnd = h;
            w.titulo = aU(std::wstring(t, n));
            GetWindowThreadProcessId(h, &w.pid);
            GetWindowRect(h, &w.rect);
            v->push_back(w);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}
std::string tituloDe(HWND h) {
    wchar_t t[512];
    const int n = h != nullptr ? GetWindowTextW(h, t, 512) : 0;
    return n > 0 ? aU(std::wstring(t, n)) : std::string("(sin título)");
}
/// «en «título» (id N)»: dónde fue lo que se hizo, para que el asistente vea
/// si le pegó a la ventana que quería (un título parcial puede coincidir con otra).
std::string donde(HWND h) {
    return "en «" + tituloDe(h) + "» (id " + std::to_string(reinterpret_cast<long long>(h)) + ")";
}

/// Trae una ventana al frente de verdad. Windows no deja que un proceso de
/// fondo le robe el foco a otro: SetForegroundWindow solo hace titilar la
/// barra de tareas. Una tecla Alt sintética antes lo habilita (es la entrada
/// del usuario más reciente). Se verifica: si no quedó al frente, se dice.
bool traerAlFrente(HWND h) {
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    else ShowWindow(h, SW_SHOW);
    INPUT alt[2]{};
    alt[0].type = alt[1].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, alt, sizeof(INPUT));
    SetForegroundWindow(h);
    for (int i = 0; i < 20 && GetForegroundWindow() != h; ++i) Sleep(25);
    return GetForegroundWindow() == h;
}

HWND buscarVentana(const json& a) {
    const auto lista = ventanas();
    if (a.contains("id")) {
        const auto id = a.value("id", 0LL);
        for (const auto& w : lista)
            if (reinterpret_cast<long long>(w.hwnd) == id) return w.hwnd;
    }
    const std::string t = a.value("titulo", std::string());
    if (!t.empty()) {
        std::string tl;
        for (char c : t) tl.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
        for (const auto& w : lista) {
            std::string wl;
            for (char c : w.titulo) wl.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            if (wl.find(tl) != std::string::npos) return w.hwnd;
        }
    }
    return nullptr;
}

// -- Buscar archivos --
void buscarEn(const std::wstring& dir, const std::wstring& patron, std::vector<std::string>& out, int max,
              int profundidad) {
    if (static_cast<int>(out.size()) >= max || profundidad > 12) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subdirs;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring ruta = dir + L"\\" + fd.cFileName;
        if (PathMatchSpecW(fd.cFileName, patron.c_str()) && static_cast<int>(out.size()) < max)
            out.push_back(aU(ruta));
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(fd.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SYSTEM)))
            subdirs.push_back(ruta);
    } while (FindNextFileW(h, &fd) && static_cast<int>(out.size()) < max);
    FindClose(h);
    for (const auto& s : subdirs) {
        if (static_cast<int>(out.size()) >= max) break;
        buscarEn(s, patron, out, max, profundidad + 1);
    }
}

std::string portapapelesLeer() {
    if (!OpenClipboard(nullptr)) return {};
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
            out = aU(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
bool portapapelesEscribir(const std::wstring& w) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (hg != nullptr) {
        if (void* p = GlobalLock(hg)) {
            memcpy(p, w.c_str(), bytes);
            GlobalUnlock(hg);
            ok = SetClipboardData(CF_UNICODETEXT, hg) != nullptr;
        }
        if (!ok) GlobalFree(hg);
    }
    CloseClipboard();
    return ok;
}

// --- Despacho en Windows --
json ejecutarHerramienta(const json& id, const std::string& nombre, const json& a, const Permisos& p) {
    const auto noPermiso = [&](const char* que) {
        return resultadoTexto(id,
                              std::string("El control de Windows no tiene permiso para «") + que +
                                  "» (Configuración → Control de Windows, o control_configurar).",
                              true);
    };
    if (nombre == "windows_pantalla") {
        if (!p.pantalla) return noPermiso("pantalla");
        const Virtual v;
        int x = v.x, y = v.y, w = v.w, h = v.h;
        if (a.contains("monitor")) {
            const auto ms = monitores();
            const int i = a.value("monitor", 0);
            if (i < 0 || i >= static_cast<int>(ms.size())) return resultadoTexto(id, "no existe ese monitor", true);
            x = ms[i].x; y = ms[i].y; w = ms[i].w; h = ms[i].h;
        }
        if (a.contains("ancho") && a.contains("alto")) {
            x = a.value("x", 0); y = a.value("y", 0); w = a.value("ancho", w); h = a.value("alto", h);
        }
        if (w <= 0 || h <= 0) return resultadoTexto(id, "tamaño inválido", true);
        std::string png, error;
        if (!capturar(x, y, w, h, png, error)) return resultadoTexto(id, error, true);
        return resultadoImagen(id, png, "Captura " + std::to_string(w) + "x" + std::to_string(h));
    }
    if (nombre == "windows_monitores") {
        if (!p.pantalla) return noPermiso("pantalla");
        json out = json::array();
        int i = 0;
        for (const auto& m : monitores())
            out.push_back({{"indice", i++}, {"x", m.x}, {"y", m.y}, {"ancho", m.w}, {"alto", m.h}, {"principal", m.principal}});
        return resultadoDatos(id, {{"monitores", out}});
    }
    if (nombre == "windows_mouse") {
        if (!p.mouse) return noPermiso("mouse");
        const std::string accion = a.value("accion", std::string());
        const std::string boton = a.value("boton", std::string("izq"));
        if (a.contains("x") && a.contains("y")) mouseAbsoluto(a.value("x", 0), a.value("y", 0));
        if (accion == "mover") {
            // ya se movió
        } else if (accion == "click" || accion == "doble") {
            const int veces = accion == "doble" ? 2 : 1;
            for (int i = 0; i < veces; ++i) { mouseBoton(boton, true); mouseBoton(boton, false); }
        } else if (accion == "arrastrar") {
            mouseBoton(boton, true);
            Sleep(30);
            mouseAbsoluto(a.value("hasta_x", a.value("x", 0)), a.value("hasta_y", a.value("y", 0)));
            Sleep(30);
            mouseBoton(boton, false);
        } else if (accion == "rueda") {
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = static_cast<DWORD>(a.value("pasos", 1) * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
        }
        POINT pt{};
        GetCursorPos(&pt);
        HWND bajo = WindowFromPoint(pt);
        HWND raiz = bajo != nullptr ? GetAncestor(bajo, GA_ROOT) : nullptr;
        return resultadoTexto(id, "Hecho. El mouse quedó en " + std::to_string(pt.x) + "," + std::to_string(pt.y) +
                                      ", sobre «" + tituloDe(raiz) + "».");
    }
    if (nombre == "windows_teclado") {
        if (!p.teclado) return noPermiso("teclado");
        if (a.contains("texto")) escribirTexto(aW(a.value("texto", std::string())));
        if (a.contains("teclas")) {
            const std::string combo = a.value("teclas", std::string());
            std::vector<WORD> vks;
            std::string parte;
            const auto empujar = [&]() {
                if (parte.empty()) return true;
                bool ok;
                const WORD vk = teclaVk(parte, ok);
                parte.clear();
                if (ok) vks.push_back(vk);
                return ok;
            };
            bool ok = true;
            for (char c : combo) {
                if (c == '+') ok = empujar() && ok;
                else parte.push_back(c);
            }
            ok = empujar() && ok;
            if (!ok || vks.empty()) return resultadoTexto(id, "no entendí la combinación de teclas", true);
            for (WORD vk : vks) tecla(vk, true);
            for (auto it = vks.rbegin(); it != vks.rend(); ++it) tecla(*it, false);
        }
        return resultadoTexto(id, "Hecho, " + donde(GetForegroundWindow()) + ".");
    }
    if (nombre == "windows_ejecutar") {
        if (!p.ejecutar) return noPermiso("ejecutar");
        const bool admin = a.value("admin", false);
        if (admin && !p.ejecutar_admin) return noPermiso("ejecutar como administrador");
        const std::string prog = a.value("programa", std::string());
        if (prog.empty()) return resultadoTexto(id, "falta el programa", true);
        SHELLEXECUTEINFOW ei{};
        ei.cbSize = sizeof(ei);
        ei.fMask = SEE_MASK_NOCLOSEPROCESS;
        const std::wstring verbo = admin ? L"runas" : L"open";
        const std::wstring wprog = aW(prog), wargs = aW(a.value("argumentos", std::string())),
                           wdir = aW(a.value("carpeta", std::string()));
        ei.lpVerb = verbo.c_str();
        ei.lpFile = wprog.c_str();
        if (!wargs.empty()) ei.lpParameters = wargs.c_str();
        if (!wdir.empty()) ei.lpDirectory = wdir.c_str();
        ei.nShow = a.value("oculto", false) ? SW_HIDE : SW_SHOWNORMAL;
        if (!ShellExecuteExW(&ei)) {
            const DWORD e = GetLastError();
            if (e == ERROR_CANCELLED) return resultadoTexto(id, "el usuario canceló el pedido de administrador (UAC)", true);
            return resultadoTexto(id, "no se pudo ejecutar «" + prog + "» (error " + std::to_string(e) + ")", true);
        }
        DWORD pid = 0;
        if (ei.hProcess != nullptr) { pid = GetProcessId(ei.hProcess); CloseHandle(ei.hProcess); }
        return resultadoTexto(id, "Ejecutado «" + prog + "»" + (admin ? " como administrador" : "") +
                                      (pid ? " (pid " + std::to_string(pid) + ")" : "") + ".");
    }
    if (nombre == "windows_buscar") {
        if (!p.buscar) return noPermiso("buscar");
        const std::string patron = a.value("patron", std::string());
        if (patron.empty()) return resultadoTexto(id, "falta el patrón", true);
        std::wstring dir = aW(a.value("carpeta", std::string()));
        if (dir.empty()) {
            wchar_t perfil[MAX_PATH];
            if (GetEnvironmentVariableW(L"USERPROFILE", perfil, MAX_PATH) > 0) dir = perfil;
            else dir = L"C:\\";
        }
        std::wstring pat = aW(patron);
        if (pat.find(L'*') == std::wstring::npos && pat.find(L'?') == std::wstring::npos) pat = L"*" + pat + L"*";
        std::vector<std::string> res;
        buscarEn(dir, pat, res, a.value("max", 100), 0);
        return resultadoDatos(id, {{"encontrados", res}, {"total", res.size()}});
    }
    if (nombre == "windows_ventanas") {
        if (!p.ventanas) return noPermiso("ventanas");
        json out = json::array();
        for (const auto& w : ventanas())
            out.push_back({{"id", reinterpret_cast<long long>(w.hwnd)},
                           {"titulo", w.titulo},
                           {"pid", static_cast<long long>(w.pid)},
                           {"x", w.rect.left},
                           {"y", w.rect.top},
                           {"ancho", w.rect.right - w.rect.left},
                           {"alto", w.rect.bottom - w.rect.top}});
        return resultadoDatos(id, {{"ventanas", out}});
    }
    if (nombre == "windows_ventana") {
        if (!p.ventanas) return noPermiso("ventanas");
        HWND h = buscarVentana(a);
        if (h == nullptr) return resultadoTexto(id, "no encontré esa ventana", true);
        const std::string accion = a.value("accion", std::string());
        if (accion == "activar") {
            if (!traerAlFrente(h))
                return resultadoTexto(id, "Windows no dejó traer esa ventana al frente (probá con windows_mouse "
                                          "un clic sobre ella)", true);
        }
        else if (accion == "minimizar") ShowWindow(h, SW_MINIMIZE);
        else if (accion == "maximizar") ShowWindow(h, SW_MAXIMIZE);
        else if (accion == "restaurar") ShowWindow(h, SW_RESTORE);
        else if (accion == "cerrar") PostMessageW(h, WM_CLOSE, 0, 0);
        else return resultadoTexto(id, "acción desconocida", true);
        return resultadoTexto(id, "Hecho " + donde(h) + ".");
    }
    if (nombre == "windows_portapapeles") {
        if (!p.portapapeles) return noPermiso("portapapeles");
        if (a.contains("texto")) {
            if (!portapapelesEscribir(aW(a.value("texto", std::string())))) return resultadoTexto(id, "no se pudo escribir", true);
            return resultadoTexto(id, "Copiado al portapapeles.");
        }
        return resultadoDatos(id, {{"texto", portapapelesLeer()}});
    }
    return resultadoTexto(id, "herramienta desconocida: " + nombre, true);
}
#else  // no Windows
json ejecutarHerramienta(const json& id, const std::string& nombre, const json&, const Permisos&) {
    (void)nombre;
    return resultadoTexto(id, "El control del escritorio es solo para Windows.", true);
}
#endif

json llamarHerramienta(const json& id, const std::string& nombre, const json& a) {
    const Permisos p = permisos();
    if (!p.habilitado)
        return resultadoTexto(id,
                              "El control de Windows está deshabilitado (Configuración → Control de Windows, "
                              "o control_configurar {habilitado: true}).",
                              true);
    try {
        return ejecutarHerramienta(id, nombre, a, p);
    } catch (const std::exception& e) {
        return resultadoTexto(id, std::string("error: ") + e.what(), true);
    }
}

} // namespace

int main() {
#if defined(_WIN32)
    // El protocolo va en bytes: sin la conversión \n → \r\n del modo texto.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    SetProcessDPIAware(); // coordenadas en píxeles reales
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiTok = 0;
    Gdiplus::GdiplusStartup(&gdiTok, &gsi, nullptr);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        const json doc = json::parse(line, nullptr, false);
        if (!doc.is_object()) continue;
        const std::string metodo = doc.value("method", std::string());
        const json id = doc.contains("id") ? doc["id"] : json();
        const bool notificacion = !doc.contains("id");
        if (metodo == "initialize") {
            escribir({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"result",
                       {{"protocolVersion", "2024-11-05"},
                        {"capabilities", {{"tools", json::object()}}},
                        {"serverInfo", {{"name", "ba-control"}, {"version", "1.0"}}},
                        {"instructions",
                         "Control del escritorio de ESTA máquina Windows (mouse, teclado, captura, abrir "
                         "programas, buscar, ventanas, portapapeles). Mirá windows_monitores y windows_pantalla "
                         "antes de mover el mouse por coordenadas."}}}});
        } else if (metodo == "tools/list") {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"tools", catalogo()}}}});
        } else if (metodo == "tools/call") {
            const json params = doc.value("params", json::object());
            escribir(llamarHerramienta(id, params.value("name", std::string()),
                                       params.value("arguments", json::object())));
        } else if (metodo == "ping") {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}});
        } else if (!notificacion) {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", "método desconocido"}}}});
        }
    }
#if defined(_WIN32)
    if (gdiTok) Gdiplus::GdiplusShutdown(gdiTok);
#endif
    return 0;
}
