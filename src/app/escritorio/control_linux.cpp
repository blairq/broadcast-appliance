// ============================================================================
// src/app/escritorio/control_linux.cpp — el motor de Linux del control del sistema
//
// Las MISMAS herramientas system_* que en Windows y macOS, con lo de Linux:
//   pantalla, mouse, teclado, ventanas   X11 (Xlib, XTest, XRandR, XScreenSaver),
//                         CARGADAS AL USAR (dlopen): el binario no depende de ellas
//                         (portable); en Wayland anda con XWayland (DISPLAY), y
//                         sin X11 la herramienta lo dice. Ventanas por EWMH (lo que
//                         entienden todos los escritorios: activar, maximizar, siempre
//                         arriba, cerrar).
//   procesos, sistema     /proc, statvfs, /sys/class/power_supply, /etc/os-release.
//   ejecutar              xdg-open o el programa; como administrador, pkexec.
//   portapapeles          xclip / xsel / wl-clipboard (el que haya).
//   registro              no hay: las preferencias de GNOME (gsettings: `clave` =
//                         el esquema, `nombre` = la clave).
//   aviso                 la notificación estándar (notify-send o D-Bus por gdbus).
//   papelera              la de freedesktop (~/.local/share/Trash).
//   system_ui_*           AT-SPI: en camino (contesta eso).
// ============================================================================

#include "escritorio/control.hpp"

#if defined(__linux__)

#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <signal.h>
#include <spawn.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace bactl {

namespace {

namespace fs = std::filesystem;

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}
void dormir(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// --- Correr un programa y leer lo que contesta -------------------------------------------

struct Salida {
    int codigo = -1;
    std::string texto;
    bool vencido = false, cortada = false;
};

/// Corre `argv` (sin shell), con `entrada` por stdin si hay; espera hasta `segundos`.
Salida correr(const std::vector<std::string>& argv, int segundos, const std::string& carpeta = {},
              const std::string& entrada = {}, bool grupo = true) {
    Salida r;
    int sal[2], ent[2];
    if (pipe(sal) != 0 || pipe(ent) != 0) return r;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sal[1], 1);
    posix_spawn_file_actions_adddup2(&fa, sal[1], 2);
    posix_spawn_file_actions_adddup2(&fa, ent[0], 0);
    posix_spawn_file_actions_addclose(&fa, sal[0]);
    posix_spawn_file_actions_addclose(&fa, ent[1]);
    if (!carpeta.empty()) posix_spawn_file_actions_addchdir_np(&fa, carpeta.c_str());
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    if (grupo) {
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&at, 0);
    }
    std::vector<char*> av;
    for (const auto& x : argv) av.push_back(const_cast<char*>(x.c_str()));
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, av[0], &fa, &at, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(sal[1]);
    close(ent[0]);
    if (rc != 0) {
        close(sal[0]);
        close(ent[1]);
        r.texto = "no se pudo correr " + argv[0];
        return r;
    }
    if (!entrada.empty()) {
        const ssize_t n = write(ent[1], entrada.data(), entrada.size());
        (void)n;
    }
    close(ent[1]);
    fcntl(sal[0], F_SETFL, O_NONBLOCK);
    const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(segundos);
    int estado = 0;
    bool termino = false;
    char buf[8192];
    while (true) {
        const ssize_t n = read(sal[0], buf, sizeof(buf));
        if (n > 0) {
            r.texto.append(buf, static_cast<size_t>(n));
            if (r.texto.size() > 200 * 1024) {
                r.texto.erase(0, r.texto.size() - 200 * 1024);
                r.cortada = true;
            }
            continue;
        }
        if (!termino && waitpid(pid, &estado, WNOHANG) == pid) termino = true;
        if (termino && n == 0) break;
        if (std::chrono::steady_clock::now() > fin) {
            r.vencido = true;
            kill(grupo ? -pid : pid, SIGTERM);
            dormir(500);
            kill(grupo ? -pid : pid, SIGKILL);
            if (!termino) waitpid(pid, &estado, 0);
            break;
        }
        dormir(20);
    }
    close(sal[0]);
    r.codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
    return r;
}

std::string enPath(const std::string& prog) {
    if (prog.find('/') != std::string::npos) return access(prog.c_str(), X_OK) == 0 ? prog : std::string();
    const char* p = std::getenv("PATH");
    std::stringstream ss(p ? p : "/usr/bin:/bin:/usr/local/bin");
    std::string d;
    while (std::getline(ss, d, ':'))
        if (!d.empty() && access((d + "/" + prog).c_str(), X_OK) == 0) return d + "/" + prog;
    return {};
}

std::string leerArchivo(const std::string& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// --- X11, cargado al usar ------------------------------------------------------------------

// Lo mínimo de Xlib (su ABI es estable desde hace décadas): así compila sin sus headers
// y el binario no las necesita si nadie usa la pantalla.
using Display = struct _XDisplay;
using Window = unsigned long;
using Atom = unsigned long;
using KeySym = unsigned long;
using KeyCode = unsigned char;
using Bool = int;
using Status = int;
struct XImage;
struct XImageFuncs {
    XImage* (*create_image)(...);
    int (*destroy_image)(XImage*);
    unsigned long (*get_pixel)(XImage*, int, int);
    int (*put_pixel)(XImage*, int, int, unsigned long);
    XImage* (*sub_image)(XImage*, int, int, unsigned int, unsigned int);
    int (*add_pixel)(XImage*, long);
};
struct XImage {
    int width, height, xoffset, format;
    char* data;
    int byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
    char* obdata;
    XImageFuncs f;
};
struct XClientMessageEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
};
union XEvent {
    int type;
    XClientMessageEvent xclient;
    long pad[24];
};
struct XRRMonitorInfo {
    Atom name;
    Bool primary, automatic;
    int noutput, x, y, width, height, mwidth, mheight;
    unsigned long* outputs;
};
struct XScreenSaverInfo {
    Window window;
    int state, kind;
    unsigned long til_or_since, idle, eventMask;
};
constexpr long kAllPlanes = ~0L;
constexpr int kZPixmap = 2, kClientMessage = 33, kSubstructureMask = (1L << 19) | (1L << 20);
constexpr Atom kAnyPropertyType = 0;

