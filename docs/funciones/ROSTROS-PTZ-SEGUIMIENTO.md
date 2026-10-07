# Rostros, PTZ y Seguimiento

Tres tarjetas del rack de una entrada de video. Cada una es un elemento de GStreamer propio
(`src/gst-plugins/rostros`, `src/gst-plugins/filtros/ptz.cpp`). Entre ellas se comunican solo con
mecanismos estándar de GStreamer: eventos propios, metas en los buffers y mensajes de elemento al bus.
Por eso **van en cualquier orden** y la vista Video › Racks muestra, debajo de cada tarjeta, los eventos
que pasan por ella (↓ hacia abajo, ↑ hacia arriba).

La misma información la dan:

- la tarjeta, en la sección «Parámetros y eventos» de su panel;
- el MCP, con `video_tarjeta_ayuda {"tipo": "rostros"}` (o `seguir`, `ptz`, …; sin tipo, todas).

Las dos salen de `GrafoVideo::parametrosDe`, `eventosDe` y `ayudaTextoOverlay`
(`src/app/video/GrafoVideo.cpp`): para cambiar la documentación se cambia ahí y nada más.

| Tarjeta | Elemento | Toca la imagen | Cuántas |
|---|---|---|---|
| Rostros | `barostros` (`barostrosgl`, `barostrosd3d12`) | solo si `marcar` | las que quieras (dos en la misma entrada se funden en un elemento) |
| PTZ | `baptz` (`baptzgl`, `baptzd3d12`) | sí (recorta y agranda); en reposo pasa sin copiar | una por entrada, se crea sola, no se quita (se apaga con `activo`) |
| Seguimiento | `baseguir` | no | una por entrada |

## Rostros (`rostros`)

**Análisis** (en una rama aparte, con cola que descarta: nunca demora la imagen):

| Parámetro | Valores | Qué hace |
|---|---|---|
| `activo` | sí/no | analizar; si no, la imagen solo pasa |
| `fps` | 0,5–30 (5) | análisis por segundo |
| `reconocer` | sí/no | sacar el vector de cada cara para Personas (lo prende Personas sola) |
| `detector` | `auto` · `vision` · `yunet` | `auto`: Apple Vision en la Mac si no hace falta reconocer, si no YuNet |
| `confianza` | 0,1–1 (0,7) | descarta detecciones dudosas |

**Overlay**:

| Parámetro | Valores | Qué hace |
|---|---|---|
| `marcar` | sí/no | dibujar sobre cada cara |
| `estilo` | `recuadro` · `esquinas` · `relleno` · `tapar` · `pixelar` | `tapar` y `pixelar` sirven para no mostrar una cara |
| `color` | ARGB (`0xFFFFC83C`) | color de la marca y del texto |
| `grosor` | 1–40 px (4) | borde |
| `texto` | plantilla | ver abajo |
| `texto_posicion` | `arriba` · `abajo` | si arriba no entra, va abajo |
| `texto_tamano` | 0,01–0,2 (0,035) | alto del texto, en fracción del alto del video |

### El texto del overlay

`texto` es una plantilla: se escribe lo que se quiera y estas marcas se reemplazan por lo de cada cara.

| Marca | Qué pone |
|---|---|
| `{nombre}` | quién es (cuando Personas la reconoce; vacío si no) |
| `{cargo}` | su cargo, de su ficha en Personas |
| `{id}` | el número de la cara (se conserva mientras se mueve) |
| `{confianza}` | qué tan segura está la detección («91 %») |
| `{n}` | cuántas caras hay en el cuadro |

Lo que queda vacío no deja separadores colgando: «{nombre} — {cargo}» sin cargo muestra solo el
nombre. Si no queda nada, no se dibuja cartel. Ejemplos:

- `{nombre}`
- `{nombre} — {cargo}`
- `Cara {id}`
- `{confianza}`
- `{nombre} #{id}`

Si `texto` está vacío, no hay texto.

El nombre y el cargo los manda Personas por el canal de control (`ba/control`, campo
`rostros_etiquetas`: `[{id, nombre, cargo, persona}]`). Desde afuera también se pueden fijar con la
propiedad `etiquetas` del elemento.

