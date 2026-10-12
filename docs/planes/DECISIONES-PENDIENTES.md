# Decisiones pendientes (para el usuario)

Cosas del plan (docs/planes/TAREAS-INFORME-EXPERTO.md) donde hay que elegir un camino antes de seguir.
Cada una dice qué opciones hay, qué cuesta cada una y qué recomiendo.

## T12 · Limitador de seguridad en el máster

libobs no tiene un filtro de máster: la mezcla de programa (pista 1) se suma y va directo a los
codificadores. El loudness (T11) ya se mide sobre esa mezcla, pero para LIMITARLA hay que meterse en el
camino. Opciones:

| Opción | Cómo | Costo | Latencia |
|---|---|---|---|
| A. Parche a libobs | `audio_output_set_mix_filter` en `media-io/audio-io.c`, antes del recorte a ±1 y de los codificadores | Mac y Linux compilan libobs desde fuente (fácil). **Windows usa el `obs.dll` precompilado de OBS**: habría que compilar libobs para Windows (MinGW o MSVC en Docker) | 0 ms (lookahead del limitador: 1,5 ms) |
| B. Bus máster por la matriz | Todas las fuentes de programa a una «entrada virtual» Máster con el limitador, y esa a la pista 1 | Sin tocar libobs; cambia el ruteo de todo el programa | +20-30 ms al audio del programa (hay que compensarlo en el video) |
| C. Limitador por fuente | Un filtro limitador en cada fuente que va al programa | Fácil | 0 ms, pero la SUMA puede pasarse igual: no es un máster |

**Recomendación:** A. Es la única que limita de verdad lo que sale sin agregar latencia. Hoy el
loudness avisa (disparador de fábrica «Loudness fuera de objetivo» y el true peak en rojo en el
Mezclador), así que no hay apuro de aire.

## T32 · Automixer de las bancas (y, de fondo, dónde vive el audio)

**Decidido (2026-10-05): A, y todo lo demás también va pasando a GStreamer (libobs se va a sacar).** Hecho: micrófonos por GStreamer y `baautomixer` (ver T32 en TAREAS-INFORME-EXPERTO.md). Lo que queda en libobs: docs/desarrollo/MIGRACION-GSTREAMER.md.

El automixer (Dugan, `src/core/dsp/Automixer`) está escrito y probado, pero para usarlo hay que meterlo en
el camino del audio de cada micrófono. La regla es que el procesamiento vaya en GStreamer (capa de
entrada), no en OBS. Hoy los micrófonos son fuentes **nativas de OBS** (`coreaudio_input_capture`,
`wasapi_input_capture`, `pulse_input_capture`) y los racks de audio (LSP/CLAP) también son filtros de
OBS. Opciones:

| Opción | Cómo | Costo |
|---|---|---|
| A. Micrófonos por GStreamer | Entrada de audio nueva en `ba_gst_fuente` (osxaudiosrc / wasapi2src / pulsesrc) y un elemento `baautomixer grupo=…` en `filtros_audio`, con un coordinador compartido por grupo dentro del proceso | Es el camino de la regla: todo el audio en GStreamer, el día que se va libobs no se pierde nada. Hay que migrar los micrófonos existentes y medir la latencia de cada sistema |
| B. Filtro de OBS | Un filtro por fuente que reporta su nivel a un `AutomixerGrupo` y aplica la ganancia | Rápido, pero va contra la regla y se pierde si se saca libobs |
| C. Sin automixer | El operador mezcla; «Seguir al micrófono» (T34, ya hecho) usa los niveles sin tocar el audio | Nada |

**Recomendación:** A, como parte de una tarea más grande: «el audio de entrada en GStreamer» (micrófonos
y racks). Mientras tanto, T34 ya funciona sobre los niveles.

## T47 · Passphrase para las credenciales

**Decidido (2026-10-05): C, sin passphrase.** El aparato tiene que volver solo después de un corte de luz, «todo como estaba». Hecho: retoma la transmisión y la grabación si la corrida anterior no se cerró; «Arrancar solo al encender el equipo» (opcional, GUI y MCP `inicio_con_el_equipo`); los dispositivos que faltan se esperan y se conectan solos al aparecer. Proteger el equipo con el disco cifrado del sistema (ojo: en Mac, FileVault impide el inicio de sesión automático).

Hoy `Credenciales/` se cifra con una clave del dispositivo: alguien con la carpeta y la máquina puede
leerla. `SecretsStore` (Argon2id) ya existe para envolver esa clave con una passphrase.

| Opción | Qué cambia | Costo |
|---|---|---|
| A. Passphrase al arrancar | El launcher (o la app) la pide antes de abrir `Credenciales/` | Nadie puede arrancar la transmisión desatendida (p. ej. tras un corte de luz, con la app en inicio automático) |
| B. Llavero del sistema | Keychain (Mac) / DPAPI (Windows) / Secret Service (Linux) guarda la clave | Rompe la portabilidad: la carpeta deja de andar en otra máquina sin volver a cargar todo |
| C. Como está | Clave del dispositivo | Proteger el disco (FileVault/BitLocker) y la cuenta del sistema |

**Recomendación:** C con disco cifrado para el equipo de la sala, y A como opción para quien la
necesite (apagada de fábrica). La decisión depende de si el aparato tiene que volver solo al aire
después de un corte.
