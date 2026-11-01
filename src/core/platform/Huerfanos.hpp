// ============================================================================
// src/core/platform/Huerfanos.hpp — los hijos de la app, anotados, y su barrido
//
// La app lanza servidores y agentes (OpenCode, llama-server, las consolas, los roles) que
// a su vez lanzan los suyos (los MCP: ba-mcp, los de oficina). OpenCode los lanza
// DESPRENDIDOS (su propia sesión: `detached` es lo de fábrica en su lanzador), así que ni
// matar el grupo del hijo ni que el hijo muera se los lleva: quedaban huérfanos (PPID 1).
// Por eso:
//   - terminar un hijo es terminar su ÁRBOL: los descendientes se juntan ANTES de
//     matarlo (mientras su padre vive, se los encuentra por el PPID) y cada uno con su grupo;
//   - cada hijo queda anotado en <datos>/Runtime/hijos/<nombre>.json (pid, ejecutable y
//     DUEÑO: el pid y el ejecutable del proceso que lo lanzó). Si el dueño muere sin
//     pararlo (lo mataron, se cayó), el barrido lo encuentra y termina su árbol. Barren la
//     app al arrancar y el launcher al arrancar y al cerrar; un anotado cuyo dueño sigue
//     vivo (el launcher volvió y readoptó a la app) no se toca. Un pid reusado por otro
//     programa tampoco (se compara el ejecutable).
// En Windows lo hace el Job Object de cada hijo (el sistema mata todo lo que lanzó si la
// app muere): el barrido solo borra las anotaciones de dueños muertos.
// Sin Qt: lo usan la app y el launcher.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ba::platform::huerfanos {

/// <datos>/Runtime/hijos.
std::filesystem::path carpeta();

/// Anota a `pid` (programa `exe`) como hijo de este proceso.
void anotar(const std::string& nombre, std::int64_t pid, const std::filesystem::path& exe);
/// Ya no hay que barrerlo (se paró o terminó solo).
void quitar(const std::string& nombre);

/// Los descendientes de `pid` (hijos, nietos…; sin él), aunque se hayan desprendido de
/// su sesión. Vacío en Windows.
std::vector<std::int64_t> descendientes(std::int64_t pid);
/// Les manda TERM a todos (y a sus grupos), espera hasta `graciaMs` a que se vayan y
/// mata a los que quedan. Con 0, los mata de una. Un zombi cuenta como ido.
void terminar(const std::vector<std::int64_t>& pids, int graciaMs);
/// `pid` con todo lo que lanzó.
void terminarArbol(std::int64_t pid, int graciaMs);
/// Vive (no es un zombi).
bool vivo(std::int64_t pid);
/// El ejecutable de `pid`, con los enlaces resueltos ("" si no vive o no se sabe).
std::filesystem::path ejecutableDe(std::int64_t pid);

struct Barrido {
    std::string nombre;
    std::int64_t pid = 0;
};
/// Termina los anotados en `dir` cuyo dueño ya no vive (con todo su árbol) y borra sus
/// anotaciones. Devuelve los que terminó.
std::vector<Barrido> barrer(const std::filesystem::path& dir = carpeta());

} // namespace ba::platform::huerfanos
