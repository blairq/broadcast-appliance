// ============================================================================
// src/core/persist/Esquema.hpp — la versión de cada JSON de Config/
//
// Cada archivo de configuración lleva `"esquema": N` (lo de antes de versionar no
// lo dice: es el 1). Al leerlo, `migrar` corre en orden los pasos que le falten
// (el paso i lleva del esquema i+1 al i+2) y lo deja marcado con el actual; al
// guardarlo, el dueño escribe el actual. Un archivo con un esquema MÁS NUEVO (lo
// escribió una versión posterior del programa) no se toca: se avisa y se lee
// como está, sin pasos para atrás.
//
// Sin Qt: lo usan la app, el gateway y el launcher.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ba::persist {

inline constexpr const char* kClaveEsquema = "esquema";

/// El esquema de `j` (1 si no lo dice o no es un objeto).
int esquemaDe(const nlohmann::json& j);

/// Un paso de migración: cambia `j` de un esquema al siguiente.
using PasoMigracion = std::function<void(nlohmann::json& j)>;

/// Lleva `j` al esquema `actual` con `pasos` (pasos.size() == actual - 1) y lo marca.
/// Devuelve el esquema que tenía. `nombre` es para el registro (qué archivo se migró).
int migrar(nlohmann::json& j, int actual, const std::vector<PasoMigracion>& pasos, const std::string& nombre);

} // namespace ba::persist
