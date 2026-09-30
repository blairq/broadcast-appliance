#!/usr/bin/env python3
"""Elige la etiqueta de cada herramienta con el objetivo que salio de medir.

POR QUE NO ALCANZABA LO ANTERIOR. Probamos tres criterios y los tres cayeron:

  · «lo mas lejos posible»: |d| varia un 5% entre pares. En 2048 dimensiones
    dos tokens cualesquiera ya son casi ortogonales. No hay mas lejos.
  · «que la geometria de las etiquetas copie la de los conceptos»: las etiquetas
    a mano tienen alineacion -0,001 con los conceptos y son las que mejor
    puntuaron. No predice nada.
  · «primer token unico»: eso SI, pero es un arreglo del decodificador —evita
    decidir dos veces— y no mejora la separacion.

EL QUE SI TIENE MECANISMO. Sobre las 2.700 frases del corpus, cada herramienta
es una NUBE de estados ocultos. Para cada par se calcula `w`, la direccion que
mejor separa las dos nubes: eso es la informacion que el modelo TIENE. La lectura
usa `d`, la diferencia entre las filas de salida de las dos etiquetas. Si `w` y
`d` son casi ortogonales, la informacion esta y el lector apunta a otro lado.

Medido en v7: pasa en 13 pares de 3.654, y dos de ellos —grafico_en_escena
contra grafico_mostrar y contra grafico_ocultar, alineacion 0,055— son fallos
del examen. Ahi cambiar la etiqueta puede dar puntos, y no por separar mas: por
apuntar a donde esta la separacion.

    objetivo = suma sobre pares de  peso(i,j) . cos( w_ij , W_eti - W_etj )

    peso(i,j) es alto cuando las nubes se tocan (fisher bajo): un par que se
    separa por veinte desviaciones no necesita ayuda.

QUE HACE. Arranca de las etiquetas escritas a mano, que ya tienen el primer token
unico, y hace busqueda local: prueba reemplazos del vocabulario y se queda con
los que suben el objetivo SIN romper la unicidad del primer token. No inventa
etiquetas de cero —las escritas a mano son legibles y estan en el registro del
oficio— las mejora donde la medicion dice que hace falta.

  tools/entrenar/elegir-etiquetas.py --modelo ~/entrenamientos/reactivo-v7 \\
      --esquema datos/etiquetas.json --salida datos/etiquetas.json
"""
import argparse
import collections
import json
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENTRENAR = RAIZ / "datos/entrenamiento/entrenar.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--esquema", default=str(RAIZ / "datos/etiquetas.json"))
    ap.add_argument("--salida")
    ap.add_argument("--minimo", type=int, default=8)
    ap.add_argument("--pares", type=int, default=300,
                    help="cuantos pares (los que mas se tocan) entran al objetivo")
    ap.add_argument("--vueltas", type=int, default=3)
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
    # FUSIONAR EL ADAPTADOR: medido, 21,4 ms -> 11,8 ms por frase. El envoltorio
    # de peft mete un matmul extra y una indireccion de Python por capa, y acá
    # hay 2.700 frases que pasar.
    modelo = modelo.merge_and_unload()
    modelo.eval()
    dev = modelo.device
    W = modelo.lm_head.weight.float()
    norma = modelo.model.norm
    pre = {}
    norma.register_forward_pre_hook(lambda _m, e: pre.__setitem__("x", e[0].detach()))

    # --- las nubes ---------------------------------------------------------
    filas = [json.loads(l) for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip()]
    por_h = collections.defaultdict(list)
    for f in filas:
        por_h[f["herramienta"]].append(f["frase"])
    usables = {h: fs for h, fs in por_h.items() if len(fs) >= args.minimo}
    e = tok(cabeza, return_tensors="pt").to(dev)
    with torch.no_grad():
        sal = modelo(**e, use_cache=True)
    cache, n_cab = sal.past_key_values, e.input_ids.shape[1]
    peso_n = norma.weight.float()
    print(f"leyendo {sum(len(v) for v in usables.values())} frases de "
          f"{len(usables)} herramientas...", file=sys.stderr)
    nubes = {}
    for h, frases in sorted(usables.items()):
        pts = []
        for fr in frases:
            r = tok(fr + cola, add_special_tokens=False, return_tensors="pt").to(dev)
            with torch.no_grad():
                modelo(input_ids=r.input_ids, past_key_values=cache, use_cache=True)
            cache.crop(n_cab)
            x = pre["x"][0, -1, :].float()
            pts.append(((x / (x.pow(2).mean().sqrt() + 1e-6)) * peso_n).cpu())
        nubes[h] = torch.stack(pts)

    hs = sorted(nubes)
    ix = {h: i for i, h in enumerate(hs)}
    # w y peso de cada par. El peso cae con el fisher: un par que se separa por
    # veinte desviaciones no necesita que le arreglemos la etiqueta.
    pares = []
    for i in range(len(hs)):
        for j in range(i + 1, len(hs)):
            A, B = nubes[hs[i]], nubes[hs[j]]
            w = A.mean(0) - B.mean(0)
            nw = w.norm()
            if nw < 1e-6:
                continue
            w = w / nw
            pa, pb = A @ w, B @ w
            disp = ((pa.std() ** 2 + pb.std() ** 2) / 2).sqrt().item()
            fisher = abs(pa.mean().item() - pb.mean().item()) / max(1e-6, disp)
            pares.append((fisher, hs[i], hs[j], w))
    pares.sort(key=lambda x: x[0])
    pares = pares[:args.pares]
    Wp = torch.stack([p[3] for p in pares])                     # [pares, dim]
    pesos = torch.tensor([1.0 / max(0.5, p[0]) for p in pares])  # mas peso si se tocan
    ia = torch.tensor([ix[p[1]] for p in pares])
    jb = torch.tensor([ix[p[2]] for p in pares])
    print(f"objetivo sobre los {len(pares)} pares que mas se tocan "
          f"(fisher {pares[0][0]:.2f} a {pares[-1][0]:.2f})", file=sys.stderr)

    # --- candidatos --------------------------------------------------------
    ok = re.compile(r"^[a-záéíóúñ]{3,14}$")
    cand = {}
    for s_, i in tok.get_vocab().items():
        t = s_.replace("Ġ", "").replace("▁", "").lower()
        if ok.match(t) and t not in cand and len(tok(t, add_special_tokens=False).input_ids) == 1:
            cand[t] = tok(t, add_special_tokens=False).input_ids[0]
    print(f"{len(cand)} etiquetas candidatas", file=sys.stderr)

    d0 = json.loads(Path(args.esquema).read_text(encoding="utf-8"))
    d0 = d0.get("etiquetas", d0)
    actual = {h: d0[h] for h in hs}
    Wn = torch.nn.functional.normalize(W, dim=1).cpu()

    memo = {}

    def primer(e_):
        """El primer token de una etiqueta. Memorizado: el bucle lo pide
        cientos de miles de veces y tokenizar cuesta mas que la cuenta."""
        if e_ not in memo:
            memo[e_] = tok(e_, add_special_tokens=False).input_ids[0]
        return memo[e_]

    def objetivo(asig):
        L = torch.stack([Wn[primer(asig[h])] for h in hs])
        d = L[ia] - L[jb]
        d = torch.nn.functional.normalize(d, dim=1)
        return float((pesos * (Wp * d).sum(1)).sum())

    base = objetivo(actual)
    print(f"\nobjetivo de partida (etiquetas a mano): {base:.3f}")
    usados = {primer(v) for v in actual.values()}
    mejor = dict(actual)
    mejor_val = base
    for vuelta in range(args.vueltas):
        cambios = 0
        for h in hs:
            act = primer(mejor[h])
            for pal, tid in cand.items():
                if tid in usados and tid != act:
                    continue
                if pal == mejor[h]:
                    continue
                prueba = dict(mejor)
                prueba[h] = pal
                v = objetivo(prueba)
                if v > mejor_val + 1e-6:
                    usados.discard(act)
                    usados.add(tid)
                    mejor, mejor_val, act = prueba, v, tid
                    cambios += 1
        print(f"  vuelta {vuelta+1}: objetivo {mejor_val:.3f} · {cambios} cambios")
        if cambios == 0:
            break

    print(f"\nobjetivo: {base:.3f} -> {mejor_val:.3f}  ({100*(mejor_val-base)/abs(base):+.1f}%)")
    dif = [(h, actual[h], mejor[h]) for h in hs if actual[h] != mejor[h]]
    print(f"cambiaron {len(dif)} etiquetas:")
    for h, a, b in dif[:25]:
        print(f"   {h:24s} {a:14s} -> {b}")
    if args.salida:
        doc = json.loads(Path(args.esquema).read_text(encoding="utf-8"))
        etq = doc.get("etiquetas", doc)
        etq.update(mejor)
        Path(args.salida).write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n",
                                     encoding="utf-8")
        print(f"\n-> {args.salida}")


if __name__ == "__main__":
    main()
