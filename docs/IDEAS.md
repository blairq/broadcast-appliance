# Ideas y pendientes

Lo que queremos hacer y todavía no está. Se va agregando a medida que aparece,
para que no se pierda. Cuando algo se hace, se borra de acá (el detalle queda
en el historial de git).

## Eventos (lo grande)

El storyboard, el teleprompter, las pantallas, las cámaras de público y el
asistente de armado están desglosados aparte: **[EVENTOS.md](EVENTOS.md)**.

## Próximo

- **Prueba de carga con invitados emulados.** Levantar N clientes WebRTC
  (Chrome headless contra la app web del invitado) para ver cuánto aguanta la
  sala. Primero en esta máquina; después, desde un servidor por SSH contra este
  equipo, para que la prueba no compita por CPU con el gateway. Mirar durante
  la prueba el panel «Uso de la sala»: CPU total y, sobre todo, el hilo más
  cargado.

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
- **`docs/TODO.md` y `docs/TASKS.md` están desactualizados**: archivarlos o
  rehacerlos.
