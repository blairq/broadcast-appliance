#!/usr/bin/env python3
"""Donde se TOCAN las nubes de dos herramientas, y si la lectura mira para ahi.

LA IDEA, que es de Jorge, y ataca el termino que manda. La decision es
`argmax de h . W_etiqueta`, y el margen es |h|.|d|.cos(h,d). Medido en
`geometria.py`: |d| es practicamente constante (varia un 5%) porque en 2048
dimensiones dos tokens cualesquiera ya son casi ortogonales. Todo el margen
esta en cos(h,d), o sea en DONDE cae el estado oculto.

Entonces se mira eso directamente: cada frase de entrenamiento se pasa por el
modelo y se guarda su h en la posicion donde contesta. Eso da una NUBE de puntos
por herramienta. Dos herramientas que el modelo confunde son dos nubes que se
tocan, y eso se puede medir sin entrenar nada.

QUE SALE DE ACA, que son tres cosas distintas y todas utiles:

1. LA LISTA DE PARES QUE SE TOCAN, medida sobre las 2.700 frases del corpus y no
   sobre los 16 fallos del examen. Muchisima mas muestra, y aparecen las
   confusiones que el examen no llega a ver.

2. LAS FRASES QUE ESTAN DEL LADO EQUIVOCADO. Una frase de `audio_set` que cae
   dentro de la nube de `audio_measure` es una de dos cosas: una semilla mal
   escrita, o una ambiguedad de verdad. Las primeras se arreglan reescribiendo;
   las segundas son una decision de producto. Hoy no teniamos como distinguirlas.

3. SI LA LECTURA MIRA DONDE ESTA LA SEPARACION. Para cada par se calcula la
   direccion `w` que mejor separa las dos nubes, y se compara con la direccion
   `d` que separa las dos etiquetas. Si las nubes se separan bien pero w y d son
   casi ortogonales, la informacion ESTA y el lector no la usa: ese es el unico
   caso en que cambiar la etiqueta puede dar puntos, y se puede buscar un token
   cuya diferencia apunte para el lado de w.

El prefijo del prompt es identico en todas las frases (2.650 tokens), asi que se
lee una vez y se reusa su cache: lo que se procesa por frase son diez tokens.

  tools/entrenar/nubes.py --modelo ~/entrenamientos/reactivo-v7
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--minimo", type=int, default=8, help="frases minimas para tener nube")
    ap.add_argument("--pares", type=int, default=15, help="cuantos pares peores mostrar")
    ap.add_argument("--json")
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
    modelo = PeftModel.from_pretrained(modelo, args.modelo)
    modelo.eval()
    dev = modelo.device
    nucleo = modelo
    while not hasattr(nucleo, "lm_head") and hasattr(nucleo, "model"):
        nucleo = nucleo.model
    W = nucleo.lm_head.weight.float()
    norma, m = None, modelo
    for _ in range(6):
        if hasattr(m, "norm"):
            norma = m.norm
            break
        m = getattr(m, "model", getattr(m, "base_model", m))
    pre = {}
    norma.register_forward_pre_hook(lambda _m, e: pre.__setitem__("x", e[0].detach()))

    filas = [json.loads(l) for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip()]
    por_h = collections.defaultdict(list)
    for f in filas:
        por_h[f["herramienta"]].append(f["frase"])
    usables = {h: fs for h, fs in por_h.items() if len(fs) >= args.minimo}
    print(f"{len(filas)} frases · {len(usables)} herramientas con nube de {args.minimo}+",
          file=sys.stderr)

    # El prefijo se lee UNA vez: es identico en todas las frases.
    ids_cab = tok(cabeza, return_tensors="pt").to(dev)
    with torch.no_grad():
        sal = modelo(**ids_cab, use_cache=True)
    cache, n_cab = sal.past_key_values, ids_cab.input_ids.shape[1]

    nubes = {}
    for k, (h, frases) in enumerate(sorted(usables.items())):
        pts = []
        for fr in frases:
            resto = tok(fr + cola, add_special_tokens=False, return_tensors="pt").to(dev)
            with torch.no_grad():
                modelo(input_ids=resto.input_ids, past_key_values=cache, use_cache=True)
            cache.crop(n_cab)
            x = pre["x"][0, -1, :].float()
            rms = (x.pow(2).mean().sqrt() + 1e-6)
            peso = norma.weight.float() if hasattr(norma, "weight") else torch.ones_like(x)
            pts.append(((x / rms) * peso).cpu())
        nubes[h] = torch.stack(pts)
        if (k + 1) % 15 == 0:
            print(f"  {k+1}/{len(usables)}", flush=True, file=sys.stderr)

    hs = sorted(nubes)
    sec = {n: tok(n, add_special_tokens=False).input_ids[0] for n in nombres}
    resultados = []
    for i in range(len(hs)):
        for j in range(i + 1, len(hs)):
            A, B = nubes[hs[i]], nubes[hs[j]]
            # La direccion que mejor separa: diferencia de medias. Es la misma
            # forma que tiene la lectura del modelo —un funcional lineal— asi
            # que mide lo que el lector podria llegar a usar.
            w = A.mean(0) - B.mean(0)
            nw = w.norm()
            if nw < 1e-6:
                continue
            w = w / nw
            pa, pb = A @ w, B @ w
            corte = (pa.mean() + pb.mean()) / 2
            # Cuantos puntos caen del lado ajeno con el mejor corte.
            mal = ((pa < corte).sum() + (pb > corte).sum()).item()
            total = len(pa) + len(pb)
            # Margen normalizado: cuanto se separan las medias en unidades de
            # dispersion. Es el discriminante de Fisher en una dimension.
            disp = ((pa.std() ** 2 + pb.std() ** 2) / 2).sqrt().item()
            fisher = abs(pa.mean().item() - pb.mean().item()) / max(1e-6, disp)
            # ¿La lectura mira para donde estan separadas?
            d = (W[sec[hs[i]]] - W[sec[hs[j]]]).cpu()
            alineado = torch.dot(w, d / d.norm()).item()
            resultados.append({"a": hs[i], "b": hs[j], "solapan": mal / total,
                               "fisher": fisher, "alineacion_lectura": alineado})
    resultados.sort(key=lambda r: -r["solapan"])

    print("\nPARES QUE MAS SE TOCAN (de %d pares con nube)" % len(resultados))
    print("  solapan = puntos del lado ajeno con el mejor corte · fisher = separacion")
    print("  lectura = cuanto se alinea la direccion que separa con la que lee el modelo\n")
    for r in resultados[:args.pares]:
        aviso = ""
        if r["fisher"] > 1.5 and abs(r["alineacion_lectura"]) < 0.1:
            aviso = "  <- SEPARADAS pero la lectura no mira ahi"
        print(f"  {r['a']:22s} {r['b']:22s} solapan {100*r['solapan']:5.1f}%"
              f" · fisher {r['fisher']:5.2f} · lectura {r['alineacion_lectura']:+.3f}{aviso}")

    ali = [r for r in resultados if r["fisher"] > 1.5 and abs(r["alineacion_lectura"]) < 0.1]
    print(f"\npares bien separados que la lectura NO mira: {len(ali)} de {len(resultados)}")
    print("  Esos son los unicos donde cambiar la etiqueta puede dar puntos: la")
    print("  informacion esta y el lector apunta a otro lado.")
    import statistics
    print(f"\nsolapamiento medio {100*statistics.mean(r['solapan'] for r in resultados):.1f}%"
          f" · fisher medio {statistics.mean(r['fisher'] for r in resultados):.2f}"
          f" · alineacion media {statistics.mean(abs(r['alineacion_lectura']) for r in resultados):.3f}")
    if args.json:
        Path(args.json).write_text(json.dumps(resultados, ensure_ascii=False, indent=1),
                                   encoding="utf-8")


if __name__ == "__main__":
    main()
