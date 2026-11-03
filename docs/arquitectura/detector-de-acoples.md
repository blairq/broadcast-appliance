# El detector de acoples de la interfaz

**Para qué.** Con 200 invitados entrando, la interfaz se clavó una o dos veces. Las reglas ya
están claras (memoria *causa de fondo, no parche* y [PLAN-MOTOR-SIN-ESPERAS](../planes/PLAN-MOTOR-SIN-ESPERAS.md)):
el modelo avisa (NOTIFY, roles), la QML se enlaza a propiedades y roles y nunca a funciones
que calculan o consultan, y la interfaz nunca espera al motor, al gateway, al disco ni a la
red. Lo que faltaba era una manera **automática** de encontrar dónde no se cumplen.

Un **acople** es un camino de la interfaz que no pasa por el modelo: la vista le pregunta
algo a un objeto de C++ en vez de enlazarse a lo que avisa, o el C++ de la interfaz espera a
otro (al motor, a la configuración del dueño, al disco, a la red, a otro hilo). Casi nunca
tarda, así que no se ve; con la sala llena, cualquiera puede ser la traba.

El detector tiene tres partes: una que **lee el código** (corre en cada `make probar`), una
que **sabotea** la app andando (lo que propuso el usuario: hacer lento a propósito todo lo
que no pasa por el modelo y ver dónde se cuelga la vista) y un **recorrido** automático que
pasa por toda la interfaz con el sabotaje encendido y junta el informe.

## 1. La parte estática: `tools/diag/acoples.py` y `tests/test_acoples.py`

`tools/diag/acoples.py` lista, con archivo:línea:

- **QML**: cada *binding* (`propiedad: expresión`; no un manejador `onAlgo:`, no una función
  guardada como `f: () => …`) que llama a una `Q_INVOKABLE` de C++ que **devuelve algo**. Las
  Q_INVOKABLE no-void salen de los `.hpp` de `src/app`; los receptores que se reconocen son
  `Studio…`, `Reloj`, `Fotos` y sus alias (`property var vm: Studio.sala`). Tipos:
  - `directo`: el binding la llama;
  - `indirecto`: el binding llama a una función del mismo `.qml` que la llama (`vía f()`);
  - `probable`: el receptor no se sabe qué es (`vg.algo(x)` con `vg` pasado de afuera) pero
    el método solo existe como Q_INVOKABLE no-void.
  Una función así en un binding tiene dos problemas: **la vista queda vieja** (QML no la
  vuelve a evaluar cuando el dato cambia, solo cuando cambia lo que se le pasa) y **cuesta en
  cada vuelta** (en un delegado de una grilla de 200, 200 llamadas cada vez que algo se mueve).
  Se arregla con una propiedad con NOTIFY o un rol del modelo.
- **C++ de la interfaz** (`src/app/ui` y los `.cpp` cuyos `.hpp` declaran Q_INVOKABLE o
  Q_PROPERTY; sin `arranque/`, que corre detrás del cartel de carga, ni `mcp/`, donde cada
  herramienta tiene su hilo), por clase:
  - `bloqueante`: `BlockingQueuedConnection`, `waitFor…`, `QEventLoop`, `wait`/`join`,
    `sleep`, `QFuture::result`;
  - `red`: DNS o sockets sincrónicos, `HttpsClient::request/download`, `LineSocket`;
  - `proceso`: `QProcess::execute`, `system`, `popen`;
  - `disco`: `QFile`, `ifstream/ofstream`, `fopen`, `QDir`/`QDirIterator`, `std::filesystem`,
    `persist::` sincrónico;
  - `motor`: `rt.engine()`, `config()`, `gateway()`, `launcher()`, `save()`;
  - `runtime`: los demás métodos de AppRuntime que no son «desde cualquier hilo».
  Lo que está adentro de una llamada que encarga a otro hilo (`ventanilla.pedir(…)`, `pedirQ`,
  `encolar…`, `fondo_.start(…)`, `QtConcurrent::run`, `std::thread`) no se cuenta. Es una
  lectura de texto: un `join` en un destructor o un `wait` en el bucle de un hilo propio
  también aparecen (y también cuentan: el destructor traba la interfaz al cerrar una página).

```sh
tools/diag/acoples.py            # todo, agrupado
tools/diag/acoples.py --qml      # solo los bindings
tools/diag/acoples.py --cpp      # solo el C++
tools/diag/acoples.py --json     # para otra herramienta
tools/diag/acoples.py --deuda    # las tablas de tests/test_acoples.py, recontadas
```

