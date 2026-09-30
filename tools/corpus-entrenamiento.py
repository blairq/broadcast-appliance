#!/usr/bin/env python3
"""Arma el conjunto de entrenamiento del modelo chico, desde las semillas.

QUE PRODUCE. `datos/entrenamiento/` con tres archivos en JSONL, en el MISMO
formato con el que se va a servir (ver tools/lib_prompt.py):
    entrenar.jsonl   lo que ve el modelo al entrenar
    validar.jsonl    para mirar mientras entrena, sin tocarlo
    examen.jsonl     el corpus de siempre (src/app/tests/datos/preguntas.json)

EL EXAMEN NO SE TOCA NUNCA. Es el unico numero honesto que hay: 125 preguntas
escritas antes de pensar en entrenar nada. Si entrenas sobre el, deja de medir.

DE DONDE SALEN LAS FRASES. De semillas escritas a mano (datos/semillas-
reactivas.json), con variacion mecanica encima: sin tildes, con el verbo al
final, con «che» o «dale» adelante, con un typo. Es como se habla y se tipea en
cabina. La variacion es de SUPERFICIE: el significado —que frase corresponde a
que herramienta— lo pone la semilla, escrita por una persona.

LO QUE ESTO NO ES. No es dato real de operacion. Las frases son verosimiles,
no observadas, y eso tiene un limite: ensenan el mapa, no la distribucion de lo
que de verdad pasa un martes a las ocho. Lo real sale de la traza del aparato
—lo que el operador hizo con las manos y en que estado estaba todo— y todavia
no se guarda. Este corpus es el arranque, no el final.

  tools/corpus-entrenamiento.py
"""
import json
import random
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent
SEMILLAS = RAIZ / "datos/semillas-reactivas.json"
# Las propuestas automaticas (ver tools/corpus-descubrir.py). Entran al
# conjunto porque es mejor que una herramienta nueva no exista para el modelo,
# pero viven aparte para que se vea cuanto del corpus es de segunda mano.
SEMILLAS_AUTO = RAIZ / "datos/semillas-auto.json"
# La jerga del oficio: sinonimos del gremio y giros enteros. Ver datos/jerga.json.
JERGA = RAIZ / "datos/jerga.json"
DERIVACIONES = RAIZ / "datos/derivaciones.json"
# Pedidos REALES para las herramientas que no son del repertorio: como la gente
# pide configurar una cuenta o armar una lamina. Reemplazan a las frases armadas
# con palabras sueltas de las etiquetas del buscador, que es como nadie habla.
DERIVACIONES_ADMIN = RAIZ / "datos/derivaciones-admin.jsonl"
# El banco de orientacion: frases de OTRO autor con las que se mide donde se
# pierde el modelo. No se puede entrenar sobre ellas, ni sobre nada casi igual.
ORIENTACION = RAIZ / "datos/orientacion.jsonl"
CORPUS = RAIZ / "src/app/tests/datos/preguntas.json"
SALIDA = RAIZ / "datos/entrenamiento"

# Como se escribe apurado. Cada una se aplica o no, al azar.
ADELANTE = ["", "", "", "che, ", "dale, ", "rapido, ", "ojo, ", "eh, "]
ATRAS = ["", "", "", "", " ya", " ahora", " porfa", " dale"]


def sin_tildes(s):
    return "".join(c for c in unicodedata.normalize("NFD", s)
                   if unicodedata.category(c) != "Mn")


def con_typo(s, rnd):
    """Un dedo que se corre. Una sola letra: mas que eso es otra palabra."""
    i = rnd.randrange(len(s))
    if not s[i].isalpha():
        return s
    vecinos = {"a": "s", "s": "a", "e": "r", "r": "e", "o": "i", "i": "o",
               "n": "m", "m": "n", "l": "k", "c": "v", "t": "y", "u": "y"}
    c = s[i].lower()
    return s[:i] + vecinos.get(c, c) + s[i + 1:]


