#!/usr/bin/env python3
"""Etiquetas que el modelo ya conoce: PA-Tool (arxiv 2510.07248).

POR QUE. v11 entrenó con etiquetas elegidas por geometría (`ource`, `heets`) y
perdió mucho contra los nombres reales: la etiqueta no traía ningún significado
del preentrenamiento. PA-Tool elige al revés: le pide al PROPIO modelo base que
nombre la herramienta a partir de su descripción, muchas veces, y se queda con
el nombre más «picudo» — el que tiene más candidatos parecidos alrededor
(distancia de edición <= alfa * largo máximo). Ese pico es la forma de nombrar
que el modelo internalizó; en el paper sube la elección de herramienta hasta 17%.

REGLAS NUESTRAS encima de PA-Tool:
  - una etiqueta no puede ser igual al nombre de ninguna OTRA herramienta del MCP
    (las ~290), ni a otra etiqueta elegida;
  - en un choque, gana la herramienta con el pico más alto y la otra toma su
    siguiente candidato (el «bloqueo por prioridad» del paper).
  - `derivar` no se renombra.

    tools/entrenar/etiquetas-familiares.py --herramientas todas.txt \\
        --descripciones lotes/*.txt --salida datos/etiquetas.json
Escribe el juego «familiares» dentro del archivo, sin tocar los otros.
"""
import argparse
import glob
import json
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))

PEDIDO = ("Tenés que ponerle nombre a una función de una API para operar un "
          "estudio de televisión. Esto es lo que hace:\n\n{desc}\n\n"
          "Respondé SOLO el nombre de la función, en snake_case, sin nada más.")


def lev(a, b):
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def picos(cands, alfa):
    """Candidatos distintos ordenados por peakedness (y por frecuencia)."""
    tau = alfa * max(len(c) for c in cands)
    unicos = sorted(set(cands))
    pk = {u: sum(1 for c in cands if lev(u, c) <= tau) - 1 for u in unicos}
    return sorted(unicos, key=lambda u: (-pk[u], -cands.count(u), u)), pk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="unsloth/Qwen3-1.7B")
    ap.add_argument("--herramientas", required=True, help="todos los nombres del MCP")
    ap.add_argument("--descripciones", nargs="+", required=True,
                    help="archivos con lineas «- nombre — titulo. descripcion»")
    ap.add_argument("--salida", default=str(RAIZ / "datos/etiquetas.json"))
    ap.add_argument("--muestras", type=int, default=24)
    ap.add_argument("--temperatura", type=float, default=0.8)
    ap.add_argument("--alfa", type=float, default=0.3)
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    import lib_prompt as lp

    propias = sorted(lp.herramientas_reactivas())
    todas = {l.strip() for l in Path(args.herramientas).read_text().splitlines() if l.strip()}
    desc = {}
    for patron in args.descripciones:
        for f in glob.glob(patron):
            for l in Path(f).read_text(encoding="utf-8").splitlines():
                m = re.match(r"- (\w+) — (.*)", l)
                if m:
                    desc.setdefault(m.group(1), m.group(2))
    falta = [h for h in propias if h not in desc]
    if falta:
        sys.exit(f"sin descripcion: {falta}")

    tok = AutoTokenizer.from_pretrained(args.base)
    mod = AutoModelForCausalLM.from_pretrained(args.base, torch_dtype=torch.bfloat16).cuda().eval()
    torch.manual_seed(0)
    ranking = {}
    for h in propias:
        msgs = [{"role": "user", "content": PEDIDO.format(desc=desc[h])}]
        txt = tok.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True,
                                      enable_thinking=False)
        ids = tok(txt, return_tensors="pt").to("cuda")
        with torch.no_grad():
            out = mod.generate(**ids, do_sample=True, temperature=args.temperatura,
                               max_new_tokens=16, num_return_sequences=args.muestras,
                               pad_token_id=tok.eos_token_id)
        cands = []
        for o in out:
            s = tok.decode(o[ids.input_ids.shape[1]:], skip_special_tokens=True).strip()
            s = re.sub(r"[^a-z0-9_]", "", s.split()[0].lower()) if s else ""
            if s:
                cands.append(s)
        orden, pk = picos(cands, args.alfa)
        ranking[h] = [(c, pk[c]) for c in orden]
        print(f"  {h:22} -> {orden[0]:28} pico {pk[orden[0]]}/{len(cands) - 1}"
              f"   ({', '.join(orden[1:4])})")

    # Bloqueo por prioridad: el pico más alto elige primero.
    elegidas, usadas = {}, set()
    for h in sorted(propias, key=lambda h: -ranking[h][0][1]):
        for c, _ in ranking[h]:
            if c == h or (c not in todas and c not in usadas):
                elegidas[h] = c
                usadas.add(c)
                break
        else:
            elegidas[h] = h  # sin candidato libre: queda el nombre real
    elegidas["derivar"] = lp.DERIVAR
    cambian = sum(1 for h in propias if elegidas[h] != h)
    print(f"\n{cambian} de {len(propias)} cambian de nombre")

    f = Path(args.salida)
    d = json.loads(f.read_text(encoding="utf-8"))
    d["familiares"] = elegidas
    f.write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"-> juego «familiares» en {f}")


if __name__ == "__main__":
    main()