`tests/test_acoples.py` (en `test_codigo_py`, con `make probar`) usa esa lectura como
`test_escritor_unico.py`: `DEUDA_QML` (por `.qml`) y `DEUDA_CPP` (por `archivo|clase`) son lo
que ya estaba y **solo pueden bajar**; lo nuevo falla con su archivo:línea. Al arreglar uno,
se baja su número. Además:

- `GANCHOS`: cuántos `BA_ACOPLE` hay por archivo (la parte dinámica). **No pueden bajar**:
  sacar uno deja un camino sin sabotaje.
- Toda Q_INVOKABLE no-void que llame un binding tiene que empezar con
  `BA_ACOPLE("invocable", "Clase::metodo");`. Un binding nuevo que llama a una función
  nueva falla dos veces: por la deuda y porque a la función le falta el gancho.

## 2. La parte dinámica: el modo sabotaje (`src/core/diag/Acoples.hpp`)

**Solo en desarrollo y pruebas.** La opción de CMake `BA_DIAG_ACOPLES` (prendida de fábrica:
`make compilar`, `make app`, `make dist-win-docker` para instalar en la PC de pruebas) define
`BA_DIAG_ACOPLES=1` en `ba_core` y todo lo que lo usa. Lo que se publica (`make paquetes`,
`paquete-*` y `publicar*`, que pasan `DIAG_ACOPLES=0`) sale sin ella: `Acoples.hpp/.cpp`
quedan vacíos bajo `#ifdef`, `BA_ACOPLE`/`BA_ACOPLE_EN` son `((void)0)`, la herramienta MCP
`diag_acoples` no se registra y `--detectar-acoples` (en la app y el launcher) solo avisa «este
programa no trae el detector». No queda ningún símbolo `ba::diag::acoples` en el binario
(comprobado con `nm`). La parte estática (§1 y §3) no depende de esto.

Con la app en `--test` y **`--detectar-acoples[=ms]`** (o la herramienta MCP `diag_acoples`
con `demora_ms`), cada paso por un acople **en el hilo de la interfaz** espera `ms` de más
(300 de fábrica) y queda anotado: la clase, el lugar, el archivo:línea, lo que corría el
bucle de la interfaz (el vigía del bucle: un evento de Qt, una herramienta MCP…) y la pila de
la primera vez (con nombres en macOS/Linux; módulo+desplazamiento en Windows, para
`tools/caidas/analizar.py`/`atos`). La primera vez de cada lugar va también al registro
(«acople de la interfaz (…)»). Así la interfaz se traba **exactamente ahí**: si un evento
junta más de `diag.trabaInterfazMs` (2 s), el vigía de trabas saca las pilas de todos los
hilos como en una traba de verdad.

Para que la app no quede inservible, lo dormido tiene tope: 3 s en cada ventana de 5 s (lo
que pasa del tope se anota igual, sin dormir: `sin_dormir` en el informe). Fuera de los otros
hilos (el control, GStreamer, el pool de la foto) no hace nada. Apagado cuesta una lectura
atómica.

**El punto de enganche** es uno: `BA_ACOPLE("clase", "lugar")` al principio de un método (una
guardia que además mide lo que de verdad duró la llamada, sin lo dormido) o
`BA_ACOPLE_EN(clase, lugar, donde)` donde ya hay un `std::source_location`. Hoy está en:

| Dónde | Clase | Qué agarra |
|---|---|---|
| `TurnoMotor::contarDirecta` | `motor` | cada entrada directa de la interfaz a `rt.engine()`, `config()`, `gateway()`, `launcher()` y a los métodos del motor o de AppRuntime con su `Guarda` (una vez por lugar y evento, antes de tomar el turno); el lugar es el archivo:línea de quien llamó |
| `motor::Candado` | `candado` | la interfaz tomando el candado del motor |
| `persist::escribir/agregar/borrar/leerArchivo` | `disco` | el disco sincrónico (desde la interfaz va por `persist::cola()`) |
| `HttpsClient::request/download`, `LineSocket::open` | `red` | la red sincrónica |
| las 32 Q_INVOKABLE no-void que llama un binding | `invocable` | cada evaluación de esos bindings |

Sumar uno es una línea (`#include "diag/Acoples.hpp"` y `BA_ACOPLE("disco", "Clase::metodo");`);
sacarlo lo nota `test_acoples.py`.

### El informe

`diag_acoples` (MCP, solo `--test`) siempre lo devuelve:

```json
{"activo": true, "demora_ms": 300, "total": 812, "dormido_ms": 61200, "sin_dormir": 604,
 "lugares": [{"clase": "invocable", "lugar": "ConferenceViewModel::nameOf",
              "donde": "src/app/ui/ConferenceViewModel.cpp:520", "veces": 140,
              "demora_ms": 4200, "real_ms": 380.5, "real_max_ms": 12.1, "total_ms": 4580.5,
              "corria": "QQuickWindow evento 77", "pila": ["…"]}]}
```

`lugares` va ordenado por `total_ms` (lo dormido más lo real). Con `{"reiniciar": true}` se
borra lo anotado; con `{"demora_ms": 0}`, se apaga.

## 3. El recorrido: `tools/diag/recorrer-ui.py`

Con la app abierta en `--test`, recorre **todas** las páginas (el enum de `ui_show_page`, que
lee de la app), sus secciones (audio, asamblea, personas, latencias, cámaras) y sus paneles
(`ui_abrir_panel`), con el sabotaje encendido. Con `--invitados N` arranca la prueba de sala
(`prueba_sala_arrancar`), pasa por la sala y el tablero mientras entran, y vuelve a recorrer
todo con la sala llena; al final la para (también con Ctrl+C). No cambia nada del evento
(solo muestra páginas y paneles), pero la prueba de sala toca el cupo mientras dura: usarlo
en un evento de prueba.

```sh
~/.claude/arbitro/bin/turno app tomar NOMBRE
open dist/BroadcastAppliance.app --args --test --detectar-acoples
tools/diag/recorrer-ui.py --invitados 200 --minutos 10 --salida ~/mediciones/acoples-200
# … y cerrar la app (app_quit) y soltar el turno
```

En `--salida`: `informe.json` (el de `diag_acoples` con `primer_paso` y `pasos` de cada
lugar), `resumen.md` (la tabla ordenada por tiempo total, para pegar) y `pasos.jsonl` (qué
apareció en cada paso). Opciones: `--demora-ms`, `--pausa-s` (lo que se queda en cada
página), `--espera-sala-s`, `--vueltas-sala`, `--ba-mcp`.

## Qué hacer con cada clase

- `invocable`: pasar el dato a una propiedad con NOTIFY o a un rol del modelo (el modelo
  avisa cuando cambia, venga de donde venga), y que el binding lea eso.
- `motor` / `candado` / `runtime`: pedir por la ventanilla (`VentanillaMotor::pedir`) y leer
  la foto (`FotoMotor`); lo que la vista muestra, que lo publique el dueño.
- `disco`: `persist::cola()` (o un hilo propio) y avisar al terminar.
- `red`, `proceso`, `bloqueante`: a un hilo propio (o asincrónico de Qt) con su respuesta
  encolada a la interfaz; un destructor que espera un hilo, que lo suelte (`deleteLater`,
  `quit` sin `wait`) o que espere con tope.

## Lo que encontró la parte estática al ponerlo (2026-11-03)

**QML (39):** los más preocupantes con la sala llena son los de las grillas y listas largas:
`pages/ConferencePage.qml:547` y `:562` (`conf.nameOf`: recorre la foto de los participantes
por cada mensaje del chat), `cards/GuestsContent.qml:221` (`cifras()` → `grilla.model.fila`,
en cada invitado), `controls/VistaVideo.qml:22` y `pages/DashboardPage.qml:22`
(`Studio.multivistas.de`), `cards/MixerContent.qml:471` (`Studio.mixer.delGrupo` como
`model`), `controls/PanelMezclador.qml:194` (`vu.fraccion` por tira), `controls/RielNavegacion.qml:349`
(`Studio.riel.idDePagina` por botón del riel) y `pages/SettingsPage.qml:161/172/178/351`
(`seccionVisible`, `tieneBasicos`, `tieneAvanzados` por campo). La lista entera, con
`tools/diag/acoples.py --qml`.

**C++ (463 en 5 clases):** 46 `motor` (sobre todo `ui/SettingsModel.cpp`,
`ui/ControlViewModel.cpp`, `ui/PdfEscena.cpp`, `medios/Playlist.cpp`, `plugins/Plugins.cpp`),
40 `runtime`, 5 `red` (`ui/PantallasViewModel.cpp:136`: un `QUdpSocket` con
`waitForConnected(300)` para elegir la IP), 49 `bloqueante` (entre otros
`ui/ConferenceClient.cpp:231` `BlockingQueuedConnection` al destruir el cliente de la sala,
`ui/Reconocedor.cpp:57`, `ui/PruebaSalaViewModel.cpp:105` `waitForFinished(2000)` de hinchada)
y 323 `disco` (la mayoría en `video/SuperRes.cpp`, `agent/AgentViewModel.cpp`,
`plugins/Plugins.cpp`, `agent/ModelosLocales.cpp`, `ui/Discursos.cpp`). Algunos son falsos
positivos (código que corre en un hilo propio que la lectura no ve: p. ej.
`InputThumbProvider::requestImage` corre en el hilo de carga de imágenes): lo confirma o lo
descarta la parte dinámica.

