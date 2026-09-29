#!/usr/bin/env python3
"""Con que ETIQUETA conviene que el modelo conteste cada herramienta.

EL PROBLEMA. Los 86 nombres actuales comparten 25 primeros tokens: `scene`
cubre 13 herramientas. Con gramatica, el modelo escribe token por token y cada
token lo compromete, asi que al escribir `scene` todavia no eligio nada y ya no
puede salir a `laminas_*`. Medido: de los 11 fallos de v7 entre herramientas, 6
son dentro de la misma familia (al azar seria el 6%).

LA ETIQUETA IDEAL MIDE UN TOKEN. Si cada herramienta se contesta con un token
unico, no hay compromiso prematuro porque no hay segundo paso: una pasada, un
argmax sobre 86 tokens. Es mas rapido y el sesgo del logit queda exacto.

QUE SE MIDE, Y EN QUE ESPACIO. No en un embedder externo tipo RAG: en la matriz
de salida del propio modelo, que es donde la decision ocurre. Qwen3-1.7B tiene
los embeddings ATADOS —la matriz de entrada y la de salida son la misma— asi que
los vectores de los tokens y los estados ocultos son comparables sin traducir
nada.

    vector de la herramienta = estado oculto medio de su ficha (titulo + que hace)
    vector de la etiqueta    = fila de la matriz de salida de su token

EL PESO POR IMPORTANCIA, que es la idea de Jorge. No todas las confusiones
cuestan igual: que se pisen dos etiquetas de herramientas que nadie va a
confundir no molesta. Entonces cada par se pesa por cuanto se parecen las
HERRAMIENTAS, y lo que se minimiza es

    costo = suma sobre pares de  parecido_de_concepto(i,j) x parecido_de_token(i,j)

Dos herramientas conceptualmente vecinas tienen que tener etiquetas LEJANAS; dos
que nadie confunde pueden tener etiquetas parecidas sin que pase nada.

  tools/entrenar/etiquetas.py --modelo ~/entrenamientos/reactivo-v7
  tools/entrenar/etiquetas.py --modelo ... --proponer > datos/etiquetas.json
"""
import argparse
import json
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))


