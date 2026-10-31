// ============================================================================
// src/core/platform/Termica_mac.mm — ver Termica.hpp
// ============================================================================
#include "platform/Termica.hpp"

#import <Foundation/Foundation.h>

namespace ba::platform {

EstadoTermico leerTermica() {
    // macOS no da la temperatura sin permisos (powermetrics pide administrador): da el
    // estado térmico, que es lo que decide si el sistema frena el procesador.
    EstadoTermico e;
    e.fuente = "thermalState";
    @autoreleasepool {
        switch ([[NSProcessInfo processInfo] thermalState]) {
            case NSProcessInfoThermalStateNominal: e.nivel = NivelTermico::Normal; break;
            case NSProcessInfoThermalStateFair: e.nivel = NivelTermico::Templado; break;
            case NSProcessInfoThermalStateSerious: e.nivel = NivelTermico::Serio; break;
            case NSProcessInfoThermalStateCritical: e.nivel = NivelTermico::Critico; break;
            default: e.nivel = NivelTermico::Desconocido; break;
        }
    }
    return e;
}

} // namespace ba::platform
