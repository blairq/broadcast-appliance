// ============================================================================
// src/gateway/Borde.hpp — lo común de los bordes de la sala
//
// La botonera, el control web, la sesión remota y el registro por tótem (y su
// atención) son bordes de red con las mismas defensas: si la IP es de la red local,
// si el Origin es el Host que pidió, las credenciales guardadas como hash con sal, y
// la página servida desde su carpeta sin salirse de ella. Antes cada uno tenía su
// copia (y unos dependían de los estáticos de la botonera): acá hay una.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::gateway::borde {

/// La IP (orden de red) en texto: es para que el operador reconozca el aparato.
std::string ipTexto(std::uint32_t ip);
/// ¿De la red local? 10/8, 127/8, 172.16/12, 192.168/16, 169.254/16.
bool ipLocal(std::uint32_t ip);
/// ¿El Origin es el Host que se pidió? Sin Origin no es un navegador de otra página.
bool mismoOrigen(const std::string& headers);
/// SHA-256 en hexadecimal (las credenciales se guardan como hash de sal + token).
std::string hashConSal(const std::string& salYToken);
/// El tipo de un archivo de las páginas, por su extensión.
const char* tipoDe(const std::string& rel);
/// La página `rel` de `webroot` (sin salirse de la carpeta), con `cabeceras` de seguridad
/// (la CSP de cada borde). Siempre contesta (404/400 si no). :returns: true.
bool servir(long long fd, const std::string& webroot, const std::string& rel, const char* cabeceras);

} // namespace ba::gateway::borde
