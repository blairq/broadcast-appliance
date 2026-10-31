一个图形就是 `Graficos/<nombre>/` 中的一个文件夹，里面有它的 `grafico.json`（参数、尺寸、操作），以及一个声明式描述（来自 `grafico_repertorio` 的图层）或一个 `grafico.qml`。它在另一个进程（`ba-grafico`）中用自己的时钟绘制：第 380 ms 的那一帧永远是同一帧，无论在预览、`grafico_probar` 中还是在播时。

## 做法
1. `grafico_listar`：几乎总是用一个现有图形、换些参数就够了。
2. 新建一个：尽量用声明式（`grafico_repertorio`）；只有素材库做不到时才用 QML（陷阱见 docs/funciones/GRAFICOS.md）。
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}`，然后**看**这些图片。
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}`（或用 `"armar": true` 载入预览）。

## 内嵌直播视频（字幕条里的摄像机画面）
带有 `fuente` 类型参数的图形（例如“带摄像机的字幕条”）会携带一个视频来源，随它一起进出：`grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Cámara 1"}`。用 `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` 声明，单位为图形的像素（docs/funciones/GRAFICOS.md，«Video en vivo adentro»）。

## 图形中的 GIF 和带透明通道的视频
- `grafico_medio_agregar {"nombre", "archivo"}`：GIF、PNG、JPG 或一个装满 PNG 的文件夹会被复制；**视频**（带 alpha 的 WebM VP9、ProRes 4444、MP4……）会一次性转成带透明度的帧（`ancho` 用于缩小，`maxSegundos`）。看返回结果中的 `alfa`：false = 会挡住后面的内容。
- 在描述中：`{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`。`inicio` 是它在图形中开始的毫秒数（之前不可见）；不加 `bucle` 会停在最后一帧。在 QML 中：`import BA.Grafico` 和 `Medio { source: "medios/x"; inicio: 300 }`。
- 它的时间就是图形的时间：在编辑器里往回拖动时间条，会显示对应的那一帧。
- `grafico_medios {"nombre"}` 列出它包含的媒体；`grafico_medio_quitar` 移除一个。

## 带数据的饼图和柱状图（实时表决）
- 图层 `torta`（`grosor` > 0 时为环形）和 `barras`（`orientacion` 纵向或横向）。`"datos": "{votos}"` 取该参数；在 grafico.json 中用 `"tipo": "datos"` 声明。
- 数据可以写成列表 `[{"etiqueta": "Sí", "valor": 34, "color": "#2e7d32"}]`、对象 `{"Sí": 34, "No": 12}`，或文本（操作员在“字幕条”页面中写的那种）：`Sí: 34; No: 12 #c62828; Abstenciones: 3`。
- 入场带动画（饼图旋转，柱子逐根增长，数字递增）。`mostrar`：百分比、数值、两者或都不显示。
- **在播时修改而不重新入场**：`grafico_actualizar {"fuente", "parametros": {"votos": …}}`（只传变化的部分）。扇区和柱子会从旧值过渡到新值（`cambio`，700 ms）。操作员在“字幕条”页面对同一个已上屏的图形按“在播”时也是如此。带 `vivo: true` 的数据关联也会触发动画。
- 手写的 QML：`Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`，根元素中加 `property var anteriores` 和 `property bool sinEntrada`（变化时由系统填写），参考卡中加 `"cambio": {"duracion": 700}`。

## 插件的图形（按需编写）
当素材库不够用，或图形需要逻辑（计算、来自 API 的数据、要显示多少项）时，用 `@plugin.grafico` 写一个 Python 插件（`help {"topic": "plugins"}`；示例 `ejemplo-grafico`：`plugin_ver {"id": "ejemplo-grafico"}`）：
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
- 几乎总是“描述”更合适（外观与自带图形一致，数值变化有动画）。“绘制”用于素材库没有的东西；帧会被缓存，相同的数值不会重绘。
- `plugin_escribir` → `plugin_probar`（报告中有 `graficos`：每个图形返回了什么、是否绘制成功）→ `plugin_activar`。启用后，图形会以 `plugin-<id>-<nombre>`（类别“Plugins”）出现在 `grafico_listar` 中，用法与其他图形相同：`grafico_probar`、`grafico_mostrar`、“字幕条”页面、串联单。
- 插件关闭时，图形仍在列表中，但使用时会提示插件没有运行。
