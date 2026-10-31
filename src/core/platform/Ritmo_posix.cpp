// ============================================================================
// src/core/platform/Ritmo_posix.cpp — ver Ritmo.hpp (macOS y Linux)
// ============================================================================
#include "platform/Ritmo.hpp"

#include <thread>

namespace ba::platform {

// macOS y Linux despiertan un sleep con precisión de decenas de µs: no hace falta nada más.
void dormirHasta(std::chrono::steady_clock::time_point plazo) { std::this_thread::sleep_until(plazo); }

} // namespace ba::platform
