Графика — это папка `Graficos/<nombre>/` со своим `grafico.json` (параметры, размер, действия) и либо декларативным описанием (слои из `grafico_repertorio`), либо `grafico.qml`. Она рисуется в отдельном процессе (`ba-grafico`) со своими часами: кадр на 380 мс всегда один и тот же — в предпросмотре, в `grafico_probar` и в эфире.

## Путь
1. `grafico_listar`: почти всегда достаточно существующей графики с другими параметрами.
2. Новая: декларативная, когда только можно (`grafico_repertorio`); QML — только если репертуара не хватает (ловушки описаны в docs/funciones/GRAFICOS.md).
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}` и ПОСМОТРИТЕ на изображения.
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}` (или `"armar": true` для предпросмотра).

## Живое видео внутри (камера в титре)
Графика с параметром типа `fuente` (например, «Титр с камерой») несёт источник видео, который появляется и уходит вместе с ней: `grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Cámara 1"}`. Объявляется через `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` в пикселях графики (docs/funciones/GRAFICOS.md, «Video en vivo adentro»).

## GIF и видео с альфа-каналом внутри графики
- `grafico_medio_agregar {"nombre", "archivo"}`: GIF, PNG, JPG или папка с PNG копируется; ВИДЕО (WebM VP9 с альфой, ProRes 4444, MP4…) один раз переводится в кадры с прозрачностью (`ancho`, чтобы уменьшить, `maxSegundos`). Посмотрите `alfa` в ответе: false = будет перекрывать то, что позади.
- В описании: `{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`. `inicio` — мс графики, когда оно начинается (до этого не видно); без `bucle` останавливается на последнем кадре. В QML: `import BA.Grafico` и `Medio { source: "medios/x"; inicio: 300 }`.
- Его время — это время графики: если тянуть ползунок редактора назад, показывается соответствующий кадр.
- `grafico_medios {"nombre"}` перечисляет, что в ней есть; `grafico_medio_quitar` убирает одно.

## Круговые и столбчатые диаграммы с данными (голосование в прямом эфире)
- Слои `torta` (с `grosor` > 0 это кольцо) и `barras` (`orientacion` вертикальная или горизонтальная). `"datos": "{votos}"` берёт параметр; объявите его в grafico.json с `"tipo": "datos"`.
- Данные пишутся списком `[{"etiqueta": "Sí", "valor": 34, "color": "#2e7d32"}]`, объектом `{"Sí": 34, "No": 12}` или текстом (то, что пишет оператор в «Титрах»): `Sí: 34; No: 12 #c62828; Abstenciones: 3` (подписи могут быть на любом языке: «За: 34; Против: 12»).
- Появляются с анимацией (круг вращается, столбцы растут по одному, число отсчитывается). `mostrar`: процент, значение, оба или ничего.
- **Изменить в эфире без повторного появления**: `grafico_actualizar {"fuente", "parametros": {"votos": …}}` (только то, что меняется). Сектора и столбцы плавно переходят от старых значений к новым (`cambio`, 700 мс). То же происходит, если оператор нажмёт «В эфир» в «Титрах» с той же графикой. Привязка с `vivo: true` тоже анимирует её.
- QML, написанный вручную: `Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`, с `property var anteriores` и `property bool sinEntrada` в корне (система заполняет их при изменении) и `"cambio": {"duracion": 700}` в описании.

## Графика из плагина (запрограммировать по запросу)
Когда репертуара не хватает или графике нужна логика (расчёты, данные из API, сколько элементов показывать), напишите плагин на Python с `@plugin.grafico` (`help {"topic": "plugins"}`; пример `ejemplo-grafico`: `plugin_ver {"id": "ejemplo-grafico"}`):
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
- Описывать — почти всегда лучший вариант (выглядит как встроенная графика, и смена значений анимируется). Рисовать — для того, чего нет в репертуаре; кадры сохраняются и не перерисовываются при тех же значениях.
- `plugin_escribir` → `plugin_probar` (отчёт содержит `graficos`: что вернула каждая и рисовала ли) → `plugin_activar`. После активации графика появляется в `grafico_listar` как `plugin-<id>-<nombre>` (категория «Plugins») и используется как любая другая: `grafico_probar`, `grafico_mostrar`, «Титры», сценарий.
- Когда плагин выключен, графика остаётся в списке, но при использовании сообщает, что плагин не запущен.
