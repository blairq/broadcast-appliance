# Tareas que salen del informe de experto

> Plan de trabajo a partir de [INFORME-EXPERTO-BROADCAST.md](INFORME-EXPERTO-BROADCAST.md). Cada hallazgo del informe se verificó contra el código del 2026-10-05 (rama `master`, commit `42be809` más los cambios sin commitear de superres). Las rutas `archivo:línea` son de ese momento: si el código se movió, buscá el símbolo.
>
> **Regla del producto que ordena todo:** cada función se usa desde la interfaz (un humano con mouse, teclado o pantalla táctil) **y** desde el MCP (el asistente). Por eso cada tarea tiene una parte **MCP** y una parte **GUI**. Ninguna se da por terminada con una sola de las dos.
>
> **Regla del bundle:** nada del sistema en runtime. Toda biblioteca nueva (Python o nativa) se vendorea en el bundle por plataforma.
>
> **Hardware** (botoneras, pedales, tally físico, MIDI, GPIO, Stream Deck, X-keys, Loupedeck, joysticks, sistemas de conferencia): **no se escriben drivers nativos.** Se arma una superficie estable de API, MCP y eventos (T50, T51, T52-T58). Con esa base, el operador escribe con ayuda del asistente un plugin de Python para su equipo, usando protocolos abiertos.

**Prioridades**

| Nivel | Significado |
|---|---|
| **P0** | Crítico al aire: sin esto una sesión real corre riesgo de corte, de error visible o de incumplimiento. |
| **P1** | Importante: lo espera un operador con experiencia, y su falta cuesta tiempo o calidad todos los días. |
| **P2** | Mejora: suma, pero se puede vivir sin eso. |

**Estado verificado**: **hecho**, **parcial** o **falta**. Al lado va la evidencia en el código.

---

## Resumen

| ID | Título | Prioridad | Estado |
|---|---|---|---|
| T01 | Latido del supervisor fuera del hilo de la interfaz | P0 | hecho |
| T02 | Guardas de aire para todo lo automático (asistente, voz, recetas, plugins, Bash) | P0 | hecho |
| T03 | Disparadores con rol restringido | P0 | hecho |
| T04 | Disparadores de fábrica activos, alarma sonora y tipos nuevos | P0 | hecho |
| T05 | Revisión previa que diga la verdad, cue Pantalla y lista de chequeo de sesión | P0 | hecho |
| T06 | "Al aire" del editor arma en vez de cortar; tecla T segura | P0 | hecho |
| T07 | FTB / placa de seguridad a un toque | P0 | hecho |
| T08 | `stream_start` con otro destino estando al aire: error explícito | P0 | hecho |
| T09 | Multidestino (principal + respaldo) | P0 | hecho (destinos RTMP extra sin probar contra un servidor) |
| T10 | Grabación oficial segura: MKV o remux y chequeo de disco | P0 | hecho |
| T11 | Medidor de loudness del programa (M/S/I, true peak, LRA) | P0 | hecho |
| T12 | Limitador de seguridad en el máster | P0 | decisión pendiente (docs/DECISIONES-PENDIENTES.md) |
| T13 | Watchdog de tiempo para plugins CLAP y aislamiento por proceso | P0 | hecho |
| T14 | Mix-minus por invitado | P0 | hecho, sin probar con un invitado real |
| T15 | Estadísticas de conexión por invitado y disparador `guest_quality` | P0 | hecho, sin probar con un invitado real |
| T16 | Base de legisladores (fuente única) | P0 | hecho |
| T17 | Bancas presenciales en la fila de oradores | P0 | hecho |
| T18 | Contenido de terceros al aire: zócalo validado, chat con aprobación, votación rotulada | P0 | hecho |
| T19 | Placa y alarma de pérdida de señal, con escena de respaldo | P0 | hecho |
| T20 | Interlock de PTZ: no mover la cámara al aire | P0 | hecho |
| T21 | Closed captions (CEA-608 en el stream y API de subtítulos de YouTube) | P0 | hecho, sin probar contra YouTube ni el 608 en un reproductor |
| T22 | Vocabulario propio para el STT (legisladores, localidades) | P0 | hecho |
| T23 | Interfaz para crear disparadores y notificación remota | P1 | hecho (Telegram y mail sin probar contra servidores reales) |
| T24 | Recuperación visible: cartel de causa, configuración con dos generaciones, volcados | P1 | hecho en parte (cartel con causa y tiempo, dos generaciones de Config/; faltan volcados con Crashpad en Mac y Linux) |
| T25 | Overlays globales (DSK) y recuadro fijo de intérprete LSA | P1 | hecho (sin componer en el preview ni miniaturas) |
| T26 | Transiciones profesionales: CUT/AUTO, por take, por escena, stinger, T-bar, AFV | P1 | parcial |
| T27 | Panel de salud de la salida | P1 | parcial |
| T28 | Salidas profesionales: SRT, clean feed y evaluación NDI/Decklink | P1 | falta |
| T29 | Grabación independiente del stream: encoder propio, multipista, replay, segunda copia | P1 | falta |
| T30 | Interfaz para la grabación ISO (entradas e invitados) | P1 | hecho |
| T31 | Herramientas de audio del operador: PFL, retardo, SincroLabios, latencia CLAP, PPM | P1 | parcial |
| T32 | Automixer para las bancas | P1 | decisión pendiente (docs/DECISIONES-PENDIENTES.md: los micrófonos tienen que pasar a GStreamer) |
| T33 | Sala: IFB en sala, cupo en caliente, prueba técnica con eco y red | P1 | parcial |
| T34 | Seguir al micrófono abierto | P1 | hecho (sobre los niveles; sin automixer) |
| T35 | Plugin oficial para el sistema de conferencia del recinto | P1 | falta |
| T36 | Modo "orden del día" y tiempos de uso de la palabra en el acta | P1 | hecho |
| T37 | Quórum y mayorías configurables, resultado oficial importado, acta con hash | P1 | hecho (sin plantilla nominal en pantalla: T43; el hash cubre el acta, no la grabación) |
| T38 | Multiview profesional: tally, costo por tarjeta, cuenta regresiva | P1 | hecho en parte (tally, vúmetro y cuenta regresiva por celda; falta el costo por nodo en el Patchbay) |
| T39 | Scopes y legalizador broadcast-safe | P1 | falta |
| T40 | Superresolución: alarma, bypass y entrenamiento desde cámaras locales | P1 | parcial |
| T41 | PTZ: presets con miniatura, grilla de bancas, velocidad | P1 | parcial |
| T42 | PTZ: VISCA por serie y joysticks vía plugin | P1 | parcial |
| T43 | Gráficos legislativos: plantillas, preview en PVW, datos vivos, HTML | P1 | parcial |
| T44 | Subtítulos con corrección humana y parciales | P1 | hecho en parte (cola de corrección en la app y por MCP; falta el puesto web /correccion/ y los parciales en streaming) |
| T45 | Archivo y rotulado: WebVTT, transcripción al acta, "Traducción automática" | P1 | hecho (la voz sintética todavía no prende el rótulo) |
| T46 | Tiempos en Dirección: cuenta regresiva del medio, playlist, back-timing | P1 | hecho en parte (contador del medio, back-timing; falta la playlist) |
| T47 | Credenciales y accesos: passphrase, PIN por sesión, enlaces que vencen, token MCP | P1 | parcial |
| T48 | Modo en vivo que protege la interfaz | P1 | hecho en parte (páginas con CONFIRMAR y ajustes que cortan bloqueados; falta el Loader con tiempo máximo para extensiones y los plugins aprobados de T60) |
| T49 | Atajos de teclado configurables y globales (con pedales de prompter) | P1 | hecho (globales en Mac y Windows; Linux solo locales; la pestaña está en Botonera) |
| T50 | Bus de eventos de la app (para superficies, plugins y web) | P1 | hecho (bus, MCP, plugins; la pestaña Eventos está en la página Botonera) |
| T51 | SDK de dispositivos para plugins: vendorear HID, MIDI, serie, OSC, Stream Deck | P1 | hecho en parte (bibliotecas vendoreadas, ejemplos de superficies y tally; falta la allowlist de herramientas por plugin) |
| T52 | Superficie de control: modelo de datos y motor de acciones y estado | P1 | hecho |
| T53 | Superficie de control: servidor en el gateway y puente con la app | P1 | hecho |
| T54 | Superficie de control: emparejamiento por QR y seguridad | P1 | hecho (sin rotación de token ni PIN local en la tablet) |
| T55 | Superficie de control: cliente web para tablet | P1 | hecho (sin el modo edición en la tablet) |
| T56 | Superficie de control: editor en el escritorio | P1 | hecho (editor por formulario y JSON, sin arrastrar celdas) |
| T57 | Superficie de control: herramientas MCP | P1 | hecho |
| T58 | Superficie de control: superficies de hardware vía plugin | P1 | hecho (probado con un aparato falso, sin hardware real) |
| T59 | Compatibilidad con Bitfocus Companion (Satellite API y API remota) | P2 | Satellite hecho (API 1.7, modo simple; probado con un cliente simulado); falta la API remota HTTP |
| T60 | Plugins aprobados para el aire y modelo local en Windows y Linux | P2 | falta |

**Ya hecho (no genera tarea)**
- **fps fraccionarios**: `src/app/ui/SettingsModel.cpp:44` ofrece 24000/1001, 30000/1001 y 60000/1001; se guardan en `src/app/AppSettings.cpp:166-171` y `:283`.
- **Matriz sin realimentación**: `haceLazo` en `src/app/audio/Matriz.cpp:308-316`, aplicado en `:365`.
- **Varias salidas físicas a la vez**: los buses virtuales van a dispositivos distintos (`src/app/audio/Matriz.hpp:14-15`, hilo propio en `Matriz.cpp:683`).
- **Plugins de Python en Windows**: la ruta del intérprete está en `src/app/plugins/Plugins.cpp:196-197`. Falta probarlos a mano.

**Camino crítico.**
- **T16 (legisladores)** desbloquea T17, T18, T34, T35, T41 y T43.
- **T50 (bus de eventos)** desbloquea T04, T49, T51 y toda la superficie de control (T52-T59).
- **T25 (DSK)** desbloquea T07 en su forma completa y el recuadro de LSA.
- **T02 (guardas de aire)** es requisito de seguridad de T52 (las superficies disparan herramientas).

---

# P0 — crítico al aire

## Supervivencia y automatismos

### T01 · Latido del supervisor fuera del hilo de la interfaz — P0
- **Estado: falta.**
  - El latido sale de un `QTimer` de 100 ms en el hilo de la UI (`src/app/ui/StudioViewModel.cpp:545-558`). Llama a `AppRuntime::tick`, que a su vez llama a `health_.beat` (`src/app/OperateMode.cpp:368-377`).
  - El umbral es de 5 s (`src/core/ipc/HealthBlock.hpp:219-225`). Si se pasa, el supervisor vuelca y mata (`src/launcher/Supervisor.cpp:327-335`, `:394`).
  - No hay hilo propio ni se mira el contador de cuadros codificados.
- **Qué hacer.**
  - Crear `src/app/LatidoAire.{hpp,cpp}`: un `std::thread` que cada 250 ms lee `obs_output_get_total_frames()` y `obs_output_active()` de la salida activa, y `video_output_get_total_frames(obs_get_video())`. Late en `HealthBlock` con dos campos nuevos:
    - `motorVivo`: cuadros que avanzan.
    - `uiViva`: lo marca el tick de la UI.
  - En `Supervisor.cpp`, la política nueva es:
    - si el motor avanza y la UI no: **no matar**. Escribir un aviso en el registro y en `Runtime/estado-ui.json`; si pasan 60 s (configurable), pedir confirmación en el puesto o matar cuando no hay transmisión ni grabación;
    - si el motor no avanza: matar como hoy.
  - Ampliar `HealthBlock` con versión de layout para no romper la compatibilidad entre `ba-launcher` y `ba-app`.
- **MCP.**
  - `status` y `system_info` suman `latido: {motor_vivo, ui_viva, ui_trabada_ms, politica}`.
  - Herramienta nueva `supervisor_politica` con `{espera_ui_s, matar_si_ui_trabada_sin_aire: bool}`, de alcance restart, bloqueada en vivo (ver T02).
- **GUI.** En la tarjeta Sistema (`src/app/qml/cards/HealthContent.qml`), un renglón "Interfaz: viva / trabada N s (el aire sigue)". En Configuración → Sistema, la espera máxima. Cuando la UI vuelve después de una traba, un cartel cuenta cuánto duró.
- **Depende de:** nada. Va primero.

### T02 · Guardas de aire para todo lo automático — P0
- **Estado: falta.**
  - La aprobación solo se pide con la política `Always`, o con `Untrusted` y la sesión contaminada (`src/app/mcp/McpCore.cpp:764-770`). No hay lista "de aire" ni se mira si hay transmisión.
  - La única guarda en vivo es `settings_set` para el asistente (`src/app/mcp/McpService.cpp:1907-1914`).
  - Los plugins llaman con rol vacío, así que ven todo (`src/app/GuiMode.cpp:169-173`, `McpCore.cpp:315-316`).
  - El asistente interactivo siempre recibe `Bash PowerShell` (`src/app/agent/AgentViewModel.cpp:1167`).
  - `agente_configurar consola=true` no mira `enVivo` (`src/app/mcp/McpAgenteTools.cpp:50-89`).
  - Las acciones automáticas de voz deciden sin mirar el aire (`src/app/agent/Voz.cpp:682-693`).
- **Qué hacer.**
  - En `McpCore`, crear `ClaseAire` por herramienta: `normal`, `aire` y `aire_si(args)`, este último para `settings_set` con alcance gateway o restart y para `ptz_*` sobre la cámara en PGM.
    - Es una tabla en `src/app/mcp/McpAire.{hpp,cpp}` con: `scene_switch`, `scene_take`, `stream_stop`, `stream_start` (si cambia destino), `record_stop`, `participant_kick`, `gateway_restart`, `app_quit`, `transition_set`, `speaker_on_air` (cuando saca a alguien) y `audio_set` mute del máster.
  - `McpCore::callTool` consulta un `EstadoAire` (inyectado por la app: transmitiendo, grabando, `EventoViewModel::enVivo()` en `src/app/ui/EventoViewModel.cpp:914-920`).
    - Si la herramienta es de aire y hay aire, **siempre** pide confirmación humana, sin importar política, rol ni contaminación.
    - Aplica a todos los orígenes de `CallContext`: asistente, plugin, receta, disparador, voz y superficie (T52).
    - La confirmación usa el mecanismo de aprobación que ya existe, con una marca visual "de aire".
  - En vivo:
    - el asistente interactivo se lanza sin `Bash`/`PowerShell`, y si ya estaba corriendo se relanza;
    - `agente_configurar consola=true` devuelve error;
    - la voz automática solo ejecuta herramientas `lectura` y una lista reversible (mute, volumen, `prompter_control`).
- **MCP.**
  - `mcp_aire_lista`: ver la tabla con la clase de cada herramienta.
  - `mcp_aire_ajustar`: `{herramienta, clase}`. Solo se permite fuera de vivo, y nunca para bajar de `aire` las de la lista fija.
  - El error de una herramienta bloqueada devuelve `{necesita_confirmacion: true, motivo: "de aire"}`.
