// ============================================================================
// src/app/audio/PluginTerminos.hpp — como se llaman los plugins EN CASTELLANO
//
// POR QUE EXISTE. El rack trae ~200 plugins LSP y todos se describen en
// ingles: «performs gating of input signal», «reduce hissing and whistling».
// Un operador no busca asi. Dice «que la musica baje cuando hablan», «sacale
// el siseo», «que no pase de cierto volumen». Sin estos terminos, buscar en
// castellano no encuentra NADA, y el asistente ni se entera de que existen.
//
// ES UN INDICE A MANO, y tiene que serlo: no hay metadato en el plugin que
// diga para que se usa en una cabina de television. Eso lo sabe quien opera.
//
// SE INDEXA POR FAMILIA, no por plugin: `gate_mono`, `gate_stereo`, `gate_lr`
// y `gate_ms` son el mismo aparato con distinta cantidad de canales, y
// repetir los terminos cuatro veces solo desordena.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::app::audio {

/// La familia de un id de plugin: `in.lsp-plug.sc_gate_stereo` -> `sc_gate`.
/// Vacio si no se reconoce la forma.
std::string familiaDe(const std::string& pluginId);

/// Cuantos canales dice manejar, segun el sufijo: "mono", "estereo",
/// "izq/der", "medio/lado", "x8"... Vacio si no lo dice.
std::string canalesDe(const std::string& pluginId);

/// Los terminos en castellano de ese plugin (por su familia). Vacio si la
/// familia no esta en la tabla: entonces solo se lo encuentra por su nombre en
/// ingles, que es mejor que nada.
std::vector<std::string> terminosDePlugin(const std::string& pluginId);

/// Para que sirve, en una linea y en castellano. Vacio si no esta en la tabla.
std::string paraQueSirve(const std::string& pluginId);

} // namespace ba::app::audio