Con `gst-launch-1.0`:

```
gst-launch-1.0 -m avfvideosrc ! videoconvert ! barostros marcar=true estilo=esquinas \
    texto="Cara {id} ({confianza})" ! videoconvert ! autovideosink
```

## PTZ (`ptz`)

Pan, tilt y zoom digital. Lo manejan Cámaras PTZ, el joystick y los presets. Parámetro de la tarjeta:
`activo`. Lo que el usuario mueve va por el canal de control, no por parámetros.

## Seguimiento (`seguir`)

| Parámetro | Valores | Qué hace |
|---|---|---|
| `activo` | sí/no | seguir |
| `objetivo` | `""` · `id:N` · `persona:X` · `x,y` · `mayor` · `centro` | a quién: nadie, la cara N, una persona de Personas (id o nombre), la más cercana a un punto de la toma (0..1), la más grande, la más centrada |
| `zoom` | 0–8 (0) | zoom que busca; 0 = no lo toca |
| `tamano_cara` | 0–1 (0) | qué parte del alto ocupa la cara (calcula el zoom) |
| `aire` | −0,5–0,5 (0,12) | cuánto queda la cara arriba del centro |
| `suavidad` | s (0,6) | constante de tiempo del movimiento |
| `zona_muerta` | 0–0,5 (0,04) | cuánto se puede correr la cara sin mover nada |
| `perdida_s` | s (3) | cuánto la busca donde estaba antes de soltarla |

En la GUI se elige a quién seguir haciendo clic en la vista previa, o doble clic en una cara en Cámaras PTZ.
Por MCP se usa `ptz_seguir` (con `cara` o `regla`). Con una cámara motorizada, la app la mueve con pulsos a
partir del mensaje `ba-seguir`.

## Eventos y mensajes

| Nombre | Clase | Dirección | Lo manda | Campos |
|---|---|---|---|---|
| `ba/rostros` | evento propio serializado | ↓ y ↑ | Rostros | `caras` (`x,y,w,h;…`), `json`, `cantidad`, `pts`, `ms`, `detector` |
| `ba/ptz` | evento propio serializado | ↓ | PTZ, cuando cambia | `x`, `y`, `zoom`, `moviendo` |
| `ba/encuadre` | evento propio serializado | ↓ y ↑ | Seguimiento | `x`, `y`, `zoom`, `suavidad`, `id`; o `soltar=true` |
| `ba/control` | evento propio fuera de banda | ↓ | la app (la entrada) | `rostros_etiquetas`, `seguir_objetivo`, … |
| `face` | `GstVideoRegionOfInterestMeta` | en cada buffer | Rostros | parámetro `ba-rostro`: `id`, `confianza`, `nombre`, `cargo`, `edad-ms` |
| `ba-rostros` | mensaje de elemento | bus | Rostros | `json`, `cuadro` (`GstSample` analizado) |
| `ba-seguir` | mensaje de elemento | bus | Seguimiento | `json` (estado, cara, encuadre) |

El `json` de las caras tiene, por cada una:

- `id`;
- `x`, `y`, `ancho`, `alto` (0..1 del cuadro);
- `confianza` y `puntos`;
- `nombre`, `cargo` y `persona`;
- `toma` (lo mismo, pero en la toma completa, deshaciendo la ventana del PTZ).

Arriba de todo están `ventana`, `analisis`, `video`, `detector`, `ms` y `pts`.

**Orden.** El PTZ sigue el `ba/encuadre` que le llegue de abajo o de arriba durante 1,5 s; después suelta.
Rostros usa el `ba/ptz` que vio pasar para dar cada cara también en la toma. Seguimiento escucha
`ba/rostros` venga de donde venga. Así andan tanto rostros → ptz → seguir como seguir → ptz → rostros.

**Todos los elementos dejan pasar todos los eventos.** `src/gst-plugins/tests/test_eventos.cpp` lo
verifica en cada elemento: serializado, fuera de banda y hacia arriba. Un plugin nuevo tiene que entrar en
esa lista.
