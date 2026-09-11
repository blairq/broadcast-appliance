# BROADCAST APPLIANCE PORTABLE v2 — ESPECIFICACIÓN DE ARQUITECTURA Y PROMPT DE IMPLEMENTACIÓN
### libobs embebido + Gateway WebRTC desacoplado + Supervisor

> **Cómo usar este documento.** Es la fuente de verdad del proyecto. En cada sesión con el modelo, pegá las secciones 1 a 16 como contexto fijo y agregá al final **una** tarea de la sección 17. No pidas el sistema completo en una sola respuesta: no entra, y el modelo empieza a inventar.

---

## 1. ROL Y REGLAS DE TRABAJO PARA EL MODELO

Actuá como arquitecto de software sénior y líder técnico en C++20 sobre Windows. Tenés experiencia práctica en:
- libobs (núcleo de OBS Studio, API C).
- Qt 6 Widgets.
- Windows Core Audio (IMMDevice, IAudioClient, loopback).
- Direct3D 11/DXGI y DirectShow/UVC.
- Configuration Manager (cfgmgr32).
- IPC de baja latencia (memoria compartida, named pipes, Spout2/SpoutDX).
- libwebrtc nativo.
- Procesamiento de audio en tiempo real.

Reglas no negociables al generar código:

1. **No inventes APIs.** libobs, Spout2, libwebrtc y cloudflared cambian entre versiones. Si no estás seguro de una firma, un flag o un comportamiento en la versión fijada (sección 14), escribilo igual, pero marcá la línea con `// VERIFY: <qué chequear y en qué header/doc>`. Al final de cada respuesta, listá todos los VERIFY.
2. **Código completo y compilable.** Nada de `// TODO: implementar` en caminos críticos. Si algo queda fuera del alcance de la tarea, dejá un stub explícito que falle de forma segura, loguee y esté documentado.
3. **Diseño antes que código.** Antes del código de cada módulo entregá:
   - la interfaz pública (`.hpp`);
   - el modelo de threading (qué thread invoca cada método o callback);
   - la estrategia de errores;
   - la lista de casos de prueba.
4. **Ambigüedades.** Si un requisito es ambiguo, contradictorio o técnicamente inviable, decilo y proponé una alternativa antes de codificar. Si asumís algo, listalo como supuesto.
5. **RAII y errores.** Todo recurso Win32/COM usa RAII (WIL: `wil::unique_handle`, `wil::com_ptr`, `wil::unique_event`, etc.). Verificá cada HRESULT. Ninguna excepción C++ puede cruzar un callback C (libobs, Win32, WASAPI, libwebrtc): capturala en el borde.
6. **Threads de audio en tiempo real.** Sin allocations, sin locks bloqueantes, sin I/O, sin logging sincrónico, con FTZ/DAZ activados. La comunicación con otros threads va por colas lock-free.
7. **Núcleo independiente.** El núcleo (`ba_core`) no depende de Qt ni de libobs y es testeable por unidad. Qt solo existe en la UI del Proceso A.
8. **Logging.** Todo camino de error deja un log estructurado (componente, código, contexto). Nunca se muestra un diálogo del sistema.

---

## 2. CONTEXTO, USUARIOS Y PRINCIPIOS

### 2.1 Qué es
Un sistema de emisión para conferencias y eventos. Contempla:
- disertante en atril, con lavalier o micrófono de mano;
- presentación por HDMI;
- de 1 a 4 invitados remotos por WebRTC;
- salida a YouTube.

Corre 100% desde un disco NVMe USB-C en un Windows 10/11 x64 ajeno, como usuario estándar, sin instalar nada ni pedir UAC.

### 2.2 Dos roles
- **Ingeniero (autor del sistema).** Prepara plantillas de escenas, presets DSP, perfiles de hardware y credenciales. Accede al "modo ingeniería" con PIN.
- **Operador (no técnico).** Usa solo la interfaz de operación. Nada de lo que el operador puede tocar debe permitirle desconfigurar el sistema de forma destructiva.

### 2.3 Principios
- **Degradación elegante.** Ninguna falla parcial (un USB, un invitado, el túnel, la red) detiene el vivo.
- **Sin diálogos del sistema ni ventanas emergentes.** Todo aviso es un banner dentro de la UI, con severidad.
- **Acciones destructivas con confirmación física.** Se ejecutan manteniendo presionado el botón.
- **Estado siempre visible y verificable.** La UI nunca muestra "OK" sin una medición que lo respalde.
- **Defaults seguros y restauración automática.**

### 2.4 Fuera de alcance (explícito)
- Refuerzo sonoro de micrófonos locales hacia el PA de la sala (motivo en 7.1). Los micrófonos llegan al PA por la consola del lugar.
- Cámara virtual, captura de juegos, scripting y obs-websocket.

### 2.5 Hosts objetivo
- **Sistema operativo:** Windows 10 22H2 y Windows 11 x64. Windows 10 ya no tiene soporte de Microsoft, pero los equipos de salones suelen tenerlo. Verificar que el tag de OBS fijado lo siga soportando.
- **GPU:** desconocida (NVIDIA, AMD, Intel o solo iGPU), incluyendo laptops híbridas.
- **Pantallas:** de 1366×768 a 4K, con escalado de 100 a 200 %.
- **Red del lugar:** posiblemente restrictiva (proxy, portal cautivo, solo puertos 80/443, reloj del sistema desfasado).

---

## 3. ARQUITECTURA DE PROCESOS

Tres procesos, cada uno con una responsabilidad y un radio de falla acotado.

### 3.1 Proceso 0 — `BroadcastAppliance.exe` (Launcher / Supervisor)

Es el único punto de entrada y vive en la raíz del disco. Está escrito en Win32 puro, con CRT estático (`/MT`), sin Qt ni DLLs propias: es la pieza más simple y, por lo tanto, la más robusta. Reemplaza al `Launch.bat` original. Un `.bat` abre una consola que el operador puede cerrar y hereda variables de entorno del host.

1. **Instancia única.** Usa el mutex `Local\BroadcastAppliance.Instance`. Una segunda ejecución solo trae la UI al frente.
2. **Integridad.** Verifica los archivos contra `manifest.json` (SHA-256, verificación paralela). Si un archivo está corrupto, muestra un mensaje claro en lugar de crashear.
3. **Entorno saneado para los hijos.**
   - Bloque de entorno explícito, sin `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH`, `QT_QPA_PLATFORM`, `QML2_IMPORT_PATH`, `PYTHONHOME` ni `PYTHONPATH`.
   - PATH mínimo: System32 más los directorios propios.
4. **Modo de error.** Antes de lanzar procesos llama a `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`; los hijos lo heredan. Resultado:
   - un crash termina el proceso de inmediato, en vez de quedar colgado en el diálogo de Windows Error Reporting;
   - si el disco desaparece, no aparecen diálogos de "inserte un disco".
