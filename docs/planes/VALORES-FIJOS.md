# Valores fijos en el código que deberían ir a Configuración

Relevamiento del 2026-10-11 (pedido: «busca cosas hardcodeadas como tiempos y
cosas así … todo eso debería ir a la configuración»). Excluye lo que ya está en
`src/app/ui/SettingsModel.cpp`, animaciones de la interfaz, constantes de
protocolo, tests y detalles internos (spins, buffers de parsers). Precedente:
la placa de entradas sin uso (`entradas.placaReposo`).

Prioridad: **alta** = se nota al aire o en la confiabilidad; media; baja.

## Hecho (2026-10-11)

Ya están en Configuración (clave entre paréntesis):

- Grabación: colchón si el disco se traba (`record.colchon`), qué hacer si el disco no da abasto (`disco.prioridad`, antes solo en `disco.json` y por MCP), cuándo entra y sale de «apuros» (`disco.apuros`, `disco.recuperado`).
- Transmisión: espera máxima entre reconexiones (`stream.reintentoMax`; el cálculo estaba copiado dos veces, ahora es uno).
- Cámaras y entradas: placa «sin señal» (`entradas.sinSenal`), rearmar sin datos (`entradas.rearmarSinDatos`), espera máxima entre reintentos (`entradas.reintentoMax`).
- Invitados: placa si un invitado no manda video (`gateway.sinVideoPlaca`).
- Sala: PIN, intentos y bloqueo (`room.pinIntentos`, `room.pinBloqueo`); integrantes mirando a la vez y retorno a las pantallas (`room.vistasRemotas`, `gateway.programaW/H`, antes solo en `gateway.json`).
- Asistente (MCP): cuánto espera una aprobación (`mcp.aprobacionMin`).
- Modelos locales: contexto y capas en la GPU del chat sin internet (`modelo.contexto`, `modelo.capasGpu`).
- Sistema y datos → Arranque y recuperación: colgado si no responde, gracia al arrancar, caídas antes del modo seguro y su ventana, reinicios sin preguntar (`launcher.*`, en `launcher.json`).
- Guardar la configuración de la sala ya no borra de `gateway.json` lo que la interfaz no maneja.

No se duplicaron en la página porque ya tienen su lugar en la interfaz: la retención de grabaciones y el aviso de disco (página Histórico), la espera de la interfaz trabada (tarjeta de salud) y el tope de latencia del estudio (panel de sincronía).

### Prioridad media y baja (2026-10-11, segunda tanda)

Todo lo de las tablas de abajo pasó a Configuración como **ajustes avanzados** (170), salvo lo que se lista al final. Cómo funciona:

- `src/core/ajustes/Ajustes.{hpp,cpp}`: un registro por proceso. La app lo carga de `app.json`, la sala de `gateway.json` y el launcher de `launcher.json`, siempre bajo `"ajustes"` y solo con lo que se cambió. El código lo consulta al usarlo (`ajustes::entero("stream.colaMs", 1000)`), con el valor de siempre como respaldo.
- `src/app/ui/AjustesAvanzados.cpp`: la tabla (rótulo, rango, unidad, explicación, sección, a qué archivo va). `SettingsModel` los suma a su sección con el rol `avanzado`.
- En la página: interruptor «Mostrar ajustes avanzados» arriba a la derecha. Las secciones que solo tienen avanzados (Audio, Plugins y descargas) los muestran siempre. También van en Traducción, Voz del operador y Modelos locales.
- Lo de la app rige en caliente (al aplicar se recarga el registro); lo de la sala reinicia la sala; lo del launcher, al volver a abrir el programa.
- Tests: `src/core/tests/test_ajustes.cpp`.
- Plugins incluidos: sus esperas van en su `config` (`espera_s`, `conectar_s`, `sincronizar_s`, `reconectar_s`, `sin_aparato_s`, `timeout_s`), con el valor de siempre si no está.
- Además: «Ancho de las vistas previas» (`ui.previewWidth`), el puerto de métricas del túnel y los puertos que redirige WireGuard (antes solo en los archivos).

De paso: con la grabación partida en tramos, al terminar se pasan a MP4/MOV todos los tramos (antes solo el último); y un reinicio pedido de la sala (al aplicar Configuración) ya no se anota como caída ni muestra el cartel «se reinició».

