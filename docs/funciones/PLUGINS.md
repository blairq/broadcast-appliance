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
| El intérprete | `Resources/python` (CPython 3.12 de python-build-standalone + numpy, `make deps-python`; nada del sistema) |
| El SDK | `Resources/plugins-python/sdk` (fuente: `src/plugins/sdk`) |
| Los ejemplos | `Resources/plugins-python/ejemplos` (fuente: `assets/plugins`) |

Una carpeta de plugin:

```
Plugins/ptz-rally/
├── plugin.json   {"id", "nombre", "descripcion", "version", "principal": "plugin.py",
│                  "dependencias": ["pyusb", "libusb-package"], "config": {...}, "escena": "Sala",
│                  "secretos": [{"clave": "clave", "etiqueta": "Clave de la cámara"}]}
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
     baplugin     → tarjeta `gst` del grafo de video, o en cualquier pipeline;
                    en una entrada de GStreamer corre EN SU CADENA (capa de entrada):
                    video con las tarjetas del grafo, audio con entrada_filtros_audio
     ba_plugin_audio → filtro de audio de OBS sobre cualquier fuente (las que no son de GStreamer)
```

**Audio en la cadena de la entrada.** Una tarjeta de audio de un plugin va en la cadena de
GStreamer de la entrada con `entrada_filtros_audio`:
`{"entrada": "Cámara 1", "filtros": [{"elemento": "baplugin", "propiedades": {"plugin": "mi-plugin",
"tarjeta": "limpiar", "medio": "audio"}}]}`. `baplugin` declara su demora (consulta LATENCY: lo que
puede esperar la vuelta, `espera-ms`) y el colchón de la entrada mide la demora real de audio y
video y atrasa los dos lo mismo: el audio procesado no se adelanta ni se atrasa respecto de la imagen.

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
- `plugin.entrada(nombre, camara="")`: una cámara USB o capturadora del equipo (su id, o `""` para la primera), que la app abre como cualquier cámara y en cada sistema con lo suyo (Media Foundation, AVFoundation, V4L2). Mejor que un `gst` con `avfvideosrc`/`mfvideosrc`, que anda en un solo sistema. `camara_modo="1920x1080@30"` opcional.
- En la configuración (`config` de plugin.json), una clave `escena` (o `…_escena`) se elige de la lista de escenas y una `dispositivo` (o `…_dispositivo`) de las cámaras del equipo.
- `@plugin.tarjeta(nombre, medio="video"|"audio", parametros={…})`: `cuadro.array()` es (alto, ancho, 4) RGBA en memoria compartida y se cambia en el lugar.
- `@plugin.ptz(entrada)` sobre una clase con `mover/parar/home/ir_a_preset/guardar_preset/enfocar/auto_foco` y `capacidades`.
- `plugin.panel([...])`: **la interfaz del plugin para el operador**, descripta y dibujada por la app (página Plugins → «Usar»; el asistente la maneja con `plugin_panel`). Controles: `ba.seccion`, `ba.deslizador`, `ba.interruptor`, `ba.opcion`, `ba.texto`, `ba.numero`, `ba.boton` (lo que mueve el operador) y `ba.indicador`, `ba.medidor` (lo que muestra el plugin con `plugin.mostrar(clave, valor)`, a lo sumo 10 veces por segundo). Lo del operador queda en `plugin.valores` y llega a `@plugin.al_cambiar(clave)`; se guarda en `Plugins/<id>/.panel.json` y vuelve al reiniciar. No hay QML propio de un plugin a propósito: correría adentro de la app, y un plugin no puede colgar nada.
- `@plugin.control(nombre)`, `plugin.evento(canal, datos)`, `@plugin.escuchar("*/canal")`.
- `plugin.llamar(herramienta, args)`: cualquier herramienta MCP.
- `plugin.estado(texto)`, `plugin.aviso(texto)`.
- `@plugin.al_iniciar`, `@plugin.al_terminar`, `@plugin.cada(s)`, `@plugin.al_cambiar_config`.
- `ba_plugin.dispositivos`:
  - USB (`usb_listar`);
  - **UVC**: `UVC.abrir(vid, pid)` con PTZ estándar (`mover/parar/ir_a/rangos`) y las unidades de extensión de la marca (`unidades/xu_explorar/xu_leer/xu_escribir`). No le saca la imagen al sistema;
  - HID (`hid_listar/hid_abrir`);
  - serie (`serie_listar`).
