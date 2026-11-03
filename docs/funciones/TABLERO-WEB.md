# El tablero web (el dashboard de la app en el navegador)

`/control/app/` muestra en una tablet, una notebook o un teléfono **el mismo dashboard de
la app**: las mismas tarjetas, con la misma vista, el mismo fondo y las mismas acciones.
Arriba lleva el **preview**, el **programa** y el **TAKE**.

No es una copia: es la misma interfaz de la app (sus archivos QML) compilada para el
navegador con Qt para WebAssembly. Cuando se cambia una tarjeta en la app, cambia también
en la web, sin hacer nada aparte.

La superficie de control de siempre (`/control/`, en HTML) sigue estando y no cambia.

## Cómo se abre

1. Emparejá el aparato **una sola vez**, como para el control web: en la app,
   **Servicios → Control web → Emparejar**, escaneá el QR con el aparato y aprobalo en la
   app. Para el tablero completo el aparato necesita el perfil **«todo»**.
2. En ese aparato abrí `http://<la-ip-de-la-app>:8787/control/app/` (o el mismo
   enlace del control web, cambiando `/control/` por `/control/app/`). No pide nada más:
   usa el permiso que el aparato guardó al emparejarse. Si refrescás la página o se
   reinicia la app, vuelve solo.

La primera vez baja unos 10 MB (comprimidos); después el navegador lo guarda y abre en un
par de segundos. Cuando sale una versión nueva de la app, la baja de nuevo una vez.

## Qué hay

- **Preview y programa**, en vivo, con el **TAKE** y el **CUT** (en un teléfono parado,
  uno debajo del otro).
- **Las tarjetas del dashboard**, como estén acomodadas en la app: aire, escenas, fuentes,
  mezclador, monitor, oradores, invitados, salidas, salud, PTZ, línea, dirección, PDF,
  carrusel y lobby. Se pueden agregar, mover, achicar y cerrar como en la app (lo que se
  cambia se ve en las dos: es la misma disposición).
- **El fondo** del dashboard (las fuentes o la multivista elegida) y los avisos de la app.

Lo que es de la máquina donde corre la app no aparece en la web: sacar algo a una ventana,
abrir otra página de la app, el asistente. Esos botones dicen «Eso se hace en la app.».

## El video

Las vistas de video (los monitores, el fondo, las caras de los invitados) llegan como
cuadros comprimidos por la misma conexión del control web, a la medida en que se dibujan
y a los cuadros por segundo que hacen falta (los monitores grandes, hasta 25-30; las
miniaturas, 12). En la red local se ven con muy poco retraso respecto de la app. Cuantas
más vistas grandes hay abiertas, más ancho de banda usa (un monitor de 640 píxeles a 25
cuadros por segundo, unos 3 Mbit/s).

## Requisitos

- Un navegador actual: Chrome, Edge o Firefox de los últimos dos años, o Safari 16.4 o
  más nuevo (iPad, iPhone, Mac). Tiene que tener WebAssembly y WebGL2 (todos los de esas
  versiones lo tienen).
- La red local de la app (como el control web: desde afuera solo si se habilitó el acceso
  por el túnel, con PIN).
- Memoria: el tablero usa unos 200-300 MB en el navegador.

## Permisos y seguridad

- Entra solo un aparato **emparejado y aprobado por una persona** en la app, con su token
  (el mismo del control web). Quitarlo en la app lo corta enseguida.
- El tablero completo necesita el perfil «todo»; con otro perfil la página lo dice.
- Lo que se cambia desde la web (TAKE, una escena, un volumen, mover una tarjeta) queda en
  el registro con el nombre del aparato.
- Nunca salen por ahí claves, cuentas ni los datos de los otros aparatos.

## Licencias

El tablero lleva Qt adentro (enlazado de forma estática, como se hace en el navegador). Qt
se usa bajo la LGPL-3.0 y el conjunto se distribuye bajo la GPL-3.0. Junto al tablero, en
`Web/control/app/licencia/`, van las instrucciones y los objetos para volver a enlazarlo
con otra versión de Qt.

## Para quien lo arma

- `make deps-wasm` baja Qt 6.6.3 para WebAssembly y emscripten 3.1.37 a `extern/` (nada se
  instala en el sistema). `make tablero-web` lo compila y deja el resultado en
  `build-tablero-web/salida/`; el bundle lo lleva a `Web/control/app/` en las tres
  plataformas (es el mismo archivo para todas).
- Fuente: `src/tablero-web/` (el marco, la réplica del modelo, el video) y
  `src/app/control/ReplicaPublicador` y `CuadrosWeb` del lado de la app.