- **GUI.**
  - Diálogo de aprobación con banda roja "ACCIÓN DE AIRE", tecla Enter para aprobar y Esc para rechazar.
  - En Asistente → Seguridad, la tabla de herramientas de aire (solo lectura en vivo).
  - En la barra del asistente, un candado "Bash apagado en vivo".
- **Depende de:** nada. Es requisito de T03, T52 y T57.

### T03 · Disparadores con rol restringido — P0
- **Estado: falta.** Las acciones de un disparador llaman `core_.handle(...)` sin rol, sesión ni aprobación (`src/app/mcp/McpAlertTools.cpp:27-38`). Los que despiertan a un LLM sí van a un rol (`src/app/agent/Despachador.cpp:107-128`).
- **Qué hacer.**
  - Crear el rol `disparador` en `src/app/agent/Roles.cpp`. Excluye `stream_stop`, `record_stop`, `app_quit`, `gateway_restart`, `participant_kick`, `settings_set` y `plugin_*`.
  - En `McpAlertTools`, construir `CallContext{"disparador:<id>", "disparador"}`.
  - Campo nuevo de la regla: `permitir_aire: [herramientas]`. Solo lo puede poner un humano desde la GUI, o el MCP fuera de vivo. Igual pasa por T02.
  - Cada ejecución queda en `alert_events` con resultado y herramienta.
- **MCP.** `alert_create` y `alert_update` aceptan `permitir_aire`. `alert_events` muestra `bloqueada_por_rol`.
- **GUI.** En el editor de disparadores (T23), una sección "Puede tocar el aire" con advertencia, y el historial con lo bloqueado en ámbar.
- **Depende de:** T02.

### T04 · Disparadores de fábrica activos, alarma sonora y tipos nuevos — P0
- **Estado: parcial.**
  - Los tipos están en `src/app/agent/AlertEngine.cpp:60-130`, incluido `disk` (`:126-129`, `:546-552`, solo grabando).
  - `AlertEngine::load` solo lee `alertas.json` (`:666-678`) y no siembra nada.
  - No hay sonido (ni `QSoundEffect` ni beep).
  - Faltan los tipos de entrada sin señal y de calidad del invitado.
- **Qué hacer.**
  - En el primer arranque (y con "restablecer"), sembrar en `Config/alertas.json` reglas marcadas `fabrica: true`:
    - silencio al aire más de 8 s (`silence` sobre la mezcla de programa);
    - saturación del máster;
    - `stream_health` caída o pérdida > 2 %;
    - `disk` < 30 min al bitrate actual, también **sin** grabar;
    - `input_signal` (nuevo);
    - `guest_drop`;
    - `guest_quality` (nuevo, T15).
  - Tipos nuevos en `AlertEngine`:
    - `input_signal`: sin cuadros nuevos en N ms, usando el diagnóstico de `video_estado` y `VideoGrafos`;
    - `guest_quality`;
    - `loudness` (fuera de objetivo durante N s, T11).
  - Crear `src/app/agent/AlarmaSonora.{hpp,cpp}` con `QSoundEffect` y sonidos en `assets/sonidos/`. Sale por el dispositivo de escucha del operador (el de `audio_escuchar`) y nunca por el programa. Tiene niveles: aviso (un tono) y crítico (repite hasta reconocer).
  - Cada regla tiene `sonido: ninguno|aviso|critico` y `reconocer: bool`.
  - Publicar cada disparo en el bus de eventos (T50).
- **MCP.**
  - `alert_types` suma `input_signal`, `guest_quality` y `loudness`.
  - `alert_create` acepta `sonido` y `reconocer`.
  - Herramientas nuevas: `alert_reconocer {id|todas}` y `alert_fabrica {restablecer: bool}`.
- **GUI.** En la tarjeta Sistema, una lista de alarmas activas con botón "Reconocer" (tecla `R` con foco y atajo global, T49). Una franja roja titilante arriba de `Main.qml` mientras haya una crítica. En Configuración → Alarmas, el volumen y el dispositivo.
- **Depende de:** T50 (publicar), T15 y T11 (tipos nuevos). La siembra se puede hacer ya.

### T05 · Revisión previa que diga la verdad, cue Pantalla y lista de chequeo de sesión — P0
- **Estado: parcial.**
  - El cue `Pantalla` se valida (`src/app/evento/Evento.cpp:103-106`) pero no se ejecuta: cae en "Todavía no sé…" (`src/app/ui/EventoViewModel.cpp:1353-1360`).
  - `ensayoAcelerado` (`:524-529`) informa mal Música, Teleprompter y Audio como no ejecutables, aunque sí se ejecutan (`:1145`, `:1172`, `:1262`).
  - La revisión (`Evento.cpp:286-337`, `EventoViewModel.cpp:850-895`) no marca cues no ejecutables ni revisa dispositivos, disco, destino ni credenciales.
- **Qué hacer.**
  - Implementar `case Pantalla` en `EventoViewModel::ejecutarCue` con `PantallasViewModel` (las mismas acciones que `pantalla_mostrar`).
  - Sacar la lista de ejecutables a una sola función `evento::ejecutable(TipoCue)`, usada por el ensayo, la revisión y la ejecución. Así no se vuelven a desincronizar.
  - En `revisar()`, sumar estas verificaciones:
    - dispositivos de audio y video presentes;
    - horas de disco al bitrate configurado;
    - destino de stream configurado y credencial válida (`account_check`);
    - entradas con señal;
    - invitados con prueba técnica hecha (T33);
    - pantallas vivas (`superficie_listar`).
  - Lista de chequeo de sesión, manual, guardada por evento en `Eventos/<id>/chequeo.json`:
    - PTZ probado con las cámaras reales;
    - casteo probado en los televisores del recinto;
    - ensayo de caída: matar `ba-app` al aire y cronometrar la vuelta, con el resultado de T24;
    - glosario revisado por la Legislatura;
    - máquina de respaldo lista con la escena básica;
    - auriculares para los remotos hasta que esté T14.
- **MCP.**
  - `evento_revisar` devuelve `{errores, avisos, chequeo}` con las verificaciones nuevas.
  - `evento_ensayo_rapido` usa la función única.
  - Herramientas nuevas: `evento_chequeo {evento, item, hecho, nota}` y `evento_chequeo_ver`.
- **GUI.** En la página Evento, una pestaña "Antes de salir" con la revisión automática en semáforo y la lista manual con casillas. Si quedan errores, el botón "Correr guion" pide confirmación.
- **Depende de:** T24 (medición de vuelta) para un ítem.

## Escenas

### T06 · "Al aire" del editor arma en vez de cortar; tecla T segura — P0
- **Estado: falta.**
  - El botón de `src/app/qml/pages/ScenesPage.qml:150` llama `Studio.takeScene(name)`, que corta directo (`src/app/ui/StudioViewModel.cpp:894`). Es deliberado según `StudioViewModel.hpp:331-333`.
  - `armarEscena` (`:923`) y `take` (`:936`) existen.
  - La tecla T se maneja en `src/app/qml/cards/ScenesContent.qml:72-75` y en `src/app/qml/cards/DireccionContent.qml:29-33`, sin chequeo explícito de foco de texto.
- **Qué hacer.**
  - En el editor, "Al aire" pasa a "Armar" (preview). El corte directo queda detrás de un ajuste `escenas.corte_directo_editor` (por defecto apagado), y con aire siempre pide confirmación.
  - Agregar `Studio.focoEnTexto()` (mira `activeFocusItem` por `TextInput`/`TextEdit`) y usarlo en los dos handlers.
  - Mover TAKE a una acción central de `Acciones.qml`, que después reusan T49 y T52.
- **MCP.** `scene_switch` sigue cortando, pero cae en T02. Herramienta nueva `escenas_ajustes {corte_directo_editor}`.
- **GUI.** El botón del editor se llama "Armar" y es verde. Con aire, un corte directo abre `ConfirmDialog` con el nombre de la escena.
- **Depende de:** T02 para la parte MCP.

### T07 · FTB / placa de seguridad a un toque — P0
- **Estado: falta.** No hay FTB ni placa (lo más cercano es el tipo de transición `fade_to_color`, `src/app/BroadcastEngineScenes.cpp:41-47`). "Cuarto intermedio" aparece solo como texto de ejemplo de traducción.
- **Qué hacer.**
  - En `BroadcastEngineScenes`, crear una escena interna protegida `__placa__`, que no depende de que el usuario la cree. Lleva imagen configurable más texto ("Cuarto intermedio", "Volvemos enseguida", "Minuto de silencio"). La imagen de fábrica está en `assets/placas/`.
  - Funciones `placaSeguridad(on, texto, fundido_ms)` y `ftb(on, ms)`. FTB es un fundido a negro del canal de salida, y el audio baja con él salvo que se elija "mantener audio".
  - Estado persistido en `Runtime/al-aire.json` para que sobreviva a una caída.
  - Cuando exista T25, la placa es un DSK de máxima prioridad.
- **MCP.** Herramientas nuevas:
  - `aire_ftb {activar: bool, ms?: int, audio?: "bajar"|"mantener"}`;
  - `aire_placa {activar: bool, texto?, imagen?, ms?}`;
  - `aire_placas_listar`.
  Desactivar no es de aire; activar sí (T02).
- **GUI.** En la tarjeta Dirección y en Escenas, dos botones grandes "FTB" y "PLACA", con el texto elegido en un desplegable. Estado titilante en la barra superior mientras estén activos. Atajo de fábrica configurable (T49).
- **Depende de:** nada. Mejora con T25.

## Salida y grabación

### T08 · `stream_start` con otro destino estando al aire: error explícito — P0
- **Estado: falta.**
  - El motor devuelve `true` si ya está activa (`src/app/BroadcastEngine.cpp:921-923`).
  - El MCP responde "Ya esta transmitiendo." como éxito (`src/app/mcp/McpService.cpp:1749`).
  - `StudioViewModel::startStreaming` (`:880-882`) muestra el aviso de "Al aire".
- **Qué hacer.**
  - `startStreaming` devuelve un `enum ResultadoInicio {Iniciado, YaIgual, YaOtroDestino, Error}` y compara URL y clave contra la salida activa.
  - Con `YaOtroDestino`: error, salvo `cambiar_en_caliente: true`, que para y arranca esa salida. Con T09, solo esa salida, sin tocar las demás.
- **MCP.** `stream_start` devuelve `isError` con `{activo_en: "<destino enmascarado>"}`. Parámetro nuevo `cambiar_en_caliente` (de aire).
- **GUI.** Aviso rojo "Ya estás transmitiendo a X. ¿Cambiar a Y?" con confirmación. Nunca un aviso verde si no cambió nada.
- **Depende de:** nada.

### T09 · Multidestino — P0
- **Estado: falta.** Hay un solo `obs_output* streamOutput_` (`src/app/BroadcastEngine.hpp:697`), creado como `rtmp_custom` + `rtmp_output` (`BroadcastEngine.cpp:935-963`).
- **Qué hacer.**
  - Reemplazarlo por `std::vector<SalidaStream>`, con `{id, nombre, servicio, url, clave_cred, obs_output*, estado, reconexiones, kbps}`. Todas comparten `videoEncoder_`/`audioEncoder_`, con una opción por salida de encoder propio cuando cambia la resolución o el bitrate.
  - Reconexión y señales por salida. `Runtime/al-aire.json` guarda la lista activa para retomar.
  - Credenciales por destino en `CredentialStore` (`stream-<id>`).
  - Las integraciones de YouTube, Twitch y Facebook crean su destino automáticamente.
  - Destinos SRT de T28.
- **MCP.** Herramientas nuevas:
  - `stream_destinos` (listar con estado);
  - `stream_destino_agregar {nombre, tipo: rtmp|srt|youtube|twitch|facebook, url, clave?, respaldo?: bool}`;
  - `stream_destino_editar`, `stream_destino_borrar`.
  - `stream_start {destino?: id|"todos"}` y `stream_stop {destino?}` (de aire).
- **GUI.** En la tarjeta Salidas (`src/app/qml/cards/OutputsContent.qml`), una fila por destino con LED, kbps, botón propio y reconexiones. Arriba, "Iniciar todos". En Configuración → Salida, el ABM de destinos con el formulario de credenciales que el modelo no ve.
- **Depende de:** T08. Habilita T27 y T28.

### T10 · Grabación oficial segura: MKV o remux y chequeo de disco — P0
- **Estado: parcial.**
  - Hay ajuste de contenedor `record.container` con MKV por defecto (`src/app/ui/SettingsModel.cpp:61`, `AppSettings.cpp:194`). Se escribe con `ffmpeg_muxer` (`BroadcastEngine.cpp:994-1003`).
  - No se fuerza MKV ni hay remux al terminar (`stopRecording`, `:1019-1029`).
  - No hay chequeo de disco al arrancar (`StudioViewModel.cpp:869-878`). El disparador `disk` existe, pero no se crea solo (`AlertEngine.cpp:666-678`).
- **Qué hacer.**
  - Si el usuario elige MP4 o MOV, se graba en MKV (o MP4 fragmentado vía `movflags=frag_keyframe`) y al cerrar se hace un remux en un hilo aparte con `ffmpeg` del bundle o `obs_remux`. El MKV queda hasta que el remux se verifica.
  - Retomar después de una caída une los pedazos en un manifiesto `sesion-<ts>.txt`.
  - `chequeoDisco()` antes de grabar: horas restantes = libre / (bitrate total). Con menos de 2 h, aviso; con menos de 15 min, bloqueo con confirmación.
- **MCP.**
  - `record_start` devuelve `{horas_restantes, contenedor_real}`.
  - Herramientas nuevas: `record_disco` (libre y horas), `record_remux {archivo}` y `record_ajustes {contenedor, remux_al_terminar, umbral_horas}`.
- **GUI.** En la tarjeta Salidas, el renglón "Disco: 4 h 12 min a este bitrate", en ámbar o rojo según corresponda. El progreso del remux al terminar. En Configuración → Grabación, la casilla "Remux a MP4 al terminar".
- **Depende de:** T04 para el disparador de fábrica.

## Audio

### T11 · Medidor de loudness del programa — P0
- **Estado: parcial.**
  - El analizador BS.1770-4 existe en `src/app/audio/Analyzer.cpp` (K-weighting en `:74`, M/S/I en `:247-273`), pero como módulo de rack por fuente (`AudioRack.hpp:75,151`). No está enganchado a la mezcla 0.
  - No hay true peak ni LRA (`Analyzer.hpp:35-36`).
  - La tarjeta Mezclador solo muestra el pico de muestra (`src/app/qml/cards/MixerContent.qml:69-70`, alimentado en `BroadcastEngine.cpp:700-713`).
- **Qué hacer.**
  - Crear `src/app/audio/LoudnessPrograma.{hpp,cpp}`, que se cuelga del callback de la mezcla 0 que ya existe (`BroadcastEngine.cpp:504`). Hace el cálculo en un hilo propio con una cola sin bloqueo.
  - Agregar a `Analyzer`:
    - true peak con sobremuestreo ×4 (FIR polifásico de 48 taps);
    - LRA (percentiles 10-95 del corto con compuerta relativa de -20 LU).
  - El integrado se resetea al arrancar la grabación o el stream, o a mano.
  - El objetivo es configurable: -23 LUFS / -1 dBTP (R128), -24 LKFS (A/85), -16 o -14.
  - Publicar en el bus (T50) y alimentar el disparador `loudness` (T04).
