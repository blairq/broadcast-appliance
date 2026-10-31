// ============================================================================
// src/app/escritorio/ba_control_main.cpp — ba-control: control del sistema (el
// escritorio de esta máquina) como servidor MCP, para los asistentes del aparato.
// Las mismas herramientas system_* en Windows, macOS y Linux; cada sistema con su
// motor (docs/planes/PLAN-CONTROL-SISTEMA.md).
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
// <datos>/Config/control-sistema.json: un interruptor general `habilitado` y
// uno por capacidad (`pantalla`, `mouse`, `teclado`, `ejecutar`,
// `ejecutar_admin`, `buscar`, `ventanas`, `portapapeles`, `procesos`,
// `comandos`, `sistema`). El operador los
// cambia en Configuración → Control del sistema, o por el MCP
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
// el launcher. Donde una herramienta todavía no tiene motor, contesta eso (todaviaNo).
// ============================================================================

#include "escritorio/control.hpp"
#include "platform/Paths.hpp"
#include "platform/Hilo.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <thread>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2 // GetProcessMemoryInfo de kernel32 (K32…), sin psapi.lib
#endif
#include <psapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace bactl {

// --- Configuración de permisos (Permisos: control.hpp) ---------------------------------

std::string dataRoot() {
    const char* r = std::getenv("BA_DATA_ROOT");
    return r != nullptr ? std::string(r) : std::string();
}

/// Lee los permisos del archivo (en cada llamada: un cambio vale enseguida).
Permisos permisos() {
    Permisos p;
    const std::string root = dataRoot();
    if (root.empty()) return p; // sin datos: de fábrica (apagado)
    // control-sistema.json (y el de antes, control-windows.json, hasta que se guarde el nuevo).
    std::ifstream f(root + "/Config/control-sistema.json");
    if (!f) f.open(root + "/Config/control-windows.json");
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
    leer("procesos", p.procesos);
    leer("comandos", p.comandos);
    leer("sistema", p.sistema);
    leer("archivos", p.archivos);
    leer("archivos_escribir", p.archivos_escribir);
    leer("navegador", p.navegador);
    leer("registro", p.registro);
    leer("avisos", p.avisos);
    return p;
}

// --- Salida MCP -----------------------------------------------------------------------

std::mutex gSalida;
void escribir(const json& j) {
    const std::string s = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::lock_guard<std::mutex> lk(gSalida);
    std::cout << s << "\n";
    std::cout.flush();
}

json resultadoTexto(const json& id, const std::string& texto, bool error) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"result", {{"content", json::array({{{"type", "text"}, {"text", texto}}})}, {"isError", error}}}};
}

json resultadoImagen(const json& id, const std::string& base64, const std::string& texto) {
    json content = json::array();
    if (!texto.empty()) content.push_back({{"type", "text"}, {"text", texto}});
    content.push_back({{"type", "image"}, {"data", base64}, {"mimeType", "image/png"}});
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"content", content}, {"isError", false}}}};
}

json resultadoDatos(const json& id, const json& datos) {
    return json{{"jsonrpc", "2.0"},
                {"id", id},
                {"result",
                 {{"content", json::array({{{"type", "text"}, {"text", datos.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)}}})},
                  {"structuredContent", datos},
                  {"isError", false}}}};
}

std::string base64(const unsigned char* d, size_t n) {
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

json esquema(json props, std::vector<std::string> req) {
    // Sin argumentos, `{}` llega acá como null: los clientes (Claude) exigen un
    // objeto en `properties`, aunque esté vacío, y con null no cargaban el servidor.
    if (!props.is_object()) props = json::object();
    json j{{"type", "object"}, {"properties", props}};
    if (!req.empty()) j["required"] = req;
    return j;
}

json herramienta(const char* nombre, const std::string& descripcion, json esq) {
    return {{"name", nombre}, {"description", descripcion}, {"inputSchema", std::move(esq)}};
}

std::string todaviaNo(const std::string& herramienta) {
#if defined(__APPLE__)
    const char* sis = "macOS";
#elif defined(_WIN32)
    const char* sis = "Windows";
#else
    const char* sis = "Linux";
#endif
    return ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 todavía no está hecha en %2 (las herramientas system_* son las "
                                                      "mismas en los tres sistemas; el motor de este está en camino)."),
                            {herramienta, sis});
}

json sinPermiso(const json& id, const char* que) {
    return resultadoTexto(id,
                          ba::i18n::llenar(ba::i18n::traducir("mcp", "El control del sistema no tiene permiso para «%1» "
                                                                     "(Configuración → Control del sistema, o control_configurar)."),
                                           {que}),
                          true);
}

