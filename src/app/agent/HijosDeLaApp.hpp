// ============================================================================
// src/app/agent/HijosDeLaApp.hpp — los servidores que lanza la app mueren con ella
//
// El servidor de OpenCode (`opencode serve`) y llama-server son QProcess de la app: se
// paraban al cerrarla (aboutToQuit). Pero si la app muere sin cerrar (el launcher la mata
// por colgada y la relanza, una caída) quedaban huérfanos (PPID 1) con su puerto, su
// memoria y sus propios hijos (los MCP de OpenCode), y la app nueva levantaba otro.
// Ahora, cada uno:
//   - en Windows, a un Job Object con KILL_ON_JOB_CLOSE: si la app muere, el sistema lo
//     mata con todo lo que haya lanzado;
//   - en POSIX, en un grupo de procesos propio: parar() le manda la señal a todo el
//     grupo; en Linux, además, PR_SET_PDEATHSIG (muere con la app);
//   - anotado en Runtime/hijos/<nombre>.json (pid y ejecutable): macOS no tiene cómo
//     atarlo a la app, así que la app, al arrancar, mata a los que quedaron de la corrida
//     anterior (barrerHuerfanos; también en Linux: lo que lancen ellos no tiene PDEATHSIG).
//     Solo si el pid sigue siendo ESE ejecutable (un pid reusado por otro programa no se toca).
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
/// Lo para con todo su grupo (TERM; KILL si en 3 s no se fue) y lo saca de la lista.
void parar(QProcess& p, const QString& nombre);
/// Al arrancar la app: mata a los anotados que siguen vivos (de una corrida que murió).
void barrerHuerfanos();

} // namespace ba::app::agent::hijos
