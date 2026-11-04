// ============================================================================
// src/app/arranque/AppRuntime.hpp — Paraguas: AppRuntime con todo, como antes
//
// La clase está en arranque/RuntimeConfig.hpp. Este header trae lo mismo que traía antes
// de la Ola 3 (el motor, el aire, la configuración) para que nada se rompa mientras se
// migran los includes (tools/dev/migrar_includes.py runtime). Código nuevo: incluir
// arranque/RuntimeConfig.hpp, arranque/RuntimeAire.hpp o arranque/RuntimeMotor.hpp,
// según lo que use.
// ============================================================================
#pragma once

#include <map>
#include <mutex>
#include <set>

#include "arranque/RuntimeMotor.hpp"
