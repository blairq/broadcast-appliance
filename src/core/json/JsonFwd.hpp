// ============================================================================
// src/core/json/JsonFwd.hpp — nlohmann::json declarado, sin definirlo
//
// Para los headers que nombran nlohmann::json en una firma (por referencia o por
// valor), en un puntero o en un `unique_ptr`, sin usarlo por dentro. Incluir
// <nlohmann/json.hpp> cuesta ~1 s de frontend por TU (instancia, entre otras cosas,
// basic_json::parse<const char*> por el `_json` que define el propio header): eso va en
// el .cpp que lo usa. Un miembro por valor o una función inline que lo toca sí
// necesitan el header entero.
// ============================================================================
#pragma once

#include <nlohmann/json_fwd.hpp>
