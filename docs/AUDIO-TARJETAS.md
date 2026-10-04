# Las tarjetas del rack: entradas, voz a texto, texto a voz, grabar

Cada fuente con audio tiene su **rack** (Audio → patchbay): un grafo de tarjetas unidas con cables, como JACK. Este
documento cubre las tarjetas que mueven audio y texto de un lado a otro del sistema. Las de procesamiento (gate,
compresor, EQ…) están en el catálogo LSP.

## Entrada: el audio de otra fuente

**«+ Entrada»** trae el audio de otra fuente (un micrófono, un invitado, un canal de una placa), después de su propio
rack, como una tarjeta más del grafo. No se conecta sola: se cablea a donde haga falta (a la mezcla, al sidechain de un
compresor, a una voz a texto).

- No se puede traer el audio de la misma fuente, ni armar un lazo entre racks (A trae de B y B trae de A): el sonido
  daría vueltas con eco. Las dos cosas se rechazan.
- Si la fuente es una entrada de placa, su captura se abre sola: no hace falta rutearla en la Matriz.
- **El rack procesa mientras su fuente suena.** Para armar una mezcla con tarjetas Entrada conviene ponerlas en una
  fuente que siempre tenga audio: una **entrada virtual** de la Matriz (que suena siempre, aunque sea silencio) o un
  micrófono. En un video detenido, la tarjeta no recibe nada.
- MCP: `audio_module_add {"source", "module": "entrada", "fuente"}`.

**¿Y la Matriz?** La Matriz rutea fuentes enteras a pistas, a la escucha, a salidas físicas y a buses virtuales. La
tarjeta Entrada sirve dentro de un grafo: para mezclar de a dos dentro de un rack, para una entrada lateral (sidechain),
o para transcribir dos fuentes juntas.

## Voz a texto: salidas de texto con nombre

**«+ Voz a texto»** transcribe lo que le llega (Parakeet, 25 idiomas europeos, el idioma se detecta solo). Puede haber
varias por rack. Cada una tiene:

- **Nombre de la salida de texto** (único en todo el sistema; de fábrica, el de la fuente). Con ese nombre se usa en
  otras partes: disparadores, texto a voz, MCP (`texto_flujos`, `voz_transcripcion {flujo}`).
- **Modo**: *dictado* (con F9, al asistente) o *continuo* (sin parar).
- **Archivo**: `Recordings/Transcripciones/<nombre>-<fecha>.txt` (con hora) y `.srt` (subtítulos con tiempos).
- **Subtítulos en**: una fuente de texto de la escena cuyo texto pasa a ser la última frase.

Las salidas de texto viven en `Studio.flujos` (agent/FlujosTexto).

## Disparadores por lo que se dice

Los **disparadores** (antes «vigilancias») tienen un tipo nuevo, `texto`: salta cuando una salida de texto dice algo
que coincide.

```json
{"type": "texto", "params": {"flujo": "Orador", "palabras": ["cuarto intermedio", "votación"]}}
```

`flujo` ("*" = todas), `contains`, `palabras` (alcanza con una) o `regex`, sin distinguir mayúsculas ni tildes. Es lo
que dijo una persona: al asistente le llega como dato no confiable, y sus acciones piden aprobación.

## Texto a voz

**«+ Texto a voz»** dice con una voz elegida lo que llega por una salida de texto, o lo que se le pide. Su audio sale
por su puerto y se cablea como cualquier otro: a la mezcla, a la salida de audio, a otra voz a texto.

- **Voz**: un modelo piper con su idioma (castellano rioplatense y de España, inglés de EE. UU. y británico,
  portugués de Brasil, francés, italiano, alemán, catalán). Las que no están se bajan desde la misma tarjeta (unos
  80 MB cada una). Vacío = la voz del sistema.
- **Lee**: la salida de texto que dice en voz alta.
- **Velocidad** y **hablante** (las voces que traen más de uno).
- Como la tarjeta Entrada, conviene ponerla en una fuente que siempre suene (una entrada virtual).
- MCP: `voces {idioma}`, `texto_a_voz {fuente, voz, lee, velocidad, decir}`.

## Grabar lo que le llega a la salida

La tarjeta **«Salida de audio»** tiene un interruptor **«Grabar todo lo que le llega»**: un `.wav` (32 bits flotante,
estéreo, 48 kHz) en `Recordings/Audio/<fuente>-<fecha>.wav`, escrito en un hilo propio. Antes de los 4 GB de un WAV
(unas 2 h 50 min) sigue en `-parte2.wav`. Si la tarjeta no tiene cable, lo avisa. MCP: `audio_output_set {grabar}`.

## Un ejemplo: subtitular y repetir en otro idioma

1. Matriz → «+ Entrada virtual» «Traducción».
2. En el rack de «Traducción»: «+ Entrada» desde el micrófono del orador → «+ Voz a texto» (salida «Orador», continuo,
   con archivo y subtítulos en el rótulo «Subtítulos»).
3. Barra del rack → «Traducción…» (o Configuración → Traducción): traducir «Orador» al inglés; publica «Orador · en»
   (con subtítulos en un rótulo y archivo si se quiere). Sin red: modelos OPUS-MT (Argos Translate) en el Python del
   programa, 0,1 a 0,6 s por frase; castellano <-> inglés y <-> portugués directos, francés, alemán, ruso y chino por
   el inglés. Un glosario fija los términos del evento (viene con los de la Legislatura del Chubut); se edita ahí, con
   `glosario_agregar` o en `Datos/Config/glosario-traduccion.json`. MCP: `traducciones`, `traduccion_agregar`,
   `traducir`, `traduccion_bajar`, `glosario_*`; ayuda: `help {"topic": "traduccion"}`.
4. «+ Texto a voz» con una voz en inglés, leyendo «Orador · en», cableada a `out`; en la Matriz, «Traducción» a la
   pista 2 (el canal en inglés).
5. Un disparador `texto` con «cuarto intermedio» avisa en pantalla.

## Notas

- El anillo de las tomas respeta la hora de cada bloque: la voz del sistema y la del teleprompter entregan frases
  enteras agendadas a futuro, y suenan a su hora y enteras (antes, por la Matriz, se oía un chasquido de 80 ms).
- Los racks de fuentes que aparecen después del arranque (entradas virtuales, de placa, invitados) vuelven solos.
- Abrir y cerrar los dispositivos del equipo va en un hilo propio: con el audio del sistema trabado (CoreAudio sin
  responder) la app ya no se cuelga.
