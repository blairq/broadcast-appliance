"""El prompt del modelo local. UNA sola vez, para todos.

POR QUE VIVE ACA Y NO EN CADA SCRIPT. Si el conjunto de entrenamiento se arma
con una forma de prompt y despues se sirve con otra, el modelo se degrada y no
hay como darse cuenta mirando: parece que "no aprendio". Se midio en este
mismo proyecto, por accidente: con el sistema pidiendo JSON y la gramatica
dejando escribir solo el nombre, el acierto se cayo de 56% a 16%.

Asi que el banco, el generador del corpus y —el dia que exista— el servidor
llaman todos a estas funciones. Si el prompt cambia, cambia para los tres.
"""
import json
import os
import re
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
INVENTARIO = RAIZ / "docs" / "mcp.json"

# Lo que se usa cuando algo se rompe en vivo. NO son las 291: el repertorio
# reactivo es chico y FIJO, y por eso se puede cachear entero.
REACTIVAS = ["audio", "escenas", "fuentes", "sala", "oradores", "imagenes", "estado", "salidas",
             # Cambiar un zocalo al aire —«esta mal el apellido»— es de lo mas
             # frecuente que pasa en vivo, y la categoria entera estaba afuera.
             "graficos",
             # «Volve a la del grafico de barras» es LA razon por la que existe
             # el carrusel, y la categoria estaba afuera del repertorio en vivo.
             "laminas"]

# Herramientas que caen en una categoria reactiva pero son TRABAJO PREVIO. El
# corte del MCP es administrativo y no coincide con el del oficio: `graficos`
# mezcla disenar una plantilla (antes, con tiempo) con mostrarla al aire
# (ahora, a los gritos). Sacarlas achica el espacio donde el modelo se pierde y
# ademas las vuelve IMPOSIBLES de elegir en vivo, que para las destructivas es
# lo que corresponde.
NO_REACTIVAS = {
    # Sale del repertorio (decision de Jorge): es configuracion mas que accion
    # en vivo, y en tres rondas del bucle sus frases cayeron siempre en los
    # canales de audio y video. 1 de 9 propuestas aceptadas.
    "operator_feed",
    "grafico_probar",      # ver como queda mientras se disena
    "grafico_repertorio",  # que se puede usar al escribir un grafico
    # De laminas: armar y desarmar una presentacion es trabajo PREVIO, y
    # desarmar ademas tarda minutos. En vivo se pasa, se salta y se busca.
    "laminas_armar",
    "laminas_editar",
    "laminas_desarmar",
    "laminas_etiquetar",
    "laminas_sin_analizar",
}

# PERFILES: sobre que herramientas del MCP se entrena, agrupadas por categoria.
# BA_PERFIL=<nombre> lee datos/perfiles/<nombre>.json y reemplaza las dos
# listas de arriba. El perfil «reactivo» es EXACTAMENTE esas listas: sin
# BA_PERFIL, o con el reactivo, el prompt queda identico byte por byte y los
# modelos ya entrenados siguen valiendo.
#   {"categorias": [...], "incluir": [...], "excluir": [...], "entradas": ["pedidos"]}
# `incluir` suma herramientas sueltas de otras categorias; `entradas` es lo que
# el modelo ve: «pedidos» del operador hoy, «eventos» del aparato despues.
PERFIL = os.environ.get("BA_PERFIL") or "reactivo"
INCLUIR = set()
SOLO = None  # «herramientas»: la lista EXACTA (el perfil no crece solo cuando el MCP suma herramientas)
ORDEN = None  # «grupos»: {grupo: [herramientas]}; la union es la lista exacta y el orden es el del prompt
GRUPOS = None
FUERA = []  # «fuera»: las que se sacaron del perfil a proposito (sus anotaciones sirven de derivar)
ENTRADAS = ["pedidos"]
_perfil = RAIZ / "datos" / "perfiles" / f"{PERFIL}.json"
if _perfil.exists():
    _p = json.loads(_perfil.read_text(encoding="utf-8"))
    REACTIVAS = list(_p.get("categorias", REACTIVAS))
    NO_REACTIVAS = set(_p.get("excluir", []))
    INCLUIR = set(_p.get("incluir", []))
    SOLO = set(_p["herramientas"]) if "herramientas" in _p else None
    if "grupos" in _p:
        GRUPOS = _p["grupos"]
        ORDEN = [n for g in GRUPOS.values() for n in g]
        SOLO = set(ORDEN)
    FUERA = list(_p.get("fuera", []))
    ENTRADAS = list(_p.get("entradas", ["pedidos"]))
    # el juego de etiquetas del perfil, salvo que se pida otro a mano
    if _p.get("etiquetas") and "BA_ETIQUETAS" not in os.environ:
        os.environ["BA_ETIQUETAS"] = "1"
        os.environ.setdefault("BA_ETIQUETAS_JUEGO", _p["etiquetas"])
