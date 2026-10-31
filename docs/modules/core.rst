.. ============================================================================
   ba_core — Núcleo (C++20 puro, sin Qt, sin libobs)
   ============================================================================

..
   Página del componente `ba_core` (src/core). Documenta los namespaces y
   clases reales del núcleo. Los docstrings completos viven en los headers de
   src/core/** y son extraíbles con Doxygen (ver docs/index.rst, sección
   "Docstrings C++").

ba_core (núcleo)
================

`ba_core` es la librería estática central del proyecto: **C++20 puro, sin Qt y
sin libobs**, por lo que compila en Windows/MSVC y en Linux (gcc/clang) y sus
unidades se prueban por separado (CTest). Enlaza como dependencias de terceros
`mbedTLS` (cripto), `nlohmann/json` (JSON header-only), `libsamplerate`
(re-muestreo) y `Argon2` (KDF), todas traídas por FetchContent desde el
`CMakeLists.txt` raíz.

Los includes públicos apuntan a `src/core/`, por lo que los headers se consumen
como `ipc/SharedAudioRing.hpp`, `dsp/Automixer.hpp`, etc.

El núcleo se divide en los siguientes namespaces.

ipc — Contratos de IPC
----------------------

Namespace `ba::ipc`. Primitivas de memoria compartida y protocolos que cruzan
entre procesos (launcher ↔ A ↔ B). Todas sin Qt/libobs.

.. code-block:: cpp

   namespace ba::ipc { ... }

- `SharedMemoryRegion` — RAII sobre un buffer contiguo en memoria compartida.
  Primitiva de transporte común de los contratos de IPC. Move-only. Backend
  Windows: `CreateFileMappingW`/`MapViewOfFile` en `Local\` con DACL al usuario
  actual; resto: `mmap MAP_ANONYMOUS` + registro de proceso por nombre.
  - `Status` (ok, code) y `ErrorCode` (kOk, kInvalidArgument, kOutOfMemory,
    kNotFound, kPlatform, kAlreadyMapped) para logueo estructurado.
  - `create(name, bytes, out)`, `open(name, out)`, `data()`, `size()`,
    `valid()`, `close()`.
- `SharedAudioRing` — Ring SPSC (un escritor, un lector) de audio en memoria
  compartida (spec 4.3). Float32 intercalado en bloques de 480 frames (10 ms a
  48 kHz) con timestamp QPC. Sin locks en el camino de datos
  (`std::atomic_ref<uint64_t>` lock-free).
  - `RingHeader` (header versionado a offset 0, índices en cache lines
    propias), `PoppedBlock` (bloque + metadatos `isSilence`/`isFadeIn`),
    constantes del layout en `ba::ipc::audio_ring` (kMagic, kVersion,
    kSampleRate, kBlockFrames, kDefaultCapacityBlocks, ...).
  - `init(...)` (escritor), `openReader(name, out)`, `openWriter(...)`,
    `push(samples, qpc)`, `pop()`, `blockFloats()`, `capacityBlocks()`,
    `header()`, `region()`. Overrun descarta el bloque; underrun devuelve
    silencio con fade; cambio de generación resincroniza con fade-in.
- `HealthBlock` — Bloque de salud "BA.Health" (spec §4.4): publica por proceso
  PID, estado, QPC del último latido y versión. Base del watchdog del launcher
  (un heartbeat sin actualizar > 5 s cuenta como cuelgue, spec §3.1.7).
  Escritura con seqlock por slot; lectura lock-free. Move-only.
  - `ProcessState` (Unknown, Starting, Running, Hung, Terminated), `HealthHeader`,
    `HealthEntry`, `Snapshot`.
  - `create(name, slotCount, out)`, `open(name, out)`, `beat(slot)`,
    `setState(slot, state)`, `setIdentity(slot, pid, version)`, `read(slot)`,
    `isStale(slot, nowQpc, thresholdNs)`, `nowQpc()` (reloj QPC cruzado),
    `kDefaultHangThresholdNs` (5 s).
- `ControlProtocol` — Protocol de cable del canal de control (spec 4.1):
  "prefijo de longitud + JSON". Librería autocontenida y testeable.
  - `Frame` (`encodeFrame`, `decodeFrame`, `kMaxPayloadBytes` = 1 MiB).
  - `Type` (Hello, TunnelState, GuestList, GuestAdmit, GuestReject, GuestExpel,
    GuestMute, ChangeRoom, ChangePin, FlowState, Shutdown, Response),
    `typeToString`/`typeFromString`.
  - `Message` (protocolVersion, id, type, payload; `toJson`/`fromJson`).
  - `DuplexStream` (stream abstracto write/read) y `ControlEndpoint`
    (request/response + reconexión: `sendRequest`, `poll`, `connected`,
    `onDisconnect`; roles `kLauncher`/`kGateway`).
- `SpoutTransport` — El canal de video entre procesos (spec §4.2) sobre
  `SharedMemoryRegion`: el mismo en las tres plataformas, memoria compartida por
  CPU con triple buffer y seqlock por buffer (sin texturas compartidas ni Spout2:
  el nombre quedó de la spec original).
  - `FrameMeta` (frameIndex, qpc: viajan en la cabecera de cada buffer),
    `SlotConfig` (resolución fija por slot). `SpoutMetaBlock`/`SpoutMetaLayout`
    son una primitiva suelta que el sender y el receiver no usan.
  - `SpoutSender` (`publish`, `send`, `published`) y `SpoutReceiver`
    (`subscribe`, `receive`, `hasData`). Move-only.

dsp — Procesamiento de audio
----------------------------

Namespace `ba::dsp`. La cadena DSP nativa del producto (corazón del appliance,
spec 7.2/7.4). RT-safe: sin asignación, locks ni I/O en el camino de audio.

.. code-block:: cpp

   namespace ba::dsp { ... }

- `Automixer` — Automixer de mesa redonda (spec 7.3, estilo Dugan). Objeto
  COMPARTIDO: recibe el nivel de cada micro y devuelve la ganancia dinámica de
  cada canal (normalización Dugan + hold + atenuación por cantidad de micros
  abiertos). Lock-free con `std::atomic<float>`.
  - `AutomixerParams` (fs, openThresholdDb, attack/release, hold, maxOpenMics,
    targetSummedGain).
  - `setLevel(ch, level)`, `gainFor(ch)`, `reset()`, `setParams(p, smoothMs)`,
    `maxChannels()`.
- `Presets` — Presets de channel strip (spec 7.3) como funciones puras que
  devuelven `StripParams` de fábrica para los tres micrófonos de broadcast:
  `presetPodium()` (atril), `presetLavalier()` (corbatero),
  `presetHandheld()` (de mano).
- `Resampler` — Envoltorio RAII fino sobre `libsamplerate` (Secret Rabbit Code,
  MIT) para el re-muestreo adaptativo (spec 7.4). Convierte un buffer de una
  frecuencia a otra (p. ej. 44.1 → 48 kHz) conservando el contenido espectral.
  `configure()` es control path (única asignación); `resample()` es RT
  (sin asignaciones). No lanza.
  - `Quality` (Linear, SincMedium, SincBest).
  - `configure(inRate, outRate, channels, q)`,
    `resample(in, inFrames, out, maxOutFrames, outFrames)`.

persist — Snapshot y restauración
---------------------------------

Namespace `ba::persist`. Persistencia atómica del estado con doble generación
y restauración/recuperación (spec §11.1–§11.3).

.. code-block:: cpp

   namespace ba::persist { ... }

- `StateDoc` — El ÚNICO modelo de estado (fuente de verdad, spec §11.1) con
  `schemaVersion`. POD plano: `schemaVersion`, `sequence`, `cleanShutdown`,
  `sections` (mapa nombre → blob JSON opaco).
  - `serialize()` (JSON determinista) y `deserialize(text, out)`.
- `SnapshotStore` — Writer/reader del snapshot con doble generación alternada
  (`snapshot.A.json`/`snapshot.B.json`), debounce (máx. 1 escritura física cada
  500 ms) y escritura atómica (temp + fsync + rename). Move-only.
  - `Status`/`ErrorCode` (kOk, kInvalidArgument, kNotInitialized, kIo,
    kNotFound, kCorrupt), `Step` (None, Temp, Flush, Rename) para
    fault-injection.
  - `init(dir, fileA, fileB)`, `scheduleWrite(doc, immediate)`, `flushNow()`,
    `readLatest(out)`, `sha256Hex(body)`, `physicalWrites()`,
    `setDiskPresent(bool)`, `setFaultAtStep(step)`, `pathA()`, `pathB()`,
    `hasPending()`, `pendingDoc()`, `kDebounceMs`.
- `Restore` — Restauración (spec §11.3) y flujos de recuperación. Stateless:
  funciones estáticas puras sobre rutas. Reutiliza el checksum de
  `SnapshotStore::sha256Hex` y el JSON de `StateDoc::deserialize`.
  - `RestoreResult` (status: Ok/ChecksumMismatch/Corrupt/Missing, detail).
  - `restore(snapshotPath, out)` y `recover(dir, out)` (elige el snapshot bueno
    más nuevo entre las generaciones retenidas, saltando los corruptos).

security — Bóveda de secretos y señalización
--------------------------------------------

Namespace `ba::security`. Cifrado en reposo y seguridad de la señalización
(spec §12, §8.3).

.. code-block:: cpp

   namespace ba::security { ... }

- `SecretsStore` — Bóveda de secretos cifrada en disco (spec §12, D7): guarda
  pares clave→valor en `Config/secrets.json` cifrados con **AES-256-GCM**
  (mbedTLS) bajo una clave derivada de la passphrase maestra con **Argon2id**
  (referencia P-H-C). Control path (E/S + KDF), no RT. No lanza.
  - `load(path, masterPassphrase)`, `save(path, masterPassphrase)`,
    `set(key, value)`, `get(key, out)`, `size()`.
- `TokenService` — Emisión/verificación de tokens cortos firmados con HMAC a
  partir de "sala + PIN", con rate-limit/lockout de intentos (spec 8.3). El
  PIN no se guarda en claro (validador inyectado) y el reloj es inyectable.
  - `VerifyResult` (Ok, Malformed, BadSignature, Expired, WrongRoom),
    `PinValidator` (callback).
  - `issue(room, pin, ttlSec, nowUnix)`, `verify(token, room, nowUnix)`,
    `attemptAllowed()`, `recordFailure()`, `recordSuccess()`,
    `resetLockout(nowUnix)`, `setPinValidator(cb)`.
  - Utilidades: `nowUnix()`, `base64UrlEncode`/`base64UrlDecode`.

diag — Diagnóstico
------------------

Namespace `ba::diag`. Logs rotativos y exportación de diagnóstico (spec §11.4).

.. code-block:: cpp

   namespace ba::diag { ... }

- `LogRotator` — Rotador de logs por proceso: escribe a `<dir>/ba.log` y rota a
  `ba.log.1`, `ba.log.2`, ... manteniendo a lo sumo `keepFiles` archivos.
  Camino de control (no RT). Move-only.
  - Constructor `LogRotator(dir, maxBytes, keepFiles)`, `write(line)`,
    `files()`.
- `DiagExport` — Exportador de diagnóstico: empaqueta los logs rotados + un
  encabezado de metadatos en un único archivo (spec §11.4, "Exportar
  diagnóstico"). Funciones estáticas, no lanzan.
  - `ExportResult` (ok, path).
  - `exportBundle(logDir, outPath)`, `writeMinidump(path)` (stub, GATED:Win).

fault — Inyección de fallas
---------------------------

Namespace `ba::fault`. Andamiaje genérico de fault-injection para tests/
integración (spec §13). Ayuda de control/test, nunca del camino RT.

.. code-block:: cpp

   namespace ba::fault { ... }

- `FaultInjector` — Registro thread-safe de "fallo armado" (`std::atomic`).
  - `FaultKind` (None, DiskFull, ChecksumMismatch, PluginCrash, Timeout).
  - `instance()` (singleton process-wide), `arm(kind)`, `disarm()`, `armed()`,
    `fire()`.
- `writeWithFault(injector, sourcePath, outPath)` — Copia un archivo
  alterando exactamente un byte si hay `ChecksumMismatch` armado (demo del
  modo de fallo para SnapshotStore/Restore).

crypto — Primitivas criptográficas
----------------------------------

Namespace `ba::crypto`. Respaldadas por mbedTLS (no re-implementadas a mano).

.. code-block:: cpp

   namespace ba::crypto { ... }

- `Sha256` — SHA-256 incremental (FIPS 180-4) sobre `mbedtls_sha256_context`.
  - `reset()`, `update(data, len)`, `digest()` (const, no muta).
  - Funciones puras: `sha256(data, len)`, `sha256Hex(data, len)`.
- `HmacSha256` — HMAC-SHA256 (RFC 2104) sobre la API genérica de mbedTLS.
  Funciones puras, thread-safe.
  - `hmacSha256(key, keyLen, msg, msgLen)`, `hmacSha256Hex(key, msg)`,
    constantes `kHmacSha256BlockSize` (64) y `kHmacSha256Size` (32).

hw — Identidad de dispositivos
------------------------------

Namespace `ba::hw`. Algoritmos de re-identificación de hardware (spec §9.2,
§9.4). Código pura y determinista, testeable en Linux (el walk cfgmgr32 real es
GATED:Win).

.. code-block:: cpp

   namespace ba::hw { ... }

- `DeviceIdentity` — Estructura de datos pura con la identidad física de un
  dispositivo: `endpointId`, `containerId`, `usbInstanceId`, `vid`/`pid`/`mi`,
  `serial`, `locationPaths`, `friendlyName`, `driver`.
- `DeviceMatcher` — Matching con puntaje (spec 9.4) para re-identificar un
  dispositivo observado contra los conocidos. Stateless.
  - `MatchKind` (ExactEndpoint=100, Serial=80, VidPidLocation=60,
    UniqueVidPid=40, AskOperator=10, NoMatch), `MatchResult` (kind, index,
    score).
  - `match(observed, known, alreadyAssigned)`.
- `DeviceIdentityResolver` — Resolución del ancestro USB subiendo por el árbol
  de devnodes (spec 9.2), modelado sobre un árbol abstracto para testear sin
  cfgmgr32. Stateless.
  - `DevNode` (id, parent, isUsb, vid, pid, serial).
  - `resolveUsbAncestor(startNodeId, nodes, outUsb)`.