5. **Copia sombra (decisión abierta D3, activada por defecto).**
   - Copia `bin/`, `obs-plugins/` y `data/` a `%TEMP%\BroadcastAppliance\<hash-de-versión>\` y ejecuta desde ahí. Si ya existe una copia con el mismo hash, la reutiliza.
   - Motivo: Windows pagina el código de los ejecutables bajo demanda desde el disco de origen. Si el cable USB se mueve, los procesos mueren con `EXCEPTION_IN_PAGE_ERROR`. Los flags `/SWAPRUN` no son confiables porque muchos NVMe USB se reportan como discos fijos.
   - Configuración, logs y grabaciones quedan en el disco.
   - La copia se borra en el cierre ordenado.
6. **Lanzamiento.** Lanza B y después A, ambos de subsistema GUI y sin consola. Intenta `CREATE_BREAKAWAY_FROM_JOB` por si el launcher corre dentro de un job ajeno; si falla, reintenta sin ese flag.
7. **Watchdog.**
   - Espera sobre los handles de proceso para detectar crashes.
   - Lee heartbeats en memoria compartida (4.4) para detectar cuelgues: un heartbeat sin actualizar por más de 5 s cuenta como cuelgue.
   - Ante un cuelgue: toma un minidump del proceso colgado (`MiniDumpWriteDump`), lo termina con `TerminateProcess` y lo reinicia.
8. **Política de reinicio.**
   - Backoff: 0 s, 1 s, 2 s, 5 s, con tope de 10 s.
   - Circuit breaker: si A crashea más de 5 veces en 2 minutos, arranca en "modo seguro" (sin fuentes de navegador ni overlays).
   - A, cuando se reinicia por crash, arranca con `--restore` (restauración automática sin preguntar).
9. **Si el launcher muere, A y B siguen funcionando.** Por eso no se los mete en jobs con `KILL_ON_JOB_CLOSE`. Al relanzar el launcher, readopta los procesos vivos a partir de los PID publicados en memoria compartida.
10. **Cierre ordenado.** Pide a A y a B que terminen por el canal de control, espera y recién entonces marca `clean_shutdown`.
11. **UI de recuperación mínima (Win32).** Solo es visible si A no está corriendo: muestra el estado y un botón "Reiniciar sistema".

### 3.2 Proceso A — `ba-app.exe` (Motor de emisión + UI de operación)

Es un frontend Qt 6 propio sobre **libobs sin modificar**, más un patch set mínimo (6.2). **No es un fork del frontend de OBS**: se compilan la librería y los plugins oficiales de un tag estable y se escribe una aplicación nueva que los usa.

Responsabilidades:
- captura de video (HDMI/UVC) y del audio de micrófonos para el stream;
- escenas y render del programa;
- encoding, salida RTMPS a YouTube y grabación de respaldo;
- salidas a proyector;
- UI del operador y modo ingeniería.

### 3.3 Proceso B — `ba-gateway.exe` (Gateway WebRTC + motor de conversación + túnel)

Es headless: tiene un ícono en la bandeja y una ventana de estado opcional. No usa Qt, para minimizar dependencias.

Responsabilidades:
- servidor de señalización y app web del invitado;
- sesiones WebRTC;
- **motor de audio de conversación sala↔remotos** (8.7);
- video de los invitados hacia A y video de programa hacia los invitados;
- supervisión de `cloudflared.exe`.

### 3.4 Matriz de fallas (criterio de aceptación)

| Evento | Comportamiento esperado |
|---|---|
| **A crashea o se cuelga** | Hueco en YouTube de ≤ 10 s (objetivo: 5 s). La conversación sala↔invitados **no se interrumpe**, porque vive en B. Los invitados ven la placa "Reanudando transmisión" en su retorno de video. A vuelve con `--restore` y se reengancha a los flujos IPC en < 1 s desde que termina su inicialización. |
| **B crashea** | Los invitados se desconectan y su página reintenta sola con backoff. A muestra una placa en los slots de invitados y un banner. El launcher reinicia B. |
| **cloudflared cae** | B lo reinicia. Las llamadas ya establecidas **siguen**, porque el medio no pasa por el túnel. Solo fallan las conexiones nuevas hasta que vuelva. |
| **El launcher muere** | A y B siguen. No hay watchdog hasta relanzarlo. |
| **Se desconecta un USB de audio o video** | Ver 9.6. El resto del sistema sigue. |
| **Cae la red** | La salida RTMP reintenta indefinidamente, la grabación local sigue y aparece un banner rojo. |
| **Se desconecta el disco del sistema** | *Con copia sombra:* A y B siguen. Snapshot y logs se mantienen en memoria y se reintentan. La grabación se pausa y retoma en un archivo nuevo cuando vuelve el disco (la letra de unidad puede cambiar). *Sin copia sombra:* caída total. |
| **Pérdida del dispositivo D3D (TDR o reset del driver)** | A reconstruye el device (libobs lo soporta) y reabre los receptores Spout. B recrea sus senders. |
| **Corte de energía** | Al volver, se ejecuta el flujo de restauración de 11.3. |

---

## 4. CONTRATOS DE IPC

Reglas comunes:
- Todos los objetos con nombre van en el namespace `Local\`. `Global\` exige un privilegio que un usuario estándar no tiene para crear file mappings.
- La DACL es explícita y restringida al usuario actual.

### 4.1 Control: named pipe
- **Nombre:** `\\.\pipe\BroadcastAppliance.Control`, con `PIPE_REJECT_REMOTE_CLIENTS`.
- **Formato:** mensajes con prefijo de longitud + JSON (o MessagePack), con campo `protocolVersion`.
- **Semántica:** request/response con `id`, más eventos asíncronos. Ambos lados reconectan solos.
- **Mensajes mínimos:**
  - `hello` con versión y heartbeat;
  - estado del túnel;
  - lista de invitados con estado y estadísticas;
  - admitir, rechazar, expulsar o mutear a un invitado;
  - cambiar sala y PIN;
  - estado de los flujos IPC;
  - apagado ordenado.

### 4.2 Video: Spout2 (SpoutDX, Direct3D 11)
- **API.** Usar la clase `spoutDX` del SDK de Spout2 (API DX11: abre con el device propio y envía o recibe texturas). **No** usar la API `SpoutSender`/`CreateSender`, que está basada en OpenGL. Enlazar el SDK estático; no hace falta `Spout2.dll`.
- **Senders:**
  - `BA.Guest.1` a `BA.Guest.4`, de B hacia A;
  - `BA.Program`, de A hacia B, para el retorno de video a los invitados.
- **Resolución fija por slot** (default 1280×720, opción 1920×1080). Nunca redimensionar un sender en caliente. B resuelve letterbox, pillarbox y rotación (celulares en vertical) al componer el frame dentro del slot.
- **Un sender existe siempre.** Si no hay invitado en ese slot, B publica una placa. A solo maneja un "sender ausente" si B está caído.
- **Misma GPU obligatoria.**
  - A elige el adaptador en el preflight (por LUID) y lo publica; B crea su device D3D11 sobre ese mismo LUID.
  - Ambos ejecutables exportan `NvOptimusEnablement` y `AmdPowerXpressRequestHighPerformance`, para laptops híbridas.
  - Si el sharing de texturas falla, se pasa a modo copia por CPU con una advertencia visible.
- **Metadatos por frame.** La textura no transporta timestamps, así que cada sender tiene un bloque de memoria compartida paralelo con contador de frame y timestamp QPC. A mantiene una cola de 2 a 4 texturas para poder retrasar el video y alinearlo con el audio.

### 4.3 Audio: ring buffers SPSC en memoria compartida
- **Mecanismo:** un ring por flujo, con `CreateFileMappingW`/`MapViewOfFile`.
- **Header versionado:**
  - magic y versión;
  - PID y *generation* del escritor (se incrementa en cada arranque);
  - sample rate (48 000) y cantidad de canales;
  - capacidad en frames (potencia de 2);
  - índices de escritura y lectura de 64 bits, en líneas de caché separadas, accedidos con `std::atomic_ref<uint64_t>` y un `static_assert` de lock-free;
  - QPC de la última escritura;
  - contadores de underrun y overrun.
- **Datos:** Float32 intercalado, en bloques de 480 frames (10 ms), cada uno con su timestamp QPC.
- **Reloj.**
  - Todos los flujos IPC se marcan al ritmo de QPC, que es el mismo reloj que usa libobs vía `os_gettime_ns`. Así no hay deriva entre A y B.
  - Se usan timers de alta resolución (`CreateWaitableTimerExW` con `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`).
  - Toda interfaz física (WASAPI) se adapta a ese reloj con resampling adaptativo (7.4).
- **Despertar al lector:** evento auto-reset opcional. El lector tiene que funcionar igual con polling cada 5 ms.
- **Recuperación.**
  - Si cambia la *generation* del escritor, el lector descarta lo que tiene, resincroniza y hace un fade-in de 10 ms.
  - Ante un underrun emite silencio con fade e incrementa el contador.
- **Latencia objetivo del ring:** 20 a 40 ms.
- **Flujos:**
  - `BA.Audio.Guest.<n>` (B→A): la voz de cada invitado, para el mix del stream.
  - `BA.Audio.ContentForReturn` (A→B): el audio de presentación y reproducción, para que lo escuchen los invitados.
  - `BA.Audio.RoomAEC` (B→A), opcional: los micrófonos de sala ya procesados con AEC.

### 4.4 Heartbeats
El bloque `BA.Health` en memoria compartida guarda, por proceso: PID, estado (enum), QPC del último latido (se actualiza cada 250 ms) y versión. Lo leen el launcher y los otros procesos.

---

## 5. PORTABILIDAD Y AISLAMIENTO DEL HOST

### 5.1 Layout del disco (formateado NTFS)
Espeja el layout portable de OBS, para que la resolución de rutas de libobs y sus plugins funcione sin trucos.

```
<raíz del disco>
├── BroadcastAppliance.exe            # Proceso 0: launcher/supervisor (único punto de entrada)
├── manifest.json                     # SHA-256 de binarios y datos (autoverificación)
├── bin/64bit/
│   ├── ba-app.exe                    # Proceso A
│   ├── ba-gateway.exe                # Proceso B (libwebrtc enlazado estático)
│   ├── cloudflared.exe               # versión fijada
│   ├── qt.conf                       # rutas relativas de plugins Qt
│   ├── obs.dll                       # libobs (en Windows la DLL se llama obs.dll)
│   ├── obs-frontend-api.dll          # la importa obs-browser
│   ├── libobs-d3d11.dll, w32-pthreads.dll, obs-ffmpeg-mux.exe
│   ├── avcodec/avformat/avutil/swscale/swresample-*.dll, libx264-*.dll, srt, rist, zlib…
│   ├── Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll, Qt6Svg.dll
│   ├── platforms/ styles/ imageformats/ iconengines/ tls/   # vía windeployqt
│   └── vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll, msvcp140_1.dll,
│       msvcp140_2.dll, msvcp140_atomic_wait.dll              # runtime VC++ app-local
├── obs-plugins/64bit/
│   ├── win-dshow.dll, win-wasapi.dll, obs-ffmpeg.dll, obs-outputs.dll, rtmp-services.dll,
│   │   obs-x264.dll, encoders HW (NVENC/QSV/AMF según el tag), obs-filters.dll,
│   │   image-source.dll, obs-text.dll, obs-transitions.dll
│   ├── obs-browser.dll, obs-browser-page.exe, libcef.dll, chrome_elf.dll, *.pak,
│   │   icudtl.dat, locales/ …        # CEF completo
│   └── ba-sources.dll                # módulo propio: fuentes IPC, channel strip, automixer
├── data/
│   ├── libobs/                       # *.effect (shaders). Sin esto libobs NO inicializa gráficos
│   └── obs-plugins/<plugin>/         # locale y datos de cada plugin (incluye ba-sources)
├── Web/guest/                        # app web del invitado (la sirve B)
├── Assets/                           # placas, clips de identificación de audio, tipografías
├── Overlays/lower_thirds/            # template.html, style.css, script.js
├── Media/                            # videos institucionales (opcional)
├── Config/
│   ├── engineer.json                 # plantillas de escenas, roles, hash del PIN de ingeniero
│   ├── dsp_presets.json              # esquema versionado
│   ├── hosts/<host-id>/hardware_map.json
│   ├── session/snapshot.A.json, snapshot.B.json   # doble generación
│   ├── secrets.json
│   └── plugin_config/                # module_config_path de libobs (incluye caché de CEF)
├── Logs/                             # app, gateway, cloudflared, launcher, dumps/
└── Recordings/
```

Los nombres exactos de las DLLs de OBS, FFmpeg y CEF son los que produzca el tag fijado. El script de deploy los toma del build, no de esta lista.

### 5.2 Carga de DLLs
- Al inicio de cada proceso: `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` y `AddDllDirectory` solo para los directorios propios. Nunca depender del PATH.
- `qt.conf` junto a `ba-app.exe`, con rutas relativas de los plugins de Qt.
- Desplegar con `windeployqt`, que incluye `tls/qschannelbackend.dll`. Sin ese plugin, Qt Network no hace TLS.

### 5.3 Rutas
- Todas se derivan de `GetModuleFileNameW`, porque la letra de unidad cambia entre equipos.
- Deben soportar espacios y caracteres no ASCII: UTF-16 en Win32, UTF-8 hacia libobs.

### 5.4 Persistencia solo en el disco
- Llamar a `obs_startup(locale, <disco>\Config\plugin_config, ...)`, para que los plugins (incluida la caché de CEF de obs-browser) escriban ahí.
- `QSettings` solo en formato INI con ruta explícita. El formato nativo escribe en el Registro.
- Nada de registro COM, servicios, drivers, tareas programadas ni reglas de firewall.

### 5.5 Huella residual honesta
Windows y los drivers escriben cosas que la app no controla: cachés de shaders D3D, NVIDIA y AMD, volumen por aplicación del mezclador de Windows, Prefetch y listas de saltos. Se documentan y se aceptan.

**Criterio de aceptación:** una auditoría con Process Monitor (Sysinternals) no muestra escrituras *de la app* fuera del disco, salvo la lista documentada y la copia sombra. Incluir en esa auditoría a Spout2 y a CEF.

### 5.6 Red sin avisos de firewall
Ningún proceso escucha en interfaces que no sean loopback. El aviso del Firewall de Windows necesita permisos de administrador para "Permitir", así que evitarlo es obligatorio. La señalización de B escucha en `127.0.0.1`, y cloudflared se conecta ahí.

### 5.7 Comportamientos de Windows a neutralizar (sin admin)
- `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)` mientras haya un evento activo.
- `SetProcessInformation(ProcessPowerThrottling)` para desactivar EcoQoS y el ignorado de resolución de timer. Windows 11 degrada los timers de procesos minimizados u ocultos, y B no tiene ventana visible.
- MMCSS (`AvSetMmThreadCharacteristicsW(L"Pro Audio")`) en los threads de audio.
- Prioridad de proceso `ABOVE_NORMAL` en A y B.
- Streams WASAPI con categoría de medios, nunca de comunicaciones. Windows atenúa el resto del audio cuando detecta actividad de comunicaciones.
- Capturar en modo RAW (`AUDCLNT_STREAMOPTIONS_RAW`) cuando el endpoint lo soporte, para esquivar las "mejoras" de audio del fabricante.

### 5.8 Firma y políticas del host
- Firmar todos los `.exe` y `.dll` propios: reduce los bloqueos de SmartScreen y los falsos positivos de antivirus.
- Documentar dos riesgos:
  - equipos con AppLocker o WDAC pueden bloquear la ejecución desde USB;
  - cloudflared a veces es marcado por EDR corporativos.
- Procedimiento obligatorio: probar el equipo del lugar antes del evento.

### 5.9 Disco
- Hardware: NVMe en gabinete USB 3.2, formateado NTFS. exFAT no tiene journaling y no garantiza renombres atómicos ante un corte de energía.
- Al iniciar se verifican: sistema de archivos, espacio libre y velocidad de escritura (prueba rápida).

---

## 6. PROCESO A — LIBOBS EMBEBIDO

### 6.1 Build y plugins
- obs-studio es un submódulo git fijado a un tag estable. Se compila con sus presets de CMake y con el frontend de OBS desactivado.
- Nuestra app consume `libobs` como paquete CMake.
- `obs-frontend-api.dll` se despliega porque obs-browser la importa, pero nuestra app no registra callbacks de frontend (VERIFY: comportamiento de obs-browser en ese caso).

| | Plugins |
|---|---|
| **Incluidos** | win-dshow, win-wasapi, obs-ffmpeg (+ `obs-ffmpeg-mux.exe`), obs-outputs, rtmp-services, obs-x264, encoders de hardware del tag, obs-filters, image-source, obs-text, obs-transitions, obs-browser (+ CEF + `obs-browser-page.exe`) y nuestro `ba-sources.dll` |
| **Excluidos** | win-capture, obs-virtualcam (requiere admin para registrarse), obs-scripting, obs-websocket, obs-vst |

### 6.2 Patch set mínimo sobre libobs ("fork fino")
Cada patch va documentado y debe poder rebasearse sobre versiones nuevas de libobs.

- **P1 — Hook de procesamiento por mix.**
  - Es un callback por pista, registrado en `audio_output`.
  - Se invoca con el buffer float planar de la mezcla, antes del clamp y antes de entregarlo a encoders y callbacks.
  - Motivo: libobs no tiene efectos por bus, solo filtros por fuente, y el bus de YouTube necesita leveler y limitador true-peak.
- **Alternativa sin patch** (si se prefiere libobs 100 % vanilla): una "bus source" que toma la mezcla N por el callback de audio crudo, la procesa y la reinyecta como fuente async en una pista dedicada. Cuesta +1 tick de latencia.

### 6.3 Video

**Canvas y escenas**
- Canvas 1920×1080 a 30 fps por defecto (60 opcional), NV12, Rec.709 rango parcial.
- Device D3D11 sobre el adaptador elegido en el preflight.
- Escenas generadas desde plantillas del ingeniero, no editables por el operador:
  - ESPERA (placa)
  - DISERTANTE
  - PRESENTACIÓN
  - SPLIT (disertante + slides)
  - INVITADO
  - DISERTANTE + INVITADO
  - PRESENTACIÓN + INVITADO (PiP)
  - VIDEO (desde `Media/`, opcional)
  - CIERRE
- Transición: fade de 300 ms.

**Capturadoras**
- Configuración determinística: resolución, fps y formato explícitos por dispositivo. Nunca "default del dispositivo".
- Preferir NV12/YUY2 en USB 3, y MJPEG con decodificación por hardware en USB 2. Buffering desactivado.
- El audio embebido del HDMI se captura con la misma fuente dshow, para compartir base de tiempo con el video.
- Offset de sincronía A/V por dispositivo (`obs_source_set_sync_offset`), editable en modo ingeniería, con un asistente opcional de "aplauso".

**Pérdida de señal**
- Cada slot de captura es una escena anidada: placa "SEÑAL INTERRUMPIDA" debajo, captura encima.
- Un watchdog detecta la ausencia de frames nuevos por más de 500 ms, oculta la captura y levanta una alerta en la UI.
- Si una fuente llega en negro con señal presente, mostrar el diagnóstico "posible contenido protegido (HDCP)".

**Salidas a pantallas**
- Ventanas sin borde a pantalla completa, con un `obs_display` por rol: proyector de sala (programa o presentación limpia) y monitor de confianza para el disertante.
- Reaccionar a `screenAdded`/`screenRemoved` y reubicar las ventanas cuando vuelve un monitor.
- Opción de forzar la topología "Extender" con `SetDisplayConfig`.

**Zócalos**
- Fuente obs-browser con una plantilla local.
- Actualización instantánea con el proc handler `javascript_event` de obs-browser (calldata `eventName` + `jsonString`). La página lo recibe como `CustomEvent`. Sin polling de archivos.
- `data.json` queda solo para persistencia.

### 6.4 Encoding y salida
- **Encoder.** Selección automática en el preflight, con una prueba real de 3 s en este orden: NVENC → QSV → AMF → x264 (preset veryfast).
- **Parámetros.** H.264 CBR, keyframe cada 2 s, perfil High. Bitrate según canvas y ancho de banda medido (VERIFY contra las recomendaciones vigentes de YouTube).
- **Salida.**
  - RTMPS por defecto: `rtmps://a.rtmps.youtube.com:443/live2`.
  - RTMP por el puerto 1935 como fallback.
  - Reintentos infinitos con demora corta.
  - Bitrate dinámico ante congestión activado.
