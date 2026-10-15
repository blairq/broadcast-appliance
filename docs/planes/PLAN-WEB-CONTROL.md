# Plan: las interfaces web de control (neumorfismo) y el dashboard web

Pedido del 2026-10-15: rediseñar TODAS las interfaces web de control con neumorfismo
(botones, sliders, perillas), rehacer la consola web del operador y sumar un dashboard web
avanzado con la mayor cantidad de funciones (PTZ, mezclador de audio…), donde cada
tarjeta se puede abrir a pantalla completa en una pestaña nueva. Quedan afuera la página
web del evento y el teleprompter (al que solo se le arregló el mensaje en espejo).

## Lo que hay hoy (inventario)

| Interfaz | URL | Para quién | Cómo habla con la app | Estado |
|---|---|---|---|---|
| Consola del operador | `/admin/` (solo 127.0.0.1:8788) | operador | REST + SSE del gateway | solo la sala de invitados; no se puede usar desde un celular o tablet; pide pegar un token |
| Consola de presidencia | `/presidencia/` (red local o túnel, PIN) | quien preside | WS `/ws?presidencia=1` | completa, estilo «vidrio» propio |
| Botonera | `/botonera/` (red local, emparejada) | superficie de control | WS `/ws?botonera` → celda → acción → herramienta MCP | grilla genérica |
| Rótulos | `/rotulos/#k=` | quien escribe los textos | sondeo cada 1,5 s | básica |
| Corrección de subtítulos | `/correccion/#k=` | quien corrige | sondeo cada 0,5 s | básica |
| Teleprompter (lector y control) | `/prompter/` | orador / operador | sondeo cada 0,5 s | **afuera del rediseño** |
| Participantes | `/invitados/`, `/sesion/`, `/publico/` | invitados, integrantes, público | WS / REST | no son de control (ver «Para decidir») |
| Pantallas | `/cast/`, `/pantalla/…`, `/board/` | televisores | WS / sondeo | no son de control |

Problemas de fondo:
- **No hay un sistema de diseño común:** cada página tiene su CSS y sus colores. `/comun/` solo tiene JS.
- **La API que lo cubre todo (MCP, 670 herramientas) solo escucha en 127.0.0.1**, y por HTTP entra como «asistente»: sin `humano`, con el resguardo del aire y las aprobaciones. Desde la red, lo único que llega a la app son la botonera y Companion, de forma indirecta.
- **Mucho sondeo** (cada 0,5 o 1,5 s) en vez de un canal que empuje los cambios.

## 1. El sistema de diseño: `/comun/neuro/`

Un solo lenguaje para todas las interfaces de control, sin frameworks (como todo lo web
del sistema): `neuro.css` (tokens y estilos) más `neuro.js` (componentes como Web
Components, sin dependencias).

- **Neumorfismo** (soft UI): superficies del mismo color que el fondo, con relieve por luz y sombra.
  - «En relieve»: un botón suelto.
  - «Hundido»: apretado, un campo, la canaleta de un fader.
  - Los tokens se derivan de un color base: luz arriba a la izquierda, sombra abajo a la derecha.
  - **Variante oscura** (de fábrica, para regia y sala a oscuras) y **clara**.
- **Lo que manda es la legibilidad y el estado:** el neumorfismo puro tiene poco contraste. Por eso:
  - el texto y los íconos tienen contraste AA;
  - **lo que está al aire va en rojo tally sólido**, lo armado en verde y lo prendido con un anillo de color;
  - el relieve es para la forma, el color para el estado.
- **Componentes:** `<ba-boton>` (con modo «mantener» y confirmación), `<ba-toggle>`, `<ba-fader>` (vertical, con vúmetro y marca de 0 dB), `<ba-slider>`, `<ba-perilla>` (arrastrar o rueda; doble toque vuelve al valor de fábrica), `<ba-joystick>` (PTZ: pan/tilt con regreso al centro, más zoom), `<ba-vumetro>`, `<ba-tarjeta>` (con «⤢ pantalla completa»), `<ba-chip>`, `<ba-tally>`.
- **Táctil primero:** objetivos de 44 px o más, gestos (arrastrar y pellizcar), sin depender del hover.

## 2. El puente seguro: `/ws?control`

El dashboard y las consolas nuevas necesitan llegar a casi todo lo de la app desde la red
local. Se copia el patrón de presidencia y sesión: el gateway pasa los mensajes a la app,
que es la que decide.

- **Emparejar el aparato**, igual que la botonera: QR → pedido → **una persona lo aprueba en la app**. El aparato guarda su token (en la app se guarda el hash).
  - Cada aparato tiene un **perfil**: qué categorías de herramientas puede usar (por ejemplo, «PTZ y audio»; «todo»).
  - Ampliar el perfil lo hace una persona, nunca el aparato ni el asistente.