- **MCP.** Herramientas nuevas:
  - `audio_loudness` devuelve `{m, s, i, tp, lra, objetivo, desde}`;
  - `audio_loudness_reset`;
  - `audio_loudness_objetivo {perfil: r128|a85|plataforma16|plataforma14|custom, lufs?, tp?}`.
  `audio_measure` lo menciona.
- **GUI.** En la tarjeta Mezclador, una columna fija con barras M y S, número grande del integrado, TP con indicador rojo arriba del objetivo, LRA, el objetivo y el botón Reset. Versión compacta en la barra superior de `Main.qml`.
- **Depende de:** nada.

### T12 · Limitador de seguridad en el máster — P0
- **Estado: falta.**
  - Los racks son por fuente (`src/app/audio/RackManager.hpp:1-18`) y no hay rack de programa.
  - El `ChannelStrip` del núcleo tiene un limitador de -1 dBFS (`src/core/dsp/Presets.hpp:60`), pero solo lo usa `OperatorEngine` en el autotest (`src/app/main.cpp:49`).
  - `guests.audioPreset` se guarda y nadie lo lee (`AppSettings.cpp:205`).
- **Qué hacer.**
  - Insertar un limitador true-peak (lookahead de 1,5 ms, techo -1 dBTP) en la salida de la mezcla de programa antes de los encoders. Como libobs no tiene filtro de máster, se hace con `obs_set_audio_output_filter` si existe en el libobs del bundle; si no, con una fuente de máster interna por la que pasan las pistas 1-6.
  - Se reusa `core/dsp` y no se usa un plugin CLAP, para que nunca se cuelgue.
  - Está fuera de la edición normal: solo se apaga con confirmación y nunca en vivo.
  - Publicar la reducción de ganancia.
- **MCP.**
  - `audio_master {techo_dbtp?, activo?}`; apagarlo es de aire.
  - `audio_loudness` suma `gr_db`.
- **GUI.** En el Mezclador, junto al loudness, un indicador "LIM" con la reducción de ganancia en dB y un candado. La edición está en Configuración → Audio → Máster.
- **Depende de:** nada.

### T13 · Watchdog de tiempo para plugins CLAP y aislamiento por proceso — P0
- **Estado: falta.**
  - `node.clap->process()` se llama directo en el hilo de audio de OBS (`src/app/audio/AudioRack.cpp:985-998`), sin tiempo máximo ni aislamiento.
  - Hay bypass manual (`:752`), pero aun en bypass el plugin procesa.
- **Qué hacer.**
  - **Fase 1 (chica, P0).**
    - Medir el tiempo de `process()` por nodo con `steady_clock`. Si pasa de X ms (por defecto el 50 % del bloque) en N bloques seguidos, el rack pone el nodo en **bypass real** (sin llamar `process`) y avisa.
    - Si un `process()` no vuelve en 200 ms, un hilo vigía marca el rack como colgado; el callback siguiente de OBS no entra al rack y deja pasar la señal seca. No se puede abortar el hilo, pero el aire sigue.
    - Corregir el bypass para que no procese.
    - En vivo, `audio_module_add` de plugins que no son de fábrica pide confirmación (T02).
  - **Fase 2 (grande, P1).** Crear `ba-clap-host`, un proceso aparte por rack con memoria compartida (`core/ipc` ya tiene anillos) y un bloque de latencia. Si el host muere, bypass y reinicio. Así trabajan Bitwig y Reaper con el "plugin bridging".
- **MCP.**
  - `audio_rack` devuelve por nodo `{ms_p50, ms_max, bypass_auto}`.
  - Herramientas nuevas: `audio_rack_vigia {umbral_pct, bloques}` y `audio_module_rearmar {nodo}` (sacar del bypass automático).
- **GUI.** En la página Audio, cada módulo del rack muestra los ms por bloque y una etiqueta roja "Saltado por lento" con botón "Rearmar". También aviso en la tarjeta Sistema.
- **Depende de:** nada.

## Sala de invitados

### T14 · Mix-minus por invitado — P0
- **Estado: falta.**
  - `obs_add_raw_audio_callback(0, …)` toma la mezcla completa (`src/app/BroadcastEngine.cpp:504`). `onRawAudio` (`:715-745`) escribe un solo anillo, `BA.Audio.ContentForReturn` (`:138`).
  - El gateway manda el mismo anillo a todos (`src/gateway/MediaPump.hpp:19`).
- **Qué hacer.**
  - Recomiendo la forma limpia, en la app (A): una mezcla por slot de invitado activo, que es el programa menos `BA.Audio.Guest.n`. Como todas las fuentes ya pasan por A, se arma en `onRawAudio` restando la fuente del invitado con el mismo reloj, antes del limitador.
    - Implementación: tomar la contribución de cada fuente de invitado del mezclador de libobs (con `obs_source_add_audio_capture_callback` sobre la fuente del invitado, ya con volumen y mute aplicados). Así `retorno_n = programa - invitado_n` sale alineado al cuadro de audio.
    - Escribir N anillos `BA.Audio.Return.<n>` y que `MediaPump` lea el de cada slot.
  - Alternativa por si la resta queda imperfecta (la fuente tiene filtros): un bus de la Matriz "retorno-n" con todas las fuentes menos la n. La Matriz ya sabe armar buses (`src/app/audio/Matriz.cpp`), pero suma unos 20-30 ms.
  - Por invitado se puede elegir: `mix_minus` (fábrica), `programa` o `solo_operador`.
- **MCP.**
  - `participant_profile` y `room_set` suman `retorno: mix_minus|programa|solo_operador`.
  - `room_status` muestra el tipo de retorno de cada invitado.
- **GUI.** En la tarjeta Invitados (`src/app/qml/cards/GuestsContent.qml`), un selector "Oye:" por invitado (por defecto "Programa sin su voz") y un medidor del retorno. La ayuda de la guía del invitado se actualiza.
- **Depende de:** nada. Ayuda a T33 (IFB).

### T15 · Estadísticas de conexión por invitado y disparador `guest_quality` — P0
- **Estado: falta.** No hay `GetStats` en `src/gateway/Webrtc.cpp` ni en `Room.cpp`. El disparador más cercano es `guest_drop` (`src/app/agent/AlertEngine.cpp:93`).
- **Qué hacer.**
  - En `Webrtc.cpp`, llamar cada 2 s a `PeerConnection::GetStats` y extraer bitrate de entrada y salida, `packetsLost`, `jitter`, `roundTripTime`, `framesDropped` y la resolución recibida.
  - Guardar el resultado en `Room` por participante y exponerlo en la API de admin (`/api/v1/room` y por SSE).
  - En A, la clase `CalidadRed`: semáforo (verde, ámbar o rojo con umbrales configurables) y alimenta `guest_quality` (T04).
  - Acción "bajar calidad": pedirle al navegador del invitado que use 720p o 360p, o poner un techo de bitrate, vía `RTCRtpSender.setParameters` por mensaje del canal.
- **MCP.**
  - `room_status` y `participants` suman `red: {kbps_in, kbps_out, perdida_pct, jitter_ms, rtt_ms, semaforo}`.
  - Herramienta nueva `participant_calidad {id, maximo: "1080p"|"720p"|"360p", kbps?}`.
- **GUI.** En la tarjeta del invitado, un semáforo con tooltip de cifras y un menú "Bajar a 720p / 360p". Cuando un invitado se pone rojo al aire, aviso en la tarjeta Sistema.
- **Depende de:** nada. Alimenta T04.

## Legislatura: oradores y contenido institucional

### T16 · Base de legisladores — P0
- **Estado: falta.** No hay tabla de legisladores. Existen el padrón y Personas (`src/app/ui/PersonasViewModel.cpp:653`) y las posiciones PTZ (`src/app/ptz/Posiciones.hpp:25-41`), pero no están vinculados.
- **Qué hacer.**
  - Ampliar la libreta de Personas en vez de duplicarla: `Persona` suma `{bloque, distrito, cargo_oficial, banca, preset_ptz: {camara, posicion}, microfono: {fuente_audio|canal_conferencia}, foto, rostro_id, validado_por, validado_en}`.
  - Una colección "Cuerpo" (por ejemplo "Legislatura del Chubut 2025-2029") agrupa personas con banca.
  - Importar desde CSV o XLSX; la planilla de fábrica está en `assets/plantillas/legisladores.csv`.
  - Esta es la fuente única para zócalos (T18, T43), padrón, rostros, bancas (T17), seguir al micrófono (T34) y el sistema de conferencia (T35).
- **MCP.** Herramientas nuevas:
  - `cuerpo_listar`, `cuerpo_crear {nombre}`;
  - `cuerpo_importar {archivo, columnas?}`;
  - `persona_editar {id, bloque?, banca?, preset_ptz?, microfono?, cargo_oficial?}`;
  - `persona_ver {id|banca}`.
  Mantener compatibilidad con `padron_*`.
- **GUI.** En la página Personas (`src/app/qml/pages/PersonasPage.qml`), una pestaña "Cuerpo" con la tabla editable (banca, nombre, bloque, cargo, cámara/preset con selector y miniatura de T41, micrófono). Botón "Importar planilla". Las filas no validadas se marcan.
- **Depende de:** nada. Desbloquea T17, T18, T34, T35, T41 y T43.

### T17 · Bancas presenciales en la fila de oradores — P0
- **Estado: parcial.**
  - La fila y `speaker_on_air` existen (`src/app/mcp/McpRoomTools.cpp:678-846`, `src/gateway/Room.cpp:786-838`), pero solo para participantes WebRTC.
  - El tiempo usado se lleva en `Room.cpp:837`.
- **Qué hacer.**
  - Las entradas de la fila pasan a tener `tipo: remoto|banca`. Una banca no tiene sesión WebRTC: Room la trata como participante virtual con `personaId`.
  - `speaker_on_air` sobre una banca ejecuta, en una sola acción reversible:
    1. preset PTZ con el interlock de T20: si la cámara del preset está al aire, primero se usa otra cámara o el plano general;
    2. armar la escena de la cámara en preview, o corte según el modo;
    3. vincular el zócalo con los datos de la persona;
    4. arrancar el reloj;
    5. abrir el micrófono de la banca si está mapeado.
  - Modo "propuesta": arma todo en PVW y el operador toma con TAKE.
- **MCP.**
  - `speaker_queue {agregar: {banca|persona_id}}`.
  - `speaker_on_air {id, modo: "armar"|"directo"}`.
  - `speakers_status` distingue `tipo`.
  - Herramienta nueva `bancas_listar`.
- **GUI.** En la tarjeta Oradores (`src/app/qml/cards/SpeakersContent.qml`), una pestaña "Bancas" con la grilla del recinto (la misma de T41): tocar una banca la suma a la fila, y doble toque la saca al aire. `SpeakerCard.qml` muestra el ícono de banca o de remoto.
- **Depende de:** T16 y T20. Mejora con T41.

### T18 · Contenido de terceros al aire: zócalo validado, chat con aprobación, votación rotulada — P0
- **Estado: falta.**
  - El `info` del invitado va directo a `cargo` (`src/app/ui/StudioViewModel.cpp:824-828`, en `contextoDeVinculos`, `:812`), y el contexto no ofrece Personas ni padrón.
  - El overlay de chat solo tiene llaves por plataforma, sin aprobación (`src/app/ChatOverlaySource.hpp`).
  - La votación distingue "formal" de "encuesta" (`src/app/ui/EventoViewModel.cpp:724-728`), pero no dice "consulta / no vinculante".
- **Qué hacer.**
  - **Zócalo.** `contextoDeVinculos` toma nombre y cargo de la Persona vinculada (T16) cuando existe. El `info` del invitado queda como `sugerencia` y solo pasa al zócalo si el operador lo aprueba; eso queda grabado en la Persona con `validado_por`.
  - **Chat.** `ChatOverlaySource` pasa a modo "destacado" por defecto: solo muestra mensajes que el operador marcó. El modo "flujo automático" queda como opción y con aire pide confirmación.
  - **Votación.** El rótulo "Consulta – no vinculante" aparece en la página pública, en la placa de resultado y en el acta, salvo un ajuste `votacion.vinculante_adoptada` (con texto de quién la adoptó).
- **MCP.**
  - `rotulo_vincular` acepta `fuente: persona|sala|guion`.
  - Herramienta nueva `invitado_info_aprobar {id, texto?}`.
  - `chat_overlay_add {modo: destacado|flujo}`.
  - Herramientas nuevas: `chat_destacar {plataforma, mensaje_id}`, `chat_quitar_destacado`.
  - `votacion_abrir` devuelve `vinculante: false`.
- **GUI.**
  - En Rótulos (`RotulosPage.qml`), un ícono "sin validar" en el zócalo, con "Aprobar texto".
  - En YouTube, Twitch y Facebook, una estrella "Destacar" en cada mensaje y "Sumar a preguntas" (T37 la reusa).
  - En el panel de votación, la leyenda fija.
- **Depende de:** T16.

## Video y PTZ

### T19 · Placa y alarma de pérdida de señal, con escena de respaldo — P0
- **Estado: parcial.**
  - "Sin señal" solo aparece en el panel de sincronía (`src/app/qml/controls/PanelSincronia.qml:169`) y como texto fijo en PTZ y Monitor.
  - NDI pone negro (`src/app/NdiSource.cpp:284`).
  - El multiview (`src/app/qml/controls/VideoWall.qml:62-74`) no avisa, y no hay placa ni escena de respaldo.
- **Qué hacer.**
  - En `ba_gst_fuente.cpp` y `VideoGrafos`, crear el estado `senal: ok|tarde|perdida`: `perdida` si no llega cuadro en 500 ms, configurable.
  - Por entrada:
    - `al_perder: congelar|negro|placa|respaldo`;
    - `placa`: imagen configurable, por defecto el último cuadro con cartel "SIN SEÑAL", solo en multiview y nunca al aire salvo que se elija;
    - `escena_respaldo`: si la entrada está en PGM y se pierde más de N ms, armar o cortar a esa escena según `al_perder_aire: avisar|armar|cortar`.
  - Disparador `input_signal` de fábrica (T04).
- **MCP.**
  - `video_estado` y `input_status` suman `senal` y `perdida_desde_ms`.
  - `video_entrada_red` y `entrada_control` aceptan `al_perder`, `placa`, `escena_respaldo` y `al_perder_aire`.
- **GUI.**
  - Borde rojo titilante y "SIN SEÑAL" en la celda del multiview (T38).
  - Lista en la tarjeta Sistema.
  - En la tarjeta de la entrada (Entradas o Video), una sección "Si se cae la señal" con el selector y la escena de respaldo.
- **Depende de:** T04. Va junto con T38.

