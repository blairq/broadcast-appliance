# Drivers: prueba aparte y cuarentena

Un driver de terceros que se cuelga o tumba el proceso no se arregla desde el programa; lo que sí se puede es
**dejar de cargarlo**. Cada driver o proveedor de dispositivos de la máquina se prueba en un **proceso aparte**
con tiempo límite, y a las **4 fallas seguidas** queda **en cuarentena**: no se vuelve a cargar solo, ni al
arrancar ni cuando cambian los dispositivos. La interfaz (Dispositivos y equipos → **Drivers**) y el asistente
(`drivers_listar`, `driver_probar`, `drivers_revisar`) ven y hacen lo mismo.

## De dónde salió

Una notebook con Windows tenía instalados ASIO4ALL, JackRouter (sin el servidor de JACK corriendo), ReaRoute y
SAR (configurado sobre JackRouter). El proveedor de dispositivos de ASIO de GStreamer cargaba el driver de SAR
**adentro del programa** en cada enumeración: 416 veces en 12,5 minutos, cada una terminando en «Open error: is
JACK server running?». La enumeración corría en el hilo de la interfaz (trabas de 3 a 12,8 s) y cargar
ASIO4ALL/SAR movía los dispositivos de Windows, que avisaba, y se volvía a enumerar: un círculo.

## Qué es un «driver»

| Tipo | Id | Qué se prueba |
|---|---|---|
| Proveedor de dispositivos de GStreamer | `gst:<proveedor>` (`gst:wasapi2deviceprovider`, `gst:mfdeviceprovider`, `gst:avfdeviceprovider`, `gst:osxaudiodeviceprovider`, `gst:pulsedeviceprovider`, `gst:v4l2deviceprovider`, `gst:decklinkdeviceprovider`, los de plugins…) | Se carga su plugin y se le piden los dispositivos. Solo los de rango MARGINAL o más (los que usan los monitores de dispositivos). |
| Driver ASIO (Windows) | `asio:{CLSID}` | Se lista desde `HKLM\SOFTWARE\ASIO` **sin cargarlo**; probarlo es crearlo por COM, `init()` y preguntarle canales y frecuencia. |

El proveedor de ASIO de GStreamer **no** es un driver de la lista: carga todos los ASIO a la vez (eso era el
problema) y sigue con rango NONE (`src/core/platform/GstEntorno.cpp`). Cada driver ASIO es una entrada propia.

## Cómo se prueba (proceso hijo)

La app se lanza a sí misma: `ba-app --sonda-drivers listar | probar <id>…` (`src/app/dispositivos/SondaDrivers.*`,
`SondaGst.cpp`, `SondaAsio.cpp`). Así no hay otro ejecutable que empaquetar: ba-app ya está en el bundle de los
tres sistemas con su GStreamer y sus bibliotecas al lado. El hijo cuenta lo que hace por stdout, una línea JSON
por evento (`{"empieza":id}`, `{"resultado":id,"ok":…}`, `{"fin":true}`); la app (`Sondeo.cpp`, con QProcess,
asincrónico) le pone **tiempo límite a cada driver**:

- no contesta en 8 s desde su «empieza» → se mata el hijo, falla **tiempo**, y los que faltaban siguen en otro hijo;
- el hijo termina sin «fin» con un driver empezado → ese driver **se cayó** (con lo último que escribió por stderr);
- contesta que no anda → **error**, con su mensaje.

El hijo arranca GStreamer con los rangos **verdaderos** (la app le saca `GST_PLUGIN_FEATURE_RANK`) y sin rehacer
el registro de plugins (`GST_REGISTRY_UPDATE=no`).

Motivos en castellano (`hw::motivoLegible`): «no contesta: ¿está corriendo el servidor de JACK?» (JackRouter y lo
que va encima, por lo que dicen en stderr), «no contestó en 8 s: se colgó al cargarlo», «se cayó el proceso que
lo probaba (el driver lo tumbó): …», «no arrancó: …».

## Cuarentena

`src/core/hw/SaludDrivers.*` — un registro único, sin Qt (lo lee también GstEntorno antes de `gst_init`):

- por driver: fallas seguidas y totales, cómo fue la última (error | tiempo | caida), el motivo, cuándo, y lo
  del último éxito (canales, dispositivos);
- a las `drivers.fallasCuarentena` (4) fallas **seguidas** queda en cuarentena; un éxito pone el contador en cero;
- un proveedor de GStreamer en cuarentena pasa a rango **NONE** en el proceso (ningún monitor de dispositivos lo
  carga) y, desde el arranque siguiente, antes de `gst_init` (`GstEntorno`);