**No se pasaron**, a propósito:

- El anillo de audio entre procesos (`SharedAudioRing::kDefaultCapacityBlocks`): lo crean la app y la sala y lo lee el otro; si cada uno tuviera otro tamaño se rompe el audio de los invitados.
- Los anillos estéreo (`RingEstereo`): están en el camino de cada bloque de audio.
- La caché del chequeo de integridad del launcher, los 3 s para cerrar procesos hijos sueltos en plugins/modelos/cloudflared (internos; el del launcher sí está), el puerto de WireGuard por defecto (51820) y el de Cast (8009): son del protocolo.
- `statusEverySec` (solo para el modo sin interfaz).

## Ya configurables, pero solo en un archivo (no en la página)

- `app.json`: `estudio.topeMs` (latencia del estudio, 250 ms por defecto, `src/motor/Reloj.hpp:86`), `ui.previewWidth` (960), `statusEverySec`.
- `gateway.json`: `metricsPort` (9099), `vistasRemotas` (16), `programVideoW/H` (1280x720), `wgHttpPort/wgHttpsPort`.
- `Config/disco.json` (`priorizar_programa`), `Config/historico.json` (`retencion.grabaciones_dias`, `aviso_disco_pct` 90), `Config/supervisor.json` (`espera_ui_s`).

## Salidas / grabación

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/app/emision/BroadcastEngine.cpp:1269 | `colchonSeg = 60` | colchón en RAM de la grabación del programa si el disco se traba | `record.colchon` (s) | alta |
| src/motor/Salidas.cpp:718,754 (+677) | `{1,2,4,8,10}` | espera entre reconexiones de la transmisión (tope 10 s) | `stream.reintentoMax` (s) | alta |
| src/app/emision/BroadcastEngine.cpp:1715 | `colchonSeg = 10` | colchón de cada ISO | `record.colchonIso` (s) | media |
| src/motor/Salidas.cpp:327,354 | `512 MB` | tope en bytes del colchón | `record.colchonMB` | media |
| src/app/emision/BroadcastEngine.cpp:1263/1708/1807 | `tramoSeg = 0` | grabar en un archivo, sin partir en tramos | `record.tramo` (min) | media |
| src/motor/Salidas.cpp:706,761 | 2 s | sin bytes salientes → «desconectada» | `stream.sinDatos` (s) | media |
| src/motor/Salidas.cpp:588,592 | 1 s leaky | cola antes del encoder del stream | `stream.colaMs` | media |
| src/motor/Salidas.cpp:330,334 | 3 s leaky | cola antes del encoder de la grabación | `record.colaEncoder` (s) | baja |
| src/motor/Salidas.cpp:35 | 300 ms | margen de latencia de las salidas | `salidas.margenMs` | baja |
| src/motor/Salidas.cpp:692 | 2 s | espera del EOS al cortar la transmisión | `stream.cierreMs` | baja |
| src/motor/Salidas.cpp:209 | 120 s | tope para juntar los tramos de la repetición | `record.replayJuntarS` | baja |
| src/motor/Salidas.cpp:264-268 | 2 s / 1 s / 10 s | tramos de la repetición | `record.replayTramo` | baja |