### T20 · Interlock de PTZ: no mover la cámara al aire — P0
- **Estado: falta.** No hay chequeo de aire ni de tally en `src/app/ptz/*.cpp` ni en `src/app/mcp/McpPtzTools.cpp`. `publicarTally()` ya sabe qué fuentes están en PGM (`src/app/ui/StudioViewModel.cpp:908-920`).
- **Qué hacer.**
  - En `ControlCamaras`, la función `alAire(camara)` mapea cámara → fuentes → `fuentesAire`.
  - Todo movimiento (`irAPosicion`, `mover`, `apuntar`, `seguir`, `encuadre`) recibe `Origen {humano, asistente, disparador, plugin, superficie}`. Si la cámara está al aire:
    - humano: confirmación;
    - el resto: error `camara_al_aire`;
    - excepción: el `seguir` con modo `suave` marcado explícitamente.
  - Con `forzar: true` igual pasa por T02.
- **MCP.**
  - Todas las `ptz_*` de movimiento aceptan `forzar`, y el error es explícito.
  - `ptz_camaras` suma `al_aire`.
  - Herramienta nueva `ptz_interlock {activo, seguir_suave_permitido}`.
- **GUI.** En la página PTZ y en la tarjeta PTZ (`PtzContent.qml`), un borde rojo "AL AIRE" en la cámara y los presets en gris con candado. Tocar uno igual abre "La cámara está al aire. ¿Mover igual?".
- **Depende de:** nada. Lo usa T17.

## Accesibilidad

### T21 · Closed captions en el stream — P0
- **Estado: falta.** No hay `obs_output_output_caption_text2` ni subtítulos en `src/app/youtube`. Solo existen subtítulos quemados (`src/obs-module/ba_subtitulos.cpp`, `src/app/agent/Subtitulos.hpp:28`).
- **Qué hacer.**
  - Crear `src/app/agent/SubtitulosCC.{hpp,cpp}`, que se suscribe a las frases de `Subtitulos` (o de T44 si hay corrección humana).
  - Canal 1, CEA-608, con `obs_output_output_caption_text2(output, texto, duracion)` sobre cada salida RTMP H.264 (T09). Verificar que el libobs del bundle lo tiene y que x264 o VideoToolbox insertan SEI.
  - Alternativa y respaldo: la API de subtítulos HTTP de YouTube (POST con secuencia y marca de tiempo a la URL de ingesta de subtítulos del evento). Se configura en el destino YouTube.
  - Los quemados quedan como opción independiente. Guardar `.srt` y `.vtt` (T45).
- **MCP.** Herramientas nuevas:
  - `cc_estado`;
  - `cc_activar {destinos?: [id], metodo: "608"|"youtube_http"|"ambos", idioma}`;
  - `cc_desactivar`;
  - `cc_probar {texto}`.
  `subtitulos_limpiar` limpia también el CC.
- **GUI.** En el panel de Traducción y Subtítulos (`PanelTraduccion.qml`) o en la página Audio → Voz, una sección "Subtítulos ocultos (CC)" con interruptor por destino, método y una vista de las últimas líneas enviadas. Un indicador "CC" en la tarjeta Salidas.
- **Depende de:** T09 para varios destinos. Mejora con T22 y T44.

### T22 · Vocabulario propio para el STT — P0
- **Estado: falta.** No hay hotwords, vocabulario ni diccionario en el camino del STT (`src/app/audio/Escucha.hpp`, `src/app/agent`). El glosario existe solo para la traducción (`src/app/agent/Traductor.hpp:19-21`, `Datos/Config/glosario-traduccion.json`).
- **Qué hacer.**
  - Crear `src/app/agent/Vocabulario.{hpp,cpp}`, con fuentes:
    - nombres y apellidos del Cuerpo (T16);
    - localidades del Chubut (lista de fábrica en `assets/vocabulario/chubut.txt`);
    - términos legislativos;
    - términos del operador.
  - Si el modelo de sherpa-onnx admite `hotwords_file` (transducers), pasarlo con un `hotwords_score` configurable.
  - Para Parakeet o CTC sin hotwords: corrección posterior por diccionario, con distancia fonética (Metaphone en español o una distancia de edición ponderada). Solo se reemplaza con confianza alta, y se registra cada reemplazo.
  - Reusar el formato del glosario de traducción.
- **MCP.** Herramientas nuevas:
  - `voz_vocabulario` (ver);
  - `voz_vocabulario_agregar {termino, variantes?: [], grupo}`;
  - `voz_vocabulario_quitar`;
  - `voz_vocabulario_probar {texto}`, que devuelve la corrección propuesta.
- **GUI.** En la página Audio → Voz (o en la tarjeta de Transcripción), una pestaña "Vocabulario" con grupos activables, una tabla, "Importar del Cuerpo" y un probador. Los reemplazos aparecen subrayados en la transcripción en vivo.
- **Depende de:** T16, para importar los nombres.

---

# P1 — importante

## Monitoreo y recuperación

### T23 · Interfaz para crear disparadores y notificación remota — P1
- **Estado: falta.**
  - `src/app/qml/pages/AssistantPage.qml:336-380` solo lista y borra; crear exige pedírselo al asistente.
  - No hay webhook, Telegram, Slack ni SMTP.
- **Qué hacer.**
  - Editor de reglas en QML, generado desde `alert_types` (cada tipo declara sus parámetros, como el formulario de propiedades de `FormularioPropiedades.qml`).
  - Las acciones se eligen de las recetas o de herramientas, con el editor de argumentos de T56, que se reusa.
  - Notificadores en `src/app/agent/Notificador.{hpp,cpp}`:
    - webhook genérico (POST JSON);
    - Telegram (bot token y chat id);
    - mail SMTP con TLS (mbedtls ya está en el gateway).
    Los secretos van en `CredentialStore`.
  - Cada regla tiene `notificar: [notificador]`.
- **MCP.** Herramientas nuevas: `notificador_listar`, `notificador_agregar {tipo, nombre, …}` (con formulario de credenciales), `notificador_probar {id}`. `alert_create` acepta `notificar`.
- **GUI.** Una página o pestaña "Disparadores" con lista, "+ Nuevo", un editor con condición, acciones, sonido, notificar y "puede tocar el aire" (T03), y la vista del historial. En Configuración → Notificaciones, el ABM y "Probar".
- **Depende de:** T03 y T04.

### T24 · Recuperación visible: cartel de causa, configuración con dos generaciones, volcados — P1
- **Estado: parcial.**
  - Hay retoma de stream y grabación (`Runtime/al-aire.json`) y un cartel de reconexión del stream (`src/app/ui/StudioViewModel.cpp:612`, `src/app/qml/Main.qml:402-406`).
  - No se muestra la causa de la caída anterior; solo `src/app/Registrar.cpp:90` lee `Caidas`.
  - `SnapshotStore` solo se usa en el autotest (`src/app/main.cpp:48-50`) y la configuración es JSON plano (`src/app/AppSettings.cpp:31-54`).
  - Hay volcados solo en Windows (`src/launcher/Supervisor.cpp:5-6`, `:77-119`).
- **Qué hacer.**
  - El supervisor escribe en `Runtime/ultima-caida.json` `{causa, hora, tiempo_hasta_aire_ms}`. La app la muestra al volver y mide hasta el primer cuadro saliente.
  - `AppSettings::save/load` pasa a `SnapshotStore`, con dos generaciones y checksum: si `app.json` está corrupto, se carga la generación anterior y se avisa.
  - Volcados en macOS y Linux con Crashpad (fase 9), vendoreado.
- **MCP.**
  - `system_info` suma `ultima_caida`.
  - Herramientas nuevas: `supervisor_caidas {n}` y `config_generaciones` (listar y restaurar, fuera de vivo).
- **GUI.** Al volver de una caída, un cartel grande en `Main.qml`: "Reconectando: la app se reinició por <causa>. Aire de vuelta en 6,2 s". En la página Registro, la lista de caídas con volcado adjunto.
- **Depende de:** T01.

## Escenas

### T25 · Overlays globales (DSK) y recuadro fijo de intérprete LSA — P1
- **Estado: falta.** No hay DSK. La salida es una transición en el canal 0 (`src/app/BroadcastEngineScenes.cpp:167`).
- **Qué hacer.**
  - Usar los canales de salida superiores de libobs (1..4) con escenas internas `__dsk1__`..`__dsk4__`, cada una con su visibilidad y su transición (fundido en ms).
  - Se componen encima del programa y también del preview, de forma opcional.
  - El clean feed (T28) se arma sin los DSK.
  - El DSK "LSA" está marcado `fijo`: no se baja con FTB ni con cambios de escena, solo con confirmación.
  - La placa de T07 es el DSK de mayor prioridad.
- **MCP.** Herramientas nuevas:
  - `dsk_listar`;
  - `dsk_definir {capa: 1-4, nombre, escena|fuentes, fijo?, transicion_ms?}`;
  - `dsk_mostrar {capa, visible}` (bajar un DSK fijo es de aire).
- **GUI.** En la tarjeta Dirección y en Escenas, una fila de 4 botones DSK con miniatura, iluminados cuando están arriba. En Escenas, un editor para asignar el contenido de cada capa. La plantilla "Intérprete LSA" pone la cámara en un recuadro inferior derecho.
- **Depende de:** nada. Lo usan T07, T28 y T43.

### T26 · Transiciones profesionales: CUT/AUTO, por take, por escena, stinger, T-bar, AFV — P1
- **Estado: parcial.**
  - Hay una transición global (`src/app/BroadcastEngineScenes.cpp:150-176`) con los tipos cut, fade, swipe, slide y fade_to_color (`:41-47`, enum MCP en `src/app/mcp/McpService.cpp:1235`).
  - `scene_take` no recibe parámetros (`McpService.cpp:920/930`).
  - No hay stinger, T-bar ni audio follow video.
- **Qué hacer.**
  - `take(transicion?, ms?)` y `cut()` separados.
  - Cada escena tiene `transicion_entrada` como override.
  - Stinger con `obs_stinger_transition`: verificar que `obs-transitions` está en el bundle; si no, agregarlo en la compilación de libobs. Parámetros: video con alfa y punto de corte.
  - T-bar con `obs_transition_set_manual_time(t)` y `obs_transition_set_manual_torque`.
  - AFV: por fuente, `sigue_video: bool`. Al entrar o salir de PGM se hace un fundido de volumen con la transición.
- **MCP.**
  - `scene_take {transicion?, ms?}`.
  - Herramienta nueva `scene_cut`.
  - `scene_define {transicion_entrada?}`.
  - `transition_set` suma `stinger` con `{archivo, punto_ms}`.
  - Herramienta nueva `transicion_manual {t: 0..1, soltar?: bool}`, que usa el fader de la superficie (T52).
  - `audio_set {sigue_video}`.
- **GUI.** En la tarjeta Dirección, botones CUT y AUTO separados, el selector de transición y su duración, y una T-bar vertical arrastrable. En la escena, un menú "Transición de entrada". En el Mezclador, un botón "AFV" por canal.
- **Depende de:** nada.

## Salida y grabación

### T27 · Panel de salud de la salida — P1
- **Estado: parcial.**
  - La tarjeta Salidas muestra solo `streamKbps` (`src/app/qml/cards/OutputsContent.qml:50`).
  - Los cuadros perdidos están en la tarjeta Sistema (`src/app/qml/cards/HealthContent.qml:54-55`).
  - `youtube_live_health` es solo MCP (`src/app/mcp/McpYouTubeTools.cpp:419`, con datos de `YouTubeClient.cpp:786`).
- **Qué hacer.** Por destino (T09) se muestran:
  - kbps reales;
  - cuadros perdidos por encoder (`obs_output_get_frames_dropped`) y por red;
  - congestión (`obs_output_get_congestion`);
  - reconexiones y tiempo de reconexión.
  Además, la salud de ingesta de YouTube y Twitch consultada cada 30 s y un historial de 10 minutos.
- **MCP.** `stream_destinos` incluye `salud: {kbps, perdidos_enc, perdidos_red, congestion, reconexiones, plataforma: {estado, problemas[]}}`. Herramienta nueva `stream_salud {destino, minutos}` con la serie.
- **GUI.** En la tarjeta Salidas, por destino, una mini gráfica de kbps, contadores y el semáforo de la plataforma con su texto ("Bitrate bajo", "Resolución no recomendada").
- **Depende de:** T09.

### T28 · Salidas profesionales: SRT, clean feed y evaluación NDI/Decklink — P1
- **Estado: falta.**
  - Solo existe `rtmp_output` (`src/app/BroadcastEngine.cpp:948`).
  - SRT es solo de entrada (`src/app/InputSpecs.cpp:19`).
  - NDI es solo de entrada (`src/app/NdiSource.cpp:242`).
  - No hay clean feed.
- **Qué hacer.**
  - **SRT** como tipo de destino de T09, con `ffmpeg_mpegts_muxer` y `srt://host:port?mode=caller|listener&latency=…&passphrase=…`. La passphrase va en `CredentialStore`. Verificar que el FFmpeg del bundle trae libsrt; si no, vendorearla. Alternativa: `srtsink` de GStreamer, que ya está en el bundle.
  - **Clean feed:** una vista (`obs_view`) con el programa sin DSK (T25) y sin gráficos marcados `grafico`, con su encoder, como destino o grabación.
  - **NDI de salida:** evaluar la licencia del SDK frente a la regla del bundle portable. Mientras tanto, documentar la salida por GStreamer (`ndisink` del plugin de la comunidad), si se vendorea.
  - **Decklink de salida:** reactivar el plugin de OBS en el bundle, solo si hay hardware.
- **MCP.**
  - `stream_destino_agregar {tipo: "srt", modo, latencia_ms, passphrase?}`.
  - Herramientas nuevas: `salida_limpia {activar, destino?|grabar?}`, `salidas_profesionales` (lista de lo disponible en este bundle).
- **GUI.** En Configuración → Salida, el tipo "SRT" en el formulario de destino y la sección "Señal limpia (sin gráficos)". En la tarjeta Salidas, la fila del clean feed.
- **Depende de:** T09 y T25.

### T29 · Grabación independiente del stream: encoder propio, multipista, replay, segunda copia — P1
- **Estado: falta.**
  - La grabación comparte encoders con el stream (comentario en `src/app/BroadcastEngine.cpp:999-1000`, adjuntos en `:888-889`).
  - Hay una sola pista de audio (`:889`).
  - No hay `replay_buffer` ni segunda copia (`RecordConfig`, con un solo directorio, `BroadcastEngine.hpp:151-153`).
  - El encoder de respaldo actúa solo al arrancar (`startOutputWithFallback`, `:885-910`).
- **Qué hacer.**
  - `RecordConfig` suma `calidad: "como_stream"|"archivo"`. "archivo" usa un encoder propio: CRF o CQP alto, o ProRes con VideoToolbox en Mac.
  - Pistas: 1 = programa, 2..6 = buses o micrófonos elegidos en la Matriz. Se usa `obs_output_set_audio_encoder(out, enc, idx)` con un encoder AAC por pista.
  - `directorios: [principal, copia]`: una segunda salida `ffmpeg_muxer` con el mismo encoder hacia otro disco.
  - `replay_buffer` de libobs con N s y botón "Repetir": guarda el clip y lo carga como medio en preview.
  - Encoder de respaldo en caliente: ante la señal `stop` con código de encoder (`:1269-1275`), recrear con x264 y reanudar.
