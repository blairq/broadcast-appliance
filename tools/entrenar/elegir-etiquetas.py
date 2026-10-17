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
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
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

    # LO QUE NO SE PUEDE USAR. Las etiquetas van a reemplazar los nombres reales
    # del MCP, asi que no alcanza con que no se repitan entre las 86: no pueden
    # chocar con ninguno de los ~206 nombres restantes. Dos niveles:
    #
    #   · exacto: una etiqueta igual al nombre de otra herramienta rompe el
    #     registro del MCP el dia que se renombre (paso: remotos_fila_ver -> status,
    #     y `status` ya es una herramienta).
    #   · primer token: si esto anda y se extiende a las 292, una etiqueta que
    #     comparte primer token con un nombre del resto habria que cambiarla de
    #     nuevo. Se prohibe desde ahora para no tocarlas dos veces.
    subset = set(nombres)
    resto = {h["nombre"] for h in inv["herramientas"]} - subset
    todos_los_nombres = {h["nombre"] for h in inv["herramientas"]}
    prohibidos_tok = {tok(o, add_special_tokens=False).input_ids[0] for o in resto}
    prohibidas_pal = set(todos_los_nombres)
    print(f"prohibidos: {len(prohibidos_tok)} primeros tokens del resto del MCP"
          f" y {len(prohibidas_pal)} nombres exactos", file=sys.stderr)
    cand = {p: t for p, t in cand.items()
            if t not in prohibidos_tok and p not in prohibidas_pal}

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

    # TODOS LOS CANDIDATOS DE UN SAQUE, y no de a uno. La primera version probaba
    # candidato por candidato: 86 herramientas x 16.000 palabras x 3 vueltas son
    # cuatro millones de evaluaciones y una hora de reloj. Pero cambiar la
    # etiqueta de UNA herramienta solo afecta a los pares donde esa herramienta
    # aparece, y el coseno se abre en dos productos que son matmuls:
    #
    #   cos(w, C - O) = (w.C - w.O) / |C - O|
    #   |C - O|^2     = |C|^2 + |O|^2 - 2 C.O
    #
    # Con eso los 16.000 candidatos salen en dos multiplicaciones de matrices.
    lista_cand = sorted(cand)
    tid_cand = torch.tensor([cand[p] for p in lista_cand])
    C = Wn[tid_cand]                                  # [cand, dim] normalizadas
    C2 = (C * C).sum(1)                               # |C|^2 = 1, pero explicito
    mejor = dict(actual)
    # Las que arrancan prohibidas se cambian SI O SI: no se comparan contra su
    # valor actual, que no es una opcion.
    forzadas = {h for h in hs
                if primer(actual[h]) in prohibidos_tok or actual[h] in prohibidas_pal}
    if forzadas:
        print(f"a cambiar si o si ({len(forzadas)}): "
              + ", ".join(f"{h}={actual[h]}" for h in sorted(forzadas)), file=sys.stderr)
    usados = {primer(v) for h, v in actual.items() if h not in forzadas} | prohibidos_tok
    # Indice de los pares que toca cada herramienta.
    toca = collections.defaultdict(list)
    for k in range(len(pares)):
        toca[int(ia[k])].append((k, +1))
        toca[int(jb[k])].append((k, -1))

    def aporte_actual(asig, ks):
        L = torch.stack([Wn[primer(asig[h])] for h in hs])
        d = L[ia[ks]] - L[jb[ks]]
        d = torch.nn.functional.normalize(d, dim=1)
        return float((pesos[ks] * (Wp[ks] * d).sum(1)).sum())

    mejor_val = base
    for vuelta in range(args.vueltas):
        cambios = 0
        for h in hs:
            mios = toca[ix[h]]
            if not mios:
                continue
            ks = torch.tensor([k for k, _ in mios])
            signos = torch.tensor([sg for _, sg in mios], dtype=torch.float32)
            # El OTRO de cada par, y la direccion w con su signo.
            otros = torch.stack([Wn[primer(mejor[hs[int(jb[k])] if sg > 0 else hs[int(ia[k])]])]
                                 for k, sg in mios])
            Wk = Wp[ks] * signos[:, None]      # w orientada hacia esta herramienta
            wc = Wk @ C.T                      # [pares_h, cand]
            wo = (Wk * otros).sum(1, keepdim=True)
            co = C @ otros.T                   # [cand, pares_h]
            dist2 = (C2[:, None] + (otros * otros).sum(1)[None, :] - 2 * co).clamp(min=1e-9)
            cos = (wc.T - wo.T) / dist2.sqrt()  # [cand, pares_h]
            val = (cos * pesos[ks][None, :]).sum(1)
            # No se puede usar un primer token que ya tenga otra herramienta.
            act = primer(mejor[h])
            libre = torch.tensor([(t == act and h not in forzadas) or (t not in usados)
                                  for t in tid_cand.tolist()])
            val[~libre] = float("-inf")
            j = int(val.argmax())
            viejo = float("-inf") if h in forzadas else aporte_actual(mejor, ks)
            if float(val[j]) > viejo + 1e-6:
                if h not in forzadas:
                    usados.discard(act)
                usados.add(int(tid_cand[j]))
                mejor[h] = lista_cand[j]
                forzadas.discard(h)
                cambios += 1
        mejor_val = objetivo(mejor)
        print(f"  vuelta {vuelta+1}: objetivo {mejor_val:.3f} · {cambios} cambios")
        if cambios == 0:
            break

    # LAS FORZADAS QUE SOBREVIVIERON. El bucle de arriba saltea las herramientas
    # que no tienen ningun par entre los que entran al objetivo, y si una de esas
    # arrancaba prohibida se quedaba con su etiqueta prohibida: paso con
    # invitados_chat_leer -> chat, que choca con chat_overlay_add. A esas se les da el
    # candidato libre mas cercano a su propia nube, que es lo mejor que se puede
    # hacer sin un par que optimizar.
    for h in sorted(forzadas):
        cen = torch.nn.functional.normalize(nubes[h].mean(0), dim=0) if h in nubes else None
        mejor_j, mejor_v = None, float("-inf")
        for j, t in enumerate(tid_cand.tolist()):
            if t in usados:
                continue
            v = float(torch.dot(C[j], cen)) if cen is not None else 0.0
            if v > mejor_v:
                mejor_j, mejor_v = j, v
        if mejor_j is not None:
            print(f"  forzada sin par en el objetivo: {h} {mejor[h]!r} -> "
                  f"{lista_cand[mejor_j]!r}", file=sys.stderr)
            usados.add(int(tid_cand[mejor_j]))
            mejor[h] = lista_cand[mejor_j]
    forzadas.clear()

    sin_nube = [h for h in nombres if h not in ix]
    for h in sin_nube:
        if h in d0 and (primer(d0[h]) in prohibidos_tok or d0[h] in prohibidas_pal):
            print(f"  OJO: {h} no tiene nube y su etiqueta {d0[h]!r} esta prohibida",
                  file=sys.stderr)

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