- **«Probar de nuevo»** lo habilita para UNA prueba aparte: si anda, sale de la cuarentena y vuelve a su rango; si
  no, se queda. Un aviso en la barra del operador cuando uno entra en cuarentena.

## Por máquina

El disco es portable: `<datos>/Config/drivers.json` guarda una sección por **huella de máquina**
(`src/core/platform/Maquina.*`: hash del id que da el sistema —IOPlatformUUID, MachineGuid, /etc/machine-id— y
del modelo; sin nombres de máquina ni de usuario, no se puede volver al id).

- **Máquina nueva** (su huella no tiene un sondeo completo): al arrancar, en segundo plano, se lista y se prueba
  **todo**, con su progreso («Máquina nueva: revisando todo (3 de 9)…»). La primera búsqueda de cámaras y
  micrófonos espera a que termine (tope `drivers.esperaPrimeraBusquedaS`, 90 s).
- **Máquina conocida**: lo que anduvo se usa directo, **sin volver a probarlo**; lo que está en cuarentena ya
  arrancó afuera. Se lista lo instalado (sin cargar nada) y solo pasa por la sonda lo **nuevo**: un driver que no
  estaba, uno que cambió de versión o de archivo (su firma: versión, archivo y tamaño del plugin; archivo, tamaño
  y fecha de la DLL del ASIO; sale de la cuarentena: es otra versión) y lo que venía fallando sin llegar al tope.
  Lo que ya no está queda **ausente** (no es una falla). «Máquina conocida: 7 drivers listos, revisando
  novedades…».

## La búsqueda de dispositivos en el proceso

`src/app/dispositivos/Registro.cpp` (detector «local»):

- corre en **otro hilo**, una a la vez (los pedidos que llegan mientras tanto esperan esa misma), con tiempo
  límite (`dispositivos.busquedaLimiteS`, 15 s). Si no vuelve, la app sigue, se avisa a quien esperaba y se
  prueban **aparte** los proveedores de GStreamer que se venían usando, para encontrar al que se colgó;
- **antibucle** (`src/core/hw/Antibucle.*`): los avisos de «cambiaron los dispositivos» que llegan mientras se
  enumera o hasta 2 s después no cuentan (son el eco de la enumeración); al vencer la espera corta (800 ms) se
  mira la firma de lo que dice el sistema (los ids que ya tiene Qt) y si volvió a lo que vio la última búsqueda
  no se enumera (un driver que toma y suelta la placa); y nunca más de una búsqueda cada
  `dispositivos.minimoEntreBusquedasMs` (10 s), lleguen los avisos que lleguen.

## Interfaz y MCP

- **Dispositivos y equipos**: la fila **Drivers** (con el resumen) y «Ver»: cada driver con su estado (anda / en
  cuarentena con motivo y fallas / falló k de 4 / probando / no está instalado), «Probar de nuevo» y «Revisar
  todo». Filas de alto fijo (sin texto que se parta).
- `drivers_listar` (estado de todos, la máquina, el resumen), `driver_probar {"id"}` (espera el resultado),
  `drivers_revisar` (vuelve a probar todo lo que no está en cuarentena). Categoría «equipos».

## Ajustes (Configuración → avanzados → Dispositivos y drivers)

`drivers.fallasCuarentena` (4), `drivers.tiempoLimiteS` (8), `drivers.esperaPrimeraBusquedaS` (90),
`dispositivos.minimoEntreBusquedasMs` (10000), `dispositivos.busquedaLimiteS` (15).

## Pruebas

- `test_salud_drivers` (core): contador, umbral, reseteo con éxito, persistencia, por máquina, qué es nuevo, que
  lo conocido-bueno no se re-sondea, «Probar de nuevo», motivos, antibucle con avisos sintéticos, huella.
- `test_drivers` (app): la sonda en un proceso hijo (el test mismo con `--sonda-drivers`) con drivers de mentira
  que andan, dan error, se cuelgan, se caen y hacen lo de JackRouter; y la cuarentena de punta a punta (máquina
  nueva / conocida / otra máquina / otra versión). Sin ASIO ni GStreamer: corre igual en los tres sistemas.

## Lo que falta

- Ofrecer un driver ASIO que anda como entrada del mezclador (`asiosrc device-clsid=…`): hoy se lista con sus
  canales en Drivers, pero las entradas siguen siendo WASAPI.
- El runtime de NDI y los detectores de Python (USB/HID/serie) no pasan por la cuarentena.
- Otras enumeraciones a pedido siguen en el hilo de la interfaz (agregar una cámara desde Fuentes, las
  propiedades de una entrada, algunas herramientas MCP): no son el círculo, pero conviene pasarlas al registro.