struct X11 {
    void *x = nullptr, *xtst = nullptr, *xrr = nullptr, *xss = nullptr;
    Display* d = nullptr;
    Window raiz = 0;
    Display* (*XOpenDisplay)(const char*) = nullptr;
    Window (*XDefaultRootWindow)(Display*) = nullptr;
    XImage* (*XGetImage)(Display*, Window, int, int, unsigned, unsigned, unsigned long, int) = nullptr;
    Bool (*XQueryPointer)(Display*, Window, Window*, Window*, int*, int*, int*, int*, unsigned*) = nullptr;
    Atom (*XInternAtom)(Display*, const char*, Bool) = nullptr;
    int (*XGetWindowProperty)(Display*, Window, Atom, long, long, Bool, Atom, Atom*, int*, unsigned long*, unsigned long*,
                              unsigned char**) = nullptr;
    int (*XFree)(void*) = nullptr;
    Status (*XGetGeometry)(Display*, Window, Window*, int*, int*, unsigned*, unsigned*, unsigned*, unsigned*) = nullptr;
    Bool (*XTranslateCoordinates)(Display*, Window, Window, int, int, int*, int*, Window*) = nullptr;
    Status (*XSendEvent)(Display*, Window, Bool, long, XEvent*) = nullptr;
    int (*XMoveResizeWindow)(Display*, Window, int, int, unsigned, unsigned) = nullptr;
    Status (*XIconifyWindow)(Display*, Window, int) = nullptr;
    int (*XFlush)(Display*) = nullptr;
    int (*XSync)(Display*, Bool) = nullptr;
    int (*XDisplayWidth)(Display*, int) = nullptr;
    int (*XDisplayHeight)(Display*, int) = nullptr;
    KeyCode (*XKeysymToKeycode)(Display*, KeySym) = nullptr;
    int (*XDisplayKeycodes)(Display*, int*, int*) = nullptr;
    KeySym* (*XGetKeyboardMapping)(Display*, KeyCode, int, int*) = nullptr;
    int (*XChangeKeyboardMapping)(Display*, int, int, KeySym*, int) = nullptr;
    int (*XTestFakeMotionEvent)(Display*, int, int, int, unsigned long) = nullptr;
    int (*XTestFakeButtonEvent)(Display*, unsigned, Bool, unsigned long) = nullptr;
    int (*XTestFakeKeyEvent)(Display*, unsigned, Bool, unsigned long) = nullptr;
    XRRMonitorInfo* (*XRRGetMonitors)(Display*, Window, Bool, int*) = nullptr;
    void (*XRRFreeMonitors)(XRRMonitorInfo*) = nullptr;
    XScreenSaverInfo* (*XScreenSaverAllocInfo)() = nullptr;
    Status (*XScreenSaverQueryInfo)(Display*, Window, XScreenSaverInfo*) = nullptr;
    std::string error;

    template <class F>
    bool cargar(void* lib, const char* n, F& f) {
        f = lib ? reinterpret_cast<F>(dlsym(lib, n)) : nullptr;
        return f != nullptr;
    }
    bool abrir() {
        if (d) return true;
        if (std::getenv("DISPLAY") == nullptr) {
            error = std::getenv("WAYLAND_DISPLAY")
                        ? "Wayland sin X11: la pantalla, el mouse y el teclado necesitan XWayland (que esté definido DISPLAY)."
                        : "no hay pantalla gráfica (DISPLAY sin definir)";
            return false;
        }
        x = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
        if (!x) {
            error = "no está libX11 en este sistema (instalá libx11-6)";
            return false;
        }
        xtst = dlopen("libXtst.so.6", RTLD_NOW);
        xrr = dlopen("libXrandr.so.2", RTLD_NOW);
        xss = dlopen("libXss.so.1", RTLD_NOW);
        bool ok = cargar(x, "XOpenDisplay", XOpenDisplay) && cargar(x, "XDefaultRootWindow", XDefaultRootWindow) &&
                  cargar(x, "XGetImage", XGetImage) && cargar(x, "XQueryPointer", XQueryPointer) &&
                  cargar(x, "XInternAtom", XInternAtom) && cargar(x, "XGetWindowProperty", XGetWindowProperty) &&
                  cargar(x, "XFree", XFree) && cargar(x, "XGetGeometry", XGetGeometry) &&
                  cargar(x, "XTranslateCoordinates", XTranslateCoordinates) && cargar(x, "XSendEvent", XSendEvent) &&
                  cargar(x, "XMoveResizeWindow", XMoveResizeWindow) && cargar(x, "XIconifyWindow", XIconifyWindow) &&
                  cargar(x, "XFlush", XFlush) && cargar(x, "XSync", XSync) && cargar(x, "XDisplayWidth", XDisplayWidth) &&
                  cargar(x, "XDisplayHeight", XDisplayHeight) && cargar(x, "XKeysymToKeycode", XKeysymToKeycode) &&
                  cargar(x, "XDisplayKeycodes", XDisplayKeycodes) && cargar(x, "XGetKeyboardMapping", XGetKeyboardMapping) &&
                  cargar(x, "XChangeKeyboardMapping", XChangeKeyboardMapping);
        if (!ok) {
            error = "libX11 no tiene lo que hace falta";
            return false;
        }
        cargar(xtst, "XTestFakeMotionEvent", XTestFakeMotionEvent);
        cargar(xtst, "XTestFakeButtonEvent", XTestFakeButtonEvent);
        cargar(xtst, "XTestFakeKeyEvent", XTestFakeKeyEvent);
        cargar(xrr, "XRRGetMonitors", XRRGetMonitors);
        cargar(xrr, "XRRFreeMonitors", XRRFreeMonitors);
        cargar(xss, "XScreenSaverAllocInfo", XScreenSaverAllocInfo);
        cargar(xss, "XScreenSaverQueryInfo", XScreenSaverQueryInfo);
        d = XOpenDisplay(nullptr);
        if (!d) {
            error = "no se pudo abrir la pantalla X11 (DISPLAY=" + std::string(std::getenv("DISPLAY")) + ")";
            return false;
        }
        raiz = XDefaultRootWindow(d);
        return true;
    }
    bool xtest() {
        if (!XTestFakeKeyEvent) error = "falta XTest (instalá libxtst6) para el mouse y el teclado";
        return XTestFakeKeyEvent != nullptr;
    }
    Atom atomo(const char* n) { return XInternAtom(d, n, 0); }
};

X11& x11() {
    static X11 x;
    return x;
}

// --- Monitores --------------------------------------------------------------------------

struct Monitor {
    int x, y, w, h;
    bool principal;
};

std::vector<Monitor> monitores() {
    X11& x = x11();
    std::vector<Monitor> out;
    if (x.XRRGetMonitors) {
        int n = 0;
        if (XRRMonitorInfo* m = x.XRRGetMonitors(x.d, x.raiz, 1, &n)) {
            for (int i = 0; i < n; ++i) out.push_back({m[i].x, m[i].y, m[i].width, m[i].height, m[i].primary != 0});
            x.XRRFreeMonitors(m);
        }
    }
    if (out.empty()) out.push_back({0, 0, x.XDisplayWidth(x.d, 0), x.XDisplayHeight(x.d, 0), true});
    std::stable_sort(out.begin(), out.end(), [](const Monitor& a, const Monitor& b) { return a.principal > b.principal; });
    return out;
}

// --- PNG sin bibliotecas (deflate «stored»: grande pero sin dependencias; usar `escala`) ---

uint32_t crc32(const unsigned char* d, size_t n, uint32_t c = 0) {
    static uint32_t t[256];
    static bool hecho = false;
    if (!hecho) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t k = i;
            for (int j = 0; j < 8; ++j) k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
            t[i] = k;
        }
        hecho = true;
    }
    c = ~c;
    for (size_t i = 0; i < n; ++i) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

