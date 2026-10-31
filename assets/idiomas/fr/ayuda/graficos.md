Un graphique est un dossier dans `Graficos/<nombre>/` avec son `grafico.json` (paramètres, taille, actions) et, soit une description déclarative (calques de `grafico_repertorio`), soit un `grafico.qml`. Il est dessiné dans un autre processus (`ba-grafico`) avec sa propre horloge : l'image à 380 ms est toujours la même, dans la preview, dans `grafico_probar` et à l'antenne.

## Le chemin
1. `grafico_listar` : presque toujours, un graphique existant avec d'autres paramètres suffit.
2. Un nouveau : déclaratif chaque fois que c'est possible (`grafico_repertorio`) ; QML seulement si le répertoire ne suffit pas (les pièges sont dans docs/funciones/GRAFICOS.md).
3. `grafico_probar {"nombre", "parametros", "momentos": [0, 150, 300, 450, 900]}` et REGARDEZ les images.
4. `grafico_en_escena` → `grafico_mostrar {"fuente", "nombre", "parametros"}` (ou `"armar": true` pour la preview).

## Vidéo en direct à l'intérieur (la caméra dans le synthé)
Un graphique avec un paramètre de type `fuente` (par ex. « Synthé avec caméra ») contient une source vidéo qui entre et sort avec lui : `grafico_mostrar {"fuente", "nombre": "zocalo-camara", "video": "Cámara 1"}`. Il se déclare avec `"escena": [{"fuente": "{camara}", "claves": [{"ms", "x", "y", "ancho", "alto", "alfa"}], "salida": [...]}]` en pixels du graphique (docs/funciones/GRAFICOS.md, « Video en vivo adentro »).

## GIF et vidéos avec alpha dans un graphique
- `grafico_medio_agregar {"nombre", "archivo"}` : un GIF, PNG, JPG ou un dossier de PNG est copié ; une VIDÉO (WebM VP9 avec alpha, ProRes 4444, MP4…) est convertie une fois en images avec sa transparence (`ancho` pour la réduire, `maxSegundos`). Regardez `alfa` dans la réponse : false = elle va masquer ce qu'il y a derrière.
- Dans la description : `{"tipo": "medio", "archivo": "medios/bandera.gif", "ancho": 200, "alto": 120, "izquierda": 20, "arriba": 20, "inicio": 300, "bucle": true}`. `inicio` est la ms du graphique à laquelle il démarre (avant, on ne le voit pas) ; sans `bucle`, il reste sur la dernière image. En QML : `import BA.Grafico` et `Medio { source: "medios/x"; inicio: 300 }`.
- Son temps est celui du graphique : faire glisser la barre de l'éditeur en arrière montre l'image correspondante.
- `grafico_medios {"nombre"}` liste ce qu'il contient ; `grafico_medio_quitar` en retire un.

## Camemberts et barres avec données (un vote en direct)
- Calques `torta` (avec `grosor` > 0 c'est un anneau) et `barras` (`orientacion` verticale ou horizontale). `"datos": "{votos}"` prend le paramètre ; déclarez-le dans grafico.json avec `"tipo": "datos"`.
- Les données s'écrivent comme une liste `[{"etiqueta": "Sí", "valor": 34, "color": "#2e7d32"}]`, comme un objet `{"Sí": 34, "No": 12}` ou comme du texte (ce qu'écrit l'opérateur dans Titres) : `Sí: 34; No: 12 #c62828; Abstenciones: 3`.
- Ils entrent animés (le camembert tourne, les barres poussent une à une et le nombre compte). `mostrar` : pourcentage, valeur, les deux ou rien.
- **Changer à l'antenne sans refaire l'entrée** : `grafico_actualizar {"fuente", "parametros": {"votos": …}}` (seulement ce qui change). Les parts et les barres passent des anciennes valeurs aux nouvelles (`cambio`, 700 ms). Il se passe la même chose si l'opérateur appuie sur « À l'antenne » dans Titres avec le même graphique affiché. Une liaison avec `vivo: true` l'anime aussi.
- Un QML écrit à la main : `Torta { datos: raiz.votos; anteriores: raiz.anteriores ? raiz.anteriores.votos : null }`, avec `property var anteriores` et `property bool sinEntrada` à la racine (le système les remplit lors du changement) et `"cambio": {"duracion": 700}` dans la fiche.

## Graphiques d'un plugin (en programmer un à la demande)
Quand le répertoire ne suffit pas ou que le graphique a besoin de logique (calculs, données d'une API, combien d'éléments afficher), écrivez un plugin Python avec `@plugin.grafico` (`help {"topic": "plugins"}` ; exemple `ejemplo-grafico` : `plugin_ver {"id": "ejemplo-grafico"}`) :
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
- Décrire est presque toujours préférable (le rendu ressemble à ceux d'origine et le changement de valeurs est animé). Dessiner, pour ce que le répertoire n'a pas ; les images sont mises en cache et ne sont pas redessinées avec les mêmes valeurs.
- `plugin_escribir` → `plugin_probar` (le rapport contient `graficos` : ce que chacun a renvoyé et s'il a dessiné) → `plugin_activar`. Une fois actif, le graphique apparaît dans `grafico_listar` sous `plugin-<id>-<nombre>` (catégorie « Plugins ») et s'utilise comme n'importe quel autre : `grafico_probar`, `grafico_mostrar`, Titres, le conducteur.
- Avec le plugin éteint, le graphique reste dans la liste, mais à l'utilisation il indique que le plugin ne tourne pas.
