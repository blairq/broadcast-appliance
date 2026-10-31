.. ============================================================================
   ba-app — Proceso A (motor de emisión + UI de operación)
   ============================================================================

..
   Página del componente app (src/app): interfaz QML + BroadcastEngine sobre
   el motor de GStreamer (src/motor).

ba-app (Proceso A)
==================

`ba-app` es el **Proceso A**: motor de emisión + UI de operación (spec §6). La
interfaz es QML (Qt 6 vendoreado) y el motor es GStreamer (`src/motor`), a
través de `BroadcastEngine` (`src/app/emision`). libobs ya no se usa. Sin Qt,
`main.cpp` arranca en modo consola (`runOperate`); sin GStreamer no hay motor.

Autotest: ``ba-app --engine-selftest`` (``emision/EngineSelfTest``) ejercita el
motor real de punta a punta (escena, retorno a B, codificadores, una grabación
breve).

Enlaza `ba_core`.

Rol (spec §6)
-------------

- **Motor de emisión** sobre libobs: escenas, fuentes, encoding y salida en
  vivo (RTMPS).
- **UI de operación** (spec §5): monitor program + panel de canales (Fase 5,
  GATED:Qt/libobs).
- **Fuentes IPC** en `ba-sources.dll` que consumen los rings de audio, el
  transporte Spout y el canal de control del núcleo (ver :doc:`modules/core`).
- **Rack de audio** (`src/app/audio`, pestaña Audio): un filtro OBS
  `ba_audio_rack` por fuente con un grafo de módulos LSP (host CLAP propio,
  `ClapHost`; la interfaz original de cada módulo en una ventana X11/Win32,
  `ClapEditor`), medidores por módulo, analizador (LUFS, espectro, piso de
  ruido) y generador (`Analyzer`). Manejable por MCP (categoría `audio_rack`).
- **DSP nativo**: channel strip, automixer y presets del núcleo (`ba::dsp`,
  ver :doc:`modules/core`) como filtros libobs RT-safe.

Instalación
-----------

Proceso A va a `bin/64bit/` (spec §5.1).
