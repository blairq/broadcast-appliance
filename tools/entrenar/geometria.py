#!/usr/bin/env python3
"""Que termino de la decision se puede tocar sin reentrenar, y cuanto rinde.

LA ECUACION. El modelo elige `argmax de h . W_etiqueta`, asi que se equivoca
cuando h . (W_correcta - W_elegida) < 0. Llamando d a esa diferencia:

    margen = h . d = |h| . |d| . cos(h, d)

    |h|        escala del flujo residual. Multiplica todos los pares por igual:
               NO cambia quien gana. Se cancela.
    |d|        cuan separadas estan las dos etiquetas. GRATIS: se cambia
               eligiendo otros tokens, sin reentrenar.
    cos(h, d)  si el estado que el modelo produce tiene componente en esa
               direccion. Esto SOLO lo mueve el entrenamiento.

QUE MIDE ESTE SCRIPT, y es lo que decide si conviene invertir en las etiquetas:

1. CUANTO MANDA CADA TERMINO. Sobre los casos del examen, descompone el margen
   real en |d| y cos(h,d) y mira cual de los dos explica la varianza. Si cos
   manda por dos ordenes de magnitud, elegir tokens no va a salvar nada y hay
   que entrenar; si |d| pesa, las etiquetas son la palanca barata.

2. LA ALINEACION GEOMETRICA, que es el criterio que reemplaza a «lo mas lejos
   posible». No conviene separar las etiquetas al maximo: conviene que la
   geometria de las etiquetas COPIE la de los conceptos —los pares lejanos
   lejanos, los cercanos cercanos y en la misma direccion— porque entonces la
   funcion que el modelo tiene que aprender se parece a la identidad.

   Se mide como la correlacion entre la matriz de distancias de los conceptos
   (las fichas) y la de las etiquetas. Es una prueba tipo Mantel, cuesta
   segundos y no necesita entrenar nada.

   Por eso los NUMEROS midieron peor que todo: son la geometria mas separada
   que existe y no tiene ninguna relacion con los conceptos.

  tools/entrenar/geometria.py --modelo ~/entrenamientos/reactivo-v7 \\
      --esquema datos/etiquetas.json
"""
import argparse
import json
import statistics
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def dar_vuelta(n):
    p = n.split("_")
    return "_".join(p[1:] + p[:1]) if len(p) > 1 else n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--esquema", help="json {herramienta: etiqueta} a evaluar tambien")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import lib_prompt as lp

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    por = {h["nombre"]: h for h in inv["herramientas"]}
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

    # --- vectores de concepto: la ficha de cada herramienta ----------------
    vecs = []
    for n in nombres:
        h = por.get(n)
        txt = n if h is None else f"{h['titulo']}. {h['descripcion'][:400]}"
        ent = tok(txt, return_tensors="pt", truncation=True, max_length=192).to(dev)
        with torch.no_grad():
            sal = modelo(**ent, output_hidden_states=True)
        vecs.append(sal.hidden_states[-1][0].float().mean(0))
    V = torch.nn.functional.normalize(torch.stack(vecs), dim=1)
    Cc = (V @ V.T).cpu()                                   # conceptos

    def alineacion(etqs):
        """Correlacion entre la geometria de los conceptos y la de las etiquetas."""
        ids = [tok(e, add_special_tokens=False).input_ids[0] for e in etqs]
        L = torch.nn.functional.normalize(W[ids].cpu(), dim=1)
        Cl = L @ L.T
        n = len(etqs)
        a, b = [], []
        for i in range(n):
            for j in range(i + 1, n):
                a.append(Cc[i, j].item())
                b.append(Cl[i, j].item())
        ma, mb = statistics.mean(a), statistics.mean(b)
        sa = sum((x - ma) ** 2 for x in a) ** 0.5
        sb = sum((x - mb) ** 2 for x in b) ** 0.5
        r = sum((x - ma) * (y - mb) for x, y in zip(a, b)) / max(1e-9, sa * sb)
        return r, statistics.mean(b), min(b), max(b)

    print("ALINEACION GEOMETRICA (conceptos contra etiquetas)")
    print("  r alto = la geometria de las etiquetas copia la de los conceptos,")
    print("  que es lo que vuelve el mapeo parecido a la identidad.\n")
    esquemas = [("tal cual", list(nombres)),
                ("dado vuelta", [dar_vuelta(n) for n in nombres]),
                ("numeros", [str(i + 1) for i in range(len(nombres))])]
    if args.esquema:
        d = json.loads(Path(args.esquema).read_text(encoding="utf-8"))
        d = d.get("etiquetas", d)
        esquemas.append(("a mano", [d[n] for n in nombres]))
    for etq, e in esquemas:
        r, med, mn, mx = alineacion(e)
        print(f"  {etq:14s} r = {r:+.3f}   coseno entre etiquetas:"
              f" medio {med:+.3f} · min {mn:+.3f} · max {mx:+.3f}")

    # --- cuanto manda cada termino, sobre el examen ------------------------
    print("\nQUE TERMINO MANDA (sobre los casos del examen, esquema actual)")
    secuencias = {n: tok(n, add_special_tokens=False).input_ids for n in nombres}
    pre = {}
    if norma is not None:
        norma.register_forward_pre_hook(lambda _m, e: pre.__setitem__("x", e[0].detach()))
    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    filas = []
    for c in casos:
        ent = tok(cabeza + c["frase"] + cola, return_tensors="pt").to(dev)
        with torch.no_grad():
            sal = modelo(**ent)
        lg = sal.logits[0, -1].float()
        cand = sorted(((lg[secuencias[n][0]].item(), n) for n in nombres), reverse=True)
        (s1, n1), (s2, n2) = cand[0], cand[1]
        if secuencias[n1][0] == secuencias[n2][0]:
            continue                      # comparten el primer token: no compiten acá
        x = pre["x"][0, -1, :].float()
        rms = (x.pow(2).mean().sqrt() + 1e-6)
        peso = norma.weight.float() if hasattr(norma, "weight") else torch.ones_like(x)
        hh = (x / rms) * peso
        d = W[secuencias[n1][0]] - W[secuencias[n2][0]]
        margen = torch.dot(hh, d).item()
        nd = d.norm().item()
        cosv = margen / max(1e-9, hh.norm().item() * nd)
        filas.append((margen, nd, cosv))
    mg = [f[0] for f in filas]
    nd = [f[1] for f in filas]
    cs = [f[2] for f in filas]
    def cv(v):
        return statistics.pstdev(v) / max(1e-9, abs(statistics.mean(v)))
    print(f"  {len(filas)} casos donde los dos primeros compiten de verdad")
    print(f"  |d|        medio {statistics.mean(nd):8.2f} · variacion relativa {cv(nd):.2f}")
    print(f"  cos(h,d)   medio {statistics.mean(cs):8.4f} · variacion relativa {cv(cs):.2f}")
    print(f"  margen     medio {statistics.mean(mg):8.2f}")
    print("\n  Lo que se puede tocar sin reentrenar es |d|. Su variacion relativa")
    print("  contra la de cos(h,d) dice cuanto de la suerte del margen esta en")
    print("  la eleccion de tokens y cuanto en el entrenamiento.")
    if cv(nd) < cv(cs) / 3:
        print("  -> cos manda por lejos: elegir tokens da poco, hay que entrenar.")
    elif cv(nd) > cv(cs):
        print("  -> |d| manda: las etiquetas son la palanca barata y conviene usarla.")
    else:
        print("  -> pesan parecido: las etiquetas ayudan pero no alcanzan solas.")


if __name__ == "__main__":
    main()