json catalogo() {
    const auto obj = [](json props, std::vector<std::string> req = {}) { return esquema(std::move(props), std::move(req)); };
    json tools = json::array();
    const auto tool = [&](const char* name, const std::string& desc, json schema) {
        tools.push_back({{"name", name}, {"description", desc}, {"inputSchema", schema}});
    };
    tool("system_pantalla",
         ba::i18n::traducir("mcp", "Saca una captura de pantalla de ESTA máquina (PNG; Windows, macOS o Linux). Sin argumentos: todo el "
         "escritorio (todos los monitores). `monitor`: el índice de system_monitores. `x,y,ancho,alto`: una región. "
         "`ventana` (id de system_ventanas) o `titulo`: solo esa ventana, aunque esté tapada. `escala`: achicar "
         "(0.1..1, p. ej. 0.5 en un monitor 4K). Las coordenadas de la imagen son las del mouse (en macOS, puntos "
         "de pantalla). Permiso: pantalla."),
         obj({{"monitor", {{"type", "integer"}}},
              {"ventana", {{"type", "integer"}}},
              {"titulo", {{"type", "string"}}},
              {"escala", {{"type", "number"}, {"minimum", 0.1}, {"maximum", 1}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"ancho", {{"type", "integer"}}},
              {"alto", {{"type", "integer"}}}}));
    tool("system_monitores", ba::i18n::traducir("mcp", "Lista los monitores: índice, posición, tamaño y cuál es el principal. Permiso: pantalla."),
         obj({}));
    tool("system_mouse",
         ba::i18n::traducir("mcp", "Mueve o acciona el mouse. `accion`: mover, click, doble, arrastrar, rueda. `x,y` el destino (en "
         "píxeles del escritorio virtual); `hasta_x,hasta_y` para arrastrar; `boton`: izq, der, medio; "
         "`pasos` para la rueda (positivo arriba). Permiso: mouse."),
         obj({{"accion", {{"type", "string"}, {"enum", {"mover", "click", "doble", "arrastrar", "rueda"}}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"hasta_x", {{"type", "integer"}}},
              {"hasta_y", {{"type", "integer"}}},
              {"boton", {{"type", "string"}, {"enum", {"izq", "der", "medio"}}}},
              {"pasos", {{"type", "integer"}}}},
             {"accion"}));
    tool("system_teclado",
         ba::i18n::traducir("mcp", "Escribe texto o manda una combinación de teclas. `texto`: lo escribe tal cual (Unicode). `teclas`: "
         "una combinación como \"mod+c\", \"alt+tab\", \"enter\", \"f5\". `mod` es la tecla de los atajos de "
         "este sistema (Control en Windows y Linux, Comando en macOS): «mod+c» copia en los tres. `cmd`/`win`/"
         "`super` es Comando en macOS y la tecla Windows en Windows. Permiso: teclado."),
         obj({{"texto", {{"type", "string"}}}, {"teclas", {{"type", "string"}}}}));
    tool("system_ejecutar",
         ba::i18n::traducir("mcp", "Abre un programa, un archivo o una URL (como lo haría doble clic). `programa`: la ruta del programa o "
         "de un archivo, una URL o (en macOS) el nombre de la aplicación («Safari») o su identificador. "
         "`argumentos`: opcional. `carpeta`: carpeta de trabajo. `admin`: true lo corre como administrador (pide "
         "UAC en Windows, la clave en macOS). `oculto`: sin ventana / sin traerlo al frente. Permiso: ejecutar (y "
         "ejecutar_admin si admin)."),
         obj({{"programa", {{"type", "string"}}},
              {"argumentos", {{"type", "string"}}},
              {"carpeta", {{"type", "string"}}},
              {"admin", {{"type", "boolean"}}},
              {"oculto", {{"type", "boolean"}}}},
             {"programa"}));
    tool("system_buscar",
         ba::i18n::traducir("mcp", "Busca archivos por nombre. `patron`: parte del nombre (acepta * y ?). `carpeta`: dónde (por defecto "
         "el perfil del usuario). `max`: cuántos como mucho (por defecto 100). Permiso: buscar."),
         obj({{"patron", {{"type", "string"}}},
              {"carpeta", {{"type", "string"}}},
              {"max", {{"type", "integer"}}}},
             {"patron"}));
    tool("system_ventanas", ba::i18n::traducir("mcp", "Lista las ventanas visibles: título, id, aplicación, si es un navegador "
         "(Chrome, Edge…: ver system_chrome), proceso y posición. Permiso: ventanas."),
         obj({}));
    tool("system_ventana",
         ba::i18n::traducir("mcp", "Acciona una ventana. `id` (de system_ventanas) o `titulo` (parte del título o de la aplicación). "
         "`accion`: activar, minimizar, maximizar, restaurar, cerrar, mover (con `x,y` y/o `ancho,alto`), "
         "fijar_arriba (siempre visible; macOS no lo deja para otros programas) o soltar_arriba. Permiso: ventanas."),
         obj({{"id", {{"type", "integer"}}},
              {"titulo", {{"type", "string"}}},
              {"accion",
               {{"type", "string"},
                {"enum", {"activar", "minimizar", "maximizar", "restaurar", "cerrar", "mover", "fijar_arriba", "soltar_arriba"}}}},
              {"x", {{"type", "integer"}}},
              {"y", {{"type", "integer"}}},
              {"ancho", {{"type", "integer"}}},
              {"alto", {{"type", "integer"}}}},
             {"accion"}));
    tool("system_portapapeles",
         ba::i18n::traducir("mcp", "Lee o escribe el portapapeles de texto. Sin `texto`: lo lee. Con `texto`: lo pone. Permiso: portapapeles."),
         obj({{"texto", {{"type", "string"}}}}));
    tool("system_cursor",
         ba::i18n::traducir("mcp", "Dónde está el mouse, qué ventana tiene abajo y cuál tiene el foco (la que recibe el teclado), y si la "
         "persona está usando la máquina (`inactivo_s`: hace cuánto tocó mouse o teclado; `usandola` si fue hace "
         "menos de 5 s: esperar antes de mover el mouse). Sin argumentos. Permiso: pantalla."),
         obj({}));
    tool("system_procesos",
         ba::i18n::traducir("mcp", "Lista los procesos: pid, nombre, memoria (MB) y si tienen ventana. `nombre`: filtrar (parte del nombre "
         "del ejecutable). `max`: cuántos (por defecto 200, los que más memoria usan primero). Permiso: procesos."),
         obj({{"nombre", {{"type", "string"}}}, {"max", {{"type", "integer"}}}}));
    tool("system_proceso_cerrar",
         ba::i18n::traducir("mcp", "Cierra un proceso por su `pid` (de system_procesos): primero le pide que cierre sus ventanas; con "
         "`forzar`: true lo termina en seco (se pierde lo no guardado). Nunca por nombre: el pid es uno solo. "
         "Permiso: procesos."),
         obj({{"pid", {{"type", "integer"}}}, {"forzar", {{"type", "boolean"}}}}, {"pid"}));
    tool("system_comando",
         ba::i18n::traducir("mcp", "Corre un comando y devuelve lo que contesta (salida, errores y código de salida). `comando`: la línea a "
         "correr. `shell`: sistema (por defecto: cmd en Windows, zsh en macOS, sh en Linux), cmd, powershell, sh o "
         "bash. `carpeta`: dónde. `segundos`: lo más que espera (por defecto 30, hasta 300; si se pasa, lo termina). "
         "Sin ventana. Permiso: comandos."),
         obj({{"comando", {{"type", "string"}}},
              {"shell", {{"type", "string"}, {"enum", {"sistema", "cmd", "powershell", "sh", "bash"}}}},
              {"carpeta", {{"type", "string"}}},
              {"segundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 300}}}},
             {"comando"}));
    tool("system_sistema",
         ba::i18n::traducir("mcp", "Los datos del equipo: el sistema (Windows, macOS o Linux, con su versión), nombre, usuario, procesadores, memoria total y libre, discos "
         "con su espacio libre, hace cuánto que está prendido y la batería si tiene. Sin argumentos. Permiso: sistema."),
         obj({}));
    tool("system_esperar",
         ba::i18n::traducir("mcp", "Espera `ms` milisegundos (hasta 30000), o hasta que aparezca una ventana cuyo título contenga "
         "`titulo` (con `ms` como límite). Para dar tiempo a que abra un programa antes de seguir."),
         obj({{"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 30000}}}, {"titulo", {{"type", "string"}}}}));
    // Las otras partes: UI Automation, Chrome, archivos y registro.
    for (const json& parte : {herramientasUia(), herramientasChrome(), herramientasArchivos()})
        for (const auto& t : parte) tools.push_back(t);
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

/// El bitmap a PNG en base64, achicado a `escala` (0.1..1).
bool aPng(HBITMAP bmp, int w, int h, double escala, std::string& png, int& sw, int& sh) {
    Gdiplus::Bitmap original(bmp, nullptr);
    Gdiplus::Bitmap* gbmp = &original;
    std::unique_ptr<Gdiplus::Bitmap> chico;
    escala = std::clamp(escala, 0.1, 1.0);
    sw = w, sh = h;
    if (escala < 0.999) {
        sw = std::max(1, static_cast<int>(w * escala));
        sh = std::max(1, static_cast<int>(h * escala));
        chico = std::make_unique<Gdiplus::Bitmap>(sw, sh, PixelFormat24bppRGB);
        Gdiplus::Graphics g(chico.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(&original, 0, 0, sw, sh);
        gbmp = chico.get();
    }
    bool listo = false;
    {
        CLSID clsid;
        IStream* stream = nullptr;
        if (pngEncoder(&clsid) >= 0 && CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
            if (gbmp->Save(stream, &clsid, nullptr) == Gdiplus::Ok) {
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
    return listo;
}

bool capturar(int x, int y, int w, int h, double escala, std::string& png, int& sw, int& sh, std::string& error) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ viejo = SelectObject(mem, bmp);
    const bool ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT) != 0;
    SelectObject(mem, viejo);
    const bool listo = ok && aPng(bmp, w, h, escala, png, sw, sh);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!listo) error = ba::i18n::traducir("mcp", "no se pudo capturar la pantalla");
    return listo;
}

/// Una ventana sola, aunque esté tapada (PrintWindow con todo su contenido, también lo
/// que dibuja la GPU). Minimizada no hay nada que dibujar.
bool capturarVentana(HWND h, double escala, std::string& png, int& sw, int& sh, std::string& error) {
    if (IsIconic(h)) {
        error = ba::i18n::traducir("mcp", "la ventana está minimizada (system_ventana accion=restaurar primero)");
        return false;
    }
    RECT r{};
    GetWindowRect(h, &r);
    const int w = r.right - r.left, alto = r.bottom - r.top;
    if (w <= 0 || alto <= 0) {
        error = ba::i18n::traducir("mcp", "la ventana no tiene tamaño");
        return false;
    }
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, alto);
    HGDIOBJ viejo = SelectObject(mem, bmp);
    constexpr UINT kTodo = 0x00000002; // PW_RENDERFULLCONTENT
    const bool ok = PrintWindow(h, mem, kTodo) != 0;
    SelectObject(mem, viejo);
    const bool listo = ok && aPng(bmp, w, alto, escala, png, sw, sh);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!listo) error = ba::i18n::traducir("mcp", "Windows no dejó capturar esa ventana");
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
        {"win", VK_LWIN},       {"cmd", VK_LWIN},        {"command", VK_LWIN}, {"super", VK_LWIN},
        {"meta", VK_LWIN},      {"mod", VK_CONTROL},     {"option", VK_MENU},  {"opcion", VK_MENU},
        {"mayus", VK_SHIFT},    {"enter", VK_RETURN},    {"return", VK_RETURN}, {"tab", VK_TAB},
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
std::string ejecutableDe(DWORD pid) {
    std::string out;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t ruta[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, ruta, &n)) out = aU(PathFindFileNameW(ruta));
        CloseHandle(h);
    }
    return out;
}

bool mandarTeclas(const std::string& combo) {
    std::vector<WORD> vks;
    std::string parte;
    bool ok = true;
    const auto empujar = [&]() {
        if (parte.empty()) return;
        bool bien;
        const WORD vk = teclaVk(parte, bien);
        parte.clear();
        if (bien) vks.push_back(vk);
        else ok = false;
    };
    for (char c : combo) {
        if (c == '+') empujar();
        else parte.push_back(c);
    }
    empujar();
    if (!ok || vks.empty()) return false;
    for (WORD vk : vks) tecla(vk, true);
    for (auto it = vks.rbegin(); it != vks.rend(); ++it) tecla(*it, false);
    return true;
}

std::string tituloDe(HWND h) {
    wchar_t t[512];
    const int n = h != nullptr ? GetWindowTextW(h, t, 512) : 0;
    return n > 0 ? aU(std::wstring(t, n)) : ba::i18n::traducir("mcp", "(sin título)");
}
/// «en «título» (id N)»: dónde fue lo que se hizo, para que el asistente vea
/// si le pegó a la ventana que quería (un título parcial puede coincidir con otra).
std::string donde(HWND h) {
    return ba::i18n::llenar(ba::i18n::traducir("mcp", "en «%1» (id %2)"),
                            {tituloDe(h), std::to_string(reinterpret_cast<long long>(h))});
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

// -- Procesos --
json listarProcesos(const std::string& filtro, int max) {
    std::set<DWORD> conVentana;
    for (const auto& w : ventanas()) conVentana.insert(w.pid);
    std::string fl;
    for (char c : filtro) fl.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
    struct P {
        DWORD pid;
        std::string nombre;
        double mb;
    };
    std::vector<P> ps;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            const std::string n = aU(pe.szExeFile);
            std::string nl;
            for (char c : n) nl.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            if (!fl.empty() && nl.find(fl) == std::string::npos) continue;
            double mb = 0;
            if (HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
                PROCESS_MEMORY_COUNTERS pmc{};
                if (GetProcessMemoryInfo(hp, &pmc, sizeof(pmc))) mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
                CloseHandle(hp);
            }
            ps.push_back({pe.th32ProcessID, n, mb});
        }
        CloseHandle(snap);
    }
    std::sort(ps.begin(), ps.end(), [](const P& x, const P& y) { return x.mb > y.mb; });
    json out = json::array();
    for (const auto& q : ps) {
        if (static_cast<int>(out.size()) >= std::max(1, max)) break;
        out.push_back({{"pid", static_cast<long long>(q.pid)},
                       {"nombre", q.nombre},
                       {"memoria_mb", std::round(q.mb * 10) / 10},
                       {"ventana", conVentana.count(q.pid) > 0}});
    }
    return {{"procesos", out}, {"total", ps.size()}};
}

json cerrarProceso(const json& id, DWORD pid, bool forzar) {
    HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp == nullptr) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no pude abrir el proceso %1 (¿no existe, o es de otro usuario?)"),
                                                          {std::to_string(pid)}), true);
    std::string nombre;
    wchar_t ruta[MAX_PATH];
    DWORD n = MAX_PATH;
    if (QueryFullProcessImageNameW(hp, 0, ruta, &n)) nombre = aU(PathFindFileNameW(ruta));
    // Primero, como lo cerraría el usuario: que cierre sus ventanas.
    int ventanasCerradas = 0;
    for (const auto& w : ventanas())
        if (w.pid == pid) PostMessageW(w.hwnd, WM_CLOSE, 0, 0), ++ventanasCerradas;
    DWORD r = ventanasCerradas > 0 ? WaitForSingleObject(hp, 4000) : WAIT_TIMEOUT;
    std::string como = ba::i18n::traducir("mcp", "cerró sus ventanas");
    if (r != WAIT_OBJECT_0) {
        if (!forzar) {
            CloseHandle(hp);
            return resultadoTexto(id,
                                  ba::i18n::llenar(ventanasCerradas
                                                       ? ba::i18n::traducir("mcp", "«%1» (pid %2) sigue abierto (quizás pregunta "
                                                                                   "si guardar). Con forzar: true se termina en seco.")
                                                       : ba::i18n::traducir("mcp", "«%1» (pid %2) sigue abierto (no tiene "
                                                                                   "ventanas). Con forzar: true se termina en seco."),
                                                   {nombre, std::to_string(pid)}),
                                  true);
        }
        TerminateProcess(hp, 1);
        r = WaitForSingleObject(hp, 3000);
        como = ba::i18n::traducir("mcp", "terminado en seco");
    }
    CloseHandle(hp);
    if (r != WAIT_OBJECT_0) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» no terminó"), {nombre}), true);
    return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Cerrado «%1» (pid %2, %3)."),
                                          {nombre, std::to_string(pid), como}));
}

// -- Comandos --
/// Lo que contesta un comando: UTF-8 si ya lo es; si no, de la página de la consola (OEM).
std::string deConsola(const std::string& s) {
    const int nw = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (nw > 0 || s.empty()) return s;
    const int n = MultiByteToWideChar(CP_OEMCP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_OEMCP, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return aU(w);
}

json correrComando(const json& id, const std::string& cmd, const std::string& shell, const std::string& carpeta, int segundos) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE leer = nullptr, escribirP = nullptr;
    if (!CreatePipe(&leer, &escribirP, &sa, 0)) return resultadoTexto(id, ba::i18n::traducir("mcp", "no se pudo crear la tubería"), true);
    SetHandleInformation(leer, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = escribirP;
    si.hStdError = escribirP;
    const std::wstring w = aW(cmd);
    std::wstring linea = shell == "powershell"
                             ? L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + w
                             : L"cmd.exe /d /s /c \"" + w + L"\"";
    const std::wstring dir = aW(carpeta);
    PROCESS_INFORMATION pi{};
    // En un Job: si se pasa del tiempo, se termina el árbol entero (no solo cmd.exe).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    const BOOL ok = CreateProcessW(nullptr, linea.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(escribirP);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        const DWORD e = GetLastError();
        CloseHandle(leer);
        if (job) CloseHandle(job);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo correr el comando (error %1)"), {std::to_string(e)}),
                              true);
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    // La salida se lee en otro hilo (si no, un comando que escribe mucho se traba esperando).
    std::string salida;
    constexpr size_t kMax = 200 * 1024;
    bool cortada = false;
    std::thread lector([&]() {
        ba::platform::nombrarHilo("control-lector");
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(leer, buf, sizeof(buf), &n, nullptr) && n > 0) {
            salida.append(buf, n);
            if (salida.size() > kMax) {
                salida.erase(0, salida.size() - kMax); // queda lo último
                cortada = true;
            }
        }
    });
    const DWORD r = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(segundos) * 1000);
    const bool vencido = r != WAIT_OBJECT_0;
    if (vencido) {
        if (job) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
    }
    DWORD codigo = 0;
    GetExitCodeProcess(pi.hProcess, &codigo);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job); // mata lo que haya quedado del árbol
    lector.join();
    CloseHandle(leer);
    json d{{"codigo", static_cast<long long>(codigo)}, {"salida", deConsola(salida)}};
    if (cortada) d["nota"] = ba::i18n::traducir("mcp", "salida larga: quedan los últimos 200 KB");
    if (vencido) d["vencido"] = ba::i18n::llenar(ba::i18n::traducir("mcp", "pasaron %1 s y se terminó"), {std::to_string(segundos)});
    return resultadoDatos(id, d);
}