elif PERFIL != "reactivo":
    raise SystemExit(f"no existe el perfil {PERFIL} ({_perfil})")


def en_perfil(h):
    """Si una herramienta del inventario entra en el perfil. Con una lista exacta
    (`herramientas`), solo esas: definido por categorias, el perfil sumaba solo
    las nuevas del MCP (vivo4 paso de 104 a 172 al regenerar el inventario) y el
    prompt cambiaba sin que nadie lo decidiera."""
    if SOLO is not None:
        return h["nombre"] in SOLO
    return h["nombre"] in INCLUIR or (h["categoria"] in REACTIVAS and h["nombre"] not in NO_REACTIVAS)

# LA VALVULA DE ESCAPE. Tiene que ser un nombre mas: la gramatica solo deja
# escribir nombres del repertorio, asi que sin esto el modelo esta OBLIGADO a
# elegir una herramienta aunque le pidan un cafe.
#
# POR QUE «derivar» Y NO «no_es_mio», que es como estaba. Decir que no es un
# callejon sin salida: el pedido se pierde y alguien tiene que repetirlo.
# Derivar RESUELVE: el asistente grande tiene las 292 herramientas, el buscador
# y el estado del aparato, asi que lo que este chico no sabe hacer, aquel si.
# El chico no decide si el pedido es imposible —no puede saberlo—, decide si es
# SUYO. Lo demas lo pasa, con la frase original tal cual la dijeron.
#
# Y por eso es UNA sola clase y no dos. Separar «no se puede» de «no me toca»
# le pide una distincion que va a errar, y errarla del lado malo significa
# rechazar algo que el grande podia hacer. Esa distincion la hace el grande.
DERIVAR = "derivar"

# EL DICCIONARIO DE ETIQUETAS (provisorio, decision de Jorge). La idea es que las
# etiquetas de datos/etiquetas.json REEMPLACEN a los nombres del MCP en el codigo
# del aparato; hasta que eso pase, este diccionario traduce en los dos sentidos.
#
# Se prende con BA_ETIQUETAS=1 y apagado todo queda identico byte por byte: la
# firma del prompt no cambia y un modelo entrenado sin etiquetas se sigue
# evaluando igual. Prendido cambian tres cosas y solo tres: el nombre con que
# cada herramienta aparece en las fichas, la respuesta que se entrena, y la
# gramatica del evaluador. Por que etiquetas y no nombres: con los nombres del
# MCP, 76 de 86 compartian el primer token (scene_* cubria 13) y la gramatica
# obligaba a elegir la familia antes que el miembro. Ver docs/modelos/modelo-local.md.
USAR_ETIQUETAS = os.environ.get("BA_ETIQUETAS") == "1"
_ETIQUETAS = None


def etiquetas():
    global _ETIQUETAS
    if _ETIQUETAS is None:
        f = RAIZ / "datos" / "etiquetas.json"
        # BA_ETIQUETAS_JUEGO elige el juego dentro del archivo: «etiquetas» (las
        # optimizadas, por defecto) o «legibles» (palabras del oficio que conservan
        # el significado; v11 mostró que los fragmentos lo pierden).
        juego = os.environ.get("BA_ETIQUETAS_JUEGO", "etiquetas")
        _ETIQUETAS = json.loads(f.read_text(encoding="utf-8"))[juego] if USAR_ETIQUETAS else {}
    return _ETIQUETAS


def salida(nombre):
    """Con que CONTESTA el modelo por esa herramienta. Sin etiquetas, su nombre."""
    return etiquetas().get(nombre, nombre) if USAR_ETIQUETAS else nombre


def entrada(etiqueta):
    """La herramienta real que corresponde a lo que contesto el modelo."""
    if not USAR_ETIQUETAS:
        return etiqueta
    return {v: k for k, v in etiquetas().items()}.get(etiqueta, etiqueta)

SISTEMA_NOMBRE = """Sos el asistente de un estudio de television EN VIVO. El operador te habla apurado y corto.
Contestas UNICAMENTE el nombre de la herramienta que corresponde. Una palabra, nada mas.
Si te piden HACER algo, elegi la que lo HACE. Los listados son para cuando preguntan QUE HAY.
Ante un problema de audio o de imagen, MEDIR primero tambien vale.

Si el pedido NO es de esta lista —es trabajo de preparacion, es de otra parte del
aparato, no es del aparato, le falta el objeto («sacalo» a quien) o no es un
pedido— contestas derivar y lo toma el asistente grande, que tiene todas las
herramientas. Eso ATIENDE el pedido; elegir la mas parecida lo arruina.

Herramientas:
"""