- **Grabación de respaldo.**
  - Graba el programa, siempre activa durante el vivo, en `Recordings/`.
  - Contenedor tolerante a crash: MKV o Hybrid MP4, según el tag.
  - Aviso si el espacio libre alcanza para menos de 2 h.

### 6.5 Audio del stream
libobs corre a 48 kHz estéreo. Cada entrada es una fuente libobs con un filtro "channel strip" propio (sección 7). Los buses son pistas de mezcla de libobs:

| Pista | Bus | Contenido |
|---|---|---|
| 1 | YouTube + grabación | Mezcla completa del programa |
| 2 | Sala/PA | **Solo contenido**: presentación, videos, placas con audio. **Nunca micrófonos locales.** La voz de los invitados al PA la maneja B (8.7). |
| 3 | Contenido para el retorno | Va al ring `BA.Audio.ContentForReturn` |
| 4 | Monitoreo del operador | Programa, o PFL de la fuente seleccionada |
| 5–6 | Reservadas | — |

**Salidas físicas de bus (PA y monitoreo)**
- Clientes WASAPI propios, alimentados por el callback de audio crudo de libobs por pista (VERIFY nombre y firma en el tag).
- Resampler adaptativo controlado por el nivel de buffer, porque el reloj de la placa de audio no es el de libobs.
- **No** usar el monitoreo de libobs: admite un único dispositivo global.

