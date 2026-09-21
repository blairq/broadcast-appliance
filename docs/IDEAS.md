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
- **La memoria no vuelve.** Subió de 91 MB a 344 MB con las corridas y ahí se
  quedó cuando los invitados se fueron. Puede ser el asignador que no devuelve
  al sistema, pero conviene mirarlo antes de un evento largo con gente que
  entra y sale.
- **Correr la prueba desde otro equipo.** La herramienta ya trae los dos modos
  (`--solo-medir` acá, `--sin-admin` allá, probado contra el HTTPS propio). Lo
  que falta es el equipo.

## Sala y oradores

- **Plantillas de audio.** Guardar un rack con nombre (módulos, cables y
  parámetros), aplicarlo a otra fuente y marcar uno como predeterminado por
  tipo de fuente, para que los invitados lo tomen solos al crearse su rack.
  Guardadas en `Config/audio-plantillas.json` (viajan con la carpeta portable).
- **El interruptor «Los atiende el asistente» no se guarda** entre arranques:
  hoy es un control de función. Decidir si va a la configuración.

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

- **Ícono del ejecutable** (Windows `.rc`, Linux `.desktop` en el bundle) y
  acceso directo: hoy sale con el ícono genérico.
- **`manifest.json` en los dist**, para que la verificación de integridad del
  launcher tenga algo que verificar (hoy pasa con 0 archivos).
- **Asistente de primer arranque**: cámara, micrófono, destino y prueba.
- **«Reconectando…» con la causa** cuando se cae la transmisión: hay que
  conectar las señales de las salidas de libobs.
