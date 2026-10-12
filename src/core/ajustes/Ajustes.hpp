// ============================================================================
// src/core/ajustes/Ajustes.hpp — los ajustes «avanzados» de Configuración
//
// Tiempos, topes y reintentos que antes estaban fijos en el código (relevamiento
// en docs/planes/VALORES-FIJOS.md). Cada proceso carga los suyos al arrancar:
//   - ba-app: app.json → "ajustes" (y de nuevo al aplicar en Configuración);
//   - ba-gateway: gateway.json → "ajustes";
//   - el launcher: launcher.json → "ajustes".
// El código los consulta EN EL MOMENTO de usarlos, con el valor de siempre como
// respaldo: `ajustes::entero("stream.colaMs", 1000)`. Lo que no está cargado
// vale el respaldo (así un proceso viejo o un test andan igual). La página
// (SettingsModel) tiene el mismo valor por defecto, el rango y la explicación.
//
// No va en caminos calientes por cuadro: se lee al armar algo, al programar una
// espera o en un temporizador.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace ba::ajustes {

/// Reemplaza todos los valores (un objeto {clave: valor}; lo demás se ignora).
void cargar(const nlohmann::json& valores);
/// Una sola clave (null la saca: vuelve al respaldo).
void poner(const std::string& clave, const nlohmann::json& valor);
nlohmann::json todos();

double numero(const char* clave, double respaldo);
int entero(const char* clave, int respaldo);
std::int64_t entero64(const char* clave, std::int64_t respaldo);
bool siNo(const char* clave, bool respaldo);
std::string texto(const char* clave, const std::string& respaldo);

/// Atajos de unidades: lo guardado está en la unidad del nombre de la clave.
inline std::int64_t msANs(const char* clave, int respaldoMs) {
    return entero64(clave, respaldoMs) * 1'000'000LL;
}
inline std::int64_t sANs(const char* clave, int respaldoS) {
    return entero64(clave, respaldoS) * 1'000'000'000LL;
}
inline int sAMs(const char* clave, int respaldoS) { return entero(clave, respaldoS) * 1000; }

} // namespace ba::ajustes