- **Mensajes:**
  - `llamar {herramienta, args}`: corre una herramienta MCP con `origen: "control-web"` y `humano: true`, porque es una persona apretando, como la botonera. Así no pide aprobación ni cae en el resguardo del asistente, pero las herramientas «de aire» siguen pidiendo confirmación en pantalla.
  - `mirar {temas}`: la app **empuja** el estado de lo pedido (escenas, audio con vúmetros a 15 Hz, PTZ, salidas, oradores, latencias…). No hay sondeo.
- **El video** (programa, preview, cada cámara) va por WebRTC, como el tablero y la presidencia.
- **Red local, y por el túnel solo si se habilita a propósito** (con PIN).
- **Auditoría:** cada llamada queda en el registro con el aparato y la persona.

## 3. El dashboard web: `/control/`

La consola completa para tablet, notebook o PC. Es la versión web del tablero de la app,
y reemplaza a la vieja consola `/admin/`.

- **Tarjetas** que se mueven, se agrandan, se esconden y se guardan por aparato:
  - **Escenas:** preview y programa en video, la grilla de escenas, CORTE / AUTO, T-bar.
  - **PTZ:** cámara elegida, joystick, zoom, posiciones (con foto), la foto para apuntar con un toque y seguimiento.
  - **Mezclador de audio:** un fader por canal con vúmetro, mute, PFL, ganancia y el master con loudness.
  - **Salidas:** transmisión y grabación (arrancar con confirmación), kbps, salud.
  - **Gráficos y DSK:** zócalos, placas y capas del DSK.
  - **Rótulos:** los textos de la escena al aire (absorbe `/rotulos/`).
  - **Sala:** lobby, admitir, quién habla, chat (lo que hoy hace `/admin/`).
  - **Asamblea:** oradores y fila, dar la palabra, votación (lo de presidencia, para el operador).
  - **Teleprompter:** solo el control (play, velocidad, aviso). La página del lector no se toca.
  - **Latencias:** el resumen de Latencias del pipeline y su línea de tiempo.
  - **Estado:** CPU, temperatura, disco, alertas, lo que dice el asistente.
- **Cada tarjeta abre a pantalla completa en una pestaña nueva** (`/control/#tarjeta=ptz&camara=…`). Así se pueden tener, por ejemplo, el PTZ en una tablet y el mezclador en otra. Cada pestaña es independiente y ya viene emparejada (el token es del aparato).
- **Atajos de teclado** en la compu (los mismos que la app) y **tally** en el borde de la pantalla.

## 4. Las consolas que se rediseñan

Con el sistema de diseño y el puente nuevo:

- **Presidencia:** el mismo protocolo (`/ws?presidencia=1`) con la interfaz nueva. Botones grandes para dar la palabra, sumar tiempo y silenciar a todos; el recinto con las bancas y la toma de cada una; la votación.
- **Botonera:** las celdas como botones, faders y perillas neumórficos (hoy son cuadrados planos).
- **Rótulos y corrección:** la interfaz nueva, y del sondeo pasan al canal que empuja.
- **La consola `/admin/`:** se reemplaza por `/control/`. Queda un aviso que lleva al dashboard.

## Fases

| Fase | Qué | Cómo se prueba |
|---|---|---|
| W1 | `/comun/neuro/`: tokens oscuro/claro y los componentes, con una página de muestra (`/comun/neuro/muestra.html`) | capturas en Chrome y Safari (iPad), táctil |
| W2 | `/ws?control`: emparejar y aprobar, perfiles, `llamar` y `mirar` con empuje, auditoría; las herramientas MCP para administrar los aparatos | prueba del gateway y de la app (aparato sin aprobar: no entra; categoría fuera del perfil: no corre) |
| W3 | `/control/` con escenas, PTZ, mezclador y salidas; pantalla completa en pestaña nueva | Chrome con un aparato emparejado contra la app de prueba |
| W4 | El resto de las tarjetas: gráficos, rótulos, sala, asamblea, teleprompter, latencias, estado | igual |
| W5 | Presidencia rediseñada | la prueba de punta a punta de la asamblea, desde la consola |
| W6 | Botonera, rótulos y corrección rediseñados; `/admin/` lleva a `/control/` | capturas |

## Para decidir

1. **Oscuro o claro de fábrica.** Recomiendo oscuro (regia, sala a oscuras), con claro a un toque.
2. **Las interfaces de participantes** (invitados, integrantes remotos, público): ¿entran al rediseño? Recomiendo que entren después, en una fase propia: son para gente que no es técnica y piden otro cuidado.
3. **El dashboard por el túnel** (desde afuera de la sala): recomiendo que de fábrica sea solo red local, y por el túnel únicamente si se habilita a propósito, con PIN.