**Bus YouTube (vía P1)**
- Leveler de loudness para voz, con objetivo configurable (default −16 LUFS integrado).
- Limitador true-peak a −1 dBTP.
- Medición LUFS momentánea y de corto plazo en la UI.

### 6.6 UI del operador (Qt 6 Widgets, una sola ventana)
Se mantiene el layout del spec original: barra superior, panel de ingesta, matriz de escenas, monitor de salida con faders y panel de emergencia. Cambios:

**Layout**
- Layout alternativo compacto para 1366×768.
- Probar con escalado de 100, 125, 150 y 200 %.

**Barra superior**
- estado de hardware y CPU/GPU;
- frames perdidos (render, encoder y red) y bitrate real;
- LUFS del programa y estado REC;
- espacio en disco;
- estado de red, túnel e invitados;
- hora.

**Botones críticos**

| Botón | Comportamiento |
|---|---|
| **INICIAR VIVO** | Habilitado solo con el preflight en verde (o con override por PIN de ingeniero). Mantener presionado 2 s. |
| **DETENER VIVO** | Mantener presionado 3 s. |
| **PÁNICO** | **No corta la salida RTMP**: cortarla puede terminar la emisión en YouTube. Silencia los buses de YouTube y PA y pasa el programa a la placa ESPERA con una transición corta. Salir del pánico: mantener 2 s. |
| **Cerrar la app** (Alt+F4 o botón) | Interceptado. Requiere mantener presionado y, si hay un vivo en curso, el PIN. |

