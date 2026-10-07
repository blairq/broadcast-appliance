# Histórico de eventos: todo lo de cada evento, en su carpeta

Pedido (2026-10-07): «un modo o un tab más para organizar todos los eventos, quizá
por carpeta, que se registre todo: la configuración de cada dato del sistema, la
lista de eventos trabajados, configuraciones, streams, logs, los discursos, las
grabaciones, las URL de los vivos… configurable. Un histórico total.»

## Lo que ya hay (relevado)

- **El evento ya es una carpeta**: `Eventos/<nombre>/` con `evento.json` (guion:
  bloques y cues), `Medios/`, `Discursos/`, `Respaldos/`, `padron.json`,
  `chequeo.json`, y lo que se genera al cerrar: `acta-*.md` (+ `.sha256`) e
  `informe-*.json` (previsto contra real). `EventoStore` lista, crea y guarda.
  `Guion` registra el recorrido real (cada paso con desde/hasta).
- **Pero casi todo lo que el evento produce va a carpetas globales, sin vínculo**:
  - grabaciones en `Recordings/` (`ba-<fecha>-00000.mkv`, entradas, señal limpia,
    repeticiones), sin índice: la UI de YouTube/Facebook lista la carpeta;
  - transcripciones en `Recordings/Transcripciones/<flujo>-<fecha>.{txt,srt,vtt}`;
  - orden del día, quórum, votaciones, sesión remota en `Config/*.json` como
    «sesión en curso» (se pisan en la próxima);
  - logs en `Logs/` (app, mcp, botonera, sesion-remota.jsonl) que rotan;
  - inicio y fin de grabación y transmisión: solo en memoria o en app.log;
  - la configuración: unos 60 JSON sueltos en `Config/`, sin foto ni historia;
  - URLs de los vivos: YouTube las devuelve (`YouTubeClient`), no se guardan.
- `BusEventos` ya publica lo que pasa (`salida/stream`, `salida/grabacion`,
  `aire/escena`, `oradores/al_aire`…), pero solo guarda el último valor.
- No hay base de datos: todo es JSON y JSONL (nlohmann). Así sigue (portable,
  legible, se copia con el Finder).

## La idea

**Un evento «abierto» a la vez.** Mientras está abierto, todo lo que pasa y todo
lo que se produce cae en su carpeta. Cerrarlo congela todo (sellos sha256) y lo
deja en el histórico. Sin evento abierto se usa uno automático por día
(«Sin evento — 2026-10-07»), para que nunca se pierda nada.

### La carpeta de un evento

```
Eventos/<carpeta>/<subcarpeta>/<evento>/
  evento.json            guion (lo que ya existe)
  ficha.json             qué fue: nombre, fecha, lugar, carpeta, etiquetas, estado
                         (planificado / abierto / cerrado), quién operó, notas
  bitacora.jsonl         TODO lo que pasó, una línea por hecho (ver abajo)
  config/inicio/…        foto de Config/ al abrir (los ~60 JSON)
  config/cierre/…        foto al cerrar; los cambios del medio, en la bitácora
  Grabaciones/           las del evento (o un índice si van a otro disco)
  grabaciones.json       cada archivo: tipo, desde/hasta, tramos, sha256, disco
  transmisiones.json     cada vivo: destino, URL pública (watch), estudio, inicio,
                         fin, caídas y reconexiones, estadísticas (SIN la clave)
  Transcripciones/       txt/srt/vtt de cada flujo (y traducciones)
  Discursos/             lo que ya existe (teleprompter)
  asamblea/              orden del día, quórum, votaciones, uso de la palabra,
                         sesión remota (copias al cerrar, no «la sesión en curso»)
  registro/              el pedazo de app.log, mcp.log y botonera.log del evento
  acta-*.md, informe-*.json, capitulos.txt   (lo que ya se genera)
  sellos.json            sha256 de todo al cerrar (integridad del histórico)
```

Las **carpetas** son carpetas de verdad dentro de `Eventos/` (anidadas, como el
usuario quiera: «2026/Octubre», «Consejo/Ordinarias», «Clientes/X»). Mover un
evento de carpeta es mover el directorio; la app solo lee. Etiquetas aparte, para
cruzar (p. ej. «con votación», «con invitados remotos»).

### La bitácora: una sola línea de tiempo

`bitacora.jsonl`, de solo agregar: `{"t": ms, "tipo": "...", "origen": "...", ...}`.
Se alimenta de lo que ya existe, sin tocar cada módulo:

| Fuente | Qué entra |
|---|---|
| `BusEventos` (suscripción a todos los temas) | escena al aire y TAKE, grabación y transmisión arrancan/paran, oradores al aire, banca, alarmas |
| Traza del MCP | cada herramienta llamada (quién, qué, resultado), sin secretos |
| Botonera | cada botón apretado |
| Guion | cada cue lanzado (ya lo mide; se escribe además) |
| `registro()` nivel aviso/error | avisos del motor, caídas de señal, reconexiones |
| Escritura de `Config/` | qué archivo cambió y el cambio (diff JSON), ver abajo |
| Asamblea | puntos del orden del día, uso de la palabra, votaciones y resultados |
| Transcripción | cada frase final, con orador si se sabe (el texto completo queda en Transcripciones/) |

Un solo escritor (hilo propio, `fsync` cada segundo) para que un corte de luz deje
la bitácora entera hasta el último segundo (va con T47, corte de luz: al volver,
el evento sigue abierto y sigue escribiendo).

### «La configuración de cada dato del sistema»

- **Foto al abrir y al cerrar**: copia de `Config/` (son KB). Con deduplicación
  por sha256 (`Eventos/.objetos/<sha>`) cien eventos no ocupan cien veces lo mismo.
- **Cada cambio en el medio**: un punto único de escritura (`guardarConfig(nombre,
  json)`, que ya hace `AppSettings` con .tmp + rename) agrega a la bitácora el diff
  (JSON Patch). Los módulos que escriben con `ofstream` directo pasan por ahí.
- **Qué se puede hacer después**: ver la configuración de cualquier evento,
  comparar dos eventos («qué cambió entre la sesión del 3 y la del 10») y
  **restaurar** una (toda, o solo escenas, solo audio…) como punto de partida.
- **Secretos nunca**: claves de transmisión, tokens, contraseñas se tachan
  (`"***"`) en fotos, diffs y bitácora. `Credenciales/` no se toca.

### Grabaciones, vivos, discursos

- Con evento abierto, la grabación escribe en `Eventos/…/Grabaciones/` (o en el
  disco elegido, y el evento guarda el índice con la ruta y el sha256). Cada
  archivo con desde/hasta, escena al aire en cada momento (capítulos) y oradores:
  se puede saltar a «cuando habló X» o «el punto 3 del orden del día».
- Cada transmisión: destino, URL pública (YouTube/Twitch/Facebook la dan al crear
  el vivo), duración, caídas. La lista de vivos de un evento queda para compartir.
- Transcripciones y traducciones del evento, con el texto buscable.

### Lo configurable (Ajustes → Histórico)

- Qué se registra (casillas por categoría: configuración, bitácora de acciones,
  MCP, botonera, transcripciones, logs, asamblea) y el detalle de la bitácora.
- Dónde: carpeta de eventos y disco de grabaciones (con copia a otro disco).
- Retención: guardar todo / borrar grabaciones de más de N días dejando el resto /
  avisar cuando el disco llega a X %.
- Evento automático por día sí/no; nombre por defecto («Sesión {fecha}»).
- Exportar: un evento entero como ZIP (o solo acta + transcripción + URLs).

## La pantalla: «Histórico» (página nueva del riel)

```
┌ Carpetas ─────────┐┌ Eventos (filtros: fecha, etiqueta, texto) ──────────────┐
│ ▸ 2026            ││ ● ABIERTO  Sesión ordinaria 07/10   2 h 14   3 vivos   │
│   ▸ Octubre       ││   Sesión ordinaria 03/10   1 h 52   acta ✓   12 votos  │
│ ▸ Consejo         ││   Ensayo general 01/10     0 h 40                       │
│ + carpeta         ││  [Nuevo evento] [Abrir] [Cerrar] [Mover] [Exportar]     │
└───────────────────┘└─────────────────────────────────────────────────────────┘
┌ Detalle: Resumen │ Línea de tiempo │ Grabaciones │ Vivos │ Discursos y texto │
│          Asamblea │ Configuración │ Registro │ Archivos                       │
└───────────────────────────────────────────────────────────────────────────────┘
```

- **Línea de tiempo**: la bitácora filtrable (escenas, audio, oradores, MCP,
  avisos), con salto a la grabación en ese instante.
- **Configuración**: árbol de la foto, comparar con otro evento o con lo actual,
  restaurar.
- **Búsqueda de texto completo** en todos los eventos (transcripciones, bitácora,
  actas, discursos): índice en memoria armado en segundo plano al abrir la página.