- **MCP.**
  - `record_ajustes {calidad, encoder?, pistas: [{n, bus}], copia_dir?}`.
  - Herramientas nuevas: `replay_ajustes {segundos}`, `replay_guardar {y_armar?: bool}`.
  - `status` muestra `encoder_respaldo_activo`.
- **GUI.** En Configuración → Grabación, "Calidad de archivo", pistas con un selector de bus cada una y "Copia en otro disco". En la tarjeta Salidas, un botón "Repetir últimos N s" y un indicador de copia doble. En la Matriz, las columnas de pistas de grabación rotuladas.
- **Depende de:** nada.

### T30 · Interfaz para la grabación ISO (entradas e invitados) — P1
- **Estado: parcial.** El motor tiene `SourceRec` (`src/app/BroadcastEngine.cpp:1031-1060`, hasta 8 en `BroadcastEngine.hpp:534`) y la herramienta `grabacion_individual` (`src/app/mcp/McpService.cpp:~1780-1830`). Ningún QML la usa.
- **Qué hacer.**
  - Exponer en `StudioViewModel` un modelo `isos` con `{fuente, grabando, archivo, mb, pista}`.
  - Validar el máximo y el disco con T10.
  - Asociar cada invitado a su fuente.
- **MCP.** `grabacion_individual` ya existe. Sumarle `invitado: id` y devolver `{archivo, mb}`.
- **GUI.**
  - Un botón ● "ISO" por entrada en la página Entradas y por invitado en la tarjeta Invitados.
  - En la tarjeta Salidas, "ISO: 3 grabando" con la lista desplegable y "Todas / Ninguna".
- **Depende de:** T10.

## Audio

### T31 · Herramientas de audio del operador: PFL, retardo, SincroLabios, latencia CLAP, PPM — P1
- **Estado: parcial.**
  - **PFL:** `audio_escuchar` existe (`src/app/mcp/McpService.cpp:1643`) y en la GUI es una columna de la Matriz (`MatrizAudio.qml:47,179,386`), no un botón por canal.
  - **Retardo:** existe en el motor y en el MCP `audio_sincro` (`BroadcastEngineScenes.cpp:900-916`, `McpService.cpp:1696-1724`), sin control en QML.
  - **SincroLabios:** solo está en los tests (`src/app/CMakeLists.txt:722`).
  - **CLAP:** `CLAP_EXT_LATENCY` está anunciado, pero `latencyChanged` no hace nada (`src/app/audio/ClapHost.cpp:191`).
  - **Medidores:** son lineales, sin balística PPM (`LevelMeter.qml:9,31-32`).
  - **Matriz:** marca el retardo por tipo de bus y no por cruce (`MatrizAudio.qml:106,202`).
- **Qué hacer.**
  - Botón PFL en cada tira del Mezclador.
  - Campo de retardo (ms) por fuente con botón "Medir (aplauso)" que corre `SincroLabios` entre la cámara y el micrófono elegidos, y propone el valor.
  - Consultar `clap_plugin_latency.get()` y compensar dentro del rack, retrasando las ramas paralelas.
  - Avisar cuando dos fuentes correlacionadas (por correlación cruzada de la `Matriz`) tienen distinta latencia.
  - Modo de medidor `pico|ppm_iib|vu` en `LevelMeter`, con constantes EBU tipo IIb.
  - Loudness por fuente opcional (reusa T11).
  - Marca de retardo por cruce en la Matriz.
- **MCP.**
  - `audio_escuchar` ya existe.
  - Herramienta nueva `audio_sincro_medir {video, audio}`.
  - `audio_rack` devuelve `latencia_muestras`.
  - Herramienta nueva `audio_medidores {balistica}`.
  - `audio_matriz` marca `retardo_ms` por cruce.
- **GUI.** En el Mezclador: botón "PFL" amarillo por canal, menú del canal con "Retardo…" y "Medir con aplauso", y selector de balística. En la Matriz, una insignia "+25 ms" en los cruces que suman retardo.
- **Depende de:** T11 para el loudness por fuente.

### T32 · Automixer para las bancas — P1
- **Estado: falta (escrito, sin usar).** Solo se instancia en `src/app/OperatorEngine.cpp:109,335-338`, que corre en `--headless-selftest` (`src/app/main.cpp:49`).
- **Qué hacer.**
  - Envolver `core/dsp/Automixer` como filtro de OBS que trabaja sobre un **grupo** de fuentes. Como el filtro de OBS es por fuente, se necesita un coordinador compartido: un `AutomixerGrupo` singleton, con el que cada filtro registra su nivel por bloque y del que recibe su ganancia (Dugan, suma de ganancias = 1).
  - Indicador del micrófono que "abrió", publicado en el bus (T50), para T34.
- **MCP.** Herramientas nuevas:
  - `automixer_grupos`;
  - `automixer_crear {nombre, fuentes: []}`;
  - `automixer_ajustar {grupo, activo, ultimo_abierto_mantener_ms, ruido_umbral_db}`;
  - `automixer_estado {grupo}` con la ganancia de cada fuente.
- **GUI.** En la página Audio, una sección "Automezcla" con la lista de grupos, las fuentes con su barra de ganancia en vivo y el micrófono abierto resaltado. En el Mezclador, una insignia "AM" en los canales del grupo.
- **Depende de:** nada. Lo usa T34.

## Sala y Legislatura

### T33 · Sala: IFB en sala, cupo en caliente, prueba técnica con eco y red — P1
- **Estado: parcial.**
  - `participant_talk` solo funciona en el lobby (`src/gateway/Room.cpp:321`, `MediaPump.cpp:478-482`).
  - El cupo se fija al arrancar el gateway (`src/gateway/main.cpp:561,592`; `SettingsModel.cpp:68` dice "reinicia el gateway").
  - `invitado_chequeo` mira cara, nivel y mute, pero no eco ni red (`src/app/mcp/McpPreguntaTools.cpp:311-340`).
  - La ISO del invitado: ver T30.
- **Qué hacer.**
  - **IFB:** en `MediaPump`, sumar la voz del operador al retorno de un invitado admitido o al aire, sin pasar al programa. Con T14 es simplemente otra entrada del retorno n. Tecla "mantener para hablar".
  - **Cupo:** el gateway reserva las 16 sesiones (`Config.hpp:51,56`) y el cupo pasa a ser un límite lógico en `Room`, cambiable por la API de admin sin reiniciar.
  - **Prueba técnica:**
    - eco: reproducir un tono por el retorno y medir cuánto vuelve por el micrófono del invitado con correlación; si vuelve, recomendar auriculares;
    - red: con las estadísticas de T15 durante 10 s más un paquete de prueba.
- **MCP.**
  - `participant_talk {id, activar}` funciona también en sala.
  - `room_set {cupo}` sin reinicio.
  - `invitado_chequeo` devuelve `{eco_db, red: {…}, recomendacion}`.
- **GUI.** En la tarjeta del invitado, un botón "Hablarle" (mantener) y "Prueba técnica" con resultado. En Configuración → Sala, el cupo sin el aviso de reinicio.
- **Depende de:** T14 y T15.

### T34 · Seguir al micrófono abierto — P1
- **Estado: falta.** `audio_peak` solo es una alarma de saturación (`src/app/agent/AlertEngine.cpp:62,279`).
- **Qué hacer.**
  - Crear `src/app/legislatura/SeguirMicrofono.{hpp,cpp}`, que se suscribe a "micrófono abierto" del automixer (T32) o, sin automixer, a niveles sostenidos (> umbral durante N ms, con histéresis).
  - Mapea micrófono → Persona → banca (T16) y **propone** `speaker_on_air {banca, modo: armar}` (T17). El operador confirma con TAKE.
  - Modo automático solo con la casilla marcada y nunca sobre la cámara al aire (T20).
- **MCP.** Herramientas nuevas: `seguir_microfono {activo, modo: proponer|automatico, umbral_db, sostener_ms}` y `seguir_microfono_estado`.
- **GUI.** En la tarjeta Oradores, el interruptor "Seguir al micrófono" y la propuesta como tarjeta destacada "Banca 12 – Pérez pidió la palabra · [TAKE]".
- **Depende de:** T16, T17 y T32.

### T35 · Plugin oficial para el sistema de conferencia del recinto — P1
- **Estado: falta.** No hay nada de DICENTIS, Televic ni Shure en `src`.
- **Qué hacer.** **Sin driver nativo:** un plugin de Python oficial en `assets/plugins/conferencia-recinto/`, que usa la superficie estable.
  - Escucha el sistema de conferencia:
    - Bosch DICENTIS: API JSON por WebSocket o "Conference Protocol";
    - Televic Confidea: API IP;
    - Shure MXCW y Sennheiser: comandos TCP o JSON documentados por cada fabricante.
  - Publica `plugin.evento("conferencia/microfono", {banca, abierto})`.
  - Con `plugin.llamar("speaker_on_air", {banca, modo})` dispara lo de T17 (respeta T02 y T20).
  - Expone un panel con el estado de cada banca.
  - El núcleo agrega:
    - el canal de evento estándar `conferencia/microfono`, que T34 consume igual que el automixer;
    - el mapeo de canal de conferencia a Persona (T16, `microfono.canal_conferencia`);
    - un modo "simulador" para probar sin el equipo.
  - Hay que probarlo con el equipo real del recinto antes de llamarlo "oficial".
- **MCP.** Las de plugins (`plugin_activar`, `plugin_control`, `plugin_eventos`) más `seguir_microfono {fuente: "conferencia"}`.
- **GUI.** En Plugins, la tarjeta del plugin con la configuración (marca, IP, credencial) y la grilla de bancas con LED de micrófono. En la tarjeta Oradores, la fuente "Sistema de conferencia".
- **Depende de:** T16, T17, T34 y T50.

### T36 · Modo "orden del día" y tiempos de uso de la palabra en el acta — P1
- **Estado: falta.**
  - No hay agenda ni puntos.
  - El tiempo usado se lleva (`src/gateway/Room.cpp:837`), pero `EventoViewModel::acta` (`src/app/ui/EventoViewModel.cpp:670-757`) no lo exporta.
- **Qué hacer.**
  - `evento::OrdenDelDia`: una lista de puntos `{n, expediente, titulo, estado: pendiente|en_tratamiento|votado|comision|retirado, inicio, fin, resultado}`. Se importa de texto o CSV.
  - Marcar un punto "en tratamiento":
    - actualiza el zócalo "Punto N: …";
    - crea un capítulo en la grabación (`evento_capitulos`);
    - lo anota en el acta.
  - El acta suma los tiempos por orador (de `usedMs` y de las bancas de T17).
- **MCP.** Herramientas nuevas:
  - `orden_del_dia_cargar {texto|archivo}`;
  - `orden_del_dia_ver`;
  - `orden_del_dia_punto {n, estado, resultado?}`.
  `evento_acta` incluye `tiempos_palabra`.
- **GUI.** En la página Evento, una pestaña "Orden del día" con los puntos y botones de estado. En la tarjeta Dirección, el punto actual con "Siguiente punto".
- **Depende de:** T16 y T17.

### T37 · Quórum y mayorías configurables, resultado oficial importado, acta con hash — P1
- **Estado: parcial.**
  - Existe "votaron X de Y" (`src/gateway/Votacion.cpp:189`, `PanelPadron.qml:44`).
  - No hay reglas, ni importación del resultado, ni firma (`EventoViewModel.cpp:670-764`).
  - No se puede sumar a la cola de preguntas desde el chat (solo a mano: `pregunta_sumar`, `McpPreguntaTools.cpp:151-179`).
- **Qué hacer.**
  - `ReglaQuorum {miembros, presentes_min: "mitad_mas_uno"|n}` y `ReglaMayoria {simple|absoluta|dos_tercios_presentes|dos_tercios_miembros}`.
  - Indicador "hay quórum: sí/no".
  - Importar el resultado oficial del tablero del recinto en CSV o con carga rápida manual. Se muestra con la plantilla nominal (T43).
  - Acta con SHA-256 del contenido y de los archivos adjuntos (transcripción de T45 y grabación), en un `acta.sha256`.
  - Botón "Sumar a preguntas" desde el chat (lo comparte con T18).
- **MCP.** Herramientas nuevas:
  - `quorum_regla {miembros, minimo}`;
  - `quorum_estado`;
  - `votacion_resultado_oficial {csv|afirmativos, negativos, abstenciones, nominal?: []}`;
  - `pregunta_desde_chat {plataforma, mensaje_id}`.
  `evento_acta` devuelve `hash`.
- **GUI.** En el panel del padrón, un indicador grande de quórum (también en la pantalla de proyección). En el panel de votación, "Cargar resultado oficial". En la página Evento → Acta, el hash visible.
- **Depende de:** T16 y T43 (plantilla).

## Video y PTZ

### T38 · Multiview profesional: tally, costo por tarjeta, cuenta regresiva — P1
- **Estado: parcial.**
  - `VideoWall.qml:62-74` tiene nombre, pero el borde siempre es blanco (`:67`).
  - `publicarTally()` existe (`StudioViewModel.cpp:908-920`).
  - El costo por tarjeta ya se mide (`demorasDe()`, `src/app/video/VideoGrafos.cpp:639`, `demoras_tarjetas_ms`, `:1513`) pero no se dibuja en `Patchbay.qml`.
- **Qué hacer.**
  - `VideoWall` recibe `fuentesAire` y `fuentesArmadas`: borde rojo para PGM y verde para PVW.
  - Además: estado de señal (T19), vúmetro por celda (nivel de la fuente de audio asociada), reloj y la cuenta regresiva del medio (T46).
  - En `Patchbay.qml`, una insignia de ms por cuadro en cada nodo, en ámbar si pasa del presupuesto del lienzo.
- **MCP.**
  - `capture_multiview` ya existe.
  - Herramienta nueva `multiview_ajustes {celdas, vumetros, reloj, tally}`.
  - `video_grafo` incluye `ms` por nodo.
- **GUI.** El multiview con tally, nombre, vúmetro y alarma. En la página Video, el grafo con su costo.
- **Depende de:** T19 y T46.

### T39 · Scopes y legalizador broadcast-safe — P1
- **Estado: falta.** No hay scopes. Solo existe un ajuste de rango limitado o completo (`src/app/ui/SettingsModel.cpp:47`).
- **Qué hacer.**
  - Elemento de GStreamer `bascopes`, propio en `src/gst-plugins/scopes`, o un shader GLSL: forma de onda Y y RGB parade, vectorscopio e histograma sobre una rama `tee` de la entrada o del programa, a 10 fps de baja resolución.
  - Legalizador: clip o soft-clip de luma y croma al rango legal en la salida de programa (opción, para entregas a TV), como elemento de GStreamer o filtro de OBS.
- **MCP.** Herramientas nuevas: `video_scope {fuente|programa, tipo: onda|parade|vector|histograma}` (devuelve la imagen) y `video_legalizar {activo, modo}`.
- **GUI.** En la página Video, un panel de scopes con selector, también como celda del multiview. En Configuración → Video, "Legalizar para TV".
- **Depende de:** nada.