- `ba_plugin.herramientas`: ffmpeg, ffprobe, yt-dlp y gst-launch **del programa** (`tools/deps/herramientas.sh`, en `herramientas/` del bundle y primeros en el PATH de cada plugin): `ruta(nombre)`, `correr(nombre, *args)`, `probar(archivo)` (ffprobe en JSON). No se bajan.
- `ba_plugin.gst`: **GStreamer adentro del plugin** (el programa trae PyGObject y gst-python para su Python, en los tres sistemas):
  - `@bagst.elemento("nombre", propiedades={...})` sobre `def f(a, props)`: un elemento de GStreamer escrito en Python (un `GstBase.BaseTransform` de verdad, registrado en el proceso del plugin); `a` es el cuadro RGBA en numpy, sin copiar.
  - `bagst.cadena("videobalance saturation=0 ! mielemento")`: pasa el cuadro de una tarjeta por una cadena de módulos (los del programa y los propios), una por tarjeta puesta (por hilo); `.procesar(cuadro)` deja el resultado en el lugar y `.poner("nombre", prop=v)` cambia un elemento en vivo.
  - Por qué adentro del plugin y no como elemento cargado en la app: el plugin corre en su proceso y la tarjeta `baplugin` tiene tope de espera; un elemento de Python adentro de la app podría trabar la emisión.
- `ba_plugin.probar` corre el plugin **sin la app**. Le pasa cuadros y audio de prueba a sus tarjetas, escucha sus entradas, llama controles y devuelve un informe JSON. Es `plugin_probar`.

## Procesos: posix_spawn, no QProcess

Los procesos de los plugins se lanzan con `plugins/Hijo` (`posix_spawn`), no con QProcess: el pip, el
probador, la lista de equipos y el zip también.

**Por qué.** QProcess hace `fork()` y corre código antes del `exec`. En una app con decenas de hilos, el
hijo heredaba candados tomados y quedaba dormido para siempre como una copia de ba-app. Pasaba varias
veces por sesión: un plugin que «no arrancaba en 20 s», `dispositivos_listar` o `plugin_probar` colgados.

**Qué garantiza `Hijo`:**
- el hijo no hereda descriptores (`POSIX_SPAWN_CLOEXEC_DEFAULT`);
- va en su propio grupo de procesos, así que matarlo mata también lo que lanzó;
- la salida y el final se revisan cada 200 ms, además del aviso del sistema.

## Claves y compartir

- **Claves**: el plugin las declara en `"secretos"` y las lee con `plugin.secreto("clave")` (None si
  todavía no están). Se guardan cifradas en `Credenciales/plugin-<id>-<clave>`. Se cargan desde la
  página Plugins (Configuración) o el formulario seguro de la barra (`plugin_credenciales`); nunca por el
  chat ni en `config`.
- **Exportar / importar**: un .zip con el código y plugin.json, sin `_libs` ni `datos`. Está en la página
  Plugins y en `plugin_exportar` / `plugin_importar`. Lo arma el `zipfile` del Python del programa.

## Qué puede llamar y cuándo arranca en vivo

- **Lista de herramientas** (`"herramientas": ["audio_set", "scene_switch"]` en plugin.json): con
  la lista, `plugin.llamar` solo puede usar esas; las demás contestan error. Sin la lista puede llamar
  todas (como el asistente). Cada llamada queda en el registro como `plugin:<id>`, y las acciones de
  aire esperan a una persona cuando se transmite o se graba (guarda de aire, T02), igual que el
  asistente.
