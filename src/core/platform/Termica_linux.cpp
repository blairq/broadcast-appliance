// ============================================================================
// src/core/platform/Termica_linux.cpp — ver Termica.hpp
// ============================================================================
#include "platform/Termica.hpp"

namespace ba::platform {

EstadoTermico leerTermica() { return leerTermicaLinux("/sys/class/thermal", "/sys/class/hwmon"); }

} // namespace ba::platform
