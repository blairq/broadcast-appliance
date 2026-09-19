// ============================================================================
// src/app/audio/Modules.hpp — modulos de audio con nombre propio (LSP)
//
// El catalogo le pone nombres claros a los modulos mas usados y a sus
// parametros, para el asistente (MCP) y la interfaz:
//   gate        compuerta (ruido de fondo entre frases)
//   compresor   compresion de dinamica
//   deesser     sibilancias (s, ch)
//   eq          ecualizador parametrico de 8 bandas (filtro multibanda)
//   multibanda  compresor multibanda (hasta 8 bandas)
//   limitador   techo de seguridad
// Los valores van en UNIDADES REALES ("20 ms", -30 = dB, "Bell"): se
// convierten con la propia conversion del plugin (muchos parametros de LSP
// son 0..1 con curva). Cualquier otro parametro se puede tocar por su nombre
// LSP (lspParams / setParam).
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/ClapHost.hpp"

namespace ba::app::audio {

struct ModuleParam {
    std::string key;     // "umbral_db"
    std::string lspName; // "Attack threshold" ({n} = numero de banda)
    std::string help;
};

struct ModuleType {
    std::string key; // "compresor"
    std::vector<std::string> aliases;
    std::string pluginId; // "in.lsp-plug.compressor_stereo"
    std::string title;
    std::string help;
    std::vector<ModuleParam> params;     // globales
    std::vector<ModuleParam> bandParams; // por banda ({n})
    int bandFirst = 0, bandCount = 0;
    nlohmann::json presets; // {"voz": {...}}
};

const std::vector<ModuleType>& moduleCatalog();
/// Por clave, alias o id de plugin. nullptr si no es del catalogo.
const ModuleType* findModule(const std::string& keyOrPluginId);

/// Parametro por nombre LSP (sin importar mayusculas) o por id numerico.
const ParamInfo* findParam(const std::vector<ParamInfo>& ps, const std::string& nameOrId);
/// Valor + texto + rango (y opciones si es una lista).
nlohmann::json paramJson(ClapInstance& inst, const ParamInfo& p);
/// Cambia un parametro. `v`: numero en unidades reales, texto ("20 ms",
/// "Bell", "on") o booleano. "" si anduvo; si no, el error.
std::string setParam(ClapInstance& inst, const ParamInfo& p, const nlohmann::json& v);

/// Parametros curados de un modulo del catalogo (con sus valores).
nlohmann::json readModule(ClapInstance& inst, const ModuleType& t);
/// Aplica {clave: valor} (claves del catalogo, "bandas": [{banda, ...}] o
/// nombres LSP). Devuelve {"aplicados": [...], "errores": {...}}.
nlohmann::json applyModule(ClapInstance& inst, const ModuleType* t, const nlohmann::json& values);

} // namespace ba::app::audio
