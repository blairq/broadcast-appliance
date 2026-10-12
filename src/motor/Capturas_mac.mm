// ============================================================================
// src/motor/Capturas_mac.mm — ver Capturas.hpp (la Mac)
//
// Las ventanas, de CGWindowListCopyWindowInfo (sin permiso de grabación de
// pantalla los títulos vienen vacíos: queda el nombre de la aplicación). La
// pantalla, con avfvideosrc (su device-index es el orden de
// CGGetActiveDisplayList); la ventana, con bacapturaventana (ScreenCaptureKit).
// ============================================================================
#include "Capturas.hpp"

#include "Piezas.hpp" // hayFabrica

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <unistd.h>

namespace ba::motor {

using json = nlohmann::json;

namespace {
std::string texto(CFDictionaryRef d, CFStringRef clave) {
    const auto v = static_cast<CFStringRef>(CFDictionaryGetValue(d, clave));
    if (v == nullptr || CFGetTypeID(v) != CFStringGetTypeID()) return {};
    return [(__bridge NSString*)v UTF8String] ?: "";
}
long long numero(CFDictionaryRef d, CFStringRef clave) {
    const auto v = static_cast<CFNumberRef>(CFDictionaryGetValue(d, clave));
    long long n = 0;
    if (v != nullptr && CFGetTypeID(v) == CFNumberGetTypeID()) CFNumberGetValue(v, kCFNumberLongLongType, &n);
    return n;
}
} // namespace

std::vector<VentanaCapturable> ventanasCapturables() {
    std::vector<VentanaCapturable> out;
    @autoreleasepool {
        CFArrayRef lista = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                      kCGNullWindowID);
        if (lista == nullptr) return out;
        const pid_t yo = getpid();
        for (CFIndex i = 0; i < CFArrayGetCount(lista); ++i) {
            const auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(lista, i));
            if (numero(d, kCGWindowLayer) != 0) continue; // barras, menús, el Dock
            VentanaCapturable v;
            v.pid = static_cast<int>(numero(d, kCGWindowOwnerPID));
            if (v.pid == yo) continue;
            v.handle = static_cast<std::uint64_t>(numero(d, kCGWindowNumber));
            v.app = texto(d, kCGWindowOwnerName);
            v.titulo = texto(d, kCGWindowName);
            CGRect r{};
            if (const auto b = static_cast<CFDictionaryRef>(CFDictionaryGetValue(d, kCGWindowBounds)))
                CGRectMakeWithDictionaryRepresentation(b, &r);
            v.ancho = static_cast<int>(r.size.width);
            v.alto = static_cast<int>(r.size.height);
            if (v.ancho < 60 || v.alto < 40) continue;
            if (v.titulo.empty()) v.titulo = v.app; // sin permiso de grabación, sin títulos
            out.push_back(v);
        }
        CFRelease(lista);
    }
    return out;
}

bool hayCapturaDeVentana(std::string* porque) {
    if (hayFabrica("bacapturaventana")) return true;
    if (porque) *porque = "falta bacapturaventana (ScreenCaptureKit, macOS 12.3 o más nuevo)";
    return false;
}

std::string origenPantalla(const json& a) {
    // El monitor del recuadro (puntos, como Qt y CGDisplayBounds: arriba a la izquierda
    // del principal); su posición en CGGetActiveDisplayList es el device-index.
    int indice = a.value("indice", 0);
    if (a.contains("ancho") && a.value("ancho", 0) > 0) {
        CGDirectDisplayID ids[16];
        uint32_t n = 0;
        if (CGGetActiveDisplayList(16, ids, &n) == kCGErrorSuccess) {
            const double x = a.value("x", 0.0), y = a.value("y", 0.0), w = a.value("ancho", 0.0), h = a.value("alto", 0.0);
            for (uint32_t i = 0; i < n; ++i) {
                const CGRect b = CGDisplayBounds(ids[i]);
                if (std::abs(b.origin.x - x) < 2 && std::abs(b.origin.y - y) < 2 && std::abs(b.size.width - w) < 2 &&
                    std::abs(b.size.height - h) < 2) {
                    indice = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    return "avfvideosrc capture-screen=true capture-screen-cursor=" + std::string(a.value("cursor", true) ? "true" : "false") +
           " device-index=" + std::to_string(indice);
}

std::string origenVentana(const std::string& app, const std::string& titulo, bool cursor) {
    VentanaCapturable v;
    if (!buscarVentanaCapturable(app, titulo, v)) return {};
    return "bacapturaventana ventana=" + std::to_string(v.handle) + " cursor=" + (cursor ? "true" : "false");
}

} // namespace ba::motor
