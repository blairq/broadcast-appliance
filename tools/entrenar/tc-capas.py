#!/usr/bin/env python3
"""Las nubes capa por capa: como convergen las frases hacia su herramienta.

DE DONDE SALE. Las nubes de tc-geometria miran solo la ULTIMA capa. Despues de
entrenar, el corpus queda bien separado (v8 vuelta 0: 29 frases fuera de su grupo
de 2.700) pero el banco, que el modelo nunca vio, acierta 74 % de precision. Esto
mira las 33 salidas (la de los embeddings y las 32 capas) en la posicion donde el
modelo va a escribir el nombre, para el corpus y para el banco, y compara:

  - en que capa se separa cada nube del corpus (CDNV, acierto al centroide);
  - en que capa una frase del banco cae en la nube de su herramienta, y si las
    que el modelo erra la tenian en alguna capa y la perdieron, o nunca la tuvieron.

COMO. Todo con el MISMO estado del aparato que usa tc-evaluar (Random(0)), asi la
eleccion del modelo que guardo la evaluacion vale para estos vectores. El banco es
la misma muestra de --limite-banco que midio el bucle; acierto/error sale de los
fallos de ese medidas.json. En cada capa los vectores se estandarizan con la media
y el desvio del corpus (las activaciones masivas de unas pocas dimensiones no
mandan) y se compara por coseno contra los centroides del corpus.

  BA_PERFIL=vivo6 CUDA_VISIBLE_DEVICES=0 tools/entrenar/tc-capas.py \\
      --modelo ~/entrenamientos/tc-v8/vuelta-0/adaptador \\
      --medidas ~/entrenamientos/tc-v8/vuelta-0/medidas.json --salida ~/entrenamientos/capas/v8v0

Deja <salida>/estados.pt (los vectores), <salida>/curvas.json (lo medido, con la
huella de las anotaciones y del banco) y <salida>/vista.json (proyeccion de 16
dimensiones por capa para el grafico 3D). Para guardarlo en el sistema de analisis
y verlo en el navegador: capas-guardar.py y capas-web.py.
"""
import argparse
import base64
import collections
import hashlib
import importlib.util
import json
import random
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def casos(limite_banco, medidas):
    """(corpus, banco): listas de {usuario, frase, destino|aceptables, ...}."""
    rnd = random.Random(0)
    est, formas = tc.estado_sintetico(rnd)  # el mismo que tc-evaluar
    anot = json.loads((RAIZ / "datos" / "tc-anotaciones.json").read_text())
    mias = set(tc.esquemas()) | {tc.DERIVAR}
    corpus, sin_llenar = [], 0
    for h, lista in sorted(anot.items()):
        if h not in mias:
            continue
        for a in lista:
            if "falta" in a or not a.get("plantilla"):
                continue
            r = random.Random(int(hashlib.sha1(f"{h}|{a['plantilla']}".encode()).hexdigest()[:8], 16))
            frase, _ = tc.rellenar(a["plantilla"], a.get("args", {}), formas, r)
            if frase is None:  # el estado de la evaluacion no tiene esa clase de entidad
                sin_llenar += 1
                continue
            corpus.append({"frase": frase, "destino": h, "usuario": tc.usuario(est, frase)})
    conocidas = set(tc.esquemas()) | {tc.DERIVAR}
    banco = [json.loads(l) for l in (RAIZ / "datos/orientacion.jsonl").read_text().splitlines() if l.strip()]
    for x in banco:
        if "aceptables" not in x:
            x["aceptables"] = [x["herramienta"]] if x["herramienta"] in conocidas else [tc.DERIVAR]
        else:
            x["aceptables"] = [a for a in x["aceptables"] if a in conocidas] or [tc.DERIVAR]
    if limite_banco and len(banco) > limite_banco:
        paso = len(banco) / limite_banco
        banco = [banco[int(i * paso)] for i in range(limite_banco)]
    fallos = {}
    if medidas:
        for f in json.loads(Path(medidas).expanduser().read_text())["fallos"].get("banco", []):
            fallos[f["frase"]] = f["elegida"]
    out = []
    for x in banco:
        el = fallos.get(x["frase"])
        out.append({"frase": x["frase"], "aceptables": x["aceptables"], "usuario": tc.usuario(est, x["frase"]),
                    "acerto": el is None, "elegida": el if el is not None else x["aceptables"][0]})
    return corpus, out, sin_llenar


