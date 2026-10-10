// ============================================================================
// src/core/diag/Utf8.hpp — que un texto de afuera sea UTF-8 válido
//
// Los mensajes del sistema y de bibliotecas de terceros (GStreamer, ONNX
// Runtime, drivers) llegan en Windows en la página de códigos del sistema
// (cp1252 en castellano): «operación» trae un byte 0xF3 suelto, que no es
// UTF-8. Metido así en un JSON, nlohmann::json tira una excepción al
// serializarlo; sin nadie que la ataje, en Windows la app queda colgada en el
// cuadro de diálogo de abort() (pasó el 2026-10-10: la placa se quedó sin
// memoria, ONNX Runtime lo dijo en cp1252 y la app se colgó dos veces).
//
// `utf8Valido`: si ya es UTF-8, igual; si no, en Windows se convierte desde la
// página de códigos del sistema (se lee bien: «operación»); en otro sistema,
// cada byte inválido pasa a «?».
// ============================================================================
#pragma once

#include <string>
#include <string_view>

namespace ba::diag {

bool esUtf8(std::string_view s);
std::string utf8Valido(std::string_view s);
inline std::string utf8Valido(const char* s) { return s != nullptr ? utf8Valido(std::string_view(s)) : std::string(); }

} // namespace ba::diag
