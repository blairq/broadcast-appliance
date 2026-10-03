# Plugins de Python

Un plugin es un programa en Python que le suma al sistema lo que no trae: un equipo que se
maneja de una forma rara (el PTZ de una cámara USB que "solo anda con la app de la marca", una
cámara 360 con dos ojos de pez que además dice hacia dónde habla alguien, varias de esas que
tienen que trabajar juntas), una tarjeta de video o de audio escrita en Python, o una
automatización que maneja el estudio. **Lo escribe el asistente** (ayuda MCP: `help {"topic":
"plugins"}`); el operador no programa.

## Dónde viven

| Qué | Dónde |
|---|---|
| Los plugins del usuario | `Datos/Plugins/<id>/` (en macOS, `BroadcastAppliance Datos/Plugins`): **no** están adentro del programa, así que sobreviven a las actualizaciones |
| Qué está prendido | `Datos/Config/plugins.json` |
| Sus registros | `Datos/Logs/plugins/<id>.log` |
| El intérprete | `Resources/python` (CPython 3.12 de python-build-standalone, `make deps-python`; nada del sistema) |
| El SDK | `Resources/plugins-python/sdk` (fuente: `src/plugins/sdk`) |
| Los ejemplos | `Resources/plugins-python/ejemplos` (fuente: `assets/plugins`) |

Una carpeta de plugin:

```
Plugins/ptz-rally/
├── plugin.json   {"id", "nombre", "descripcion", "version", "principal": "plugin.py",
│                  "dependencias": ["pyusb", "libusb-package"], "config": {...}, "escena": "Sala"}
├── plugin.py     el código (import ba_plugin as ba … plugin.correr())
├── _libs/        las dependencias de pip (las baja la app; portables)
└── datos/        lo que el plugin quiera guardar
```

## La arquitectura

```
                      ┌──────────────── app (ba-app) ────────────────┐
  plugin.py  ◄─stdin/stdout (JSON por línea)─►  plugins::Plugins      │
  (su proceso)        │   entradas → VideoGrafos::crearEntradaPipeline │
     │                │   ptz      → ControlCamaras (backend «plugin») │
     │                │   eventos  → bus (otros plugins, plugin_eventos)│
     │                │   llamar   → McpCore::callTool (todo el sistema)│
     │                └───────────────────────────────────────────────┘
     │ puerto de medios (127.0.0.1) + memoria compartida
     ▼
  ba-sources (módulo de video, en el proceso de libobs):
     bapluginsrc  → origen de ba_gst_fuente (colchón y reloj del estudio)
     baplugin     → tarjeta `gst` del grafo de video, o en cualquier pipeline
     ba_plugin_audio → filtro de audio de OBS sobre cualquier fuente
```

**Fuera de proceso, siempre.** Un plugin no puede trabar ni tumbar la emisión:
- corre en su propio proceso;
- todo lo que viene de él llega por señales (sin esperas);
- todo pedido a él tiene timeout;
- cada cuadro que va a una tarjeta tiene un tope de espera (`espera-ms`, 25 de fábrica). Si no vuelve a tiempo, sale el último que volvió;
- si el plugin no está, la tarjeta deja pasar la imagen y la entrada espera sin bloquear.

Si el plugin se cae, la app lo vuelve a arrancar con espera creciente (1, 2, 4, 8, 16 s). Con 5 caídas en un minuto queda apagado y avisa en la barra del operador.

**El medio no pasa por el canal de control.** Los cuadros y el audio van por **memoria
compartida** (`shm_open`, la crea el plugin). Por un socket local van solo avisos de pocos
bytes. El protocolo está documentado en `src/plugins/sdk/ba_plugin/medios.py`. Una entrada que
declara un pipeline de GStreamer (`gst=`) ni siquiera pasa por Python: la corre la app, sin
copias.

**El canal de control** (stdin/stdout del plugin; `print` va a stderr, al registro):

| Del plugin | A la app |
|---|---|
| `hola` (puerto de medios), `entrada`, `tarjeta`, `ptz`, `control`, `escuchar`, `listo` | declaraciones |
| `evento` {canal, datos, hora_ns} | al bus |
| `llamar` {n, herramienta, args} | una herramienta MCP → `respuesta` |
| `estado`, `aviso` | lista de plugins / barra del operador |

| De la app | Al plugin |
|---|---|
| `pedido` {n, accion: ptz.mover…, control, info} | → `respuesta` |
| `evento` (de otro plugin al que escucha), `config`, `chau` | |

## El SDK

Está en `src/plugins/sdk/ba_plugin/__init__.py`, con el docstring completo. En resumen:

