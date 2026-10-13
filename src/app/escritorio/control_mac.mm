// ============================================================================
// src/app/escritorio/control_mac.mm — el motor de macOS del control del sistema
//
// Las MISMAS herramientas system_* que en Windows (ba_control_main.cpp), con los
// mismos argumentos y las mismas respuestas, hechas con lo de la Mac:
//   pantalla, monitores   ScreenCaptureKit (macOS 14+; CGWindowList en 13) y
//                         CoreGraphics. Las capturas salen en PUNTOS de pantalla
//                         (no en los píxeles dobles de la Retina): así una
//                         coordenada de la imagen es la misma que se le pasa al mouse.
//   mouse, teclado        CGEvent.
//   ventanas, ventana     CGWindowList (listar) y Accesibilidad (AX: activar, mover,
//                         minimizar, cerrar). «fijar_arriba» no existe para ventanas
//                         de otros programas en macOS: lo dice.
//   ejecutar              NSWorkspace (como un doble clic) y, como administrador,
//                         AppleScript «with administrator privileges».
//   portapapeles          NSPasteboard.   procesos: libproc.   comando: /bin/zsh.
//   sistema               sysctl, statfs, IOKit (batería).
//
// PERMISOS DEL SISTEMA. macOS pide dos, por aplicación: «Grabación de pantalla»
// (capturas y los títulos de las ventanas) y «Accesibilidad» (mouse, teclado y
// manejar ventanas). Si falta uno, la herramienta lo dice y dónde se prende (la
// primera vez macOS muestra su propio cartel).
// ============================================================================