## Entradas / video

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/motor/Entrada.cpp:292 | 3 s | sin cuadros → placa «sin señal» | `entradas.sinSenal` (ms) | alta |
| src/motor/Entrada.cpp:1176-1177 | 10 s | entrada sin datos y sin error: se rearma | `entradas.rearmarSinDatos` (s) | alta |
| src/motor/Entrada.cpp:603 | `{200ms,500ms,1s,2s}` | reintentos al armar una entrada (tope 2 s) | `entradas.reintentoMax` (s) | alta |
| src/motor/Entrada.cpp:295 | 2,5 s | cuánto se sostiene el último cuadro | `entradas.congelarMs` | media |
| src/motor/Entrada.cpp:817 | 20 ms | latencia mínima del jitter buffer RTSP/SRT | `entradas.latenciaRedDefecto` | media |
| src/app/emision/InputSpecs.cpp:204-207 | `low`, `tcp` | modo y transporte por defecto de las entradas de red | `entradas.redModoDefecto`, `entradas.rtspTransporte` | media |
| src/motor/Composicion.cpp:31 | 700 ms | lo más que un corte espera el primer cuadro | `video.esperaCorte` | media |
| src/motor/Reloj.hpp:86 | 250 ms | tope automático de latencia del estudio | `video.topeEstudio` | media |
| src/gateway/Config.hpp:81-82 | 1280x720 | retorno del programa a las pantallas | `gateway.programaW/H` | media |
| src/motor/Entrada.cpp:1252 | 2 s / 15 s | buscar de nuevo una ventana capturada | `entradas.buscarVentana` | baja |
| src/motor/Entrada.cpp:1104-1105 | 10 s / 20 s / 60 ms | realineo automático A/V | `entradas.realineoQuieto` | baja |
| src/motor/Entrada.cpp:1153 | −400 ms | aviso «hora de la cámara corrida» | `entradas.avisoNtpMs` | baja |
| src/app/emision/BroadcastEngine.cpp:821,846 | 10 s / 700 ms | vistas capturadas sin uso / primera captura | `video.vistaReposo` | baja |
| src/app/video/VideoGrafos.cpp:2005-2010 | 10 min, 2 s, 15 s→120 s | superresolución: paso a «barata» y vuelta | `superres.bypass*` | baja |
| src/app/arranque/AppSettings.cpp:281 | 960 | ancho del preview | `ui.previewWidth` | baja |

## Audio

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/core/ipc/SharedAudioRing.hpp:61 | 16 bloques (160 ms) | anillo de audio de los invitados | `audio.anilloMs` | media |
| src/motor/Mezcla.hpp:131 | 30 ms | margen de latencia del mezclador | `audio.margenMezcla` | baja |
| src/motor/Mezcla.cpp:138 | 1,5 s | cola de cada fuente antes del mezclador | `audio.colaFuente` | baja |
| src/motor/Mezcla.cpp:140-141 | 50 ms / 300 ms | ritmo de los medidores y retención del pico | `audio.medidorPicoMs` | baja |
| src/motor/Motor.cpp:72,941; src/motor/Escucha.cpp:53 | 2 s | colas de monitoreo | `audio.colaEscucha` | baja |
| src/app/audio/RingEstereo.hpp:19-20 | 20 ms / 80 ms | colchón y tope de los anillos estéreo | `audio.colchonMs` | baja |
| src/app/audio/AudioRack.cpp:850 | 2 s | tope del retardo de línea | `audio.retardoMax` | baja |
| src/app/audio/Escucha.hpp:56 | 30 s | segmento máximo a transcripción | `escucha.segmentoMax` | baja |
| src/app/audio/Matriz.cpp:153 | 3 s | reescaneo de equipos de audio | `audio.rescanEquipos` | baja |