def con_jerga(frase, sinonimos, rnd):
    """La misma frase con la palabra del gremio.

    Un control no dice «cámara», dice «cam», «plano» o «toma». No es adorno:
    el modelo fallo en «salimos al aire» —eligio PARAR la transmision— porque
    nunca habia visto esa construccion. Cambiar el vocabulario ensena mas que
    mil parafrasis con las mismas palabras.

    Se cambia UN concepto por vez: cambiar tres deja una frase que no dice
    nadie.
    """
    palabras = sorted(sinonimos)
    rnd.shuffle(palabras)
    for concepto in palabras:
        if concepto in frase:
            reemplazo = rnd.choice(sinonimos[concepto])
            if reemplazo != concepto:
                return frase.replace(concepto, reemplazo, 1)
    return None


def variantes(frase, rnd, cuantas=4):
    """La misma intencion, escrita de varias maneras. Sin repetir."""
    out = {frase}
    for _ in range(cuantas * 4):
        if len(out) >= cuantas + 1:
            break
        v = frase
        # Casi siempre sin tildes: nadie las pone escribiendo apurado.
        if rnd.random() < 0.6:
            v = sin_tildes(v)
        if rnd.random() < 0.35:
            v = rnd.choice(ADELANTE) + v
        if rnd.random() < 0.3:
            v = v + rnd.choice(ATRAS)
        # POCOS TYPOS, y medido por que. Con 0,18 el 34% del corpus tenia un
        # dedo corrido, y un typo cae en la nube de OTRA herramienta 1,8 veces
        # mas que una frase limpia (12,6% contra 7,1%, sobre v7). El tokenizador
        # de Qwen parte el castellano en pedazos, asi que «nandalo» no es
        # «mandalo» con una letra cambiada: es otra secuencia de tokens entera. Y
        # v7 vio typos al entrenar y aun asi no generalizo a typos nuevos —el
        # espacio de dedos corridos es enorme y unos cientos al azar no lo
        # cubren—. Se dejan algunos, porque en cabina se tipea apurado, pero ya
        # no son un tercio del corpus.
        if rnd.random() < 0.06:
            v = con_typo(v, rnd)
        if rnd.random() < 0.25:
            v = v.lower()
        out.add(v.strip())
    return sorted(out)


def parecido(frase, prohibidas_tokens, umbral=0.6):
    """True si la frase se parece DEMASIADO a una del examen.

    POR QUE NO ALCANZA CON LAS EXACTAS, que es como estaba. La guarda tiraba
    «se corto la transmision» por identica, pero dejaba pasar «eh, se corto la
    transmision» y «se corto la transmision ya». Medido cuando se descubrio:
    25 de 48 casos del examen tenian una frase casi igual adentro del
    entrenamiento. O sea que el modelo se aprendia medio examen y el numero
    que salia no media nada.

    Se compara por palabras compartidas y no por distancia de letras: lo que
    importa es que sea OTRA forma de pedir lo mismo, no que cambien dos
    caracteres.
    """
    t = set(sin_tildes(frase.lower()).split())
    if not t:
        return True
    return any(len(t & p) / max(len(t), len(p)) >= umbral for p in prohibidas_tokens if p)


def ejemplo(cabeza, cola, frase, herramienta):
    """Un ejemplo con el prompt EXACTO del servidor. El unico campo que
    cambia entre entrenar y servir es que aca viene la respuesta."""
    return {"prompt": cabeza + frase + cola,
            # La RESPUESTA es la etiqueta si el diccionario esta prendido
            # (BA_ETIQUETAS=1); el campo `herramienta` sigue siendo el nombre
            # real, para poder analizar el conjunto sin traducir.
            "completion": lp.salida(herramienta) + "<|im_end|>",
            # Sueltos, para poder mirar el conjunto sin desarmar el prompt.
            "frase": frase, "herramienta": herramienta}


