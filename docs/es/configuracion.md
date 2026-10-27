[English](../en/configuration.md) · **Español** · [← README](../../README.es.md)

# Configuración de referencia

Lo que se puede ajustar a mano. En el uso normal no hace falta: el asistente de eventos lo configura por vos (ver el [README](../../README.es.md)). Esto queda como referencia.

## Configurar la emisión (`Config/app.json`)

La primera vez, A escribe `release/Config/app.json` con valores por defecto
(fondo + título, 1080p30, dos invitados). Todo es editable:

```json
{
  "video":   {"width": 1920, "height": 1080, "fps": 30},
  "encoder": {"video": "auto", "bitrateKbps": 6000},
  "record":  {"enabled": true, "dir": "Recordings", "container": "mkv"},
  "stream":  {"enabled": true, "server": "rtmp://a.rtmp.youtube.com/live2", "keyEnv": "BA_STREAM_KEY"},
  "guests":  {"count": 2},
  "sources": [
    {"id": "v4l2_input", "name": "camara", "settings": {"device_id": "/dev/video0"}},
    {"id": "text_ft2_source_v2", "name": "titulo", "settings": {"text": "En vivo"}, "rect": [0.05, 0.05, 0.6, 0.12]}
  ]
}
```

- **`sources`** acepta las fuentes del motor con sus *settings* (los ids son
  los de siempre: cámara `v4l2_input`, `image_source`, `ffmpeg_source` para
  archivos o URLs, `pulse_input_capture`, etc.). `rect` es la ubicación en
  fracciones del lienzo `[x, y, ancho, alto]`. Una fuente que falla (cámara
  desenchufada) se omite con un aviso: no tumba la emisión.
- **`encoder.video`**: `auto` elige el hardware disponible en esta máquina y cae
  a x264 si no hay o si falla al arrancar. Alias: `vaapi` (AMD/Intel en Linux),
  `nvenc`, `qsv`, `amf`, `videotoolbox` (macOS) y `x264`. En una GPU AMD
  integrada (Ryzen) `auto` usa VAAPI (`vah264enc`).
- **La clave de transmisión no va en el archivo**: se tipea al salir al aire
  (queda cifrada en `Credenciales/`) o se lee de la variable de entorno que
  indique `keyEnv` (por defecto `BA_STREAM_KEY`).

Opciones de `ba-app` por línea de comandos (pisan el archivo): `--encoder=`,
`--bitrate=`, `--resolution=WxH`, `--fps=`, `--record`/`--no-record`,
`--rec-dir=`, `--stream-server=`, `--guests=`, `--duration=<seg>`,
`--config=<archivo>`. Para pasarlas desde el launcher:
`--app-arg=--record`. `ba-app` también corre solo, sin launcher:

```bash
./release/bin/64bit/ba-app --record --duration=10
```

Para diagnosticar el motor sin la escena completa:
`./release/bin/64bit/ba-app --engine-selftest` (lista los codificadores
disponibles, graba unos segundos y verifica el archivo; devuelve 77 si no hay
contexto gráfico).

## La conferencia (gateway)

El launcher ya lo levanta. Para correrlo solo:

```bash
BA_GATEWAY_PIN=123456 ./release/bin/64bit/ba-gateway --port 8787
```

Los invitados entran por el enlace de la página **Conferencia** de la app (o
`https://127.0.0.1:8789`): nombre, sala y PIN → **lobby** (el operador los ve y
les habla en privado; ellos solo ven al operador) → admitidos a la sala, con
cámara, micrófono, **pantalla o ventana compartida**, archivos para mostrar,
chat y mano levantada. El operador maneja todo desde la página Conferencia o desde el
**dashboard web** (`/control/`: Servicios → Control web, se empareja con un QR
y se aprueba en la app; anda en celular o tablet).

- **WebRTC real** en Linux: `make deps-webrtc BA_WEBRTC_TARGET=linux` y
  `make build` compilan el gateway con libwebrtc (estático, sin dependencias
  del sistema).
