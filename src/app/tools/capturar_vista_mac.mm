// src/app/tools/capturar_vista_mac.mm — el último cuadro que dibujó la interfaz de LSP
//
// Para capturar_lsp. Leer la pantalla no sirve: pide el permiso de grabación
// de pantalla y, con la sesión bloqueada, solo devuelve el fondo. Pedirle a la
// vista que se dibuje tampoco (la de LSP solo dibuja cuando tiene un cuadro
// nuevo y las capas no se dejan leer). Así que se intercepta el cuadro mismo:
// -[CocoaCairoView renderCairoImage] (lsp-ws-lib) arma un CGImage con la
// superficie de cairo de toda la ventana; se reemplaza por uno que llama al
// original y se queda con una copia de los píxeles.

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <QImage>

#include <mutex>

namespace {

std::mutex g_mu;
QImage g_ultimo;
IMP g_original = nullptr;

CGImageRef renderCairoImageCapturando(id self, SEL cmd) {
    auto img = reinterpret_cast<CGImageRef (*)(id, SEL)>(g_original)(self, cmd);
    if (img == nullptr) return img;
    // El CGImage apunta a la superficie de cairo sin copiarla (y la superficie
    // se destruye después de dibujar): se copia ya. Es BGRA premultiplicado.
    CFDataRef datos = CGDataProviderCopyData(CGImageGetDataProvider(img));
    if (datos != nullptr) {
        const QImage vista(CFDataGetBytePtr(datos), static_cast<int>(CGImageGetWidth(img)),
                           static_cast<int>(CGImageGetHeight(img)), static_cast<qsizetype>(CGImageGetBytesPerRow(img)),
                           QImage::Format_ARGB32_Premultiplied);
        std::lock_guard<std::mutex> l(g_mu);
        // Las filas vienen de abajo hacia arriba (el CGImage se dibuja volteado).
        g_ultimo = vista.mirrored(false, true);
        CFRelease(datos);
    }
    return img;
}

} // namespace

bool engancharCuadrosLsp() {
    Class c = objc_getClass("CocoaCairoView");
    Method m = c != nil ? class_getInstanceMethod(c, sel_registerName("renderCairoImage")) : nullptr;
    if (m == nullptr) return false;
    g_original = method_setImplementation(m, reinterpret_cast<IMP>(renderCairoImageCapturando));
    return true;
}

QImage ultimoCuadroLsp() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_ultimo;
}