- La página Evento (guion, ensayo, correr) sigue igual; «Histórico» es el archivo.
  Desde el Histórico, «Abrir» un evento planificado lleva a Evento.

## MCP (todo lo de la pantalla también por MCP)

Categoría «historico»: `historico_listar` (carpetas, filtros), `historico_ver`,
`historico_abrir` / `historico_cerrar`, `historico_mover`, `historico_buscar`
(texto completo), `historico_bitacora` (rango, filtros), `historico_config`
(ver, comparar, restaurar con aprobación), `historico_grabaciones`,
`historico_vivos`, `historico_exportar`, `historico_ajustes`.

## Fases

| Fase | Qué | Notas |
|---|---|---|
| H1 | Evento en curso + `ficha.json` + carpetas anidadas en `EventoStore`; evento automático del día | **hecho** (ver abajo) |
| H2 | `Bitacora` (escritor único) colgada de BusEventos, traza MCP, botonera, guion, avisos | **hecho** (ver abajo) |
| H3 | Fotos de `Config/` al abrir/cerrar + diffs + tachado de secretos + dedupe | **hecho** (ver abajo); restaurar en H6 |
| H4 | Grabaciones, transcripciones y vivos al evento: índice con horas, capítulos, URLs | **hecho** (ver abajo) |
| H5 | Asamblea por evento (orden del día, quórum, votaciones, palabra, remota) y el pedazo de logs al cerrar; `sellos.json` | deja de pisarse «la sesión en curso» |
| H6 | Página Histórico (carpetas, lista, detalle con pestañas, búsqueda, comparar/restaurar, exportar) + MCP + Ajustes → Histórico | test_qml_carga y capturas |

Migración: los eventos existentes en `Eventos/` quedan en la raíz como
«cerrados»; las grabaciones y transcripciones viejas se pueden asignar a un
evento por fecha (botón «Traer lo del día»), sin moverlas si el usuario no quiere.

## Decidido (2026-10-07)

1. Página nueva «Histórico» (Evento sigue siendo preparar y correr).
2. Grabaciones dentro de la carpeta del evento.
3. Evento automático por día: sí (se puede apagar).

## H1 (hecho)

- «En curso» y no «abierto»: en la app «abrir» ya era cargar un guion para
  editarlo. El evento EN CURSO es el que recibe todo; se puede editar otro.
- `EventoStore`: eventos en carpetas anidadas (ruta con «/»; una carpeta con
  `evento.json` es un evento, una sin, agrupa), `carpetas()`, `crearCarpeta`,
  `crear(…, enCarpeta)`, `mover`, `ficha.json` (`DatosDelEvento`: estado,
  iniciado/terminado, lugar, etiquetas, notas, automático; conserva claves que no
  conoce), `Eventos/en-curso.json` (sobrevive al corte de luz), `iniciar`,
  `terminar`, `asegurarDelDia` («Jornada del 07/10/2026» en `2026/Octubre`; a la
  medianoche el automático pasa al de hoy; uno elegido sigue). El automático no
  cuenta como «el último tocado».
- `EventoViewModel`: `enCurso`, `carpetas`, `eventoAutomatico`
  (`Config/historico.json`), iniciar/terminar/mover/crear carpeta/ficha; revisa
  cada minuto; correr el guion de verdad (no en ensayo) lo pone en curso.
- MCP: `evento_en_curso`, `evento_iniciar`, `evento_terminar` (también prende o
  apaga el automático), `evento_carpeta`, `evento_ficha`; `evento_listar` con
  grupo, estado y carpetas; `evento_abrir` con `en_carpeta`.
- Página Evento: franja «En curso» (Poner en curso este, Terminar, Carpeta…,
  Jornada automática), el selector con la carpeta de cada evento, «Evento nuevo»
  con carpeta. La página Histórico completa es H6.
- Tests: `test_evento_store` (carpetas, mover, en curso, día, cambio de día).

## H2 (hecho)

- `evento/Bitacora`: escritor único en su hilo, tandas con un fsync por segundo,
  `bitacora.jsonl` del evento en curso. El cambio de evento viaja en la cola como
  una marca: lo de antes queda en el anterior, lo de después en el nuevo. Con el
  disco trabado no se come la memoria (tope de 20 000 líneas, se pierde lo más viejo).
