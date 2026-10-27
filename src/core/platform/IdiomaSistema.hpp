// ============================================================================
// src/core/platform/IdiomaSistema.hpp — el idioma que eligió el usuario en el sistema
//
// Lo usa ba::i18n la primera vez (sin idioma guardado en launcher.json): el programa
// arranca en el idioma del sistema si lo tiene, en vez de en español porque sí.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::platform {

/// Los idiomas preferidos del usuario, el primero el más preferido, como etiquetas
/// BCP 47 ("es-AR", "en-US", "pt-BR", "zh-Hans-CN"). Vacía si el sistema no dice nada.
std::vector<std::string> idiomasDelSistema();

} // namespace ba::platform
