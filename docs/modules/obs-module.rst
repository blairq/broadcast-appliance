.. ============================================================================
   ba-sources — Módulo propio de fuentes libobs
   ============================================================================

..
   Página del componente obs-module (src/obs-module). Es un SKELETON: solo el
   andamiaje de build. Documenta su rol previsto (spec §7, §4).

ba-sources (módulo libobs)
==========================

`ba-sources` es el **módulo propio de fuentes libobs**: una **DLL** (shared
lib) que OBS carga como plugin y que se despliega en `obs-plugins/64bit/`
(spec §5.1). En Windows se llama `ba-sources.dll` y enlaza libobs; en Linux se
configura como esqueleto para que el árbol compile. Usa `/MD` (CRT dinámica)
por consistencia con libobs.

Estado actual: **SKELETON** — solo el andamiaje de build. El contenido real
(filtros de channel strip, fuentes IPC, automixer — spec §7, §4) lo aporta el
agente del módulo. Enlaza `ba_core`.

Rol previsto (spec §7, §4)
--------------------------

- **Filtros de channel strip**: la cadena nativa `ba::dsp::ChannelStrip`
  (HPF → EQ → de-esser → gate → comp → lim) corriendo como filtro libobs
  RT-safe dentro de la DLL (spec 7.2).
- **Automixer de mesa redonda**: `ba::dsp::Automixer` (Dugan) como objeto
  compartido entre filtros (spec 7.3).
- **Presets DSP**: `ba::dsp::presetPodium`/`presetLavalier`/`presetHandheld`
  como sets de parámetros (spec 7.3), ajustables en `dsp_presets.json`.
- **Fuentes IPC**: consumen los rings de audio
  (`ba::ipc::SharedAudioRing`), el transporte Spout (`ba::ipc::SpoutTransport`)
  y el canal de control (`ba::ipc::ControlProtocol`) para el retorno de
  invitados (spec §4).

Instalación
-----------

El módulo va a `obs-plugins/64bit/` (spec §5.1).
