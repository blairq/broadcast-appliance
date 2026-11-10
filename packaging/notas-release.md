*Español más abajo.*

## What's new in 2.1
- **Many more people in the Conference.** The backend was reworked from the ground up: it went from 10–15 external connections to about **60 on a MacBook** and **up to 150 on a mid-range PC**. Network and work threads spread across the cores, the TURN relay and the tunnel hand packets over in memory, the return video is encoded once per stream (like an SFU) and the return audio once per mix, H.264 is decoded and encoded on the graphics card on Mac and Windows, and nobody's video is received or decoded unless someone is watching it. Up to 512 guests.
- **A new web dashboard:** the app's own dashboard in the browser of a tablet, laptop or phone — the same cards, preview, program and TAKE, the outputs, and a layout of its own per device. The **whole Mixer** opens in a browser tab (live faders and meters, PFL/AFL, VCA groups, matrix and room calibration). The previous HTML web control is deprecated.
- **Multiple outputs:** several outputs, each one a complete operator with shared scenes — its own program, matrix, loudness, sync and alarms — and destinations per output, with several YouTube, Twitch and Facebook accounts.
- **Event timeline:** automatic cues and pauses run by themselves, with a 5-second warning and HOLD; after a cut it comes back on HOLD and the assistant adapts.
- **Atomic TAKE:** the incoming scene is built off air and goes out whole once every layer has a picture; the audio follows the picture.
- **People grid:** what happens when someone speaks (highlight, feature, overlay or the speaker's scene), every cell at the time of its content, and high definition for the ones that matter.
- **Room test and Capacity:** measure how many guests this machine (or another one) can take, with simulated guests; a Tests page of its own.
- **Screen layouts** saved with each event, and **events** in their own tab; one assistant conversation per event.
- **Graphics:** a whole scene can go inside a lower third, not just a camera.
- **Languages:** French, Russian, Portuguese and Chinese, complete.
- **Fixes:** video files show up again in the program, a file inside the data folder is found again after restarting, the assistant's terminal no longer crashes when resized, and the interface no longer stalls with 200 guests. Qt 6.8 LTS.

## What to download
- **Windows 10 (1809 or later) / 11, 64-bit:** `BroadcastAppliance-*-windows-x64.zip`
- **64-bit Linux** (desktop with OpenGL; built on Ubuntu 24.04, so it needs that distribution or a newer one): `BroadcastAppliance-*-linux-x64.tar.gz`
- **macOS 13.3 or newer, Mac with Apple Silicon (M1 and later):** `BroadcastAppliance-*-macos-arm64.dmg`
- The `.sha256` files let you verify the download (optional).

## How to use it
1. Unpack the whole file into a folder (a disk or USB stick works). Nothing gets installed.
2. Open `BroadcastAppliance` (`.exe` on Windows; `./BroadcastAppliance` on Linux). On Windows, if "Windows protected your PC" shows up: *More info → Run anyway*.
   On macOS: open the `.dmg`, drag `BroadcastAppliance` to Applications (or to your disk) and the **first time** open it with *right-click → Open → Open* (it isn't signed with an Apple certificate). Inside the `.dmg` there's a "Read me first".
3. Read `README.txt` (in Spanish, `LEEME.txt`), which comes inside the folder: first steps, where everything goes and what to do if something goes wrong.

## If you already had it
Unpack this version into a **different** folder and pick it in *Settings → System and data → Update from a folder*. Your settings, credentials and recordings are left untouched, and you can go back to the previous version.

Operator manual and guest guide: the [`docs/en/manual`](https://github.com/blairq/broadcast-appliance/tree/master/docs/en/manual) folder.

---

## Novedades de la 2.1
- **Mucha más gente en la Conferencia.** Se rehízo el backend de punta a punta: pasó de 10-15 conexiones externas a unas **60 en una MacBook** y **hasta 150 en una PC mediana**. Los hilos de red y de trabajo se reparten entre los núcleos, el TURN y el túnel se pasan los paquetes en memoria, el video de retorno se codifica una vez por flujo (como un SFU) y el audio de retorno una vez por mezcla, el H.264 se decodifica y codifica en la placa en Mac y Windows, y no se recibe ni se decodifica el video de nadie que no se esté mirando. Hasta 512 invitados.
- **Tablero web nuevo:** el mismo dashboard de la app en el navegador de una tablet, una notebook o un teléfono: las mismas tarjetas, preview, programa y TAKE, las salidas y una disposición propia por aparato. El **Mezclador entero** se abre en una pestaña del navegador (faders y vúmetros en vivo, PFL/AFL, grupos VCA, la matriz y la calibración de sala). El control web en HTML de antes queda en desuso.
- **Varias salidas:** cada salida es un operador completo con las escenas compartidas, con su programa, su matriz, loudness, sincronía y alarmas, y sus destinos, con varias cuentas de YouTube, Twitch y Facebook.
- **Línea de tiempo del evento:** los cues automáticos y las pausas pasan solos, con aviso de 5 s y HOLD; después de un corte vuelve en HOLD y el asistente se adecua.
- **TAKE atómico:** la escena que entra se arma fuera del aire y sale entera cuando todas sus capas tienen imagen; el audio sigue a la imagen.
- **Grilla de personas:** qué pasa cuando alguien habla (resaltar, destacar, superponer o la escena del orador), cada celda en la hora de su contenido y alta definición para los que importan.
- **Prueba de sala y Capacidad:** medir cuántos invitados aguanta esta máquina (u otra) con invitados simulados; página Pruebas propia.
- **Distribuciones de pantallas** guardadas en cada evento, y los **eventos** en su propia pestaña; una conversación del asistente por evento.
- **Gráficos:** adentro de un zócalo puede ir una escena entera, no solo una cámara.
- **Idiomas:** francés, ruso, portugués y chino, completos.
- **Arreglos:** los videos de archivo vuelven a verse en el programa, un archivo de la carpeta de datos se vuelve a encontrar después de reiniciar, la terminal del asistente ya no se cae al cambiar de tamaño y la interfaz ya no se traba con 200 invitados. Qt 6.8 LTS.

## Qué bajar
- **Windows 10 (1809 o posterior) / 11, 64 bits:** `BroadcastAppliance-*-windows-x64.zip`
- **Linux 64 bits** (escritorio con OpenGL; compilado en Ubuntu 24.04, necesita una distribución igual o más nueva): `BroadcastAppliance-*-linux-x64.tar.gz`
- **macOS 13.3 o más nuevo, Mac con Apple Silicon (M1 en adelante):** `BroadcastAppliance-*-macos-arm64.dmg`
- Los `.sha256` sirven para verificar la descarga (opcional).

## Cómo usarlo
1. Descomprimí el archivo completo en una carpeta (sirve un disco o pen drive). No se instala nada.
2. Abrí `BroadcastAppliance` (`.exe` en Windows; `./BroadcastAppliance` en Linux). En Windows, si aparece "Windows protegió su PC": *Más información → Ejecutar de todas formas*.
   En macOS: abrí el `.dmg`, arrastrá `BroadcastAppliance` a Aplicaciones (o a tu disco) y la **primera vez** abrilo con *clic derecho → Abrir → Abrir* (no está firmado con un certificado de Apple). Adentro del `.dmg` hay un "Leeme primero".
3. Leé `LEEME.txt` (en inglés, `README.txt`), que viene dentro de la carpeta: primeros pasos, dónde queda cada cosa y qué hacer si algo falla.

## Si ya lo tenías
Descomprimí esta versión en **otra** carpeta y elegila en *Configuración → Sistema y datos → Actualizar desde una carpeta*. Tu configuración, credenciales y grabaciones no se tocan, y se puede volver a la versión anterior.

Manual del operador y guía del invitado: carpeta [`docs/es/manual`](https://github.com/blairq/broadcast-appliance/tree/master/docs/es/manual).

---
