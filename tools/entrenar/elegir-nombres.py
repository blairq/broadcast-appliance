#!/usr/bin/env python3
"""Elige el nombre de cada herramienta: FAMILIAR para el modelo y DISTINTO de sus vecinas.

POR QUE, en tres mediciones:
  · v11 (etiquetas elegidas por geometria, `ource`, `heets`) perdio mucho contra
    los nombres reales: la etiqueta no traia significado del preentrenamiento.
  · PA-Tool (arxiv 2510.07248): en modelos chicos, adaptar los nombres al modelo
    y no al reves; el nombre que el modelo «ya conoce» rinde mas. Su forma de
    medirlo (muestrear al modelo y buscar el pico) con 1.7B da ecos de la
    consigna, asi que aca los CANDIDATOS los propone otro (un agente) y el
    modelo que se va a entrenar solo los PUNTUA.
  · ToolChoiceConfusion / ToolSearcher (2606.06284, 2609.30906): elegir entre
    menos opciones confunde menos. Con nombres `familia_accion` y decodificacion
    token a token, el primer token elige la familia y el resto la herramienta:
    es la eleccion jerarquica gratis. Entonces lo que tiene que ser distinto es la
    ACCION entre herramientas que se pisan.

QUE MIDE, por candidato:
  familiaridad  log-prob media por token del nombre, dado el pedido «nombrá la
                funcion que hace: <descripcion>», con el modelo BASE. Es cuanto
                le cuesta al modelo producir ese nombre para esa descripcion.
  parecido      entre dos herramientas que se pisan: coseno entre las filas de
                la cabeza de salida (W) de los tokens de sus acciones, promedio.
                La decision es argmax h·W: dos acciones con filas parecidas se
                leen parecido. Si ademas el primer token de la accion es el mismo,
                el decodificador no decide ahi: se suma 1.

    objetivo = sum familiaridad  -  lambda * sum_{pares} max(0, parecido)

Busqueda local por coordenadas desde el mejor candidato de cada una. Reglas
duras: ningun nombre igual a otra herramienta del MCP ni a otro elegido.

    tools/entrenar/elegir-nombres.py --candidatos nombres-candidatos.json \\
        --herramientas todas.txt --descripciones 'lotes/*.txt' \\
        --pares choques-8.json [--lambda 2]
Escribe el juego «familias» en datos/etiquetas.json (se usa con
BA_ETIQUETAS=1 BA_ETIQUETAS_JUEGO=familias).
"""
import argparse
import glob
import json
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))