def main():
    rnd = random.Random(7)  # fijo: dos corridas dan el mismo conjunto
    inv = lp.inventario()
    validas = set(lp.herramientas_reactivas(inv))
    cabeza, cola = lp.partes(inv)

    sem = json.loads(SEMILLAS.read_text(encoding="utf-8"))["herramientas"]
    jerga = json.loads(JERGA.read_text(encoding="utf-8")) if JERGA.exists() else {"sinonimos": {}, "giros": {}}
    # Los giros entran con peso NORMAL, no como correctivas. Marcarlos como
    # excepcion llevo el corpus al 48% de «casos raros», y es al reves: la
    # jerga no es un caso raro, es el REGISTRO EN QUE SE HABLA. Rara es la
    # frase de manual que escribi yo. Las que si van con peso extra son los
    # pares que distinguen dos giros parecidos («salir» contra «bajar» del
    # aire), y esas ya estaban marcadas.
    for herr, frases in jerga.get("giros", {}).items():
        if herr in sem:
            sem[herr]["frases"] = list(dict.fromkeys(sem[herr]["frases"] + frases))
    # LO QUE EL BUCLE SACO. tools/entrenar/bucle-corpus.py puede quitar una
    # semilla que empuja a su herramienta hacia una vecina; si esa semilla venia
    # de la jerga, la anota en `excluir` y aca se respeta. Si no, cada
    # regeneracion la volveria a meter.
    for herr, d_ in sem.items():
        fuera = set(d_.get("excluir", []))
        if fuera:
            d_["frases"] = [f for f in d_["frases"] if f not in fuera]
            d_["correctivas"] = [f for f in d_.get("correctivas", []) if f not in fuera]
    auto = {}
    if SEMILLAS_AUTO.exists():
        auto = json.loads(SEMILLAS_AUTO.read_text(encoding="utf-8"))["herramientas"]
        # Las escritas a mano MANDAN: si una herramienta tiene las dos, la
        # propuesta sobra y encima es peor.
        auto = {k: v for k, v in auto.items() if k not in sem}
        sem = {**sem, **auto}
    desconocidas = [h for h in sem if h not in validas]
    if desconocidas:
        sys.exit("semillas para herramientas que no estan en el repertorio: " + ", ".join(desconocidas))

    # El examen primero: sus frases quedan PROHIBIDAS en el entrenamiento.
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))["casos"]
    examen = [c for c in corpus if any(t in validas for t in c["herramientas"])]
    # Las de abstencion se parten ACA, antes de calcular lo prohibido: si se
    # parten despues, sus variantes entran al entrenamiento y el examen mide
    # memoria. Es el mismo error que ya nos costo 25 casos contaminados.
    der_examen, der_entrenar = [], []
    if DERIVACIONES.exists():
        _a = json.loads(DERIVACIONES.read_text(encoding="utf-8"))
        _todas = [f for k, v in _a.items() if not k.startswith("_") for f in v]
        rnd.shuffle(_todas)
        _corte = max(1, len(_todas) // 5)
        der_examen, der_entrenar = _todas[:_corte], _todas[_corte:]
        examen += [{"pregunta": f, "herramientas": [lp.DERIVAR]} for f in der_examen]

    prohibidas = {sin_tildes(c["pregunta"].lower()) for c in examen}
    prohibidas_tokens = [set(p.split()) for p in prohibidas]

    # Las SEMILLAS se revisan primero: si una semilla se parece a una pregunta
    # del examen, todas sus variantes tambien, y conviene saber cual hay que
    # reescribir en vez de tirar el resultado en silencio.
    semillas_malas = []
    for herr, d in sorted(sem.items()):
        for frase in d["frases"]:
            if parecido(frase, prohibidas_tokens):
                semillas_malas.append((herr, frase))

    # LAS CORRECTIVAS PESAN MAS. Una semilla marcada con `arregla` salio de un
    # fallo medido de una version anterior: son las que ensenan lo que el
    # modelo NO sabia. Treinta frases diluidas entre mil trescientas no mueven
    # nada, asi que se les generan mas variantes.
    #
    # PERO CON TECHO, y es importante: si las dificiles se acumulan version
    # tras version sin limite, el corpus termina pareciendose a una lista de
    # casos raros y no a lo que un operador dice de verdad. Se entrena para el
    # examen y se pierde en la cabina.
    #
    # MENOS VARIANTES DESDE QUE LOS GRUPOS SE AFINARON (30/09). Con 2 o 3 frases
    # por herramienta, 4 variantes mecanicas por frase eran la unica fuente de
    # variedad. Con 15 a 20 frases REALES y distintas por herramienta, eso
    # multiplicaba el corpus por 5 con copias sin tildes o con muletillas
    # (7.760 ejemplos). Una variante por frase alcanza para que vea la forma
    # apurada; la variedad la ponen las frases.
    VARIANTES_NORMAL, VARIANTES_CORRECTIVA, TECHO_CORRECTIVAS = 1, 2, 0.35

    filas, chocadas, n_correctivas = [], 0, 0
    for herr, d in sorted(sem.items()):
        # `correctivas` son FRASES, no la herramienta entera: marcar
        # `audio_set` completa multiplicaba sus 59 frases por un error en tres,
        # y el corpus se iba al 58% de casos raros.
        for frase, corrige in ([(f, False) for f in d["frases"]] +
                               [(f, True) for f in d.get("correctivas", [])]):
            cuantas = VARIANTES_CORRECTIVA if corrige else VARIANTES_NORMAL
            if parecido(frase, prohibidas_tokens):
                chocadas += 1
                continue
            for v in variantes(frase, rnd, cuantas=cuantas):
                # Si una variante cae encima de una pregunta del examen, se
                # tira. Entrenar con el examen adentro infla el numero y no
                # avisa.
                if parecido(v, prohibidas_tokens):
                    chocadas += 1
                    continue
                f = ejemplo(cabeza, cola, v, herr)
                f["arregla"] = (d.get("arregla") or []) if corrige else []
                filas.append(f)
                n_correctivas += bool(corrige)

    # PASAR LA PELOTA. Una parte del corpus ensena a NO elegir ninguna y
    # derivar al asistente grande, que tiene las 292 herramientas.
    #
    # CON TECHO, y por un motivo distinto al de las correctivas: derivar es la
    # respuesta comoda, porque nunca se equivoca de herramienta. Si el modelo
    # ve demasiados ejemplos, aprende que pasarla siempre paga, y entonces deja
    # de servir para lo unico que sirve: contestar en 0,8 s sin salir de la
    # maquina. Un chico que deriva todo es un rodeo, no un asistente.
    # De donde salen las frases de derivar:
    #   1. las escritas a mano (datos/abstenciones.json): lo de FUERA del
    #      aparato —el clima, mandar un mail, apagar la luz— y lo ambiguo.
    #   2. las 207 herramientas del MCP que NO estan en el repertorio: armar
    #      una lamina, configurar una cuenta, editar una receta. Es la mitad
    #      que importa: lo de afuera es facil, lo de ADENTRO que no le toca es
    #      lo que lo tienta, porque se parece a lo suyo. Y es justo donde
    #      derivar gana: el grande SI tiene esa herramienta.
    # 15% y no 12%. El 12% se fijo por miedo a que derivara de mas, y el banco
    # de orientacion midio lo contrario: v7 deriva solo el 44% de lo que no le
    # toca. El fine-tune publico mas parecido al nuestro (Qwen3-1.7B, tool
    # calling) usa 15% de ejemplos de «esto no es para mi».
    TECHO_DERIVAR = 0.15
    n_der = 0
    for f in der_entrenar:
        for v in variantes(f, rnd, cuantas=3):
            if parecido(v, prohibidas_tokens):
                chocadas += 1
                continue
            filas.append(dict(ejemplo(cabeza, cola, v, lp.DERIVAR), arregla=[]))
            n_der += 1

    # LAS ADMINISTRATIVAS. Las frases salen de las etiquetas del buscador, o
    # sea que son bolsas de palabras: se las mete en un pedido para que suenen
    # a alguien pidiendo algo y no a una lista.
    MARCOS = ["necesito {}", "hacé {}", "podés {}", "quiero {}", "andá a {}",
              "dale con {}", "{}", "me ayudás con {}"]
    # LA GUARDA QUE IMPORTA. Una etiqueta administrativa puede decir casi lo
    # mismo que una frase del repertorio (`grafico_probar` y `grafico_mostrar`
    # comparten media etiqueta). Ensenarle a decir que no a algo que SI es suyo
    # es el peor error posible: no se nota en el numero general y le saca una
    # herramienta. Ante la duda, la frase no entra.
    suyas = [set(sin_tildes(f.lower()).split())
             for d in sem.values() for f in d["frases"] + d.get("correctivas", [])]
    fuera = lp.fuera_del_repertorio(inv)
    # Las frases reales, donde las haya. Las armadas con palabras sueltas quedan
    # solo como respaldo para las herramientas que todavia no tienen pedidos
    # escritos. Medido con el banco: 569 de los 667 errores de v7 son pedidos
    # administrativos que no derivo, y sus ejemplos de derivar eran del tipo
    # «necesito configurar agente motor».
    reales = {}
    if DERIVACIONES_ADMIN.exists():
        for l in DERIVACIONES_ADMIN.read_text(encoding="utf-8").splitlines():
            if l.strip():
                r = json.loads(l)
                reales.setdefault(r["herramienta"], []).append(r["frase"])
    banco = []
    if ORIENTACION.exists():
        banco = [set(sin_tildes(json.loads(l)["frase"].lower()).split())
                 for l in ORIENTACION.read_text(encoding="utf-8").splitlines() if l.strip()]
    fuera = [(n, c, reales.get(n) or frs, n in reales) for n, c, frs in fuera]
    # Repartidas por categoria y no de corrido: si se toman las primeras
    # doscientas alfabeticamente, el modelo ve `agente_*` cuarenta veces y
    # `receta_*` ninguna, y aprende a rechazar una palabra en vez de un tipo
    # de pedido.
    por_cat = {}
    for nombre, cat, frs, es_real in fuera:
        por_cat.setdefault(cat, []).append((nombre, frs, es_real))
    for v_ in por_cat.values():
        rnd.shuffle(v_)
    ronda, cats = [], sorted(por_cat)
    for i in range(max(len(v_) for v_ in por_cat.values())):
        for c in cats:
            if i < len(por_cat[c]):
                ronda.append(por_cat[c][i])
    # El cupo sale del techo: lo que ya pusieron las de a mano se descuenta.
    cupo = int(len(filas) * TECHO_DERIVAR / (1 - TECHO_DERIVAR)) - n_der
    n_admin, rechazadas, n_reales, n_banco = 0, 0, 0, 0
    # DE A UNA FRASE POR HERRAMIENTA Y POR VUELTA. Tomar hasta tres de cada una
    # antes de pasar a la siguiente llenaba el cupo con la mitad: medido, 99 de
    # 207 herramientas. Para aprender a derivar, el modelo tiene que ver TODAS,
    # aunque sea una vez; recien despues, mas de cada una.
    tocadas_set = set()
    for vuelta in range(3):
        if n_admin >= cupo:
            break
        for nombre, frs, es_real in ronda:
            if n_admin >= cupo:
                break
            if vuelta >= len(frs) or (not es_real and vuelta >= 2):
                continue
            base = frs[vuelta]
            frase = base if es_real else rnd.choice(MARCOS).format(base)
            if parecido(frase, suyas, umbral=0.5):
                rechazadas += 1
                continue
            if banco and parecido(frase, banco):
                n_banco += 1
                continue
            for v in variantes(frase, rnd, cuantas=1):
                if parecido(v, prohibidas_tokens) or parecido(v, suyas, umbral=0.5):
                    chocadas += 1
                    continue
                filas.append(dict(ejemplo(cabeza, cola, v, lp.DERIVAR), arregla=[]))
                n_admin += 1
                n_reales += bool(es_real)
                tocadas_set.add(nombre)
    tocadas = len(tocadas_set)
    n_der += n_admin

    # EQUILIBRIO. Una herramienta con cinco ejemplos y otra con treinta le
    # ensenan al modelo a preferir la frecuente cuando duda, que es justo
    # cuando no tiene que inventar preferencias. A las flacas se les generan
    # mas variantes de sus propias semillas hasta llegar al minimo; no se
    # inventan frases nuevas, se escribe distinto lo mismo.
    #
    # EL PISO SE CUENTA SOBRE LO QUE QUEDA, y esto estaba mal. Se contaba
    # sobre `filas`, que todavia tiene repetidos y todavia incluye el octavo
    # que se va a validacion. Una herramienta que llegaba justo a 14 terminaba
    # con 9 o 10 en entrenar.jsonl. Medido cuando se descubrio: 38 de las 85
    # herramientas por debajo del minimo, y dos de ellas —laminas_listar con
    # 10 y laminas_aplicar con 9— eran fallos del examen de v6.
    #
    # Asi que el piso se aplica sobre el conteo EFECTIVO: descontando los
    # repetidos y el corte de validacion.
    minimo = 14
    CORTE_VAL = 8  # uno de cada ocho se va a validar
    objetivo = int(minimo * CORTE_VAL / (CORTE_VAL - 1)) + 1

    def unicas_de(herr, filas_):
        return len({f["frase"] for f in filas_ if f["herramienta"] == herr})

    flacas = []
    for herr, d_ in sorted(sem.items()):
        intentos = 0
        while unicas_de(herr, filas) < objetivo and intentos < 400:
            intentos += 1
            frase = rnd.choice(d_["frases"] + d_.get("correctivas", []))
            if parecido(frase, prohibidas_tokens):
                continue
            for v in variantes(frase, rnd, cuantas=3):
                if parecido(v, prohibidas_tokens):
                    continue
                f = ejemplo(cabeza, cola, v, herr)
                f["arregla"] = d_.get("arregla") or []
                filas.append(f)
        if unicas_de(herr, filas) < objetivo:
            # No se pudo: la herramienta tiene DEMASIADO POCAS semillas y la
            # variacion mecanica ya no saca frases distintas. Eso no se arregla
            # generando mas: hay que escribir mas frases a mano.
            flacas.append((herr, unicas_de(herr, filas),
                           len(d_["frases"]) + len(d_.get("correctivas", []))))

    # Sin repetidos exactos, y barajado: si quedan agrupadas por herramienta,
    # los ultimos pasos de entrenamiento ven una sola clase.
    vistas, unicas = set(), []
    for f in filas:
        k = (f["frase"], f["herramienta"])
        if k not in vistas:
            vistas.add(k)
            unicas.append(f)
    rnd.shuffle(unicas)

    # EL TECHO SE RESPETA RECORTANDO, no estimando. El cupo de mas arriba se
    # calcula antes del equilibrio y del deduplicado, asi que siempre queda
    # corto o largo; aca ya esta el conjunto entero y el numero es el que es.
    # Se sacan al azar, con lo cual la mezcla entre las de a mano y las
    # administrativas queda igual.
    sobran = len([f for f in unicas if f["herramienta"] == lp.DERIVAR]) \
        - int(len(unicas) * TECHO_DERIVAR * 0.9)  # 0.9: el techo se mide
    # despues de partir validacion, y la particion no reparte las de «no» en
    # la misma proporcion. Se recorta un poco de mas y queda abajo siempre.
    if sobran > 0:
        idx = [i for i, f in enumerate(unicas) if f["herramienta"] == lp.DERIVAR]
        rnd.shuffle(idx)
        fuera_ = set(idx[:sobran])
        n_der -= sobran
        unicas = [f for i, f in enumerate(unicas) if i not in fuera_]

    # La validacion se parte POR FRASE SEMILLA, no al azar: si la misma
    # intencion queda de los dos lados, validar mide memoria y no aprendizaje.
    #
    # Y SE PARTE POR HERRAMIENTA, que es lo que faltaba. Cortando un octavo del
    # monton entero, a una herramienta con dos semillas le podia tocar que se
    # fuera una: perdia la mitad de lo que tenia. Asi quedaban herramientas con
    # nueve ejemplos aunque el piso dijera catorce, y dos de esas —laminas_listar
    # y laminas_aplicar— fueron fallos medidos de v6.
    #
    # Las flacas no donan: una herramienta con menos de ocho semillas se queda
    # con todas. La validacion sale de las gordas, que es de donde tiene que
    # salir.
    por_semilla = {}
    for f in unicas:
        por_semilla.setdefault((f["herramienta"], sin_tildes(f["frase"].lower())[:14]), []).append(f)
    por_herr_claves = {}
    for k in sorted(por_semilla):
        por_herr_claves.setdefault(k[0], []).append(k)
    claves_val = []
    for herr, ks in sorted(por_herr_claves.items()):
        rnd.shuffle(ks)
        claves_val += ks[:len(ks) // 8]
    claves_val = set(claves_val)
    val = [f for k, fs in por_semilla.items() if k in claves_val for f in fs]
    ent = [f for k, fs in por_semilla.items() if k not in claves_val for f in fs]

    SALIDA.mkdir(parents=True, exist_ok=True)
    def escribir(nombre, filas_):
        (SALIDA / nombre).write_text(
            "\n".join(json.dumps(f, ensure_ascii=False) for f in filas_) + "\n", encoding="utf-8")
    escribir("entrenar.jsonl", ent)
    escribir("validar.jsonl", val)
    escribir("examen.jsonl", [{"frase": c["pregunta"], "aceptables": c["herramientas"]} for c in examen])

    porc = {}
    for f in ent:
        porc[f["herramienta"]] = porc.get(f["herramienta"], 0) + 1
    flojas = sorted(porc.items(), key=lambda x: x[1])[:5]
    gordas = sorted(porc.items(), key=lambda x: -x[1])[:3]
    # La firma del prompt, al lado del conjunto: es lo que despues deja
    # preguntar «¿el modelo que tengo corresponde a estas fichas?».
    huella = lp.huella(inv)
    huella["ejemplos_entrenar"] = len(ent)
    huella["herramientas_con_frases"] = len(porc)
    huella["propuestas_automaticas"] = sorted(auto)
    corr = [f for f in ent if f.get("arregla")]
    porcion = len(corr) / max(1, len(ent))
    n_ab = sum(1 for f in ent if f["herramienta"] == lp.DERIVAR)
    huella["derivar"] = {"ejemplos": n_ab, "porcion": round(n_ab / max(1, len(ent)), 3),
                            "herramientas_fuera_del_repertorio": len(fuera),
                            "fuera_con_ejemplos": tocadas}
    huella["correctivas"] = {"ejemplos": len(corr), "porcion": round(porcion, 3),
                             "versiones": sorted({v for f in corr for v in f["arregla"]})}
    (SALIDA / "huella.json").write_text(json.dumps(huella, ensure_ascii=False, indent=1) + "\n",
                                        encoding="utf-8")

    print(f"entrenar: {len(ent)} · validar: {len(val)} · examen: {len(examen)} (intacto)")
    if auto:
        print(f"de segunda mano (propuestas sin revisar): {len(auto)} herramientas")
    print(f"firma del prompt: {huella['prompt']} · {huella['cuantas']} herramientas")
    print(f"administrativas que NO son suyas: {tocadas} de {len(fuera)} herramientas, "
          f"{n_admin} ejemplos ({n_reales} de pedidos reales)"
          + (f" · {rechazadas} no entraron por parecerse a una del repertorio" if rechazadas else "")
          + (f" · {n_banco} por parecerse al banco de orientacion" if n_banco else ""))
    if n_ab:
        pab = n_ab / max(1, len(ent))
        print(f"derivar al grande: {n_ab} ejemplos, {100*pab:.0f}% del corpus"
              + ("  OJO: pasa el techo, va a derivar de mas" if pab > TECHO_DERIVAR else ""))
    if corr:
        aviso = "  OJO: pasan el techo, el corpus se esta pareciendo a una lista de casos raros" \
                if porcion > TECHO_CORRECTIVAS else ""
        print(f"correctivas (de fallos de {', '.join(huella['correctivas']['versiones'])}): "
              f"{len(corr)} ejemplos, {100*porcion:.0f}% del corpus{aviso}")
    print(f"herramientas cubiertas: {len(porc)} de {len(validas)} del repertorio reactivo")
    print(f"descartadas por parecerse al examen: {chocadas}")
    if flacas:
        print(f"\nHERRAMIENTAS QUE NECESITAN MAS SEMILLAS A MANO ({len(flacas)}): no se")
        print("  llega al piso variando las que hay; las variantes salen repetidas.")
        for h_, q, sem_ in sorted(flacas, key=lambda x: x[1])[:12]:
            print(f"    {h_:24s} {q:3d} ejemplos, de {sem_} semilla(s)")
    if semillas_malas:
        print(f"SEMILLAS QUE HAY QUE REESCRIBIR ({len(semillas_malas)}): se parecen a una")
        print("  pregunta del examen, asi que no entran. Decir lo mismo con otras palabras:")
        for herr, f in semillas_malas[:14]:
            print(f"    {herr:22s} {f!r}")
    print(f"ejemplos por herramienta: {min(porc.values())} a {max(porc.values())}"
          f" (mediana {sorted(porc.values())[len(porc)//2]})")
    print(f"  las mas flacas: {', '.join(f'{h}({n})' for h, n in flojas)}")
    print(f"  las mas gordas: {', '.join(f'{h}({n})' for h, n in gordas)}")
    sin = sorted(validas - set(porc))
    if sin:
        print(f"SIN UNA SOLA FRASE ({len(sin)}): {', '.join(sin[:14])}")


if __name__ == "__main__":
    main()