def dar_vuelta(n):
    p = n.split("_")
    return "_".join(p[1:] + p[:1]) if len(p) > 1 else n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--proponer", action="store_true", help="buscar etiquetas de UN token")
    ap.add_argument("--salida", help="donde guardar la propuesta (.json)")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import lib_prompt as lp

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    por = {h["nombre"]: h for h in inv["herramientas"]}
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
    W = nucleo.lm_head.weight.float()          # [vocabulario, dimension]
    Wn = torch.nn.functional.normalize(W, dim=1)

    # --- vector de cada herramienta: su ficha, promediada ------------------
    print("leyendo las fichas...", file=sys.stderr)
    vecs = []
    for n in nombres:
        h = por.get(n)
        txt = n if h is None else f"{h['titulo']}. {h['descripcion'][:400]}"
        ent = tok(txt, return_tensors="pt", truncation=True, max_length=192).to(dev)
        with torch.no_grad():
            sal = modelo(**ent, output_hidden_states=True)
        vecs.append(sal.hidden_states[-1][0].float().mean(0))
    V = torch.nn.functional.normalize(torch.stack(vecs), dim=1)
    Sc = (V @ V.T).cpu()                        # parecido de CONCEPTO
    Sc.fill_diagonal_(0)
    Sc = Sc.clamp(min=0)

    def parecido_token(etqs):
        """Parecido de token entre cada par, en la posicion donde se separan."""
        ids = [tok(e, add_special_tokens=False).input_ids for e in etqs]
        n = len(etqs)
        St = torch.zeros(n, n)
        for i in range(n):
            for j in range(i + 1, n):
                a, b = ids[i], ids[j]
                k = 0
                while k < min(len(a), len(b)) and a[k] == b[k]:
                    k += 1
                if k >= len(a) or k >= len(b):
                    v = 1.0            # uno es prefijo del otro: lo peor
                else:
                    # Cuantos tokens comparten de arranque castiga igual que el
                    # parecido de los tokens que los separan: las dos cosas
                    # hacen difícil la decision.
                    cos = torch.dot(Wn[a[k]], Wn[b[k]]).item()
                    v = (k / max(len(a), len(b))) + max(0.0, cos)
                St[i, j] = St[j, i] = v
        return St, ids

    def puntuar(etiqueta, etqs):
        St, ids = parecido_token(etqs)
        costo = (Sc * St).sum().item() / 2
        prim = {}
        for e, i in zip(etqs, ids):
            prim.setdefault(i[0], []).append(e)
        tras = sum(len(v) for v in prim.values() if len(v) > 1)
        pasos = []
        for i in ids:
            for k in range(1, len(i) + 1):
                if sum(1 for j in ids if j[:k] == i[:k]) == 1:
                    pasos.append(k)
                    break
            else:
                pasos.append(len(i))
        print(f"{etiqueta:16s} costo {costo:9.1f} · primeros tokens distintos"
              f" {len(prim):3d}/{len(etqs)} · comparten el 1ro {tras:3d}"
              f" · tokens hasta decidir: max {max(pasos)}")
        # los pares que mas cuestan
        peores = []
        for i in range(len(etqs)):
            for j in range(i + 1, len(etqs)):
                peores.append((Sc[i, j].item() * St[i, j].item(), nombres[i], nombres[j]))
        peores.sort(reverse=True)
        for c, a, b in peores[:3]:
            print(f"                   peor par: {a} / {b}  ({c:.2f})")
        return costo

    print()
    puntuar("tal cual", list(nombres))
    puntuar("dado vuelta", [dar_vuelta(n) for n in nombres])
    puntuar("numeros", [str(i + 1) for i in range(len(nombres))])

    if not args.proponer:
        return

    # --- UN TOKEN por herramienta -----------------------------------------
    # Se buscan candidatos en el vocabulario: palabras de verdad, en minusculas,
    # que sean UN token solo. Cada herramienta prefiere el token mas cercano a
    # su ficha, y se asigna con avidez resolviendo los choques por margen: el
    # que mas pierde si no lo consigue, elige primero.
    print("\nbuscando etiquetas de un token...", file=sys.stderr)
    vocab = tok.get_vocab()
    ok = re.compile(r"^[a-záéíóúñ]{3,14}$")
    cand = []
    for s_, i in vocab.items():
        t = s_.replace("Ġ", "").replace("▁", "").lower()
        if ok.match(t) and len(tok(t, add_special_tokens=False).input_ids) == 1:
            cand.append((i, t))
    vistos, limpio = set(), []
    for i, t in cand:
        if t not in vistos:
            vistos.add(t)
            limpio.append((i, t))
    print(f"  {len(limpio)} tokens candidatos", file=sys.stderr)
    idx = torch.tensor([i for i, _ in limpio], device=dev)
    C = Wn[idx]                                  # [candidatos, dimension]
    afinidad = (V @ C.T).cpu()                   # herramienta x candidato

    orden = sorted(range(len(nombres)),
                   key=lambda i: -(afinidad[i].max() - afinidad[i].kthvalue(
                       max(1, afinidad.shape[1] - 5)).values).item())
    tomados, asignado = set(), {}
    for i in orden:
        for j in torch.argsort(afinidad[i], descending=True).tolist():
            if limpio[j][1] not in tomados:
                tomados.add(limpio[j][1])
                asignado[nombres[i]] = limpio[j][1]
                break
    etqs = [asignado[n] for n in nombres]
    print()
    puntuar("un token", etqs)
    print("\nejemplos de la propuesta:")
    for n in nombres[:14]:
        print(f"   {n:24s} -> {asignado[n]}")
    if args.salida:
        Path(args.salida).write_text(json.dumps(asignado, ensure_ascii=False, indent=1),
                                     encoding="utf-8")
        print(f"\n-> {args.salida}")


if __name__ == "__main__":
    main()