std::string png(const std::vector<unsigned char>& rgb, int w, int h) {
    std::string out("\x89PNG\r\n\x1a\n", 8);
    const auto be32 = [](std::string& s, uint32_t v) {
        for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
    };
    const auto chunk = [&](const char* tipo, const std::string& datos) {
        be32(out, static_cast<uint32_t>(datos.size()));
        std::string c(tipo, 4);
        c += datos;
        out += c;
        be32(out, crc32(reinterpret_cast<const unsigned char*>(c.data()), c.size()));
    };
    std::string ihdr;
    be32(ihdr, static_cast<uint32_t>(w));
    be32(ihdr, static_cast<uint32_t>(h));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5); // 8 bits, RGB
    chunk("IHDR", ihdr);
    std::string crudo;
    crudo.reserve(static_cast<size_t>((w * 3 + 1) * h));
    for (int y = 0; y < h; ++y) {
        crudo.push_back(0);
        crudo.append(reinterpret_cast<const char*>(&rgb[static_cast<size_t>(y * w * 3)]), static_cast<size_t>(w * 3));
    }
    std::string z("\x78\x01", 2);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < crudo.size(); i += 65535) {
        const size_t n = std::min<size_t>(65535, crudo.size() - i);
        z.push_back(i + n >= crudo.size() ? 1 : 0);
        z.push_back(static_cast<char>(n & 0xFF));
        z.push_back(static_cast<char>(n >> 8));
        z.push_back(static_cast<char>(~n & 0xFF));
        z.push_back(static_cast<char>((~n >> 8) & 0xFF));
        z.append(crudo, i, n);
    }
    for (unsigned char c : crudo) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", "");
    return base64(reinterpret_cast<const unsigned char*>(out.data()), out.size());
}

bool capturar(int x0, int y0, int w, int h, double escala, std::string& b64, int& sw, int& sh, std::string& error) {
    X11& x = x11();
    XImage* img = x.XGetImage(x.d, x.raiz, x0, y0, static_cast<unsigned>(w), static_cast<unsigned>(h), kAllPlanes, kZPixmap);
    if (!img) {
        error = "X11 no dio la imagen (¿la región se sale de la pantalla?)";
        return false;
    }
    sw = std::max(1, static_cast<int>(w * escala));
    sh = std::max(1, static_cast<int>(h * escala));
    std::vector<unsigned char> rgb(static_cast<size_t>(sw * sh * 3));
    const auto corrimiento = [](unsigned long m) {
        int s = 0;
        while (m && !(m & 1)) m >>= 1, ++s;
        return s;
    };
    const int sr = corrimiento(img->red_mask), sg = corrimiento(img->green_mask), sb = corrimiento(img->blue_mask);
    for (int y = 0; y < sh; ++y)
        for (int xx = 0; xx < sw; ++xx) {
            const unsigned long p = img->f.get_pixel(img, xx * w / sw, y * h / sh);
            unsigned char* o = &rgb[static_cast<size_t>((y * sw + xx) * 3)];
            o[0] = static_cast<unsigned char>((p & img->red_mask) >> sr);
            o[1] = static_cast<unsigned char>((p & img->green_mask) >> sg);
            o[2] = static_cast<unsigned char>((p & img->blue_mask) >> sb);
        }
    img->f.destroy_image(img);
    b64 = png(rgb, sw, sh);
    return true;
}

// --- Ventanas (EWMH) ----------------------------------------------------------------------

std::vector<unsigned char> propiedad(Window w, const char* nombre, Atom tipo = kAnyPropertyType) {
    X11& x = x11();
    Atom real = 0;
    int formato = 0;
    unsigned long n = 0, resto = 0;
    unsigned char* datos = nullptr;
    std::vector<unsigned char> out;
    if (x.XGetWindowProperty(x.d, w, x.atomo(nombre), 0, 1 << 20, 0, tipo, &real, &formato, &n, &resto, &datos) == 0 && datos) {
        const size_t tam = formato == 32 ? sizeof(long) : static_cast<size_t>(formato / 8);
        out.assign(datos, datos + n * tam);
        x.XFree(datos);
    }
    return out;
}

std::vector<Window> listaVentanas(Window w, const char* nombre) {
    const auto p = propiedad(w, nombre);
    std::vector<Window> out(p.size() / sizeof(long));
    for (size_t i = 0; i < out.size(); ++i) {
        long v;
        std::memcpy(&v, &p[i * sizeof(long)], sizeof(long));
        out[i] = static_cast<Window>(v);
    }
    return out;
}

struct Ventana {
    Window id = 0;
    std::string titulo, app;
    long pid = 0;
    int x = 0, y = 0, w = 0, h = 0;
};

Ventana datosVentana(Window w) {
    X11& x = x11();
    Ventana v;
    v.id = w;
    const auto t = propiedad(w, "_NET_WM_NAME");
    v.titulo = t.empty() ? std::string() : std::string(t.begin(), t.end());
    if (v.titulo.empty()) {
        const auto t2 = propiedad(w, "WM_NAME");
        v.titulo = std::string(t2.begin(), t2.end());
    }
    const auto pid = propiedad(w, "_NET_WM_PID");
    if (pid.size() >= sizeof(long)) std::memcpy(&v.pid, pid.data(), sizeof(long));
    const auto cls = propiedad(w, "WM_CLASS"); // «instancia\0Clase\0»
    if (!cls.empty()) {
        const std::string c(cls.begin(), cls.end());
        const auto cero = c.find('\0');
        v.app = cero != std::string::npos && cero + 1 < c.size() ? c.substr(cero + 1, c.find('\0', cero + 1) - cero - 1) : c;
    }
    if (v.app.empty() && v.pid > 0) {
        v.app = leerArchivo("/proc/" + std::to_string(v.pid) + "/comm");
        while (!v.app.empty() && v.app.back() == '\n') v.app.pop_back();
    }
    Window r = 0, hijo = 0;
    int gx = 0, gy = 0;
    unsigned gw = 0, gh = 0, borde = 0, prof = 0;
    if (x.XGetGeometry(x.d, w, &r, &gx, &gy, &gw, &gh, &borde, &prof)) {
        x.XTranslateCoordinates(x.d, w, x.raiz, 0, 0, &gx, &gy, &hijo);
        v.x = gx;
        v.y = gy;
        v.w = static_cast<int>(gw);
        v.h = static_cast<int>(gh);
    }
    return v;
}

/// Las ventanas de las aplicaciones, de adelante hacia atrás.
std::vector<Ventana> ventanas() {
    X11& x = x11();
    std::vector<Window> ws = listaVentanas(x.raiz, "_NET_CLIENT_LIST_STACKING");
    if (ws.empty()) ws = listaVentanas(x.raiz, "_NET_CLIENT_LIST");
    std::reverse(ws.begin(), ws.end());
    std::vector<Ventana> out;
    for (Window w : ws) {
        Ventana v = datosVentana(w);
        if (v.w < 40 || v.h < 30) continue;
        out.push_back(v);
    }
    return out;
}

std::string tituloDe(const Ventana& v) { return v.titulo.empty() ? v.app : v.titulo + " — " + v.app; }

bool buscarVentana(const json& a, Ventana& out) {
    const auto vs = ventanas();
    if (a.contains("id") && a["id"].is_number()) {
        const auto id = static_cast<Window>(a.value("id", 0LL));
        for (const auto& v : vs)
            if (v.id == id) return out = v, true;
        return false;
    }
    const std::string t = minusculas(a.value("titulo", std::string()));
    if (t.empty()) return false;
    for (const auto& v : vs)
        if (minusculas(v.titulo).find(t) != std::string::npos || minusculas(v.app).find(t) != std::string::npos)
            return out = v, true;
    return false;
}

