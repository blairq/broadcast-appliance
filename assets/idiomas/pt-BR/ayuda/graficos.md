Um gráfico é uma pasta em `Graficos/<nombre>/` com seu `grafico.json` (parâmetros, tamanho, ações) e, ou uma descrição declarativa (camadas de `grafico_repertorio`), ou um `grafico.qml`. É desenhado em outro processo (`ba-grafico`) com um relógio próprio: o quadro dos 380 ms é sempre o mesmo, na prévia, em `grafico_probar` e no ar.

## O caminho
1. `grafico_listar`: quase sempre basta um que já existe com outros parâmetros.
2. Um novo: declarativo sempre que possível (`grafico_repertorio`); QML só se o repertório não der conta (as armadilhas estão em docs/funciones/GRAFICOS.md).
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}` e OLHE as imagens.
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}` (ou `"armar": true` para a prévia).

## Vídeo ao vivo dentro (a câmera no GC)
Um gráfico com um parâmetro do tipo `fuente` (p. ex. “GC com câmera”) leva uma fonte de vídeo que entra e sai com ele: `grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Cámara 1"}`. Declara-se com `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` em pixels do gráfico (docs/funciones/GRAFICOS.md, «Video en vivo adentro»).

## GIF e vídeos com alfa dentro de um gráfico
- `grafico_medio_agregar {"nombre", "archivo"}`: um GIF, PNG, JPG ou uma pasta de PNG é copiado; um VÍDEO (WebM VP9 com alfa, ProRes 4444, MP4…) é convertido uma vez em quadros com sua transparência (`ancho` para reduzi-lo, `maxSegundos`). Olhe `alfa` na resposta: false = vai cobrir o que está atrás.
- Na descrição: `{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`. `inicio` é o ms do gráfico em que começa (antes não aparece); sem `bucle` fica no último quadro. Em QML: `import BA.Grafico` e `Medio { source: "medios/x"; inicio: 300 }`.
- O tempo dele é o do gráfico: arrastar a barra do editor para trás mostra o quadro correspondente.
- `grafico_medios {"nombre"}` lista o que ele tem; `grafico_medio_quitar` remove um.

## Pizzas e barras com dados (uma votação ao vivo)
- Camadas `torta` (com `grosor` > 0 é um anel) e `barras` (`orientacion` vertical ou horizontal). `"datos": "{votos}"` pega o parâmetro; declare-o em grafico.json com `"tipo": "datos"`.
- Os dados são escritos como lista `[{"etiqueta": "Sí", "valor": 34, "color": "#2e7d32"}]`, como objeto `{"Sí": 34, "No": 12}` ou como texto (o que o operador escreve em GCs): `Sí: 34; No: 12 #c62828; Abstenciones: 3`.
- Entram animados (a pizza gira, as barras crescem uma a uma e o número conta). `mostrar`: porcentagem, valor, ambos ou nada.
- **Mudar no ar sem entrar de novo**: `grafico_actualizar {"fuente", "parametros": {"votos": …}}` (só o que muda). As fatias e as barras se movem dos valores anteriores para os novos (`cambio`, 700 ms). O mesmo acontece se o operador apertar “No ar” em GCs com o mesmo gráfico colocado. Um vínculo com `vivo: true` também o anima.
- Um QML escrito à mão: `Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`, com `property var anteriores` e `property bool sinEntrada` na raiz (o sistema as preenche ao mudar) e `"cambio": {"duracion": 700}` na ficha.

## Gráficos de um plugin (programar um sob encomenda)
Quando o repertório não basta ou o gráfico precisa de lógica (contas, dados de uma API, quantas coisas mostrar), escreva um plugin de Python com `@plugin.grafico` (`help {"topic": "plugins"}`; exemplo `ejemplo-grafico`: `plugin_ver {"id": "ejemplo-grafico"}`):
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
- Descrever é o que convém quase sempre (aparece como os padrão e a mudança de valores é animada). Desenhar, para o que o repertório não tem; os quadros são guardados e não são redesenhados com os mesmos valores.
- `plugin_escribir` → `plugin_probar` (o relatório traz `graficos`: o que cada um devolveu e se desenhou) → `plugin_activar`. Já ativo, o gráfico aparece em `grafico_listar` como `plugin-<id>-<nombre>` (categoria «Plugins») e é usado como qualquer outro: `grafico_probar`, `grafico_mostrar`, GCs, o roteiro.
- Com o plugin desligado o gráfico continua na lista, mas ao usá-lo diz que o plugin não está rodando.