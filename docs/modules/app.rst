.. ============================================================================
   ba-app — Proceso A (motor de emisión + UI de operación)
   ============================================================================

..
   Página del componente app (src/app). Es un SKELETON: solo el andamiaje de
   build. Documenta su rol previsto (spec §6).

ba-app (Proceso A)
==================

`ba-app` es el **Proceso A**: motor de emisión + UI de operación (spec §6). En
Windows enlaza **Qt 6** + **libobs**; en Linux se configura como esqueleto para
que el árbol compile. Usa `/MD` (CRT dinámica) por consistencia con libobs y
Qt.

Estado actual: **SKELETON** — solo el andamiaje de build. La lógica (Qt 6 sobre
libobs, escenas, encoding, UI del operador) la aporta el agente del Proceso A.
Enlaza `ba_core`.

Rol previsto (spec §6)
----------------------

- **Motor de emisión** sobre libobs: escenas, fuentes, encoding y salida en
  vivo (RTMPS).
- **UI de operación** (spec §5): escenas, zócalos, proyectores, pánico y
  monitor (Fase 5, GATED:Qt/libobs).
- **Fuentes IPC** en `ba-sources.dll` que consumen los rings de audio, el
  transporte Spout y el canal de control del núcleo (ver :doc:`modules/core`).
- **Host de plugins** (VST3/LSP): embebe los editores de los bundles `.vst3`
  vía `ba::plugin::Vst3Host` (ver :doc:`modules/plugin-host`).
- **DSP nativo**: channel strip, automixer y presets del núcleo (`ba::dsp`,
  ver :doc:`modules/core`) como filtros libobs RT-safe.

Instalación
-----------

Proceso A va a `bin/64bit/` (spec §5.1).