Window activa() {
    const auto l = listaVentanas(x11().raiz, "_NET_ACTIVE_WINDOW");
    return l.empty() ? 0 : l[0];
}

/// Un pedido EWMH al gestor de ventanas (_NET_ACTIVE_WINDOW, _NET_WM_STATE…).
void mensajeWm(Window w, const char* tipo, long d0, long d1 = 0, long d2 = 0, long d3 = 0) {
    X11& x = x11();
    XEvent e{};
    e.xclient.type = kClientMessage;
    e.xclient.send_event = 1;
    e.xclient.display = x.d;
    e.xclient.window = w;
    e.xclient.message_type = x.atomo(tipo);
    e.xclient.format = 32;
    e.xclient.data.l[0] = d0;
    e.xclient.data.l[1] = d1;
    e.xclient.data.l[2] = d2;
    e.xclient.data.l[3] = d3;
    x.XSendEvent(x.d, x.raiz, 0, kSubstructureMask, &e);
    x.XFlush(x.d);
}

// --- Mouse y teclado (XTest) -------------------------------------------------------------

void moverMouse(int px, int py) {
    X11& x = x11();
    x.XTestFakeMotionEvent(x.d, -1, px, py, 0);
    x.XFlush(x.d);
}
void botonMouse(unsigned b, bool abajo) {
    X11& x = x11();
    x.XTestFakeButtonEvent(x.d, b, abajo, 0);
    x.XFlush(x.d);
}
unsigned numBoton(const std::string& n) { return n == "der" ? 3 : n == "medio" ? 2 : 1; }

void pulsar(KeyCode k, bool abajo) {
    X11& x = x11();
    x.XTestFakeKeyEvent(x.d, k, abajo, 0);
}

/// Una tecla libre para escribir cualquier carácter (se le asigna el que haga falta).
KeyCode teclaLibre() {
    X11& x = x11();
    static KeyCode libre = 0;
    if (libre) return libre;
    int min = 8, max = 255, por = 0;
    x.XDisplayKeycodes(x.d, &min, &max);
    KeySym* m = x.XGetKeyboardMapping(x.d, static_cast<KeyCode>(min), max - min + 1, &por);
    for (int k = max; k >= min && m; --k) {
        bool vacia = true;
        for (int j = 0; j < por; ++j) vacia = vacia && m[(k - min) * por + j] == 0;
        if (vacia) {
            libre = static_cast<KeyCode>(k);
            break;
        }
    }
    if (m) x.XFree(m);
    return libre;
}

void escribirTexto(const std::string& s) {
    X11& x = x11();
    const KeyCode k = teclaLibre();
    if (!k) return;
    for (size_t i = 0; i < s.size();) {
        // UTF-8 → punto de código → keysym (Latin-1 directo, el resto 0x01000000 + cp).
        uint32_t cp = static_cast<unsigned char>(s[i]);
        int n = cp < 0x80 ? 1 : cp < 0xE0 ? 2 : cp < 0xF0 ? 3 : 4;
        if (n > 1) cp &= (0xFF >> (n + 1));
        for (int j = 1; j < n && i + static_cast<size_t>(j) < s.size(); ++j) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(j)]) & 0x3F);
        i += static_cast<size_t>(n);
        KeySym ks = cp == '\n' ? 0xff0d : cp == '\t' ? 0xff09 : cp < 0x100 ? cp : 0x01000000 | cp;
        KeySym par[2] = {ks, ks};
        x.XChangeKeyboardMapping(x.d, k, 2, par, 1);
        x.XSync(x.d, 0);
        pulsar(k, true);
        pulsar(k, false);
        x.XSync(x.d, 0);
        dormir(6);
    }
    KeySym nada[2] = {0, 0};
    x.XChangeKeyboardMapping(x.d, k, 2, nada, 1);
    x.XFlush(x.d);
}

/// «mod+c», «ctrl+alt+t», «enter», «f5»… `mod` es Control (la tecla de los atajos en Linux);
/// `super`, `win`, `cmd` y `meta` son la tecla Super.
bool mandarTeclas(const std::string& combo) {
    X11& x = x11();
    static const std::map<std::string, KeySym> nombres = {
        {"ctrl", 0xffe3}, {"control", 0xffe3}, {"mod", 0xffe3}, {"alt", 0xffe9}, {"option", 0xffe9}, {"opcion", 0xffe9},
        {"shift", 0xffe1}, {"mayus", 0xffe1}, {"super", 0xffeb}, {"win", 0xffeb}, {"cmd", 0xffeb}, {"command", 0xffeb},
        {"meta", 0xffeb}, {"enter", 0xff0d}, {"return", 0xff0d}, {"tab", 0xff09}, {"esc", 0xff1b}, {"escape", 0xff1b},
        {"space", 0x20}, {"espacio", 0x20}, {"backspace", 0xff08}, {"borrar", 0xff08}, {"delete", 0xffff}, {"supr", 0xffff},
        {"left", 0xff51}, {"izquierda", 0xff51}, {"up", 0xff52}, {"arriba", 0xff52}, {"right", 0xff53}, {"derecha", 0xff53},
        {"down", 0xff54}, {"abajo", 0xff54}, {"home", 0xff50}, {"inicio", 0xff50}, {"end", 0xff57}, {"fin", 0xff57},
        {"pageup", 0xff55}, {"pagedown", 0xff56}, {"print", 0xff61}};
    std::vector<KeyCode> ks;
    std::string parte;
    for (char c : minusculas(combo) + "+") {
        if (c == '+' && !parte.empty()) {
            KeySym s = 0;
            if (const auto it = nombres.find(parte); it != nombres.end()) s = it->second;
            else if (parte.size() == 1) s = static_cast<unsigned char>(parte[0]);
            else if (parte[0] == 'f' && parte.size() <= 3 && std::isdigit(static_cast<unsigned char>(parte[1])))
                s = 0xffbe + static_cast<KeySym>(std::atoi(parte.c_str() + 1) - 1);
            else return false;
            const KeyCode k = x.XKeysymToKeycode(x.d, s);
            if (!k) return false;
            ks.push_back(k);
            parte.clear();
        } else if (c != ' ')
            parte.push_back(c);
    }
    if (ks.empty()) return false;
    for (KeyCode k : ks) pulsar(k, true);
    for (auto it = ks.rbegin(); it != ks.rend(); ++it) pulsar(*it, false);
    x.XFlush(x.d);
    return true;
}

// --- Procesos y datos del equipo -----------------------------------------------------------

std::string nombreProceso(long pid) {
    std::string n = leerArchivo("/proc/" + std::to_string(pid) + "/comm");
    while (!n.empty() && n.back() == '\n') n.pop_back();
    return n;
}

