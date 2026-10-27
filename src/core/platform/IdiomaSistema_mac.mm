// ============================================================================
// src/core/platform/IdiomaSistema_mac.mm — ver IdiomaSistema.hpp
//
// Los de Ajustes del Sistema → General → Idioma y región, en orden ("es-AR"…).
// ============================================================================
#include "platform/IdiomaSistema.hpp"

#import <Foundation/Foundation.h>

namespace ba::platform {

std::vector<std::string> idiomasDelSistema() {
    std::vector<std::string> out;
    @autoreleasepool {
        for (NSString* l in [NSLocale preferredLanguages]) {
            if (const char* s = [l UTF8String]; s != nullptr && *s != '\0') out.emplace_back(s);
        }
    }
    return out;
}

} // namespace ba::platform