### T40 · Superresolución: alarma, bypass y entrenamiento desde cámaras locales — P1
- **Estado: parcial.**
  - El programador ajusta al presupuesto (`src/app/video/VideoGrafos.cpp:1890-1934`), pero pasarse solo pone el número en ámbar (`PanelProgramador.qml:117`). La primera red de cada acelerador corre siempre (`:1913-1918`).
  - Se entrena solo desde una URI de red (`SuperRes.cpp:238`, `uriDe()` en `VideoGrafos.hpp:213`).
- **Qué hacer.**
  - Alarma "la superres no llega a tiempo" cuando los ms por cuadro superan el 80 % del colchón durante 2 s. Bypass automático (modo barato o reposo) con histéresis, y publicación en el bus.
  - Entrenar desde una cámara local, grabando del grafo con un `tee` a `filesink` en vez de usar la URI.
- **MCP.**
  - `superres_programador {bypass_auto: bool, umbral_pct}`.
  - `superres_entrenar {entrada}` acepta entradas locales.
  - Tipo de disparador `superres_tarde`.
- **GUI.** En `PanelSuperres.qml` y `PanelProgramador.qml`, un cartel rojo "No llega: pasada a barato" y el interruptor "Bypass automático". "Entrenar" habilitado para cámaras locales.
- **Depende de:** T04 y T50.

### T41 · PTZ: presets con miniatura, grilla de bancas, velocidad — P1
- **Estado: parcial.**
  - Hay posiciones con nombre (`src/app/ptz/Posiciones.hpp:25-41`), pero sin imagen y sin grilla.
  - El preset no lleva velocidad (`src/app/ptz/PtzVisca.cpp:204-207`). Solo el movimiento digital tiene duración (`ControlCamaras.hpp:132`).
- **Qué hacer.**
  - `Posicion` suma `miniatura` (JPEG de 320 px capturado al guardar con `capture_source`), `velocidad` y `segundos`. Para VISCA: velocidad de pan/tilt en el recall cuando la cámara lo admite; si no, pasos interpolados.
  - Grilla del recinto: `Recinto {filas, columnas, bancas: [{n, x, y, persona, posicion}]}`, editable.
- **MCP.**
  - `ptz_posicion_crear` y `ptz_posicion_editar` suman `{miniatura: auto, velocidad?, segundos?}`.
  - `ptz_posiciones` devuelve la ruta de la miniatura.
  - Herramientas nuevas: `recinto_ver` y `recinto_editar {bancas}`.
- **GUI.** En la página PTZ, presets como tarjetas con miniatura y una pestaña "Recinto" con la grilla para tocar. Esa grilla la reusan T17 y la superficie de control.
- **Depende de:** T16.

### T42 · PTZ: VISCA por serie y joysticks vía plugin — P1
- **Estado: parcial.** VISCA va solo por red (`src/app/ptz/PtzVisca.hpp:4-7,63-64`). No hay serie ni joystick.
- **Qué hacer.** **Sin drivers nativos.**
  - **VISCA serie** (RS-232/422 con adaptador USB): un plugin de ejemplo `assets/plugins/ptz-visca-serie/` con `pyserial` (vendoreado en T51). Registra la cámara con la API de PTZ de plugins (`plugin.ptz`, que ya existe en el SDK).
  - **Joystick:**
    - plugin `ptz-joystick-hid` con `hidapi` (T51) para joysticks USB genéricos y paneles que se presentan como HID; mapea los ejes a `ptz_mover` con velocidad;
    - plugin `ptz-visca-ip-escucha` que escucha VISCA-over-IP en UDP 52381 como "cámara virtual", para controladores comerciales (Sony RM-IP, Panasonic AW-RP, Skaarhoj) que hablan VISCA, y reenvía a la cámara real con el interlock (T20).
  - El núcleo agrega:
    - un `ptz_mover` continuo con `{vel_pan, vel_tilt, vel_zoom}` y auto-stop si no llega un refresco en 300 ms (seguridad);
    - un evento `ptz/seleccion` para la cámara activa del joystick.
- **MCP.** `ptz_mover {continuo: true, …, ttl_ms}`. Herramientas nuevas: `ptz_seleccionar {camara}` y `ptz_camara_plugin` (listar las que registró un plugin).
- **GUI.** En la página PTZ, el indicador "Controlada por: joystick (plugin)" y la cámara seleccionada resaltada. En Plugins, las tarjetas de los ejemplos con su configuración (puerto serie, dispositivo HID, ejes).
- **Depende de:** T20 y T51.

## Gráficos y accesibilidad

### T43 · Gráficos legislativos: plantillas, preview en PVW, datos vivos, HTML — P1
- **Estado: parcial.**
  - Las plantillas son placa-titulo, pregunta, proximo-orador, ticker y zocalo (`assets/graficos/`). Ninguna es legislativa.
  - El preview existe solo en el editor (`src/app/qml/pages/GraficosPage.qml:194-242`).
  - El render es PNG previo en base64 (`src/grafico/main.cpp:13-19`). No hay obs-browser.
- **Qué hacer.**
  - Plantillas de fábrica:
    - zócalo de orador con bloque y distrito (T16);
    - "Orden del día – Punto N" (T36);
    - resultado de votación nominal (T37);
    - "Cuarto intermedio" y "Minuto de silencio";
    - quórum.
  - "Armar gráfico": el gráfico sale primero en el preview (canal de preview) y sube al aire con TAKE.
  - Datos vivos: primero, campos "dinámicos" (reloj, cronómetro, contadores) dibujados por la fuente de OBS encima del PNG, sin re-render. Después, render a 30 fps por memoria compartida (pendiente del motor) para lo demás.
  - HTML: evaluar `obs-browser` (CEF pesa mucho para el bundle) o renderizar HTML con el motor propio. Decidir y documentar.
- **MCP.**
  - `grafico_mostrar {armar: bool}`.
  - `grafico_repertorio` incluye las legislativas.
  - Herramienta nueva `grafico_campo_vivo {grafico, campo, fuente: reloj|cronometro_orador|votos}`.
- **GUI.** En Rótulos, el botón "Armar en PVW" además de "Mostrar". En la galería de Gráficos, una categoría "Legislatura". En el editor de plantillas, los campos vivos se marcan con un ícono.
- **Depende de:** T16, T36, T37 y T25.

### T44 · Subtítulos con corrección humana y parciales — P1
- **Estado: falta.**
  - Las frases van directo a la caja de subtítulos (`src/app/agent/Subtitulos.hpp:28`, `alFrase`).
  - El reconocimiento corta por VAD sin parciales (`src/app/audio/Escucha.hpp:83-96`).
- **Qué hacer.**
  - Crear `ColaCorreccion` entre `Escucha` y `Subtitulos`/CC (T21): cada frase espera N s (configurable, 0 = sin cola). En ese tiempo, el corrector puede editarla, descartarla o aprobarla antes.
  - Puesto remoto del corrector como página web (`/correccion/`) con clave por arranque, o desde la superficie de control (T52).
  - Después, sherpa-onnx `OnlineRecognizer` (streaming) para parciales: solo en la vista del corrector, no al aire.
- **MCP.** Herramientas nuevas:
  - `subtitulos_correccion {activa, demora_s}`;
  - `subtitulos_pendientes`;
  - `subtitulos_corregir {id, texto|descartar}`.
- **GUI.** En el panel de Subtítulos, la cola con cuenta regresiva por frase, edición en línea y Enter para aprobar. "Abrir puesto de corrección" con QR (`QrDialog.qml`).
- **Depende de:** T21 y T22.

### T45 · Archivo y rotulado: WebVTT, transcripción al acta, "Traducción automática" — P1
- **Estado: falta.**
  - Solo hay `.txt` y `.srt` (`src/app/agent/FlujosTexto.cpp:88`, `TarjetaTraduccion.qml:120`).
  - No hay rótulo automático "Traducción automática". La pista separada de traducción ya se puede (`src/app/mcp/McpHelp.cpp:661`).
  - El glosario no fue revisado por la Legislatura.
- **Qué hacer.**
  - Exportar `.vtt`. La transcripción con tiempos va a `Eventos/<id>/` y se adjunta al acta (T37).
  - Mientras se muestre una traducción o salga una voz sintética, se activa un DSK (T25) con el rótulo "Traducción automática".
  - Exportar e importar el glosario para la revisión (CSV), con la marca `revisado_por`.
- **MCP.**
  - `texto_flujos {formato: srt|vtt|txt}`.
  - Herramienta nueva `traduccion_rotulo {activo}`.
  - Herramientas nuevas: `glosario_exportar` y `glosario_importar {archivo, revisado_por}`.
- **GUI.** En la tarjeta de Traducción, el formato de exportación y la casilla "Rotular traducción automática" (prendida por defecto). En el panel del glosario, "Exportar para revisión" e "Importar revisado".
- **Depende de:** T25 y T37.

### T46 · Tiempos en Dirección: cuenta regresiva del medio, playlist, back-timing — P1
- **Estado: parcial.**
  - La duración y la posición del medio se leen (`src/app/BroadcastEngineScenes.cpp:994-995`), pero solo alimentan `media_end`.
  - La cuenta regresiva de Dirección es la del cue (`DireccionContent.qml:128`, `EventoViewModel.cpp:955`).
  - No hay playlist (`src/app/medios/Medios.hpp` es un explorador).
  - El back-timing es parcial: hay `desvioSeg` y `restanteSeg` del cue (`EventoViewModel.cpp:943-957`), pero no hora de fin ni tiempo restante del bloque.
- **Qué hacer.**
  - `StudioViewModel` expone `mediosAlAire: [{fuente, restante_s}]`, con aviso a los 10 s.
  - Playlist en `src/app/medios/Playlist.{hpp,cpp}`: ítems con in/out, auto-next, loop y preview en PVW.
  - Back-timing: `hora_fin_prevista` del evento, restante del bloque y "para terminar a las HH:MM te quedan X".
- **MCP.**
  - Herramientas nuevas: `media_restante`, `playlist_crear {items}`, `playlist_control {play|next|stop, auto_next}`.
  - `evento_ver` incluye `hora_fin_prevista`, `restante_bloque_s` y `back_timing_s`.
- **GUI.**
  - En la tarjeta Dirección, un contador grande del medio al aire (en rojo en los últimos 10 s), el restante del bloque y la hora de fin.
  - En Medios, una vista de playlist simple.
  - En el multiview, el contador (T38).
- **Depende de:** nada.

## Seguridad, modo en vivo y control

### T47 · Credenciales y accesos: passphrase, PIN por sesión, enlaces que vencen, token MCP — P1
- **Estado: parcial.**
  - El almacén usa una clave de dispositivo sin passphrase (`SecretsStore` con Argon2id existe, pero no se usa para esto).
  - `docs/MCP.md:217` dice que la clave de stream no se guarda, y se guarda como `stream-last` (`src/app/OperateMode.cpp:466`, `:547`).
  - El PIN es aleatorio por arranque del gateway, no por sesión (`src/gateway/main.cpp:496-499`).
  - Los enlaces de invitado no vencen.
  - El token MCP es persistente (`src/app/mcp/McpHttp.cpp:66-76`).
- **Qué hacer.**
  - Passphrase opcional: `CredentialStore` envuelve su clave con Argon2id (`SecretsStore`) y la pide en el arranque, desde el launcher o la UI. Opción Keychain o DPAPI cuando no importa la portabilidad.
  - Corregir `docs/MCP.md`.
  - PIN nuevo en cada `evento_correr` o "Nueva sesión".
  - Enlaces de invitado con `vence` (fábrica: 12 h).
  - Rotar el token MCP en cada arranque: el puente lee `Runtime/mcp.json`.
- **MCP.**
  - Herramientas nuevas: `seguridad_estado`, `seguridad_passphrase {activar}` (con formulario fuera del modelo), `room_pin {regenerar}`.
  - `invitacion {vence_h}`.
  - `mcp_token_rotar`.
- **GUI.** En Configuración → Seguridad, la passphrase, "Rotar token MCP al arrancar" y "PIN nuevo por sesión". En la invitación, la caducidad. En el arranque, el diálogo de passphrase.
- **Depende de:** nada.

### T48 · Modo en vivo que protege la interfaz — P1
- **Estado: parcial.**
  - Las extensiones QML se apagan solo si colgaron al cargar (`src/app/qml/pages/DashboardPage.qml:244-254`, `ExtensionesModel.cpp:143-168`).
  - `ConfirmDialog` solo recuerda; "No se bloquea nada" (`src/app/qml/controls/ConfirmDialog.qml:85-100`).
  - Los ajustes de alcance gateway o restart solo están bloqueados para el MCP (`McpService.cpp:1911`), no para el humano (`SettingsModel.cpp:68,76-79`).
- **Qué hacer.**
  - "Modo en vivo" = `enVivo` ∨ transmitiendo (configurable). Mientras dure:
    - extensiones QML nuevas no cargan, y las cargadas corren en un `Loader` asíncrono con tiempo máximo;
    - Configuración, Plugins y Asistente → Consola piden contraseña o confirmación fuerte (escribir "CONFIRMAR");
    - ajustes gateway o restart bloqueados con explicación;
    - solo plugins aprobados (T60).
- **MCP.** Herramientas nuevas: `modo_vivo_estado` y `modo_vivo_ajustes {paginas_bloqueadas, contrasena?, extensiones}` (fuera de vivo).
- **GUI.** Un candado en el riel de navegación sobre las páginas bloqueadas, y un diálogo de desbloqueo con motivo que queda en el registro.
- **Depende de:** T02.

### T49 · Atajos de teclado configurables y globales — P1
- **Estado: falta.**
  - Solo hay `Shortcut` fijos (`OutputWindow.qml:91-92`, `PageWindow.qml:96`, `PanelWindow.qml:106`) y handlers con foco (`DireccionContent.qml:28`, `ScenesContent.qml:59`).
  - No hay atajos globales.
  - El prompter no tiene teclado ni HID (`assets/web/prompter/app.js`).
- **Qué hacer.**
  - Crear `src/app/control/Acciones.{hpp,cpp}`: un catálogo de **acciones** con id, nombre, herramienta MCP y argumentos. Ejemplos: TAKE, CUT, FTB, siguiente cue, siguiente orador, mute de cada micrófono, DSK n, prompter más rápido, prompter pausa.
  - Es el mismo catálogo que usa la superficie (T52).
  - `Config/atajos.json` mapea tecla → acción, en dos ámbitos:
    - local: un `Shortcut` QML generado;
    - global, aunque la ventana no tenga foco: macOS con `CGEventTap` o `RegisterEventHotKey` (pide Accesibilidad), Windows con `RegisterHotKey`, Linux X11 con `XGrabKey` (Wayland a través del portal `GlobalShortcuts` si está, si no avisar).
  - Los pedales de prompter que se presentan como teclado se mapean igual. Los HID se resuelven con un plugin (T51).
- **MCP.** Herramientas nuevas:
  - `acciones_listar`;
  - `atajos_listar`;
  - `atajo_asignar {tecla, accion, global?: bool}`;
  - `atajo_quitar`;
  - `accion_ejecutar {accion}` (pasa por T02).
- **GUI.** En Configuración → Atajos, la tabla de acciones con "Presioná la tecla…", la columna "Global" y la detección de conflictos. La ayuda del atajo aparece en el tooltip de cada botón.
- **Depende de:** T02. Lo comparte con T52.

