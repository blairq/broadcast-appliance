# Botonera: la superficie de control

Una grilla de botones, faders y perillas en una **tablet** (emparejada por QR), un **Stream Deck**, un
controlador **MIDI** o una superficie **OSC** (TouchOSC, Open Stage Control). Cada celda dispara algo del
sistema (cortar, armar una escena, mutear un micrófono, mover un PTZ, correr una receta) y muestra
estado vivo: el tally de la escena, si se está grabando, el loudness, el orador que está al aire.

Es la misma idea que Bitfocus Companion, pero adentro del programa: los permisos, la ejecución y el
registro de lo que pasa viven en la app. El aparato solo dice «apretaron la celda (x, y)».

## Armar una botonera

Página **Botonera** (riel, grupo Producción) → **Editor**:

1. **Perfiles.** Cada perfil es un *puesto* (director, audio, gráficos, sala) con su lista de
   **herramientas permitidas**: lo que no está en la lista, el aparato no lo puede hacer aunque la celda
   lo pida. De fábrica hay un perfil **Director** armado con las escenas que existen. Con «＋» se crea
   otro, vacío o desde una plantilla (Director, Audio, Asamblea, Prompter).
2. **Páginas.** Una grilla de columnas × filas (hasta 16 × 16). Un perfil puede tener varias; en la
   tablet se cambia con las pestañas de abajo, o con una celda de acción «página».
3. **Celdas.** Tocá un hueco o una celda: tipo (botón, fader, perilla, medidor, texto, reloj), texto,
   colores, herramienta y argumentos. Lo avanzado (estado vivo, valor del fader, acciones al mantener o
   al soltar) va en el JSON de abajo. **Doble clic** en una celda de la grilla: la prueba.
4. **Permisos.** La lista de herramientas del perfil se edita a mano (separadas por coma, o `*` para
   todas). El asistente puede armar celdas pero **no puede sumar permisos**: eso lo decide una persona.

También se arma por MCP (`botonera_*`; el asistente sabe hacerlo: pedíselo).

## Emparejar una tablet

Página Botonera → **Aparatos** → elegí el perfil, un nombre y cuántos días sin uso hasta que venza →
**Generar QR**. Escanealo con la tablet: abre `https://<IP de esta máquina>:8789/botonera/` con un
código de un solo uso (vence en 2 minutos) en la parte `#p=` del enlace (no viaja al servidor ni queda
en registros). La tablet pide emparejarse y en la app aparece **«… pide emparejarse»**: **Aprobar**.

- **El gateway tiene que escuchar en la red local** (Configuración → Red). Si escucha solo en esta
  máquina (127.0.0.1), la página avisa al generar el QR.
- El certificado HTTPS es propio: la primera vez la tablet muestra una advertencia. Para estar seguro de
  que es esta máquina, compará la **huella** que muestra la página con la del navegador. Con un
  certificado de verdad (dejá `cert.pem` y `key.pem` en `Credenciales/`) no hay advertencia.
- En la tablet, «Agregar a la pantalla de inicio» la deja como una app a pantalla completa.

## Seguridad

- **Solo red local**: se aceptan direcciones privadas (192.168.x, 10.x, 172.16–31.x), link-local y esta
  máquina. Lo que llega por el túnel de Cloudflare se rechaza.
- **Token por aparato** de 256 bits, que la tablet guarda y manda en el primer mensaje (nunca en la
  URL). La app guarda solo un hash con sal. Vence a los N días sin uso (de fábrica 30) y se revoca uno
  por uno o todos (Aparatos → Revocar): la conexión se corta en el acto.
- **Permisos del lado de la app**: cada acción se compara con las herramientas del perfil antes de
  ejecutarse. El aparato nunca manda nombres de herramientas.
- **Acciones de aire**: con la transmisión o la grabación andando, lo que cambia el aire pide
  confirmación **en el aparato** (en la tablet, mantener apretado 1 s; en un Stream Deck, apretar de
  nuevo la tecla que titila en amarillo, dentro de 5 s).
- **Límites**: 5 intentos de emparejar por IP cada 10 minutos, 20 acciones por segundo por aparato
  (los faders aparte, a 30 por segundo), hasta 32 aparatos, latido de 30 s.
- **Auditoría**: cada emparejamiento, conexión, acción hecha o rechazada (y por qué), confirmación y
  edición queda en `Logs/botonera.log` y en la pestaña **Auditoría**.

## Hardware

Los aparatos de hardware se manejan con **plugins** (página Plugins → copiar un ejemplo):

