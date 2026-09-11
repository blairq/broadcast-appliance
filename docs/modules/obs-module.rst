.. ============================================================================
   ba-sources — Módulo propio de fuentes libobs
   ============================================================================

..
   Página del componente obs-module (src/obs-module). IMPLEMENTADO:
   ba_sources.cpp con obs_module_register, una fuente y un filtro basados en
   ba_core::dsp y presets podium/lavalier/handheld. Feature-flag
   BA_SOURCES_HAVE_OBS. Stubs locales en src/obs-module/obs/.

ba-sources (módulo libobs)
=========================

`ba-sources` es el **módulo propio de fuentes libobs**: una **DLL** (shared
lib) que OBS carga como plugin y que se despliega en `obs-plugins/64bit/`
(spec §5.1). En Windows se llama `ba-sources.dll` y enlaza libobs (feature-flag
`BA_SOURCES_HAVE_OBS`); en el cross-build MinGW/Linux se compila contra los
**stubs locales** vendoreados en `src/obs-module/obs/` (headers mínimos de la
API de libobs + `obs-stub.cpp` con las definiciones de enlace) para que el
árbol compile sin libobs instalada. Usa `/MD` (CRT dinámica) por consistencia
con libobs.

Estado actual: **IMPLEMENTADO**. `ba_sources.cpp` registra el módulo
(`obs_module_register`) y aporta:

- **Filtro de channel strip**: la cadena nativa `ba::dsp::ChannelStrip`
  (HPF → EQ → de-esser → gate → comp → lim) corriendo como filtro libobs
  RT-safe dentro de la DLL (spec 7.2).
- **Fuente de preset de canal**: expone los tres presets de fábrica
  (`ba::dsp::presetPodium`/`presetLavalier`/`presetHandheld`) como fuente,
  aplicables por canal (spec 7.3).
- **Automixer de mesa redonda**: `ba::dsp::Automixer` (Dugan) como objeto
  compartido entre filtros (spec 7.3).
- **Fuentes IPC**: consumen los rings de audio
  (`ba::ipc::SharedAudioRing`), el transporte Spout (`ba::ipc::SpoutTransport`)
  y el canal de control (`ba::ipc::ControlProtocol`) para el retorno de
  invitados (spec §4).

Enlaza `ba_core`.

Rol (spec §7, §4)
-----------------

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