def inventario():
    return json.loads(INVENTARIO.read_text(encoding="utf-8"))


def orden_prompt(inv=None):
    """Las herramientas del perfil en el orden en que las ve el modelo: por grupos
    (las parecidas juntas) si el perfil los tiene; si no, alfabetico, como
    siempre (los perfiles viejos dan el mismo prompt)."""
    nombres = herramientas_reactivas(inv)
    if ORDEN is None:
        return nombres
    return [n for n in ORDEN if n in set(nombres)]


def herramientas_reactivas(inv=None):
    inv = inv or inventario()
    return sorted(h["nombre"] for h in inv["herramientas"]
                  if en_perfil(h))


def fichas(inv=None):
    """Cortas: que hace y que pide. Enteras son 7.200 tokens y la mayor parte
    es prosa que no hace falta para ELEGIR."""
    inv = inv or inventario()
    out = []
    for h in inv["herramientas"]:
        if not en_perfil(h):
            continue
        desc = re.split(r"(?<=[.!?])\s", h["descripcion"].replace("\n", " "))[0][:180]
        if USAR_ETIQUETAS:
            # Las descripciones nombran a OTRAS herramientas por su nombre real
            # («queda armada para el proximo `scene_take`»). Con el diccionario
            # prendido el modelo contesta `take`, asi que las fichas tienen que
            # hablar el mismo idioma que la respuesta.
            for real in sorted(etiquetas(), key=len, reverse=True):
                desc = re.sub(rf"\b{re.escape(real)}\b", salida(real), desc)
        req = h["esquema"].get("required", [])
        props = list((h["esquema"].get("properties") or {}).keys())
        args = ", ".join(f"{p}*" if p in req else p for p in props[:6])
        out.append(f"- {salida(h['nombre'])}({args}): {desc}")
    return sorted(out)


def ejemplos_resueltos(cuantos, semilla=11, archivo=None):
    """Pares «frase -> herramienta» para meter DENTRO del prefijo.

    POR QUE ACA Y NO EN CADA PEDIDO. El prefijo se cachea: estos ejemplos se
    procesan UNA vez, al arrancar, y despues no cuestan absolutamente nada por
    pedido. Aprendizaje en contexto a costo cero de latencia, que es una
    economia rara y conviene aprovecharla.

    COMO SE ELIGEN. Uno por herramienta antes de repetir ninguna: con 73 clases
    y veinte ejemplos, veinte clases tendrian ejemplo y cincuenta y tres no, y
    el modelo aprende a preferir las que vio. Dentro de cada herramienta se
    prefiere la frase MAS CORTA, que es la que mejor muestra la intencion sin
    ruido de superficie.
    """
    import random
    archivo = archivo or (RAIZ / "datos/entrenamiento/entrenar.jsonl")
    if cuantos <= 0 or not archivo.exists():
        return ""
    porh = {}
    for linea in archivo.read_text(encoding="utf-8").splitlines():
        if not linea.strip():
            continue
        f = json.loads(linea)
        porh.setdefault(f["herramienta"], []).append(f["frase"])
    rnd = random.Random(semilla)
    for h in porh:
        porh[h].sort(key=lambda x: (len(x), x))
    herrs = sorted(porh)
    rnd.shuffle(herrs)
    elegidos, vuelta = [], 0
    while len(elegidos) < cuantos and vuelta < 12:
        for h in herrs:
            if vuelta < len(porh[h]):
                elegidos.append((porh[h][vuelta], h))
                if len(elegidos) >= cuantos:
                    break
        vuelta += 1
    rnd.shuffle(elegidos)
    cuerpo = "\n".join(f"{frase} -> {salida(herr)}" for frase, herr in elegidos)
    return "\nEjemplos de como pide el operador:\n" + cuerpo + "\n"


def prefijo(inv=None, ejemplos=0):
    """La parte FIJA: sistema + fichas (+ ejemplos). Es lo unico que se
    cachea, asi que tiene que ser identica byte a byte en cada pedido."""
    return SISTEMA_NOMBRE + "\n".join(fichas(inv)) + ejemplos_resueltos(ejemplos)


def partes(inv=None, ejemplos=0):
    """(cabeza, cola) alrededor de la frase del operador.

    Lo variable va DESPUES de lo fijo: el cache es de prefijo. Y `<think>`
    vacio es el modo no-thinking de Qwen3, escrito en el propio prompt para
    que no dependa de una opcion del servidor.
    """
    cabeza = f"<|im_start|>system\n{prefijo(inv, ejemplos)}<|im_end|>\n<|im_start|>user\n"
    cola = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
    return cabeza, cola


