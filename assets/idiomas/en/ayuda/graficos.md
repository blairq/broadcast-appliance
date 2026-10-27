A graphic is a folder in `Graficos/<nombre>/` with its `grafico.json` (parameters, size, actions) and either a declarative description (layers from `grafico_repertorio`) or a `grafico.qml`. It's drawn in another process (`ba-grafico`) with its own clock: the frame at 380 ms is always the same, in the preview, in `grafico_probar` and on air.

## The path
1. `grafico_listar`: almost always an existing one with different parameters is enough.
2. A new one: declarative whenever possible (`grafico_repertorio`); QML only if the repertoire can't do it (the pitfalls are in docs/funciones/GRAFICOS.md).
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}` and LOOK at the images.
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}` (or `"armar": true` for the preview).

## Live video inside (the camera in the lower third)
A graphic with a parameter of type `fuente` (e.g. “Lower third with camera”) carries a video source that comes in and goes out with it: `grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Camera 1"}`. It's declared with `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` in the graphic's pixels (docs/funciones/GRAFICOS.md, “Video en vivo adentro”).

## GIFs and videos with alpha inside a graphic
- `grafico_medio_agregar {"nombre", "archivo"}`: a GIF, PNG, JPG or a folder of PNGs is copied; a VIDEO (WebM VP9 with alpha, ProRes 4444, MP4…) is converted once into frames with its transparency (`ancho` to shrink it, `maxSegundos`). Check `alfa` in the reply: false = it will cover what's behind it.
- In the description: `{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`. `inicio` is the graphic's ms at which it starts (before that it isn't shown); without `bucle` it stays on the last frame. In QML: `import BA.Grafico` and `Medio { source: "medios/x"; inicio: 300 }`.
- Its time is the graphic's time: dragging the editor's bar backwards shows the matching frame.
- `grafico_medios {"nombre"}` lists what it has; `grafico_medio_quitar` removes one.

## Pie and bar charts with data (a live vote)
- `torta` layers (with `grosor` > 0 it's a ring) and `barras` (`orientacion` vertical or horizontal). `"datos": "{votos}"` takes the parameter; declare it in grafico.json with `"tipo": "datos"`.
- The data is written as a list `[{"etiqueta": "Yes", "valor": 34, "color": "#2e7d32"}]`, as an object `{"Yes": 34, "No": 12}` or as text (what the operator types in Lower thirds): `Yes: 34; No: 12 #c62828; Abstain: 3`.
- They come in animated (the pie spins, the bars grow one by one and the number counts up). `mostrar`: percentage, value, both or nothing.
- **Change on air without coming in again**: `grafico_actualizar {"fuente", "parametros": {"votos": …}}` (only what changes). The slices and bars move from the previous values to the new ones (`cambio`, 700 ms). The same happens if the operator presses “On air” in Lower thirds with the same graphic up. A link with `vivo: true` animates it too.
- A hand-written QML: `Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`, with `property var anteriores` and `property bool sinEntrada` on the root (the system fills them when it changes) and `"cambio": {"duracion": 700}` in the definition.

## Graphics from a plugin (programming one on demand)
When the repertoire isn't enough or the graphic needs logic (calculations, data from an API, how many things to show), write a Python plugin with `@plugin.grafico` (`help {"topic": "plugins"}`; example `ejemplo-grafico`: `plugin_ver {"id": "ejemplo-grafico"}`):
```python
@plugin.grafico("marcador", titulo="Scoreboard", ancho=1000, alto=420, duracion_ms=1500,
                parametros=[{"clave": "puntos", "titulo": "Points", "tipo": "datos", "porDefecto": "Yes: 3; No: 1"}])
def marcador(valores):                    # DESCRIBES: the renderer draws it (with its animations)
    return {"declarativo": {"ancho": 1000, "alto": 420, "capas": [
        {"tipo": "barras", "datos": "{puntos}", "izquierda": 40, "arriba": 40, "ancho": 920, "alto": 340}]}}

@plugin.grafico("ondas", ancho=600, alto=160, duracion_ms=3000, bucle=True)
def ondas(lienzo, ms, valores):           # DRAWS every frame: numpy (height, width, 4) RGBA, transparent
    lienzo[100:, :50] = (255, 106, 43, 235)
```
- Describing is almost always the better choice (it looks like the built-in ones and value changes are animated). Drawing, for what the repertoire doesn't have; frames are cached and not redrawn with the same values.
- `plugin_escribir` → `plugin_probar` (the report includes `graficos`: what each one returned and whether it drew) → `plugin_activar`. Once active, the graphic appears in `grafico_listar` as `plugin-<id>-<nombre>` (category “Plugins”) and is used like any other: `grafico_probar`, `grafico_mostrar`, Lower thirds, the rundown.
- With the plugin off, the graphic stays in the list, but using it says the plugin isn't running.