#include "escritorio/control.hpp"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h> // kVK_* (códigos de tecla)
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <fnmatch.h>
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace bactl {

namespace {

namespace fs = std::filesystem;

std::string aU(NSString* s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }
NSString* aNS(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// --- Permisos del sistema (TCC) ------------------------------------------------------

const char* kSinAccesibilidad =
    "macOS no le dio el permiso «Accesibilidad» (mouse, teclado y ventanas): Ajustes del Sistema › Privacidad y "
    "seguridad › Accesibilidad, y habilitá Broadcast Appliance (y la terminal del asistente si aparece). La "
    "primera vez macOS muestra su cartel; después de prenderlo, volvé a intentar.";
const char* kSinPantalla =
    "macOS no le dio el permiso «Grabación de pantalla» (capturas y títulos de las ventanas): Ajustes del Sistema › "
    "Privacidad y seguridad › Grabación de pantalla y del audio del sistema, y habilitá Broadcast Appliance. "
    "Puede pedir volver a abrir la app.";

bool accesibilidad() {
    const void* claves[] = {kAXTrustedCheckOptionPrompt};
    const void* valores[] = {kCFBooleanTrue};
    CFDictionaryRef op = CFDictionaryCreate(nullptr, claves, valores, 1, &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
    const bool ok = AXIsProcessTrustedWithOptions(op);
    CFRelease(op);
    return ok;
}

bool permisoPantalla() {
    if (CGPreflightScreenCaptureAccess()) return true;
    CGRequestScreenCaptureAccess(); // el cartel de macOS (la primera vez)
    return CGPreflightScreenCaptureAccess();
}

// --- Monitores ------------------------------------------------------------------------

struct Monitor {
    CGDirectDisplayID id;
    int x, y, w, h;
    bool principal;
};

std::vector<Monitor> monitores() {
    std::vector<Monitor> out;
    CGDirectDisplayID ids[32];
    uint32_t n = 0;
    if (CGGetActiveDisplayList(32, ids, &n) != kCGErrorSuccess) return out;
    const CGDirectDisplayID principal = CGMainDisplayID();
    for (uint32_t i = 0; i < n; ++i) {
        const CGRect b = CGDisplayBounds(ids[i]); // en puntos, origen arriba a la izquierda del principal
        out.push_back({ids[i], static_cast<int>(b.origin.x), static_cast<int>(b.origin.y),
                       static_cast<int>(b.size.width), static_cast<int>(b.size.height), ids[i] == principal});
    }
    std::stable_sort(out.begin(), out.end(), [](const Monitor& a, const Monitor& b) { return a.principal > b.principal; });
    return out;
}

CGRect escritorioVirtual() {
    CGRect r = CGRectNull;
    for (const auto& m : monitores()) r = CGRectUnion(r, CGRectMake(m.x, m.y, m.w, m.h));
    return CGRectIsNull(r) ? CGRectMake(0, 0, 0, 0) : r;
}

// --- Imágenes -------------------------------------------------------------------------

/// La imagen a `w`x`h` (los puntos que ocupa, por la escala pedida) en PNG.
bool aPng(CGImageRef img, int w, int h, std::string& png) {
    if (img == nullptr || w <= 0 || h <= 0) return false;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(nullptr, static_cast<size_t>(w), static_cast<size_t>(h), 8, 0, cs,
                                             kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (ctx == nullptr) return false;
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGImageRef chica = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (chica == nullptr) return false;
    CFMutableDataRef datos = CFDataCreateMutable(nullptr, 0);
    CGImageDestinationRef dst = CGImageDestinationCreateWithData(datos, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    bool ok = false;
    if (dst != nullptr) {
        CGImageDestinationAddImage(dst, chica, nullptr);
        ok = CGImageDestinationFinalize(dst);
        CFRelease(dst);
    }
    if (ok) png = base64(CFDataGetBytePtr(datos), static_cast<size_t>(CFDataGetLength(datos)));
    CFRelease(datos);
    CGImageRelease(chica);
    return ok;
}

/// Una región del escritorio (en puntos) o una ventana, como CGImage (el que llama la libera).
CGImageRef capturarImagen(CGRect r, CGWindowID ventana, std::string& error) {
    if (@available(macOS 14.0, *)) {
        __block SCShareableContent* contenido = nil;
        __block NSError* err = nil;
        dispatch_semaphore_t s = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                  onScreenWindowsOnly:YES
                                                    completionHandler:^(SCShareableContent* c, NSError* e) {
                                                        contenido = c;
                                                        err = e;
                                                        dispatch_semaphore_signal(s);
                                                    }];
        dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        if (contenido == nil) {
            error = err ? std::string(kSinPantalla) : "macOS no contestó qué hay en pantalla";
            return nullptr;
        }
        SCContentFilter* filtro = nil;
        SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
        cfg.showsCursor = YES;
        if (ventana != 0) {
            SCWindow* w = nil;
            for (SCWindow* x in contenido.windows)
                if (x.windowID == ventana) w = x;
            if (w == nil) {
                error = "esa ventana no está a la vista";
                return nullptr;
            }
            filtro = [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
            cfg.width = static_cast<size_t>(w.frame.size.width);
            cfg.height = static_cast<size_t>(w.frame.size.height);
        } else {
            // La pantalla que tiene la región (si abarca varias, la principal recortada).
            SCDisplay* d = contenido.displays.firstObject;
            for (SCDisplay* x in contenido.displays)
                if (CGRectContainsRect(CGDisplayBounds(x.displayID), r)) d = x;
            if (d == nil) {
                error = "no hay pantallas";
                return nullptr;
            }
            const CGRect bd = CGDisplayBounds(d.displayID);
            filtro = [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
            const CGRect dentro = CGRectIntersection(r, bd);
            cfg.sourceRect = CGRectOffset(dentro, -bd.origin.x, -bd.origin.y);
            cfg.width = static_cast<size_t>(dentro.size.width);
            cfg.height = static_cast<size_t>(dentro.size.height);
        }
        __block CGImageRef img = nullptr;
        dispatch_semaphore_t s2 = dispatch_semaphore_create(0);
        [SCScreenshotManager captureImageWithFilter:filtro
                                      configuration:cfg
                                  completionHandler:^(CGImageRef i, NSError* e) {
                                      if (i) img = CGImageRetain(i);
                                      err = e;
                                      dispatch_semaphore_signal(s2);
                                  }];
        dispatch_semaphore_wait(s2, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC));
        if (img == nullptr) error = err ? aU(err.localizedDescription) : "la captura no llegó";
        return img;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGImageRef img = ventana != 0 ? CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, ventana,
                                                            kCGWindowImageBoundsIgnoreFraming)
                                  : CGWindowListCreateImage(r, kCGWindowListOptionOnScreenOnly, kCGNullWindowID,
                                                            kCGWindowImageDefault);
#pragma clang diagnostic pop
    if (img == nullptr) error = kSinPantalla;
    return img;
}

// --- Ventanas -------------------------------------------------------------------------

struct Ventana {
    CGWindowID id = 0;
    std::string titulo, app;
    pid_t pid = 0;
    CGRect r{};
};

/// Las ventanas a la vista, de adelante hacia atrás (las de las aplicaciones: capa 0).
std::vector<Ventana> ventanas() {
    std::vector<Ventana> out;
    CFArrayRef l = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                              kCGNullWindowID);
    if (l == nullptr) return out;
    for (NSDictionary* d in (__bridge NSArray*)l) {
        if ([d[(id)kCGWindowLayer] intValue] != 0) continue;
        if ([d[(id)kCGWindowAlpha] doubleValue] <= 0.01) continue;
        CGRect r{};
        CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)d[(id)kCGWindowBounds], &r);
        if (r.size.width < 40 || r.size.height < 30) continue;
        Ventana v;
        v.id = [d[(id)kCGWindowNumber] unsignedIntValue];
        v.titulo = aU(d[(id)kCGWindowName]); // vacío sin «Grabación de pantalla»
        v.app = aU(d[(id)kCGWindowOwnerName]);
        v.pid = [d[(id)kCGWindowOwnerPID] intValue];
        v.r = r;
        if (v.app == "Window Server" || v.app == "Dock") continue;
        out.push_back(v);
    }
    CFRelease(l);
    return out;
}

std::string tituloDe(const Ventana& v) { return v.titulo.empty() ? v.app : v.titulo + " — " + v.app; }

bool buscarVentana(const json& a, Ventana& out) {
    const auto vs = ventanas();
    if (a.contains("id") && a["id"].is_number()) {
        const auto id = static_cast<CGWindowID>(a.value("id", 0LL));
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

/// La ventana de Accesibilidad que corresponde a una de CGWindowList (por título o lugar).
AXUIElementRef axDe(const Ventana& v) {
    AXUIElementRef app = AXUIElementCreateApplication(v.pid);
    CFArrayRef ws = nullptr;
    AXUIElementRef elegida = nullptr;
    if (AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, (CFTypeRef*)&ws) == kAXErrorSuccess && ws) {
        for (CFIndex i = 0; i < CFArrayGetCount(ws) && elegida == nullptr; ++i) {
            AXUIElementRef w = (AXUIElementRef)CFArrayGetValueAtIndex(ws, i);
            CFTypeRef t = nullptr, pos = nullptr, tam = nullptr;
            CGPoint p{};
            CGSize s{};
            AXUIElementCopyAttributeValue(w, kAXTitleAttribute, &t);
            AXUIElementCopyAttributeValue(w, kAXPositionAttribute, &pos);
            AXUIElementCopyAttributeValue(w, kAXSizeAttribute, &tam);
            if (pos) AXValueGetValue((AXValueRef)pos, (AXValueType)kAXValueCGPointType, &p);
            if (tam) AXValueGetValue((AXValueRef)tam, (AXValueType)kAXValueCGSizeType, &s);
            const std::string titulo = t ? aU((__bridge NSString*)t) : std::string();
            const bool mismoLugar = std::abs(p.x - v.r.origin.x) < 4 && std::abs(p.y - v.r.origin.y) < 4 &&
                                    std::abs(s.width - v.r.size.width) < 4;
            if ((!v.titulo.empty() && titulo == v.titulo) || mismoLugar) elegida = (AXUIElementRef)CFRetain(w);
            if (t) CFRelease(t);
            if (pos) CFRelease(pos);
            if (tam) CFRelease(tam);
        }
        if (elegida == nullptr && CFArrayGetCount(ws) > 0) elegida = (AXUIElementRef)CFRetain(CFArrayGetValueAtIndex(ws, 0));
        CFRelease(ws);
    }
    CFRelease(app);
    return elegida;
}

void activarApp(pid_t pid) {
    NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [a activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
}

// --- Mouse y teclado ------------------------------------------------------------------

CGPoint cursor() {
    CGEventRef e = CGEventCreate(nullptr);
    const CGPoint p = CGEventGetLocation(e);
    CFRelease(e);
    return p;
}

void mouse(CGEventType tipo, CGPoint p, CGMouseButton b, int clics = 1) {
    CGEventRef e = CGEventCreateMouseEvent(nullptr, tipo, p, b);
    if (clics > 1) CGEventSetIntegerValueField(e, kCGMouseEventClickState, clics);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

struct Boton {
    CGMouseButton b;
    CGEventType abajo, arriba, arrastre;
};
Boton boton(const std::string& n) {
    if (n == "der") return {kCGMouseButtonRight, kCGEventRightMouseDown, kCGEventRightMouseUp, kCGEventRightMouseDragged};
    if (n == "medio") return {kCGMouseButtonCenter, kCGEventOtherMouseDown, kCGEventOtherMouseUp, kCGEventOtherMouseDragged};
    return {kCGMouseButtonLeft, kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGEventLeftMouseDragged};
}

void escribirTexto(const std::string& texto) {
    NSString* s = aNS(texto);
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    // De a pedazos (CGEventKeyboardSetUnicodeString acepta hasta ~20 caracteres por evento).
    for (NSUInteger i = 0; i < s.length; i += 16) {
        const NSRange r = NSMakeRange(i, std::min<NSUInteger>(16, s.length - i));
        std::vector<UniChar> u(r.length);
        [s getCharacters:u.data() range:r];
        CGEventRef d = CGEventCreateKeyboardEvent(src, 0, true), up = CGEventCreateKeyboardEvent(src, 0, false);
        CGEventKeyboardSetUnicodeString(d, u.size(), u.data());
        CGEventKeyboardSetUnicodeString(up, u.size(), u.data());
        CGEventPost(kCGHIDEventTap, d);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(d);
        CFRelease(up);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    if (src) CFRelease(src);
}

/// «cmd+shift+4», «ctrl+c», «enter», «f5»… `mod` es la tecla de los atajos del sistema
/// (Comando en la Mac, Control en Windows y Linux); `win`, `super` y `meta` son Comando.
bool mandarTeclas(const std::string& combo) {
    static const std::map<std::string, CGKeyCode> teclas = {
        {"a", kVK_ANSI_A}, {"b", kVK_ANSI_B}, {"c", kVK_ANSI_C}, {"d", kVK_ANSI_D}, {"e", kVK_ANSI_E},
        {"f", kVK_ANSI_F}, {"g", kVK_ANSI_G}, {"h", kVK_ANSI_H}, {"i", kVK_ANSI_I}, {"j", kVK_ANSI_J},
        {"k", kVK_ANSI_K}, {"l", kVK_ANSI_L}, {"m", kVK_ANSI_M}, {"n", kVK_ANSI_N}, {"o", kVK_ANSI_O},
        {"p", kVK_ANSI_P}, {"q", kVK_ANSI_Q}, {"r", kVK_ANSI_R}, {"s", kVK_ANSI_S}, {"t", kVK_ANSI_T},
        {"u", kVK_ANSI_U}, {"v", kVK_ANSI_V}, {"w", kVK_ANSI_W}, {"x", kVK_ANSI_X}, {"y", kVK_ANSI_Y},
        {"z", kVK_ANSI_Z}, {"0", kVK_ANSI_0}, {"1", kVK_ANSI_1}, {"2", kVK_ANSI_2}, {"3", kVK_ANSI_3},
        {"4", kVK_ANSI_4}, {"5", kVK_ANSI_5}, {"6", kVK_ANSI_6}, {"7", kVK_ANSI_7}, {"8", kVK_ANSI_8},
        {"9", kVK_ANSI_9}, {"enter", kVK_Return}, {"return", kVK_Return}, {"tab", kVK_Tab}, {"esc", kVK_Escape},
        {"escape", kVK_Escape}, {"space", kVK_Space}, {"espacio", kVK_Space}, {"backspace", kVK_Delete},
        {"borrar", kVK_Delete}, {"delete", kVK_ForwardDelete}, {"supr", kVK_ForwardDelete},
        {"up", kVK_UpArrow}, {"arriba", kVK_UpArrow}, {"down", kVK_DownArrow}, {"abajo", kVK_DownArrow},
        {"left", kVK_LeftArrow}, {"izquierda", kVK_LeftArrow}, {"right", kVK_RightArrow},
        {"derecha", kVK_RightArrow}, {"home", kVK_Home}, {"inicio", kVK_Home}, {"end", kVK_End},
        {"fin", kVK_End}, {"pageup", kVK_PageUp}, {"pagedown", kVK_PageDown}, {"f1", kVK_F1}, {"f2", kVK_F2},
        {"f3", kVK_F3}, {"f4", kVK_F4}, {"f5", kVK_F5}, {"f6", kVK_F6}, {"f7", kVK_F7}, {"f8", kVK_F8},
        {"f9", kVK_F9}, {"f10", kVK_F10}, {"f11", kVK_F11}, {"f12", kVK_F12}, {"-", kVK_ANSI_Minus},
        {"=", kVK_ANSI_Equal}, {",", kVK_ANSI_Comma}, {".", kVK_ANSI_Period}, {"/", kVK_ANSI_Slash}};
    CGEventFlags flags = 0;
    CGKeyCode tecla = 0xFFFF;
    std::string parte;
    std::vector<std::string> partes;
    for (char c : minusculas(combo) + "+") {
        if (c == '+' && !parte.empty()) {
            partes.push_back(parte);
            parte.clear();
        } else if (c != ' ')
            parte.push_back(c);
    }
    for (const auto& p : partes) {
        if (p == "ctrl" || p == "control") flags |= kCGEventFlagMaskControl;
        else if (p == "alt" || p == "option" || p == "opcion") flags |= kCGEventFlagMaskAlternate;
        else if (p == "shift" || p == "mayus") flags |= kCGEventFlagMaskShift;
        else if (p == "cmd" || p == "command" || p == "win" || p == "super" || p == "meta" || p == "mod")
            flags |= kCGEventFlagMaskCommand;
        else if (const auto it = teclas.find(p); it != teclas.end()) tecla = it->second;
        else return false;
    }
    if (tecla == 0xFFFF) return false;
    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef d = CGEventCreateKeyboardEvent(src, tecla, true), up = CGEventCreateKeyboardEvent(src, tecla, false);
    CGEventSetFlags(d, flags);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, d);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(d);
    CFRelease(up);
    if (src) CFRelease(src);
    return true;
}

std::string donde() {
    NSRunningApplication* a = NSWorkspace.sharedWorkspace.frontmostApplication;
    return a ? "en «" + aU(a.localizedName) + "»" : "sin aplicación al frente";
}

// --- Procesos y comandos --------------------------------------------------------------

std::string nombreProceso(pid_t pid) {
    char n[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if (proc_name(pid, n, sizeof(n)) > 0) return n;
    if (proc_pidpath(pid, n, sizeof(n)) > 0) return fs::path(n).filename().string();
    return {};
}

json listarProcesos(const std::string& filtro, int max) {
    std::set<pid_t> conVentana;
    for (const auto& v : ventanas()) conVentana.insert(v.pid);
    const int n = proc_listallpids(nullptr, 0);
    std::vector<pid_t> pids(static_cast<size_t>(std::max(0, n)) + 64);
    const int got = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    struct P {
        pid_t pid;
        std::string nombre;
        double mb;
    };
    std::vector<P> ps;
    const std::string fl = minusculas(filtro);
    for (int i = 0; i < got; ++i) {
        const pid_t pid = pids[static_cast<size_t>(i)];
        if (pid <= 0) continue;
        const std::string nom = nombreProceso(pid);
        if (nom.empty() || (!fl.empty() && minusculas(nom).find(fl) == std::string::npos)) continue;
        proc_taskinfo ti{};
        double mb = 0;
        if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) == sizeof(ti)) mb = ti.pti_resident_size / 1048576.0;
        ps.push_back({pid, nom, mb});
    }
    std::sort(ps.begin(), ps.end(), [](const P& a, const P& b) { return a.mb > b.mb; });
    json out = json::array();
    for (size_t i = 0; i < ps.size() && static_cast<int>(i) < max; ++i)
        out.push_back({{"pid", static_cast<long long>(ps[i].pid)},
                       {"nombre", ps[i].nombre},
                       {"memoria_mb", std::round(ps[i].mb * 10) / 10},
                       {"ventana", conVentana.count(ps[i].pid) > 0}});
    return {{"procesos", out}, {"total", ps.size()}};
}

bool vivo(pid_t pid) { return kill(pid, 0) == 0 || errno == EPERM; }

json cerrarProceso(const json& id, pid_t pid, bool forzar) {
    const std::string nombre = nombreProceso(pid);
    if (nombre.empty() || !vivo(pid)) return resultadoTexto(id, "no existe el proceso " + std::to_string(pid), true);
    // Primero como al cerrar la aplicación (le pregunta si guardar); si no, SIGTERM.
    NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (app) [app terminate];
    else kill(pid, SIGTERM);
    std::string como = app ? "le pidió que se cierre" : "SIGTERM";
    for (int i = 0; i < 40 && vivo(pid); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (vivo(pid)) {
        if (!forzar)
            return resultadoTexto(id, "«" + nombre + "» (pid " + std::to_string(pid) +
                                          ") sigue abierto (quizás pregunta si guardar). Con forzar: true se termina en seco.",
                                  true);
        kill(pid, SIGKILL);
        for (int i = 0; i < 30 && vivo(pid); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        como = "terminado en seco";
    }
    if (vivo(pid)) return resultadoTexto(id, "«" + nombre + "» no terminó", true);
    return resultadoTexto(id, "Cerrado «" + nombre + "» (pid " + std::to_string(pid) + ", " + como + ").");
}

json correrComando(const json& id, const std::string& cmd, const std::string& shell, const std::string& carpeta,
                   int segundos) {
    int tubo[2];
    if (pipe(tubo) != 0) return resultadoTexto(id, "no se pudo crear la tubería", true);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, tubo[1], 1);
    posix_spawn_file_actions_adddup2(&fa, tubo[1], 2);
    posix_spawn_file_actions_addclose(&fa, tubo[0]);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (!carpeta.empty()) posix_spawn_file_actions_addchdir_np(&fa, carpeta.c_str());
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP); // su propio grupo: se termina el árbol entero
    posix_spawnattr_setpgroup(&at, 0);
    // El shell del sistema (zsh). «cmd»/«powershell» son de Windows: acá corre en zsh
    // (pwsh si lo pidieron y está instalado).
    std::string sh = "/bin/zsh";
    if (shell == "sh") sh = "/bin/sh";
    else if (shell == "bash") sh = "/bin/bash";
    else if (shell == "powershell" && access("/usr/local/bin/pwsh", X_OK) == 0) sh = "/usr/local/bin/pwsh";
    const char* argv[] = {sh.c_str(), sh.find("pwsh") != std::string::npos ? "-Command" : "-c", cmd.c_str(), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, sh.c_str(), &fa, &at, const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(tubo[1]);
    if (rc != 0) {
        close(tubo[0]);
        return resultadoTexto(id, "no se pudo correr el comando (error " + std::to_string(rc) + ")", true);
    }
    std::string salida;
    bool cortada = false, vencido = false;
    const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(segundos);
    fcntl(tubo[0], F_SETFL, O_NONBLOCK);
    int estado = 0;
    bool termino = false;
    char buf[8192];
    while (true) {
        const ssize_t n = read(tubo[0], buf, sizeof(buf));
        if (n > 0) {
            salida.append(buf, static_cast<size_t>(n));
            if (salida.size() > 200 * 1024) {
                salida.erase(0, salida.size() - 200 * 1024);
                cortada = true;
            }
            continue;
        }
        if (!termino && waitpid(pid, &estado, WNOHANG) == pid) termino = true;
        if (termino && n == 0) break;
        if (std::chrono::steady_clock::now() > fin) {
            vencido = true;
            kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            kill(-pid, SIGKILL);
            if (!termino) waitpid(pid, &estado, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    close(tubo[0]);
    const long long codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
    json d{{"codigo", codigo}, {"salida", salida}};
    if (cortada) d["nota"] = "salida larga: quedan los últimos 200 KB";
    if (vencido) d["vencido"] = "pasaron " + std::to_string(segundos) + " s y se terminó";
    return resultadoDatos(id, d);
}

// --- Datos del equipo -----------------------------------------------------------------

std::string sysctlTexto(const char* n) {
    size_t t = 0;
    if (sysctlbyname(n, nullptr, &t, nullptr, 0) != 0 || t == 0) return {};
    std::string s(t, '\0');
    sysctlbyname(n, s.data(), &t, nullptr, 0);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

json datosSistema() {
    json d;
    NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
    d["macos"] = {{"version", std::to_string(v.majorVersion) + "." + std::to_string(v.minorVersion) + "." +
                                  std::to_string(v.patchVersion)},
                  {"nombre", "macOS " + std::to_string(v.majorVersion)}};
    d["equipo"] = aU(NSHost.currentHost.localizedName ?: NSProcessInfo.processInfo.hostName);
    d["modelo"] = sysctlTexto("hw.model");
    d["usuario"] = aU(NSUserName());
    d["procesadores"] = static_cast<int>(NSProcessInfo.processInfo.processorCount);
    d["procesador"] = sysctlTexto("machdep.cpu.brand_string");
    const double total = static_cast<double>(NSProcessInfo.processInfo.physicalMemory);
    vm_statistics64_data_t vs{};
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    double libre = 0;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &cnt) == KERN_SUCCESS)
        libre = static_cast<double>(vs.free_count + vs.inactive_count) * static_cast<double>(vm_page_size);
    d["memoria"] = {{"total_gb", std::round(total / 1e8) / 10},
                    {"libre_gb", std::round(libre / 1e8) / 10},
                    {"en_uso", total > 0 ? static_cast<int>(std::lround(100.0 * (1.0 - libre / total))) : 0}};
    json discos = json::array();
    struct statfs* m = nullptr;
    const int n = getmntinfo(&m, MNT_NOWAIT);
    for (int i = 0; i < n; ++i) {
        const std::string punto = m[i].f_mntonname;
        if (punto != "/" && punto.rfind("/Volumes/", 0) != 0) continue;
        if ((m[i].f_flags & MNT_DONTBROWSE) != 0) continue;
        const double bs = static_cast<double>(m[i].f_bsize);
        discos.push_back({{"unidad", punto},
                          {"total_gb", std::round(m[i].f_blocks * bs / 1e8) / 10},
                          {"libre_gb", std::round(m[i].f_bavail * bs / 1e8) / 10},
                          {"extraible", (m[i].f_flags & MNT_LOCAL) == 0 || punto.rfind("/Volumes/", 0) == 0}});
    }
    d["discos"] = discos;
    struct timeval boot{};
    size_t t = sizeof(boot);
    if (sysctlbyname("kern.boottime", &boot, &t, nullptr, 0) == 0) {
        const long long s = static_cast<long long>(time(nullptr)) - boot.tv_sec;
        d["prendido_hace"] = std::to_string(s / 3600) + " h " + std::to_string((s / 60) % 60) + " min";
    }
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (info) {
        CFArrayRef l = IOPSCopyPowerSourcesList(info);
        for (CFIndex i = 0; l && i < CFArrayGetCount(l); ++i) {
            NSDictionary* ps = (__bridge NSDictionary*)IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(l, i));
            if (![ps[@kIOPSTypeKey] isEqual:@kIOPSInternalBatteryType]) continue;
            const int cap = [ps[@kIOPSCurrentCapacityKey] intValue], max = [ps[@kIOPSMaxCapacityKey] intValue];
            d["bateria"] = {{"porcentaje", max > 0 ? cap * 100 / max : cap},
                            {"enchufada", [ps[@kIOPSPowerSourceStateKey] isEqual:@kIOPSACPowerValue]}};
        }
        if (l) CFRelease(l);
        CFRelease(info);
    }
    d["monitores"] = static_cast<int>(monitores().size());
    return d;
}

// --- Buscar archivos -------------------------------------------------------------------

void buscarEn(const fs::path& dir, const std::string& patron, std::vector<std::string>& out, int max) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && static_cast<int>(out.size()) < max; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (!n.empty() && n[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec) && (n == "Library" || n.ends_with(".app"))) it.disable_recursion_pending();
        if (fnmatch(patron.c_str(), n.c_str(), FNM_CASEFOLD) == 0) out.push_back(it->path().string());
    }
}

} // namespace

// --- Papelera, preferencias y avisos (lo que Windows hace a su manera) -------------------

bool aLaPapelera(const std::string& ruta, std::string& error) {
    @autoreleasepool {
        NSError* e = nil;
        if ([NSFileManager.defaultManager trashItemAtURL:[NSURL fileURLWithPath:aNS(ruta)] resultingItemURL:nil error:&e])
            return true;
        error = e ? aU(e.localizedDescription) : "macOS no dejó";
        return false;
    }
}

/// system_registro en macOS: no hay registro; lo equivalente son las preferencias
/// (`defaults`): `clave` es el dominio (com.apple.dock, NSGlobalDomain…), `nombre` la
/// preferencia. leer, listar, escribir (sz, dword, qword, multi_sz) y borrar.
json registroSistema(const json& id, const json& a, const Permisos& p) {
    @autoreleasepool {
        const std::string accion = a.value("accion", std::string());
        const bool escribe = accion == "escribir" || accion == "borrar";
        if (escribe ? !p.registro : !p.sistema) return sinPermiso(id, escribe ? "registro" : "sistema");
        const std::string dom = a.value("clave", std::string());
        if (dom.empty()) return resultadoTexto(id, "falta `clave`: en macOS, el dominio de las preferencias (com.apple.dock, NSGlobalDomain…)", true);
        if (dom.rfind("HK", 0) == 0)
            return resultadoTexto(id, "En macOS no hay registro de Windows: system_registro maneja las preferencias. `clave` es "
                                      "el dominio (com.apple.dock, NSGlobalDomain…) y `nombre` la preferencia.",
                                  true);
        CFStringRef d = (__bridge CFStringRef)(dom == "NSGlobalDomain" ? (NSString*)kCFPreferencesAnyApplication : aNS(dom));
        const auto aJson = [](NSObject* v) -> json {
            if (v == nil) return nullptr;
            NSData* jd = [NSJSONSerialization isValidJSONObject:@[v]] ? [NSJSONSerialization dataWithJSONObject:@[v] options:0 error:nil] : nil;
            if (jd) {
                const json j = json::parse(std::string(static_cast<const char*>(jd.bytes), jd.length), nullptr, false);
                if (j.is_array() && !j.empty()) return j[0];
            }
            return aU([v description]);
        };
        if (accion == "listar") {
            NSDictionary* todo = CFBridgingRelease(CFPreferencesCopyMultiple(nullptr, d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
            json valores = json::object();
            for (NSString* k in todo) valores[aU(k)] = aJson(todo[k]);
            return resultadoDatos(id, {{"dominio", dom}, {"valores", valores}});
        }
        const std::string nombre = a.value("nombre", std::string());
        if (nombre.empty()) return resultadoTexto(id, "falta `nombre`: la preferencia", true);
        CFStringRef k = (__bridge CFStringRef)aNS(nombre);
        if (accion == "leer") {
            NSObject* v = CFBridgingRelease(CFPreferencesCopyValue(k, d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
            if (v == nil) return resultadoTexto(id, "no hay «" + nombre + "» en " + dom, true);
            return resultadoDatos(id, {{"dominio", dom}, {"nombre", nombre}, {"valor", aJson(v)}});
        }
        if (accion == "borrar") {
            CFPreferencesSetValue(k, nullptr, d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
            CFPreferencesSynchronize(d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
            return resultadoTexto(id, "Borrada «" + nombre + "» de " + dom + ".");
        }
        if (accion == "escribir") {
            const json v = a.contains("valor") ? a["valor"] : json();
            const std::string tipo = a.value("tipo", std::string("sz"));
            NSObject* nv = nil;
            if (tipo == "dword" || tipo == "qword") nv = @(v.is_number() ? v.get<long long>() : std::atoll(v.is_string() ? v.get<std::string>().c_str() : "0"));
            else if (tipo == "multi_sz") {
                NSMutableArray* l = [NSMutableArray array];
                for (const auto& x : (v.is_array() ? v : json::array({v}))) [l addObject:aNS(x.is_string() ? x.get<std::string>() : x.dump())];
                nv = l;
            } else if (v.is_boolean()) nv = @(v.get<bool>());
            else nv = aNS(v.is_string() ? v.get<std::string>() : v.dump());
            CFPreferencesSetValue(k, (__bridge CFPropertyListRef)nv, d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
            CFPreferencesSynchronize(d, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
            return resultadoTexto(id, "Escrita «" + nombre + "» en " + dom + " (la aplicación la ve al volver a abrirse).");
        }
        return resultadoTexto(id, "acción desconocida: " + accion, true);
    }
}

json avisoSistema(const json& id, const json& a, const Permisos& p) {
    if (!p.avisos) return sinPermiso(id, "avisos");
    @autoreleasepool {
        const auto esc = [](const std::string& x) {
            std::string o;
            for (char c : x) o += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
            return o;
        };
        const std::string titulo = a.value("titulo", std::string("Broadcast Appliance"));
        const std::string tipo = a.value("tipo", std::string("info"));
        const std::string sub = tipo == "error" ? "Error" : tipo == "aviso" ? "Atención" : "";
        NSAppleScript* s = [[NSAppleScript alloc]
            initWithSource:aNS("display notification \"" + esc(a.value("texto", std::string())) + "\" with title \"" + esc(titulo) +
                               "\"" + (sub.empty() ? "" : " subtitle \"" + sub + "\""))];
        NSDictionary* err = nil;
        [s executeAndReturnError:&err];
        if (err) return resultadoTexto(id, "macOS no dejó mostrar el aviso: " + aU([err description]), true);
        return resultadoTexto(id, "Aviso mostrado.");
    }
}

json ventanasDeNavegador() {
    @autoreleasepool {
        static const std::set<std::string> navegadores = {"google chrome", "microsoft edge", "brave browser", "firefox",
                                                          "opera", "safari", "arc", "chromium"};
        json l = json::array();
        for (const auto& v : ventanas())
            if (navegadores.count(minusculas(v.app)) > 0)
                l.push_back({{"id", static_cast<long long>(v.id)}, {"titulo", v.titulo}, {"app", v.app}});
        return l;
    }
}

// --- Despacho en macOS -----------------------------------------------------------------
json ejecutarHerramienta(const json& id, const std::string& nombre, const json& a, const Permisos& p) {
    @autoreleasepool {
        json out;
        if (atenderUia(id, nombre, a, p, out) || atenderChrome(id, nombre, a, p, out) || atenderArchivos(id, nombre, a, p, out))
            return out;
        const auto noPermiso = [&](const char* que) {
            return resultadoTexto(id,
                                  std::string("El control del sistema no tiene permiso para «") + que +
                                      "» (Configuración → Control del sistema, o control_configurar).",
                                  true);
        };
        if (nombre == "system_pantalla") {
            if (!p.pantalla) return noPermiso("pantalla");
            if (!permisoPantalla()) return resultadoTexto(id, kSinPantalla, true);
            CGRect r = escritorioVirtual();
            if (a.contains("monitor")) {
                const auto ms = monitores();
                const int i = a.value("monitor", 0);
                if (i < 0 || i >= static_cast<int>(ms.size())) return resultadoTexto(id, "no existe ese monitor", true);
                r = CGRectMake(ms[i].x, ms[i].y, ms[i].w, ms[i].h);
            }
            const double escala = std::clamp(a.value("escala", 1.0), 0.1, 1.0);
            std::string png, error;
            if (a.contains("ventana") || a.contains("titulo")) {
                json b = a;
                if (a.contains("ventana")) b["id"] = a["ventana"];
                Ventana v;
                if (!buscarVentana(b, v)) return resultadoTexto(id, "no encontré esa ventana (system_ventanas las lista)", true);
                CGImageRef img = capturarImagen(v.r, v.id, error);
                if (img == nullptr) return resultadoTexto(id, error, true);
                const int sw = std::max(1, static_cast<int>(v.r.size.width * escala));
                const int sh = std::max(1, static_cast<int>(v.r.size.height * escala));
                const bool ok = aPng(img, sw, sh, png);
                CGImageRelease(img);
                if (!ok) return resultadoTexto(id, "no se pudo armar la imagen", true);
                return resultadoImagen(id, png, "Ventana «" + tituloDe(v) + "»: " + std::to_string(sw) + "x" + std::to_string(sh));
            }
            if (a.contains("ancho") && a.contains("alto"))
                r = CGRectMake(a.value("x", 0), a.value("y", 0), a.value("ancho", 0), a.value("alto", 0));
            if (r.size.width <= 0 || r.size.height <= 0) return resultadoTexto(id, "tamaño inválido", true);
            CGImageRef img = capturarImagen(r, 0, error);
            if (img == nullptr) return resultadoTexto(id, error, true);
            const int w = static_cast<int>(r.size.width), h = static_cast<int>(r.size.height);
            const int sw = std::max(1, static_cast<int>(w * escala)), sh = std::max(1, static_cast<int>(h * escala));
            const bool ok = aPng(img, sw, sh, png);
            CGImageRelease(img);
            if (!ok) return resultadoTexto(id, "no se pudo armar la imagen", true);
            return resultadoImagen(id, png, "Captura de " + std::to_string(w) + "x" + std::to_string(h) + " en " +
                                                std::to_string(static_cast<int>(r.origin.x)) + "," +
                                                std::to_string(static_cast<int>(r.origin.y)) + " (en puntos de pantalla)" +
                                                (sw != w ? " (achicada a " + std::to_string(sw) + "x" + std::to_string(sh) + ")" : ""));
        }
        if (nombre == "system_monitores") {
            if (!p.pantalla) return noPermiso("pantalla");
            json l = json::array();
            int i = 0;
            for (const auto& m : monitores())
                l.push_back({{"indice", i++}, {"x", m.x}, {"y", m.y}, {"ancho", m.w}, {"alto", m.h}, {"principal", m.principal},
                             {"escala", CGDisplayPixelsWide(m.id) > 0 ? static_cast<double>(CGDisplayPixelsWide(m.id)) / m.w : 1.0}});
            return resultadoDatos(id, {{"monitores", l}, {"nota", "coordenadas en puntos de pantalla (las del mouse)"}});
        }
        if (nombre == "system_mouse") {
            if (!p.mouse) return noPermiso("mouse");
            if (!accesibilidad()) return resultadoTexto(id, kSinAccesibilidad, true);
            const std::string accion = a.value("accion", std::string());
            const Boton b = boton(a.value("boton", std::string("izq")));
            CGPoint pt = cursor();
            if (a.contains("x") && a.contains("y")) {
                pt = CGPointMake(a.value("x", 0), a.value("y", 0));
                mouse(kCGEventMouseMoved, pt, kCGMouseButtonLeft);
            }
            if (accion == "click" || accion == "doble") {
                mouse(b.abajo, pt, b.b);
                mouse(b.arriba, pt, b.b);
                if (accion == "doble") {
                    mouse(b.abajo, pt, b.b, 2);
                    mouse(b.arriba, pt, b.b, 2);
                }
            } else if (accion == "arrastrar") {
                const CGPoint hasta = CGPointMake(a.value("hasta_x", static_cast<int>(pt.x)), a.value("hasta_y", static_cast<int>(pt.y)));
                mouse(b.abajo, pt, b.b);
                for (int i = 1; i <= 12; ++i) {
                    const CGPoint q = CGPointMake(pt.x + (hasta.x - pt.x) * i / 12, pt.y + (hasta.y - pt.y) * i / 12);
                    mouse(b.arrastre, q, b.b);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                mouse(b.arriba, hasta, b.b);
            } else if (accion == "rueda") {
                CGEventRef e = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, a.value("pasos", 1));
                CGEventPost(kCGHIDEventTap, e);
                CFRelease(e);
            } else if (accion != "mover")
                return resultadoTexto(id, "acción desconocida", true);
            const CGPoint fin = cursor();
            std::string sobre;
            for (const auto& v : ventanas())
                if (CGRectContainsPoint(v.r, fin)) {
                    sobre = tituloDe(v);
                    break;
                }
            return resultadoTexto(id, "Hecho. El mouse quedó en " + std::to_string(static_cast<int>(fin.x)) + "," +
                                          std::to_string(static_cast<int>(fin.y)) + (sobre.empty() ? "." : ", sobre «" + sobre + "»."));
        }
        if (nombre == "system_teclado") {
            if (!p.teclado) return noPermiso("teclado");
            if (!accesibilidad()) return resultadoTexto(id, kSinAccesibilidad, true);
            if (a.contains("texto")) escribirTexto(a.value("texto", std::string()));
            if (a.contains("teclas") && !mandarTeclas(a.value("teclas", std::string())))
                return resultadoTexto(id, "no entendí la combinación de teclas", true);
            return resultadoTexto(id, "Hecho, " + donde() + ".");
        }
        if (nombre == "system_ejecutar") {
            if (!p.ejecutar) return noPermiso("ejecutar");
            const bool admin = a.value("admin", false);
            if (admin && !p.ejecutar_admin) return noPermiso("ejecutar como administrador");
            const std::string prog = a.value("programa", std::string());
            if (prog.empty()) return resultadoTexto(id, "falta el programa", true);
            const std::string args = a.value("argumentos", std::string());
            if (admin) {
                // macOS pide la clave del administrador en su propio cartel.
                std::string linea = "'" + prog + "'" + (args.empty() ? "" : " " + args);
                std::string esc;
                for (char c : linea) esc += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
                NSAppleScript* s = [[NSAppleScript alloc]
                    initWithSource:aNS("do shell script \"" + esc + " > /dev/null 2>&1 &\" with administrator privileges")];
                NSDictionary* err = nil;
                [s executeAndReturnError:&err];
                if (err) return resultadoTexto(id, "no se pudo como administrador: " + aU([err description]), true);
                return resultadoTexto(id, "Ejecutado «" + prog + "» como administrador.");
            }
            NSWorkspace* ws = NSWorkspace.sharedWorkspace;
            NSURL* url = nil;
            if (prog.find("://") != std::string::npos) url = [NSURL URLWithString:aNS(prog)];
            else if (fs::exists(prog)) url = [NSURL fileURLWithPath:aNS(prog)];
            else if (NSURL* app = [ws URLForApplicationWithBundleIdentifier:aNS(prog)]) url = app;
            else {
                // Por nombre: «Safari», «Google Chrome», «TextEdit»…
                for (NSString* base in @[@"/Applications", @"/System/Applications", @"/System/Applications/Utilities",
                                          [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]]) {
                    NSString* cand = [base stringByAppendingPathComponent:[aNS(prog) stringByAppendingString:@".app"]];
                    if ([NSFileManager.defaultManager fileExistsAtPath:cand]) url = [NSURL fileURLWithPath:cand];
                }
            }
            if (url == nil) return resultadoTexto(id, "no encontré «" + prog + "» (ruta, URL, nombre de la aplicación o su identificador)", true);
            __block NSError* err = nil;
            __block pid_t pid = 0;
            dispatch_semaphore_t s = dispatch_semaphore_create(0);
            NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
            cfg.activates = !a.value("oculto", false);
            NSArray<NSString*>* argv = args.empty() ? @[] : [aNS(args) componentsSeparatedByString:@" "];
            cfg.arguments = argv;
            const bool esApp = [url.pathExtension isEqualToString:@"app"];
            auto listo = ^(NSRunningApplication* app, NSError* e) {
                err = e;
                pid = app ? app.processIdentifier : 0;
                dispatch_semaphore_signal(s);
            };
            if (esApp) [ws openApplicationAtURL:url configuration:cfg completionHandler:listo];
            else [ws openURL:url configuration:cfg completionHandler:listo];
            dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
            if (err) return resultadoTexto(id, "no se pudo abrir «" + prog + "»: " + aU(err.localizedDescription), true);
            return resultadoTexto(id, "Ejecutado «" + prog + "»" + (pid ? " (pid " + std::to_string(pid) + ")" : "") + ".");
        }
        if (nombre == "system_buscar") {
            if (!p.buscar) return noPermiso("buscar");
            std::string patron = a.value("patron", std::string());
            if (patron.empty()) return resultadoTexto(id, "falta el patrón", true);
            std::string dir = a.value("carpeta", std::string());
            if (dir.empty()) dir = aU(NSHomeDirectory());
            if (patron.find('*') == std::string::npos && patron.find('?') == std::string::npos) patron = "*" + patron + "*";
            std::vector<std::string> res;
            buscarEn(dir, patron, res, a.value("max", 100));
            return resultadoDatos(id, {{"encontrados", res}, {"total", res.size()}});
        }
        if (nombre == "system_ventanas") {
            if (!p.ventanas) return noPermiso("ventanas");
            static const std::set<std::string> navegadores = {"google chrome", "microsoft edge", "brave browser", "firefox",
                                                              "opera", "safari", "arc", "chromium"};
            json l = json::array();
            for (const auto& v : ventanas())
                l.push_back({{"id", static_cast<long long>(v.id)},
                             {"titulo", v.titulo},
                             {"app", v.app},
                             {"navegador", navegadores.count(minusculas(v.app)) > 0},
                             {"pid", static_cast<long long>(v.pid)},
                             {"x", static_cast<int>(v.r.origin.x)},
                             {"y", static_cast<int>(v.r.origin.y)},
                             {"ancho", static_cast<int>(v.r.size.width)},
                             {"alto", static_cast<int>(v.r.size.height)}});
            json d{{"ventanas", l}};
            if (!CGPreflightScreenCaptureAccess())
                d["nota"] = "sin el permiso «Grabación de pantalla» macOS no da los títulos: va el nombre de la aplicación";
            return resultadoDatos(id, d);
        }
        if (nombre == "system_ventana") {
            if (!p.ventanas) return noPermiso("ventanas");
            Ventana v;
            if (!buscarVentana(a, v)) return resultadoTexto(id, "no encontré esa ventana", true);
            const std::string accion = a.value("accion", std::string());
            if (accion == "fijar_arriba" || accion == "soltar_arriba")
                return resultadoTexto(id, "macOS no deja poner una ventana de otro programa siempre arriba (solo el "
                                          "propio programa puede). Alternativa: activarla cuando haga falta.",
                                      true);
            if (accion == "activar") {
                activarApp(v.pid);
                if (accesibilidad())
                    if (AXUIElementRef w = axDe(v)) {
                        AXUIElementPerformAction(w, kAXRaiseAction);
                        CFRelease(w);
                    }
                return resultadoTexto(id, "Hecho: «" + tituloDe(v) + "» al frente.");
            }
            if (!accesibilidad()) return resultadoTexto(id, kSinAccesibilidad, true);
            AXUIElementRef w = axDe(v);
            if (w == nullptr) return resultadoTexto(id, "no pude manejar esa ventana (Accesibilidad no la ve)", true);
            std::string res = "Hecho «" + tituloDe(v) + "».";
            if (accion == "minimizar") AXUIElementSetAttributeValue(w, kAXMinimizedAttribute, kCFBooleanTrue);
            else if (accion == "restaurar") {
                AXUIElementSetAttributeValue(w, kAXMinimizedAttribute, kCFBooleanFalse);
                AXUIElementSetAttributeValue(w, CFSTR("AXFullScreen"), kCFBooleanFalse);
            } else if (accion == "maximizar" || accion == "mover") {
                CGRect r = v.r;
                if (accion == "maximizar") {
                    // El área útil del monitor donde está (sin la barra de menú ni el Dock).
                    for (NSScreen* s in NSScreen.screens) {
                        const CGFloat altoPrincipal = NSScreen.screens.firstObject.frame.size.height;
                        const NSRect vf = s.visibleFrame, f = s.frame;
                        const CGRect fTop = CGRectMake(f.origin.x, altoPrincipal - f.origin.y - f.size.height, f.size.width, f.size.height);
                        if (CGRectContainsPoint(fTop, CGPointMake(CGRectGetMidX(v.r), CGRectGetMidY(v.r))))
                            r = CGRectMake(vf.origin.x, altoPrincipal - vf.origin.y - vf.size.height, vf.size.width, vf.size.height);
                    }
                } else {
                    r.origin.x = a.value("x", static_cast<int>(r.origin.x));
                    r.origin.y = a.value("y", static_cast<int>(r.origin.y));
                    r.size.width = a.value("ancho", static_cast<int>(r.size.width));
                    r.size.height = a.value("alto", static_cast<int>(r.size.height));
                    if (r.size.width < 50 || r.size.height < 30) {
                        CFRelease(w);
                        return resultadoTexto(id, "tamaño muy chico", true);
                    }
                }
                AXValueRef pos = AXValueCreate((AXValueType)kAXValueCGPointType, &r.origin);
                AXValueRef tam = AXValueCreate((AXValueType)kAXValueCGSizeType, &r.size);
                AXUIElementSetAttributeValue(w, kAXPositionAttribute, pos);
                AXUIElementSetAttributeValue(w, kAXSizeAttribute, tam);
                CFRelease(pos);
                CFRelease(tam);
                res = (accion == "mover" ? "Movida «" : "Maximizada «") + tituloDe(v) + "»: " + std::to_string(static_cast<int>(r.origin.x)) +
                      "," + std::to_string(static_cast<int>(r.origin.y)) + ", " + std::to_string(static_cast<int>(r.size.width)) + "x" +
                      std::to_string(static_cast<int>(r.size.height)) + ".";
            } else if (accion == "cerrar") {
                AXUIElementRef b = nullptr;
                if (AXUIElementCopyAttributeValue(w, kAXCloseButtonAttribute, (CFTypeRef*)&b) == kAXErrorSuccess && b) {
                    AXUIElementPerformAction(b, kAXPressAction);
                    CFRelease(b);
                } else {
                    CFRelease(w);
                    return resultadoTexto(id, "esa ventana no tiene botón de cerrar", true);
                }
            } else {
                CFRelease(w);
                return resultadoTexto(id, "acción desconocida", true);
            }
            CFRelease(w);
            return resultadoTexto(id, res);
        }
        if (nombre == "system_portapapeles") {
            if (!p.portapapeles) return noPermiso("portapapeles");
            NSPasteboard* pb = NSPasteboard.generalPasteboard;
            if (a.contains("texto")) {
                [pb clearContents];
                if (![pb setString:aNS(a.value("texto", std::string())) forType:NSPasteboardTypeString])
                    return resultadoTexto(id, "no se pudo escribir", true);
                return resultadoTexto(id, "Copiado al portapapeles.");
            }
            return resultadoDatos(id, {{"texto", aU([pb stringForType:NSPasteboardTypeString])}});
        }
        if (nombre == "system_cursor") {
            if (!p.pantalla) return noPermiso("pantalla");
            const CGPoint pt = cursor();
            int monitor = -1, i = 0;
            for (const auto& m : monitores()) {
                if (pt.x >= m.x && pt.x < m.x + m.w && pt.y >= m.y && pt.y < m.y + m.h) monitor = i;
                ++i;
            }
            const double inactivo = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
            json debajo{{"id", 0}, {"titulo", ""}}, foco{{"id", 0}, {"titulo", ""}};
            const auto vs = ventanas();
            for (const auto& v : vs)
                if (CGRectContainsPoint(v.r, pt)) {
                    debajo = {{"id", static_cast<long long>(v.id)}, {"titulo", tituloDe(v)}};
                    break;
                }
            if (NSRunningApplication* f = NSWorkspace.sharedWorkspace.frontmostApplication)
                for (const auto& v : vs)
                    if (v.pid == f.processIdentifier) {
                        foco = {{"id", static_cast<long long>(v.id)}, {"titulo", tituloDe(v)}};
                        break;
                    }
            return resultadoDatos(id, {{"x", static_cast<int>(pt.x)}, {"y", static_cast<int>(pt.y)}, {"monitor", monitor},
                                       {"inactivo_s", std::round(inactivo * 10) / 10},
                                       {"usandola", inactivo >= 0 && inactivo < 5},
                                       {"debajo", debajo},
                                       {"foco", foco}});
        }
        if (nombre == "system_procesos") {
            if (!p.procesos) return noPermiso("procesos");
            return resultadoDatos(id, listarProcesos(a.value("nombre", std::string()), a.value("max", 200)));
        }
        if (nombre == "system_proceso_cerrar") {
            if (!p.procesos) return noPermiso("procesos");
            const auto pid = static_cast<pid_t>(a.value("pid", 0LL));
            if (pid <= 1) return resultadoTexto(id, "ese proceso es del sistema: no se cierra", true);
            if (pid == getpid() || pid == getppid()) return resultadoTexto(id, "ese soy yo (ba-control) o quien me lanzó", true);
            return cerrarProceso(id, pid, a.value("forzar", false));
        }
        if (nombre == "system_comando") {
            if (!p.comandos) return noPermiso("comandos");
            const std::string cmd = a.value("comando", std::string());
            if (cmd.empty()) return resultadoTexto(id, "falta el comando", true);
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
            if (t.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                return resultadoTexto(id, "Listo: pasaron " + std::to_string(ms) + " ms.");
            }
            if (!p.ventanas) return noPermiso("ventanas");
            const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            do {
                Ventana v;
                if (buscarVentana(json{{"titulo", t}}, v)) return resultadoTexto(id, "Apareció «" + tituloDe(v) + "».");
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            } while (std::chrono::steady_clock::now() < fin);
            return resultadoTexto(id, "No apareció ninguna ventana con «" + t + "» en " + std::to_string(ms) + " ms.", true);
        }
        return resultadoTexto(id, "herramienta desconocida: " + nombre, true);
    }
}

} // namespace bactl

#endif // __APPLE__