- **Aprobado para el aire**: en la página Plugins, «Aprobar para el aire» fija el SHA-256 del código
  (todo menos `_libs`, `datos`, cachés y los valores del panel). Con el modo en vivo solo arrancan
  (y se reinician) los aprobados y sin cambios desde la aprobación; si el código cambió, el plugin
  queda «bloqueado en vivo» hasta que una persona lo vuelva a aprobar (y ve qué archivos cambiaron).
  El asistente no puede aprobar (`plugin_aprobados` solo lista y quita).

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
| `plugin_credenciales`, `plugin_exportar`, `plugin_importar` | Claves cifradas; pasar plugins entre aparatos |

También `video_pipeline` / `video_tarjeta_agregar` aceptan `{"tipo": "plugin", "plugin",
"tarjeta", "parametros"}`, que es una tarjeta `gst` con el módulo `baplugin`.

## Los ejemplos (moldes) y «Nuevo desde un ejemplo»

Los ejemplos (`assets/plugins`) llevan `"categoria"` en su plugin.json (`inicio`, `video`,
`automatizacion`, `equipo`, `superficie`): la página Plugins los ofrece en **«Nuevo desde un ejemplo»**
por categoría, con un id sugerido, y `plugin_listar` los da en ese mismo orden (una sola lista,
`Plugins::ejemplosDisponibles`). `categoria` no cuenta para el hash del código.

| Molde | Para qué |
|---|---|
| `ejemplo-video-gst` | Un efecto, filtro o análisis de video: tres tarjetas del grafo (`baplugin`): `vineta` (un elemento de GStreamer escrito en Python), `efecto` (cualquier cadena de módulos de GStreamer) y `medir` (brillo y movimiento sin tocar la imagen, con el evento `movimiento`) |
| `ejemplo-automatizacion` | «Cuando pasa X, hacé Y»: escucha `aire/escena` y `salida/stream`, lleva la minuta de lo que salió al aire, recuerda al operador cuánto lleva y le manda el resumen; `"herramientas": ["operator_message"]`; control `simular` para probarlo |
| `ejemplo-equipo-red` | Un aparato con protocolo de texto por TCP: conexión con reconexión, órdenes con respuesta, avisos del equipo como eventos y acciones (`al_boton`), panel, y un simulador del aparato adentro (`modo: simulador`) |
| `ejemplo-basico` | Todo lo que puede un plugin: tarjeta de video, filtro de audio, entrada propia, panel, control y evento |

El camino completo de una tarjeta de video: `plugin_desde_ejemplo` (o la página) → `plugin_probar` →
`plugin_activar` → `plugin_tarjeta {"id", "tarjeta", "entrada"}` (queda en el grafo como «Plugin ·
<tarjeta>», el módulo `baplugin`) → `video_tarjeta_ajustar`. `Plugins.tarjetasVideo` (propiedad para
QML) lista las tarjetas de video de los plugins que corren, para el catálogo del grafo.

## Los casos que dieron origen a esto

- **PTZ raro por USB** (`ejemplo-ptz-uvc`): la imagen es una entrada `camara` (la de «dispositivo» en la configuración; vacío = la primera) y el PTZ, `@plugin.ptz` con UVC estándar o las XU de la marca. La cámara queda como cualquier PTZ: botonera, posiciones con nombre, `ptz_*`.
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
- `test_plugins_ejemplos` (ctest, `src/plugins/sdk/tests/test_ejemplos.py`): los cuatro moldes con el Python de extern y su GStreamer (las tarjetas cambian el cuadro, la automatización avisa, el equipo contesta a su simulador).

## Pendiente

- **Windows**: memoria compartida con nombre (`CreateFileMapping`) y sockets con winsock en `ba_plugin_gst.cpp`. El SDK ya es portable.
- **Linux**: tendría que andar tal cual, pero no está probado.
- **Director nativo**: el «quién habla» como entrada del compositor (hoy lo hace un plugin director).
