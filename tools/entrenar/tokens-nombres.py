#!/usr/bin/env python3
"""Cuanto se pisan los nombres de las herramientas EN TOKENS.

EL PROBLEMA QUE MIDE. El modelo contesta con gramatica: escribe el nombre token
por token y cada token que escribe lo COMPROMETE. Si 13 herramientas empiezan
con el token `scene`, al elegir ese token todavia no eligio nada —le quedan 13—
y encima ya no puede arrepentirse y salir a `laminas_*`.

Medido acá (Qwen3-1.7B, 86 nombres): 76 de 86 comparten su primer token. Los 86
colapsan en 25 primeros tokens. Y de los fallos de v7, 6 de 11 son entre
herramientas de la MISMA familia, cuando al azar serian el 6%.

La literatura lo tiene visto y con el mismo remedio: en tool calling, los
nombres que comparten el primer token se resuelven mal aunque el modelo acierte
la familia, y renombrarlos para que el primer token sea unico recupera el
acierto (arxiv 2605.07990).

DAR VUELTA EL NOMBRE es el remedio barato: `audio_set` -> `set_audio`. La
palabra que DISTINGUE pasa adelante y la que agrupa va atras, con lo cual la
decision dificil se toma cuando el modelo todavia tiene todo el contexto y no
se comprometio con nada.

  tools/entrenar/tokens-nombres.py
"""
import collections
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))


def dar_vuelta(n):
    """audio_set -> set_audio."""
    p = n.split("_")
    return "_".join(p[1:] + p[:1]) if len(p) > 1 else n


def main():
    import lib_prompt as lp
    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained("unsloth/Qwen3-1.7B")
    nombres = lp.nombres_posibles()

    for etiqueta, fn in (("tal cual", lambda n: n), ("dado vuelta", dar_vuelta)):
        lista = [fn(n) for n in nombres]
        ids = {a: tok(a, add_special_tokens=False).input_ids for a in lista}
        c1 = collections.Counter(tuple(v[:1]) for v in ids.values())
        quedan = [c1[tuple(v[:1])] for v in ids.values()]
        pasos = []
        for v in ids.values():
            for k in range(1, len(v) + 1):
                if sum(1 for w in ids.values() if w[:k] == v[:k]) == 1:
                    pasos.append(k)
                    break
            else:
                pasos.append(len(v))
        print(f"{etiqueta:12s} primeros tokens distintos: {len(c1):3d}/{len(nombres)}"
              f" · candidatos tras el 1er token: media {statistics.mean(quedan):.1f},"
              f" peor {max(quedan)}"
              f" · tokens hasta decidir: mediana {statistics.median(pasos):.0f},"
              f" peor {max(pasos)}")
        comp = collections.defaultdict(list)
        for a, v in ids.items():
            comp[tuple(v[:1])].append(a)
        peores = sorted((v for v in comp.values() if len(v) > 1), key=len, reverse=True)[:4]
        for v in peores:
            print(f"     {len(v):2d} comparten: {', '.join(sorted(v)[:5])}"
                  + (" ..." if len(v) > 5 else ""))


if __name__ == "__main__":
    main()
