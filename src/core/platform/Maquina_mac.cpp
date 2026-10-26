// src/core/platform/Maquina_mac.cpp — el id y el modelo de la Mac

#include "platform/Maquina.hpp"

#include <sys/sysctl.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <ctime>

namespace ba::platform {

std::string idCrudoDeMaquina() {
    // gethostuuid da el IOPlatformUUID (el del hardware) sin IOKit.
    uuid_t u{};
    const timespec espera{1, 0};
    if (gethostuuid(u, &espera) != 0) return {};
    uuid_string_t s{};
    uuid_unparse_upper(u, s);
    return s;
}

std::string modeloDeMaquina() {
    char buf[128] = {};
    std::size_t n = sizeof buf - 1;
    if (sysctlbyname("hw.model", buf, &n, nullptr, 0) != 0) return {};
    return buf;
}

} // namespace ba::platform