### T50 · Bus de eventos de la app — P1
- **Estado: parcial.**
  - Hay un bus de plugins (`Plugins::publicarEvento`, `src/app/plugins/Plugins.cpp:996-1024`, con comodines en `:105-110`), pero los eventos de la app (escena, tally, grabación, alarmas, niveles) **no** se publican ahí.
  - El MCP no tiene notificaciones (`GET /mcp` devuelve 405, `src/app/mcp/McpHttp.cpp`).
  - El gateway solo tiene SSE de admin y el canal de superficies.
- **Qué hacer.**
  - Crear `src/app/control/BusEventos.{hpp,cpp}` con temas jerárquicos:
    - `aire/escena`, `aire/armada`, `aire/tally`;
    - `salida/stream/<id>`, `salida/grabacion`;
    - `audio/nivel/<fuente>` (submuestreado a 10 Hz), `audio/loudness`;
    - `alarma/<id>`;
    - `sala/participante`, `oradores/al_aire`, `entrada/senal/<id>`, `ptz/<camara>`;
    - `conferencia/microfono` y los de los plugins.
  - Cada tema guarda el último valor (para sincronizar al conectarse).
  - Consumidores:
    - plugins: se reemplaza `publicarEvento` y `@plugin.escuchar("aire/tally")` funciona;
    - superficie de control (T53);
    - disparadores;
    - MCP: herramienta de espera más SSE en `GET /mcp` (Streamable HTTP) para notificaciones.
- **MCP.** Herramientas nuevas:
  - `eventos_temas`;
  - `eventos_ultimo {tema}`;
  - `eventos_esperar {temas, timeout_s}`.
  `plugin_eventos` pasa a ser un alias.
- **GUI.** En la página Registro, una pestaña "Eventos" con el flujo en vivo y un filtro por tema, para que el operador vea qué puede usar en un plugin o en un botón.
- **Depende de:** nada. Desbloquea T04, T35, T51 y T52-T59.

### T51 · SDK de dispositivos para plugins: vendorear HID, MIDI, serie, OSC, Stream Deck — P1
- **Estado: parcial.**
  - El Python del bundle (`tools/deps/python.sh:36-40`) trae numpy, pillow y onnx, entre otros, pero **no** hidapi, mido, python-rtmidi, pyserial, python-osc ni streamdeck.
  - Las bibliotecas de dispositivos se instalan **a demanda** con pip (pyusb, libusb-package, hidapi, pyserial en `Datos/Plugins/.herramientas/_libs`, `src/app/plugins/Plugins.cpp:1228-1251`, `:1627-1641`). Eso choca con la regla del bundle (necesita red y no está en el bundle).
  - Los plugins corren sin sandbox, con hilos y sockets (`src/plugins/sdk/ba_plugin/__init__.py:59-64`). Llaman herramientas con `plugin.llamar` (`__init__.py:420`, `Plugins.cpp:868-905`) y **con rol vacío**, así que ven todo (`GuiMode.cpp:169-173`).
- **Qué hacer.**
  - **Vendorear en `tools/deps/python.sh`**, por plataforma y en el bundle:
    - `hidapi` (wheel `hid` con libhidapi estática);
    - `pyserial`;
    - `mido` + `python-rtmidi` (CoreMIDI, WinMM, ALSA);
    - `python-osc`;
    - `streamdeck` (python-elgato-streamdeck, que usa libhidapi por ctypes, así que hay que vendorear también `libhidapi` .dylib/.so/.dll y apuntarla con su variable de búsqueda);
    - `pillow` (ya está) para dibujar teclas.
    Así `Plugins.cpp` deja de instalarlas por pip.
  - Linux necesita reglas udev para HID: documentar el `.rules` y ofrecer instalarlo con permiso del usuario.
  - Módulo `ba_plugin.dispositivos` (ya existe el archivo): enumerar HID, MIDI y serie, más ayudas para abrir.
  - **Permisos de plugins:** el manifiesto suma `herramientas: [...]` (allowlist). `CallContext{"plugin:<id>", "plugin:<id>"}` con un rol generado, y T02 se aplica.
  - **Ejemplos oficiales** en `assets/plugins/`:
    - `superficie-streamdeck` (T58);
    - `midi-faders` (mapea CC a `audio_set` o `transicion_manual`);
    - `tally-tsl-umd` (TSL UMD v5 por UDP desde `aire/tally`, para monitores y cámaras);
    - `gpio-reles-usb` (relés HID o serie para tally o luz de "al aire");
    - `pedal-prompter-hid`.
- **MCP.**
  - `plugin_desde_ejemplo` lista los ejemplos nuevos.
  - Herramienta nueva `dispositivos_plugin` (enumera HID, MIDI y serie vistos por el Python del bundle, para que el asistente escriba el plugin).
  - `plugin_ver` muestra `herramientas` permitidas.
- **GUI.** En Plugins, "Nuevo desde ejemplo" con las categorías "Superficies", "Tally" y "Pedales", y una sección "Dispositivos conectados" (HID, MIDI y serie) con un botón "Escribir un plugin para esto con el asistente". En la tarjeta del plugin, los permisos de herramientas con casillas.
- **Depende de:** T50 y T02.

---

# Superficie de control (estilo Bitfocus Companion)

**Objetivo.** El operador abre en una tablet una botonera configurable, emparejada por QR desde la app. Tiene páginas con una grilla de celdas: botones con texto, ícono, color y estado vivo; faders; perillas y medidores. Cada control dispara una acción (una herramienta MCP con argumentos fijos, o con plantilla de valor) y muestra estado del bus de eventos. El mismo modelo de páginas lo usan las superficies de hardware vía plugin (Stream Deck, Loupedeck, X-keys, MIDI) y, de forma opcional, el ecosistema de Companion.

**Lo que ya hay y se reusa**, verificado:
- **Gateway (proceso B).** Tiene un servidor HTTP/WS propio:
  - `SignalingServer`, puerto 8787 (`src/gateway/SignalingServer.cpp:291`, `:415`);
  - bind configurable `gateway.bind` (`src/app/AppSettings.hpp:123`, `127.0.0.1` de fábrica);
  - HTTPS con mbedtls en 8789 (`src/gateway/net/TlsTerminator.{hpp,cpp}`, arrancado en `main.cpp:649-663`, con certificado propio o autofirmado);
  - archivos estáticos de `assets/web/<x>` con canonicalización (`SignalingServer.cpp:835`);
  - WebSocket `/ws` con el cliente común `assets/web/comun/canal.js`.
- **Patrón de páginas con clave por arranque:** `prompterKey`, `rotulosKey` y `publicoKey` en `main.cpp:609-618`, viajando en el fragmento `#k=`. El tablero usa comparación en tiempo constante (`SignalingServer.cpp:1024-1038`).
- **API de admin** (loopback 8788, `Bearer`, cookie `SameSite=Strict` más `X-BA-Admin` anti-CSRF, código de login de un uso de 2 min: `src/gateway/AdminApi.cpp:153-235`) y **SSE** `/api/v1/events` (`AdminApi.cpp:832`), que la app ya consume (`src/app/ui/ConferenceClient.hpp`).
- **QR:** `src/core/qr/Qr.{hpp,cpp}` (sobre `third_party/qrcodegen`), matrices en la API de admin (`AdminApi.cpp:288-327`) y `src/app/qml/controls/QrDialog.qml:57-95`.
- **Límite de intentos** solo para el PIN (`SignalingServer.hpp:297-305`). **No hay chequeo de Origin en el gateway.**
- **MCP HTTP** solo en localhost, con Origin localhost obligatorio (`src/app/mcp/McpHttp.cpp:48-53`, `:93`): una tablet **no** puede ni debe llamarlo directo.
- **Choque de nombres:** `src/gateway/Superficies.{hpp,cpp}` y las herramientas `superficie_listar`/`superficie_mostrar` ya existen para el **latido de páginas que muestran** (pantallas, carrusel, prompter). La función nueva se llama **Botonera** (`botonera_*`, `src/app/control/Botonera*`, `src/gateway/Botonera.*`, `assets/web/botonera/`) para no pisarla.

### T52 · Superficie de control: modelo de datos y motor de acciones y estado — P1
- **Estado: falta.** Existen las "recetas" (listas de llamadas guardadas en `Config/recetas.json`, `src/app/mcp/McpRecetaTools.cpp:1-23`) y el catálogo de acciones de T49. No hay modelo de botonera.
- **Qué hacer.**
  - **Modelo** en `src/app/control/BotoneraModelo.{hpp,cpp}`, persistido en `Config/botonera.json` con `SnapshotStore` (T24):
    - `Perfil {id, nombre, rol, herramientas_permitidas[], paginas[]}`. Un perfil también es un **puesto** (director, audio, gráficos, sala) con permisos acotados; esto cubre los "puestos remotos por navegador" y los "perfiles de operador" del informe (§27).
    - `Pagina {id, nombre, columnas, filas, celdas[]}`.
    - `Celda {x, y, ancho, alto, tipo: boton|fader|perilla|medidor|texto|reloj, estilo: {texto, icono, color, color_activo, tamano_texto}, accion, accion_soltar?, accion_mantener?, valor: {plantilla, min, max, paso, curva}, estado: [Ligadura]}`.
    - `Accion = {tipo: "herramienta", herramienta, args}`, o `{tipo: "accion", id}` (catálogo de T49), o `{tipo: "pagina", ir_a}`, o `{tipo: "receta", id}`, o `{tipo: "secuencia", pasos[], esperas}`. En faders y perillas, `args` admite la plantilla `{{valor}}` y, en perillas relativas, `{{delta}}`.
    - `Ligadura {tema (bus T50), ruta JSON, condicion (==, >, en), efecto: color|texto|titilar|valor|icono}`. Ejemplos:
      - "tally PGM de Cámara 2 → rojo";
      - "grabando → titilar";
      - "loudness I → texto «-23.4»";
      - "nivel de Mic 3 → medidor".
  - **Motor** en `BotoneraMotor`:
    - resuelve las ligaduras contra el bus de eventos y emite el **estado renderizado** de cada celda (texto final, colores, valor);
    - ejecuta acciones con `McpCore::callTool` y `CallContext{"botonera:<dispositivo>", "botonera:<perfil>"}`;
    - aplica `herramientas_permitidas` **del lado de la app** (no confía en el cliente);
    - pasa por T02 (si la herramienta es de aire y hay aire, el botón pide confirmación en la propia superficie);
    - aplica antirrebote y coalescencia de faders (a lo sumo 30 Hz por control; el último valor gana).
  - Plantillas de fábrica:
    - "Director" (escenas con tally, TAKE, CUT, FTB, DSK, T-bar);
    - "Audio" (faders de micrófonos con mute y medidor, loudness);
    - "Legislatura" (grilla de bancas de T41, siguiente orador, punto del orden del día, quórum);
    - "Prompter" (velocidad, pausa, saltos).
- **MCP.** Ver T57.
- **GUI.** Ver T56.
- **Depende de:** T50, T02 y T49 (catálogo de acciones). Mejora con T24.

### T53 · Superficie de control: servidor en el gateway y puente con la app — P1
- **Estado: falta.** Hoy las órdenes de páginas web llegan a la app por un buzón con sondeo cada 1 s (`src/app/ui/RotuladorViewModel.cpp:57,219`), demasiado lento para una botonera.
- **Qué hacer.**
  - **Gateway** en `src/gateway/Botonera.{hpp,cpp}`:
    - archivos estáticos `assets/web/botonera/` en `/botonera/`;
    - WebSocket `/ws?botonera=1`, autenticado con el token del dispositivo (T54).
  - **Lo que el cliente puede mandar**: solo `presionar {celda}`, `soltar {celda}`, `valor {celda, v}`, `girar {celda, delta}`, `pagina {id}` y, en modo edición con permiso, `editar {…}`. **Nunca** nombres de herramientas ni argumentos.
  - **Gateway → app:** los eventos de la botonera viajan por el SSE de admin que la app ya escucha (`/api/v1/events`, tipo `botonera`), con latencia de milisegundos.
  - **App → gateway:**
    - `POST /api/v1/botonera/estado` (admin, Bearer) con el estado renderizado (diferencias por celda);
    - `POST /api/v1/botonera/dispositivos` con los hashes de token vigentes y su perfil.
    El gateway reparte por WS a cada tablet según su perfil y página.
  - La app es la fuente de verdad (modelo, permisos, ejecución). El gateway es el borde de red (TLS, autenticación, límites y Origin) y **no** ejecuta nada.
  - Funciona por LAN (8787 u 8789) y opcionalmente por el túnel WireGuard. Por Cloudflare, solo si el operador lo habilita explícitamente por perfil.
- **MCP.** `botonera_estado` (T57) muestra las conexiones vivas.
- **GUI.** En el editor (T56), la lista de tablets conectadas con latencia y página actual.
- **Depende de:** T52 y T54.

### T54 · Superficie de control: emparejamiento por QR y seguridad — P1
- **Estado: falta.** Hay piezas para reusar: códigos de un uso de 2 min (`AdminApi.cpp:164-183`), TLS (`net/TlsTerminator`), comparación en tiempo constante y `CredentialStore`. No hay tokens por dispositivo, ni chequeo de Origin, ni límite de tasa general.
- **Qué hacer.**
  - **Emparejamiento.**
    1. En la app, "Emparejar tablet" pide al gateway un **código de emparejamiento** de un uso: 128 bits aleatorios, vence a los 2 min y queda atado al perfil elegido.
    2. Se muestra un QR con `https://<ip-lan>:8789/botonera/#p=<código>`. El código va en el **fragmento**, que no sale en logs ni en Referer.
    3. La tablet hace `POST /api/botonera/emparejar {codigo, nombre_dispositivo}`. El gateway lo canjea una sola vez y la app (por SSE) **pide confirmación en el escritorio**: "¿Emparejar 'iPad Dirección' con perfil Director?".
    4. Si se aprueba, se emite un **token de dispositivo** de 256 bits. Se guarda solo su hash (SHA-256 con sal) en `CredentialStore`. La tablet lo guarda en `localStorage` y lo manda en el primer mensaje del WS (no en la URL).
  - **Tokens:**
    - revocables uno por uno;
    - vencimiento configurable: de fábrica 30 días sin uso; "solo esta sesión" vence al terminar el evento;
    - rotación al reconectar (el gateway emite uno nuevo y el viejo vale 60 s).
  - **Mínimo privilegio:**
    - cada dispositivo tiene su perfil, y el perfil su `herramientas_permitidas`;
    - el motor (T52) rechaza toda acción fuera de esa lista;
    - las herramientas de aire (T02) **siempre** piden confirmación en el dispositivo (mantener 1 s o doble toque, configurable) y, según el ajuste, también en el escritorio;
    - el modo edición necesita la marca `puede_editar`, apagada de fábrica.
  - **Red:**
    - solo LAN de fábrica: se aceptan conexiones de redes privadas RFC1918 y link-local, salvo que se habilite el túnel;
    - si `gateway.bind` es `127.0.0.1`, emparejar ofrece cambiarlo explicando el riesgo;
    - TLS de fábrica (8789) con el certificado autofirmado: la primera vez la tablet muestra la advertencia, y el QR incluye la huella SHA-256 para verificarla a mano; o se usa el `cert.pem` propio de `Credenciales/`.
  - **Web:**
    - chequeo de `Origin`/`Host` igual al servidor en el WS y los POST;
    - `SameSite=Strict`;
    - CSP estricta en `/botonera/` (`default-src 'self'`, sin `unsafe-inline`);
    - sin cookies de sesión: el token va por mensaje WS.
  - **Límites:**
    - emparejamiento: 5 intentos por IP cada 10 min;
    - por dispositivo: 20 acciones por segundo (faders coalescidos aparte);
    - cortar la conexión ante abuso;
    - máximo de dispositivos (32).
  - **Auditoría:** cada emparejamiento, revocación, acción ejecutada o rechazada y edición se registra en `Logs/botonera.log` y en la traza MCP (`McpTraza`, con el origen `botonera:<dispositivo>`).
  - **Caducidad de sesión:** WS sin latido 30 s = desconectado; inactividad configurable que bloquea la tablet con un PIN local opcional.
