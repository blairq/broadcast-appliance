// ============================================================================
// src/motor/Prioridad_mac.mm — ver Prioridad.hpp (sinAppNap)
// ============================================================================
#include "Prioridad.hpp"

#import <Foundation/Foundation.h>

namespace ba::motor {

void sinAppNap() {
    // Con la ventana tapada o la pantalla bloqueada, App Nap baja la prioridad del proceso
    // y junta sus temporizadores (las esperas de 4, 10 y 20 ms de los anillos y del motor
    // se estiran): el video y el audio en vivo no pueden. Mientras viva el proceso.
    static id actividad = [[NSProcessInfo processInfo]
        beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                          reason:@"video y audio en vivo"];
    (void)actividad;
}

} // namespace ba::motor
