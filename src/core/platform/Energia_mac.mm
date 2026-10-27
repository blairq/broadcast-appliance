// ============================================================================
// src/core/platform/Energia_mac.mm — ver Energia.hpp
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#import <Foundation/Foundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <string>

namespace ba::platform {

struct Despierta::Impl {
    IOPMAssertionID sistema = kIOPMNullAssertionID;
    IOPMAssertionID pantalla = kIOPMNullAssertionID;
    std::string detalle;
};

Despierta::Despierta(const std::string& motivo, bool pantalla) : d_(std::make_unique<Impl>()) {
    CFStringRef razon = CFStringCreateWithCString(kCFAllocatorDefault, motivo.c_str(), kCFStringEncodingUTF8);
    const IOReturn r = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                                   razon, &d_->sistema);
    bool pOk = false;
    if (pantalla)
        pOk = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, razon,
                                          &d_->pantalla) == kIOReturnSuccess;
    if (razon != nullptr) CFRelease(razon);
    if (r != kIOReturnSuccess) d_->sistema = kIOPMNullAssertionID;
    using ba::i18n::traducir;
    const std::string rechazado = " " + traducir("energia", "(rechazado)");
    d_->detalle = traducir("energia", "sin reposo del sistema") + (r == kIOReturnSuccess ? std::string() : rechazado) +
                  (pantalla ? ", " + traducir("energia", "sin apagar la pantalla") + (pOk ? std::string() : rechazado)
                            : std::string()) +
                  " (pmset -g assertions)";
}

Despierta::~Despierta() {
    if (d_ == nullptr) return;
    if (d_->pantalla != kIOPMNullAssertionID) IOPMAssertionRelease(d_->pantalla);
    if (d_->sistema != kIOPMNullAssertionID) IOPMAssertionRelease(d_->sistema);
}

bool Despierta::activa() const { return d_ != nullptr && d_->sistema != kIOPMNullAssertionID; }
const std::string& Despierta::detalle() const { return d_->detalle; }

std::string sinAhorroDeEnergia() {
    // Con la ventana tapada o la pantalla bloqueada, App Nap baja la prioridad del proceso
    // y junta sus temporizadores (las esperas de 4, 10 y 20 ms de los anillos y del motor
    // se estiran): el video y el audio en vivo no pueden. La actividad vive lo que el
    // proceso (se guarda en una estática a propósito).
    static id actividad = nil;
    if (actividad != nil) return ba::i18n::traducir("energia", "sin App Nap");
    @autoreleasepool {
        actividad = [[NSProcessInfo processInfo]
            // «AllowingIdleSystemSleep»: esto es SOLO App Nap; no dormir lo decide el lanzador
            // (Despierta), que respeta el ajuste energia.mantenerDespierta.
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:[NSString stringWithUTF8String:ba::i18n::traducir(
                                                                        "energia", "Broadcast Appliance: estudio en vivo")
                                                                        .c_str()]];
#if !__has_feature(objc_arc)
        [actividad retain];
#endif
    }
    return actividad != nil ? ba::i18n::traducir("energia", "sin App Nap")
                            : ba::i18n::traducir("energia", "no se pudo pedir que no lo frene App Nap");
}

} // namespace ba::platform
