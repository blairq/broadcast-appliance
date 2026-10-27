# Manual del operador

Para quien **usa** Broadcast Appliance (no para quien lo compila). Si recién lo
bajaste, empezá por `LEEME.txt`, que viene dentro de la carpeta del programa.

- [Guía del invitado](guia-del-invitado.md) — una página para pasarle a quien participa.
- *In English:* [Operator manual](../../en/manual/README.md).

## 1. Qué es y qué necesitás

Un estudio de emisión en vivo portable: escenas con cámaras y gráficos, audio
procesado, invitados remotos por enlace, grabación, y salida a YouTube, Twitch,
Facebook o cualquier servidor RTMP. Corre desde una carpeta (disco o pen drive),
sin instalar nada.

- **Windows** 10 (1809 o posterior) u 11, 64 bits. **Linux** de 64 bits reciente
  (el programa publicado se compila en Ubuntu 24.04) con escritorio y OpenGL.
- Placa de video con driver actualizado (codifica por hardware; si no puede, usa
  x264 por CPU).
- Subida de internet de al menos el **doble** del bitrate que vas a transmitir.
- Auriculares para monitorear.

## 2. Tu primer vivo en 10 minutos

La barra de la izquierda tiene las páginas: ⌂ Dashboard · ▦ Escenas · ◉ Entradas
· 🎚 Audio · ☎ Sala · ▶ YouTube · ◆ Twitch · f Facebook · ⚙ Configuración · ✦
Asistente.

1. **Entradas (◉):** agregá la cámara (USB, capturadora, cámara IP o NDI) y el
   micrófono. Cada entrada muestra una miniatura viva.
2. **Escenas (▦):** en la escena «Principal» agregá la cámara y un título.
   Arrastrá para mover y tirá de la esquina para cambiar el tamaño.
3. **Destino:** en YouTube, Twitch o Facebook seguí la guía de la pestaña para
   conectar la cuenta (unos 5 minutos, una sola vez). Para otro servicio, «Salir
   al aire» pide servidor RTMP y clave; la clave queda guardada cifrada y la
   próxima vez alcanza con dejarla vacía.
4. **Probá grabar** un minuto y miralo en `Recordings/` antes del primer vivo.
5. **Salí al aire.** Arriba de todas las páginas vas a ver «AL AIRE» con el
   tiempo, y «REC» si estás grabando.

## 3. Protecciones mientras estás al aire

- Cerrar la ventana, actualizar, borrar una escena o una entrada, cortar la
  transmisión o detener la grabación **piden confirmación**. Si la escena que
  vas a borrar está al aire, el aviso lo dice.
- Si la transmisión o la grabación **se cortan solas** (internet, servidor,
  disco lleno) queda un aviso rojo fijo en la barra de abajo hasta que lo cierres.
- Si el programa se cae, se reinicia solo y **retoma** la grabación y la
  transmisión que estaban en curso.
- Si arranca en **modo seguro** (tras varias caídas seguidas) tus escenas no se
  cargan pero no se pierden: cerrá y volvé a abrir. Queda una copia en
  `Config/app.json.antes-del-modo-seguro`.

## 4. Audio

El mezclador del Dashboard tiene el volumen y el mute de cada fuente. La página
Audio (🎚) arma, por fuente, una cadena de módulos: lo habitual es **gate**
(saca el ruido cuando nadie habla) → **compresor** (empareja el volumen) →
**limitador** (evita picos). El analizador mide el volumen en **LUFS**: para voz
en vivo apuntá a entre −16 y −14. Rojo en los medidores = está saturando: bajá.

## 5. Invitados (Sala ☎)

1. En el panel **Acceso** copiá el enlace (ya incluye la sala) o mostrá el QR, y
   pasale el **PIN** al invitado por otro medio. Hay un botón para generar un PIN
   nuevo (el anterior deja de servir).
2. El invitado entra a la **sala de espera**: lo ves, podés hablarle en privado
   y lo **admitís** o rechazás.