def estados(med, usuarios):
    """[N, capas+1, D] en fp16: la ultima posicion del prefijo forzado («<function=»)."""
    torch = med.t
    filas = []
    for i, u in enumerate(usuarios):
        ids = med.tok(u, add_special_tokens=False).input_ids
        try:
            with torch.no_grad():
                o = med.m.model(input_ids=torch.tensor([ids + med.forzado], device=med.dev),
                                past_key_values=med.kv, use_cache=True, output_hidden_states=True)
        finally:
            med.ch.volver(med.kv, med.foto, med.Lp)
        filas.append(torch.stack([h[0, -1] for h in o.hidden_states]).to(torch.float16).cpu())
        if i % 200 == 0:
            print(f"  {i}/{len(usuarios)}", flush=True)
    return torch.stack(filas)


def analizar(torch, H_c, dest_c, H_b, banco):
    """Curvas por capa. Coseno contra los centroides del corpus, estandarizado."""
    nombres = sorted(set(dest_c))
    idx = {h: i for i, h in enumerate(nombres)}
    yc = torch.tensor([idx[d] for d in dest_c])
    L = H_c.shape[1]
    acept = [[idx[a] for a in x["aceptables"] if a in idx] for x in banco]
    elegida = [idx.get(x["elegida"], -1) for x in banco]
    acerto = torch.tensor([x["acerto"] for x in banco])
    curvas = collections.defaultdict(list)
    por_caso = [[] for _ in banco]  # rango de la correcta por capa
    for l in range(L):
        X = H_c[:, l].float()
        mu, sd = X.mean(0), X.std(0) + 1e-3
        Z = (X - mu) / sd
        Zb = (H_b[:, l].float() - mu) / sd
        C = torch.stack([Z[yc == k].mean(0) for k in range(len(nombres))])
        Cn = torch.nn.functional.normalize(C, dim=1)
        # corpus: CDNV mediana entre pares (varianza dentro / distancia^2 entre centroides)
        var = torch.stack([((Z[yc == k] - C[k]) ** 2).sum(1).mean() for k in range(len(nombres))])
        d2 = torch.cdist(C, C) ** 2
        iu = torch.triu_indices(len(nombres), len(nombres), 1)
        cdnv = ((var[iu[0]] + var[iu[1]]) / (2 * d2[iu[0], iu[1]] + 1e-9))
        curvas["cdnv_mediana"].append(float(cdnv.median()))
        sc = torch.nn.functional.normalize(Z, dim=1) @ Cn.T
        curvas["corpus_al_centroide"].append(float((sc.argmax(1) == yc).float().mean()))
        # banco: la correcta es la nube mas cercana? y el margen
        sb = torch.nn.functional.normalize(Zb, dim=1) @ Cn.T
        top = sb.argmax(1)
        bien = torch.tensor([int(top[i]) in a for i, a in enumerate(acept)])
        rango = []
        for i, a in enumerate(acept):
            s = sb[i]
            mejor_ok = max(float(s[k]) for k in a) if a else -1.0
            rango.append(int((s > mejor_ok).sum()))
            por_caso[i].append(rango[-1])
        curvas["banco_al_centroide"].append(float(bien.float().mean()))
        curvas["banco_al_centroide_acertadas"].append(float(bien[acerto].float().mean()))
        curvas["banco_al_centroide_erradas"].append(float(bien[~acerto].float().mean()) if (~acerto).any() else 0.0)
        # erradas: ¿la nube mas cercana es la que el modelo eligio al final?
        er = [i for i in range(len(banco)) if not banco[i]["acerto"] and elegida[i] >= 0]
        curvas["erradas_cerca_de_la_elegida"].append(
            sum(int(top[i]) == elegida[i] for i in er) / max(1, len(er)))
        # dispersion: banco vs corpus, por herramienta (las de un solo aceptable)
        disp_c = float(var.sqrt().mean())
        grupos = collections.defaultdict(list)
        for i, a in enumerate(acept):
            if len(a) == 1:
                grupos[a[0]].append(i)
        dist_b = []
        for k, ii in grupos.items():
            if len(ii) >= 3:
                zb = Zb[ii]
                dist_b.append(float(((zb - C[k]) ** 2).sum(1).sqrt().mean()))
        curvas["dispersion_corpus"].append(disp_c)
        curvas["banco_distancia_a_su_centroide"].append(sum(dist_b) / max(1, len(dist_b)))
        print(f"capa {l:2d}: corpus {curvas['corpus_al_centroide'][-1]:.2f} · banco {curvas['banco_al_centroide'][-1]:.2f}"
              f" (acertadas {curvas['banco_al_centroide_acertadas'][-1]:.2f}, erradas "
              f"{curvas['banco_al_centroide_erradas'][-1]:.2f}) · cdnv {curvas['cdnv_mediana'][-1]:.4f}", flush=True)
    # las erradas: ¿la tuvieron en alguna capa y la perdieron?
    tipos = collections.Counter()
    detalle = []
    for i, x in enumerate(banco):
        if x["acerto"]:
            continue
        r = por_caso[i]
        primera = next((l for l in range(L) if r[l] == 0), None)
        if r[-1] == 0:
            t = "la nube correcta es la mas cercana al final (pierde en la cabeza)"
        elif primera is None:
            t = "nunca la tuvo"
        else:
            t = "la tuvo y la perdio"
        tipos[t] += 1
        detalle.append({"frase": x["frase"], "aceptables": x["aceptables"], "elegida": x["elegida"],
                        "rango_por_capa": r, "primera_capa_bien": primera, "tipo": t})
    return dict(curvas), dict(tipos), detalle, nombres


