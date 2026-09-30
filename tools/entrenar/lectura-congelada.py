#!/usr/bin/env python3
"""Cuanto acertaria cada esquema de etiquetas con los MISMOS estados ocultos.

PARA QUE. Comparar esquemas de etiquetas honestamente costaba un entrenamiento
de dos horas por esquema. Pero la decision es `argmax de h . W_etiqueta`, asi que
si se congelan los h —los que el modelo produce hoy— y se cambian nada mas las
filas W, sale cuanto acertaria cada esquema. Es una multiplicacion de matrices.

QUE MIDE Y QUE NO. Compara esquemas entre si con el mismo h, que es una
comparacion justa. Lo que NO hace es predecir el acierto final: despues de
reentrenar, h se acomoda a las etiquetas que le pongas, asi que los dos numeros
van a subir. Sirve para ordenar candidatos, no para prometer puntos.

Las etiquetas tienen primer token unico, asi que la fila del primer token decide
sola: no hace falta puntuar el nombre entero.

  tools/entrenar/lectura-congelada.py --modelo <m> \\
      --esquemas nombres,datos/etiquetas.json,/tmp/etiquetas-mejoradas.json
"""
import argparse
import collections
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENTRENAR = RAIZ / "datos/entrenamiento/entrenar.jsonl"
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--esquemas", required=True,
                    help="lista separada por comas; 'nombres' = los nombres actuales")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import lib_prompt as lp

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv)
    cfg = json.loads((Path(args.modelo) / "adapter_config.json").read_text())
    tok = AutoTokenizer.from_pretrained(args.modelo)
    modelo = AutoModelForCausalLM.from_pretrained(
        cfg["base_model_name_or_path"], dtype=torch.bfloat16, device_map="cuda:0")
    modelo = PeftModel.from_pretrained(modelo, args.modelo).merge_and_unload()
    modelo.eval()
    dev = modelo.device
    W = modelo.lm_head.weight.float().cpu()
    norma = modelo.model.norm
    peso_n = norma.weight.float()
    pre = {}
    norma.register_forward_pre_hook(lambda _m, e: pre.__setitem__("x", e[0].detach()))

    e = tok(cabeza, return_tensors="pt").to(dev)
    with torch.no_grad():
        sal = modelo(**e, use_cache=True)
    cache, n_cab = sal.past_key_values, e.input_ids.shape[1]

    def estados(frases):
        out = []
        for fr in frases:
            r = tok(fr + cola, add_special_tokens=False, return_tensors="pt").to(dev)
            with torch.no_grad():
                modelo(input_ids=r.input_ids, past_key_values=cache, use_cache=True)
            cache.crop(n_cab)
            x = pre["x"][0, -1, :].float()
            out.append(((x / (x.pow(2).mean().sqrt() + 1e-6)) * peso_n).cpu())
        return torch.stack(out)

    tr = [json.loads(l) for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip()]
    ex = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    print(f"estados ocultos de {len(tr)} frases de entrenamiento...", file=sys.stderr)
    Htr = estados([f["frase"] for f in tr])
    print(f"estados ocultos de {len(ex)} casos del examen...", file=sys.stderr)
    Hex = estados([c["frase"] for c in ex])

    print(f"\n{'esquema':34s} {'entrenamiento':>14s} {'examen':>10s}")
    for nombre_esq in args.esquemas.split(","):
        nombre_esq = nombre_esq.strip()
        if nombre_esq == "nombres":
            etq = {n: n for n in nombres}
        else:
            d = json.loads(Path(nombre_esq).read_text(encoding="utf-8"))
            etq = d.get("etiquetas", d)
        ids = []
        falta = False
        for n in nombres:
            if n not in etq:
                falta = True
                break
            ids.append(tok(etq[n], add_special_tokens=False).input_ids[0])
        if falta:
            print(f"{nombre_esq:34s}  (le faltan etiquetas)")
            continue
        L = W[torch.tensor(ids)]                      # [86, dim]
        def acierto(H, etiquetas_ok):
            lg = H @ L.T                              # [casos, 86]
            eleg = lg.argmax(1).tolist()
            return sum(1 for k, i in enumerate(eleg) if nombres[i] in etiquetas_ok[k])
        a_tr = acierto(Htr, [[f["herramienta"]] for f in tr])
        a_ex = acierto(Hex, [c["aceptables"] for c in ex])
        rep = len({i for i in ids}) != len(ids)
        print(f"{Path(nombre_esq).name[:32]:34s} {a_tr:6d}/{len(tr)} {100*a_tr/len(tr):5.1f}%"
              f" {a_ex:4d}/{len(ex)} {100*a_ex/len(ex):5.1f}%"
              + ("   OJO: primeros tokens repetidos" if rep else ""))
    print("\nMismos h en todas las filas: lo que cambia es UNICAMENTE la fila de")
    print("salida de cada etiqueta. Compara esquemas entre si; no predice el")
    print("acierto final, porque al reentrenar h se acomoda a la etiqueta.")


if __name__ == "__main__":
    main()
