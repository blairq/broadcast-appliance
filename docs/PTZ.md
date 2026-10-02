# Cámaras PTZ y «Mejorar imagen»

Un solo control para todas las cámaras. La página **Cámaras PTZ**, las herramientas MCP `ptz_*` y el asistente
hablan siempre con lo mismo (`Studio.camaras`, `src/app/ptz/ControlCamaras`); lo que cambia según la cámara es el
**backend**, que se elige ahí mismo para cada entrada de video.

## Qué tiene cada entrada

| | Qué es | Dónde |
|---|---|---|
| **PTZ digital** | Recorte y zoom en la GPU (bicúbica), con joystick, presets y movimiento suave. Lo tiene **cualquier** entrada (hardware, red, NDI), aunque la cámara sea fija. Sin zoom el filtro se saltea: no cuesta nada. | filtro `ba_ptz_digital` (`src/obs-module/ba_ptz_digital.cpp`, `data/ptz-digital.effect`) |
| **PTZ de la cámara** | Si la cámara es motorizada: su backend (VISCA, ONVIF, Panasonic) y sus controles (pan, tilt, zoom, foco, autofoco, presets de la cámara, home). Se muestra solo lo que el backend dice que puede (`capacidades`). | `src/app/ptz/Ptz*.cpp` |
| **Mejorar imagen** | Ruido temporal, color pegado al brillo (saca el LED corrido, el color desparramado del 4:2:0) y nitidez adaptativa (AMD CAS). Sin demora. | filtro `ba_mejora_imagen` (`src/obs-module/ba_mejora_imagen.cpp`, `data/mejora-imagen.effect`) |

Con cámara motorizada **y** digital, el joystick mueve uno u otro: **óptico** (la cabeza) o **digital** (el recorte).
Los presets son de lo que mueve el joystick.

Los dos filtros son shaders de libobs: libobs los traduce a **D3D11** (Windows) u **OpenGL** (macOS, Linux), así que
corren en cualquier GPU (Intel, AMD, NVIDIA, Apple) sin CUDA, VideoToolbox ni modelos. En una M1 Pro, Mejorar
imagen cuesta ~1,2 ms por cuadro 1080p; está pensado para que entre en una integrada (Radeon Vega, Intel UHD): dos
pasadas a resolución completa y una a un cuarto. Orden en la fuente: primero Mejorar imagen (se limpia), después el
PTZ digital (se amplía).

## Posiciones con nombre

Cada cámara tiene su lista de **posiciones**: lugares a los que se vuelve por su nombre («Atril», «Mesa del
jurado»), aparte de los presets rápidos 1-16. Cada una guarda:

| Campo | Qué es |
|---|---|
| `id` | Número que no cambia aunque cambie el nombre (y no se reusa al borrar) |
| `nombre` | Único en la cámara, sin importar mayúsculas ni tildes; no puede ser solo un número |
| `comentario` | Para el operador |
| `definicion` | La resolución de la entrada cuando se registró (`ancho`, `alto`) |
| `digital` | El encuadre digital (`x`, `y`, `zoom`), si el PTZ digital estaba prendido |
| `camara` | Dónde quedó grabada en la cámara: `backend` y `memoria` (VISCA, Panasonic) o `token` (ONVIF) |
| `creada`, `modificada` | Fechas |

- **Registrar** toma donde está ahora la cámara: el recorte y, si es motorizada, la graba en una memoria propia.
  VISCA y Panasonic usan memorias de arriba hacia abajo (128, 127… / 100, 99…), sin pisar los presets rápidos 1-16;
  ONVIF crea una memoria nueva y guarda el token que devuelve la cámara.
- **Ir** manda la cabeza a su memoria y el recorte a su encuadre (en los segundos que se pidan). Avisa si la
  definición de la entrada cambió (el recorte es proporcional: cae en el mismo lugar) o si la cámara cambió de
  backend desde que se grabó.
- **Regrabar** la vuelve a registrar donde está ahora (en la misma memoria). **Borrar** libera la memoria en la
  cámara.
- En la página: sección **Posiciones** (nombre + comentario → «Guardar donde está ahora»; en cada una: Ir,
  Regrabar, Editar, Borrar). Se guardan con la cámara en `Config/camaras.json`.

## Backends de hardware