def vista(torch, H_c, dest_c, frases_c, H_b, banco, dims=16):
    """Por capa: PCA de 16 dimensiones ajustada en el corpus, cuantizada a int8.
    El grafico hace en el navegador la proyeccion 3D del grupo elegido."""
    L = H_c.shape[1]
    capas = []
    for l in range(L):
        X = H_c[:, l].float()
        mu, sd = X.mean(0), X.std(0) + 1e-3
        Z = (X - mu) / sd
        Zb = (H_b[:, l].float() - mu) / sd
        U, S, V = torch.pca_lowrank(Z, q=dims, center=True)
        m0 = Z.mean(0)
        P = torch.cat([(Z - m0) @ V, (Zb - m0) @ V])
        esc = float(P.abs().max()) / 127
        q = (P / esc).round().clamp(-127, 127).to(torch.int8).numpy().tobytes()
        capas.append({"escala": esc, "q": base64.b64encode(q).decode()})  # [puntos x dims] int8, por filas
    puntos = [{"t": d, "b": 0, "f": f} for d, f in zip(dest_c, frases_c)] + \
             [{"t": x["aceptables"][0], "b": 1, "ok": int(x["acerto"]), "el": x["elegida"], "f": x["frase"]}
              for x in banco]
    return {"dims": dims, "capas": capas, "puntos": puntos}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--medidas", help="medidas.json de la evaluacion del mismo modelo (aciertos y errores del banco)")
    ap.add_argument("--limite-banco", type=int, default=500)
    ap.add_argument("--salida", required=True)
    args = ap.parse_args()
    tc.mismo_perfil(modelo=args.modelo)
    out = Path(args.salida).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    corpus, banco, sin = casos(args.limite_banco, args.medidas)
    print(f"corpus {len(corpus)} frases ({sin} sin llenar con el estado de la evaluacion) · banco {len(banco)}"
          f" ({sum(not x['acerto'] for x in banco)} erradas)", flush=True)
    geo = _modulo("tc_geometria", "tc-geometria.py")
    import torch
    f = out / "estados.pt"
    if f.exists():
        d = torch.load(f)
        H_c, H_b = d["corpus"], d["banco"]
    else:
        med = geo.Medidor(args.modelo)
        H_c = estados(med, [x["usuario"] for x in corpus])
        H_b = estados(med, [x["usuario"] for x in banco])
        torch.save({"corpus": H_c, "banco": H_b}, f)
        med.cerrar()
    dest_c = [x["destino"] for x in corpus]
    curvas, tipos, detalle, nombres = analizar(torch, H_c, dest_c, H_b, banco)
    huella = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()[:16]  # como tc-corpus (huella.json)
    (out / "curvas.json").write_text(json.dumps({"modelo": args.modelo, "limite_banco": args.limite_banco,
                                                 "anotaciones": huella(RAIZ / "datos" / "tc-anotaciones.json"),
                                                 "banco": huella(RAIZ / "datos" / "orientacion.jsonl"),
                                                 "curvas": curvas, "erradas": tipos,
                                                 "detalle": detalle}, ensure_ascii=False, indent=1))
    (out / "vista.json").write_text(json.dumps(vista(torch, H_c, dest_c, [x["frase"] for x in corpus], H_b, banco), ensure_ascii=False,
                                               separators=(",", ":")))
    print(json.dumps(tipos, ensure_ascii=False))


if __name__ == "__main__":
    main()
