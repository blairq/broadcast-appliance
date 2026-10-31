// ============================================================================
// src/core/platform/Energia_mac.mm — ver Energia.hpp
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#import <Foundation/Foundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <sys/mount.h>
#include <sys/param.h>

#include <algorithm>
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

// --- El corte de luz (F1-11) -------------------------------------------------------------------

EstadoEnergia leerEnergia() {
    EstadoEnergia e;
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (info == nullptr) return e;
    e.conocido = true;
    // De dónde viene la energía ahora: «AC Power», «Battery Power» o «UPS Power».
    if (CFStringRef prov = IOPSGetProvidingPowerSourceType(info); prov != nullptr)
        e.enBateria = CFStringCompare(prov, CFSTR(kIOPMACPowerKey), 0) != kCFCompareEqualTo;
    CFArrayRef lista = IOPSCopyPowerSourcesList(info);
    int sumaPct = 0, cuantas = 0;
    for (CFIndex i = 0; lista != nullptr && i < CFArrayGetCount(lista); ++i) {
        CFDictionaryRef d = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(lista, i));
        if (d == nullptr) continue;
        CFBooleanRef presente = static_cast<CFBooleanRef>(CFDictionaryGetValue(d, CFSTR(kIOPSIsPresentKey)));
        if (presente != nullptr && !CFBooleanGetValue(presente)) continue;
        CFStringRef tipo = static_cast<CFStringRef>(CFDictionaryGetValue(d, CFSTR(kIOPSTypeKey)));
        const bool ups = tipo != nullptr && CFStringCompare(tipo, CFSTR(kIOPSUPSType), 0) == kCFCompareEqualTo;
        e.hayBateria = true;
        if (ups || e.fuente.empty()) e.fuente = ups ? "ups" : "bateria";
        int actual = -1, maximo = -1, minutos = -1;
        if (CFNumberRef n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, CFSTR(kIOPSCurrentCapacityKey))))
            CFNumberGetValue(n, kCFNumberIntType, &actual);
        if (CFNumberRef n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, CFSTR(kIOPSMaxCapacityKey))))
            CFNumberGetValue(n, kCFNumberIntType, &maximo);
        if (CFNumberRef n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, CFSTR(kIOPSTimeToEmptyKey))))
            CFNumberGetValue(n, kCFNumberIntType, &minutos); // -1 = calculando
        if (actual >= 0 && maximo > 0) {
            sumaPct += actual * 100 / maximo;
            ++cuantas;
        }
        if (minutos >= 0) e.minutos = std::max(e.minutos, 0) + minutos;
    }
    if (lista != nullptr) CFRelease(lista);
    CFRelease(info);
    e.porcentaje = cuantas > 0 ? sumaPct / cuantas : -1;
    if (!e.enBateria) e.minutos = -1;
    return e;
}

std::string sistemaDeArchivos(const std::filesystem::path& ruta) {
    std::filesystem::path p = ruta;
    std::error_code ec;
    while (!p.empty() && !std::filesystem::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    struct statfs s {};
    return statfs(p.c_str(), &s) == 0 ? std::string(s.f_fstypename) : std::string();
}

namespace {

/// Las preferencias de energía (lo que escribe pmset): el archivo general y el de
/// esta máquina (com.apple.PowerManagement.<UUID>.plist). Gana el de la máquina.
id preferenciaAC(NSString* clave) {
    id valor = nil;
    NSString* dir = @"/Library/Preferences";
    NSMutableArray* archivos = [NSMutableArray arrayWithObject:@"com.apple.PowerManagement.plist"];
    for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil])
        if ([f hasPrefix:@"com.apple.PowerManagement."] && ![f isEqualToString:@"com.apple.PowerManagement.plist"])
            [archivos addObject:f];
    for (NSString* f in archivos) {
        NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:[dir stringByAppendingPathComponent:f]];
        id v = [d[@"AC Power"] isKindOfClass:[NSDictionary class]] ? d[@"AC Power"][clave] : nil;
        if (v != nil) valor = v;
    }
    return valor;
}

} // namespace

std::vector<ItemPreparacion> preparacion(const std::filesystem::path& datos) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    std::vector<ItemPreparacion> out;
    const EstadoEnergia e = leerEnergia();
    @autoreleasepool {
        // Arrancar al volver la luz (pmset autorestart). En una notebook no existe: su batería hace de UPS.
        ItemPreparacion arr;
        arr.id = "arranque_al_volver";
        arr.titulo = traducir("preparacion", "Encender sola al volver la luz");
        if (e.hayBateria && e.fuente == "bateria") {
            arr.estado = "no_aplica";
            arr.detalle = traducir("preparacion", "Es una notebook: su batería la mantiene encendida durante el corte.");
        } else {
            id v = preferenciaAC(@"Automatic Restart On Power Loss");
            const bool si = v != nil && [v respondsToSelector:@selector(intValue)] && [v intValue] != 0;
            arr.estado = si ? "ok" : "falta";
            arr.detalle = si ? traducir("preparacion", "La Mac arranca sola cuando vuelve la luz (pmset autorestart 1).")
                             : traducir("preparacion", "La Mac NO arranca sola cuando vuelve la luz.");
            if (!si)
                arr.como = traducir("preparacion", "Ajustes del Sistema → Energía → «Arrancar automáticamente después de un corte "
                                                   "de luz», o en la Terminal, como administrador: sudo pmset -a autorestart 1");
        }
        out.push_back(arr);

        // Inicio de sesión automático (loginwindow autoLoginUser).
        ItemPreparacion ses;
        ses.id = "sesion_automatica";
        ses.titulo = traducir("preparacion", "Iniciar sesión sola");
        NSDictionary* lw = [NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/com.apple.loginwindow.plist"];
        NSString* usuario = [lw[@"autoLoginUser"] isKindOfClass:[NSString class]] ? lw[@"autoLoginUser"] : nil;
        ses.estado = usuario.length > 0 ? "ok" : "falta";
        ses.detalle = usuario.length > 0 ? llenar(traducir("preparacion", "La sesión de «%1» se inicia sola."), {usuario.UTF8String})
                                         : traducir("preparacion", "Al encender, la Mac espera que alguien inicie sesión.");
        if (usuario.length == 0)
            ses.como = traducir("preparacion", "Ajustes del Sistema → Usuarios y grupos → «Iniciar sesión automáticamente como». "
                                               "No se puede con FileVault prendido.");
        out.push_back(ses);
    }
    out.push_back(itemSistemaDeArchivos(datos));
    out.push_back(itemRespaldo(e));
    return out;
}

} // namespace ba::platform