| Ejemplo | Para |
|---|---|
| `superficie-streamdeck` | Elgato Stream Deck (Mini, MK.2, XL, Neo, Pedal, +), teclas con imagen y perillas del + |
| `superficie-midi` | X-Touch Mini, nanoKONTROL, Launchpad…: notas = botones, CC = faders/perillas, LED y faders motorizados |
| `superficie-osc` | TouchOSC, Open Stage Control (con lista de IP permitidas) |
| `tally-tsl-umd` | Tally a monitores, cámaras o mezcladores por TSL UMD v5 (UDP) |

Al prenderse, cada aparato aparece en **Aparatos** con su modelo; ahí se le asigna perfil y página.
Las bibliotecas (hidapi, streamdeck, mido, python-rtmidi, python-osc, pyserial) vienen con el programa:
no hace falta instalar nada. Para un aparato que no está en la lista, pedíselo al asistente («tengo un
X-keys de 24 teclas, armame un plugin»): escribe el plugin sobre `ba_plugin.superficie`.

**Linux:** para que un usuario común abra un Stream Deck (o cualquier HID) hace falta una regla de udev,
una sola vez, como administrador:

```
# /etc/udev/rules.d/70-botonera.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fd9", TAG+="uaccess"
KERNEL=="hidraw*", ATTRS{idVendor}=="0fd9", TAG+="uaccess"
```

y después `sudo udevadm control --reload-rules && sudo udevadm trigger` (desenchufar y volver a
enchufar el aparato).

## Companion Satellite

Si ya tenés aparatos que maneja **Bitfocus Companion Satellite** (Stream Deck, Loupedeck, X-keys,
Infinitton, paneles Blackmagic…), no hace falta ningún plugin: Satellite habla con la botonera como si
fuera un Companion.

1. Página Botonera → Aparatos → **Companion Satellite** → **Prender** (puerto 16622 de fábrica).
2. En la máquina (o Raspberry Pi) donde están los aparatos, en Companion Satellite poné la IP de esta
   máquina.
3. La primera vez, su IP aparece como «quiere conectar un Satellite»: **Permitir**. (El protocolo de
   Satellite no tiene clave: por eso solo se acepta la red local y las IP que vos permitas.)
4. Cada aparato aparece en la lista como «Companion Satellite»: asignale perfil y página. Conviene una
   página del mismo tamaño que el aparato (un Stream Deck MK.2 es 5 × 3).

Las teclas muestran el texto, el color y el tally de cada celda; las acciones de aire se confirman
apretando de nuevo la tecla que titila en amarillo.

## El bus de eventos

Lo que muestra cada celda sale del **bus de eventos** de la app (pestaña **Eventos**): temas como
`aire/tally`, `aire/escena`, `salida/grabacion`, `audio/nivel/<fuente>`, `audio/loudness`,
`alarma/<regla>`, `oradores/al_aire`. Los mismos temas los escuchan los plugins
(`@plugin.escuchar("aire/tally")`) y el asistente (`eventos_temas`, `eventos_esperar`).

## Caras, bordes e insignias (micrófonos de bancas)

Además de `fondo`, `color`, `texto`, `titilar`, `valor`, `icono`, `activo` y `nivel`, una celda puede tener:

| Efecto / estilo | Qué hace |
|---|---|
| `imagen` | Una referencia: `cuerpo:<banca>` (la foto del integrante, subida o capturada con la cámara de su banca) o `ptz:<cámara>/<posición>` (la foto de una posición PTZ). La app la manda como miniatura de 96 px dentro del estado de la celda: la tablet, el Satellite (Companion) y el Stream Deck la dibujan de fondo. |
| `borde` | Un color de borde propio (le toca hablar, presidente). |
| `borde_titilar` | Que titile solo el borde (1 Hz). |
| `insignia` | Un texto chico arriba a la derecha (el turno, «P»); acepta `{{v}}`. |
| `atenuar` | La celda se ve apagada (sin unidad de la central, sin central). |

Las rutas de las ligaduras aceptan **filtros**: `pedidos[banca=5].turno` es el turno del pedido de la banca 5, y `abiertos.#` es cuántos elementos tiene la lista. Una acción con `"confirmar": true` se confirma en el aparato (como las de aire): la usa el micrófono del presidente.

La plantilla **«Micrófonos de bancas»** arma una página con cada banca donde está en el recinto, con su foto: un toque le da o le corta el audio por la central (`dcn_microfono` con `{{no_activo}}`), mantener la pone al aire completa (`banca_al_aire`); abajo Siguiente, Cerrar todos, cuántos abiertos, quién habla, Limpiar pedidos y TAKE. Ver docs/bosch/open-interface.md §11.

## Lo que falta

- Probar con una tablet y con hardware reales (se probó con una tablet simulada y un aparato falso).
- Probar Companion Satellite de verdad (se probó con un cliente simulado que habla su protocolo) y la
  «API remota» HTTP para el módulo Generic HTTP de Companion.
- Editar celdas desde la tablet (el perfil tiene la opción, pero la tablet todavía no trae el editor).