## Lo que encontró y lo que se arregló (noche del 2026-11-03)

### Sin invitados, en la Mac (el recorrido entero)

| acople | antes | después | arreglo |
|---|---:|---:|---|
| `GuionViewModel::enlaceDeCue` (tarjeta Dirección) | 44 404 ms, 205× | 0 | `enlacesPrompter` con aviso propio (`enlacesPrompterChanged`, solo si cambia el puerto de la sala o la lista de prompters); la tarjeta lee el mapa |
| `RielConfig::paginaDeId` | 24 902 ms, 101× | 0 | mapas `paginaPorId` / `idPorPagina` (CONSTANT) y `tituloPorPagina`; las tres funciones ya no son Q_INVOKABLE (las usa el MCP desde C++) |
| `RielConfig::idDePagina` | 8 100 ms, 33× | 0 | ídem (el riel y `Main.qml`) |
| `PrompterViewModel::avisoParaOrador` | 1 200 ms, 4× | 0 | `avisoPuesto` (la clave del aviso puesto, NOTIFY con el estado) |
| `persist::leerArchivo` (bitácora, foto de Config) | 904 ms, 6× | 0 | `BitacoraEvento::revisarConfigEnElFondo`: leer `ultima.json`, comparar y guardar, todo en el fondo; vuelven las líneas |
| `ListaObjetos::fila` (matriz, «Mute de») | 300 ms, 15× | 0 | el combo lee `Studio.salidas.lista.lista` (propiedad) |
| `persist::escribir` (bitácora) | 1 ms, 1× | 0 | ídem bitácora |
| `persist::leerArchivo` (Asamblea → Orden, con un evento con documentos) | (no estaba) 6 607 ms, 44× | 0 | `DocumentosViewModel::listaUi` leía el índice del disco en cada lectura y la QML la recorre (cada acceso a un elemento relee la propiedad): armada una vez por cambio |
| **total** | **79,8 s dormidos, 7 lugares** | **2,4 s, 2 lugares** | quedan `SettingsModel::seccionVisible` (6×, binding de Configuración) y el disco de `AgentViewModel` (2×, ver abajo) |

Además, de la lectura estática: `ConferenceViewModel::nameOf` (el chat: cada mensaje recorría la
foto de la sala → rol `toName` en el mensaje y `nombres` {id: nombre}) y `MultiviewViewModel::de`
(→ `porId`; `elegida` también pasaba por el gancho). `test_acoples.py` bajó: QML 39 → 29
bindings, ganchos de los caminos que ya no existen fuera de la tabla.

### Con 200 invitados, en la PC (Ryzen 9 5900X + RX 5700 XT)

Cinco tandas de ≤ 2 min (hinchada en la Mac por TURN de la caja, 10 % TCP, `--modo grabado
--hablan 1`; el recorrido arranca con 100 con medios y sigue mientras entran los demás):
`tools/diag/recorrer-ui.py --sala-externa 100 --paginas …` en paralelo con
`tools/carga/medir-sala.py --remoto <PC> --participantes 200 --cupo --estabilizar-max 60
--regimen 40`. Mediciones crudas fuera del repo (`2026-11-03-acoples-pc177/`).

**El detector no anotaba nada en Windows**: `detalle::demoraMs` era `inline` en el .hpp y en el
.exe de clang-cl los ganchos leían una copia que `activar()` no tocaba. Ahora está definida en
`Acoples.cpp` y el informe dice `hilo_interfaz_marcado`, `de_otro_hilo` y `reentradas` (para
ver que de verdad mira).

Acoples del sabotaje (después de los arreglos de la Mac): `ListaObjetos::fila` 206× (3 000 ms,
tope) en Audio → Matriz — `Matriz::rearmarUltimas` leía su propio modelo por la invocable de la
vista (→ `ListaObjetos::datosDe`) — y `persist::leerArchivo` 1-2× (300-600 ms) al pasar por el
tablero y por Asistente (`EstadoIA::recalcular` → `AgentViewModel::current/installed/hasKey`
leen disco: queda, ver abajo).

