# El tablero web (el dashboard de la app en el navegador)

`/control/app/` muestra en una tablet, una notebook o un teléfono **el mismo dashboard de
la app**: las mismas tarjetas, con la misma vista, el mismo fondo y las mismas acciones.
Arriba lleva el **preview**, el **programa** y el **TAKE**.

No es una copia: es la misma interfaz de la app (sus archivos QML) compilada para el
navegador con Qt para WebAssembly. Cuando se cambia una tarjeta en la app, cambia también
en la web, sin hacer nada aparte.

**Es el tablero web de entrada.** El control web en HTML (`/control/`) quedó **en desuso**:
un aparato con el perfil «todo» que entra a `/control/` pasa solo a `/control/app/`. El HTML
queda para los aparatos con un perfil acotado (el tablero nuevo necesita «todo») y, a pedido,
en `/control/?clasico` (es lo que ofrece el tablero nuevo cuando no puede arrancar). Las
superficies de una página (presentador, cámara, escaleta) siguen como estaban.

## Cómo se abre

1. Emparejá el aparato **una sola vez**, como para el control web: en la app,
   **Servicios → Control web → Emparejar**, escaneá el QR con el aparato y aprobalo en la
   app. Para el tablero completo el aparato necesita el perfil **«todo»**.
2. Aprobado con el perfil «todo», el aparato pasa solo al tablero. Después basta con abrir
   el enlace del control web (`/control/`) o directo `/control/app/`. No pide nada más:
   usa el permiso que el aparato guardó al emparejarse. Si refrescás la página o se
   reinicia la app, vuelve solo.

La primera vez baja unos 10 MB (comprimidos); después el navegador lo guarda y abre en un
par de segundos. Cuando sale una versión nueva de la app, la baja de nuevo una vez.

## Qué hay

- **Una barra arriba**, siempre a la vista: un botón por **salida** (con su nombre y su
  color; la elegida, marcada), **▴ Ocultar monitores**, **▦ Disposición** y **⤢ Pantalla
  completa**.
- **Preview y programa** de la salida elegida, en vivo, con el **TAKE** y el **CUT** (en un
  teléfono parado, uno debajo del otro). Se pliegan con «▴ Ocultar monitores»: plegados, la
  app deja de mandar esos videos.
- **Las tarjetas del dashboard** (aire, escenas, fuentes, mezclador, monitor, oradores,
  invitados, salidas, salud, PTZ, línea, dirección, PDF, carrusel y lobby), compactas: el
  título aparece al pasar el mouse o tocar la tarjeta.

### El Mezclador en su pestaña

En la tarjeta Mezclador, **↗ Mezclador** abre en una pestaña nueva del navegador el Mezclador
entero, el mismo que la app abre en su ventana: las tiras con sus faders y vúmetros, mute,
escucha (PFL/AFL), AFV, retardos, los grupos (VCA), las salidas y buses, la matriz y la
calibración. Usa el mismo aparato (sin volver a emparejar) y la misma salida elegida. Los
faders se mueven en vivo; mientras la pestaña está escondida la app no le manda nada.

En la calibración de sala, la barra del asistente también anda desde la web: lo que se le
pide (o los pedidos ya escritos, como «Explicame la sala») le llega al asistente de la app
con el contexto del panel. Lo que responde se ve ahí mismo, desplegando «Lo que responde el
asistente» (las últimas líneas de su pantalla), y completo en la pestaña Asistente de la app.
El dictado por voz es solo en la app (usa su micrófono).

Con **«Avisarle al asistente al terminar cada medición»** (prendido de fábrica, se guarda con la
sesión de calibración), al terminar cada barrido el asistente mira la medición y el análisis
solo y le cuenta al operador qué se nota de la sala y qué conviene hacer. No aplica nada ni
vuelve a medir sin que se lo pidan.

### Un tablero propio por aparato

El tablero web **no es el de la app**: las tarjetas que se abren, cierran, mueven o achican, y
el fondo, son de ese aparato. Se guardan en su registro de la app (vuelven aunque se borre el
navegador o se reinicie la app) y nunca cambian el tablero de la pantalla de la app. «▦
Disposición → Restablecer» lo deja como vino de fábrica.

### Salidas múltiples

Con varias salidas, el aparato **maneja la que elige arriba**: escenas, preview, programa,
TAKE, línea… todo el tablero trabaja sobre esa salida, sin cambiar la que tiene elegida la
app. La elegida se recuerda por aparato (es la misma que en el control web HTML).

### Idioma

El tablero sale en el idioma de la app (Configuración → Idioma), con sus mismas
traducciones. Cambiarlo en la app vale en la próxima carga de la página. Para ver un aparato
en otro idioma, `?idioma=en` (o `pt-BR`, `fr`…) en la dirección.

### Pantalla completa

«⤢ Pantalla completa» usa la del navegador. En iPhone y iPad la página no puede ponerse en
pantalla completa sola: agregala a la pantalla de inicio (Compartir → Agregar a inicio) y
abrila desde ahí.

Lo que es de la máquina donde corre la app no está en la web: sacar algo a una ventana, abrir
otra página de la app, el dictado por voz.

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
- Desde la web se puede hacer **solo lo que hacen los botones del dashboard** (una lista
  sacada de las mismas tarjetas): nada más del programa. Cada botón sigue las reglas de la
  herramienta equivalente del asistente: el perfil del aparato y, si cambia lo que sale y
  hay aire (transmisión o grabación), la misma pregunta «Cambia el aire» que el control web.
- Lo que se cambia desde la web (TAKE, una escena, un volumen) queda en
  el registro con el nombre del aparato, y también lo que se intentó y no se dejó.
- Nunca salen por ahí claves, cuentas ni los datos de los otros aparatos.

## Licencias

El tablero lleva Qt adentro (enlazado de forma estática, como se hace en el navegador). Qt
se usa bajo la LGPL-3.0 y el conjunto se distribuye bajo la GPL-3.0. Junto al tablero, en
`Web/control/app/licencia/`, van las instrucciones y los objetos para volver a enlazarlo
con otra versión de Qt.

## Para quien lo arma

- `make deps-wasm` baja Qt 6.8.3 para WebAssembly y emscripten 3.1.56 a `extern/` (nada se
  instala en el sistema). `make tablero-web` lo compila y deja el resultado en
  `build-tablero-web/salida/`; el bundle lo lleva a `Web/control/app/` en las tres
  plataformas (es el mismo archivo para todas).
- Fuente: `src/tablero-web/` (el marco, la réplica del modelo, el video) y
  `src/app/control/ReplicaPublicador` y `CuadrosWeb` del lado de la app.