- **MCP.** `botonera_emparejar`, `botonera_dispositivos`, `botonera_revocar`, `botonera_auditoria` (T57). La confirmación del emparejamiento es **solo humana** (el asistente no puede aprobar un dispositivo).
- **GUI.**
  - En la página Botonera → Dispositivos, "Emparejar tablet" (elige perfil y vencimiento, muestra QR grande y huella TLS), la lista de dispositivos con último uso, IP y perfil, y "Revocar" o "Revocar todos".
  - Diálogo de confirmación de emparejamiento.
  - Pestaña "Auditoría".
- **Depende de:** T53. Lo usan T55 y T58.

### T55 · Superficie de control: cliente web para tablet — P1
- **Estado: falta.** Hay páginas móviles de referencia: Rotulador (`/rotulos/`), control de prompter (`/prompter/#control`) y tablero (`/board/`), con `canal.js`.
- **Qué hacer.**
  - En `assets/web/botonera/` (`index.html`, `app.js`, `botonera.css`), sin framework, reusando `comun/canal.js` y `trama.js`.
  - Grilla responsiva por página (celdas cuadradas, gestos táctiles, `pointer events`).
  - Controles:
    - **botón**: presionar y soltar, mantener, color, ícono, texto tipo LCD con estado vivo y titilado;
    - **fader** vertical u horizontal con toma suave: no salta al valor tocado, sigue desde la posición actual; T-bar con retorno;
    - **perilla** con arrastre circular o vertical: modo relativo (manda `delta`) o absoluto;
    - **medidor** (barra con pico retenido);
    - **texto/reloj**.
  - Navegación de páginas por pestañas o deslizando.
  - Confirmación de acciones de aire (T54).
  - Modo pantalla completa (PWA con `manifest.json`), indicador de conexión y reconexión automática con estado completo al volver.
  - **Modo edición** (si el perfil lo permite): tocar una celda abre un panel para elegir tipo, texto, color y acción (de una lista que manda la app, filtrada por el perfil) y ligaduras. Se guarda vía la app.
- **MCP.** `botonera_presionar` (T57) simula el mismo camino, para probar.
- **GUI.** Es la propia página web. En el escritorio, "Abrir vista previa" muestra la misma página en una ventana para probar sin tablet.
- **Depende de:** T52, T53 y T54.

### T56 · Superficie de control: editor en el escritorio — P1
- **Estado: falta.**
- **Qué hacer.**
  - Página nueva `src/app/qml/pages/BotoneraPage.qml` con su entrada en `RielNavegacion.qml`.
  - Columna izquierda: perfiles y páginas. Centro: grilla editable con arrastrar y soltar, redimensionado de celdas, copiar y pegar.
  - Panel derecho:
    - tipo de control y estilo;
    - acción, con un selector de herramienta (búsqueda en el índice MCP), un formulario de argumentos generado del esquema de la herramienta y una plantilla `{{valor}}` para faders;
    - ligaduras, con un selector de tema del bus (T50) y vista del valor actual.
  - Botón "Probar". Plantillas de fábrica (T52). Importar y exportar perfiles (JSON).
  - Pestañas "Dispositivos" y "Auditoría" (T54).
  - "Vista previa" embebida de la web.
  - Ver también los dispositivos de hardware (T58): cada uno con su mapeo a una página.
- **MCP.** Ver T57. Lo que se edita acá se ve y se edita igual por MCP.
- **GUI.** Es esta página.
- **Depende de:** T52.

### T57 · Superficie de control: herramientas MCP — P1
- **Estado: falta.**
- **Qué hacer.** Crear `src/app/mcp/McpBotoneraTools.cpp`, con etiquetas en `McpEtiquetas.cpp` y ayuda en `McpHelp.cpp`. Herramientas:
  - `botonera_perfiles` (listar); `botonera_perfil_crear {nombre, rol?, herramientas_permitidas?[], desde_plantilla?}`; `botonera_perfil_editar`; `botonera_perfil_borrar`.
  - `botonera_paginas {perfil}`; `botonera_pagina_crear {perfil, nombre, columnas, filas}`; `botonera_pagina_editar`; `botonera_pagina_borrar`.
  - `botonera_celda_set {perfil, pagina, x, y, tipo, estilo?, accion?, valor?, estado?[]}`; `botonera_celda_borrar`; `botonera_celdas {perfil, pagina}` (con el estado renderizado actual).
  - `botonera_plantillas`; `botonera_importar {json}`; `botonera_exportar {perfil}`.
  - `botonera_dispositivos` (emparejados, conectados, de hardware T58, con perfil, página, IP y último uso); `botonera_dispositivo_asignar {dispositivo, perfil, pagina?}`.
  - `botonera_emparejar {perfil, vence?, nombre?}`: genera el código y el QR. La aprobación queda **en la GUI**, nunca por MCP.
  - `botonera_revocar {dispositivo|todos}`.
  - `botonera_presionar {perfil, pagina, x, y, accion: presionar|soltar|valor|girar, v?}`: prueba el camino completo con el origen `asistente`. Pasa por T02.
  - `botonera_auditoria {desde?, dispositivo?}`.
  - `botonera_estado`: servidor, URL, conexiones y TLS.
  Editar perfiles y permisos es "de configuración": en vivo pide confirmación y el asistente **no puede ampliar** `herramientas_permitidas` de un perfil (solo un humano).
- **GUI.** Lo de T56.
- **Depende de:** T52 y T54.

### T58 · Superficie de control: superficies de hardware vía plugin — P1
- **Estado: falta.** El bus de plugins existe, pero sin eventos de la app. Faltan bibliotecas (T51).
- **Qué hacer.**
  - **API de superficies para plugins** en `src/plugins/sdk/ba_plugin/superficie.py`:
    - `plugin.superficie.registrar(id, nombre, modelo, grilla={columnas, filas}, capacidades={teclas_lcd, tamano_px, perillas, faders, lcd_tira})`: registra un dispositivo de hardware como un **dispositivo de botonera más**, con perfil y página asignables desde T56 o T57;
    - `@plugin.superficie.al_estado(id)` recibe el estado renderizado por celda (texto, colores, ícono, valor, titilar);
    - `plugin.superficie.render_tecla(estado, tamano)` devuelve una imagen PIL lista (fondo, texto ajustado y borde de tally), para que cada plugin no reinvente el dibujo;
    - `plugin.superficie.presionar(id, x, y)`, `soltar`, `girar(id, x, y, delta)` y `valor(id, x, y, v)`.
  - Por dentro viaja por el canal stdio del plugin con mensajes nuevos (`{"t":"superficie_*"}`) en `Plugins.cpp`. El motor de T52 lo trata igual que una tablet, con el perfil asignado. Las acciones van con `CallContext{"botonera:<plugin>/<id>"}`.
  - **Plugins de ejemplo** (T51), escritos para que el asistente los adapte:
    - `superficie-streamdeck`: python-elgato-streamdeck, teclas LCD (72×72, 96×96 o 120×120 según el modelo), perillas y tira táctil del Stream Deck +;
    - `superficie-xkeys`: hidapi con el protocolo de P.I. Engineering, LEDs rojo y azul por tecla;
    - `superficie-loupedeck`: protocolo serie o WebSocket por USB; la comunidad lo documentó (por ejemplo `loupedeck` en npm o `python-loupedeck`), así que hay que verificar la licencia antes de vendorear;
    - `superficie-midi`: mido con mapeo de notas a botones y CC a faders o perillas; feedback por nota o CC (LEDs) y por SysEx o Mackie Control para faders motorizados; plantillas para Behringer X-Touch Mini y Korg nanoKONTROL;
    - `superficie-osc`: python-osc, para TouchOSC u Open Stage Control.
- **MCP.**
  - `botonera_dispositivos` incluye los de hardware (`origen: plugin`).
  - `botonera_dispositivo_asignar` sirve igual.
  - `dispositivos_plugin` (T51) los enumera para escribir uno nuevo.
- **GUI.** En Botonera → Dispositivos, los de hardware con ícono, modelo, perfil y página, más "Identificar" (hace titilar el aparato). En Plugins, la tarjeta de cada plugin de superficie.
- **Depende de:** T51, T52 y T50.

---

# P2 — mejora

### T59 · Compatibilidad con Bitfocus Companion — P2
- **Estado: falta.** No hay nada de Companion, OSC, TSL ni obs-websocket.
- **Qué hacer.** Dos caminos complementarios.
  1. **Servidor "Satellite API".** Es el protocolo con el que la app oficial **Companion Satellite** (y otros clientes satélite) conecta superficies físicas a un servidor Companion.
     - Es TCP en el puerto **16622**, con un protocolo de texto por líneas: `BEGIN`, `ADD-DEVICE DEVICEID=… PRODUCT_NAME=… KEYS_TOTAL=… KEYS_PER_ROW=… BITMAPS=<px> COLORS=… TEXT=…`, `KEY-PRESS DEVICEID=… KEY=… PRESSED=…`, `KEY-ROTATE …`, y del servidor `KEY-STATE DEVICEID=… KEY=… BITMAP=<base64 RGB> COLOR=… TEXT=<base64>`, `BRIGHTNESS`, `KEYS-CLEAR`, `PING`/`PONG`. Las versiones recientes también lo ofrecen por WebSocket (16623).
     - **Factibilidad: alta.** Es un protocolo de texto documentado por Bitfocus, con versión negociada en `BEGIN … ApiVersion=`. La app solo implementa el **lado servidor** (`src/app/control/SatelliteServidor.{hpp,cpp}`, o en el gateway): cada `ADD-DEVICE` se registra como dispositivo de botonera (T52), y cada `KEY-STATE` se genera del estado renderizado (dibujar el bitmap con el renderizador de teclas de T58 portado a C++ o pedido a un plugin).
     - Así, Companion Satellite, instalado por el operador en otra máquina o en una Raspberry Pi junto a los Stream Deck, Loupedeck, X-keys, Infinitton o paneles Blackmagic, maneja nuestra grilla **sin que escribamos drivers**, y reusa todo el soporte de superficies de Companion.
     - **Riesgos:**
       - el protocolo evoluciona: fijar la versión que se soporta y probar contra la versión de Satellite que se documente;
       - el satélite espera un "Companion" y no hay autenticación en el protocolo: escuchar solo en la LAN, con una **lista de IP permitidas** y emparejamiento manual (el primer `ADD-DEVICE` de una IP nueva pide aprobación en el escritorio, como T54), registrando todo en la auditoría.
     - No se distribuye Satellite con el bundle, así que no viola la regla portable: es software del operador en otro equipo.
  2. **API remota compatible y módulo Companion**, para quien ya usa Companion como centro de control:
     - una API HTTP local por perfil (`POST /api/botonera/<perfil>/pagina/<p>/celda/<x>/<y>/presionar`, con un token de dispositivo de T54) que el módulo "Generic HTTP" de Companion llama sin código;
     - más adelante, un módulo de Companion propio (Node.js, `companion-module-agora-broadcast`), mantenido fuera del bundle, que expone acciones y feedbacks a partir de `botonera_*` y los temas del bus.
     - Opcional: un puerto de compatibilidad con el protocolo TCP/UDP remoto de Companion (`LOCATION <página>/<fila>/<columna> PRESS`).
- **MCP.** Herramientas nuevas:
  - `botonera_satellite {activo, puerto: 16622, ip_permitidas[]}`;
  - `botonera_satellite_estado` (clientes y dispositivos);
  - `botonera_api_remota {activo, perfiles[]}`.
- **GUI.** En Botonera → Integraciones:
  - "Companion Satellite": interruptor, puerto, IP permitidas, clientes conectados y aprobación de dispositivos nuevos;
  - "API remota (HTTP)" con la URL de ejemplo para el módulo Generic HTTP de Companion y un botón para copiarla.
- **Depende de:** T52, T54 y T58.

### T60 · Plugins aprobados para el aire y modelo local en Windows y Linux — P2
- **Estado: falta.**
  - La única "firma" es la marca de dependencias de pip (`src/app/plugins/Plugins.cpp:452-480`).
  - `hayMotor()` devuelve falso fuera de Apple: "llama.cpp: todavía no viaja en el bundle" (`src/app/agent/ModelosLocales.cpp:242-256`).
- **Qué hacer.**
  - "Aprobar para el aire" fija un SHA-256 del directorio del plugin (sin `_libs` volátiles) en `Config/plugins-aprobados.json`. En modo en vivo (T48) solo arrancan los aprobados con el hash igual.
  - Modelo local en Windows y Linux: vendorear `llama.cpp` (CPU, CUDA o Vulkan según la plataforma) en el bundle, con el mismo `ModelosLocales`.
- **MCP.** Herramientas nuevas: `plugin_aprobar {id}` y `plugin_aprobados`. `modelo_local_estado` muestra el motor disponible por plataforma.
- **GUI.** En Plugins, una insignia "Aprobado para el aire" y el botón para aprobar (que muestra qué cambió si el hash difiere). En Asistente → Modelo local, el motor disponible también en Windows y Linux.
- **Depende de:** T48 y T51.

---

## Notas para la implementación

- **Orden sugerido.**
  - Semana 1-2: T01, T02, T03, T06, T07, T08, T10, T12, T20 (todas chicas).
  - Después: T11, T04 (con T50), T19, T14, T15, T09, T13 fase 1.
  - Luego: T16 → T17 → T18 → T22 → T21.
  - En paralelo, quien toque web: T50 → T52 → T53/T54 → T55 → T56/T57 → T51/T58.
- **Toda herramienta MCP nueva** lleva etiquetas en `src/app/mcp/McpEtiquetas.cpp`, ayuda en `McpHelp.cpp` y entrada en el índice (`McpIndice.cpp`). Las que tocan el aire se registran en la tabla de T02.
- **Todo lo que hace la GUI pasa por el mismo código que el MCP**: un ViewModel que llama a la misma función del motor, o `McpCore::callTool` con el origen `humano`. Así T02, la auditoría y el bus ven lo mismo venga de donde venga.
- **Hardware no probado:** PTZ real, Chromecast real, NDI alfa, sistema de conferencia y superficies. Cada plugin o función de hardware tiene un "simulador" para probar sin el equipo, y queda en la lista de chequeo de T05 hasta probarlo con el equipo del recinto.