- **HTTPS propio** (cámara/micrófono exigen página segura) con certificado
  propio (dejar `cert.pem` + `key.pem` en `Credenciales/`: se importan
  cifrados) o autofirmado automático.
- **Acceso directo por internet** sin Cloudflare: túnel **WireGuard en espacio
  de usuario** hacia un servidor propio con IP pública (`make deps-lwip`); el
  asistente de la página Conferencia genera la configuración del servidor.

Detalle completo (arquitectura, lobby, API, servidor, seguridad):
[`docs/funciones/CONFERENCIA.md`](../../docs/funciones/CONFERENCIA.md).

## Todo en la carpeta portable

El appliance corre desde cualquier carpeta (un pen drive, por ejemplo) y **todo
lo que guarda queda adentro de esa carpeta**, nunca en el perfil del usuario
ni en la máquina:

| Carpeta | Qué guarda |
|---|---|
| `Config/` | configuración (escenas, ajustes, tablero) |
| `Credenciales/` | tokens y claves (YouTube, Twitch, Facebook, MCP, WireGuard, HTTPS), **cifrados** con AES-256-GCM; una actualización nunca los toca |
| `Respaldos/` | copia de `Config/` y `Credenciales/` antes de cada actualización (quedan 5) |
| `Runtime/` | archivos de encuentro de la sesión (API de la sala, puerto MCP) |
| `Cache/` | cachés de Qt/QML, fontconfig y shaders de la GPU (se redirigen al arrancar) |
| `Logs/`, `Recordings/`, `Shares/`, `Media/` | registros, grabaciones y archivos |

En Linux el canal de control y el lock de instancia única usan sockets del
espacio **abstracto** del kernel: no escriben nada en ningún disco. (En macOS,
que no lo tiene, van a `$TMPDIR`, temporal y por usuario.)

**Credenciales sin contraseña.** El equipo arranca solo; el cifrado evita que
se lean por accidente (un pen drive FAT/exFAT no tiene permisos), pero la
clave del equipo (`Credenciales/clave-equipo`) viaja en el mismo pen drive:
quien se lleve el pen drive entero tiene todo. Para mudar el equipo a otro
pen drive, copiar `Credenciales/` completa. Las versiones anteriores guardaban
todo en claro en `Config/`: al primer arranque se importa y se borra.

## Actualizar

Descomprimir la versión nueva en otra carpeta y elegirla en **Configuración →
Sistema y datos → Actualizar desde una carpeta** (o `BroadcastAppliance
--update <carpeta> --start`, o la herramienta MCP `system_update`). La app se
cierra; el launcher respalda `Config/` y `Credenciales/` en `Respaldos/`,
reemplaza **solo el programa** (todo o nada: si algo falla, queda la versión
anterior entera) y vuelve a abrir. La versión anterior queda guardada:
**Volver a la versión anterior** (`--update-rollback`).

## Asistente de IA (MCP)

Un servidor **MCP** integrado deja que un LLM con visión maneje y vea toda la
app: escenas, fuentes, audio, salidas, configuración, sala y oradores, con
capturas del programa, de cada persona y de la interfaz. Solo en
`127.0.0.1:8790` con token (cifrado en `Credenciales/`; `bin/64bit/ba-mcp
--token` lo muestra); puente stdio `ba-mcp`. Ver
[`docs/funciones/MCP.md`](../../docs/funciones/MCP.md).

## Asistente de IA dentro de la app