**Sin menús, docks ni `QMessageBox`.** Todos los avisos son banners in-app con severidad.

**Confirmación de emisión real, en tres niveles**
1. Siempre: estadísticas locales de la salida (bytes enviados, congestión, frames perdidos).
2. Opcional: el `healthStatus` del stream según la YouTube Data API. Requiere OAuth; el refresh token se guarda en el disco.
3. Opcional: el reproductor público embebido de YouTube, dentro de una fuente de navegador oculta que se muestra en un display secundario. No hace falta libVLC.

Hay que documentar dos cosas: el nivel 3 tiene entre 5 y 20 s de retraso, y la URL HLS de YouTube **no** se obtiene a partir de la stream key.

**Modo ingeniería (con PIN)**
Da acceso a patchbay, presets, mapeo de hardware, offsets de sincronía, encoder y plantillas de escenas.

### 6.7 Patchbay (`QGraphicsScene`/`QGraphicsView`)
- Visualiza y edita el ruteo efectivo, respaldado por máscaras de pistas de libobs: entrada → channel strip → buses 1 a 6.
- La UI no permite dibujar topologías que libobs no puede representar (bus a bus, sends).
- **Operador:** ve una matriz simplificada (solo lectura o toggles acotados).
- **Ingeniero:** edita con cables bezier.
- Si un dispositivo se desconecta, sus cables pasan a rojo punteado y el resto del grafo sigue procesando.

---

## 7. DSP DE AUDIO

### 7.1 Por qué los micrófonos locales no van al PA a través de la PC
La cadena captura WASAPI → buffering de libobs → render WASAPI suma una latencia de decenas a cientos de milisegundos. Para refuerzo sonoro en sala eso es inaceptable: el disertante escucharía su propia voz con eco.

**Regla:** los micrófonos entran a la consola del lugar y de ahí al PA. A la PC llegan por una salida auxiliar de la consola, o por una interfaz USB en paralelo.

### 7.2 Channel strip nativo
Es un filtro libobs dentro de `ba-sources.dll`: un único filtro por entrada, RT-safe, con suavizado de parámetros para cambiar de preset sin clicks. La cadena es:

**HPF** (12, 18 o 24 dB/oct) → **EQ paramétrico de 4 bandas** (biquads RBJ) → **de-esser** (compresión de banda partida, sidechain entre 5 y 9 kHz) → **expansor/gate** (con histéresis y hold) → **compresor** (RMS o peak, knee suave) → **limitador** (con lookahead)

### 7.3 Presets
Son valores iniciales. El ingeniero los ajusta en `dsp_presets.json`, que tiene esquema versionado.

**Atril / cuello de cisne**
- HPF entre 100 y 120 Hz, 18 dB/oct.
- Corte suave de −2 a −3 dB entre 250 y 400 Hz, contra el sonido "encajonado".
- Compresor 2,5:1, knee suave, attack 10 ms, release 150 ms.
- *Eliminado:* el "notch adaptativo" de 3 bandas fijas. Unos notches fijos no se adaptan a nada y dañan la voz sin motivo. La realimentación es un problema del PA, no del stream.

**Corbatero / lavalier**
- HPF entre 80 y 100 Hz.
- De-esser entre 5 y 8 kHz.
- Expansor 1:2, umbral −45 dBFS, rango −12 dB. Un gate duro corta sílabas.
- Compresor 3:1.
- Opcional: shelving de agudos de +2 dB, para compensar la ubicación en el pecho.
- *Eliminado:* el compresor multibanda, innecesario para voz.

**Micrófono de mano**
- HPF 100 Hz.
- Low-shelf de −4 dB en 200 Hz, contra el efecto de proximidad.
- Compresor 3:1.
- Limitador a −1 dBFS.

**Mesa redonda**
- Automixer por reparto de ganancia (estilo Dugan) entre N micrófonos: la ganancia de cada canal es su energía suavizada dividida por la suma de las energías.
- Incluye "último micrófono abierto" y atenuación según el número de micrófonos abiertos.
- Se implementa como un objeto compartido entre los filtros de los micrófonos (tabla de niveles lock-free), no como plugin.

### 7.4 Resampling adaptativo
Todo cruce entre el reloj de una placa de audio y el reloj QPC/libobs pasa por un resampler de razón variable. La razón la controla el nivel de llenado del buffer (un PI suave), y el resampler es de alta calidad (speexdsp o libsamplerate).

### 7.5 VST3 y LSP (opcional, fuera de la v1)
- **Estado actual:**
  - El SDK de VST 3.8 es MIT, así que el host se puede implementar sin conflictos de licencia.
  - OBS no tiene VST3 nativo; hay un PR abierto que sirve de referencia.
  - LSP tiene builds de Windows (VST2, VST3, LV2, CLAP) como servicio pago por plugin en lsp-builds.com. También se puede compilar desde el código LGPL-3.0.
  - El formato VST3 en Windows es un bundle `.vst3` (una carpeta), no un archivo suelto.
- **Riesgo:** hostear plugins de terceros dentro de A contradice el aislamiento de fallas, porque un plugin que crashea tira el vivo.
- **Si se incorpora:** solo en modo ingeniería, preferentemente en un proceso host separado, sin editores gráficos (solo parámetros y presets), y con el escaneo fijado a `assets/audio-plugins/`.

---

## 8. PROCESO B — GATEWAY WEBRTC, CONVERSACIÓN Y TÚNEL

### 8.1 Stack WebRTC (decisión abierta D4)
- **Recomendado:** libwebrtc nativo con binarios precompilados y fijados (por ejemplo, el proyecto webrtc-build de Shiguredo). Lo justifican AEC3, NetEq, el control de congestión y su robustez probada en producción.
- **Alternativa:** GStreamer, con `webrtcbin`, decodificación D3D11 por hardware y `webrtcdsp` para el AEC.

### 8.2 Topología
- Hasta 4 invitados, en estrella con B como hub.
- Cada invitado sube un solo flujo. B le envía a cada uno el retorno de audio N−1 y el video de programa a bitrate bajo.
- **Presupuesto de subida del lugar** = YouTube + N × retorno. Ejemplo: 6 Mbps + 2 × 1 Mbps.
- Opción futura, si N crece: un SFU en la nube (por ejemplo, Cloudflare Realtime).

### 8.3 Señalización y app del invitado

**Servidor**
- HTTP + WebSocket embebido (por ejemplo, Boost.Beast o uWebSockets), escuchando en `127.0.0.1:<puerto>`.
- Se expone con un **túnel con nombre** de Cloudflare, en un hostname fijo (`https://conferencia.<dominio>/`). Así el link y el QR son estables y se pueden imprimir de antemano.
- Los quick tunnels (URL aleatoria) quedan solo para pruebas.

**Seguridad**
- Sala + PIN → token corto firmado con HMAC.
- Límite de intentos de PIN, con bloqueo.
- Tamaño máximo de mensaje y validación de origen.

**Sala de espera**
El operador admite a cada invitado desde la UI de A. La admisión automática es configurable.

**App web del invitado** (`Web/guest/`, en español, mobile-first)
- chequeo previo de cámara y micrófono, con medidor de nivel;
- recomendación de usar auriculares;
- restricciones de captura (máximo 720p30);
- reconexión automática con backoff;
- manejo de la orientación vertical;
- indicador "estás al aire";
- recepción del retorno de video y de audio.

