# Informe de funciones — Broadcast Appliance

> Inventario de lo que el sistema hace **hoy**, armado leyendo el código del repositorio (rama `master`, commit `432d1b3`, 2026-10-04). Para cada área se indica qué hace, cómo la usa una persona desde la interfaz, qué herramientas MCP la cubren, los archivos principales y las limitaciones que se ven en el código.
>
> Criterio: manda el código, no la documentación. Cuando un doc de `docs/` dice otra cosa, se marca como **[doc desactualizado]**. Lo que es inferencia y no se verificó en el código se marca como **(suposición)**. No se compiló ni se corrió nada.

## Índice

1. [Resumen en números](#1-resumen-en-números)
2. [Arquitectura: procesos y qué corre dónde](#2-arquitectura-procesos-y-qué-corre-dónde)
3. [El MCP: cómo está armado](#3-el-mcp-cómo-está-armado)
4. [Funciones por área](#4-funciones-por-área)
   - 4.1 Escenas, preview/program y transiciones
   - 4.2 Salida: transmisión y codificación
   - 4.3 Grabación
   - 4.4 Multiview, dashboard, ventanas y monitores
   - 4.5 Sala de videoconferencia (invitados WebRTC)
   - 4.6 Modo oradores
   - 4.7 Audio: mezclador, racks LSP y medición
   - 4.8 Matriz de audio, buses y equipo físico
   - 4.9 Grafo de video (GStreamer)
   - 4.10 Sincronía y relojes
   - 4.11 Superresolución
   - 4.12 Entradas: cámaras locales, red, NDI
   - 4.13 Escáner de red de cámaras y equipos
   - 4.14 PTZ y seguimiento
   - 4.15 Personas y rostros
   - 4.16 Gráficos animados y rótulos
   - 4.17 Voz a texto y subtítulos
   - 4.18 Texto a voz
   - 4.19 Traducción en vivo
   - 4.20 Teleprompter
   - 4.21 Carruseles de láminas y medios
   - 4.22 Pantallas externas (Chromecast), tally web y QR
   - 4.23 Evento: guion, cues, ensayo e informe
   - 4.24 Padrón, acreditación, preguntas, votación y acta
   - 4.25 Plataformas: YouTube, Twitch, Facebook y chat en pantalla
   - 4.26 Asistente, agentes automáticos y modelo local
   - 4.27 Disparadores (alertas) y recetas
   - 4.28 Plugins de Python y extensiones QML
   - 4.29 Equipo, dispositivos y diagnóstico de hardware
   - 4.30 Registro (logs) y traza
   - 4.31 Configuración, portabilidad, supervisor y actualizaciones
   - 4.32 Seguridad y credenciales
5. [Asimetrías interfaz ↔ MCP](#5-asimetrías-interfaz--mcp)
6. [Documentación desactualizada detectada](#6-documentación-desactualizada-detectada)
7. [Estado por sistema operativo](#7-estado-por-sistema-operativo)
8. [Tabla completa de herramientas MCP](#8-tabla-completa-de-herramientas-mcp)

---

## 1. Resumen en números

| Qué | Cuánto |
|---|---|
| Herramientas MCP registradas en el código | **400** (394 definidas una por una + 6 generadas en un bucle: `audio_gate`, `audio_compressor`, `audio_deesser`, `audio_eq`, `audio_multiband`, `audio_limiter`) |
| Categorías MCP | **41** |
| Temas de ayuda (`help topic=…`) | **35** (`src/app/mcp/McpHelp.cpp`) |
| Prompts MCP | 5 (director, salir_al_aire, conducir_oradores, armar_escena, diagnostico) |
| Páginas de la interfaz (QML) | **25** (`src/app/qml/pages/`) en 4 grupos del riel |
| Tarjetas del dashboard | 12 (+ las de extensiones) |
| Procesos | launcher + `ba-app` (A) + `ba-gateway` (B) + hijos (`ba-grafico`, `ba-modelo`, traductor Python, plugins Python, agentes CLI, `cloudflared`) |

Algunas herramientas se registran solo si la pieza existe en el build: `ptz_rostros` y `ptz_seguir` (si hay `Seguidor`, o sea `BA_HAVE_ROSTROS`), toda la familia `plugin_*` (si hay `vm.plugins()`), las de voz (`BA_HAVE_VOZ`). El 400 es el máximo con todo compilado.

`docs/mcp.json` (generado por `tools/docs/mcp-inventario.py`) lista 333 y no tiene `plugins`, `video`, `equipos` ni `traduccion`; `docs/funciones/MCP.md` habla de "170 herramientas" **[doc desactualizado]**.

## 2. Arquitectura: procesos y qué corre dónde

```
BroadcastAppliance (launcher / supervisor, Proceso 0)
 ├── ba-gateway (Proceso B) — sala WebRTC, HTTP/WS, túnel, prompter, público, votación, pantallas
 │     └── cloudflared (opcional)
 └── ba-app (Proceso A) — libobs + Qt/QML: escenas, encoders, audio, video, MCP, asistente
       ├── ba-grafico     — renderizador de gráficos QML (caja de arena)
       ├── ba-modelo      — modelo local (Swift + MLX, solo macOS)
       ├── traductor.py   — Argos Translate / CTranslate2
       ├── plugins Python — un proceso por plugin (posix_spawn)
       └── agentes CLI    — Claude Code / opencode / Gemini CLI en terminal embebida o "de un tiro"
```

- **Launcher** (`src/launcher/`): instancia única, verificación de integridad SHA-256 contra `manifest.json`, lanza B y después A, watchdog por latido en memoria compartida (`BA.Health`, 5 s sin latir = colgado), reinicio con espera 0/1/2/5/10 s, *circuit breaker* (más de 5 caídas de A en 2 min → `--safe-mode`), diálogo de recuperación, actualizaciones (`Updater`), copia sombra (`ShadowCopy`, apagada por defecto), carpeta de datos (`Datos`).
- **ba-app (A)** (`src/app/`): el motor (`BroadcastEngine*.cpp`) sobre libobs, la interfaz Qt Quick (`src/app/qml/`, view models en `src/app/ui/`), el rack de audio con host CLAP (`src/app/audio/`), el grafo de video (`src/app/video/` + `src/obs-module/`), el servidor MCP (`src/app/mcp/`) y el asistente (`src/app/agent/`, `src/app/term/`).
- **ba-gateway (B)** (`src/gateway/`): sin Qt ni libobs. WebRTC (libwebrtc M123), señalización, HTTP/HTTPS, API de administración en `127.0.0.1:8788`, WireGuard en espacio de usuario (`src/gateway/net/`), túnel Cloudflare, y los servicios web para teléfonos y televisores: prompter, rótulos, público (preguntas/votación), pantallas, tally, tablero.
- **A ↔ B**: memoria compartida (`BA.Guest.n`, `BA.GuestShare.n`, `BA.Lobby.k`, `BA.Backstage.k`, `BA.Program`, `BA.Audio.*`) y HTTP local para el control.
- **Módulo de OBS propio** (`src/obs-module/`): `ba_video_grafo`, `ba_video_estudio` (tomas y compositor), `ba_gst_fuente` (entrada de red con colchón), `ba_plugin_gst`, `ba_ptz_digital`, `ba_mejora_imagen`, `ba_subtitulos`, `ba_dsp_sources` (channel strip nativo, sin uso real en la app).
- **Plugins de GStreamer propios** (`src/gst-plugins/superres/`): `basuperres`, `bavtsuperres` (Apple), `bafsr`, `baonnxsr`.
- **Núcleo** (`src/core/`, C++20 sin Qt ni libobs): IPC, DSP, persistencia, seguridad/credenciales, diagnóstico, QR, rostros (YuNet + SFace), cripto.
- **Web** (`assets/web/`): `guest/` (invitado), `publico/`, `prompter/`, `rotulos/`, `pantalla/` (mensaje, QR, reloj, tally, votación, carrusel), `cast/`, `board/` (tablero de oradores).

Todo lo de tiempo de ejecución viaja en el bundle (Qt, libobs, GStreamer, Python 3.12, ONNX Runtime, sherpa-onnx, ffmpeg, yt-dlp, LSP): es la regla de "portable innegociable".

## 3. El MCP: cómo está armado

- **Servidor** dentro de `ba-app`, en el mismo bucle que la interfaz (`McpCore`, `McpService`, `McpHttp`). JSON-RPC 2.0, protocolo 2025-06-18. HTTP en `127.0.0.1:8790` (`mcp.port`), rutas `/mcp` y `/healthz`, sin SSE (GET → 405). Puente stdio: `src/app/mcp/bridge/ba_mcp_main.cpp` (`ba-mcp`, con `--rol`).
- **Autenticación**: Bearer de 32 bytes, cifrado en `Credenciales/mcp-token.cred`; se rechaza un `Origin` que no sea localhost.
- **Contaminación por sesión**: si una sesión leyó contenido de invitados (chat de la sala, archivos compartidos, video de invitados, nombres), toda acción que modifica algo pasa a pedir aprobación del operador (`security_status` lo informa). Política `mcp.approval`: `invitados` (de fábrica), `siempre`, `nunca`. La aprobación es una tarjeta en la barra del operador que vence a los 10 min. Las herramientas "mediadas" (las decide una persona con un clic: `operator_ask`, `operator_message`, `guest_reply_suggest`, `account_credentials_request`, `account_open`) no piden aprobación. `settings_set` no puede tocar `mcp.*`.
- **Roles** (`X-BA-Rol`): filtran qué herramientas ve cada cliente (asistente, vision, organizador, recepcion, tecnico, vigilante, escenografo). El propio código aclara que **no es una frontera de seguridad**.
- **Búsqueda**: índice invertido con IDF (`McpIndice.*`) + tabla de sinónimos por herramienta y por familia (`McpEtiquetas.cpp`: "silenciar" → `audio_set`, "echar" → `participant_kick`). Un test (`test_mcp_busqueda`) falla si una herramienta no tiene etiquetas.
- **Traza**: `Logs/mcp.log` con todo lo que entra y sale, secretos tapados (`McpTraza.*`).
- **Ayuda**: 35 temas (primeros_pasos, conectar, escenas, fuentes, imagenes, audio, salidas, sala, evento, prompter, oradores, laminas, configuracion, interfaz, youtube, cuentas, audio_rack, twitch, facebook, equipo, entradas, video, equipos, plugins, camaras_red, camaras, herramientas, traduccion, superres, medios, operador, alertas, diagnostico, recetas, agentes), cada uno también como recurso `ba://ayuda/<tema>`.

### Herramientas por categoría

| Categoría (id) | Nombre | Herramientas | Archivo(s) |
|---|---|---|---|
| `audio_rack` | Rack de audio (LSP) | 33 | `McpAudioTools.cpp`, `McpMatrizTools.cpp` |
| `agentes` | Agentes automaticos | 26 | `McpAgenteTools.cpp`, `McpModeloTools.cpp` |
| `video` | Grafo de video | 24 | `McpVideoTools.cpp` |
| `sala` | Sala de videoconferencia | 21 | `McpRoomTools.cpp` |
| `plugins` | Plugins de Python | 17 | `McpPluginTools.cpp` |
| `camaras` | Cámaras PTZ | 16 | `McpPtzTools.cpp` |
| `youtube` | YouTube | 16 | `McpYouTubeTools.cpp` |
| `escenas` | Escenas | 14 | `McpService.cpp` |
| `facebook` | Facebook | 13 | `McpFacebookTools.cpp` |
| `evento` | Guion del evento | 12 | `McpEventoTools.cpp` |
| `entradas` | Entradas de video | 11 | `McpInputTools.cpp`, `McpRedTools.cpp` |
| `pantallas` | Pantallas externas | 11 | `McpPantallaTools.cpp` |
| `twitch` | Twitch | 11 | `McpTwitchTools.cpp` |
| `laminas` | Carruseles de laminas | 10 | `McpLaminaTools.cpp` |
| `rostros` | Personas y rostros | 10 | `McpRostroTools.cpp` |
| `traduccion` | Traducción en vivo | 10 | `McpTraduccionTools.cpp` |
| `interfaz` | Interfaz de la app | 10 | `McpUiTools.cpp`, `McpVentanaTools.cpp` |
| `graficos` | Gráficos animados | 9 | `McpGraficoTools.cpp` |
| `imagenes` | Imagenes y video (vision) | 9 | `McpImageTools.cpp` |
| `oradores` | Modo oradores | 9 | `McpRoomTools.cpp` |
| `preguntas` | Preguntas del publico | 8 | `McpPreguntaTools.cpp` |
| `fuentes` | Fuentes | 8 | `McpService.cpp` |
| `alertas` | Disparadores (alertas) | 7 | `McpAlertTools.cpp` |
| `padron` | La gente del evento | 7 | `McpEventoTools.cpp`, `McpRostroTools.cpp` |
| `audio` | Audio | 6 | `McpAudioTools.cpp`, `McpService.cpp` |
| `medios` | Medios (imagenes, videos, musica) | 6 | `McpMediaTools.cpp` |
| `prompter` | Teleprompter | 6 | `McpPrompterTools.cpp` |
| `cuentas` | Cuentas (YouTube, Twitch, Facebook) | 5 | `McpAccountTools.cpp` |
| `asistente` | Pestaña Asistente | 5 | `McpAssistantTools.cpp` |
| `operador` | Hablar con el operador | 5 | `McpOperatorTools.cpp` |
| `qr` | Codigos QR | 5 | `McpQrTools.cpp` |
| `registro` | Registro | 5 | `McpRegistroTools.cpp` |
| `salidas` | Salidas | 5 | `McpService.cpp` |
| `equipo` | El equipo | 5 | `McpSystemTools.cpp` |
| `sistema` | Sistema | 5 | `McpUiTools.cpp` |
| `ventanas` | Ventanas y monitores | 5 | `McpVentanaTools.cpp` |
| `equipos` | Equipos | 4 | `McpEquiposTools.cpp` |
| `recetas` | Recetas | 4 | `McpRecetaTools.cpp` |
| `ayuda` | Ayuda | 3 | `McpCore.cpp` |
| `config` | Configuracion | 3 | `McpService.cpp` |
| `estado` | Estado | 1 | `McpService.cpp` |
| **Total** | | **400** | |

---

## 4. Funciones por área

### 4.1 Escenas, preview/program y transiciones

**Qué hace.** Escenas de libobs con fuentes genéricas (`SourceSpec`), organizadas en carpetas, protegibles. Hay un bus de **preview** ("escena armada") y uno de **programa**. `take()` pasa la armada al aire con la transición global y deja armada la que salía, como una mesa de corte. `takeScene()` corta directo sin pasar por preview. Cada take guarda `app.json`. Las transiciones son cinco: `cut`, `fade`, `swipe`, `slide` y `fade_to_color`, con una sola transición global y una duración.

**Cómo se usa.**
- Tarjeta **Escenas** del dashboard: clic arma, doble clic corta directo. Con teclado, flechas para moverse, Enter para armar y **T** para TAKE.
- Tarjeta **Dirección**: AL AIRE y ARMADA en grande, más el reloj del guion. T o Enter hacen TAKE.
- **Escenas y fuentes** (`ScenesPage.qml`) es el editor: árbol de carpetas con arrastrar y soltar; copiar, proteger, renombrar y borrar; lienzo con capas (mover, tamaño, visibilidad, orden); panel de propiedades de cualquier fuente (`PropertiesModel`). El botón "Al aire" de esa página hace corte directo.

**MCP.** `scene_list`, `scene_create`, `scene_define`, `scene_copy`, `scene_delete`, `scene_rename`, `scene_protect`, `scene_folder`, `scene_move`, `scene_export`, `scene_preview`, `scene_take`, `scene_switch`, `transition_set`; fuentes: `source_list`, `source_add`, `source_remove`, `source_update`, `source_properties`, `source_set_property`, `source_types`, `guest_source_add`; estado general: `status`.

**Archivos.** `src/app/BroadcastEngine.cpp`, `src/app/BroadcastEngineScenes.cpp`, `src/app/ui/StudioViewModel.cpp` (~884-946), `src/app/ui/PropertiesModel.cpp`, `src/app/qml/pages/ScenesPage.qml`, `src/app/qml/cards/ScenesContent.qml`, `src/app/qml/cards/DireccionContent.qml`.

**Limitaciones.**
- No hay stingers, luma wipes, T-bar ni transición elegida para un take puntual.
- No hay *audio follow video* propio: micrófonos, invitados y música son globales a propósito.
- `PropertiesModel.cpp:190` deja pendientes las "listas editables" y el "frame rate".
- El comentario de `DireccionContent.qml` dice que es la única tarjeta donde una tecla saca algo al aire, pero `ScenesContent.qml` también lo hace con T.

### 4.2 Salida: transmisión y codificación

**Qué hace.** Un solo destino de transmisión: `rtmp_custom` + `rtmp_output` (`BroadcastEngine.cpp:852-901`). La URL va tal cual, así que RTMPS debería funcionar si el `rtmp_output` del bundle lo soporta (suposición). La lista de encoders se arma por plataforma:

| Plataforma | Orden de prueba |
|---|---|
| Windows | NVENC → QSV → AMF → x264 |
| macOS | VideoToolbox → x264 |
| Linux | NVENC → VAAPI → x264 |

Si un encoder no arranca, prueba el siguiente. El audio es `ffmpeg_aac`. Stream y grabación comparten encoder. Hay reconexión de libobs, con avisos en castellano: un corte no pedido queda como mensaje crítico fijo.

**Cómo se usa.**
- Tarjeta **Salidas** → "Salir al aire": diálogo con servidor y clave. Si existe `BA_STREAM_KEY`, arranca sin diálogo.
- Las páginas **YouTube / Twitch / Facebook** crean el vivo por API y transmiten.
- En **Configuración → Codificación / Transmisión**: bitrate, CBR/VBR/CQP, keyint y preset.

**MCP.** `stream_start`, `stream_stop`, `status`; por plataforma, `youtube_live_start`, `twitch_live_start` y `facebook_live_create`.

**Archivos.** `src/app/BroadcastEngine.cpp` (695-755 encoders, 852-901 stream), `src/app/AppSettings.*`, `src/app/ui/SettingsModel.cpp`, `src/app/qml/cards/OutputsContent.qml`, `src/app/qml/Main.qml:487-496`.

**Limitaciones.**
- Sin SRT/RIST/WHIP de salida, sin multidestino y sin salida NDI/SDI/cámara virtual (los plugins de salida de OBS se excluyen en `cmake/BaLibObs.cmake:137`).
- `startStreaming` devuelve `true` sin cambiar nada si ya hay una transmisión activa (`BroadcastEngine.cpp:857`). Pedir "transmití a YouTube" estando al aire en Twitch sigue mandando a Twitch, sin aviso.
- Solo fps enteros: `fpsDen` queda fijo en 1 en `AppSettings.cpp`, así que no se puede configurar 29,97 ni 59,94.
- La descripción de `stream_start` dice que la clave "NUNCA se guarda en disco", y lo mismo la ayuda de Configuración (`SettingsModel.cpp:62`). Pero `OperateMode.cpp:465` la guarda cifrada como `Credenciales/stream-last.cred` para poder retomar después de una caída. La documentación interna se contradice.

### 4.3 Grabación

**Qué hace.**
- **Programa:** `ffmpeg_muxer` a `<carpeta>/ba-<epoch>.<ext>` en mkv (de fábrica), mp4, mov o flv. Hay opción de "grabar al arrancar".
- **Grabación por entrada (ISO):** una `obs_view` + un encoder por fuente, archivo `entrada-<nombre>-<epoch>.mkv`, hasta 8 entradas. Solo las primeras 5 llevan audio propio.
- **Audio aparte:** la tarjeta «Salida» del rack graba WAV float32 en `Recordings/Audio/` y corta el archivo antes de los 4 GB (`GrabadorWav`).
- **Capítulos del evento** para la descripción del video (`evento_capitulos`).

**Cómo se usa.** Tarjeta Salidas (REC) y Configuración → Grabación. La ISO **no tiene interfaz**.

**MCP.** `record_start`, `record_stop`, `grabacion_individual`, `audio_output_set` (con `grabar`), `evento_capitulos`, `youtube_video_upload`, `facebook_video_upload`.

**Limitaciones.** La grabación usa el mismo encoder que la transmisión, así que no puede tener otra calidad. No hay replay ni buffer de repetición. Después de una caída de A la grabación arranca un archivo nuevo, y las ISO no se retoman.

### 4.4 Multiview, dashboard, ventanas y monitores

**Qué hace.**
- **Dashboard:** tarjetas de vidrio en una grilla de 24×16 con encaje magnético y video en vivo de fondo. Las 12 tarjetas son Dirección, Salidas, Escenas, Mezclador, Invitados, Sistema, Monitor, Fuentes, Sala/Lobby, Oradores, Carrusel y PTZ.
- **VideoWall:** multiview con disposiciones grid, focus, pip y row, y rótulos opcionales.
- **Ventanas sueltas:**
  - `OutputWindow`: videos en otro monitor, con pantalla completa.
  - `PageWindow`: clic derecho en el riel saca una página a su propia ventana.
  - `PanelWindow`: padrón, preguntas, urna, carrusel o PTZ en ventana propia.
- `capture_multiview` arma una imagen con todas las escenas y marca la del aire en rojo, pero es para el asistente, no un monitor en vivo.

**MCP.** `dashboard_list`, `dashboard_card`, `dashboard_background`, `output_window`, `ui_show_page`, `ui_page_window`, `ui_panel_window`, `ui_notify`, `ui_barra`, `ventana_listar`, `ventana_mover`, `ventanas_ordenar`, `ventana_cerrar`, `ventana_captura`, `monitor_identificar`, `monitors`, `feeds_list`, `capture_multiview`.

**Archivos.** `src/app/ui/DashboardModel.*`, `src/app/qml/pages/DashboardPage.qml`, `src/app/qml/cards/*`, `src/app/qml/controls/VideoWall.qml`, `src/app/qml/OutputWindow.qml`, `PageWindow.qml`, `PanelWindow.qml`, `src/app/mcp/McpVentanaTools.cpp`, `src/app/ui/RielConfig.cpp`.

**Limitaciones.** El multiview en vivo no muestra borde de tally, PGM/PVW por celda, medidores de audio ni reloj. En Wayland no se pueden ubicar ventanas. Ordenar el puesto por zonas solo se hace por MCP.

### 4.5 Sala de videoconferencia (invitados WebRTC)

**Qué hace.**
- **Gateway:** propio con libwebrtc M123, en estrella: B responde al offer de cada invitado.
- **Lobby:** el invitado ve al operador o una placa, y el operador puede hablarle en privado. Admitir cambia la ruta de medios sin cortar la PeerConnection.
- **Sala:** cada invitado tiene un slot `BA.Guest.n`, pantalla compartida y retorno del programa (video de 640×360 + mezcla de audio).
- **Acceso:** PIN con límite de intentos, tokens de un solo uso (300 s), sala cerrada.
- **Archivos compartidos:** tipo detectado por contenido, hasta 2 GB.
- **Chat:** general y privado.
- **Llegada a B:** túnel Cloudflare (HTTP/WS) o WireGuard propio en espacio de usuario con HTTPS en 8789.
- **Cupo:** 2 invitados de fábrica y hasta 16 (`kMaxGuests`). Lobby de 4 (máximo 8), fila de 12 (máximo 32).
- **Web del invitado** (`assets/web/guest/`): 720p30 con AEC, NS y AGC del navegador, mano levantada, reconexión con su lugar guardado 30 s, y una vista de orador con el prompter.

**Cómo se usa.** Página **Sala** (`ConferencePage.qml`):
- Lobby con cámara de cada uno: Admitir, Rechazar, "Admitir a todos", hablar en privado, editar datos. "Los atiende el asistente" y "Reconocer a los que llegan".
- En la sala: PRESENTADOR, ORADOR, cortar pantalla, bajar mano, mandar a la fila, sacar.
- Chat, PIN, lobby, sala cerrada, "Te ven con / Te oyen con", QR, WireGuard, archivos compartidos ("Tomar") y uso de CPU de la sala.
- Tarjetas Invitados, Sala/Lobby y Oradores en el dashboard.

**MCP.** `room_status`, `room_set`, `room_pin`, `room_link`, `network_status`, `operator_feed`, `gateway_restart`, `participant_admit`, `participant_admit_all`, `participant_reject`, `participant_kick`, `participant_talk`, `participant_request`, `participant_profile`, `chat_send`, `chat_history`, `chat_wait`, `share_list`, `share_view`, `share_take`, `share_delete`, `guest_chat_pending`, `guest_reply_suggest`, `invitado_chequeo`, `rostro_lobby`, `capture_people`, `capture_person`.

**Archivos.** `src/gateway/Room.*`, `Webrtc.*`, `SignalingServer.*`, `MediaPump.*`, `MediaRoute.hpp`, `TokenAuth.*`, `ShareStore.*`, `AdminApi.cpp`, `Cloudflared.*`, `src/gateway/net/*`, `src/app/ui/ConferenceViewModel.*`, `ConferenceClient.*`, `assets/web/guest/`, `docs/funciones/CONFERENCIA.md`, `docs/manual/guia-del-invitado.md`.

**Limitaciones.**
- **No hay mix-minus.** El retorno de audio sale de `obs_add_raw_audio_callback(0, …)` (`BroadcastEngine.cpp:~440`), que es la mezcla 0 completa, y se manda igual a todos. Cada invitado recibe su propia voz y queda a merced del AEC del navegador.
- No hay control de bitrate ni simulcast, y no hay estadísticas de red por invitado (RTT, pérdidas). `docs/planes/IDEAS.md` lo anota como pendiente.
- Un solo hilo de red de libwebrtc para todas las conexiones: es el techo esperado de escala.
- Quedan comentarios viejos que dicen "1..4 invitados".

### 4.6 Modo oradores

**Qué hace.**
- Fila de oradores con tiempo general y tiempo por persona.
- Al aire de a uno o varios, con presentador fijo.
- Carrusel manual, `chain` (sale uno y entra el siguiente) o por tiempo.
- El reloj nunca saca a nadie: cuenta en negativo.
- Tablero de solo lectura para tablet (`assets/web/board/`).
- `OnAirTimer` flotante: late en los últimos 10 s.

**Cómo se usa.** En la página Sala y en la tarjeta **Oradores** (`SpeakersContent.qml`, `SpeakerCard.qml`).

**MCP.** `speakers_status`, `speakers_config`, `speaker_queue`, `speaker_on_air`, `speaker_next`, `speaker_move`, `speaker_time`, `speaker_presenter`, `participant_requeue`.

**Limitaciones.** Los oradores presenciales que no están conectados a la sala no entran en la fila. Se manejan solo con cues del guion.

### 4.7 Audio: mezclador, racks LSP y medición

**Qué hace.**
- **Rack por fuente:** cada fuente con audio puede tener un rack. En las entradas de GStreamer (micrófonos, red) corre en su cadena de audio como el elemento `barack`; en lo que sigue en libobs, como el filtro OBS `ba_audio_rack`. Es un grafo tipo JACK con nodos `in`/`out`, plugins CLAP (en la práctica los ~200 LSP), analizador, generador (seno, rosa, blanco, barrido), salida a un dispositivo (escucha sin tocar el aire), entrada desde otra fuente, y tarjetas de voz a texto, texto a voz y traducir. Tiene sidechain.
- **Cambios sin cortes:** cada edición arma un plan nuevo que se intercambia sin bloquear el audio.
- **Nombres en castellano:** seis módulos con nombre propio y unidades reales (gate, compresor, de-esser, EQ de 8 bandas, multibanda, limitador), más un índice para buscar entre los ~200 plugins.
- **Plantillas de rack**, con una predeterminada por tipo de fuente: por ejemplo, la de los invitados.
- **Analizador BS.1770-4:** LUFS momentáneo, corto e integrado, pico de muestra, RMS, clips, DC, correlación, espectro por tercios, piso de ruido y rango dinámico.
- **Medición fuera de línea** de la cadena: respuesta en frecuencia y curva de transferencia.
- **Mezclador:** pico L/R del programa y de cada fuente, fader de -60 a +12 dB, mute, y atajos de teclado (flechas, M, Espacio).
- **PFL:** `audio_escuchar`, exclusivo como el solo de una consola.
- **Retardo de sincronía por fuente:** ±5 s.

**Cómo se usa.**
- Página **Audio → Racks**: patchbay donde se arrastran cables, menú "+" para agregar nodos, panel de parámetros, "Interfaz LSP" (la ventana original del plugin), presets, bypass, análisis, Plantillas y "Traducción…".
- Tarjeta **Mezclador**.

**MCP.** `audio_sources`, `audio_rack`, `audio_modules`, `audio_plugin_buscar`, `audio_module_add`, `audio_module_remove`, `audio_module_get`, `audio_module_set`, `audio_bypass`, `audio_patch`, `audio_ordenar`, `audio_plantillas`, `audio_analyze`, `audio_measure`, `audio_generator`, `audio_outputs`, `audio_output_set`, `audio_show`, `audio_editor`, `audio_lsp_restablecer_interfaz`, `audio_filtros_catalogo`, `audio_gate`, `audio_compressor`, `audio_deesser`, `audio_eq`, `audio_multiband`, `audio_limiter`, `mixer_list`, `audio_set`, `audio_escuchar`, `audio_sincro`.

**Archivos.** `src/app/audio/AudioRack.*`, `RackManager.*`, `ClapHost.*`, `ClapEditor.*`, `Modules.*`, `PluginTerminos.*`, `Analyzer.*`, `GrabadorWav.*`, `src/app/qml/pages/AudioPage.qml`, `src/app/qml/controls/Patchbay.qml`, `LevelMeter.qml`, `CatalogoFiltros.qml`, `src/app/qml/cards/MixerContent.qml`.

**Limitaciones.**
- No hay medidor de loudness **del programa**, ni true peak ni LRA. El LUFS se lee por nodo de rack.
- Los medidores del mezclador son solo de pico, sin balística VU o PPM.
- El mezclador no tiene botón de PFL: solo existe por MCP y por la columna «escucha» de la Matriz.
- El retardo de sincronía (`audio_sincro`) **no tiene interfaz**. La medición automática con aplauso (`src/app/camaras/SincroLabios.*`) existe, pero solo compila en un test.
- Los plugins CLAP corren **en el proceso y en el hilo de audio de OBS**, sin aislamiento: un crash de un plugin tira la app.
- No hay compensación de latencia de plugins (`ClapHost.cpp`: `latencyChanged` vacío).
- El `Automixer` (estilo Dugan) y el `ChannelStrip` del núcleo (`src/core/dsp/`, `src/obs-module/ba_dsp_sources.cpp`) **no se usan** en la app real: solo los usa `OperatorEngine` en `--headless-selftest`.

### 4.8 Matriz de audio, buses y equipo físico

**Qué hace.**
- **Filas:** cada fuente, a la salida de su rack.
- **Columnas:**
  - las 6 pistas de OBS (la 1 es el programa), nativas y sin retardo;
  - `escucha` (los auriculares del operador);
  - **salidas virtuales**: un bus a un dispositivo o cable virtual, con nivel, pan y mute por cruce y unos 20-30 ms de colchón;
  - **entradas virtuales**: submezclas que vuelven como fuente, con su propio rack;
  - las salidas físicas del equipo, una columna por par de canales.
- **Entradas físicas:** una fila por canal de cada interfaz, con detección de lo que se enchufa y desenchufa.

**Cómo se usa.** **Audio → Matriz** (`MatrizAudio.qml`): clic para prender o apagar un cruce, clic derecho para nivel, pan y mute. Los auriculares del operador se eligen en Configuración → Audio y voz.

**MCP.** `audio_matriz`, `audio_matriz_cruce`, `audio_bus_agregar`, `audio_bus_quitar`, `audio_bus_renombrar`, `audio_bus_dispositivo`, `audio_dispositivos`, `audio_matriz_equipo`, `audio_devices`, `audio_input_add`.

**Archivos.** `src/app/audio/Matriz.*`, `EquipoAudio.*`, `AnilloAudio.hpp`, `RingEstereo.hpp`, `src/app/qml/controls/MatrizAudio.qml`.

**Limitaciones.**
- libobs reproduce el monitoreo por **una sola** salida a la vez (`docs/planes/IDEAS.md`).
- La cantidad real de canales se consulta por CoreAudio solo en macOS.
- No hay mix-minus automático. Se puede armar a mano con buses, pero nada lo conecta al retorno de cada invitado.

### 4.9 Grafo de video (GStreamer)

**Qué hace.**
- **Un solo grafo** (`Config/video-grafo.json`) con un sub-grafo por entrada, armado con "tarjetas".
- **Tipos de tarjeta:**
  - Color, Croma, Recorte, Salida;
  - cualquier filtro de OBS;
  - cualquier elemento de GStreamer (catálogo de ~194 módulos documentados en castellano);
  - Toma, Traer, Retardo (0-120 cuadros), Rostros, Normalizar, Sincro, Superres.
- **Controladores:** varias tarjetas comparten parámetros (la misma corrección de color en varias cámaras).
- **Compositor de N entradas:** mosaico, PIP o lado a lado.
- **"Salidas con nombre"** que se ponen en escenas.
- **Entradas virtuales:** barras, negro, zonas, pelota, o un pipeline propio.
- **Costo de GStreamer:** la tarjeta baja la textura a RAM, pasa por `appsrc ! … ! appsink` y la vuelve a subir.

**Cómo se usa.** Página **Video** (`VideoPage.qml`, con `BarraGrafo.qml`): "+ Agregar", Grilla, Ordenar, "Usar en varias entradas…", "+ Entrada virtual…", y "Más" con "Sincronía y red…" y "Superresolución…". El catálogo está en `CatalogoModulos.qml`.

**MCP.** `video_grafo`, `video_filtros`, `video_modulos`, `video_entrada_virtual`, `video_tarjeta_agregar`, `video_tarjeta_quitar`, `video_tarjeta_ajustar`, `video_cable`, `video_controlador`, `video_salida_a_escena`, `video_grafo_limpiar`, `video_pipeline`, `video_mosaico`, `video_ordenar`, `video_estado`.

**Archivos.** `src/app/video/GrafoVideo.*`, `VideoGrafos.*`, `CatalogoGst.*`, `Diagnostico.*`, `src/obs-module/ba_video_grafo.cpp`, `ba_video_estudio.*`, `ba_gst.cpp`, `ba_plugin_gst.cpp`, `datos/gst-modulos.json`, `docs/funciones/VIDEO-GRAFO.md`.

**Limitaciones.**
- Todo depende de `BA_HAVE_GST`.
- No hay salidas de GStreamer a la red.
- No se ve el costo por cuadro de cada tarjeta.
- El ida y vuelta GPU→RAM→GPU en cada tarjeta `gst` cuesta.
- `docs/funciones/VIDEO-GRAFO.md` dice que el compositor "falta", pero está hecho **[doc desactualizado]**.

### 4.10 Sincronía y relojes

**Qué hace.**
- **Entradas de red sincronizadas** (`ba_gst_fuente`): SRT, RTMP, RTSP, UDP, HTTP o archivo. Cada cuadro sale a su hora de captura + latencia + desfase, con un colchón propio de hasta 300 cuadros.
- **RTSP:** usa `ntp-sync` y `buffer-mode=synced`. Si la hora de la cámara se corre más de 400 ms durante 3 s, se apaga solo.
- **Reloj del estudio:** sistema, NTP, PTP (IEEE 1588, necesita `gst-ptp-helper`) o el reloj de otro aparato (`GstNetClientClock`). También se puede **publicar** el reloj propio.
- **Medición de desfase entre entradas:** correlación de huellas de luma 16×9 cuadro a cuadro, con precisión de un cuadro. Se puede corregir sola.
- **Diagnóstico por entrada:** fps variables, entrelazado, HDR contra SDR, latencia y tarjetas con error.

**Cómo se usa.** Video → Más → **Sincronía y red…** (`PanelSincronia.qml`): reloj, colchón, publicar el reloj, desfases, "Medir y corregir" y "Entrada de red nueva".

**MCP.** `video_entrada_red`, `video_reloj`, `video_sincronia`, `video_desfase`, `video_estado`.

**Archivos.** `src/obs-module/ba_gst_fuente.cpp`, `src/app/video/Sincronia.*`, `Diagnostico.*`.

**Limitaciones.**
- No hay genlock.
- Las cámaras locales (USB, capturadoras) usan el reloj de libobs, no el del estudio.
- Ante un error o EOS, la entrada de red se rearma cada 2 s **sin placa de "sin señal"**. `ba_placeholder.cpp` es un esqueleto de compilación, no una placa.
- Con imagen quieta, la medición no funciona.

### 4.11 Superresolución

**Qué hace.** Agranda x2 con cuatro motores:
- **Apple** (`VTLowLatencySuperResolutionScaler`): solo macOS 26 y entradas de hasta 960×960. Unos 2-3 ms por cuadro.
- **FSR 1:** cualquier GPU con OpenGL.
- **ONNX:** CoreML en Mac, DirectML en Windows, CPU como respaldo.
- **auto:** elige entre los anteriores.

Hay dos lugares donde ponerla: como tarjeta del grafo en cualquier entrada, o dentro de la fuente de red, antes de `videorate`. Además hay un laboratorio en Python (`assets/superres/laboratorio.py`) que entrena una "red chica" (solo luma) grabando ~1 min de la propia cámara, y baja SPAN x2 o Real-ESRGAN x4.

**Cómo se usa.** Video → Más → **Superresolución…** (`PanelSuperres.qml`). La tarjeta `superres` tiene motor, modelo, nitidez y "Activa".

**MCP.** `superres_estado`, `superres_tarjeta`, `superres_aplicar`, `superres_modelo`, `superres_entrenar`.

**Archivos.** `src/gst-plugins/superres/*` (`plugin.cpp`, `vtsuperres_mac.mm`, `fsr.cpp`, `onnxsr.cpp`), `src/app/video/SuperRes.*`.

**Limitaciones.**
- Entrenar exige una entrada **de red** (graba de su URI) y ffmpeg.
- El laboratorio baja ~800 MB de PyTorch.
- Real-ESRGAN tarda ~290 ms por cuadro: no sirve para vivo.
- Corre un solo trabajo de entrenamiento a la vez.

### 4.12 Entradas: cámaras locales, red, NDI

**Qué hace.**
- **Cámaras USB y capturadoras:** `macos-avcapture`, `dshow_input` o `v4l2_input`, con formato, resolución y fps.
- **Red "a mano":** `ffmpeg_source` con reconexión a los 5 s. **No tiene colchón ni reloj del estudio**.
- **Red sincronizada:** `ba_gst_fuente` (ver 4.10).
- **NDI de entrada:** libNoIDea (alfa, solo SpeedHQ). No soporta NDI|HX.
- **Micrófonos e interfaces de audio.**

**Cómo se usa.** Página **Entradas** (`InputsPage.qml`): micrófonos, cámaras y capturadoras, NDI ("Buscar"), "Buscar cámaras en la red…" y URL a mano.

**MCP.** `video_devices`, `video_input_add`, `network_input_add`, `ndi_sources`, `ndi_input_add`, `input_status`, `entrada_borrar`, `audio_devices`, `audio_input_add`.

**Archivos.** `src/app/InputSpecs.*`, `src/app/ui/InputsViewModel.*`, `src/app/NdiSource.*`.

**Limitaciones.**
- No hay salida NDI, ni SDI/Decklink/AJA (de entrada ni de salida), ni ST 2110.
- NDI está apagado en macOS según `docs/historia/PLAN_MULTIPLATAFORMA.md`.

### 4.13 Escáner de red de cámaras y equipos

**Qué hace.**
- **Escaneo:** WS-Discovery ONVIF, barrido de la /24 por puertos de video (554, 8554, 80, 8000, 37777, 1935, 5960…), fabricante por la MAC (`datos/oui.tsv`) y marca por la web.
- **URLs RTSP:** prueba las típicas con DESCRIBE y Digest o Basic (`datos/camaras-urls.json`).
- **Ficha completa:** ONVIF + `GstDiscoverer`.
- **Plan de conexión:** el perfil más chico que cubra el lienzo, H.264/H.265 antes que MJPEG, y si hace falta normalizar.
- **Credenciales** cifradas por IP.
- **"Equipos":** registro genérico de detectores (red, local, USB/HID/serie vía Python) y acciones (ficha, ONVIF crudo, HTTP a la API de la marca, activar PTZ, explorar UVC).

**Cómo se usa.**
- En Entradas, **Buscar cámaras en la red…** (`BuscadorRed.qml`): equipos, clave, "Analizar", el plan, "Agregar" y "Configurar su PTZ".
- Página **Equipos** (`EquiposPage.qml`).

**MCP.** `red_escanear`, `camara_credenciales`, `camara_ficha`, `camara_agregar`, `camara_urls`, `equipos_escanear`, `equipos_listar`, `equipo_ver`, `equipo_accion`, `dispositivos_listar`.

**Archivos.** `src/app/red/Red.*`, `Biblioteca.*`, `Rtsp.*`, `Plan.*`, `src/app/dispositivos/*`.

**Limitaciones.** Barre solo la /24 local. El detector USB depende del Python del bundle.

### 4.14 PTZ y seguimiento

**Qué hace.**
- **Backends:** VISCA sobre IP (Sony UDP 52381, TCP 5678, UDP 1259), ONVIF, Panasonic AW (HTTP), plugin de Python y **PTZ digital** (recorte y zoom en la GPU, en cualquier entrada).
- **Presets 1-16** y posiciones con nombre: guardan la memoria de la cámara y el encuadre digital.
- **Apuntar con el mouse** sobre la imagen.
- **Seguir a una persona:** con el PTZ real (pulsos proporcionales) o con el recorte digital, buscando por identidad (libreta de Personas) o por cercanía.
- **"Mejorar imagen":** shader de ruido, color y nitidez.

**Cómo se usa.** Página **Cámaras PTZ** (`PtzPage.qml`): joystick óptico o digital, zoom, foco, velocidad, presets, Posiciones y backend. Tarjeta o ventana **PTZ** (`PtzContent.qml`): imagen para apuntar, "Analizar rostros", "¿A quién seguir?" y "Tracking".

**MCP.** `ptz_camaras`, `ptz_configurar`, `ptz_buscar`, `ptz_mover`, `ptz_parar`, `ptz_preset`, `ptz_encuadre`, `ptz_apuntar`, `ptz_posiciones`, `ptz_posicion_crear`, `ptz_posicion_editar`, `ptz_posicion_borrar`, `ptz_ir_posicion`, `ptz_rostros`, `ptz_seguir`, `video_mejora`.

**Archivos.** `src/app/ptz/*`, `src/obs-module/ba_ptz_digital.cpp`, `ba_mejora_imagen.cpp`, `src/app/ui/Seguidor.*`, `docs/funciones/PTZ.md`.

**Limitaciones** (según `docs/funciones/PTZ.md`).
- Probado solo contra cámaras falsas en 127.0.0.1.
- Faltan VISCA por puerto serie (RS-232/422), Pelco-D/P, Canon, JVC, Blackmagic y NDI PTZ.
- Apuntar con PTZ motorizado es aproximado, porque la cámara no informa su campo de visión.
- Falta validar en Windows y Linux.

### 4.15 Personas y rostros

**Qué hace.**
- **Modelos:** YuNet (detección) + SFace (vector de 128) en ONNX Runtime, en CPU.
- **Libreta de personas:** nombre, cargo y varias poses por persona.
- **Calidad de la cara**, con qué pedirle a la persona si no sirve.
- **Autoalta:** fichas sin nombre de quien pase por una cámara.
- **Fusión** de fichas duplicadas.
- **Cámara de la puerta** que acredita en el padrón.
- **Reconocimiento en el lobby.**
- **Nunca pone nada al aire por su cuenta:** solo identifica.

**Cómo se usa.** Página **Personas** (`PersonasPage.qml`). En la Sala, "Reconocer a los que llegan".

**MCP.** `rostro_ver`, `rostro_guardar`, `rostro_listar`, `rostro_borrar`, `rostro_calidad`, `rostro_sumar`, `rostro_pendientes`, `rostro_fusionar`, `rostro_autoalta`, `rostro_lobby`, `padron_puerta`, `ptz_rostros`.

**Archivos.** `src/core/rostros/*`, `src/app/ui/PersonasViewModel.*`, `Reconocedor.*`, `CalidadCara.*`.

**Limitaciones.**
- Depende de `BA_HAVE_ROSTROS` (`make deps-rostros`).
- La libreta **no alimenta** los rótulos automáticos.

### 4.16 Gráficos animados y rótulos

**Qué hace.**
- **Motor propio en QML:** `ba-grafico`, en un proceso aparte que hace de caja de arena.
- **Dos niveles:**
  - JSON declarativo, con capas y animaciones de un repertorio fijo;
  - QML escrito a mano.
- **Pre-render:** los cuadros se dibujan antes. Máximo 15 s de animación; el último cuadro queda congelado.
- **Plantillas incluidas:** `zocalo`, `placa-titulo`, `pregunta`, `proximo-orador` y `ticker` (`assets/graficos/`).
- **Rótulos vinculados:** un rótulo se escribe solo a partir de un origen: `{orador.nombre}`, `{orador.cargo}`, `{siguiente.*}`, `{invitadoN.*}`, `{guion.bloque}`, `{guion.cue}`, `{evento.nombre}`, `{escena}`. Se revisa cada 1 s.
- **Rotulador desde el celular** (`/rotulos/`): buzón con clave y tope de 16 encargos.

**Cómo se usa.**
- Página **Gráficos** (`GraficosPage.qml`): el IDE con vista previa.
- Página **Rótulos** (`RotulosPage.qml`): los textos de la escena al aire. Nada sale hasta apretar el botón.

**MCP.** `grafico_listar`, `grafico_repertorio`, `grafico_probar`, `grafico_mostrar`, `grafico_ocultar`, `grafico_en_escena`, `rotulo_vinculos`, `rotulo_vincular`, `rotulo_desvincular`.

**Archivos.** `src/grafico/*`, `src/app/graficos/*` (`GraficoProceso`, `Emisor`, `GraficoSource`, `Vinculos`), `src/app/ui/GraficoViewModel.*`, `RotuladorViewModel.*`, `src/gateway/Rotulador.*`, `docs/funciones/GRAFICOS.md`, `docs/funciones/ROTULOS.md`.

**Limitaciones.**
- No hay render en tiempo real a 30 fps (figura como pendiente), así que los datos vivos (reloj, marcador, contador de votos) tienen que re-renderizar.
- "Quien habla" es el participante `onair` en la sala o el del cue de orador. No usa detección de voz ni de rostro.
- No hay HTML/CasparCG. `obs-browser` está en el bundle, pero el sistema de gráficos no lo usa.
- `docs/funciones/ROTULOS.md` deja pendiente un cue que prenda o apague vínculos.

### 4.17 Voz a texto y subtítulos

**Qué hace.**
- **Motor:** sherpa-onnx con Parakeet-TDT-0.6B-v3 int8 (25 idiomas europeos) + Silero VAD, en CPU.
- **Dos modos:**
  - **Dictado:** push-to-talk F9, va al asistente. Mientras está apretado, el lobby recibe silencio.
  - **Continuo:** frase por frase, cortada por VAD, de 1 a 3 s por frase.
- **Cortes ajustables:** pausa, máximo y umbral.
- **Salidas de texto con nombre** (`FlujosTexto`). Alimentan disparadores, traducción, TTS, archivos `.txt` y `.srt` en `Recordings/Transcripciones/`, un rótulo, y el cuadro de escena **«Subtítulos»** (`ba_subtitulos`): open captions con líneas, tiempo por caracteres y borrado.

**Cómo se usa.** En el rack: "+ Voz a texto: dictado / transcripción continua". En Configuración: micrófono del operador.

**MCP.** `voz_texto`, `voz_transcripcion`, `voz_cortes`, `texto_flujos`, `texto_publicar`, `subtitulos_limpiar`, `voz_atender`, `voz_microfono`.

**Archivos.** `src/app/audio/Escucha.*`, `src/app/agent/Voz.*`, `FlujosTexto.*`, `Subtitulos.*`, `src/obs-module/ba_subtitulos.cpp`, `docs/funciones/AUDIO-TTS.md`, `docs/funciones/AUDIO-TARJETAS.md`.

**Limitaciones.**
- No es streaming: no hay resultados parciales.
- **No hay closed captions CEA-608/708** en el stream; solo subtítulos quemados en la imagen.
- No hay WebVTT.
- F9 no es configurable y solo funciona con la ventana en foco.
- `docs/funciones/EVENTO-COMPLETO.md` §7 dice que los subtítulos "faltan" **[doc desactualizado]**.

### 4.18 Texto a voz

**Qué hace.**
- **Voces:** piper (VITS) vía sherpa-onnx: es_AR Daniela, es_ES, en_US, en_GB, pt_BR, fr, it, de y ca.
- **Usos:**
  - **Voz del sistema:** contesta al operador solo por auriculares.
  - **Tarjeta «Texto a voz»:** lee una salida de texto y se puede rutear al aire.
  - **Voz del teleprompter:** sincronizada con la posición del prompter.

**Cómo se usa.** En el rack: "+ Texto a voz", con voz, velocidad y "Decir". En Configuración → Audio y voz. En la página Prompter.

**MCP.** `texto_a_voz`, `voces`, `voz_ajustes`, `voz_decir`, `voz_teleprompter`.

**Archivos.** `src/app/audio/Habla.*`, `MonitorVoz.*`, `src/app/agent/VocesRack.*`, `VozPrompter*.cpp`.

**Limitaciones.** `docs/funciones/AUDIO-TTS.md` dice que "contestar hablando falta" y que falta Windows, pero las dos cosas están hechas **[doc desactualizado]**.

### 4.19 Traducción en vivo

**Qué hace.**
- **Motor:** Argos Translate (OPUS-MT en CTranslate2), sin red, en un proceso Python aparte que se relanza si se cae. Después de 5 caídas seguidas queda en error.
- **Idiomas:**
  - directos: es↔en y es↔pt;
  - fr, de, ru y zh pasan por el inglés.
- **Glosario de 45 términos:** 29 del grupo `asamblea`, 8 de `localidades` y 8 generales. El operador suma términos y apaga grupos.
- **Armado de oraciones:** espera el punto, o 0,6 s, o 8 s / 280 caracteres.
- **Salida:** una nueva salida de texto («Orador · en»), que sirve para subtítulos, TTS y archivos.

**Cómo se usa.** Configuración → Traducción, o Audio → "Traducción…" (`PanelTraduccion.qml`). La tarjeta «Traducir» del rack usa `TarjetaTraduccion.qml` y `TiemposTraduccion.qml`.

**MCP.** `traducciones`, `traducir`, `traduccion_agregar`, `traduccion_ajustar`, `traduccion_bajar`, `glosario_ver`, `glosario_agregar`, `glosario_quitar`, `voz_cortes`, `subtitulos_limpiar`.

**Archivos.** `src/app/agent/Traductor.*`, `assets/traductor/traductor.py`, `datos/glosario-traduccion.json`.

**Limitaciones.**
- La latencia se acumula: VAD + armado + traducción (el doble con pivote) + TTS. Varios segundos.
- Procesa un pedido a la vez.
- El glosario todavía no lo revisó nadie de la asamblea (memoria del proyecto).

### 4.20 Teleprompter

**Qué hace.**
- **Servidor:** vive en el gateway. La posición va en palabras (140 ppm de fábrica) y cada vista calcula sola dónde va el texto.
- **Opciones:** espejo, tamaño, avisos al orador y "pánico" (para en seco).
- **Control:** desde `/prompter/#control` en un celular, con clave.
- **Conexiones:** se ve quién está conectado (atril y control).
- **Invitado remoto:** al aire, ve el texto en su propia página.
- **Discursos:** se guardan en `Discursos/` del evento.

**Cómo se usa.** Página **Prompter** (`PrompterPage.qml`).

**MCP.** `prompter_estado`, `prompter_documentos`, `prompter_leer`, `prompter_escribir`, `prompter_cargar`, `prompter_control`, `voz_teleprompter`.

**Archivos.** `src/gateway/Prompter.*`, `assets/web/prompter/`, `src/app/ui/PrompterViewModel.*`, `Discursos.*`, `docs/funciones/TELEPROMPTER.md`.

**Limitaciones.**
- No hay pedal, rueda de mano ni seguimiento por voz.
- La velocidad es una sola, sin marcas de ritmo por párrafo.

### 4.21 Carruseles de láminas y medios

**Qué hace.**
- **Láminas:** convierte PDF (QtPdf) o PPTX (LibreOffice, opcional, ~280 MB) en láminas. Salta al número N al instante, busca por etiquetas y las etiquetas las genera el rol `vision`.
- **Medios:** explorador de `Media/`, `Shares/` y `Recordings/` con filtro mientras se escribe. Borrar manda a `Papelera/`.
- **Herramientas incluidas:** ffmpeg, ffprobe, yt-dlp y gst-launch.

**Cómo se usa.** Tarjeta **Carrusel** (`CarruselContent.qml`, `PanelCarruseles.qml`), que no tiene página propia. Página **Medios** (`MediosPage.qml`).

**MCP.** `laminas_listar`, `laminas_ver`, `laminas_ir`, `laminas_buscar`, `laminas_desarmar`, `laminas_armar`, `laminas_editar`, `laminas_aplicar`, `laminas_sin_analizar`, `laminas_etiquetar`, `media_list`, `media_find`, `media_carpetas`, `media_view`, `media_use`, `media_ordenar`.

**Archivos.** `src/app/carrusel/*`, `src/app/ui/CarruselViewModel.*`, `src/app/medios/*`, `src/app/ui/MediosViewModel.*`, `docs/funciones/CARRUSEL.md`.

**Limitaciones.** No hay playlist de video con cola y cuenta regresiva (sin playout). Un PPTX necesita LibreOffice en el bundle.

### 4.22 Pantallas externas (Chromecast), tally web y QR

**Qué hace.**
- **Chromecast:** protocolo Cast propio (TLS 8009, receptora DashCast) y búsqueda por mDNS.
- **Contenidos:** páginas del gateway: mensaje, QR, reloj, carrusel, **tally**, votación, y el programa por WebRTC.
- **Pantalla fijada:** se reenvía cuando el aparato vuelve a aparecer.
- **"Superficies vivas":** se les cambia el contenido sin volver a castear.
- **Tally:** `publicarTally()` se llama en cada armado, take y cambio de aire o grabación. La página `/pantalla/tally/?fuente=…` se pone roja si esa fuente está al aire y verde si está armada.
- **QR:** generador SVG propio con estilos e íconos. Biblioteca del aparato o del evento. Exporta PNG o SVG.

**Cómo se usa.** Página **Pantallas** (`PantallasPage.qml`) y página **QR** (`QrPage.qml`).

**MCP.** `pantalla_listar`, `pantalla_buscar`, `pantalla_agregar`, `pantalla_mostrar`, `pantalla_fijar`, `pantalla_parar`, `pantalla_renombrar`, `carrusel_listar`, `carrusel_armar`, `superficie_listar`, `superficie_mostrar`, `qr_listar`, `qr_generar`, `qr_guardar`, `qr_borrar`, `qr_exportar`.

**Archivos.** `src/app/pantallas/Cast.*`, `Buscar.*`, `src/app/ui/PantallasViewModel.*`, `src/gateway/Superficies.*`, `Carruseles.*`, `src/core/qr/*`, `assets/web/pantalla/`, `assets/web/cast/`, `docs/funciones/PANTALLAS.md`, `docs/funciones/QR.md`.

**Limitaciones.**
- El primer casteo contra un televisor real **no se probó** (`docs/funciones/PANTALLAS.md`).
- El tally es solo web: no hay GPIO, TSL UMD ni luces de cámara.
- El cue `pantalla` del guion **no se ejecuta**.
- El único destino es Chromecast.

### 4.23 Evento: guion, cues, ensayo e informe

**Qué hace.**
- **Evento:** una carpeta `Eventos/<nombre>/` con `evento.json`, medios, discursos y respaldos.
- **Guion:** bloques y cues con duración prevista, manuales o automáticos.
- **Tipos de cue:** Escena, Música, Video, Teleprompter, Orador, Gráfico, Pantalla, Audio, Pausa, Espera y Nota.
- **Ensayo sin aire, ensayo acelerado y revisión previa:** busca cues incompletos y escenas que no existen.
- **Vista de conducción:** cue actual y siguiente, desvío, saltar, pausar y retroceder.
- **Modo `enVivo`:** la interfaz pide confirmación para lo peligroso, y el asistente no puede tocar lo que reinicia el gateway.
- **Informe** de lo previsto contra lo real, y **capítulos** de la grabación.

**Cómo se usa.** Página **Guion** (`EventoPage.qml`). Tarjeta Dirección.

**MCP.** `evento_listar`, `evento_abrir`, `evento_armar`, `evento_ver`, `evento_bloque`, `evento_cue`, `evento_guardar`, `evento_revisar`, `evento_ensayo_rapido`, `evento_correr`, `evento_informe`, `evento_capitulos`.

**Archivos.** `src/app/evento/Evento.*`, `EventoStore.*`, `src/app/ui/EventoViewModel.cpp` (~1108-1360 ejecución), `docs/funciones/EVENTOS.md`, `docs/funciones/EVENTO-COMPLETO.md`.

**Limitaciones.**
- El cue **Pantalla** cae en "Todavía no sé ejecutar…".
- El ensayo acelerado informa como "sin ejecución" a Música, Teleprompter y Audio, que sí se ejecutan.
- La revisión no verifica archivos, dispositivos ni disco.
- No hay timecode ni hora de inicio sincronizada con una fuente externa.

### 4.24 Padrón, acreditación, preguntas, votación y acta

**Qué hace.**
- **Padrón:** papel, canal, estado, contacto, si vota, y un código de votante.
- **Acreditación:** por cámara de la puerta, QR personal de la invitación o mostrador.
- **Preguntas del público:** página web `/publico/` con un recorrido nueva → elegida → al aire → respondida o descartada.
- **Votación:**
  - encuesta: una vez por teléfono;
  - **formal**: con código del padrón, solo presentes con voto, y puede ser secreta.
  - Hasta 10 opciones y una urna por vez.
- **Acta:** `acta-<fecha>.md` con el quórum (presentes con voto sobre el total con voto) y todas las votaciones.

**Cómo se usa.** Página **Público** (`PublicoPage.qml`), con `PanelPadron.qml`, `PanelPreguntas.qml` y `PanelVotacion.qml` (se pueden sacar a ventanas propias). Pantalla de votación para proyectar.

**MCP.** `padron_ver`, `padron_anotar`, `padron_cambiar`, `padron_acreditar`, `padron_quitar`, `padron_puerta`, `invitacion`, `preguntas_ver`, `pregunta_moderar`, `pregunta_sumar`, `publico_pagina`, `votacion_abrir`, `votacion_ver`, `evento_acta`.

**Archivos.** `src/app/evento/Padron.*`, `src/gateway/Preguntas.*`, `Votacion.*`, `src/app/ui/PublicoViewModel.*`, `assets/web/publico/`.

**Limitaciones.**
- El quórum es un conteo: no hay una regla de mayoría codificada (mitad más uno, dos tercios).
- Las preguntas de YouTube, Twitch y Facebook no entran solas.
- Poner una pregunta "al aire" no la dibuja: hace falta un gráfico.
- La transcripción no se integra al acta.
- El voto se cuenta por navegador en la encuesta.

### 4.25 Plataformas: YouTube, Twitch, Facebook y chat en pantalla

**Qué hace.**
- **YouTube:** OAuth con PKCE. Crea, inicia y termina vivos, consulta la salud de la ingesta, maneja videos (subir, editar, borrar) y lee y escribe en el chat.
- **Twitch:** Device Code. Canal, vivo, VODs y chat IRC.
- **Facebook:** solo Páginas. Vivos de prueba o públicos, videos y comentarios.
- **Fuente «Chat en vivo»:** junta las tres plataformas y la sala, sobre fondo transparente.
- **Guías paso a paso** para crear las credenciales.

**Cómo se usa.** Páginas **YouTube**, **Twitch** y **Facebook**. La credencial se carga con un formulario seguro en la barra del operador.

**MCP.**
- YouTube: 15 `youtube_*` + `chat_overlay_add`.
- Twitch: 11 `twitch_*`.
- Facebook: 13 `facebook_*`.
- Cuentas: `accounts_status`, `account_setup_guide`, `account_open`, `account_credentials_request`, `account_check`.

**Archivos.** `src/app/youtube/*`, `twitch/*`, `facebook/*`, `src/app/AccountGuides.*`, `src/app/net/HttpsClient.*`, `src/app/ChatOverlaySource.*`, `src/app/ui/ChatOverlayRenderer.*`.

**Limitaciones.**
- Un solo destino a la vez (ver 4.2).
- El chat en pantalla no tiene moderación ni mensaje destacado.

### 4.26 Asistente, agentes automáticos y modelo local

**Qué hace.**
- **Asistente interactivo:** una terminal embebida (libvterm + PTY/ConPTY) corre Claude Code, Gemini CLI u opencode (OpenRouter, opencode-go, o un servidor propio compatible con OpenAI). Las versiones están fijadas por SHA-256 en `agentes.json`. Se conecta al MCP con el rol `asistente`.
- **Roles automáticos "de un tiro"** (`Roles.cpp`, `Despachador.*`):

  | Rol | Estado de fábrica |
  |---|---|
  | vision | prendido |
  | organizador | prendido |
  | escenografo | apagado |
  | recepcion | apagado |
  | tecnico | apagado |
  | vigilante | apagado |

  Atienden eventos de los disparadores. Corren una a la vez por rol, con tope de 20 min, y guardan las últimas 200 corridas.
- **Modelo local:** un clasificador que elige la herramienta para una frase de cabina o la deriva al asistente grande, con sesgo ajustable. Completa los argumentos y los valida contra el estado real. En Mac corre en `ba-modelo` (Swift + MLX, con visión).
- **Voz del operador:** F9 + modelo local. Las herramientas marcadas "automáticas" se ejecutan sin confirmar; el resto queda como propuesta en la barra.
- **Barra del operador:** mensajes por prioridad, preguntas con botones, respuestas sugeridas a invitados, formularios de credenciales y aprobaciones.

**Cómo se usa.**
- Página **Asistente** (`AssistantPage.qml`): terminal, proveedor y modelo, instalación y disparadores.
- Página **Agentes** (`AgentesPage.qml`): roles, motor, corridas paso a paso y encargo manual.
- `BarraAsistente.qml`: zócalo global para escribirle al asistente.
- Configuración → Modelos locales / Audio y voz.

**MCP.**
- Asistente: `assistant_status`, `assistant_config`, `assistant_install`, `assistant_run`, `assistant_send`.
- Agentes: `agente_roles`, `agente_configurar`, `agente_encargar`, `agente_corridas`, `agente_corrida`, `agente_detener`.
- Modelo local: `modelo_local_*` (8).
- Voz: `voz_*` y `texto_*`.
- Operador: `operator_message`, `operator_ask`, `operator_messages`.
- Visión: `imagen_analizar`, `capture_*` (program, source, feed, sequence, ui, people, person, multiview).

**Archivos.** `src/app/agent/*`, `src/app/term/*`, `src/modelo-mac/`, `src/app/ui/OperatorMessages.*`, `docs/funciones/AGENTES.md`, `docs/modelos/modelo-local.md`, `docs/modelos/voz-modelo-local.md`.

**Limitaciones.**
- El modelo local en Linux y Windows (llama.cpp) "todavía no viaja en el bundle" (`ModelosLocales.cpp:249`).
- Los roles automáticos no pueden usar Gemini.
- `docs/funciones/MCP.md` dice que al agente se le quita la consola, pero el interactivo tiene Bash **[doc desactualizado]**.
- `agente_configurar` puede prender `consola` en un rol sin bloqueo especial.
- `docs/funciones/AGENTES.md` nombra solo dos roles.

### 4.27 Disparadores (alertas) y recetas

**Qué hace.**
- **Disparadores** (`AlertEngine`): se evalúan cada 250 ms.
- **Tipos:**
  - audio: `audio_peak`, `audio_silence` (de fábrica -55 dB durante 15 s);
  - sala y oradores: `speaker_time`, `lobby`, `hand`, `guest_drop`, `guest_message`;
  - medios y texto: `media_end`, `chat` (texto o regex por plataforma), `texto` (frases de una «Voz a texto»);
  - sistema: `stream_health` (cuadros perdidos y corte), `disk` (solo grabando), `timer`.
- **Qué hace cada disparo:** avisa en el chat del asistente o en la barra, lo atiende un rol automático, o ejecuta `actions` (llamadas MCP).
- **Recetas:** listas de llamadas MCP con nombre, sin condiciones, que se cortan al primer fallo.

**Cómo se usa.** Página Asistente → panel **Disparadores**: prender, apagar y borrar. **Crear un disparador o una receta no tiene interfaz.**

**MCP.** `alert_create`, `alert_update`, `alert_delete`, `alert_list`, `alert_types`, `alert_events`, `alert_wait`, `receta_guardar`, `receta_listar`, `receta_correr`, `receta_borrar`.

**Archivos.** `src/app/agent/AlertEngine.*`, `src/app/mcp/McpAlertTools.cpp`, `McpRecetaTools.cpp`.

**Limitaciones.** Las acciones de un disparador corren sin rol ni sesión: ven todas las herramientas.

### 4.28 Plugins de Python y extensiones QML

**Qué hace.**
- **Aislamiento:** cada plugin corre en su propio proceso (`posix_spawn`), con Python 3.12 del bundle y dependencias por pip en `_libs` dentro de la carpeta portable.
- **Datos:** control por JSON en stdin/stdout. Cuadros y audio por memoria compartida, con espera máxima por cuadro (25 ms): si no vuelve a tiempo, sale el último.
- **Caídas:** reinicio con espera creciente. Con 5 caídas en un minuto queda apagado.
- **Qué puede declarar:** entradas (con un pipeline de GStreamer), tarjetas de video y de audio, PTZ, controles, eventos, un panel declarativo, secretos cifrados, "disparador", y `plugin.llamar` para cualquier herramienta MCP.
- **Ejemplos incluidos:** básico, PTZ UVC, 360 y director.
- **Extensiones QML:** tarjetas del dashboard desde `Extensiones/<id>/`, **sin sandbox**. Si cuelgan el arranque, se apagan solas.

**Cómo se usa.** Página **Plugins** (`PluginsPage.qml`, `PanelPlugin.qml`): prender, apagar, probar, registro, código, configuración, controles, exportar e importar zip.

**MCP.** `plugin_listar`, `plugin_ver`, `plugin_desde_ejemplo`, `plugin_escribir`, `plugin_probar`, `plugin_activar`, `plugin_reiniciar`, `plugin_borrar`, `plugin_registro`, `plugin_control`, `plugin_panel`, `plugin_tarjeta`, `plugin_eventos`, `plugin_credenciales`, `plugin_exportar`, `plugin_importar`, `dispositivos_listar`, `ext_listar`, `ext_prender`.

**Archivos.** `src/app/plugins/Plugins.*`, `Hijo.*`, `src/plugins/sdk/ba_plugin/*`, `assets/plugins/*`, `src/app/ui/ExtensionesModel.*`, `docs/funciones/PLUGINS.md`, `docs/funciones/EXTENSIONES.md`.

**Limitaciones.**
- En Windows `Hijo::iniciar` devuelve "los plugins todavía no andan en Windows" (`Hijo.cpp:52`).
- En Linux no está probado.
- Los ejemplos de PTZ no se probaron con hardware real (memoria del proyecto).

### 4.29 Equipo, dispositivos y diagnóstico de hardware

**Qué hace.** Inventario de monitores, placas de audio, cámaras, GPU, disco, memoria, CPU y temperatura.

**Cómo se usa.** Tarjeta **Sistema** del dashboard (CPU, fps, encoder, cuadros perdidos). Página Equipos.

**MCP.** `system_hardware`, `monitors`, `audio_devices`, `video_devices`, `status`, `system_info`.

**Archivos.** `src/app/mcp/McpSystemTools.cpp`, `src/app/ui/SystemViewModel.*`, `src/core/hw/`.

### 4.30 Registro (logs) y traza

**Qué hace.**
- **Registro:** seis niveles, componente y código, con archivos rotativos en `Logs/` y un anillo en memoria.
- **Hijos:** el launcher captura su stdout y stderr.
- **Página Registro:** mezcla los tres procesos ordenados por hora, filtra, sigue la cola, deja poner marcas y exportar.
- **Traza MCP aparte:** `Logs/mcp.log`.

**MCP.** `log_leer`, `log_nivel`, `log_marcar`, `logs_tail`.

**Archivos.** `src/core/diag/Registro.*`, `src/app/ui/RegistroViewModel.*`, `src/app/qml/pages/RegistroPage.qml`, `docs/funciones/REGISTRO.md`.

**Limitaciones.**
- `DiagExport::writeMinidump` es un stub.
- Los volcados de memoria solo funcionan en Windows ("Crashpad: fase 9").
- El manejador de caídas no escribe la pila en Windows.

### 4.31 Configuración, portabilidad, supervisor y actualizaciones

**Qué hace.**
- **Ajustes:** todos tienen alcance `live` (en el momento), `gateway` (reinicia la sala) o `restart` (al próximo arranque), y viven en archivos JSON de `Config/` con escritura atómica.
- **Carpeta portable:** en macOS va al lado de la .app («BroadcastAppliance Datos»).
- **Actualización:** desde una carpeta descomprimida, con respaldo de Config y Credenciales (se guardan 5) y vuelta atrás. No hay actualización por red.
- **Si se cae A en medio del aire:** el supervisor lo relanza con `--restore`, A lee `Runtime/al-aire.json` y retoma la transmisión y la grabación (en un archivo nuevo).

**Cómo se usa.** Página **Configuración** (`SettingsPage.qml`), con las secciones Video, Codificación, Grabación, Transmisión, Invitados, Escena, Gateway, Sala, Acceso directo, Sistema y Asistente. Ahí están también "Actualizar desde una carpeta…" y los respaldos.

**MCP.** `settings_list`, `settings_set`, `app_save`, `system_info`, `system_update`, `app_quit`.

**Archivos.** `src/launcher/*`, `src/app/AppSettings.*`, `src/app/ui/SettingsModel.*`, `src/app/OperateMode.cpp` (482-512 retoma), `src/app/ui/SystemViewModel.*`.

**Limitaciones.**
- **El latido se emite desde el hilo de la interfaz** (`AppRuntime::tick`). Si la interfaz se traba más de 5 s, el supervisor mata a A y **corta la emisión**, aunque libobs siga codificando bien.
- El diálogo de recuperación en Linux es solo una línea en el log; en macOS se responde solo "Reintentar" a los 5 min.
- `src/core/persist` (SnapshotStore con doble generación) no se usa en la app real.
- Hay que evitar dos sistemas en la misma máquina: el canal de control es único por usuario (`docs/planes/IDEAS.md`).

### 4.32 Seguridad y credenciales

**Qué hace.**
- **CredentialStore:** un archivo `<nombre>.cred` por credencial, cifrado con AES-256-GCM. La clave es `Credenciales/clave-equipo`: 32 bytes al azar **sin contraseña**.
- **Qué se guarda ahí:** la última clave de stream, tokens y apps de las plataformas, claves de cámaras y de plugins, el token MCP, WireGuard y el certificado.
- **Formularios seguros:** van directo de la interfaz al almacén y el asistente no los ve.
- **Sala:** PIN con bloqueo por intentos, tokens HMAC y una API de administración solo en localhost con token y defensa CSRF.

**MCP.** `security_status`, `system_info` (solo nombres de credenciales), `camara_credenciales`, `plugin_credenciales`, `account_credentials_request`.

**Archivos.** `src/core/security/CredentialStore.*`, `TokenService.*`, `SecretsStore.*`, `src/app/net/Credentials.hpp`.

**Limitaciones.**
- Quien se lleve el pendrive entero tiene las claves: no hay Keychain, DPAPI ni passphrase.
- `SecretsStore` (con Argon2id) existe pero no se usa para guardar nada.
- Cualquier proceso local del mismo usuario puede leer el token MCP y entrar con acceso total.

---

## 5. Asimetrías interfaz ↔ MCP

**Tienen MCP pero no interfaz (o la interfaz es parcial):**

| Función | MCP | Interfaz |
|---|---|---|
| Grabación por entrada (ISO) | `grabacion_individual` | ninguna |
| Retardo de sincronía audio/video por fuente | `audio_sincro` | ninguna |
| PFL desde el mezclador | `audio_escuchar` | solo por la columna «escucha» de la Matriz |
| Crear disparadores | `alert_create`, `alert_update` | solo prender, apagar y borrar |
| Recetas | `receta_*` | ninguna |
| Ordenar ventanas por zonas | `ventana_mover`, `ventanas_ordenar` | solo la barra de cada ventana |
| Mirar con IA, analizar láminas | `imagen_analizar`, `laminas_sin_analizar`, `laminas_etiquetar` | se dispara a través del asistente |
| Medir y calibrar la cadena de audio | `audio_measure` | parcial (análisis en el panel) |
| Herramientas de medios (ffmpeg, yt-dlp) | tema de ayuda `herramientas` (las usa el asistente por consola) | ninguna |
| Traza completa de MCP | `log_leer` | parcial (página Registro) |

**Tienen interfaz pero no MCP (o está bloqueado a propósito):**

| Función | Interfaz | MCP |
|---|---|---|
| Política de aprobación, puerto y activación del MCP | Configuración → Asistente (MCP) | bloqueado (`settings_set` rechaza `mcp.*`) |
| Editar gráficos en el IDE (vista previa con barra de tiempo) | Gráficos | solo listar, probar y mostrar; no se escribe un gráfico nuevo |
| Arrastrar cables en los patchbays | Audio y Video | equivalente: `audio_patch`, `video_cable` |
| Joystick PTZ continuo | Cámaras PTZ | `ptz_mover` por pulsos con duración |
| Consola web del gateway, configurar WireGuard | Sala | solo `network_status` (lectura) |
| Formularios de credenciales | barra del operador | MCP solo los *pide*, nunca ve el secreto (a propósito) |
| Atajos de teclado (T/Enter/M/flechas) | tarjetas | no aplica |

---

## 6. Documentación desactualizada detectada

| Doc | Qué dice | Qué hace el código |
|---|---|---|
| `docs/funciones/MCP.md` | 170 herramientas, dos roles, el agente no tiene consola | 400 herramientas, 6 roles automáticos + asistente; el interactivo tiene Bash |
| `docs/mcp.json` | 333 herramientas, sin plugins, video, equipos ni traducción | faltan esas cuatro categorías |
| `docs/funciones/AGENTES.md` | dos roles | seis |
| `docs/funciones/VIDEO-GRAFO.md` | el compositor de N entradas falta; el PTZ no encuadra caras | las dos cosas están hechas |
| `docs/funciones/AUDIO-TTS.md` | contestar hablando falta; falta Windows | Habla y MonitorVoz existen; hay build para Windows |
| `docs/funciones/EVENTO-COMPLETO.md` §7 | subtítulos: falta | existen (`ba_subtitulos`, .srt) |
| `docs/funciones/EVENTOS.md` §3 | falta la vista del orador remoto | existe en parte en la web del invitado |
| `docs/desarrollo/MACOS.md` | Developer ID y notarización; "todavía no es un .app" | firma ad-hoc, ya hay .app |
| `src/app/video/SuperRes.hpp` | cita archivos que ya no existen | los motores están en `src/gst-plugins/superres/` |
| Descripción de `stream_start` y ayuda de Configuración | la clave nunca se guarda en disco | se guarda cifrada en `stream-last.cred` |
| `docs/index.rst` y `docs/modules/*.rst` | Windows como plataforma principal, `SecretsStore` como bóveda | macOS primero; `CredentialStore` sin passphrase |

## 7. Estado por sistema operativo

| | macOS (Apple Silicon) | Linux | Windows |
|---|---|---|---|
| Compila / bundle | sí, .app ad-hoc, datos al lado | sí | sí (cross MinGW / clang-cl, probado con Wine) |
| GStreamer | 1.28.7 arm64 | compilado de fuente | sí |
| Superres Apple | solo macOS 26 | no | no |
| Superres ONNX | CoreML | CPU | DirectML |
| Modelo local | MLX (`ba-modelo`) | no (llama.cpp pendiente) | no |
| Plugins Python | sí | sin probar | no |
| NDI | apagado | sí (libNoIDea, alfa) | alfa (suposición: no verificado) |
| Editor gráfico LSP | NSView | X11/XWayland | HWND |
| Volcados de caída | no | no | sí |
| Diálogo de recuperación | osascript | solo log | Win32 |
| Ubicar ventanas | sí | no bajo Wayland | sí |

Fuentes: `docs/historia/PLAN_MULTIPLATAFORMA.md`, `docs/desarrollo/MACOS.md`, `#ifdef` del código y memoria del proyecto. Cámara, micrófono, invitados y RTMP en macOS no se probaron a mano según `docs/desarrollo/MACOS.md`.

---

## 8. Tabla completa de herramientas MCP

Orden por categoría y nombre. "Para qué sirve" es el título con que cada herramienta se registra en el código. "Lee" = `readOnly`; "modifica" = puede pedir aprobación según la política.

| Herramienta | Categoría | Para qué sirve | Lee/Modifica |
|---|---|---|---|
| `agente_configurar` | agentes | Prender, apagar o cambiar el motor de un rol | modifica |
| `agente_corrida` | agentes | Una corrida, paso a paso | lee |
| `agente_corridas` | agentes | Corridas recientes | lee |
| `agente_detener` | agentes | Detener una corrida | modifica |
| `agente_encargar` | agentes | Encargarle algo a un rol | modifica |
| `agente_roles` | agentes | Roles automaticos | lee |
| `modelo_local_activar` | agentes | Cargar un modelo local | modifica |
| `modelo_local_ajustes` | agentes | Ajustes del modelo local | modifica |
| `modelo_local_apagar` | agentes | Apagar el modelo local | modifica |
| `modelo_local_bajar` | agentes | Bajar un modelo local | modifica |
| `modelo_local_borrar` | agentes | Borrar un modelo bajado | modifica |
| `modelo_local_cancelar_descarga` | agentes | Cancelar la descarga | modifica |
| `modelo_local_elegir` | agentes | Probar el modelo local con una frase | lee |
| `modelo_local_estado` | agentes | Modelos locales: que hay y como esta | lee |
| `texto_a_voz` | agentes | Tarjeta «Texto a voz» en un rack | modifica |
| `texto_flujos` | agentes | Las salidas de texto | lee |
| `texto_publicar` | agentes | Escribir una frase en una salida de texto | modifica |
| `voces` | agentes | Las voces (texto a voz) por idioma | lee |
| `voz_ajustes` | agentes | La voz que contesta | modifica |
| `voz_atender` | agentes | Tratar una frase como dictada | modifica |
| `voz_automaticas` | agentes | Acciones de voz sin confirmar | modifica |
| `voz_decir` | agentes | Decirle algo al operador, por los auriculares | modifica |
| `voz_microfono` | agentes | El microfono del operador | modifica |
| `voz_teleprompter` | agentes | Leer el teleprompter en voz alta | modifica |
| `voz_texto` | agentes | Voz a texto en una fuente | modifica |
| `voz_transcripcion` | agentes | Lo que se transcribio | lee |
| `alert_create` | alertas | Crear un disparador | modifica |
| `alert_delete` | alertas | Borrar disparadores | modifica |
| `alert_events` | alertas | Avisos recientes | lee |
| `alert_list` | alertas | Disparadores activos | lee |
| `alert_types` | alertas | Que se puede vigilar | lee |
| `alert_update` | alertas | Cambiar un disparador | modifica |
| `alert_wait` | alertas | Esperar el proximo aviso | lee |
| `assistant_config` | asistente | Configurar el asistente | modifica |
| `assistant_install` | asistente | Instalar el agente | modifica |
| `assistant_run` | asistente | Arrancar / detener el asistente | modifica |
| `assistant_send` | asistente | Mandar un pedido al asistente | modifica |
| `assistant_status` | asistente | Estado del asistente | lee |
| `audio_analyze` | audio | Analizar el audio | lee |
| `audio_escuchar` | audio | Escuchar una fuente por el monitor (PFL), sin tocar el aire | modifica |
| `audio_measure` | audio | Medir un modulo o la cadena (fuera de linea) | lee |
| `audio_set` | audio | Volumen / silencio | modifica |
| `audio_sincro` | audio | Sincronizar labios y voz | modifica |
| `mixer_list` | audio | Mezclador | lee |
| `audio_bus_agregar` | audio_rack | Agregar una entrada o salida virtual | modifica |
| `audio_bus_dispositivo` | audio_rack | A qué dispositivo sale una salida virtual | modifica |
| `audio_bus_quitar` | audio_rack | Quitar un bus virtual | modifica |
| `audio_bus_renombrar` | audio_rack | Renombrar un bus o una pista | modifica |
| `audio_bypass` | audio_rack | Anular / activar un modulo | modifica |
| `audio_compressor` | audio_rack | Compresor con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_deesser` | audio_rack | De-esser con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_dispositivos` | audio_rack | Dispositivos de salida del equipo | lee |
| `audio_editor` | audio_rack | Abrir la interfaz del modulo | modifica |
| `audio_eq` | audio_rack | Ecualizador con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_filtros_catalogo` | audio_rack | Catalogo de filtros de audio (LSP) | lee |
| `audio_gate` | audio_rack | Compuerta (gate) con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_generator` | audio_rack | Generador de señal de prueba | modifica |
| `audio_limiter` | audio_rack | Limitador con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_lsp_restablecer_interfaz` | audio_rack | Restablecer la interfaz LSP | modifica |
| `audio_matriz` | audio_rack | La matriz de mezcla | lee |
| `audio_matriz_cruce` | audio_rack | Prender, apagar o ajustar un cruce | modifica |
| `audio_matriz_equipo` | audio_rack | Entradas y salidas del equipo en la matriz | modifica |
| `audio_module_add` | audio_rack | Agregar un modulo | modifica |
| `audio_module_get` | audio_rack | Parametros de un modulo | lee |
| `audio_module_remove` | audio_rack | Quitar un modulo | modifica |
| `audio_module_set` | audio_rack | Cambiar parametros de un modulo | modifica |
| `audio_modules` | audio_rack | Modulos con interfaz propia | lee |
| `audio_multiband` | audio_rack | Compresor multibanda con nombre propio (lo crea si falta y ajusta sus parámetros) | modifica |
| `audio_ordenar` | audio_rack | Acomodar las tarjetas del rack | modifica |
| `audio_output_set` | audio_rack | Mandar el rack a una salida del equipo | modifica |
| `audio_outputs` | audio_rack | Salidas de audio del equipo | lee |
| `audio_patch` | audio_rack | Conectar cables | modifica |
| `audio_plantillas` | audio_rack | Plantillas de rack | modifica |
| `audio_plugin_buscar` | audio_rack | Buscar entre los ~200 plugins de audio instalados | lee |
| `audio_rack` | audio_rack | Rack de una fuente | lee |
| `audio_show` | audio_rack | Mostrar el rack en la pantalla | modifica |
| `audio_sources` | audio_rack | Fuentes con audio | lee |
| `help` | ayuda | Ayuda del appliance | lee |
| `help_search` | ayuda | Buscar en la ayuda | lee |
| `security_status` | ayuda | Salvaguarda de esta sesion | lee |
| `ptz_apuntar` | camaras | Apuntar a un punto o cuadro de la imagen | modifica |
| `ptz_buscar` | camaras | Buscar cámaras ONVIF en la red | lee |
| `ptz_camaras` | camaras | Las cámaras y su PTZ | lee |
| `ptz_configurar` | camaras | Configurar el PTZ de una entrada | modifica |
| `ptz_encuadre` | camaras | Encuadre digital exacto | modifica |
| `ptz_ir_posicion` | camaras | Llevar la cámara a una posición | modifica |
| `ptz_mover` | camaras | Mover una cámara | modifica |
| `ptz_parar` | camaras | Parar una cámara | modifica |
| `ptz_posicion_borrar` | camaras | Borrar una posición | modifica |
| `ptz_posicion_crear` | camaras | Guardar la posición actual con un nombre | modifica |
| `ptz_posicion_editar` | camaras | Cambiar una posición | modifica |
| `ptz_posiciones` | camaras | Las posiciones con nombre | lee |
| `ptz_preset` | camaras | Ir a un preset o guardarlo | modifica |
| `ptz_rostros` | camaras | Las caras que ve una cámara | lee |
| `ptz_seguir` | camaras | Seguir a una persona con el PTZ | modifica |
| `video_mejora` | camaras | Mejorar imagen (general) | modifica |
| `app_save` | config | Guardar configuracion | modifica |
| `settings_list` | config | Ajustes | lee |
| `settings_set` | config | Cambiar ajustes | modifica |
| `account_check` | cuentas | Probar la conexion | lee |
| `account_credentials_request` | cuentas | Pedir la credencial (formulario seguro) | modifica |
| `account_open` | cuentas | Abrir la pagina de un paso | modifica |
| `account_setup_guide` | cuentas | Pasos para conectar una plataforma | lee |
| `accounts_status` | cuentas | Estado de las cuentas | lee |
| `camara_agregar` | entradas | Conectar una cámara de la red al estudio | modifica |
| `camara_credenciales` | entradas | Usuario y clave de cámaras de la red | modifica |
| `camara_ficha` | entradas | Todo sobre una cámara de la red y cómo conectarla | lee |
| `camara_urls` | entradas | Volver a probar las URLs de una cámara | lee |
| `entrada_borrar` | entradas | Borrar una entrada | modifica |
| `input_status` | entradas | Estado de las entradas | lee |
| `ndi_input_add` | entradas | Agregar una fuente NDI | modifica |
| `ndi_sources` | entradas | Fuentes NDI en la red | lee |
| `network_input_add` | entradas | Agregar una camara o señal de red | modifica |
| `red_escanear` | entradas | Buscar cámaras y equipos de video en la red | lee |
| `video_input_add` | entradas | Agregar una camara o capturadora | modifica |
| `audio_devices` | equipo | Placas de audio | lee |
| `audio_input_add` | equipo | Agregar un microfono o entrada de audio | modifica |
| `monitors` | equipo | Monitores | lee |
| `system_hardware` | equipo | El equipo: monitores, audio, video, GPU | lee |
| `video_devices` | equipo | Camaras y capturadoras | lee |
| `equipo_accion` | equipos | Hacerle algo a un equipo | modifica |
| `equipo_ver` | equipos | Todo sobre un equipo | lee |
| `equipos_escanear` | equipos | Buscar equipos | lee |
| `equipos_listar` | equipos | Los equipos encontrados | lee |
| `scene_copy` | escenas | Copiar una escena | modifica |
| `scene_create` | escenas | Crear escena vacia | modifica |
| `scene_define` | escenas | Definir una escena completa | modifica |
| `scene_delete` | escenas | Borrar escena | modifica |
| `scene_export` | escenas | Exportar definicion de escenas | lee |
| `scene_folder` | escenas | Mover una escena a una carpeta | modifica |
| `scene_list` | escenas | Listar escenas | lee |
| `scene_move` | escenas | Reordenar una escena | modifica |
| `scene_preview` | escenas | Armar la proxima escena (sin sacarla al aire) | modifica |
| `scene_protect` | escenas | Proteger una escena | modifica |
| `scene_rename` | escenas | Renombrar escena | modifica |
| `scene_switch` | escenas | Poner una escena al aire (corte directo) | modifica |
| `scene_take` | escenas | TAKE: sacar al aire la escena armada | modifica |
| `transition_set` | escenas | Transicion entre escenas | modifica |
| `status` | estado | Estado general | lee |
| `evento_abrir` | evento | Abrir un evento | modifica |
| `evento_armar` | evento | Armar un evento entero de una vez | modifica |
| `evento_bloque` | evento | Agregar o cambiar un bloque | modifica |
| `evento_capitulos` | evento | Los capitulos de la grabacion | lee |
| `evento_correr` | evento | Correr el guion | modifica |
| `evento_cue` | evento | Agregar o cambiar un cue | modifica |
| `evento_ensayo_rapido` | evento | Ensayo acelerado | lee |
| `evento_guardar` | evento | Guardar el guion | modifica |
| `evento_informe` | evento | Informe del evento | lee |
| `evento_listar` | evento | Eventos que hay | lee |
| `evento_revisar` | evento | Revision previa | modifica |
| `evento_ver` | evento | El guion abierto | lee |
| `facebook_chat` | facebook | Comentarios del vivo | modifica |
| `facebook_live_create` | facebook | Crear un vivo y transmitir | modifica |
| `facebook_live_end` | facebook | Terminar un vivo | modifica |
| `facebook_live_list` | facebook | Vivos de la Pagina | lee |
| `facebook_live_publish` | facebook | Publicar un vivo en prueba | modifica |
| `facebook_login` | facebook | Conectar una cuenta de Facebook | modifica |
| `facebook_logout` | facebook | Desconectar la cuenta | modifica |
| `facebook_pages` | facebook | Paginas de la cuenta | modifica |
| `facebook_setup` | facebook | Configurar la app de Meta | modifica |
| `facebook_status` | facebook | Estado de Facebook | lee |
| `facebook_video_update` | facebook | Editar o borrar un video | modifica |
| `facebook_video_upload` | facebook | Subir un video a la Pagina | modifica |
| `facebook_videos` | facebook | Videos de la Pagina | lee |
| `guest_source_add` | fuentes | Poner a un invitado en una escena | modifica |
| `source_add` | fuentes | Agregar fuente | modifica |
| `source_list` | fuentes | Fuentes de una escena | lee |
| `source_properties` | fuentes | Propiedades de una fuente | lee |
| `source_remove` | fuentes | Quitar fuente de una escena | modifica |
| `source_set_property` | fuentes | Cambiar una propiedad (o apretar un boton) | modifica |
| `source_types` | fuentes | Tipos de fuente disponibles | lee |
| `source_update` | fuentes | Modificar una fuente en una escena | modifica |
| `grafico_en_escena` | graficos | Fuentes de gráfico que hay | lee |
| `grafico_listar` | graficos | Gráficos que hay | lee |
| `grafico_mostrar` | graficos | Poner un gráfico al aire | modifica |
| `grafico_ocultar` | graficos | Sacar un gráfico | modifica |
| `grafico_probar` | graficos | Ver cómo queda un gráfico | lee |
| `grafico_repertorio` | graficos | Qué se puede usar en un gráfico declarativo | lee |
| `rotulo_desvincular` | graficos | Sacar o apagar un vinculo | modifica |
| `rotulo_vincular` | graficos | Que un rotulo se escriba solo | modifica |
| `rotulo_vinculos` | graficos | Rotulos que se escriben solos | lee |
| `capture_feed` | imagenes | Ver un video de entrada | lee |
| `capture_multiview` | imagenes | Vista multiple de escenas | lee |
| `capture_people` | imagenes | Ver a todas las personas | lee |
| `capture_person` | imagenes | Ver a una persona | lee |
| `capture_program` | imagenes | Ver el programa | lee |
| `capture_sequence` | imagenes | Secuencia de cuadros (video) | lee |
| `capture_source` | imagenes | Ver una fuente o escena | lee |
| `capture_ui` | imagenes | Captura de la app | lee |
| `imagen_analizar` | imagenes | Que se ve: se lo pregunta a un agente que mira | lee |
| `dashboard_background` | interfaz | Fondo del dashboard | modifica |
| `dashboard_card` | interfaz | Agregar / quitar tarjeta | modifica |
| `dashboard_list` | interfaz | Dashboard y ventanas de salida | lee |
| `output_window` | interfaz | Ventanas de salida (multiview) | modifica |
| `ui_barra` | interfaz | La barra de navegacion | modifica |
| `ui_notify` | interfaz | Aviso en pantalla | modifica |
| `ui_page_window` | interfaz | Sacar una pagina a su ventana | modifica |
| `ui_panel_window` | interfaz | Sacar un panel a su ventana | modifica |
| `ui_show_page` | interfaz | Mostrar una pagina | modifica |
| `ventana_captura` | interfaz | Ver la interfaz | lee |
| `laminas_aplicar` | laminas | Poner un carrusel en una escena | modifica |
| `laminas_armar` | laminas | Armar un carrusel a mano | modifica |
| `laminas_buscar` | laminas | Buscar una lamina por lo que dice | lee |
| `laminas_desarmar` | laminas | Desarmar un PDF o un PowerPoint | modifica |
| `laminas_editar` | laminas | Cambiar un carrusel ya armado | modifica |
| `laminas_etiquetar` | laminas | Guardar lo que se ve en una lamina | modifica |
| `laminas_ir` | laminas | Saltar a una lamina | modifica |
| `laminas_listar` | laminas | Que carruseles de laminas hay | lee |
| `laminas_sin_analizar` | laminas | La proxima lamina sin etiquetas | lee |
| `laminas_ver` | laminas | Las laminas de un carrusel | lee |
| `media_carpetas` | medios | Como estan ordenados los medios | lee |
| `media_find` | medios | Buscar un archivo de medios | lee |
| `media_list` | medios | Archivos de medios | lee |
| `media_ordenar` | medios | Mover, renombrar, crear carpetas y borrar | modifica |
| `media_use` | medios | Usar un archivo de medios en una escena | modifica |
| `media_view` | medios | Ver una imagen de medios | lee |
| `guest_chat_pending` | operador | Mensajes de invitados sin responder | lee |
| `guest_reply_suggest` | operador | Sugerir respuestas a un invitado | modifica |
| `operator_ask` | operador | Preguntarle al operador | modifica |
| `operator_message` | operador | Mensaje al operador | modifica |
| `operator_messages` | operador | Barra del operador | lee |
| `participant_requeue` | oradores | Volver a la fila | modifica |
| `speaker_move` | oradores | Reordenar la fila | modifica |
| `speaker_next` | oradores | Siguiente orador | modifica |
| `speaker_on_air` | oradores | Orador al aire / fuera | modifica |
| `speaker_presenter` | oradores | Presentador fijo | modifica |
| `speaker_queue` | oradores | Mandar a la fila de oradores | modifica |
| `speaker_time` | oradores | Tiempo de un orador | modifica |
| `speakers_config` | oradores | Configurar el modo oradores | modifica |
| `speakers_status` | oradores | Fila de oradores | lee |
| `invitacion` | padron | La invitacion de una persona, con su QR | lee |
| `padron_acreditar` | padron | Marcar que alguien llego (o que se fue) | modifica |
| `padron_anotar` | padron | Anotar a alguien en el evento | modifica |
| `padron_cambiar` | padron | Cambiarle los datos a alguien del padron | modifica |
| `padron_puerta` | padron | La camara de la entrada: acreditar sola a quien llega | modifica |
| `padron_quitar` | padron | Sacar a alguien del padron | modifica |
| `padron_ver` | padron | La gente del evento, y quienes estan | lee |
| `carrusel_armar` | pantallas | Armar o cambiar un carrusel | modifica |
| `carrusel_listar` | pantallas | Los carruseles que hay | lee |
| `pantalla_agregar` | pantallas | Agregar una pantalla a mano | modifica |
| `pantalla_buscar` | pantallas | Buscar televisores en la red | modifica |
| `pantalla_fijar` | pantallas | Que una pantalla muestre siempre lo mismo | modifica |
| `pantalla_listar` | pantallas | Que pantallas hay | lee |
| `pantalla_mostrar` | pantallas | Mostrar algo en una pantalla | modifica |
| `pantalla_parar` | pantallas | Dejar de mostrar | modifica |
| `pantalla_renombrar` | pantallas | Cambiarle el nombre a una pantalla | modifica |
| `superficie_listar` | pantallas | Que paginas del aparato estan vivas | lee |
| `superficie_mostrar` | pantallas | Cambiarle la pagina a una superficie viva | modifica |
| `dispositivos_listar` | plugins | Qué equipos hay conectados (cámaras, micrófonos, USB, HID, serie) | lee |
| `plugin_activar` | plugins | Prender o apagar un plugin | modifica |
| `plugin_borrar` | plugins | Borrar un plugin | modifica |
| `plugin_control` | plugins | Pedirle algo a un plugin | modifica |
| `plugin_credenciales` | plugins | Pedir las claves de un plugin | modifica |
| `plugin_desde_ejemplo` | plugins | Copiar un ejemplo como plugin nuevo | modifica |
| `plugin_escribir` | plugins | Crear o cambiar un plugin | modifica |
| `plugin_eventos` | plugins | Lo que publican los plugins (canales de datos) | lee |
| `plugin_exportar` | plugins | Exportar un plugin (para otro aparato) | modifica |
| `plugin_importar` | plugins | Instalar un plugin desde un .zip | modifica |
| `plugin_listar` | plugins | Plugins instalados y ejemplos | lee |
| `plugin_panel` | plugins | El panel de un plugin (lo mismo que ve el operador) | modifica |
| `plugin_probar` | plugins | Probar un plugin sin tocar el sistema | lee |
| `plugin_registro` | plugins | El registro de un plugin | lee |
| `plugin_reiniciar` | plugins | Reiniciar un plugin | modifica |
| `plugin_tarjeta` | plugins | Poner una tarjeta de un plugin | modifica |
| `plugin_ver` | plugins | Ver un plugin (o un ejemplo) | lee |
| `evento_acta` | preguntas | El acta del evento: quienes estuvieron y que se decidio | modifica |
| `invitado_chequeo` | preguntas | Si los que esperan estan listos para salir al aire | lee |
| `pregunta_moderar` | preguntas | Elegir, sacar al aire o descartar una pregunta | modifica |
| `pregunta_sumar` | preguntas | Meter en la cola una pregunta que llego por otro lado | modifica |
| `preguntas_ver` | preguntas | La cola de preguntas del publico | lee |
| `publico_pagina` | preguntas | La pagina del publico: enlace, QR y como se ve | modifica |
| `votacion_abrir` | preguntas | Abrir una votacion o una encuesta | modifica |
| `votacion_ver` | preguntas | Como va la votacion, y cerrarla | modifica |
| `prompter_cargar` | prompter | Mandar un discurso al teleprompter | modifica |
| `prompter_control` | prompter | Manejar el texto que esta al aire | modifica |
| `prompter_documentos` | prompter | Los discursos que hay | lee |
| `prompter_escribir` | prompter | Escribir un discurso | modifica |
| `prompter_estado` | prompter | Como esta el teleprompter | lee |
| `prompter_leer` | prompter | Leer un discurso | lee |
| `qr_borrar` | qr | Borrar un codigo guardado | modifica |
| `qr_exportar` | qr | Dejar un codigo en un archivo | modifica |
| `qr_generar` | qr | Generar un codigo (sin guardarlo) | lee |
| `qr_guardar` | qr | Guardar un codigo en la biblioteca | modifica |
| `qr_listar` | qr | Los codigos guardados, y que se puede generar | lee |
| `receta_borrar` | recetas | Borrar una receta | modifica |
| `receta_correr` | recetas | Correr una receta | modifica |
| `receta_guardar` | recetas | Guardar una receta | modifica |
| `receta_listar` | recetas | Recetas que hay | lee |
| `ext_listar` | registro | Tarjetas de extension | lee |
| `ext_prender` | registro | Prender una extension apagada | modifica |
| `log_leer` | registro | Leer el registro | lee |
| `log_marcar` | registro | Dejar una marca | modifica |
| `log_nivel` | registro | Cuánto se registra | modifica |
| `rostro_autoalta` | rostros | Anotar sola a cada persona nueva que pase por una camara | modifica |
| `rostro_borrar` | rostros | Borrar una persona de la libreta | modifica |
| `rostro_calidad` | rostros | Si la cara que se ve sirve para reconocer, y que pedir si no | lee |
| `rostro_fusionar` | rostros | Juntar dos fichas de la misma persona | modifica |
| `rostro_guardar` | rostros | Dar de alta o corregir a una persona | modifica |
| `rostro_listar` | rostros | La libreta de personas | lee |
| `rostro_lobby` | rostros | Quien es cada uno de los que estan esperando | lee |
| `rostro_pendientes` | rostros | Las fichas sin nombre, esperando que alguien diga quien es | lee |
| `rostro_sumar` | rostros | Sumarle una pose mas a alguien que ya esta | modifica |
| `rostro_ver` | rostros | Quien aparece en una camara | lee |
| `chat_history` | sala | Chat reciente | lee |
| `chat_send` | sala | Escribir en el chat | modifica |
| `chat_wait` | sala | Esperar que alguien escriba | lee |
| `gateway_restart` | sala | Reiniciar la sala (gateway) | modifica |
| `network_status` | sala | Red y acceso de invitados | lee |
| `operator_feed` | sala | Como te ve y oye el lobby | modifica |
| `participant_admit` | sala | Hacer pasar a alguien del lobby | modifica |
| `participant_admit_all` | sala | Admitir a todos | modifica |
| `participant_kick` | sala | Sacar a alguien de la sala | modifica |
| `participant_profile` | sala | Datos de una persona | modifica |
| `participant_reject` | sala | No dejar pasar a alguien del lobby | modifica |
| `participant_request` | sala | Pedirle algo a un invitado | modifica |
| `participant_talk` | sala | Hablar en privado (lobby) | modifica |
| `room_link` | sala | Enlace y QR de ingreso (o del tablero) | lee |
| `room_pin` | sala | Cambiar el PIN | modifica |
| `room_set` | sala | Lobby / sala cerrada | modifica |
| `room_status` | sala | Estado de la sala | lee |
| `share_delete` | sala | Borrar archivo compartido | modifica |
| `share_list` | sala | Archivos compartidos | lee |
| `share_take` | sala | Poner un archivo compartido en la escena | modifica |
| `share_view` | sala | Ver un archivo compartido | lee |
| `grabacion_individual` | salidas | Grabar cada entrada por separado | modifica |
| `record_start` | salidas | Grabar | modifica |
| `record_stop` | salidas | Detener grabacion | modifica |
| `stream_start` | salidas | Salir al aire (transmitir) | modifica |
| `stream_stop` | salidas | Cortar la transmision | modifica |
| `app_quit` | sistema | Cerrar la aplicacion | modifica |
| `feeds_list` | sistema | Videos disponibles | lee |
| `logs_tail` | sistema | Registros | lee |
| `system_info` | sistema | Carpeta del sistema, credenciales y respaldos | lee |
| `system_update` | sistema | Actualizar el sistema | modifica |
| `glosario_agregar` | traduccion | Sumar o corregir un término del glosario | modifica |
| `glosario_quitar` | traduccion | Sacar un término del glosario (o apagar un grupo) | modifica |
| `glosario_ver` | traduccion | El glosario de la traducción | lee |
| `subtitulos_limpiar` | traduccion | Borrar ya los subtítulos de pantalla | modifica |
| `traduccion_agregar` | traduccion | Traducir en vivo una salida de texto | modifica |
| `traduccion_ajustar` | traduccion | Pausar o cambiar una traducción | modifica |
| `traduccion_bajar` | traduccion | Bajar los paquetes de un idioma | modifica |
| `traducciones` | traduccion | Traducciones en vivo: cuáles hay y cómo andan | lee |
| `traducir` | traduccion | Traducir una frase ahora | lee |
| `voz_cortes` | traduccion | Cuándo corta una frase la transcripción continua | modifica |
| `twitch_channel` | twitch | Canal: titulo, categoria, etiquetas | modifica |
| `twitch_chat` | twitch | Chat del canal | modifica |
| `twitch_live_start` | twitch | Transmitir a Twitch | modifica |
| `twitch_live_stop` | twitch | Cortar la transmision a Twitch | modifica |
| `twitch_login` | twitch | Conectar una cuenta de Twitch | modifica |
| `twitch_logout` | twitch | Desconectar la cuenta | modifica |
| `twitch_setup` | twitch | Configurar la app de Twitch | modifica |
| `twitch_status` | twitch | Estado de Twitch | lee |
| `twitch_stream_status` | twitch | Estado del vivo en Twitch | lee |
| `twitch_video_delete` | twitch | Borrar VODs | modifica |
| `twitch_videos` | twitch | VODs del canal | lee |
| `monitor_identificar` | ventanas | Mostrar en cada monitor cual es | modifica |
| `ventana_cerrar` | ventanas | Cerrar una ventana | modifica |
| `ventana_listar` | ventanas | Que ventanas hay abiertas y donde | lee |
| `ventana_mover` | ventanas | Mover, redimensionar o mandar a otro monitor | modifica |
| `ventanas_ordenar` | ventanas | Acomodar todas las ventanas en los monitores | modifica |
| `superres_aplicar` | video | Superresolución EN LA FUENTE de una entrada de red (alternativa a la tarjeta) | modifica |
| `superres_entrenar` | video | Entrenar la red chica mirando una cámara (un botón) | modifica |
| `superres_estado` | video | Superresolución: motores, modelos y el trabajo en curso | lee |
| `superres_modelo` | video | Bajar y convertir un modelo de superresolución (SPAN o Real-ESRGAN) | modifica |
| `superres_tarjeta` | video | Agregar una tarjeta de superresolución al grafo de una entrada | modifica |
| `video_cable` | video | Conectar o desconectar en el grafo de video | modifica |
| `video_controlador` | video | Manejar tarjetas de varias entradas con un controlador | modifica |
| `video_desfase` | video | Correr una entrada en el tiempo a mano | modifica |
| `video_entrada_red` | video | Recibir un stream de red (SRT, RTMP, RTSP, UDP, HTTP) sincronizado | modifica |
| `video_entrada_virtual` | video | Crear una entrada virtual (barras, negro, un pipeline de GStreamer) | modifica |
| `video_estado` | video | Las entradas de video y cómo está cada una (y qué le pasa) | lee |
| `video_filtros` | video | Los filtros de video de OBS (y los controles de una tarjeta) | lee |
| `video_grafo` | video | El grafo de video | lee |
| `video_grafo_limpiar` | video | Volver el video de una entrada a como vino | modifica |
| `video_modulos` | video | Los módulos de video de GStreamer (catálogo documentado) | lee |
| `video_mosaico` | video | Mosaico, imagen en imagen o lado a lado de varias entradas (en una llamada) | modifica |
| `video_ordenar` | video | Acomodar las tarjetas del grafo de video | modifica |
| `video_pipeline` | video | Armar la cadena de tarjetas de una entrada (en una llamada) | modifica |
| `video_reloj` | video | El reloj del estudio (sincronía de las entradas de red) | modifica |
| `video_salida_a_escena` | video | Poner una salida o un compositor del grafo en una escena | modifica |
| `video_sincronia` | video | Medir (y corregir) cuánto atrasa una entrada respecto de otra | modifica |
| `video_tarjeta_agregar` | video | Agregar una tarjeta de video | modifica |
| `video_tarjeta_ajustar` | video | Ajustar una tarjeta de video | modifica |
| `video_tarjeta_quitar` | video | Quitar una tarjeta de video | modifica |
| `chat_overlay_add` | youtube | Chat en pantalla (fuente de escena) | modifica |
| `youtube_chat` | youtube | Chat del vivo | modifica |
| `youtube_live_create` | youtube | Crear un evento en vivo | modifica |
| `youtube_live_end` | youtube | Terminar un evento | modifica |
| `youtube_live_health` | youtube | Salud de la ingesta | lee |
| `youtube_live_list` | youtube | Eventos en vivo | lee |
| `youtube_live_start` | youtube | Transmitir a un evento | modifica |
| `youtube_live_update` | youtube | Editar o cambiar el estado de un evento | modifica |
| `youtube_login` | youtube | Conectar una cuenta de YouTube | modifica |
| `youtube_logout` | youtube | Desconectar la cuenta | modifica |
| `youtube_setup` | youtube | Configurar la credencial de Google | modifica |
| `youtube_status` | youtube | Estado de YouTube | lee |
| `youtube_video_delete` | youtube | Borrar un video | modifica |
| `youtube_video_update` | youtube | Editar un video | modifica |
| `youtube_video_upload` | youtube | Subir un video (p. ej. la grabacion) | modifica |
| `youtube_videos` | youtube | Videos del canal | lee |