- `plugin.entrada(nombre, gst=…, gst_audio=…)` o `entrada(nombre, video=True, audio=True)` + `enviar_video(arr)` / `enviar_audio(muestras)`.
- `@plugin.tarjeta(nombre, medio="video"|"audio", parametros={…})`: `cuadro.array()` es (alto, ancho, 4) RGBA en memoria compartida y se cambia en el lugar.
- `@plugin.ptz(entrada)` sobre una clase con `mover/parar/home/ir_a_preset/guardar_preset/enfocar/auto_foco` y `capacidades`.
- `@plugin.control(nombre)`, `plugin.evento(canal, datos)`, `@plugin.escuchar("*/canal")`.
- `plugin.llamar(herramienta, args)`: cualquier herramienta MCP.
- `plugin.estado(texto)`, `plugin.aviso(texto)`.
- `@plugin.al_iniciar`, `@plugin.al_terminar`, `@plugin.cada(s)`, `@plugin.al_cambiar_config`.
- `ba_plugin.dispositivos`:
  - USB (`usb_listar`);
  - **UVC**: `UVC.abrir(vid, pid)` con PTZ estándar (`mover/parar/ir_a/rangos`) y las unidades de extensión de la marca (`unidades/xu_explorar/xu_leer/xu_escribir`). No le saca la imagen al sistema;
  - HID (`hid_listar/hid_abrir`);
  - serie (`serie_listar`).
- `ba_plugin.probar` corre el plugin **sin la app**. Le pasa cuadros y audio de prueba a sus tarjetas, escucha sus entradas, llama controles y devuelve un informe JSON. Es `plugin_probar`.

## Herramientas MCP (categoría `plugins`)

| Herramienta | Para qué |
|---|---|
| `dispositivos_listar` | Cámaras y micrófonos (con el `gst` del pipeline), USB, HID, serie |
| `plugin_listar`, `plugin_ver` | Los plugins y los ejemplos; leer código |
| `plugin_escribir`, `plugin_desde_ejemplo`, `plugin_borrar` | Crear/cambiar (borrar = a `Plugins/.papelera`) |
| `plugin_probar` | Probar sin tocar el aire (informe) |
| `plugin_activar`, `plugin_reiniciar`, `plugin_registro` | Correrlo y ver qué dice |
| `plugin_control`, `plugin_eventos` | Usarlo |
| `plugin_tarjeta` | Una tarjeta de video en una entrada, o de audio en una fuente |

También `video_pipeline` / `video_tarjeta_agregar` aceptan `{"tipo": "plugin", "plugin",
"tarjeta", "parametros"}`, que es una tarjeta `gst` con el módulo `baplugin`.

## Los casos que dieron origen a esto

- **PTZ raro por USB** (`ejemplo-ptz-uvc`): la imagen es una entrada `gst` (avfvideosrc) y el PTZ, `@plugin.ptz` con UVC estándar o las XU de la marca. La cámara queda como cualquier PTZ: botonera, posiciones con nombre, `ptz_*`.
- **Cámara 360** (`ejemplo-360`):
  - para partir una imagen en varias entradas, tarjetas `recorte` + `salida` en el grafo;
  - para desdoblar el ojo de pez, una tarjeta de plugin (numpy, 3 ms por cuadro a 1080p) que puede seguir la voz con los eventos `hablando` (`angulo`).
- **Varias cámaras que detectan quién habla** (`ejemplo-director`): cada equipo publica `hablando` y un plugin director decide (tiempo mínimo, silencio → general, modo proponer) y maneja el estudio con `plugin.llamar`.
- **Audio de un equipo**: `gst_audio` o `enviar_audio`. Para las tarjetas de audio sobre cualquier fuente, `plugin_tarjeta {"fuente"}`.

## Pruebas

- `test_video_grafo` (sección 11) arranca un plugin de Python de verdad, con el Python vendoreado y sin numpy, y verifica:
  - la tarjeta en el grafo invierte el cuadro;
  - la entrada de video llega con su tamaño y color;
  - el audio pasa por una tarjeta de audio (0,4 → 0,2);
  - con el plugin caído, la tarjeta deja pasar la imagen.
- Los ejemplos se prueban con `python3 -I src/plugins/sdk/ba_plugin/probar.py <carpeta>`.

## Pendiente

- **Windows**: memoria compartida con nombre (`CreateFileMapping`) y sockets con winsock en `ba_plugin_gst.cpp`. El SDK ya es portable.
- **Linux**: tendría que andar tal cual, pero no está probado.
- **Interfaz**: falta una página de plugins (hoy se manejan por el asistente y quedan visibles en Entradas y PTZ).
- **Director nativo**: el «quién habla» como entrada del compositor (hoy lo hace un plugin director).