// -- Datos del equipo --
json datosSistema() {
    json d;
    using RtlGetVersionF = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (auto f = reinterpret_cast<RtlGetVersionF>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
        f(&v);
    d["windows"] = {{"version", std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion)},
                    {"compilacion", static_cast<long long>(v.dwBuildNumber)},
                    {"nombre", v.dwBuildNumber >= 22000 ? "Windows 11" : v.dwMajorVersion >= 10 ? "Windows 10" : "Windows"}};
    wchar_t nombre[256];
    DWORD n = 256;
    if (GetComputerNameExW(ComputerNameDnsHostname, nombre, &n)) d["equipo"] = aU(nombre);
    wchar_t usuario[256];
    if (GetEnvironmentVariableW(L"USERNAME", usuario, 256) > 0) d["usuario"] = aU(usuario);
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    d["procesadores"] = static_cast<int>(si.dwNumberOfProcessors);
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        d["memoria"] = {{"total_gb", std::round(ms.ullTotalPhys / 1e8) / 10}, {"libre_gb", std::round(ms.ullAvailPhys / 1e8) / 10},
                        {"en_uso", static_cast<int>(ms.dwMemoryLoad)}};
    json discos = json::array();
    wchar_t letras[256];
    const DWORD largo = GetLogicalDriveStringsW(255, letras);
    for (const wchar_t* l = letras; largo > 0 && *l; l += wcslen(l) + 1) {
        const UINT tipo = GetDriveTypeW(l);
        if (tipo != DRIVE_FIXED && tipo != DRIVE_REMOVABLE) continue;
        ULARGE_INTEGER libre{}, total{};
        if (!GetDiskFreeSpaceExW(l, &libre, &total, nullptr)) continue;
        discos.push_back({{"unidad", aU(l)}, {"total_gb", std::round(total.QuadPart / 1e8) / 10},
                          {"libre_gb", std::round(libre.QuadPart / 1e8) / 10}, {"extraible", tipo == DRIVE_REMOVABLE}});
    }
    d["discos"] = discos;
    const ULONGLONG ms0 = GetTickCount64();
    d["prendido_hace"] = std::to_string(ms0 / 3600000) + " h " + std::to_string((ms0 / 60000) % 60) + " min";
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 128 && ps.BatteryFlag != 255)
        d["bateria"] = {{"porcentaje", static_cast<int>(ps.BatteryLifePercent)}, {"enchufada", ps.ACLineStatus == 1}};
    d["monitores"] = static_cast<int>(monitores().size());
    return d;
}