| Backend | Cámaras | Cómo habla |
|---|---|---|
| `visca` — transporte `sony` | Sony BRC/SRG/FR7, BirdDog, Avonic, Marshall, AVer, Lumens | UDP 52381 con el encabezado de 8 bytes de Sony (tipo, largo, secuencia); arranca reiniciando la secuencia |
| `visca` — `tcp` / `udp` | PTZOptics y la mayoría de las chinas | VISCA crudo: TCP 5678 o UDP 1259 |
| `onvif` | Cámaras IP en general (Profile S) | SOAP 1.2 a mano (`QNetworkAccessManager`, `QXmlStreamReader`): GetSystemDateAndTime → GetCapabilities → GetProfiles (el perfil con PTZ) → GetPresets; ContinuousMove/Stop, GotoPreset/SetPreset, GotoHomePosition, foco por Imaging. WS-Security UsernameToken (digest con la hora de la cámara) y HTTP Digest si la cámara lo pide. **Buscar en la red**: WS-Discovery (multicast 239.255.255.250:3702, desde cada placa) |
| `panasonic` | AW-HE40/42/50/60/70/130, AW-UE70/100/150/160 | HTTP `GET /cgi-bin/aw_ptz?cmd=%23...&res=1` (#PTS, #Z, #F, #D1, #R/#M, #APC), con los 130 ms que pide entre órdenes |

El joystick manda muchas órdenes por segundo: cada backend las junta (VISCA cada 40 ms, ONVIF cada 100 ms, Panasonic
130 ms) y sale la última; **parar** sale primero que nada. Si la cámara no contesta, la entrada queda «sin
respuesta» (punto amarillo en la lista).

Los backends están escritos desde las especificaciones de cada protocolo, no copiados: el proyecto es GPLv3 y la
referencia más usada (obs-ptz) es «GPLv2» sin «o posterior», incompatible.

## Configuración

- **Configuración → Video → «Mejorar imagen de las cámaras»** y sus tres intensidades (ruido, color, nitidez,
  0-100): lo general. En `Config/app.json`, `video.mejora`.
- **Cámaras PTZ → Digital e imagen**: el PTZ digital de cada entrada y Mejorar imagen por entrada (*como lo
  general*, *siempre*, *nunca*).
- **Cámaras PTZ → La cámara**: el backend y sus campos. Todo en `Config/camaras.json`, por nombre de entrada (si la
  entrada se borra y se vuelve a agregar con el mismo nombre, recupera su PTZ). Las claves de las cámaras van
  **cifradas** en `Credenciales/`, nunca en el json.

Los filtros se ponen y se sacan solos: cada 1,5 s (y cuando cambian las entradas o se aplica Configuración) se revisa
que cada entrada tenga lo que le corresponde.

## La página

- Lista de cámaras con miniatura, backend y estado.
- Vista viva de la entrada (ya recortada) y **mapa del encuadre digital**: clic = centrar ahí, rueda = zoom.
- **Joystick** (más lejos del centro = más rápido; soltar = parar), zoom y foco con mantener apretado, velocidad,
  home / cuadro completo.
- **Presets** 1-16: tocar = ir; «Guardar…» y tocar = guardar.
- Backend con su formulario y «Buscar en la red» (ONVIF).

## MCP (categoría `camaras`)

| Herramienta | Qué hace |
|---|---|
| `ptz_camaras` | Cada entrada con su PTZ: backend, si contesta, capacidades, modo, encuadre digital, Mejorar imagen |
| `ptz_mover` | Mueve `duracion_ms` y para (`pan`, `tilt`, `zoom`, -1..1) |
| `ptz_parar` | Para todo |
| `ptz_preset` | `ir` o `guardar` el preset `numero` |
| `ptz_encuadre` | Encuadre digital exacto (`x`, `y`, `zoom`, `segundos`) |
| `ptz_configurar` | Backend y sus datos; `digital`, `modo`, `mejora` |
| `ptz_buscar` | Busca cámaras ONVIF en la red |
| `ptz_posiciones` | Las posiciones con nombre de una cámara (o de todas) |
| `ptz_posicion_crear` | Registra donde está ahora la cámara con `nombre` y `comentario` |
| `ptz_posicion_editar` | Cambia `nombre`, `comentario`; `regrabar` la vuelve a registrar |
| `ptz_posicion_borrar` | La borra (y libera su memoria en la cámara) |
| `ptz_ir_posicion` | Lleva la cámara a la posición (id o nombre), en `segundos` |
| `video_mejora` | Mejorar imagen general (`activa`, `ruido`, `color`, `nitidez`) |

La guía para el asistente está en `help {"topic": "camaras"}`.

## Pruebas

- `test_ptz` (`src/app/tests/test_ptz.cpp`): los bytes VISCA, las órdenes Panasonic, el XML ONVIF y el digest de
  WS-Security (vector calculado aparte), y cada backend contra una **cámara falsa** en 127.0.0.1: VISCA de Sony por
  UDP (encabezado y secuencia), VISCA crudo por TCP (respuestas pegadas), Panasonic por HTTP (los 130 ms), ONVIF por
  HTTP (la cámara falsa recalcula el digest de cada pedido: la clave tiene que ser la correcta; y con la clave
  equivocada la app lo dice) y WS-Discovery. También las posiciones: nombres únicos sin tildes, búsqueda por id o
  nombre, memorias libres de arriba hacia abajo, json, y en ONVIF grabar (token de la cámara) y borrar.
- `test_mejora_imagen` (`src/obs-module/tests/`): los dos filtros en la GPU real (OpenGL en Mac/Linux, D3D11 en
  Windows): que el shader compile en ese backend, que el ruido baje, que un cambio grande no deje estela, que el
  color corrido se vaya, que el borde gane contraste; y el PTZ digital: recorte, límites, joystick con rampa y presets.
  Con argumentos pasa un clip crudo por el filtro y mide.

## Lo que falta

- **Probado contra cámaras falsas, no contra cámaras de verdad**: el primer contacto con cada modelo puede pedir
  ajustes (velocidades máximas, perfiles ONVIF raros).
- VISCA por **puerto serie** (RS-232/422), Pelco-D/P, Canon XC, JVC, Blackmagic SDI y **NDI PTZ** todavía no (la
  interfaz no cambia al agregarlos: es un `PtzBackend` más).
- Validación en Windows (D3D11) y Linux (Mesa): el shader se traduce distinto en cada uno; `test_mejora_imagen` es la
  prueba a correr ahí.
- Los temporizadores de GPU por pasada (proc `tiempos` de Mejorar imagen) no andan en el OpenGL de Apple Silicon (lo
  ignora); en Windows y Linux sí.