### 8.4 Transporte de medios
- **El túnel lleva HTTP/WebSocket (la señalización), no el tráfico UDP de WebRTC.**
- Los medios usan ICE con STUN + TURN (Cloudflare Realtime TURN, o un TURN propio).
- B genera credenciales efímeras y las entrega por la señalización.
- Incluir TURN sobre TLS en el puerto 443 como último recurso para redes restrictivas.
- Filtrar las URLs del puerto 53: los navegadores las bloquean y hacen esperar un timeout.
- Opción de forzar relay (oculta las IPs y hace el comportamiento predecible).

### 8.5 Video

**Invitados hacia A**
- Decodificación con los decoders de libwebrtc. Opcional: una factory con decodificación por hardware (D3D11/Media Foundation).
- Composición en la textura fija del slot, sobre el adaptador compartido, aplicando letterbox y rotación CVO.
- Salida al sender Spout `BA.Guest.<n>`. Si no hay invitado, placa.

**Retorno de video a los invitados**
- B recibe `BA.Program` desde A, lo escala a 720p o 360p y lo codifica.
- Si A no está corriendo, B genera la placa "Reanudando transmisión".

### 8.6 Audio de los invitados hacia el stream
Cada voz decodificada se escribe en su ring `BA.Audio.Guest.<n>`, que A usa en el mix de YouTube.

### 8.7 Motor de conversación
Vive en B por dos razones: sobrevive a un crash de A y tiene baja latencia.

- **Captura de micrófonos.** B captura directamente los micrófonos de la sala, en WASAPI compartido, con los mismos dispositivos que A según el hardware map. El modo compartido admite varios clientes sobre el mismo dispositivo.
- **Procesamiento.** Usa WebRTC APM (AEC3 + supresión de ruido + AGC2).
  - La referencia del AEC es la **captura loopback del endpoint del PA**: todo lo que suena en la sala, venga de A o de B.
  - El mix-minus elimina el camino eléctrico. El AEC elimina el camino acústico PA → micrófono, que el mix-minus no puede resolver.
- **Reproducción.** B reproduce la voz de los invitados directamente en el endpoint del PA, en WASAPI compartido; Windows la mezcla con el contenido que envía A. Opcionalmente también la reproduce en el monitoreo del operador.
- **Retorno por invitado (N−1):** micrófonos de sala con AEC + `BA.Audio.ContentForReturn` (si A está vivo) + los demás invitados, sin el propio invitado.
- **Reloj.**
  - El AudioDeviceModule propio de libwebrtc se marca con un timer QPC de alta resolución (10 ms), no con el reloj de una placa.
  - Los endpoints físicos pasan por resamplers adaptativos (7.4).

### 8.8 Supervisión de cloudflared
- **Job Object** con `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`: si B muere, el túnel muere con él.
- **Token** por la variable de entorno `TUNNEL_TOKEN` del proceso hijo, no por línea de comandos (ahí es visible en el listado de procesos).
- **Configuración.** El túnel se administra de forma remota, así que no hace falta `cloudflare_tunnel.yml`.
- **Flags:**
  - sin autoactualización (el binario del disco solo se actualiza con un release controlado);
  - métricas en loopback;
  - log a `Logs/`, nivel info.

  VERIFY: sintaxis exacta en la versión fijada.
- **Salud.** Endpoint `/ready` del servidor de métricas (VERIFY) más parseo de logs. Reinicio con backoff.
- **Salida estándar.** stdout y stderr se redirigen por pipes al log, con `CREATE_NO_WINDOW`.
- **Preflight de puertos.** cloudflared necesita salida al puerto **7844**, por UDP (QUIC) o por TCP (HTTP2). Si la red lo bloquea, el túnel no levanta aunque el 443 esté abierto. El preflight tiene que detectarlo y avisar con un diagnóstico claro. Opción de forzar http2.

### 8.9 Estadísticas y UI de B
- Estadísticas por invitado: RTT, jitter, pérdida, bitrate, codec y tipo de candidato (host, srflx o relay). Se envían a A por el canal de control.
- B no tiene ventana principal. Su estado se ve en A, y el operador opera una sola interfaz.

---

## 9. GESTIÓN DE HARDWARE (`ba_core`)

Se separa en cinco módulos, sin dependencia de Qt:
- `AudioEndpointEnumerator`
- `DeviceIdentityResolver`
- `DeviceMatcher`
- `DeviceWatcher`
- `TestToneGenerator`

La UI recibe eventos de alto nivel por una cola. Esto reemplaza al `HardwareManager` monolítico del spec original.

### 9.1 Enumeración de audio
- `IMMDeviceEnumerator` sobre los endpoints activos de render y de captura.
- Datos por endpoint: formato de mezcla, soporte de modo RAW y form factor.
- Excluir, o al menos advertir, los dispositivos Bluetooth: tienen latencia alta y cambian de perfil (HFP/A2DP).

### 9.2 Identidad física
WASAPI no expone el Device Instance Path del dispositivo USB. Hay que resolverlo con cfgmgr32: tomar el ID de instancia del endpoint y subir por el árbol de devnodes (`CM_Locate_DevNodeW`, `CM_Get_Parent`) hasta llegar al dispositivo USB.

Datos a guardar por dispositivo:
- ID del endpoint;
- `DEVPKEY_Device_ContainerId`: agrupa las funciones del mismo aparato físico, lo que permite vincular automáticamente el audio embebido de una capturadora con su video;
- ID de instancia del padre USB y VID/PID/MI;
- número de serie, solo si es único (capacidad UniqueID);
- `DEVPKEY_Device_LocationPaths`;
- friendly name y driver.

### 9.3 Video
Se enumera igual que lo hace win-dshow y se guarda la misma identidad. El device path de dshow contiene el ID de instancia.

### 9.4 Matching con puntaje
Se usa al reconectar un dispositivo o en un equipo distinto. De mayor a menor puntaje:
1. ID de endpoint exacto;
2. número de serie;
3. VID/PID + location path;
4. VID/PID único entre los dispositivos no asignados;
5. si nada coincide: preguntar al operador con un banner no bloqueante.

Muchas capturadoras e interfaces baratas no reportan un serial único. Dos iguales solo se distinguen por el puerto, y los puertos cambian de un equipo a otro.

### 9.5 Perfiles por host
- Se guardan en `Config/hosts/<host-id>/hardware_map.json`.
- El host-id se deriva del UUID SMBIOS (`GetSystemFirmwareTable`, sin Registro).
- Si el host ya es conocido, el wizard ofrece reutilizar el mapeo.

### 9.6 Hot-plug

**Notificaciones**
- Audio: `IMMNotificationClient`. Sus callbacks no bloquean y nunca registran ni desregistran desde un callback; el trabajo se deriva a un worker.
- Video y USB en general: `CM_Register_Notification` sobre las clases de interfaz relevantes. No requiere ventana.
- `WM_DEVICECHANGE` queda solo como respaldo.
- Debounce de 300 a 1000 ms, porque los dispositivos USB se re-enumeran en ráfagas.

**Desconexión**
- Video: la placa del slot queda visible.
- Audio: silencio con fade, sin detener el reloj de libobs.
- En el patchbay, cables rojos punteados. En la UI, un banner.

**Reconexión**
- Matching (9.4) y reasignación automática.
- Coordinar con la lógica de reconexión que ya traen win-wasapi y win-dshow en el tag fijado (VERIFY), en lugar de pelear con ella.

### 9.7 Tono de prueba
- `IAudioClient` en modo compartido, event-driven, con `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`.
- Default: 1 kHz, **−20 dBFS**, 1,5 s, con rampas de 20 ms. El −12 dBFS del original es muy fuerte en el PA de una sala.
- Alternativa recomendada para operadores: un clip de voz pregrabado que dice el nombre de la salida.