## Sala / gateway / red

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/gateway/MediaPump.cpp:900 | 1 s | invitado sin video → placa | `guests.sinVideoPlaca` (ms) | alta |
| src/gateway/SignalingServer.hpp:324-325 | 5 intentos / 60 s | PIN: intentos por IP y bloqueo | `room.pinIntentos`, `room.pinBloqueo` | alta |
| src/gateway/Webrtc.cpp:1299 | 10 s / 20 s | limpieza de visores caídos | `room.visorCaidoS` | media |
| src/gateway/Webrtc.cpp:775-776 | 5/2 %, 400/200 ms, 50/30 ms | semáforo de red del invitado | `room.semaforo*` | media |
| src/gateway/Webrtc.hpp:252 | 48 | tope de visores WebRTC | `room.maxVisores` | media |
| src/gateway/Config.hpp:90 | 16 | integrantes mirando un canal a la vez | `sesion.vistasRemotas` | media |
| src/gateway/Room.hpp:66,68 | 180 s / 10 s | tiempo por orador y gracia | `room.oradorSeg`, `room.oradorGracia` | media |
| src/gateway/main.cpp:518-520 | 100 / 60 s / 300 s | tope global de PIN y vida del token del invitado | `room.tokenTtl` | media |
| src/gateway/SignalingServer.hpp:426-427 | 64 / 15 s | conexiones simultáneas y handshake | `room.maxConexiones`, `room.handshakeMs` | media |
| src/gateway/Sesion.cpp:59-66 | 30 s, 15 s, 10, 15 min, 96 KB | sesión remota de integrantes | `sesion.bloqueoMin`, `sesion.intentos` | media |
| src/gateway/proxy/ProxyNombres.cpp:49-51 | 10 s, 15 min, 4000 | proxy por nombre | `proxy.ociosoMin` | media |
| src/gateway/Cloudflared.cpp:193,209 | 1 s → 30 s | reintentos del túnel de Cloudflare | `gateway.tunnelBackoffMax` | media |
| src/app/ui/ConferenceClient.cpp:20-21 | 15 s / 40 s | reconexión con la sala y cuándo se la da por muerta | `room.watchdog` | media |
| src/app/ui/PantallasViewModel.cpp:39,41,412 | 8 s, 5 s, 15 s | pantallas (TV/Cast) | `pantallas.timeout`, `pantallas.mantenimiento` | media |
| src/app/ptz/PtzVisca.cpp:17,21 | 1,5 s / 3 s | VISCA: sin respuesta y reconexión | `ptz.reconexion` | media |
| src/app/ptz/PtzOnvif.cpp:70,250; PtzPanasonic.cpp:71; dispositivos/Onvif.cpp:29 | 4/3/2/6 s | timeouts HTTP de PTZ | `ptz.timeoutHttp` | media |
| src/gateway/SignalingServer.cpp:1065 | 60 s | timeout de socket de las subidas | `room.socketTimeout` | baja |
| src/gateway/SignalingServer.hpp:383-384 | 12 h / 32 | pantallas: vida desconectada y cupo | `pantallas.ttlHoras` | baja |
| src/gateway/Botonera.cpp:62-66 | 30 s, 10 s, 5 min, 60/s | botonera web | `botonera.solicitudMin` | baja |
| src/gateway/AdminApi.cpp:177 | 120 s | código de login de la consola web | `room.adminCodigo` | baja |
| src/gateway/Superficies.hpp:36 / Prompter.hpp:51 | 15 s / 4 s | superficie o prompter remoto desconectado | — | baja |
| src/app/ui/ConferenceViewModel.cpp:126,129,29 | 5 s / 2,5 s | sondeos y espera para abordar a un recién llegado | `room.sondeo`, `asistente.esperaLobby` | baja |
| src/app/red/Red.cpp:52-53,393,609,655 | 256, 600 ms, 2,5 s, 10 s | escaneo de red | `red.escaneo*` | baja |
| src/app/control/Botonera.cpp:31-34 | 20/s, 2 min, 5 s, 700 ms | botonera local | `botonera.confirmarMs` | baja |
| src/gateway/Config.hpp:61 | 9099 | puerto de métricas de cloudflared | `gateway.metricsPort` | baja |

## Asistente / agentes / modelos

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/app/mcp/McpService.cpp:699 | 10 min | cuánto espera una aprobación del MCP antes de rechazar | `mcp.aprobacionTimeout` (min) | alta |
| src/app/agent/AgentViewModel.cpp:1612 | `-ngl 999 -c 32768` | capas en GPU y contexto del modelo local de chat | `modelo.contexto`, `modelo.capasGpu` | alta |
| src/app/agent/AgentViewModel.cpp:1406-1409 | 32768 / 4096 / temp 0,7… | parámetros del modelo local | `modelo.maxTokens`, `modelo.temperatura` | media |
| src/app/agent/AgentViewModel.cpp:1640 | 3 min | tope de carga del modelo local | `modelo.cargaTimeout` | media |
| src/app/agent/Despachador.hpp:71-73 | 20 min / 200 | tope de una corrida de rol e historial | `agentes.topeMin`, `agentes.historial` | media |
| src/app/agent/Traductor.cpp:77,461 | 5 / 500 ms | caídas del traductor antes de rendirse | `traductor.maxCaidas` | media |
| src/app/ui/StudioViewModel.cpp:3851 | 15 s | el rótulo de traducción se apaga | `traductor.rotuloApagar` | media |
| src/app/agent/VozPrompter.cpp:33-34 | 250 ms / 6 s | prompter por voz: anticipo | `vozPrompter.anticipo` | media |
| src/app/agent/Subtitulos.cpp:23 | 4 | líneas de subtítulos | `cc.lineas` | media |
| src/app/ui/PersonasViewModel.cpp:207-238 | 3, 12 s, 500, 700 ms, 0,60, 30 s | reconocimiento facial (umbral para admitir sin preguntar…) | `rostros.umbralAdmitir`, `rostros.maxFichas` | media |
| src/core/rostros/Rostros.hpp:57 | 0,45 | umbral para ponerle nombre a una cara | `rostros.umbralNombre` | media |
| src/app/agent/AgentViewModel.cpp:257,1797,1806,1708,926,423 | 2,5 s, 1,5 s, 600 ms, 2 s, 10 min, 8 s | arranque, agrupación de alertas, instalación, probar | `asistente.*` | baja |
| src/app/agent/Traductor.cpp:519 | 280 | caracteres antes de cortar y traducir | `traductor.largoMax` | baja |
| src/app/agent/ModelosLocales.cpp:424,670,865,489 | 20/30/15 s, 6 h | Hugging Face: timeouts y cache de fichas | `modelos.timeoutHf`, `modelos.cacheHoras` | baja |
| src/app/mcp/McpImageTools.cpp:188 | 45 s | generación de imágenes | `mcp.imagenTimeout` | baja |
| src/app/mcp/McpTraza.cpp:127 | 8 MB | tope de la traza MCP | `mcp.trazaMB` | baja |
| src/app/ui/CalidadCara.hpp:50-54 | 80, 35, 45-218, 0,28 | calidad mínima de una cara | `rostros.calidad*` | baja |

