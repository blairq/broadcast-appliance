Un gráfico es una carpeta en `Graficos/<nombre>/` con su `grafico.json` (parámetros, tamaño, acciones) y, o una descripción declarativa (capas de `grafico_repertorio`), o un `grafico.qml`. Se dibuja en otro proceso (`ba-grafico`) con un reloj propio: el cuadro de los 380 ms es siempre el mismo, en la vista previa, en `grafico_probar` y al aire.

## El camino
1. `grafico_listar`: casi siempre alcanza con uno que ya existe y otros parámetros.
2. Uno nuevo: declarativo siempre que se pueda (`grafico_repertorio`); QML solo si el repertorio no da (las trampas están en docs/funciones/GRAFICOS.md).
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}` y MIRÁ las imágenes.
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}` (o `"armar": true` para el preview).

## Video en vivo adentro (la cámara en el zócalo)
Un gráfico con un parámetro de tipo `fuente` (p. ej. «Zócalo con cámara») lleva una fuente de video que entra y se va con él: `grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Cámara 1"}`. Se declara con `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` en píxeles del gráfico (docs/funciones/GRAFICOS.md, «Video en vivo adentro»).

## GIF y videos con alfa adentro de un gráfico
- `grafico_medio_agregar {"nombre", "archivo"}`: un GIF, PNG, JPG o una carpeta de PNG se copia; un VIDEO (WebM VP9 con alfa, ProRes 4444, MP4…) se pasa una vez a cuadros con su transparencia (`ancho` para achicarlo, `maxSegundos`). Mirá `alfa` en la respuesta: false = va a tapar lo de atrás.
- En la descripción: `{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`. `inicio` es el ms del gráfico en que arranca (antes no se ve); sin `bucle` se queda en el último cuadro. En QML: `import BA.Grafico` y `Medio { source: "medios/x"; inicio: 300 }`.
- Su tiempo es el del gráfico: arrastrar la barra del editor para atrás muestra el cuadro que corresponde.
- `grafico_medios {"nombre"}` lista lo que tiene; `grafico_medio_quitar` saca uno.

## Tortas y barras con datos (una votación en vivo)
- Capas `torta` (con `grosor` > 0 es un anillo) y `barras` (`orientacion` vertical u horizontal). `"datos": "{votos}"` toma el parámetro; declaralo en grafico.json con `"tipo": "datos"`.
- Los datos se escriben como lista `[{"etiqueta": "Sí", "valor": 34, "color": "#2e7d32"}]`, como objeto `{"Sí": 34, "No": 12}` o como texto (lo que escribe el operador en Rótulos): `Sí: 34; No: 12 #c62828; Abstenciones: 3`.
- Entran animadas (la torta gira, las barras crecen de a una y el número cuenta). `mostrar`: porcentaje, valor, ambos o nada.
- **Cambiar al aire sin volver a entrar**: `grafico_actualizar {"fuente", "parametros": {"votos": …}}` (solo lo que cambia). Las porciones y las barras se mueven de los valores de antes a los nuevos (`cambio`, 700 ms). Lo mismo pasa si el operador aprieta «Al aire» en Rótulos con el mismo gráfico puesto. Un vínculo con `vivo: true` también lo anima.
- Un QML escrito a mano: `Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`, con `property var anteriores` y `property bool sinEntrada` en la raíz (el sistema las llena al cambiar) y `"cambio": {"duracion": 700}` en la ficha.

## Gráficos de un plugin (programar uno a pedido)
Cuando el repertorio no alcanza o el gráfico necesita lógica (cuentas, datos de una API, cuántas cosas mostrar), escribí un plugin de Python con `@plugin.grafico` (`help {"topic": "plugins"}`; ejemplo `ejemplo-grafico`: `plugin_ver {"id": "ejemplo-grafico"}`):
```python
@plugin.grafico("marcador", titulo="Marcador", ancho=1000, alto=420, duracion_ms=1500,
                parametros=[{"clave": "puntos", "titulo": "Puntos", "tipo": "datos", "porDefecto": "Sí: 3; No: 1"}])
def marcador(valores):                    # DESCRIBE: lo dibuja el renderizador (con sus animaciones)
    return {"declarativo": {"ancho": 1000, "alto": 420, "capas": [
        {"tipo": "barras", "datos": "{puntos}", "izquierda": 40, "arriba": 40, "ancho": 920, "alto": 340}]}}

@plugin.grafico("ondas", ancho=600, alto=160, duracion_ms=3000, bucle=True)
def ondas(lienzo, ms, valores):           # DIBUJA cada cuadro: numpy (alto, ancho, 4) RGBA, transparente
    lienzo[100:, :50] = (255, 106, 43, 235)
```
- Describir es lo que conviene casi siempre (se ve como los de fábrica y el cambio de valores se anima). Dibujar, para lo que el repertorio no tiene; los cuadros se guardan y no se redibujan con los mismos valores.
- `plugin_escribir` → `plugin_probar` (el informe trae `graficos`: qué devolvió cada uno y si dibujó) → `plugin_activar`. Ya activo, el gráfico aparece en `grafico_listar` como `plugin-<id>-<nombre>` (categoría «Plugins») y se usa como cualquier otro: `grafico_probar`, `grafico_mostrar`, Rótulos, el guion.
- Con el plugin apagado el gráfico sigue en la lista, pero al usarlo dice que el plugin no está corriendo.