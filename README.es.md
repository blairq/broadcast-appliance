<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/icons/logo.png">
    <img alt="Broadcast Appliance" src="assets/icons/logo-claro.png" width="480">
  </picture>
</p>

[English](README.md) · **Español**

# Broadcast Appliance

Un estudio de transmisión en vivo todo en uno —switcher, mezclador de audio,
videoconferencia, asambleas, gráficos, grabación y transmisión— que se maneja
**hablándole a un asistente de IA**. Portable: todo lo que necesita viaja
adentro de la carpeta, en Windows, macOS y Linux.

[![CI](https://github.com/blairq/broadcast-appliance/actions/workflows/ci.yml/badge.svg)](https://github.com/blairq/broadcast-appliance/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Licencia](https://img.shields.io/badge/licencia-GPL--3.0-blue.svg)](#licencia)

---

## Inicio rápido

1. **Bajá** el último release para tu sistema desde
   **[Releases](https://github.com/blairq/broadcast-appliance/releases/latest)**:
   `.zip` para Windows, `.tar.gz` para Linux, `.dmg` para macOS.
2. **Descomprimilo y abrí `BroadcastAppliance`.** No se instala nada en el
   sistema: el programa, sus datos y sus modelos viven en esa carpeta. En macOS
   la app no está notarizada: la primera vez, clic derecho → **Abrir**.
3. **Configurá el asistente.** En la página **Asistente** (✦ en el menú
   lateral) elegí el agente y el modelo, y arrancalo. Después, en **Agentes**,
   prendé los roles automáticos que quieras.
4. **Preguntale todo.** «Armame un evento para mañana a las 19 con dos cámaras
   y un invitado remoto», «transmitilo a YouTube», «¿por qué el invitado 3 se
   escucha bajo?». El asistente de eventos arma el evento, lo configura, lo
   corre con vos y te explica lo que le preguntes.

<p align="center">
  <img alt="La página Asistente: elegí el asistente de eventos y su modelo, y arrancalo" src="docs/img/quick-start-assistant.jpg" width="820">
</p>

> **No viene un manual de usuario, y es a propósito.** Todo se hace desde el
> asistente de eventos: conoce cada parte del programa (más de 700 herramientas)
> y ve lo que está en pantalla y al aire. Si no sabés cómo hacer algo, preguntale.
>
> **Si querés uno, el asistente lo arma.** En la barra de la izquierda, grupo
> *Sistema*, la página **Manuales** (arriba de *Logs del sistema*): tocá
> **Generar los manuales con el asistente** y saca una captura de cada pantalla y
> escribe los manuales en Word y PDF, en el idioma del programa. Quedan en
> `BroadcastAppliance Datos/Manuales/` y se abren desde esa misma página. Conviene
> un modelo igual o mejor que DeepSeek 4.1 Flash.

---

## Qué hace

- **Eventos, de punta a punta.** Cada evento tiene sus escenas, su mezclador,
  sus medios, su asamblea y su página web. Eventos nuevos desde cero, desde una
  copia o desde una plantilla; un histórico de todo lo que se grabó y transmitió.
- **Video.** Escenas y transiciones sobre un motor propio de GStreamer, cámaras
  (USB, de red, NDI, PTZ con seguimiento de caras), gráficos animados,
  teleprompter, códigos QR, multiview, superresolución con IA y unión de
  cámaras en panorámica. Grabación y transmisión a YouTube, Twitch, Facebook o
  cualquier servidor RTMP/SRT.
- **Audio.** Un mezclador con grupos que se compactan y **un VCA por grupo**, un
  rack de procesadores LSP por fuente, matriz de mezcla, loudness EBU R128,
  calibración de sala, automezclador, PFL, AFV y sincronía con los labios.
- **Conferencia.** Los invitados entran por WebRTC con un enlace: lobby
  privado, chat, modo oradores con reloj, hasta 64 invitados.
- **Asamblea.** Integrantes presenciales y remotos, recinto con sus bancas,
  quórum, orden del día con informes en PDF, debates y votación.
- **Bosch DCN Next Generation (beta).** Maneja la central de conferencia por su
  Open Interface (serie o TCP): micrófonos de banca, lista de oradores,
  asistencia y votación desde la central, también desde la botonera. Está
  probado contra un simulador de la central, todavía no con una de verdad.
- **Páginas web para todos.** Cada evento tiene su página pública (programa,
  estado en vivo, documentos), y el aparato sirve las páginas de cada puesto:
  invitados desde el navegador, el público (preguntas y votaciones), el
  teleprompter en una tablet o un celular, los rótulos en vivo, la corrección de
  subtítulos, la sesión remota de los integrantes, la consola de la presidencia
  y un tablero. Cada enlace, con su QR, en la página *Servicios*.
- **Desde internet.** Un túnel incorporado (Pinggy) le da al evento una
  dirección pública para que entren invitados, público e integrantes de afuera,
  con el video de los remotos por su propio relé (TURN), sin tocar el router.
- **Superficies de control.** Una botonera en una tablet o un celular
  (emparejada por QR), **Stream Deck**, controladores **MIDI** (X-Touch Mini,
  nanoKONTROL, Launchpad), superficies **OSC** (TouchOSC, Open Stage Control) y
  los aparatos de **Bitfocus Companion Satellite** (Stream Deck, Loupedeck,
  X-keys…). Cada puesto con sus permisos y con el estado en vivo en cada botón:
  la escena al aire, si se graba, quién habla.
- **Pantallas.** Los monitores de la máquina y los televisores de la red
  (Chromecast), con lo que va en cada uno.
- **Personas.** Reconocimiento de caras, padrón con acreditación y **tótems de
  registro**: una TV o un celular con cámara donde la gente se registra
  hablando con el asistente, muestra su documento (el código de barras, o el
  dorso por OCR) y se saca la foto.
- **Voz.** Dictado, subtítulos y traducción en vivo (Parakeet o Nemotron,
  locales), voces sintéticas y el asistente contestando en voz alta.
- **Plugins: lo que no trae, se lo pedís.** Lo que el programa no incluye lo
  escribe el asistente como un plugin de Python: un equipo que se maneja de una
  forma rara (el PTZ de una cámara USB, una cámara 360), una tarjeta de video o
  de audio, una superficie de control nueva (unas X-keys, por ejemplo) o una
  automatización. Con su Python portátil y sus bibliotecas adentro de la carpeta
  de datos: sobreviven a las actualizaciones y no instalan nada en el sistema.
- **IA local.** Modelos de lenguaje y de visión locales (llama.cpp / MLX) para
  que lo esencial ande sin internet; asistentes en línea (Claude, Gemini,
  opencode, pi) cuando los quieras.
- **Confiable.** Un supervisor que reinicia lo que falla sin cortar la
  transmisión, y después de un corte de luz todo vuelve como estaba.

---

## Documentación

| | |
|---|---|
| [Compilar, probar y empaquetar](docs/es/compilar.md) | Para quien desarrolla: requisitos, compilación, tests, CI y el paquete portable. |
| [Arquitectura](docs/es/arquitectura.md) | Procesos, núcleo, estado de cada parte, estructura del repositorio, dependencias. |
| [Configuración de referencia](docs/es/configuracion.md) | Lo que se puede ajustar a mano (normalmente lo hace el asistente). |
| [Cómo funciona cada parte](docs/funciones/) | Un archivo por función. |
| [Manual del operador](docs/es/manual/README.md) | Para quien lo usa: primer vivo, invitados, audio, problemas. |
| [Guía del invitado](docs/es/manual/guia-del-invitado.md) | Para quien entra a la conferencia. |
| [`docs/`](docs/README.md) | Todo lo demás: planes, modelos locales, historia. |

---

## Compilar desde el código

```bash
git clone https://github.com/blairq/broadcast-appliance.git
cd broadcast-appliance
make deps      # las dependencias de terceros para esta máquina, fijadas (se bajan; nada en el sistema)
make compilar  # compila
make probar    # los tests
```

Los builds por plataforma, el paquete portable y los detalles están en
[Compilar, probar y empaquetar](docs/es/compilar.md).

---

## Licencia

El código propio del proyecto se distribuye bajo **GNU GPL-3.0** (ver
[`LICENSE`](LICENSE)); el titular es **Jorge Ortega**. Las dependencias de
terceros conservan sus licencias (mbedTLS Apache-2.0, nlohmann/json MIT,
libsamplerate MIT, Argon2 CC0/Apache-2.0, CLAP MIT, LSP LGPL, entre otras; ver
[Arquitectura → Dependencias](docs/es/arquitectura.md)).

## Contribuir

Antes de mandar cambios: `make build` tiene que compilar sin errores,
`make test` tiene que pasar y también los chequeos del repositorio
(`pytest -q` con `requirements-dev.txt`). Los detalles, en
[Compilar, probar y empaquetar](docs/es/compilar.md).