## Descargas

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/app/net/Descargas.cpp:23 | 2 | descargas simultáneas | `descargas.simultaneas` | media |
| src/app/net/Descargas.cpp:212 | 60 s | timeout por operación de una descarga | `descargas.timeout` | media |
| src/app/plugins/Plugins.cpp:703 | 15 min | tope del pip de un plugin | `plugins.pipTimeout` | media |
| src/app/net/Descargas.cpp:24 | 100 | historial de descargas | `descargas.historial` | baja |
| src/app/net/HttpsClient.hpp:39 | 30 s | timeout HTTP por defecto | `red.httpTimeout` | baja |

## Launcher / supervisor / caídas

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/core/ipc/HealthBlock.hpp:225 | 5 s | latido quieto = cuelgue (volcado y reinicio) | `launcher.cuelgue` (s) | alta |
| src/launcher/Supervisor.cpp:50 | 20 s | gracia de arranque antes de vigilar el latido | `launcher.graciaArranque` | alta |
| src/launcher/Supervisor.cpp:39-40 | 5 en 2 min | cortacircuito de caídas | `launcher.maxCaidas`, `launcher.ventanaCaidas` | alta |
| src/launcher/Supervisor.cpp:44 | 2 | caídas seguidas que se reinician sin preguntar | `launcher.reiniciosSinPreguntar` | alta |
| src/launcher/Supervisor.cpp:55 | 90 s | espera al primer latido | `launcher.primerLatido` | media |
| src/launcher/Supervisor.cpp:66-71 | `{0,1,2,5}` tope 10 s | espera entre relanzamientos | `launcher.backoffMax` | media |
| src/launcher/Supervisor.cpp:45-46 | 5 s / 3 s | cierre ordenado y SIGTERM antes de matar | `launcher.cierreMs` | media |
| src/app/emision/LatidoAire.cpp:80,84 | 1 s / 2 s | «programa sin avance» / «interfaz trabada» | `supervisor.uiTrabada` (+ `espera_ui_s` a la página) | media |
| src/app/plugins/Plugins.cpp:131-132 | 5 en 60 s | plugin que se cae: se desactiva | `plugins.maxCaidas` | media |
| src/app/plugins/Hijo.cpp:95; Plugins.hpp:158; Plugins.cpp:1984 | 10 s / 10 s / 60 s | arranque y llamadas de plugins | `plugins.timeoutLlamada` | media |
| src/launcher/Supervisor.cpp:57 | 60 s | reintento tras falla de entorno | `launcher.reintentoEntorno` | baja |
| src/launcher/Updater.hpp:37; main.cpp:121 | 60 s | espera a que la app cierre al actualizar | `launcher.esperaCierre` | baja |
| src/launcher/ChildOutput.hpp:40 | 5 MB | rotación de la salida de los hijos | `logs.salidaMB` | baja |
| src/app/ui/StudioViewModel.cpp:532; Main.qml:632 | 10 min / 30 s | cartel «volvimos de una caída» | `launcher.cartelCaida` | baja |
| src/app/graficos/GraficoProceso.cpp:21 | 20 s | tope de render de un gráfico | `graficos.limite` | baja |
| src/app/carrusel/Conversion.cpp:35 | 5 min | conversión con LibreOffice | `carrusel.limiteOffice` | baja |

