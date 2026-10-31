// ============================================================================
// src/app/arranque/Registrar.hpp — enchufar TODO lo que habla al registro
//
// El Proceso A no habla solo: hablan Qt y QML, y cada uno tiene su propio
// canal. Hasta ahora todo eso caía en el mismo archivo de
// texto sin nivel ni módulo, mezclado con los printf nuestros.
//
// Esto los redirige al Registro (ver core/diag/Registro.hpp), con su módulo y
// con el nivel que cada uno ya declaraba y se estaba tirando a la basura:
//
//   * `qt`   — los avisos del framework.
//   * `qml`  — lo que se rompe en la interfaz. Es el que más se extrañaba: un
//              «Cannot assign to non-existent property» aparecía perdido en
//              medio de un archivo de texto y no lo veía nadie.
//
// Se llama una vez, temprano, antes de crear la QGuiApplication y antes de
// arrancar el motor.
// ============================================================================
#pragma once

#include <filesystem>

namespace ba::app {

/// Abre el registro del proceso y engancha Qt y QML.
///
/// :param dataRoot: la raíz de datos; los logs van a `<dataRoot>/Logs`.
/// :param componente: el módulo por defecto de este proceso ("app").
void registrarTodo(const std::filesystem::path& dataRoot, const char* componente = "app");

} // namespace ba::app