json listarProcesos(const std::string& filtro, int max) {
    std::set<long> conVentana;
    if (x11().d)
        for (const auto& v : ventanas()) conVentana.insert(v.pid);
    struct P {
        long pid;
        std::string nombre;
        double mb;
    };
    std::vector<P> ps;
    const long pagina = sysconf(_SC_PAGESIZE);
    const std::string fl = minusculas(filtro);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/proc", ec)) {
        const std::string n = e.path().filename().string();
        if (n.empty() || !std::all_of(n.begin(), n.end(), ::isdigit)) continue;
        const long pid = std::atol(n.c_str());
        const std::string nom = nombreProceso(pid);
        if (nom.empty() || (!fl.empty() && minusculas(nom).find(fl) == std::string::npos)) continue;
        long tam = 0, res = 0;
        std::istringstream(leerArchivo("/proc/" + n + "/statm")) >> tam >> res;
        ps.push_back({pid, nom, res * pagina / 1048576.0});
    }
    std::sort(ps.begin(), ps.end(), [](const P& a, const P& b) { return a.mb > b.mb; });
    json out = json::array();
    for (size_t i = 0; i < ps.size() && static_cast<int>(i) < max; ++i)
        out.push_back({{"pid", ps[i].pid}, {"nombre", ps[i].nombre}, {"memoria_mb", std::round(ps[i].mb * 10) / 10},
                       {"ventana", conVentana.count(ps[i].pid) > 0}});
    return {{"procesos", out}, {"total", ps.size()}};
}

bool vivo(long pid) { return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM; }

json datosSistema() {
    json d;
    std::string so = "Linux";
    std::istringstream os(leerArchivo("/etc/os-release"));
    for (std::string l; std::getline(os, l);)
        if (l.rfind("PRETTY_NAME=", 0) == 0) {
            so = l.substr(12);
            so.erase(std::remove(so.begin(), so.end(), '"'), so.end());
        }
    std::string kernel = leerArchivo("/proc/sys/kernel/osrelease");
    while (!kernel.empty() && kernel.back() == '\n') kernel.pop_back();
    d["linux"] = {{"nombre", so}, {"kernel", kernel}};
    char host[256] = {0};
    gethostname(host, sizeof(host));
    d["equipo"] = host;
    if (const char* u = std::getenv("USER")) d["usuario"] = u;
    d["procesadores"] = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    std::istringstream mi(leerArchivo("/proc/meminfo"));
    double total = 0, libre = 0;
    for (std::string k; mi >> k;) {
        double v = 0;
        std::string unidad;
        mi >> v >> unidad;
        if (k == "MemTotal:") total = v * 1024;
        if (k == "MemAvailable:") libre = v * 1024;
    }
    d["memoria"] = {{"total_gb", std::round(total / 1e8) / 10}, {"libre_gb", std::round(libre / 1e8) / 10},
                    {"en_uso", total > 0 ? static_cast<int>(std::lround(100.0 * (1.0 - libre / total))) : 0}};
    json discos = json::array();
    std::istringstream mounts(leerArchivo("/proc/mounts"));
    static const std::set<std::string> reales = {"ext4", "ext3", "btrfs", "xfs", "vfat", "exfat", "ntfs", "ntfs3", "f2fs", "zfs", "fuseblk"};
    std::set<std::string> vistos;
    for (std::string dev, punto, tipo, resto; mounts >> dev >> punto >> tipo && std::getline(mounts, resto);) {
        std::error_code ed;
        if (!reales.count(tipo) || vistos.count(dev) || !fs::is_directory(punto, ed)) continue; // un archivo montado (Docker) no es un disco
        vistos.insert(dev);
        struct statvfs s{};
        if (statvfs(punto.c_str(), &s) != 0) continue;
        discos.push_back({{"unidad", punto},
                          {"total_gb", std::round(static_cast<double>(s.f_blocks) * s.f_frsize / 1e8) / 10},
                          {"libre_gb", std::round(static_cast<double>(s.f_bavail) * s.f_frsize / 1e8) / 10},
                          {"extraible", punto.rfind("/media/", 0) == 0 || punto.rfind("/run/media/", 0) == 0}});
    }
    d["discos"] = discos;
    const double up = std::atof(leerArchivo("/proc/uptime").c_str());
    d["prendido_hace"] = std::to_string(static_cast<long>(up) / 3600) + " h " + std::to_string((static_cast<long>(up) / 60) % 60) + " min";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/power_supply", ec)) {
        if (leerArchivo((e.path() / "type").string()).rfind("Battery", 0) != 0) continue;
        const int cap = std::atoi(leerArchivo((e.path() / "capacity").string()).c_str());
        const std::string est = leerArchivo((e.path() / "status").string());
        d["bateria"] = {{"porcentaje", cap}, {"enchufada", est.rfind("Discharging", 0) != 0}};
    }
    if (x11().d) d["monitores"] = static_cast<int>(monitores().size());
    return d;
}

void buscarEn(const fs::path& dir, const std::string& patron, std::vector<std::string>& out, int max) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && static_cast<int>(out.size()) < max; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (!n.empty() && n[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (fnmatch(patron.c_str(), n.c_str(), FNM_CASEFOLD) == 0) out.push_back(it->path().string());
    }
}

/// El primero de estos programas que esté instalado, o vacío.
std::string programa(std::initializer_list<const char*> l) {
    for (const char* p : l)
        if (const std::string r = enPath(p); !r.empty()) return r;
    return {};
}

} // namespace

// --- Papelera, preferencias, avisos y navegadores (lo que cada sistema hace a su manera) ----

bool aLaPapelera(const std::string& ruta, std::string& error) {
    // La papelera de freedesktop: files/<nombre> + info/<nombre>.trashinfo (la ven Nautilus,
    // Dolphin, Thunar…).
    const char* datos = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const fs::path base = datos && *datos ? fs::path(datos) / "Trash" : fs::path(home ? home : "/tmp") / ".local/share/Trash";
    std::error_code ec;
    fs::create_directories(base / "files", ec);
    fs::create_directories(base / "info", ec);
    const fs::path origen = fs::absolute(ruta, ec);
    std::string nombre = origen.filename().string();
    for (int i = 2; fs::exists(base / "files" / nombre, ec) || fs::exists(base / "info" / (nombre + ".trashinfo"), ec); ++i)
        nombre = origen.stem().string() + "." + std::to_string(i) + origen.extension().string();
    char fecha[32];
    const std::time_t t = std::time(nullptr);
    std::tm l{};
    localtime_r(&t, &l);
    std::strftime(fecha, sizeof(fecha), "%Y-%m-%dT%H:%M:%S", &l);
    std::ofstream(base / "info" / (nombre + ".trashinfo")) << "[Trash Info]\nPath=" << origen.string() << "\nDeletionDate=" << fecha << "\n";
    fs::rename(origen, base / "files" / nombre, ec);
    if (ec) { // otro disco: copiar y borrar
        ec.clear();
        fs::copy(origen, base / "files" / nombre, fs::copy_options::recursive, ec);
        if (!ec) fs::remove_all(origen, ec);
    }
    if (ec) {
        fs::remove(base / "info" / (nombre + ".trashinfo"), ec);
        error = ec.message();
        return false;
    }
    return true;
}