// --- Despacho en Windows --
json ejecutarHerramienta(const json& id, const std::string& nombre, const json& a, const Permisos& p) {
    json out;
    if (atenderUia(id, nombre, a, p, out) || atenderChrome(id, nombre, a, p, out) || atenderArchivos(id, nombre, a, p, out))
        return out;
    const auto noPermiso = [&](const std::string& que) { return sinPermiso(id, que.c_str()); };
    if (nombre == "system_pantalla") {
        if (!p.pantalla) return noPermiso("pantalla");
        const Virtual v;
        int x = v.x, y = v.y, w = v.w, h = v.h;
        if (a.contains("monitor")) {
            const auto ms = monitores();
            const int i = a.value("monitor", 0);
            if (i < 0 || i >= static_cast<int>(ms.size())) return resultadoTexto(id, ba::i18n::traducir("mcp", "no existe ese monitor"), true);
            x = ms[i].x; y = ms[i].y; w = ms[i].w; h = ms[i].h;
        }
        const double escala = a.value("escala", 1.0);
        std::string png, error;
        int sw = 0, sh = 0;
        if (a.contains("ventana") || a.contains("titulo")) {
            json b = a;
            if (a.contains("ventana")) b["id"] = a["ventana"];
            HWND hv = buscarVentana(b);
            if (hv == nullptr) return resultadoTexto(id, ba::i18n::traducir("mcp", "no encontré esa ventana (system_ventanas las lista)"), true);
            if (!capturarVentana(hv, escala, png, sw, sh, error)) return resultadoTexto(id, error, true);
            return resultadoImagen(id, png, ba::i18n::llenar(ba::i18n::traducir("mcp", "Ventana «%1»: %2x%3"),
                                                     {tituloDe(hv), std::to_string(sw), std::to_string(sh)}));
        }
        if (a.contains("ancho") && a.contains("alto")) {
            x = a.value("x", 0); y = a.value("y", 0); w = a.value("ancho", w); h = a.value("alto", h);
        }
        if (w <= 0 || h <= 0) return resultadoTexto(id, ba::i18n::traducir("mcp", "tamaño inválido"), true);
        if (!capturar(x, y, w, h, escala, png, sw, sh, error)) return resultadoTexto(id, error, true);
        return resultadoImagen(
            id, png,
            sw != w ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Captura de %1x%2 en %3,%4 (achicada a %5x%6)"),
                                       {std::to_string(w), std::to_string(h), std::to_string(x), std::to_string(y),
                                        std::to_string(sw), std::to_string(sh)})
                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "Captura de %1x%2 en %3,%4"),
                                       {std::to_string(w), std::to_string(h), std::to_string(x), std::to_string(y)}));
    }
    if (nombre == "system_monitores") {
        if (!p.pantalla) return noPermiso("pantalla");
        json out = json::array();
        int i = 0;
        for (const auto& m : monitores())
            out.push_back({{"indice", i++}, {"x", m.x}, {"y", m.y}, {"ancho", m.w}, {"alto", m.h}, {"principal", m.principal}});
        return resultadoDatos(id, {{"monitores", out}});
    }
    if (nombre == "system_mouse") {
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
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho. El mouse quedó en %1,%2, sobre «%3»."),
                                                   {std::to_string(pt.x), std::to_string(pt.y), tituloDe(raiz)}));
    }
    if (nombre == "system_teclado") {
        if (!p.teclado) return noPermiso("teclado");
        if (a.contains("texto")) escribirTexto(aW(a.value("texto", std::string())));
        if (a.contains("teclas") && !mandarTeclas(a.value("teclas", std::string())))
            return resultadoTexto(id, ba::i18n::traducir("mcp", "no entendí la combinación de teclas"), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho, %1."), {donde(GetForegroundWindow())}));
    }
    if (nombre == "system_ejecutar") {
        if (!p.ejecutar) return noPermiso("ejecutar");
        const bool admin = a.value("admin", false);
        if (admin && !p.ejecutar_admin) return noPermiso(ba::i18n::traducir("mcp", "ejecutar como administrador"));
        const std::string prog = a.value("programa", std::string());
        if (prog.empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta el programa"), true);
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
            if (e == ERROR_CANCELLED) return resultadoTexto(id, ba::i18n::traducir("mcp", "el usuario canceló el pedido de administrador (UAC)"), true);
            return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "no se pudo ejecutar «%1» (error %2)"), {prog, std::to_string(e)}),
                                  true);
        }
        DWORD pid = 0;
        if (ei.hProcess != nullptr) { pid = GetProcessId(ei.hProcess); CloseHandle(ei.hProcess); }
        const std::string texto = admin ? (pid ? ba::i18n::traducir("mcp", "Ejecutado «%1» como administrador (pid %2).")
                                               : ba::i18n::traducir("mcp", "Ejecutado «%1» como administrador."))
                                        : (pid ? ba::i18n::traducir("mcp", "Ejecutado «%1» (pid %2).")
                                               : ba::i18n::traducir("mcp", "Ejecutado «%1»."));
        return resultadoTexto(id, ba::i18n::llenar(texto, {prog, std::to_string(pid)}));
    }
    if (nombre == "system_buscar") {
        if (!p.buscar) return noPermiso("buscar");
        const std::string patron = a.value("patron", std::string());
        if (patron.empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta el patrón"), true);
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
    if (nombre == "system_ventanas") {
        if (!p.ventanas) return noPermiso("ventanas");
        json out = json::array();
        for (const auto& w : ventanas()) {
            const std::string app = ejecutableDe(w.pid);
            std::string al;
            for (char c : app) al.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            const bool nav = al == "chrome.exe" || al == "msedge.exe" || al == "brave.exe" || al == "firefox.exe" || al == "opera.exe";
            out.push_back({{"id", reinterpret_cast<long long>(w.hwnd)},
                           {"titulo", w.titulo},
                           {"app", app},
                           {"navegador", nav},
                           {"pid", static_cast<long long>(w.pid)},
                           {"x", w.rect.left},
                           {"y", w.rect.top},
                           {"ancho", w.rect.right - w.rect.left},
                           {"alto", w.rect.bottom - w.rect.top}});
        }
        return resultadoDatos(id, {{"ventanas", out}});
    }
    if (nombre == "system_ventana") {
        if (!p.ventanas) return noPermiso("ventanas");
        HWND h = buscarVentana(a);
        if (h == nullptr) return resultadoTexto(id, ba::i18n::traducir("mcp", "no encontré esa ventana"), true);
        const std::string accion = a.value("accion", std::string());
        if (accion == "activar") {
            if (!traerAlFrente(h))
                return resultadoTexto(id, ba::i18n::traducir("mcp", "Windows no dejó traer esa ventana al frente (probá con system_mouse "
                                                              "un clic sobre ella)"), true);
        }
        else if (accion == "minimizar") ShowWindow(h, SW_MINIMIZE);
        else if (accion == "maximizar") ShowWindow(h, SW_MAXIMIZE);
        else if (accion == "restaurar") ShowWindow(h, SW_RESTORE);
        else if (accion == "cerrar") PostMessageW(h, WM_CLOSE, 0, 0);
        else if (accion == "mover") {
            RECT r{};
            GetWindowRect(h, &r);
            if (IsZoomed(h) || IsIconic(h)) ShowWindow(h, SW_RESTORE), GetWindowRect(h, &r);
            const int x = a.value("x", static_cast<int>(r.left)), y = a.value("y", static_cast<int>(r.top));
            const int w = a.value("ancho", static_cast<int>(r.right - r.left)), alto = a.value("alto", static_cast<int>(r.bottom - r.top));
            if (w < 50 || alto < 30) return resultadoTexto(id, ba::i18n::traducir("mcp", "tamaño muy chico"), true);
            SetWindowPos(h, nullptr, x, y, w, alto, SWP_NOZORDER | SWP_NOACTIVATE);
            GetWindowRect(h, &r);
            return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Movida %1: %2,%3, %4x%5."),
                                                       {donde(h), std::to_string(r.left), std::to_string(r.top),
                                                        std::to_string(r.right - r.left), std::to_string(r.bottom - r.top)}));
        }
        else if (accion == "fijar_arriba") SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        else if (accion == "soltar_arriba") SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        else return resultadoTexto(id, ba::i18n::traducir("mcp", "acción desconocida"), true);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho %1."), {donde(h)}));
    }
    if (nombre == "system_portapapeles") {
        if (!p.portapapeles) return noPermiso("portapapeles");
        if (a.contains("texto")) {
            if (!portapapelesEscribir(aW(a.value("texto", std::string())))) return resultadoTexto(id, ba::i18n::traducir("mcp", "no se pudo escribir"), true);
            return resultadoTexto(id, ba::i18n::traducir("mcp", "Copiado al portapapeles."));
        }
        return resultadoDatos(id, {{"texto", portapapelesLeer()}});
    }
    if (nombre == "system_cursor") {
        if (!p.pantalla) return noPermiso("pantalla");
        POINT pt{};
        GetCursorPos(&pt);
        HWND bajo = WindowFromPoint(pt);
        HWND raiz = bajo != nullptr ? GetAncestor(bajo, GA_ROOT) : nullptr;
        HWND foco = GetForegroundWindow();
        int monitor = -1, i = 0;
        for (const auto& m : monitores()) {
            if (pt.x >= m.x && pt.x < m.x + m.w && pt.y >= m.y && pt.y < m.y + m.h) monitor = i;
            ++i;
        }
        // Si la persona está usando la máquina (hace cuánto tocó mouse o teclado): para no
        // pelearle el mouse.
        LASTINPUTINFO li{sizeof(li), 0};
        const double inactivo = GetLastInputInfo(&li) ? (GetTickCount() - li.dwTime) / 1000.0 : -1;
        return resultadoDatos(id, {{"x", pt.x}, {"y", pt.y}, {"monitor", monitor},
                                   {"inactivo_s", std::round(inactivo * 10) / 10},
                                   {"usandola", inactivo >= 0 && inactivo < 5},
                                   {"debajo", {{"id", reinterpret_cast<long long>(raiz)}, {"titulo", tituloDe(raiz)}}},
                                   {"foco", {{"id", reinterpret_cast<long long>(foco)}, {"titulo", tituloDe(foco)}}}});
    }
    if (nombre == "system_procesos") {
        if (!p.procesos) return noPermiso("procesos");
        return resultadoDatos(id, listarProcesos(a.value("nombre", std::string()), a.value("max", 200)));
    }
    if (nombre == "system_proceso_cerrar") {
        if (!p.procesos) return noPermiso("procesos");
        const DWORD pid = static_cast<DWORD>(a.value("pid", 0LL));
        if (pid <= 4) return resultadoTexto(id, ba::i18n::traducir("mcp", "ese proceso es del sistema: no se cierra"), true);
        if (pid == GetCurrentProcessId()) return resultadoTexto(id, ba::i18n::traducir("mcp", "ese soy yo (ba-control)"), true);
        return cerrarProceso(id, pid, a.value("forzar", false));
    }
    if (nombre == "system_comando") {
        if (!p.comandos) return noPermiso("comandos");
        const std::string cmd = a.value("comando", std::string());
        if (cmd.empty()) return resultadoTexto(id, ba::i18n::traducir("mcp", "falta el comando"), true);
        return correrComando(id, cmd, a.value("shell", std::string("sistema")), a.value("carpeta", std::string()),
                             std::clamp(a.value("segundos", 30), 1, 300));
    }
    if (nombre == "system_sistema") {
        if (!p.sistema) return noPermiso("sistema");
        return resultadoDatos(id, datosSistema());
    }
    if (nombre == "system_esperar") {
        const int ms = std::clamp(a.value("ms", a.contains("titulo") ? 10000 : 1000), 0, 30000);
        const std::string t = a.value("titulo", std::string());
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        if (t.empty()) {
            Sleep(static_cast<DWORD>(ms));
            return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: pasaron %1 ms."), {std::to_string(ms)}));
        }
        if (!p.ventanas) return noPermiso("ventanas");
        do {
            if (HWND h = buscarVentana(json{{"titulo", t}})) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Apareció %1."), {donde(h)}));
            Sleep(150);
        } while (std::chrono::steady_clock::now() < fin);
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "No apareció ninguna ventana con «%1» en %2 ms."),
                                              {t, std::to_string(ms)}), true);
    }
    return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "herramienta desconocida: %1"), {nombre}), true);
}
#elif !defined(__APPLE__) && !defined(__linux__)  // otro sistema (macOS: control_mac.mm; Linux: control_linux.cpp)
json ejecutarHerramienta(const json& id, const std::string& nombre, const json& a, const Permisos& p) {
    json out;
    // Los archivos y Chrome ya andan (POSIX y el protocolo de Chrome).
    if (atenderArchivos(id, nombre, a, p, out) || atenderChrome(id, nombre, a, p, out)) return out;
    return resultadoTexto(id, todaviaNo(nombre), true);
}
bool aLaPapelera(const std::string&, std::string& error) {
    error = ba::i18n::traducir("mcp", "la papelera de Linux todavía no está hecha");
    return false;
}
json registroSistema(const json& id, const json&, const Permisos&) { return resultadoTexto(id, todaviaNo("system_registro"), true); }
json avisoSistema(const json& id, const json&, const Permisos&) { return resultadoTexto(id, todaviaNo("system_aviso"), true); }
json ventanasDeNavegador() { return json::array(); }
#endif

