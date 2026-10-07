// ============================================================================
// src/app/comun/JsonSignals.hpp — señales de Qt que llevan nlohmann::json
//
// En Windows, Qt declara `struct tagMSG` (MSG) incompleto y un operator<< de
// QDebug para el. Al registrar un tipo de señal, Qt prueba si hay operator<<
// para el y nlohmann::json intenta convertirse a MSG: con el tipo incompleto
// eso es un error duro. Con windows.h (MSG completo) la prueba simplemente
// falla y no pasa nada. Incluir este header en todo .hpp con Q_OBJECT cuyas
// señales o slots lleven json.
// ============================================================================
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>