PEDIDO = ("Esta es una funcion de la API de un estudio de television en vivo:\n\n"
          "{desc}\n\nComo se llama la funcion? Responde solo el nombre.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="unsloth/Qwen3-1.7B")
    ap.add_argument("--candidatos", required=True)
    ap.add_argument("--herramientas", required=True)
    ap.add_argument("--descripciones", required=True)
    ap.add_argument("--pares", nargs="+", required=True,
                    help="salidas de bucle-corpus.py --choques")
    # Escala: la familiaridad ronda -2 a -20 por herramienta y el parecido de dos
    # acciones 0 a 0,3 (mas 1 si arrancan con el mismo token). Con lambda chico
    # manda solo la familiaridad; 20 hace que un par con el mismo primer token de
    # accion cueste lo mismo que un nombre bastante menos familiar.
    ap.add_argument("--lambda", dest="lam", type=float, default=20.0)
    ap.add_argument("--salida", default=str(RAIZ / "datos/etiquetas.json"))
    ap.add_argument("--juego", default="familias")
    args = ap.parse_args()

    import torch
    import torch.nn.functional as F
    from transformers import AutoModelForCausalLM, AutoTokenizer
    import lib_prompt as lp

    propias = sorted(lp.herramientas_reactivas())
    todas = {l.strip() for l in Path(args.herramientas).read_text().splitlines() if l.strip()}
    ajenas = todas - set(propias)
    cands = json.loads(Path(args.candidatos).read_text(encoding="utf-8"))
    desc = {}
    for f in glob.glob(args.descripciones):
        for l in Path(f).read_text(encoding="utf-8").splitlines():
            m = re.match(r"- (\w+) — (.*)", l)
            if m:
                desc.setdefault(m.group(1), m.group(2))
    pares = set()
    for f in args.pares:
        for p in json.loads(Path(f).read_text(encoding="utf-8"))["pares"]:
            if p["a"] in propias and p["b"] in propias:
                pares.add(tuple(sorted((p["a"], p["b"]))))
    # Cada herramienta compite tambien con el nombre real (linea de base) y los
    # candidatos que chocan con el resto del MCP se descartan de entrada.
    for h in propias:
        lista = [c for c in cands.get(h, []) if re.fullmatch(r"[a-z][a-z0-9_]*", c)]
        cands[h] = [c for c in dict.fromkeys(lista + [h]) if c == h or c not in todas]

    tok = AutoTokenizer.from_pretrained(args.base)
    mod = AutoModelForCausalLM.from_pretrained(args.base, torch_dtype=torch.bfloat16).cuda().eval()
    W = F.normalize(mod.get_output_embeddings().weight.float(), dim=1)

    fam = {}
    for h in propias:
        msgs = [{"role": "user", "content": PEDIDO.format(desc=desc[h])}]
        pre = tok.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True,
                                      enable_thinking=False)
        n_pre = len(tok(pre).input_ids)
        textos = [pre + c for c in cands[h]]
        enc = tok(textos, return_tensors="pt", padding=True, padding_side="right").to("cuda")
        with torch.no_grad():
            lg = mod(**enc).logits.float().log_softmax(-1)
        for i, c in enumerate(cands[h]):
            ids = enc.input_ids[i]
            n = int(enc.attention_mask[i].sum())
            lp_ = lg[i, n_pre - 1:n - 1].gather(1, ids[n_pre:n, None]).squeeze(1)
            fam[(h, c)] = lp_.mean().item()

    def accion(c):
        partes = c.split("_")
        return "_".join(partes[1:]) if len(partes) > 1 else c

    cache = {}

    def parecido(a, b):
        k = (a, b) if a < b else (b, a)
        if k not in cache:
            ta = tok("_" + accion(a), add_special_tokens=False).input_ids
            tb = tok("_" + accion(b), add_special_tokens=False).input_ids
            s = (W[ta].mean(0) @ W[tb].mean(0)).item()
            if ta[:1] == tb[:1]:
                s += 1.0
            cache[k] = s
        return cache[k]

    vecinas = {h: [b for a, b in pares if a == h] + [a for a, b in pares if b == h]
               for h in propias}

    def objetivo(el):
        f = sum(fam[(h, el[h])] for h in propias)
        p = sum(max(0.0, parecido(el[a], el[b])) for a, b in pares)
        return f - args.lam * p, f, p

    # FAMILIAS. El prefijo tiene que ser el MISMO para toda la familia (la del
    # nombre real: laminas_*, source_*, speaker(s)_*...), porque es el que hace
    # de eleccion jerarquica. Se prueba cada prefijo propuesto para la familia y
    # gana el de mayor familiaridad total; cada herramienta queda restringida a
    # sus candidatos con ese prefijo (el nombre real cuenta si el prefijo es el real).
    def familia(h):
        return h.split("_")[0].rstrip("s") if "_" in h else h

    grupos = {}
    for h in propias:
        grupos.setdefault(familia(h), []).append(h)
    for g, hs in grupos.items():
        if len(hs) < 2:
            continue
        prefijos = {c.split("_")[0] for h in hs for c in cands[h] if "_" in c}
        def con(h, p):
            return [c for c in cands[h] if c.startswith(p + "_")]
        viables = [p for p in prefijos if all(con(h, p) for h in hs)]
        if not viables:
            continue
        mejor = max(viables, key=lambda p: sum(max(fam[(h, c)] for c in con(h, p)) for h in hs))
        for h in hs:
            cands[h] = con(h, mejor)
        print(f"  familia {g:12} -> {mejor}_  ({len(hs)} herramientas)")

    el = {h: max(cands[h], key=lambda c: fam[(h, c)]) for h in propias}
    # desempate de duplicados: se queda el de mayor familiaridad
    for h in sorted(propias, key=lambda h: -fam[(h, el[h])]):
        usados = {el[o] for o in propias if o != h}
        if el[h] in usados:
            libres = [c for c in cands[h] if c not in usados]
            el[h] = max(libres, key=lambda c: fam[(h, c)]) if libres else h
    real = {h: h for h in propias}
    o0 = objetivo(real)
    print(f"nombres reales:  objetivo {o0[0]:.2f} (familiaridad {o0[1]:.2f}, parecido {o0[2]:.2f})")
    print(f"mas familiares:  objetivo {objetivo(el)[0]:.2f}")
    for vuelta in range(5):
        cambios = 0
        for h in propias:
            usados = {el[o] for o in propias if o != h}
            def local(c):
                return fam[(h, c)] - args.lam * sum(max(0.0, parecido(c, el[v])) for v in vecinas[h])
            mejor = max((c for c in cands[h] if c not in usados), key=local)
            if mejor != el[h]:
                el[h], cambios = mejor, cambios + 1
        o = objetivo(el)
        print(f"  vuelta {vuelta + 1}: objetivo {o[0]:.2f} (familiaridad {o[1]:.2f},"
              f" parecido {o[2]:.2f}) · {cambios} cambios")
        if not cambios:
            break

    print("\nPARES QUE SE PISAN (parecido de la accion, real -> elegido):")
    for a, b in sorted(pares, key=lambda p: -parecido(p[0], p[1])):
        print(f"  {a:20} {b:20} {parecido(a, b):5.2f} -> {parecido(el[a], el[b]):5.2f}"
              f"   {el[a]} / {el[b]}")
    print("\nNOMBRES:")
    for h in propias:
        marca = "" if el[h] != h else "   (queda el real)"
        print(f"  {h:22} -> {el[h]:28} fam {fam[(h, el[h])]:6.2f} (real {fam[(h, h)]:6.2f}){marca}")
    assert len(set(el.values())) == len(el) and not (set(el.values()) - set(propias)) & ajenas
    el["derivar"] = lp.DERIVAR
    f = Path(args.salida)
    d = json.loads(f.read_text(encoding="utf-8"))
    d[args.juego] = el
    f.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"\n-> juego «{args.juego}» en {f}")


if __name__ == "__main__":
    main()