La pestaña **✦** (abajo de todo en la barra) es un chat con el LLM que maneja la
app: corre la interfaz oficial de **Claude Code** (suscripción Pro/Max o clave
de API), **Gemini CLI** (cuenta de Google o clave) u **opencode** (OpenCode Go u
OpenRouter) en una terminal propia, ya conectada al servidor MCP y con un
prompt del sistema que explica cada parte del sistema y las buenas prácticas.
El agente se instala desde la pestaña (versión fijada y verificada) dentro de
la carpeta portable. Con las **vigilancias**, el servidor avisa solo en ese
chat cuando algo pasa (un canal satura, termina un tema, a un orador se le
acaba el tiempo, alguien espera en el lobby...) y puede actuar en el acto. Ver
[`docs/funciones/MCP.md`](../../docs/funciones/MCP.md).

**Salvaguarda.** Lo que escriben los invitados o el público nunca se convierte
en acciones solas: el servidor MCP marca en código todo contenido que viene de
ellos, y una sesión del asistente que lo leyó no puede modificar nada sin que
el operador lo apruebe con un click (la tarjeta muestra la acción exacta). No
depende del modelo. El asistente además puede avisar en una **barra abajo de
la ventana** (color según prioridad), hacer preguntas y sugerir respuestas a
los invitados que se mandan con un click.

## Entradas de video

Página **Entradas** (◉ en la barra) y por MCP: cámaras USB y capturadoras
(V4L2 / DirectShow / AVFoundation, con formato y resolución), cámaras IP y
señales de red por RTSP, RTMP(S), **SRT**, HLS/HTTPS y UDP (FFmpeg del bundle,
con reconexión y latencia baja), y **NDI®** con una implementación abierta
(libNoIDea de VideoLAN, LGPL, experimental; recibe NDI completo/SpeedHQ, no
NDI|HX) — sin el SDK propietario, en Linux y en Windows. Miniatura viva de cada entrada. NDI® es una
marca de Vizrt NDI AB; este proyecto no está afiliado a Vizrt.

## Audio: rack por fuente (LSP)

Pestaña **Audio** (🎚 en la barra): a la izquierda las fuentes con audio (micrófonos,
capturas y también videos); al elegir una aparece su **patchbay** — `in` → módulos →
`out` — donde se agregan módulos (gate, compresor, de-esser, EQ, multibanda,
limitador, analizador, generador o cualquiera de los ~200 de LSP), se conectan
arrastrando desde las salidas y se desconectan con ✕. Cada módulo muestra su
nivel de entrada/salida y cuánto está bajando; el panel de detalle tiene los
parámetros en unidades reales, presets y el botón para abrir la interfaz
original de LSP. El analizador da LUFS (momentáneo, corto, integrado), espectro
por tercios, piso de ruido y pistas de mejora. Los racks se guardan en
`Config/audio-racks.json`. Por MCP el asistente mide, arma y calibra (p. ej. un
gate midiendo con la persona callada y hablando): `help {"topic": "audio_rack"}`.

## YouTube, Twitch y Facebook

Una pestaña por plataforma en la barra izquierda. Sin configurar, cada una
explica paso a paso (con enlaces) cómo crear la app propia en Google Cloud,
dev.twitch.tv o developers.facebook.com; ya conectada, administra videos,
vivos (crear, transmitir desde la app, terminar), canal/Página y chat. La
fuente de escena **Chat en vivo** mezcla los chats de las tres y el de la sala
sobre el video. Todo también por MCP. Credenciales y tokens quedan cifrados
en `Credenciales/` dentro de la carpeta portable, incluida la clave de la
última salida al aire (`stream-last`: evita tipearla de nuevo y permite retomar
la emisión tras una caída). Facebook solo permite transmitir a Páginas (no a perfiles).

Secretos por entorno, nunca por línea de comandos (se ven en la lista de
procesos): `BA_GATEWAY_PIN` (si falta, se genera uno y se muestra en la app),
`BA_TUNNEL_TOKEN` (Cloudflare), `BA_WG_PRIVATE_KEY` / `BA_WG_PSK` (WireGuard;
si falta, la clave vive cifrada en `Credenciales/`). La clave HMAC
de los tokens es aleatoria por arranque (`BA_GATEWAY_HMAC_SECRET` la fija).
