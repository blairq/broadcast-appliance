#!/usr/bin/env python3
"""El margen del decodificador para cada frase semilla (AUM, arxiv 2001.10528).

POR QUE. Frobenius (afinar-grupos) mide la geometria de los estados ocultos:
es una aproximacion. Lo que decide es el decodificador. AUM («area under the
margin») mide, para cada ejemplo de entrenamiento, la puntuacion que el modelo
le da a SU etiqueta menos la mejor otra. Negativo: aun despues de entrenar con
esa frase, el modelo elegiria otra herramienta; es la firma de una frase mal
etiquetada o ambigua. Bajo: ambigua. Se promedia sobre varios puntos del
entrenamiento (--modelo acepta varios checkpoints); con uno solo es el margen
final.

COMO SE MIDE, igual que decide evaluar.py: prefijo leido una vez, la frase, y
el nombre correcto recorrido token por token por el arbol de la gramatica. En
cada paso donde hay mas de un token posible, margen = logit(correcto) -
max logit(otro permitido). El margen de la frase es el MINIMO de esos pasos:
alcanza con perder en uno para elegir otra herramienta. Si es negativo, se
decodifica de verdad y se anota que herramienta eligiria.

    tools/entrenar/margen-frases.py --modelo ~/entrenamientos/reactivo-v14 \\
        [--modelo otro-checkpoint ...] --salida margenes.json
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))


def cargar(ruta, torch):
    from peft import PeftModel
    from transformers import AutoModelForCausalLM, AutoTokenizer
    cfg = json.loads((Path(ruta) / "adapter_config.json").read_text())
    tok = AutoTokenizer.from_pretrained(ruta)
    m = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=torch.bfloat16,
                                             device_map="cuda:0")
    m = PeftModel.from_pretrained(m, ruta).merge_and_unload().eval()
    return tok, m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", action="append", required=True,
                    help="uno o varios (checkpoints del mismo entrenamiento): se promedia")
    ap.add_argument("--semillas", default=str(RAIZ / "datos/semillas-reactivas.json"))
    ap.add_argument("--salida", required=True)
    ap.add_argument("--frases", help="jsonl {herramienta, frase}: medir estas en vez de las semillas"
                    " (para probar una reescritura antes de ponerla)")
    args = ap.parse_args()

    import torch
    import lib_prompt as lp
    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv, 0)
    sem = json.loads(Path(args.semillas).read_text(encoding="utf-8"))["herramientas"]
    items = [(h, f) for h in sorted(sem) if h in nombres
             for f in sem[h].get("frases", []) + sem[h].get("correctivas", [])]
    if args.frases:
        items = [(r["herramienta"], r["frase"]) for r in
                 (json.loads(l) for l in Path(args.frases).read_text(encoding="utf-8").splitlines() if l.strip())]

    margenes = {i: [] for i in range(len(items))}
    elegida = {}
    for ruta in args.modelo:
        tok, modelo = cargar(ruta, torch)
        dev = modelo.device
        secuencias = {n: tuple(tok(lp.salida(n), add_special_tokens=False).input_ids) for n in nombres}
        siguientes, quedan = {}, {}
        for n, sq in secuencias.items():
            for k in range(len(sq) + 1):
                quedan.setdefault(sq[:k], set()).add(n)
                if k < len(sq):
                    siguientes.setdefault(sq[:k], set()).add(sq[k])
        sig_t = {p: torch.tensor(sorted(v), device=dev) for p, v in siguientes.items()}
        ent = tok(cabeza, return_tensors="pt").to(dev)
        with torch.no_grad():
            kv = modelo(**ent, use_cache=True).past_key_values
        n_cab = ent.input_ids.shape[1]

        def paso(t):
            with torch.no_grad():
                return modelo(input_ids=torch.tensor([[t]], device=dev), past_key_values=kv,
                              use_cache=True).logits[0, -1].float()

        for i, (h, frase) in enumerate(items):
            r = tok(frase + cola, add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
            with torch.no_grad():
                lg0 = modelo(input_ids=r, past_key_values=kv, use_cache=True).logits[0, -1].float()
            n_frase = n_cab + r.shape[1]
            # el camino del nombre correcto
            lg, pre, m = lg0, (), float("inf")
            for t in secuencias[h]:
                if len(quedan[pre]) == 1:
                    break
                ops = sig_t[pre]
                if len(ops) > 1:
                    otros = ops[ops != t]
                    m = min(m, (lg[t] - lg.index_select(0, otros).max()).item())
                pre = pre + (t,)
                lg = paso(t)
            margenes[i].append(m)
            kv.crop(n_frase)
            # si pierde, que eligiria (decodificacion voraz con la gramatica)
            if m < 0:
                lg, pre = lg0, ()
                while len(quedan[pre]) > 1:
                    ops = sig_t[pre]
                    t = int(ops[int(lg.index_select(0, ops).argmax())])
                    pre = pre + (t,)
                    lg = paso(t)
                elegida[i] = next(iter(quedan[pre]))
            kv.crop(n_cab)
        del modelo
        torch.cuda.empty_cache()
        print(f"  {ruta}: listo")

    out = {}
    for i, (h, f) in enumerate(items):
        ms = margenes[i]
        prom = sum(ms) / len(ms)
        out.setdefault(h, []).append({"frase": f, "margen": round(prom, 3),
                                      "elegiria": elegida.get(i) if prom < 0 else None})
    Path(args.salida).write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    todos = [x for v in out.values() for x in v]
    neg = [x for x in todos if x["margen"] < 0]
    print(f"frases: {len(todos)} · margen negativo (el modelo elegiria otra): {len(neg)}"
          f" · margen < 2: {sum(1 for x in todos if x['margen'] < 2)}")


if __name__ == "__main__":
    main()
