# Ideas y pendientes

Lo que queremos hacer y todavía no está. Se va agregando a medida que aparece,
para que no se pierda. Cuando algo se hace, se borra de acá (el detalle queda
en el historial de git).

## Eventos (lo grande)

El storyboard, el teleprompter, las pantallas, las cámaras de público y el
asistente de armado están desglosados aparte: **[EVENTOS.md](EVENTOS.md)**.

## Próximo

- **Subir el cupo de invitados para medir de verdad.** La herramienta de carga
  ya está (`tools/test/carga-sala.py`), pero la sala está en 2 lugares y no se
  puede pasar de ahí sin reiniciar el gateway. Lo medido hasta ahora, con 8
  conectados (6 mandando video: 2 adentro + 4 de lobby) en esta misma máquina:
  **13 % de CPU** del proceso y el hilo más cargado (`IncomingVideoStream`, la
  decodificación) en **32 %**. Son ~2 puntos de CPU y ~5 % de un núcleo por
  cada video que entra. Si escala parejo, 16 videos serían ~35 % de CPU y el
  hilo al 80 % — o sea que el techo va a ser ese hilo, no la máquina. Hay que
  confirmarlo con el cupo en 16, y desde otro equipo, porque acá los ocho
  Chrome le compiten la CPU al gateway.
- **La gente del lobby también cuesta, pero está acotada.** El que espera
  conecta y manda video igual que el de adentro; lo que lo contiene es
  `lobbySlots` (4): del quinto en adelante esperan sin cámara. O sea que una
  cola larga no dispara el costo. Si algún día se sube ese número, sube la
  carga en la misma proporción.
- **La memoria del gateway no es una fuga: es un pozo acotado.** Se veía que
  subía de 91 MB a 345 MB y no volvía. Medido: los buffers de sesión del
  MediaPump se reservan al usarse por primera vez y quedan reservados, y la
  cantidad de sesiones la fija la capacidad configurada (con 16 invitados:
  `slots=16, lobby=4, fila=6, sesiones=32`). Con 36 invitados entrando y
  saliendo de una sala de 16, la memoria se movió 2 MB (353 → 355 MB) y ahí se
  quedó. O sea que el techo se puede calcular de antemano y no crece durante el
  evento. **Lo que falta** es que ese número se vea antes de arrancar, para que
  nadie configure 16 invitados en una máquina que no los aguanta.
- **Correr la prueba desde otro equipo.** La herramienta ya trae los dos modos
  (`--solo-medir` acá, `--sin-admin` allá, probado contra el HTTPS propio). Lo
  que falta es el equipo.

## Gráficos animados (zócalos, placas, tickers)

Un motor propio para armar gráficos animados, en dos niveles: **declarativo**
(un JSON con capas y animaciones de un repertorio fijo, que cubre zócalos,
placas y tickers sin correr código) y **QML propio** para lo que ese
repertorio no alcance.

Decidido: QML y no navegador. En el bundle de Linux no hay CEF, traerlo son
~200 MB y un Chromium por fuente; y sobre todo, los parámetros como
propiedades no tienen la carrera que tiene mandarlos por HTTP a una página que
todavía está cargando. Con un cue que dispara a tiempo, esa diferencia es
todo. El costo es no poder usar los packs de overlays que existen para OBS,
que son HTML.

Orden: motor con renderizado **fuera de proceso** (que un gráfico roto no
cuelgue la app, y que sea la caja de arena del nivel 2), después las
herramientas MCP, y el IDE al final.

Lo que hay que tener en cuenta, ya verificado contra el Qt 6.6.3 vendoreado:

- **`QtQuick.Effects` está** (`MultiEffect`: desenfoque, sombra, máscara).
  **`Qt5Compat.GraphicalEffects` NO** — y es el que usan casi todos los
  ejemplos que andan dando vueltas, así que el validador tiene que decirlo
  claro o la LLM lo va a escribir por inercia.
- **`qsb` no está**: sin él no hay shaders propios (Qt 6 no acepta GLSL en
  texto). `MultiEffect` cubre casi todo.
- **El plugin SVG está en el Qt vendoreado pero no se copia al bundle.**
  Ojo: en 6.6 un `.svg` como imagen se rasteriza una vez (no hay
  `VectorImage`, que es de 6.8+). Para formas animables, `PathSvg` acepta el
  atributo `d` de un SVG tal cual y sigue siendo vector.
- El reloj de las animaciones se maneja a mano (`QAnimationDriver`): sin eso
  no hay vista previa reproducible ni barra de tiempo con arrastre.
- Transparencia: alfa premultiplicado y suavizado de texto en escala de
  grises, o el texto blanco sale con halo oscuro.

### To-do del IDE

- **Chat con la LLM adentro del IDE**, para pedirle cambios sobre el gráfico
  que se está editando sin cambiar de pantalla.
- **Una forma liviana de chatear con la LLM** en general, sin levantar el CLI
  entero que usamos hoy.

## Cuidado al probar

- **Nunca dos sistemas a la vez en la misma máquina.** El canal de control
  (`@broadcast-appliance-<uid>-control.app`) y el bloque de salud `BA.Health`
  son únicos POR USUARIO, no por carpeta de datos: una segunda instancia —
  aunque se la lance con `BA_DATA_ROOT` apuntando a otro lado — le roba el
  canal a la primera, el launcher la da por colgada («heartbeat estancado») y
  la reinicia en bucle hasta pasarla a modo seguro. Para probar un binario hay
  que parar el launcher primero.
- **Después de recompilar e instalar, regenerar el manifiesto**
  (`tools/deploy --solo-manifest release`): si no, el launcher avisa
  «integridad: N con hash distinto» en cada arranque. `make release` ya lo
  hace; `cmake --install` solo, no.

## Sala y oradores

- **Plantillas de audio.** Guardar un rack con nombre (módulos, cables y
  parámetros), aplicarlo a otra fuente y marcar uno como predeterminado por
  tipo de fuente, para que los invitados lo tomen solos al crearse su rack.
  Guardadas en `Config/audio-plantillas.json` (viajan con la carpeta portable).

## Audio

- **Salidas simultáneas por dispositivo.** Hoy libobs reproduce el monitoreo
  por UNA salida a la vez: si se elige otra, cambia para todos. Para tener de
  verdad una salida por tarjeta hay que escribir nuestra propia capa de audio
  (PulseAudio en Linux, WASAPI en Windows, CoreAudio en macOS).
- **Patchear canales sueltos entre módulos** ya funciona (L/R). Falta ver si
  hace falta lo mismo para los puertos de sidechain con más de 2 canales.

## Rendimiento

- **Ancho de banda real de WebRTC.** El panel mide CPU y memoria del gateway,
  no bytes de red. Los bytes salen de `getStats()` de libwebrtc (por conexión:
  bitrate de entrada, paquetes perdidos, jitter). Sería lo que falta para
  decidir con números si la red es el límite.
- **Repartir las conexiones en varias fábricas de libwebrtc.** Hoy hay un solo
  trío de hilos (red, worker, señalización) para todas las conexiones: con
  muchos invitados ese hilo satura un núcleo aunque la máquina esté ociosa.
  Hacerlo recién cuando la medición del hilo más cargado lo confirme.

## Bundle y documentación

- **Asistente de primer arranque**: cámara, micrófono, destino y prueba.
- **«Reconectando…» con la causa** cuando se cae la transmisión: hay que
  conectar las señales de las salidas de libobs.