def huella(inv=None, ejemplos=0):
    """La firma del prompt: si cambia, el modelo entrenado quedo viejo.

    POR QUE HACE FALTA. Agregar una herramienta al MCP cambia las fichas, y las
    fichas son parte del prompt con el que se entreno. El modelo sigue
    contestando —no falla nada— pero le estan mostrando algo distinto de lo que
    vio, y ademas puede elegir una herramienta que nunca aprendio. Es de las
    degradaciones que no avisan, asi que hay que preguntarle.

    Se guarda junto al conjunto de entrenamiento y la comparan el banco y el
    entrenador.
    """
    import hashlib
    inv = inv or inventario()
    pref = prefijo(inv, ejemplos)
    return {"prompt": hashlib.sha256(pref.encode()).hexdigest()[:16],
            "herramientas": hashlib.sha256("|".join(herramientas_reactivas(inv)).encode()).hexdigest()[:16],
            "cuantas": len(herramientas_reactivas(inv)),
            "tokens_aprox": len(pref) // 4}


def nombres_posibles(inv=None):
    """TODO lo que el modelo puede contestar: las herramientas Y `derivar`.

    Existe porque no tenerla costo una corrida entera. evaluar.py armaba su
    propia lista con `herramientas_reactivas()` —las 85, sin `derivar`— y con
    eso construia el arbol de continuaciones validas. O sea que en la
    evaluacion `derivar` era una respuesta IMPOSIBLE, y el «0 de 8» que salia
    no medía al modelo: medía la lista.

    Peor: con la respuesta correcta prohibida, el barrido del vector de
    control tampoco podía dar otra cosa que precision cayendose y cero
    derivaciones, que es justo lo que interpretamos como «la direccion no
    apunta ahi». No se sabe: no se midio.

    Es el mismo error que este proyecto ya se comio con el prompt, y por el
    mismo motivo: dos lugares armando lo que tiene que ser uno.
    """
    return list(herramientas_reactivas(inv)) + [DERIVAR]


def gramatica_nombre(nombres, con_derivar=True):
    """Solo un nombre que exista. No es un pedido: es imposible desviarse.

    `derivar` entra como una opcion mas. Sin ella, «pedi un cafe» tiene que
    salir como alguna herramienta del estudio: no por tonto, por obligado.
    """
    opciones = list(nombres) + ([DERIVAR] if con_derivar else [])
    return "root ::= " + " | ".join('"%s"' % n for n in opciones) + "\n"


# Palabras que aparecen en todas las etiquetas y no distinguen nada.
VACIAS = {"el", "la", "los", "las", "un", "una", "de", "del", "al", "a", "que",
          "y", "o", "en", "se", "lo", "le", "por", "para", "con", "sin", "su",
          "esta", "este", "esa", "ese", "no", "si", "mas", "ya", "hay", "es"}


def frases_desde_etiquetas(tags, titulo):
    """Trozos de 2 a 4 palabras de las etiquetas, como pedidos cortos.

    Se cortan en trozos y no se usa la etiqueta entera porque la etiqueta es
    una bolsa —«silenciar mutear bajar subir volumen»— y eso nadie lo dice.
    Un trozo de tres palabras se parece bastante a un pedido de cabina.
    """
    palabras = [w for w in re.findall(r"[a-záéíóúñ]+", tags.lower()) if w not in VACIAS and len(w) > 2]
    out, i = [], 0
    while i < len(palabras):
        trozo = palabras[i:i + 3]
        if len(trozo) >= 2:
            out.append(" ".join(trozo))
        i += 3
    if titulo:
        out.append(titulo.lower())
    # Sin repetidas y sin las larguisimas.
    vistas, limpio = set(), []
    for f in out:
        if f not in vistas and len(f) <= 48:
            vistas.add(f)
            limpio.append(f)
    return limpio[:6]


def fuera_del_repertorio(inv):
    """Las herramientas que el modelo chico NO tiene que usar, con sus frases.

    Son las administrativas: armar una lamina, configurar una cuenta, editar
    una receta. Existen en el aparato pero no se piden al aire, y el modelo
    chico no las tiene entre sus fichas.

    POR QUE IMPORTAN IGUAL. Si nunca las ve, cuando alguien le pide una elige
    la pariente mas cercana —medido: con doce herramientas que no conocia,
    eligio la vecina 3 de 3 veces, siempre con aplomo—. Verlas etiquetadas
    como «no es mio» es lo que convierte ese reflejo en un no.

    :returns: [(nombre, categoria, [frases])]
    """
    repertorio = set(herramientas_reactivas(inv))
    out = []
    for h in inv["herramientas"]:
        if h["nombre"] in repertorio:
            continue
        tags = (h["etiquetas"]["propias"] or "") + " " + (h["etiquetas"]["porPrefijo"] or "")
        fr = frases_desde_etiquetas(tags, h["titulo"])
        if fr:
            out.append((h["nombre"], h.get("categoria", ""), fr))
    return out
