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
    desconocidas = [h for h in sem if h not in validas]
    if desconocidas:
        sys.exit("semillas para herramientas que no estan en el repertorio: " + ", ".join(desconocidas))

    # El examen primero: sus frases quedan PROHIBIDAS en el entrenamiento.
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))["casos"]
    examen = [c for c in corpus if any(t in validas for t in c["herramientas"])]
    prohibidas = {sin_tildes(c["pregunta"].lower()) for c in examen}

    filas, chocadas = [], 0
    for herr, d in sorted(sem.items()):
        for frase in d["frases"]:
            for v in variantes(frase, rnd):
                # Si una variante cae encima de una pregunta del examen, se
                # tira. Entrenar con el examen adentro infla el numero y no
                # avisa.
                if sin_tildes(v.lower()) in prohibidas:
                    chocadas += 1
                    continue
                filas.append(ejemplo(cabeza, cola, v, herr))

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
    print(f"entrenar: {len(ent)} · validar: {len(val)} · examen: {len(examen)} (intacto)")
    print(f"herramientas cubiertas: {len(porc)} de {len(validas)} del repertorio reactivo")
    print(f"descartadas por chocar con el examen: {chocadas}")
    print(f"las menos representadas: {', '.join(f'{h}({n})' for h, n in flojas)}")
    sin = sorted(validas - set(porc))
    if sin:
        print(f"SIN UNA SOLA FRASE ({len(sin)}): {', '.join(sin[:14])}")


if __name__ == "__main__":
    main()
