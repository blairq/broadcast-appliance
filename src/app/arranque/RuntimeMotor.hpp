// ============================================================================
// src/app/arranque/RuntimeMotor.hpp — AppRuntime con el motor
//
// Para quien entra al motor por el runtime: engine(), motorEntreHilos(), turno(),
// niveles(), alFallarMotor. Trae la API del motor (emision/ApiMotor.hpp), el turno y el
// canal de niveles, no emision/BroadcastEngine.hpp: tocar el motor por dentro no
// recompila a quien lo usa por acá. Quien no usa el motor incluye arranque/RuntimeAire.hpp
// o arranque/RuntimeConfig.hpp.
// ============================================================================
#pragma once

#include "arranque/RuntimeAire.hpp"
#include "emision/ApiMotor.hpp"
#include "emision/NivelesMotor.hpp"
#include "emision/TurnoMotor.hpp"