- Se cuelga de dos lugares, sin tocar cada módulo (`StudioViewModel::armarBitacora`):
  - el **registro** general (`Registro::agregarOyente`, ahora con varios oyentes):
    `mcp` (lo que modifica y los fallos), `botonera` (`Botonera::auditar` ahora
    también va al registro), `guion` (`EventoViewModel::anotar`, menos el ensayo)
    y `aviso` (avisos y errores de cualquier componente). Lo mismo repetido en 10 s
    se anota una vez; los «missed update from display link» de Qt no entran;
  - el **bus** (`#`), menos lo que es medición: `audio/nivel/#`, `audio/loudness`,
    `oradores/tiempo`, `salida/salud`, `aire/tally` (`bus_excluir` en
    `Config/historico.json` lo cambia).
- `evento`: la app arranca y se cierra, un evento entra o deja de estar en curso.
- Sin secretos: pasa por `Traza::limpio`, el mismo limpiador de la traza del MCP.
- Categorías apagables: `registrar: {bus, mcp, botonera, guion, aviso, evento}` en
  `Config/historico.json` (la pantalla de Ajustes → Histórico es H6).
- MCP `evento_bitacora` (evento, tipos, texto, desde/hasta en ms o «HH:MM», límite).
- Test `test_bitacora` (orden, cambio de archivo, categorías, limpiador, filtros,
  línea cortada por un corte de luz, 8 hilos × 500 líneas sin perder ninguna).

## H3 (hecho)

- `evento/FotosConfig`: la foto es un índice (`<evento>/config/{inicio,cierre,ultima}.json`
  → `{t, archivos: {ruta: {sha, bytes}}}`) y cada contenido se guarda una sola vez
  en `Eventos/.objetos/<sha>.json` (las carpetas con punto no son eventos). Entran
  los `*.json` de `Config/` (con subcarpetas), no las copias `*.anterior`/`*.migrado`
  ni lo de más de 4 MB. Un JSON a medio escribir entra como texto.
- Secretos tapados antes de guardar (nombre de secreto + valor de secreto: tira
  larga sin espacios); «clave» de un catálogo no se tapa. Al restaurar (H6), lo
  tapado se deja como está ahora.
- `StudioViewModel::revisarConfig`: foto `inicio` al entrar en curso (la primera
  vez), `cierre` al dejar de estarlo, y revisión cada 10 s (solo `stat` de ~35
  archivos; relee únicamente lo que cambió de fecha o tamaño). Cada cambio va a la
  bitácora como tipo `config` con el JSON Patch (recortado a 40 operaciones; los
  valores grandes, «…(grande)»). Al arrancar, lo que cambió con la app cerrada se
  anota contra la última foto del evento.
- MCP `evento_config`: lista de archivos de una foto, el contenido de uno, o
  `comparar_con` «actual» / otro evento.
- Test `test_fotos_config`.
- Pendiente fino: si la app arranca otro día, el automático de ayer pasa a
  terminado antes de que exista la bitácora, y no recibe su foto `cierre` (tiene
  su `ultima`, que es lo mismo).

## H4 (hecho)

- `AppRuntime::setCarpetaDelEvento` / `carpetaGrabacion()`: con un evento en curso,
  la grabación del programa va a `<evento>/Grabaciones/`, las de entradas a
  `Grabaciones/entradas/` y la señal limpia a `Grabaciones/limpia/`. Sin evento, a
  `Recordings/` como siempre. La repetición (un búfer) sigue en `Recordings/repeticiones`.
- `FlujosTexto::setCarpetaDelEvento`: las transcripciones a `<evento>/Transcripciones/`.
- Subir a YouTube/Facebook lista las grabaciones del evento y las de `Recordings/`
  (`carpetasDeGrabaciones()`), lo más nuevo primero.
- `grabaciones.json` (archivo, desde/hasta, bytes; si se pasó a MP4, el final) y
  `transmisiones.json` (desde/hasta, caídas, problema y, con la señal `vivo` de
  YouTube/Twitch/Facebook o el destino manual, plataforma, URL pública, estudio e id;
  nunca la clave). El bus tiene un tema nuevo, `salida/vivo`.
- La bitácora ya no anota `salida/stream` ni `salida/grabacion` cada segundo: solo
  cuando cambian `activo`, `reconectando` o `problema` (con el archivo al empezar).
- MCP `evento_grabaciones`: grabaciones con sus capítulos (la escena al aire en cada
  momento, de la bitácora, en segundos desde el inicio del archivo), vivos y
  transcripciones.
- Probado en la app: 10 s de grabación con dos cambios de escena → archivo en la
  carpeta del evento y tres capítulos correctos.