/// system_registro en Linux: no hay registro; las preferencias de GNOME (gsettings):
/// `clave` = el esquema (org.gnome.desktop.interface), `nombre` = la clave.
json registroSistema(const json& id, const json& a, const Permisos& p) {
    const std::string accion = a.value("accion", std::string());
    const bool escribe = accion == "escribir" || accion == "borrar";
    if (escribe ? !p.registro : !p.sistema) return sinPermiso(id, escribe ? "registro" : "sistema");
    const std::string gs = programa({"gsettings"});
    if (gs.empty()) return resultadoTexto(id, "En Linux no hay registro: system_registro usa las preferencias de GNOME (gsettings), y acá no está.", true);
    const std::string esq = a.value("clave", std::string()), nombre = a.value("nombre", std::string());
    if (esq.empty() || esq.rfind("HK", 0) == 0)
        return resultadoTexto(id, "En Linux `clave` es el esquema de las preferencias (org.gnome.desktop.interface…) y `nombre` la clave.", true);
    Salida s;
    if (accion == "listar") s = correr({gs, "list-recursively", esq}, 10);
    else if (nombre.empty()) return resultadoTexto(id, "falta `nombre`: la clave", true);
    else if (accion == "leer") s = correr({gs, "get", esq, nombre}, 10);
    else if (accion == "borrar") s = correr({gs, "reset", esq, nombre}, 10);
    else if (accion == "escribir") {
        const json v = a.contains("valor") ? a["valor"] : json();
        s = correr({gs, "set", esq, nombre, v.is_string() ? v.get<std::string>() : v.dump()}, 10);
    } else
        return resultadoTexto(id, "acción desconocida: " + accion, true);
    if (s.codigo != 0) return resultadoTexto(id, "gsettings: " + s.texto, true);
    return resultadoDatos(id, {{"esquema", esq}, {"nombre", nombre}, {"resultado", s.texto}});
}

json avisoSistema(const json& id, const json& a, const Permisos& p) {
    if (!p.avisos) return sinPermiso(id, "avisos");
    const std::string titulo = a.value("titulo", std::string("Broadcast Appliance")), texto = a.value("texto", std::string());
    const std::string tipo = a.value("tipo", std::string("info"));
    if (const std::string ns = programa({"notify-send"}); !ns.empty()) {
        const Salida s = correr({ns, "-u", tipo == "error" ? "critical" : "normal", titulo, texto}, 10);
        if (s.codigo == 0) return resultadoTexto(id, "Aviso mostrado.");
    }
    if (const std::string gd = programa({"gdbus"}); !gd.empty()) {
        const auto q = [](const std::string& x) {
            std::string o = "'";
            for (char c : x) o += c == '\'' ? std::string("\\'") : std::string(1, c);
            return o + "'";
        };
        const Salida s = correr({gd, "call", "--session", "--dest", "org.freedesktop.Notifications", "--object-path",
                                 "/org/freedesktop/Notifications", "--method", "org.freedesktop.Notifications.Notify",
                                 "Broadcast Appliance", "0", "", q(titulo), q(texto), "[]", "{}", "8000"},
                                10);
        if (s.codigo == 0) return resultadoTexto(id, "Aviso mostrado.");
    }
    return resultadoTexto(id, "no hay cómo mostrar avisos acá (falta notify-send y el servicio de notificaciones)", true);
}

json ventanasDeNavegador() {
    json l = json::array();
    if (!x11().abrir()) return l;
    static const std::set<std::string> navs = {"google-chrome", "chromium", "chromium-browser", "microsoft-edge", "firefox",
                                               "brave-browser", "opera"};
    for (const auto& v : ventanas())
        if (navs.count(minusculas(v.app)) > 0) l.push_back({{"id", static_cast<long long>(v.id)}, {"titulo", v.titulo}, {"app", v.app}});
    return l;
}

bool atenderUia(const json& id, const std::string& nombre, const json&, const Permisos&, json& out) {
    if (nombre.rfind("system_ui_", 0) != 0) return false;
    out = resultadoTexto(id, nombre + " todavía no está hecha en Linux (AT-SPI, en camino): system_pantalla y system_mouse sí.", true);
    return true;
}

