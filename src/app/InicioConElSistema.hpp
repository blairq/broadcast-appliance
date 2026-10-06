// ============================================================================
// src/app/InicioConElSistema.hpp — que el aparato arranque solo al encender
//
// Opcional y apagado de fábrica. Prendido, el sistema abre el programa al
// iniciar la sesión del usuario:
//   macOS    ~/Library/LaunchAgents/ar.agora.broadcast-appliance.plist (open <la .app>)
//   Windows  HKCU\Software\Microsoft\Windows\CurrentVersion\Run «BroadcastAppliance»
//   Linux    ~/.config/autostart/broadcast-appliance.desktop
// Apunta a la copia portable que lo prendió; si se movió la carpeta, `revisar()`
// (en cada arranque) corrige la ruta.
//
// Para que vuelva SOLO después de un corte de luz faltan dos cosas del equipo,
// que el programa no puede tocar (piden ser administrador): que la máquina se
// encienda al volver la luz (macOS: Ajustes → Energía → «Arrancar
// automáticamente después de un corte»; PC: BIOS → «Restore on AC power
// loss») y que la sesión se inicie sola (inicio de sesión automático). Al
// volver, el programa retoma lo que estaba al aire (al-aire.json).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ba::app::inicio {

/// ¿Está prendido (y apunta a esta copia)?
bool activo();
/// Lo prende o lo apaga. "" o el error.
std::string poner(bool activo);
/// En cada arranque: si está prendido pero apunta a otra ruta, la corrige.
void revisar();
/// {activo, ruta, notas: [lo que falta del equipo para volver solo tras un corte]}
nlohmann::json estado();

} // namespace ba::app::inicio
