// ============================================================================
// src/app/arranque/RuntimeMotor.hpp — AppRuntime con el motor
//
// Para quien entra al motor por el runtime: engine(), motorEntreHilos(), turno(),
// niveles(), alFallarMotor. Trae emision/BroadcastEngine.hpp: tocarlo recompila a todos
// los que incluyen este header; quien no usa el motor incluye arranque/RuntimeAire.hpp
// o arranque/RuntimeConfig.hpp.
// ============================================================================
#pragma once

#include "arranque/RuntimeAire.hpp"
#include "emision/BroadcastEngine.hpp"