// --- Despacho en Linux ------------------------------------------------------------------
json ejecutarHerramienta(const json& id, const std::string& nombre, const json& a, const Permisos& p) {
    json out;
    if (atenderUia(id, nombre, a, p, out) || atenderChrome(id, nombre, a, p, out) || atenderArchivos(id, nombre, a, p, out))
        return out;
    const auto noPermiso = [&](const char* que) {
        return resultadoTexto(id,
                              std::string("El control del sistema no tiene permiso para «") + que +
                                  "» (Configuración → Control del sistema, o control_configurar).",
                              true);
    };
    // Lo que no necesita la pantalla.
    if (nombre == "system_procesos") {
        if (!p.procesos) return noPermiso("procesos");
        x11().abrir(); // para marcar los que tienen ventana (si hay pantalla)
        return resultadoDatos(id, listarProcesos(a.value("nombre", std::string()), a.value("max", 200)));
    }
    if (nombre == "system_proceso_cerrar") {
        if (!p.procesos) return noPermiso("procesos");
        const long pid = a.value("pid", 0L);
        if (pid <= 1) return resultadoTexto(id, "ese proceso es del sistema: no se cierra", true);
        if (pid == getpid() || pid == getppid()) return resultadoTexto(id, "ese soy yo (ba-control) o quien me lanzó", true);
        const std::string nom = nombreProceso(pid);
        if (nom.empty() || !vivo(pid)) return resultadoTexto(id, "no existe el proceso " + std::to_string(pid), true);
        int cerradas = 0;
        if (x11().abrir())
            for (const auto& v : ventanas())
                if (v.pid == pid) mensajeWm(v.id, "_NET_CLOSE_WINDOW", 0, 2), ++cerradas;
        if (!cerradas) kill(static_cast<pid_t>(pid), SIGTERM);
        for (int i = 0; i < 40 && vivo(pid); ++i) dormir(100);
        std::string como = cerradas ? "cerró sus ventanas" : "SIGTERM";
        if (vivo(pid)) {
            if (!a.value("forzar", false))
                return resultadoTexto(id, "«" + nom + "» (pid " + std::to_string(pid) +
                                              ") sigue abierto (quizás pregunta si guardar). Con forzar: true se termina en seco.",
                                      true);
            kill(static_cast<pid_t>(pid), SIGKILL);
            for (int i = 0; i < 30 && vivo(pid); ++i) dormir(100);
            como = "terminado en seco";
        }
        if (vivo(pid)) return resultadoTexto(id, "«" + nom + "» no terminó", true);
        return resultadoTexto(id, "Cerrado «" + nom + "» (pid " + std::to_string(pid) + ", " + como + ").");
    }
    if (nombre == "system_comando") {
        if (!p.comandos) return noPermiso("comandos");
        const std::string cmd = a.value("comando", std::string());
        if (cmd.empty()) return resultadoTexto(id, "falta el comando", true);
        const std::string sh = a.value("shell", std::string("sistema"));
        std::string shell = sh == "bash" ? "bash" : "sh";
        if (sh == "powershell" && !enPath("pwsh").empty()) shell = "pwsh";
        const Salida s = correr({shell, shell == "pwsh" ? "-Command" : "-c", cmd}, std::clamp(a.value("segundos", 30), 1, 300),
                                a.value("carpeta", std::string()));
        json d{{"codigo", s.codigo}, {"salida", s.texto}};
        if (s.cortada) d["nota"] = "salida larga: quedan los últimos 200 KB";
        if (s.vencido) d["vencido"] = "pasaron " + std::to_string(std::clamp(a.value("segundos", 30), 1, 300)) + " s y se terminó";
        return resultadoDatos(id, d);
    }
    if (nombre == "system_sistema") {
        if (!p.sistema) return noPermiso("sistema");
        x11().abrir();
        return resultadoDatos(id, datosSistema());
    }
    if (nombre == "system_buscar") {
        if (!p.buscar) return noPermiso("buscar");
        std::string patron = a.value("patron", std::string());
        if (patron.empty()) return resultadoTexto(id, "falta el patrón", true);
        std::string dir = a.value("carpeta", std::string());
        if (dir.empty()) dir = std::getenv("HOME") ? std::getenv("HOME") : "/";
        if (patron.find('*') == std::string::npos && patron.find('?') == std::string::npos) patron = "*" + patron + "*";
        std::vector<std::string> res;
        buscarEn(dir, patron, res, a.value("max", 100));
        return resultadoDatos(id, {{"encontrados", res}, {"total", res.size()}});
    }
    if (nombre == "system_ejecutar") {
        if (!p.ejecutar) return noPermiso("ejecutar");
        const bool admin = a.value("admin", false);
        if (admin && !p.ejecutar_admin) return noPermiso("ejecutar como administrador");
        const std::string prog = a.value("programa", std::string());
        if (prog.empty()) return resultadoTexto(id, "falta el programa", true);
        std::vector<std::string> argv;
        const std::string ejec = enPath(prog);
        if (admin) argv = {"pkexec", ejec.empty() ? prog : ejec};
        else if (!ejec.empty() && !fs::is_directory(ejec)) argv = {ejec};
        else argv = {"xdg-open", prog}; // un archivo, una carpeta o una URL: con lo que tenga asignado
        std::istringstream ss(a.value("argumentos", std::string()));
        for (std::string x; ss >> x;) argv.push_back(x);
        std::vector<char*> av;
        for (auto& x : argv) av.push_back(x.data());
        av.push_back(nullptr);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        if (const std::string c = a.value("carpeta", std::string()); !c.empty()) posix_spawn_file_actions_addchdir_np(&fa, c.c_str());
        posix_spawnattr_t at;
        posix_spawnattr_init(&at);
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);
        pid_t pid = 0;
        const int rc = posix_spawnp(&pid, av[0], &fa, &at, av.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&at);
        if (rc != 0) return resultadoTexto(id, "no se pudo ejecutar «" + prog + "» (error " + std::to_string(rc) + ")", true);
        return resultadoTexto(id, "Ejecutado «" + prog + "»" + (admin ? " como administrador (pkexec)" : "") + " (pid " + std::to_string(pid) + ").");
    }
    if (nombre == "system_portapapeles") {
        if (!p.portapapeles) return noPermiso("portapapeles");
        const bool wl = std::getenv("WAYLAND_DISPLAY") != nullptr;
        if (a.contains("texto")) {
            const std::string t = a.value("texto", std::string());
            std::vector<std::string> cmd;
            if (wl && !enPath("wl-copy").empty()) cmd = {"wl-copy"};
            else if (!enPath("xclip").empty()) cmd = {"xclip", "-selection", "clipboard"};
            else if (!enPath("xsel").empty()) cmd = {"xsel", "--clipboard", "--input"};
            else return resultadoTexto(id, "falta xclip, xsel o wl-clipboard para el portapapeles", true);
            const Salida s = correr(cmd, 5, {}, t, false);
            if (s.codigo != 0 && !s.vencido) return resultadoTexto(id, "no se pudo escribir: " + s.texto, true);
            return resultadoTexto(id, "Copiado al portapapeles.");
        }
        std::vector<std::string> cmd;
        if (wl && !enPath("wl-paste").empty()) cmd = {"wl-paste", "--no-newline"};
        else if (!enPath("xclip").empty()) cmd = {"xclip", "-selection", "clipboard", "-o"};
        else if (!enPath("xsel").empty()) cmd = {"xsel", "--clipboard", "--output"};
        else return resultadoTexto(id, "falta xclip, xsel o wl-clipboard para el portapapeles", true);
        return resultadoDatos(id, {{"texto", correr(cmd, 5).texto}});
    }
    if (nombre == "system_esperar" && !a.contains("titulo")) {
        const int ms = std::clamp(a.value("ms", 1000), 0, 30000);
        dormir(ms);
        return resultadoTexto(id, "Listo: pasaron " + std::to_string(ms) + " ms.");
    }

    // Lo que necesita la pantalla (X11).
    X11& x = x11();
    if (!x.abrir()) return resultadoTexto(id, x.error, true);
    if (nombre == "system_monitores") {
        if (!p.pantalla) return noPermiso("pantalla");
        json l = json::array();
        int i = 0;
        for (const auto& m : monitores()) l.push_back({{"indice", i++}, {"x", m.x}, {"y", m.y}, {"ancho", m.w}, {"alto", m.h}, {"principal", m.principal}});
        return resultadoDatos(id, {{"monitores", l}});
    }
    if (nombre == "system_pantalla") {
        if (!p.pantalla) return noPermiso("pantalla");
        int x0 = 0, y0 = 0, w = x.XDisplayWidth(x.d, 0), h = x.XDisplayHeight(x.d, 0);
        if (a.contains("monitor")) {
            const auto ms = monitores();
            const int i = a.value("monitor", 0);
            if (i < 0 || i >= static_cast<int>(ms.size())) return resultadoTexto(id, "no existe ese monitor", true);
            x0 = ms[i].x, y0 = ms[i].y, w = ms[i].w, h = ms[i].h;
        }
        std::string titulo;
        if (a.contains("ventana") || a.contains("titulo")) {
            json b = a;
            if (a.contains("ventana")) b["id"] = a["ventana"];
            Ventana v;
            if (!buscarVentana(b, v)) return resultadoTexto(id, "no encontré esa ventana (system_ventanas las lista)", true);
            x0 = v.x, y0 = v.y, w = v.w, h = v.h;
            titulo = tituloDe(v);
        } else if (a.contains("ancho") && a.contains("alto"))
            x0 = a.value("x", 0), y0 = a.value("y", 0), w = a.value("ancho", w), h = a.value("alto", h);
        if (w <= 0 || h <= 0) return resultadoTexto(id, "tamaño inválido", true);
        std::string b64, error;
        int sw = 0, sh = 0;
        if (!capturar(x0, y0, w, h, std::clamp(a.value("escala", 1.0), 0.1, 1.0), b64, sw, sh, error)) return resultadoTexto(id, error, true);
        if (!titulo.empty())
            return resultadoImagen(id, b64, "Ventana «" + titulo + "»: " + std::to_string(sw) + "x" + std::to_string(sh) +
                                                " (lo que se ve en su lugar: si está tapada, sale lo de arriba)");
        return resultadoImagen(id, b64, "Captura de " + std::to_string(w) + "x" + std::to_string(h) + " en " + std::to_string(x0) +
                                            "," + std::to_string(y0) +
                                            (sw != w ? " (achicada a " + std::to_string(sw) + "x" + std::to_string(sh) + ")" : ""));
    }
    if (nombre == "system_mouse") {
        if (!p.mouse) return noPermiso("mouse");
        if (!x.xtest()) return resultadoTexto(id, x.error, true);
        const std::string accion = a.value("accion", std::string());
        const unsigned b = numBoton(a.value("boton", std::string("izq")));
        if (a.contains("x") && a.contains("y")) moverMouse(a.value("x", 0), a.value("y", 0));
        if (accion == "click" || accion == "doble") {
            for (int i = 0; i < (accion == "doble" ? 2 : 1); ++i) botonMouse(b, true), botonMouse(b, false);
        } else if (accion == "arrastrar") {
            botonMouse(b, true);
            dormir(30);
            moverMouse(a.value("hasta_x", a.value("x", 0)), a.value("hasta_y", a.value("y", 0)));
            dormir(30);
            botonMouse(b, false);
        } else if (accion == "rueda") {
            const int pasos = a.value("pasos", 1);
            for (int i = 0; i < std::abs(pasos); ++i) botonMouse(pasos > 0 ? 4 : 5, true), botonMouse(pasos > 0 ? 4 : 5, false);
        } else if (accion != "mover")
            return resultadoTexto(id, "acción desconocida", true);
        Window r = 0, hijo = 0;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned m = 0;
        x.XQueryPointer(x.d, x.raiz, &r, &hijo, &rx, &ry, &wx, &wy, &m);
        std::string sobre;
        for (const auto& v : ventanas())
            if (rx >= v.x && rx < v.x + v.w && ry >= v.y && ry < v.y + v.h) {
                sobre = tituloDe(v);
                break;
            }
        return resultadoTexto(id, "Hecho. El mouse quedó en " + std::to_string(rx) + "," + std::to_string(ry) + (sobre.empty() ? "." : ", sobre «" + sobre + "»."));
    }
    if (nombre == "system_teclado") {
        if (!p.teclado) return noPermiso("teclado");
        if (!x.xtest()) return resultadoTexto(id, x.error, true);
        if (a.contains("texto")) escribirTexto(a.value("texto", std::string()));
        if (a.contains("teclas") && !mandarTeclas(a.value("teclas", std::string())))
            return resultadoTexto(id, "no entendí la combinación de teclas", true);
        const Ventana f = datosVentana(activa());
        return resultadoTexto(id, "Hecho, en «" + tituloDe(f) + "».");
    }
    if (nombre == "system_cursor") {
        if (!p.pantalla) return noPermiso("pantalla");
        Window r = 0, hijo = 0;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned m = 0;
        x.XQueryPointer(x.d, x.raiz, &r, &hijo, &rx, &ry, &wx, &wy, &m);
        int monitor = -1, i = 0;
        for (const auto& mo : monitores()) {
            if (rx >= mo.x && rx < mo.x + mo.w && ry >= mo.y && ry < mo.y + mo.h) monitor = i;
            ++i;
        }
        double inactivo = -1;
        if (x.XScreenSaverAllocInfo && x.XScreenSaverQueryInfo)
            if (XScreenSaverInfo* info = x.XScreenSaverAllocInfo()) {
                if (x.XScreenSaverQueryInfo(x.d, x.raiz, info)) inactivo = info->idle / 1000.0;
                x.XFree(info);
            }
        json debajo{{"id", 0}, {"titulo", ""}};
        for (const auto& v : ventanas())
            if (rx >= v.x && rx < v.x + v.w && ry >= v.y && ry < v.y + v.h) {
                debajo = {{"id", static_cast<long long>(v.id)}, {"titulo", tituloDe(v)}};
                break;
            }
        const Window fw = activa();
        json d{{"x", rx}, {"y", ry}, {"monitor", monitor}, {"debajo", debajo},
               {"foco", {{"id", static_cast<long long>(fw)}, {"titulo", fw ? tituloDe(datosVentana(fw)) : std::string()}}}};
        if (inactivo >= 0) d["inactivo_s"] = std::round(inactivo * 10) / 10, d["usandola"] = inactivo < 5;
        return resultadoDatos(id, d);
    }
    if (nombre == "system_ventanas") {
        if (!p.ventanas) return noPermiso("ventanas");
        static const std::set<std::string> navs = {"google-chrome", "chromium", "chromium-browser", "microsoft-edge", "firefox",
                                                   "brave-browser", "opera"};
        json l = json::array();
        for (const auto& v : ventanas())
            l.push_back({{"id", static_cast<long long>(v.id)}, {"titulo", v.titulo}, {"app", v.app},
                         {"navegador", navs.count(minusculas(v.app)) > 0}, {"pid", v.pid}, {"x", v.x}, {"y", v.y},
                         {"ancho", v.w}, {"alto", v.h}});
        return resultadoDatos(id, {{"ventanas", l}});
    }
    if (nombre == "system_ventana") {
        if (!p.ventanas) return noPermiso("ventanas");
        Ventana v;
        if (!buscarVentana(a, v)) return resultadoTexto(id, "no encontré esa ventana", true);
        const std::string accion = a.value("accion", std::string());
        const long agregar = 1, quitar = 0;
        const auto estado = [&](long que, const char* p1, const char* p2 = nullptr) {
            mensajeWm(v.id, "_NET_WM_STATE", que, static_cast<long>(x.atomo(p1)), p2 ? static_cast<long>(x.atomo(p2)) : 0, 1);
        };
        if (accion == "activar") mensajeWm(v.id, "_NET_ACTIVE_WINDOW", 2, 0);
        else if (accion == "minimizar") x.XIconifyWindow(x.d, v.id, 0), x.XFlush(x.d);
        else if (accion == "maximizar") estado(agregar, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
        else if (accion == "restaurar") {
            estado(quitar, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
            mensajeWm(v.id, "_NET_ACTIVE_WINDOW", 2, 0);
        } else if (accion == "cerrar") mensajeWm(v.id, "_NET_CLOSE_WINDOW", 0, 2);
        else if (accion == "fijar_arriba") estado(agregar, "_NET_WM_STATE_ABOVE");
        else if (accion == "soltar_arriba") estado(quitar, "_NET_WM_STATE_ABOVE");
        else if (accion == "mover") {
            const int nx = a.value("x", v.x), ny = a.value("y", v.y), nw = a.value("ancho", v.w), nh = a.value("alto", v.h);
            if (nw < 50 || nh < 30) return resultadoTexto(id, "tamaño muy chico", true);
            estado(quitar, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
            x.XMoveResizeWindow(x.d, v.id, nx, ny, static_cast<unsigned>(nw), static_cast<unsigned>(nh));
            x.XSync(x.d, 0);
            dormir(80);
            const Ventana d = datosVentana(v.id);
            return resultadoTexto(id, "Movida «" + tituloDe(d) + "»: " + std::to_string(d.x) + "," + std::to_string(d.y) + ", " +
                                          std::to_string(d.w) + "x" + std::to_string(d.h) + ".");
        } else
            return resultadoTexto(id, "acción desconocida", true);
        return resultadoTexto(id, "Hecho «" + tituloDe(v) + "».");
    }
    if (nombre == "system_esperar") {
        if (!p.ventanas) return noPermiso("ventanas");
        const int ms = std::clamp(a.value("ms", 10000), 0, 30000);
        const std::string t = a.value("titulo", std::string());
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        do {
            Ventana v;
            if (buscarVentana(json{{"titulo", t}}, v)) return resultadoTexto(id, "Apareció «" + tituloDe(v) + "».");
            dormir(150);
        } while (std::chrono::steady_clock::now() < fin);
        return resultadoTexto(id, "No apareció ninguna ventana con «" + t + "» en " + std::to_string(ms) + " ms.", true);
    }
    return resultadoTexto(id, "herramienta desconocida: " + nombre, true);
}

} // namespace bactl

#endif // __linux__