## Disco

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/app/emision/BroadcastEngine.cpp:1498-1499 | 3 s / 0,5 s / 3 s / 30 s | cuándo el disco entra y sale de «apuros» | `disco.apurosS`, `disco.recuperadoS` | alta |
| src/app/emision/BroadcastEngine.cpp:1519 (`disco.json`) | `priorizar_programa` | preguntar / automático / nunca (está en archivo, no en la página) | `disco.prioridad` | alta |
| src/app/ui/HistoricoViewModel.cpp:369,401 | 0 días / 90 % | retención y aviso de disco de Eventos | `historico.retencionDias`, `historico.avisoPct` | media |
| src/app/ui/StudioViewModel.cpp:1609-1611 | 1 min / 1 h | revisión de disco y retención | `historico.revisarCada` | baja |

## Plugins incluidos (assets)

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| assets/plugins/bosch-dcn/plugin.py:36 | 5 s | espera de respuesta de la CCU | config del plugin `espera_s` | media |
| assets/plugins/conferencia-recinto/plugin.py:43,49 | 10 s / 5 s | conexión y reconexión al recinto | config del plugin `reconexion_s` | media |
| assets/plugins/bosch-dcn/plugin.py:321,340 | 5 s / 30 s | conexión y sincronización con la CCU | config del plugin | baja |
| assets/plugins/superficie-streamdeck/plugin.py:102 | 30 s | reintento sin Stream Deck | config del plugin | baja |

## Otros

| lugar | valor | qué controla | clave sugerida | prio |
|---|---|---|---|---|
| src/app/ui/YouTubeViewModel.cpp:438,483,446; FacebookViewModel.cpp:296,289 | 15/20/5 s, 300 | sondeo del chat y mensajes guardados | `chat.sondeo`, `chat.historial` | media |
| src/app/ui/TwitchViewModel.cpp:67; TwitchClient.cpp:616 | 30 s / 60 s | estado y reconexión del chat de Twitch | `twitch.reconexionMax` | baja |
| src/app/youtube/YouTubeClient.cpp:32; FacebookClient.cpp:242 | 5 min | tope del login OAuth | `plataformas.loginTimeout` | baja |
| src/app/agent/Notificador.cpp:134 | 10 s | timeout de webhooks | `notificaciones.timeout` | baja |
| src/app/ui/RegistroViewModel.cpp:29; Medios.cpp:25; Carrusel.hpp:50; FlujosTexto.cpp:18 | 800/5000/500/2000 | topes de listas | `limites.*` | baja |
| src/app/ui/StudioViewModel.cpp:284,1344; GuiMode.cpp:356 | 2 / 1,5 / 3 s | demoras al restaurar tras un corte | `arranque.demoraRestaurar` | baja |

## Repetidos (si se configuran, que sea un solo valor)

- Reconexión de la transmisión `{1,2,4,8,10}`: dos veces en `src/motor/Salidas.cpp` (718, 754), más el primer intento (677); la ventana de 2 s también dos veces (706, 761).
- 3 s para cerrar o matar un proceso hijo: Supervisor, Plugins, Hijo, Cloudflared, ModelosLocales, AgentViewModel, Grabacion::parar, SignalingServer.
- 5 caídas como máximo: plugins (en 60 s), traductor (sin ventana), launcher (en 2 min).
- Login OAuth de 5 min y margen de refresco de token de 60 s: YouTube, Facebook, Twitch.
- Latido de 30 s y límites de la botonera web y la sesión remota del gateway (auth 10 s en una, 15 s en la otra).
- Bloqueo de PIN de 60 s: por IP y global.
- Colas de 2 s del monitoreo: tres lugares.
- Timeouts de Hugging Face de 15, 20 y 30 s en ModelosLocales.
