#!/usr/bin/env python3
"""evaluar.py, pero con MLX: lo mismo que mide el servidor, corriendo como en la Mac.

POR QUE OTRO EVALUADOR. El modelo se sirve en una Apple M1 Pro con MLX y
cuantizado a 4 bits. Lo que importa ahi es (1) si cuantizar le cambio la
eleccion y (2) cuanto tarda por pedido. Este script mide igual que
evaluar.py —mismo prompt, prefijo leido una vez, gramatica sobre los nombres,
mismo dial de derivar, mismo formato de casos— para que comparar-modelos.py
ponga el q4 contra el bf16 caso por caso. Y agrega la latencia por pedido.

EL PREFIJO con un modelo hibrido (Qwen3.5): las capas de atencion lineal
guardan un estado (ArraysCache) y no se pueden «recortar»; entre frase y frase
se restaura una copia del estado que quedo despues del prefijo.

En Linux MLX corre en CPU: sirve para verificar una muestra (--limite). La
latencia de verdad y el banco completo, en la Mac.

    .venv-mlx/bin/python tools/entrenar/evaluar-mlx.py --modelo ~/entrenamientos/mlx/v15q4-q4 \\
        --casos datos/orientacion.jsonl [--limite 200] [--sesgo 0] [--salida-casos c.jsonl]
"""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="carpeta MLX (exportar-mlx.py)")
    ap.add_argument("--casos", help="por defecto el examen")
    ap.add_argument("--sesgo", type=float, default=0.0)
    ap.add_argument("--salida-casos")
    ap.add_argument("--limite", type=int, default=0, help="medir solo N casos, repartidos parejo")
    args = ap.parse_args()

    import mlx.core as mx
    from mlx_lm import load
    from mlx_lm.models.cache import make_prompt_cache
    from transformers import AutoTokenizer
    import lib_prompt as lp

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv, 0)
    modelo, _ = load(args.modelo)
    tok = AutoTokenizer.from_pretrained(args.modelo)

    secuencias = {n: tuple(tok(lp.salida(n), add_special_tokens=False).input_ids) for n in nombres}
    siguientes, quedan = {}, {}
    for n, sq in secuencias.items():
        for k in range(len(sq) + 1):
            quedan.setdefault(sq[:k], set()).add(n)
            if k < len(sq):
                siguientes.setdefault(sq[:k], set()).add(sq[k])
    sig = {p: sorted(v) for p, v in siguientes.items()}
    sig_mx = {p: mx.array(v) for p, v in sig.items()}
    tok_derivar = secuencias[lp.DERIVAR][0]

    # el prefijo, una vez
    ids_cab = tok(cabeza, add_special_tokens=False).input_ids
    t0 = time.time()
    cache = make_prompt_cache(modelo)
    modelo(mx.array([ids_cab]), cache=cache)
    mx.eval([c.state for c in cache])
    t_prefijo = time.time() - t0
    estado_cab = [(type(c), c.state, c.meta_state) for c in cache]

    def cache_del_prefijo():
        out = []
        for cls, st, meta in estado_cab:
            c = cls(size=2) if cls.__name__ == "ArraysCache" else cls()
            c.state = list(st) if isinstance(st, list) else st
            if meta:
                c.meta_state = meta
            out.append(c)
        return out

    def elegir(frase):
        c = cache_del_prefijo()
        r = tok(frase + cola, add_special_tokens=False).input_ids
        lg = modelo(mx.array([r]), cache=c)[0, -1]
        pre = ()
        for paso in range(12):
            if len(quedan[pre]) == 1:
                break
            ops = sig_mx[pre]
            val = lg[ops].astype(mx.float32)
            if paso == 0 and args.sesgo:
                val = val + args.sesgo * (ops == tok_derivar).astype(mx.float32)
            t = sig[pre][int(mx.argmax(val).item())]
            pre = pre + (t,)
            if len(quedan[pre]) == 1:
                break
            lg = modelo(mx.array([[t]]), cache=c)[0, -1]
        return next(iter(quedan[pre]))

    fuente = Path(args.casos) if args.casos else EXAMEN
    casos = [json.loads(l) for l in fuente.read_text(encoding="utf-8").splitlines() if l.strip()]
    conocidas = set(nombres)
    for c in casos:
        if "aceptables" not in c:
            c["real"] = c["herramienta"]
            c["aceptables"] = [c["herramienta"]] if c["herramienta"] in conocidas else [lp.DERIVAR]
    if args.limite and args.limite < len(casos):
        paso = len(casos) / args.limite
        casos = [casos[int(i * paso)] for i in range(args.limite)]

    tiempos, elegidas = [], {}
    for c in casos:
        t = time.time()
        elegidas[c["frase"]] = elegir(c["frase"])
        tiempos.append(time.time() - t)
    if args.salida_casos:
        Path(args.salida_casos).write_text("".join(
            json.dumps({"frase": c["frase"], "aceptables": c["aceptables"], "real": c.get("real"),
                        "elegida": elegidas[c["frase"]]}, ensure_ascii=False) + "\n" for c in casos),
            encoding="utf-8")
    suyos = [c for c in casos if lp.DERIVAR not in c["aceptables"]]
    ajenos = [c for c in casos if lp.DERIVAR in c["aceptables"]]
    atendidos = [c for c in suyos if elegidas[c["frase"]] != lp.DERIVAR]
    bien = [c for c in atendidos if elegidas[c["frase"]] in c["aceptables"]]
    rechazados = [c for c in ajenos if elegidas[c["frase"]] == lp.DERIVAR]
    aciertos = sum(1 for c in casos if elegidas[c["frase"]] in c["aceptables"])
    if ajenos:
        print(f"cobertura: {len(atendidos)}/{len(suyos)} ({100*len(atendidos)//max(1,len(suyos))}%) de lo suyo lo atendio solo")
        print(f"precision: {len(bien)}/{len(atendidos)} ({100*len(bien)//max(1,len(atendidos))}%) de lo que atendio lo acerto")
        print(f"derivado: {len(rechazados)}/{len(ajenos)} ({100*len(rechazados)//max(1,len(ajenos))}%) de lo ajeno lo paso al grande")
    print(f"acierto: {aciertos}/{len(casos)}  ({100*aciertos//len(casos)}%)")
    tiempos.sort()
    print(f"latencia por pedido: mediana {1000*statistics.median(tiempos):.0f} ms · "
          f"p95 {1000*tiempos[int(0.95*(len(tiempos)-1))]:.0f} ms · prefijo (una vez) {t_prefijo:.1f} s"
          f" · {mx.default_device()}")


if __name__ == "__main__":
    main()
