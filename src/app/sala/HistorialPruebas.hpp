// ============================================================================
// src/app/sala/HistorialPruebas.hpp — el historial de pruebas de la máquina
//
// Cada prueba (la de sala, la de capacidad —local, remota, recibida, la conexión— y la de
// asamblea) deja su informe en `<datos>/Pruebas/<fecha>-<máquina>-<tipo>.json`: de la
// MÁQUINA, no del evento (el evento sigue recibiendo su línea en la lista de chequeo y su
// copia en Eventos/<evento>/Pruebas/). Los de capacidad de antes están en `<datos>/Capacidad/`
// y se leen igual.
//
// Lo común de cada informe (`sobre`): cuándo, el tipo, la versión del programa, los
// parámetros, la FICHA de cada máquina (la local y la remota o las generadoras: nombre,
// sistema, CPU, núcleos, memoria, GPU, la red y si es Wi-Fi o cable), los resultados, las
// estadísticas y las series. Nada privado: credenciales, códigos, claves y tokens se sacan
// antes de guardar (`limpiarPrivado`).
//
// Puro (sin Qt): lo usan ui/HistorialPruebasViewModel (la página Pruebas → Historial) y las
// herramientas MCP pruebas_historial / pruebas_ver. Lo que toca el disco va en el pool.
// Pruebas: test_historial_pruebas.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::sala::historial {

constexpr const char* kCarpeta = "Pruebas";
/// Donde guardaba la capacidad antes (se lee, no se escribe).
constexpr const char* kCarpetaVieja = "Capacidad";

/// Saca lo privado (en cualquier nivel): credencial, codigo, codigoInternet, clave, token,
/// pin, comando, comandoWindows, comando_externo, secreto.
void limpiarPrivado(nlohmann::json& j);
/// El informe con lo común: `tipo`, `version` (la del programa), `fecha` (si no la tenía) y
/// sin lo privado. `tipo`: sala | local | remoto | recibida | internet | asamblea.
nlohmann::json sobre(nlohmann::json informe, const std::string& tipo, const std::string& version, int64_t ahoraMs);
/// El nombre del archivo: <aaaammdd-hhmmss>-<máquina>-<tipo>.json (sin caracteres raros).
std::string nombreArchivo(int64_t fechaMs, const std::string& maquina, const std::string& tipo);
/// ¿`archivo` es un nombre del historial (sin carpetas, sin «..», .json)?
bool nombreValido(const std::string& archivo);

/// Una fila de la lista: {archivo, fecha, tipo, maquina, remota, frase, conMargen, maximo,
/// limita, participantes, decodificar, generadoras, version}.
nlohmann::json resumen(const nlohmann::json& informe, const std::string& archivo);
/// Las medidas que se comparan: [{clave, a, b}] (las de `resumen` más las estadísticas).
nlohmann::json comparar(const nlohmann::json& a, const nlohmann::json& b);
/// La lista en CSV (una fila por prueba, con encabezado).
std::string csv(const nlohmann::json& resumenes);

/// EN EL POOL: los resúmenes de `<datos>/Pruebas` y `<datos>/Capacidad`, lo más nuevo
/// primero, como mucho `max`.
nlohmann::json listar(const std::filesystem::path& datos, int max = 200);
/// EN EL POOL: el informe entero (null si no está o el nombre no vale).
nlohmann::json leer(const std::filesystem::path& datos, const std::string& archivo);

} // namespace ba::app::sala::historial
