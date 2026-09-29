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
        if rnd.random() < 0.18:
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
            "completion": herramienta + "<|im_end|>",
            # Sueltos, para poder mirar el conjunto sin desarmar el prompt.
            "frase": frase, "herramienta": herramienta}


def main():
    rnd = random.Random(7)  # fijo: dos corridas dan el mismo conjunto
    inv = lp.inventario()
    validas = set(lp.herramientas_reactivas(inv))
    cabeza, cola = lp.partes(inv)

    sem = json.loads(SEMILLAS.read_text(encoding="utf-8"))["herramientas"]
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
    VARIANTES_NORMAL, VARIANTES_CORRECTIVA, TECHO_CORRECTIVAS = 4, 9, 0.35

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

    # EQUILIBRIO. Una herramienta con cinco ejemplos y otra con treinta le
    # ensenan al modelo a preferir la frecuente cuando duda, que es justo
    # cuando no tiene que inventar preferencias. A las flacas se les generan
    # mas variantes de sus propias semillas hasta llegar al minimo; no se
    # inventan frases nuevas, se escribe distinto lo mismo.
    minimo = 14
    por_herr = {}
    for f in filas:
        por_herr.setdefault(f["herramienta"], []).append(f)
    for herr, d_ in sorted(sem.items()):
        faltan = minimo - len(por_herr.get(herr, []))
        intentos = 0
        while faltan > 0 and intentos < 200:
            intentos += 1
            frase = rnd.choice(d_["frases"] + d_.get("correctivas", []))
            if parecido(frase, prohibidas_tokens):
                continue
            for v in variantes(frase, rnd, cuantas=2):
                if parecido(v, prohibidas_tokens):
                    continue
                f = ejemplo(cabeza, cola, v, herr)
                f["arregla"] = d_.get("arregla") or []
                filas.append(f)
                faltan -= 1
                if faltan <= 0:
                    break

    # Sin repetidos exactos, y barajado: si quedan agrupadas por herramienta,
    # los ultimos pasos de entrenamiento ven una sola clase.
    vistas, unicas = set(), []
    for f in filas:
        k = (f["frase"], f["herramienta"])
        if k not in vistas:
            vistas.add(k)
            unicas.append(f)
    rnd.shuffle(unicas)

    # La validacion se parte POR FRASE SEMILLA, no al azar: si la misma
    # intencion queda de los dos lados, validar mide memoria y no aprendizaje.
    por_semilla = {}
    for f in unicas:
        por_semilla.setdefault((f["herramienta"], sin_tildes(f["frase"].lower())[:14]), []).append(f)
    claves = sorted(por_semilla)
    rnd.shuffle(claves)
    corte = max(1, len(claves) // 8)
    val = [f for k in claves[:corte] for f in por_semilla[k]]
    ent = [f for k in claves[corte:] for f in por_semilla[k]]

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
    huella["correctivas"] = {"ejemplos": len(corr), "porcion": round(porcion, 3),
                             "versiones": sorted({v for f in corr for v in f["arregla"]})}
    (SALIDA / "huella.json").write_text(json.dumps(huella, ensure_ascii=False, indent=1) + "\n",
                                        encoding="utf-8")

    print(f"entrenar: {len(ent)} · validar: {len(val)} · examen: {len(examen)} (intacto)")
    if auto:
        print(f"de segunda mano (propuestas sin revisar): {len(auto)} herramientas")
    print(f"firma del prompt: {huella['prompt']} · {huella['cuantas']} herramientas")
    if corr:
        aviso = "  OJO: pasan el techo, el corpus se esta pareciendo a una lista de casos raros" \
                if porcion > TECHO_CORRECTIVAS else ""
        print(f"correctivas (de fallos de {', '.join(huella['correctivas']['versiones'])}): "
              f"{len(corr)} ejemplos, {100*porcion:.0f}% del corpus{aviso}")
    print(f"herramientas cubiertas: {len(porc)} de {len(validas)} del repertorio reactivo")
    print(f"descartadas por parecerse al examen: {chocadas}")
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
