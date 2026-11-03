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