3. Ya adentro, sus fuentes «Invitado N» se agregan a la escena como cualquier
   otra. Puede compartir pantalla, mandar archivos, chatear y levantar la mano.
4. **Modo oradores:** fila de turnos con tiempo por persona y reloj visible para
   todos.
5. **Acceso desde afuera de tu red:** por túnel de Cloudflare o por servidor
   propio con WireGuard (asistente en la misma página). Dentro de la red local
   el enlace es por IP y el navegador del invitado avisa «La conexión no es
   privada»: es esperable; la [guía del invitado](guia-del-invitado.md) explica
   cómo seguir.

Los interruptores (sala de espera, sala cerrada, modo oradores) muestran siempre
el estado **real**: si otro lo cambia desde la consola web o el asistente, se
actualizan solos.

## 6. Transmitir y grabar

| Calidad | Bitrate de video sugerido | Subida mínima |
|---|---|---|
| 720p30 | 2500–4000 kbps | 8 Mbps |
| 1080p30 | 4500–6000 kbps | 12 Mbps |
| 1080p60 | 6000–9000 kbps | 18 Mbps |

El codificador en «auto» elige el hardware disponible y cae a x264 si falla. La
grabación es MKV a propósito: si se corta la luz, el archivo sirve igual.
Facebook solo permite transmitir a Páginas, no a perfiles.

## 7. Actualizar, respaldos y mudanza

- **Actualizar:** descomprimí la versión nueva en otra carpeta y elegila en
  Configuración → Sistema y datos. Antes se respaldan `Config/` y
  `Credenciales/` en `Respaldos/` (quedan 5). Se puede volver a la versión
  anterior. La versión instalada se ve en esa misma sección.
- **Mudar a otro disco:** copiá la carpeta entera. `Credenciales/` está cifrada
  pero la clave viaja en la misma carpeta: quien tenga el disco tiene acceso.

## 8. Asistente de IA (✦)

Un chat que ve y maneja la app (Claude Code, Gemini CLI u opencode, con tu
cuenta). Lo que escriben los invitados nunca se ejecuta solo: si el asistente
leyó contenido de terceros, cada acción te pide aprobación con un clic.

## 9. Solución de problemas

| Problema | Qué hacer |
|---|---|
| No abre, o ventana en negro | Hace falta escritorio con aceleración gráfica. Actualizá el driver de video. Mirá `Logs/app.log`. |
| No aparece la cámara | Cerrá otros programas que la usen; en Entradas volvé a agregarla. |
| Eco | El invitado tiene que usar auriculares. |
| El invitado no puede entrar | PIN correcto, sala abierta, y que acepte el aviso del navegador (guía del invitado). |
| Se pixela o se corta | Bajá el bitrate; usá cable en vez de wifi. |
| Ventana «Recuperación» (Windows) | Aparece recién tras varias caídas seguidas: «Reintentar». Si sigue, enviá `Logs/`. |

Para pedir ayuda enviá la carpeta `Logs/` (`app.log`, `launcher.log`, `ba.log`).
**Nunca** envíes `Credenciales/`.

## 10. Glosario

- **Bitrate:** cuánta información por segundo se envía; más = mejor imagen y más subida necesaria.
- **RTMP:** la forma estándar de enviar video en vivo a una plataforma (servidor + clave).
- **Clave de transmisión:** la contraseña del vivo; quien la tiene puede transmitir en tu canal.
- **Sala de espera (lobby):** donde queda el invitado hasta que lo admitís.
- **Gate / compresor / limitador / de-esser:** módulos de audio: silencia el ruido / empareja el volumen / frena picos / suaviza las «s».
- **LUFS:** medida del volumen percibido.
- **NDI / SRT / RTSP:** formas de traer video por la red.
- **Túnel (Cloudflare) / WireGuard:** maneras de que un invitado entre desde fuera de tu red.
- **Modo seguro:** arranque mínimo tras varias caídas, sin cargar las escenas.