### 9.8 Identificación por desconexión
Es el método principal para dispositivos gemelos:
1. La UI pide: "Desenchufá la capturadora que querés usar como ENTRADA A".
2. El watcher detecta cuál desapareció y hace la asignación.
3. La UI pide: "Volvé a enchufarla".

Como complemento se puede identificar por contenido, mirando los previews en vivo. El cartel sobreimpreso en el preview del spec original no identifica el aparato físico, así que se reemplaza por este método.

---

## 10. ASISTENTE DE ARRANQUE (PREFLIGHT)

El asistente bloquea la operación hasta completarse, salvo que se elija restaurar un snapshot. Cada paso se valida, y con ítems en rojo no se avanza sin el PIN de ingeniero.

**Paso 0 — Equipo y red**

*Equipo*
- versión de Windows;
- alimentación (advertir si está a batería);
- adaptador de video (LUID) y prueba de encoder;
- disco: sistema de archivos, espacio libre y escritura;
- hora del sistema, comparada con el header `Date` de una respuesta HTTPS. TLS, RTMPS y cloudflared fallan con un reloj desfasado.

*Red*
- DNS y detección de portal cautivo;
- handshake TLS contra la ingesta RTMPS de YouTube (443) y conexión TCP al 1935;
- cloudflared al puerto 7844, por TCP y UDP;
- TURN por el 443;
- estimación del ancho de banda de subida.

*Host:* reconocer si es un perfil conocido.

**Paso 1 — Audio**
- Endpoints listados con su identidad física.
- **Salidas:** botón de tono o clip de voz, y asignación de rol (PA sala, monitoreo operador).
- **Entradas:** medidor peak/RMS reactivo (el fondo parpadea en verde al detectar señal), rol y preset (Mic atril, Mic corbatero, Mic mano, Audio PC).
- Identificación por desconexión para dispositivos gemelos.

**Paso 2 — Video**
- Capturadoras con preview a 10 fps.
- Formato elegido automáticamente y verificado midiendo los fps reales.
- Identificación por desconexión o por contenido.
- Vinculación automática del audio embebido por ContainerId.

**Paso 3 — Pantallas**
- `QGuiApplication::screens()`.
- En cada monitor, un número gigante y su resolución durante 3 s.
- Asignación de roles: operador, proyector de sala, monitor de confianza.
- Opción de forzar la topología "Extender".

**Paso 4 — Evento y salidas**
- Título, disertante e institución, con preview del zócalo.
- YouTube: stream key y prueba RTMPS.
- YouTube, opcional: OAuth y una prueba real con el evento en vista previa (auto-inicio desactivado).
- Sala WebRTC:
  - ID de sala y PIN;
  - URL fija y código QR (generador tipo qrcodegen de Nayuki, licencia MIT);
  - link de prueba para un invitado.
- Resumen final, todo en verde.

---

## 11. PERSISTENCIA, RESTAURACIÓN Y DIAGNÓSTICO

### 11.1 Modelo de estado
Hay un único modelo de estado, que es la fuente de verdad y tiene `schemaVersion`. El snapshot es su serialización.