json llamarHerramienta(const json& id, const std::string& nombre, const json& recibidos) {
    // Sin argumentos algunos clientes mandan null; cualquier otra cosa que no sea un objeto, se dice.
    const json a = recibidos.is_null() ? json::object() : recibidos;
    if (!a.is_object()) return resultadoTexto(id, ba::i18n::traducir("mcp", "`arguments` tiene que ser un objeto ({\"clave\": valor})"), true);
    const Permisos p = permisos();
    if (!p.habilitado)
        return resultadoTexto(id,
                              ba::i18n::traducir("mcp", "El control del sistema está deshabilitado (Configuración → Control "
                                                        "del sistema, o control_configurar {habilitado: true})."),
                              true);
    try {
        return ejecutarHerramienta(id, nombre, a, p);
    } catch (const std::exception& e) {
        return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "error: %1"), {e.what()}), true);
    }
}

} // namespace bactl

using namespace bactl;

int main() {
    // El idioma del operador (el de la máquina): las descripciones y las respuestas las lee el modelo en ese idioma.
    {
        const std::string raiz = bactl::dataRoot();
        ba::i18n::usar(ba::i18n::idiomaDeLaMaquina(raiz.empty() ? ba::platform::dataRoot() : std::filesystem::path(raiz)));
    }
#if defined(_WIN32)
    // El protocolo va en bytes: sin la conversión \n → \r\n del modo texto.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    SetProcessDPIAware(); // coordenadas en píxeles reales
    // UI Automation (control_uia) habla COM: el hilo principal, en un departamento múltiple.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
                         ba::i18n::traducir("mcp", "Control del escritorio de ESTA máquina Windows (mouse, teclado, captura, abrir "
                                                   "programas, buscar, ventanas, portapapeles, procesos, comandos, datos del "
                                                   "equipo). Mirá system_monitores y system_pantalla antes de mover el mouse "
                                                   "por coordenadas, y system_cursor antes de escribir (qué ventana tiene el "
                                                   "foco).")}}}});
        } else if (metodo == "tools/list") {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"tools", catalogo()}}}});
        } else if (metodo == "tools/call") {
            const json params = doc.value("params", json::object());
            escribir(llamarHerramienta(id, params.value("name", std::string()),
                                       params.value("arguments", json::object())));
        } else if (metodo == "ping") {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}});
        } else if (!notificacion) {
            escribir({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", ba::i18n::traducir("mcp", "método desconocido")}}}});
        }
    }
#if defined(_WIN32)
    if (gdiTok) Gdiplus::GdiplusShutdown(gdiTok);
#endif
    return 0;
}
