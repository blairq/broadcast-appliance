// ============================================================================
// src/app/agent/HijosDeLaApp.hpp — los servidores que lanza la app mueren con ella
//
// El servidor de OpenCode (`opencode serve`), llama-server y los agentes de los roles son
// QProcess de la app: se paraban al cerrarla (aboutToQuit). Pero si la app muere sin
// cerrar (el launcher la mata por colgada y la relanza, una caída) quedaban huérfanos
// (PPID 1) con su puerto, su memoria y sus propios hijos (los MCP de OpenCode, que él
// lanza desprendidos: ni su grupo ni su muerte se los llevan), y la app nueva levantaba
// otro. Ahora, cada uno:
//   - en Windows, a un Job Object con KILL_ON_JOB_CLOSE: si la app muere, el sistema lo
//     mata con todo lo que haya lanzado;
//   - en POSIX, en un grupo de procesos propio, y parar() termina su ÁRBOL entero (ver
//     platform/Huerfanos); en Linux, además, PR_SET_PDEATHSIG (muere con la app);
//   - anotado en Runtime/hijos/<nombre>.json (pid, ejecutable y la app que lo lanzó): si
//     la app muere sin pararlo, lo barren la app al arrancar y el launcher al arrancar y
//     al cerrar (barrerHuerfanos, platform::huerfanos::barrer). Solo si el pid sigue
//     siendo ESE ejecutable (un pid reusado por otro programa no se toca).
// ============================================================================
#pragma once

#include <QString>

class QProcess;

namespace ba::app::agent::hijos {

/// Antes de start(): el hijo en su grupo de procesos (POSIX) y, en Linux, que muera con la app.
void preparar(QProcess& p);
/// Después de que arrancó: al Job Object (Windows) y anotado para barrerlo si la app muere
/// sin pararlo. `nombre`: uno por servidor («opencode», «llama-server»).
void anotar(const QProcess& p, const QString& nombre);
/// Lo para con todo su árbol (TERM; KILL si en 3 s no se fue) y lo saca de la lista.
void parar(QProcess& p, const QString& nombre);
/// Lo mata ya, con todo su árbol (sin esperar), y lo saca de la lista.
void matar(QProcess& p, const QString& nombre);
/// Terminó solo: lo saca de la lista.
void olvidar(const QString& nombre);
/// Al arrancar la app: mata a los anotados que siguen vivos (de una corrida que murió).
void barrerHuerfanos();

} // namespace ba::app::agent::hijos