### 11.2 Escritura del snapshot
- **Debounce:** como máximo una escritura cada 500 ms, porque mover un fader no puede generar cientos de escrituras. Los eventos críticos (iniciar o detener el vivo, cambio de escena) se escriben de inmediato.
- **Secuencia:** archivo temporal → `FlushFileBuffers` → `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
- **Dos generaciones alternadas** (`snapshot.A.json` y `snapshot.B.json`), cada una con número de secuencia y checksum. Al leer, se toma la más nueva que sea válida.
- Si el disco no está, el estado se mantiene en memoria y la escritura se reintenta.
- `clean_shutdown: true` se escribe solo en un cierre ordenado.

### 11.3 Flujos de restauración

**Crash o cuelgue de A** (lo detecta el launcher): restauración automática, sin preguntar.

**Arranque tras un cierre no limpio (corte de energía)**
- Aparece un modal prominente: "SE DETECTÓ UN CORTE. RESTAURANDO LA TRANSMISIÓN EN 15 s".
- La cuenta regresiva restaura por defecto, y hay un botón Cancelar.
- Así no depende de que alguien esté frente a la PC para presionar Enter.

**Qué se restaura**
- asignación de dispositivos (con matching);
- escenas y escena activa;
- volúmenes y ruteo;
- zócalo;
- túnel y sala;
- el vivo, con la misma stream key.

**Objetivo medido:** programa al aire en ≤ 10 s desde que arranca A (objetivo: 5 s). El tiempo de arranque de Windows después de un corte no cuenta.

**Del lado de YouTube:** documentar la configuración del evento (auto-stop desactivado, stream key persistente) para que una reconexión continúe la misma emisión. VERIFY: opciones actuales de YouTube Studio.

### 11.4 Diagnóstico
- **Minidumps propios** (`MiniDumpWriteDump`) en `Logs/dumps/`, generados desde dos lugares: un filtro de excepciones no manejadas, y el launcher cuando detecta un cuelgue.
- **Logs rotativos** por proceso.
- **PDBs** de cada build guardados fuera del disco, para poder simbolizar los dumps.
- **Botón "Exportar diagnóstico"** en modo ingeniería: genera un zip con logs y configuración, sin secretos.

---

## 12. SEGURIDAD Y LICENCIAS

**Secretos**
- Stream key, token del túnel, credenciales TURN y refresh token de OAuth se guardan en `Config/secrets.json`.
- Decisión abierta D7:
  - *Default:* en claro, con credenciales revocables y de alcance mínimo.
  - *Alternativa:* cifrado con una passphrase que el ingeniero ingresa al iniciar (Argon2id o PBKDF2 + AES-GCM, vía CNG).
- DPAPI no sirve, porque está atado al usuario y al equipo.

**Endurecimiento**
- Pipes y memoria compartida con DACL restringida al usuario actual.
- Endpoint público de señalización con rate limit, expiración de tokens, límites de tamaño y validación de origen.

**Licencias**
- libobs y obs-browser son GPLv2+. Entregar el disco a terceros es distribución y obliga a ofrecer el código fuente.
- Registrar además: Qt (LGPL), CEF (BSD), Spout2 (BSD), cloudflared (Apache 2.0), libwebrtc (BSD), LSP (LGPL-3.0, si se usa) y el SDK de VST3 (MIT).

---

## 13. PRUEBAS Y CRITERIOS DE ACEPTACIÓN

**Unitarias (`ba_core`)**
- ring buffer: concurrencia, wraparound y reinicio del escritor;
- snapshot con inyección de fallas: corte simulado en cada paso de la escritura;
- matching de dispositivos;
- DSP: respuesta en frecuencia y ausencia de denormales o NaN.

**Integración**
- 100 ciclos de matar A, verificando el reenganche en cada uno.
- Lo mismo con B.
- Deriva de los rings durante 8 h: el nivel de llenado debe mantenerse estable.

**Soak test**
8 h continuas con 2 invitados, stream y grabación.

**Checklist de inyección de fallas**
- matar A, B, cloudflared y el launcher;
- desenchufar cada USB;
- desenchufar la red;
- desenchufar el disco (con copia sombra);
- cortar la energía;
- cambiar de monitor;
- simular un TDR.

**Auditoría de huella:** con Process Monitor, según 5.5.

**Matriz de hosts**
- VMs limpias de Windows 10 22H2 y Windows 11, como usuario estándar;
- laptops reales con GPU NVIDIA, AMD e Intel;
- escalado de 100, 125 y 150 %.

---

## 14. BUILD, DEPENDENCIAS Y DESPLIEGUE

**Estructura del repositorio**
- `extern/`: obs-studio fijado, Spout2, WIL, qrcodegen, libwebrtc precompilado, speexdsp
- `src/core`: `ba_core`
- `src/launcher`
- `src/app`: Proceso A
- `src/obs-module`: `ba-sources.dll`
- `src/gateway`: Proceso B
- `web/guest`
- `tools/deploy`

**Versiones fijadas**
Todo va en un único `versions.cmake`:
- tag estable de obs-studio (y las versiones de Qt 6 y CEF que usa ese tag);
- Spout2;
- milestone de libwebrtc;
- cloudflared.

**CMake raíz**
- C++20, MSVC 2022 x64.
- Flags: `/W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc`.
- `/MD` en A, B y el módulo, por consistencia con libobs y Qt. `/MT` solo en el launcher.
- PDBs siempre.

**Despliegue**
- Las reglas `install()` generan el layout de 5.1.
- `tools/deploy` hace el resto:
  - corre windeployqt;
  - copia el runtime de OBS, incluidos `data/libobs/*.effect`, CEF y `obs-browser-page.exe`;
  - copia el runtime de VC++ app-local, incluido `msvcp140_atomic_wait.dll` (lo requiere `std::atomic::wait` de C++20);
  - copia cloudflared en la versión fijada;
  - genera `manifest.json` con SHA-256;
  - firma los binarios.

---

## 15. RESUMEN DE CAMBIOS RESPECTO DEL SPEC v1 (para trazabilidad)

**Arquitectura de procesos**
- Se agregó el Proceso 0 (supervisor/watchdog). En v1 nadie reiniciaba A.
- `Launch.bat` se reemplazó por un launcher nativo.

**libobs**
- El "fork de OBS" pasó a ser libobs sin modificar + un frontend propio + un patch mínimo (P1).

**Túnel y medios**
- Se aclaró que el túnel no transporta WebRTC: se agregaron TURN y el preflight del puerto 7844.
- Se unificó el uso del túnel: con nombre, token remoto, sin `.yml`.

**Audio**
- Los micrófonos locales ya no van al PA a través de la PC.
- El motor de conversación se movió a B, con AEC y referencia loopback.
- Se agregaron resampling adaptativo y bus DSP.
- Se corrigieron los presets DSP y el tono de prueba.

**Plugins**
- LSP/VST3 queda opcional. El DSP de la v1 es nativo.

**Spout**
- API DX11 (spoutDX), resolución fija por slot, misma GPU obligatoria y timestamps paralelos.

**IPC**
- Se agregaron el canal de control (named pipe), los heartbeats y el retorno de video a los invitados.

**Hardware**
- La identidad física se resuelve con cfgmgr32 y ContainerId.
- Se agregaron el matching por puntaje, los perfiles por host y la identificación por desconexión.
- Las notificaciones se basan en `IMMNotificationClient` y `CM_Register_Notification`.

**Portabilidad**
- Se agregaron la copia sombra, el saneamiento de DLLs y entorno, las mitigaciones de Windows sin admin y el reconocimiento honesto de la huella residual.

**Operación**
- El pánico ya no corta el RTMP.
- Se reemplazó el monitor de YouTube vía libVLC.
- Se agregaron la grabación de respaldo y la selección de encoder por host.

**Persistencia**
- Snapshot con debounce, doble generación y checksum.
- Restauración con cuenta regresiva en lugar de Enter.

**Proceso de desarrollo**
- El orden de las fases va por riesgo, no por comodidad.

---

## 16. DECISIONES ABIERTAS (con valor por defecto)

| # | Decisión | Default |
|---|---|---|
| D1 | PA de la sala | El lugar tiene su propia consola. La PC solo aporta invitados y contenido al PA. |
| D2 | Cloudflare | Túnel con nombre sobre un dominio propio, con hostname fijo. |
| D3 | Copia sombra en `%TEMP%` | Activada. Es la única excepción a "nada fuera del disco". |
| D4 | Stack WebRTC | libwebrtc precompilado (alternativa: GStreamer). |
| D5 | Invitados simultáneos | 2 (máximo 4). |
| D6 | DSP | Channel strip nativo en la v1. VST3/LSP fuera de la v1. |
| D7 | Secretos | En claro, con credenciales revocables. |
| D8 | Canvas | 1080p30. |
| D9 | Soporte remoto (panel de estado web por el mismo túnel, con Cloudflare Access) | Fuera de la v1. |

---

## 17. PLAN DE FASES (RIESGO PRIMERO) Y PROMPTS DE TAREA

| Fase | Contenido | Criterio de aceptación |
|---|---|---|
| **0 — Spike de despliegue** | libobs embebido en una ventana Qt mínima, con un display, una capturadora y stream RTMPS. Desplegado al disco y ejecutado en una VM limpia como usuario estándar. | Funciona desde el disco. La auditoría de 5.5 da limpia. |
| **1 — Núcleo IPC y supervisión** | Rings de audio, Spout, pipe de control, heartbeats y launcher con watchdog. Un `fake-gateway.exe` que emite patrón de video y tono, y las fuentes IPC en `ba-sources.dll` que los consumen. | 100 kills de A con reenganche. Sin deriva en 8 h. |
| **2 — Gateway mínimo** | Señalización + túnel + TURN, 1 invitado, video y audio hacia A, retorno N−1 y motor de conversación básico. | Un invitado detrás de NAT simétrica conecta por TURN. Un crash de A no corta la conversación. |
| **3 — Hardware y wizard** | Secciones 9 y 10. | Identificación y reasignación correctas de dispositivos gemelos. |
| **4 — Audio del stream** | Channel strip, presets, automixer, patch P1, buses físicos y patchbay. | Loudness estable en el objetivo. Sin clicks al cambiar de preset. |
| **5 — Operación** | UI de operación, escenas, zócalos, proyectores, pánico y monitor de emisión. | Prueba con un operador no técnico, siguiendo un guion. |
| **6 — Endurecimiento** | Persistencia, restauración, diagnóstico e inyección de fallas (sección 13). | Checklist completo en verde. |
| **7 — Gateway completo** | N invitados, AEC afinado, estadísticas y app del invitado pulida. | Soak test de 8 h con 2 invitados. |

### Plantilla de prompt por tarea
> Con la especificación anterior como contexto fijo, implementá **[módulo/fase]**. Entregá en este orden:
> 1. supuestos y dudas;
> 2. interfaces públicas;
> 3. modelo de threading;
> 4. código completo;
> 5. tests;
> 6. lista de VERIFY;
> 7. qué quedó fuera y por qué.

### Primera tarea (Fase 1, parte 1)
> Entregá:
>
> **(a)** El árbol del repositorio y todos los `CMakeLists.txt`:
> - el raíz;
> - `ba_core`, `launcher`, `app`, `obs-module` y `gateway`, estos tres últimos como esqueleto;
> - `versions.cmake`;
> - las opciones de compilación de la sección 14;
> - las reglas `install()` que producen el layout de 5.1.
>
> **(b)** `ba_core/ipc/SharedAudioRing` completo, según 4.3, con tests de concurrencia, de wraparound y de reinicio del escritor.
>
> **(c)** `ba_core/ipc/HealthBlock`, según 4.4.
>
> **(d)** El `launcher` completo según 3.1, excepto la copia sombra y la UI de recuperación, de las que solo dejás las interfaces.
>
> No generes todavía nada de UI Qt ni de libobs.