Lo que más pesaba no eran los acoples del sabotaje sino **trabas de verdad** de la interfaz
mientras la sala se llena (el vigía de trabas dejó nueve volcados; `tools/caidas/analizar.py --pc`):

| traba (volcado) | duró | causa | arreglo |
|---|---:|---|---|
| 6 de 9 (`…044814`, `…045434`, `…045725`, `…050054`, `…050411`, `…050921`) | 2,0-4,6 s | `BuscadorCatalogo` (el selector de cosas: hay uno por página que lo usa, siempre vivo) recontaba el catálogo entero y avisaba con cada fila que entraba o salía; la QML rearmaba el Repeater de los tipos | mira el catálogo solo con el selector abierto; los cambios se juntan (60 ms) |
| `…045000` | 2,1 s | `Studio.catalogo.porId` era un QVariantMap con NOTIFY `cambio`: cada binding que mira UNA cosa (monitores, ventanas, TV) convertía las ~450 filas a JS en cada cambio | `porId` es un `QQmlPropertyMap`: una propiedad por id, avisa solo la que cambió |
| `…045109` | 2,6 s | `TiemposModel::eventos`: la QML la recorre y cada acceso a un elemento de una lista de una propiedad vuelve a leer la propiedad (convertía el JSON entero por elemento) | armada una vez por cambio |
| `…054757` (después) | 2,7-6,6 s | `VistaGrillas` (Sala, Escenas y Asamblea, cargadas siempre): cada aviso de `ControlGrillas` convertía y rearmaba todas las caras en las tres aunque nadie las mirara, y «Fijar a alguien» era N² | arma solo mientras se ve; `hay` no depende de verse; un `Set` |
| `…050551` | 3,3 s | el propio sabotaje (`ListaObjetos::fila` 206 × 300 ms con tope) | `datosDe` |
| `…050756` | 2,5 s | destruir un `TreeView` de QML al cambiar de página | queda |
| `…055906` (después) | 2-9 s | Latencias: el dorso de cada `TarjetaTiempo` (detalle en texto enriquecido) se rearmaba en cada vuelta del modelo (1 s) en cientos de tarjetas que nadie veía | el dorso se arma solo dado vuelta |
| `…061113` (después) | 5,0 s | Latencias al mostrarse: crear cientos de tarjetas (una por nodo del mapa) de una vez | queda: hace falta una vista que no cree lo que no se ve |

Además, de raíz y sin traba propia: el catálogo junta las ráfagas de claves y rótulos de
invitados (`pedirRefresco`, 50 ms) y una distribución cambiada rearma solo sus filas
(`refrescarDistribuciones`; arrastrar en el editor ya avisaba una vez, al soltar).

| tanda (páginas) | trabas > 2 s antes | después |
|---|---|---|
| 1 (tablero, sala, escenas, cámaras, rótulos, prompter, público) | 16 (2,0-4,3 s) | 0 |
| 2 (asamblea con sus 12 secciones, guion y sus paneles) | 0 | 0 |
| 3 (medios … multiview) | 1 (2,9 s) | 0 |
| 4 (YouTube … audio) | 1 (3,2 s, sabotaje) | 0 |
| 5 (botonera … configuración) | 1 (4,6 s) | 4 (2,7-6,6 s: `VistaGrillas`) → 6 (2-8 s: Latencias) → 2 (4,6 y 5,0 s: Latencias al mostrarse) |

Lo que queda: las tarjetas de Latencias con 200 (arriba); `AgentViewModel` lee disco en sus getters (`current()` → `pins()`, `installed()`,
`hasKey()`) y `EstadoIA` los llama en el hilo de la interfaz (54 `disco` en la deuda de ese
archivo: pasar su estado a una foto armada en el fondo); el bucle de la interfaz sigue con
~75 demoras de 50-130 ms por tanda de 2 min (la mayoría «QTimer, cada 1000 ms» cada ~10 s, y
`VentanillaMotor`/`ConferenceClient`); el `TreeView` que tarda en destruirse; y
`test_vigia_traba` falla en Windows nativo (sus pilas de prueba usan nombres de Itanium que el
resumen no desarma con el ABI de MSVC: de antes, no de esto).

La grilla con 200 (hinchada con el rótulo de cada capa en el centro): las miniaturas llegan en
180p y el orador (`--hablan 1`) en **720p** en su celda, sin agrandar
(`docs/hardware/img/pc177-noche-sala-200-rotulo.jpg`,
`docs/hardware/img/pc177-noche-programa-200-orador-720p.jpg`).
