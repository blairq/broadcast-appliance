# Próximo trabajo

Lo pedido el 2026-10-23 se hizo el 2026-10-25 (siete áreas en paralelo, integradas y probadas en
la Mac). Queda lo de abajo.

## Hecho (2026-10-25)

- Riel: abierto o cerrado, los botones quedan en el mismo lugar.
- Terminal: clic en una opción numerada (Claude Code, Antigravity) manda su número; clic en la línea
  del cursor lo mueve (solo si el programa muestra el cursor real: Claude Code no).
- Gráficos animados: sin parpadeo en la vista previa; GIF y videos con alfa adentro; tortas y barras
  con datos que cambian en vivo; plugin como gráfico (ejemplo `ejemplo-grafico`).
- Nodo «Mezcla de GStreamer» (cualquier módulo de varias entradas; probado glvideomixer lado a lado).
- Grilla de personas (`ba_grilla`), con reparto entre grillas; el mosaico lo arma la sala.
- Atención hablada en el lobby de la Sala, «Voz del sistema» con «Voz del atendedor», alta
  automática en Personas, «entra solo» del padrón visible en Sala/Asamblea/Personas.
- Salidas → Pantallas: un solo catálogo de cosas que se pueden mostrar; monitores con orden físico,
  asignación recordada y espera del que no está.
- Dispositivos y equipos: un solo registro con nombres de la máquina; «Entradas de audio».
- Plugins: ejemplos (video con GStreamer, automatización, equipo de red), «Nuevo desde un ejemplo»,
  y el asistente ofrece hacer plugins (también elementos de GStreamer en Python).

## Pendiente

- **Probar en Windows** (la PC): d3d12compositor en la mezcla, tortas en ba-grafico, VP9 con alfa,
  ids de monitores (`\\.\DISPLAYn` si no informan serie), ids de dispositivos, `libyuv::MJPG*` en
  webrtc.lib, `test_ejemplos.py` de plugins con el Python de la PC.
- **«Hablando» en la Asamblea:** quien espera manda solo foto; hace falta que la página de la sesión
  abra el micrófono hacia el gateway mientras espera.
- **Video en vivo dentro de un gráfico** (una cámara en el zócalo): animar el ítem de la escena con la
  línea de tiempo del gráfico. Propuesta: la ficha del gráfico declara claves por fuente y el Emisor
  le pide al motor que las interpole (Composicion ya anima capas en las transiciones).
- **Grilla de personas:** una vista en vivo de quién está en cada grilla (la propiedad existe, falta
  el QML); «al aire» no cuenta otras grillas ni escenas anidadas; bajar la calidad de los que están
  en la fila y no aparecen en ninguna grilla.
- **Pantallas:** OutputWindowsModel (ventanas de video sueltas que se recuerdan) sigue aparte de las
  asignaciones de monitor; `StudioViewModel::monitores()` y la herramienta `monitors` listan por su
  cuenta.
- **Dispositivos:** `audio_devices`/`video_devices` (MCP) siguen con nombres técnicos; el botón
  «+ Entrada de audio del sistema» de Audio duplica el de Entradas de audio.
- **Mezcla:** el error de una composición (un módulo que no negocia) queda en el log, no en la tarjeta.
