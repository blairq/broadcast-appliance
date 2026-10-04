.. ============================================================================
   ba-sources — Módulo propio de fuentes libobs
   ============================================================================

..
   Página del componente obs-module (src/obs-module). IMPLEMENTADO contra
   libobs real: ba_module.cpp (ABI obs_module_load), ba_ipc_sources.cpp
   (ba_guest_audio / ba_guest_video) y ba_dsp_sources.cpp (ba_channel_strip /
   ba_channel_preset).

ba-sources (módulo libobs)
=========================

`ba-sources` es el **módulo propio de fuentes libobs**: una biblioteca
compartida que OBS carga como plugin y que se despliega en
`obs-plugins/64bit/` (spec §5.1). Se compila contra libobs **real** en
cualquier plataforma — libobs es cross-platform, así que el módulo no tiene
ningún ``#ifdef _WIN32``. Si libobs no está disponible se compila solo un
esqueleto (``ba_placeholder.cpp``) para que el árbol siga compilando.

Estado actual: **IMPLEMENTADO**. ``ba_module.cpp`` expone el ABI que libobs
espera (``obs_module_load``) y registra:

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
- **Filtro «Mejorar imagen»** (``ba_mejora_imagen``): ruido temporal, color
  guiado por el brillo y nitidez CAS, en la GPU y sin demora
  (``data/mejora-imagen.effect``, que libobs traduce a D3D11 u OpenGL).
- **Filtro «PTZ digital»** (``ba_ptz_digital``): pan/tilt/zoom recortando en
  la GPU (bicúbica), con joystick y presets por proc handler
  (``data/ptz-digital.effect``). Lo maneja el módulo PTZ de la app: ver
  ``docs/PTZ.md``.

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
