#!/usr/bin/env python3
"""Cuanta VARIEDAD tiene el corpus de cada herramienta, y donde le falta.

EL DIAGNOSTICO QUE ESTO ATACA. Medido en nubes.py: las nubes de entrenamiento
estan separadisimas —fisher medio 21, solapamiento 0,0%— pero el 41% de los casos
del examen cae del lado ajeno. Eso no es falta de separacion: es EXCESO DE
CONCENTRACION. Cada herramienta aprendio un punado de frases casi iguales, y
cualquier forma de pedir que se salga de esa linea aterriza afuera.

EL PUNTO MEDIO, que tiene forma exacta: la razon de Fisher es varianza ENTRE
clases sobre varianza DENTRO de cada clase. Separar mas sube el numerador;
engordar cada nube baja el denominador... y es justo lo que hace falta, porque el
denominador chico es memoria y no aprendizaje. Lo que se busca es la nube mas
GORDA que todavia no toque a su vecina.

QUE INFORMA, por herramienta:

  radio          distancia media de sus frases al centro. Nube flaca = todas
                 las frases dicen lo mismo con otras letras.
  dimension      dimension efectiva de la nube (razon de participacion de los
                 autovalores). 1,5 significa que toda la variedad del corpus para
                 esa herramienta cabe en una linea: hay UNA forma de pedirlo y
                 variaciones de superficie.
  hueco          cuanto sobra entre el borde de esta nube y el de su vecina mas
                 cercana. Hueco grande + nube flaca = lugar para crecer sin
                 riesgo: ahi hay que escribir frases nuevas.
  afuera         casos del examen de esta herramienta que caen mas cerca del
                 centro de OTRA. Es la cuenta de lo que ya se esta perdiendo.

Y la lista de FRASES RARAS: las que estan lejos del centro de su propia
herramienta. Son de dos tipos y hay que leerlas para saber cual: una semilla mal
escrita (se corrige) o la unica frase que cubre una forma legitima de pedirlo (no
se toca, se le suman hermanas).

Los estados ocultos se guardan en cache: calcularlos son 30 segundos y los usan
varias herramientas.

  tools/entrenar/variedad.py --modelo ~/entrenamientos/reactivo-v7
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


def estados_con_cache(args, frases, etiqueta):
    """h de cada frase, cacheado en disco: son 30 s y los piden varios scripts."""
    import torch
    cache_f = Path(args.cache) / f"{etiqueta}.pt"
    firma = str(len(frases)) + "|" + (frases[0] if frases else "")
    if cache_f.exists():
        d = torch.load(cache_f)
        if d.get("firma") == firma:
            return d["H"]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--cache", default="/tmp/nubes-cache")
    ap.add_argument("--minimo", type=int, default=8)
    ap.add_argument("--cuantas", type=int, default=18, help="cuantas herramientas listar")
    ap.add_argument("--raras", type=int, default=25, help="cuantas frases raras listar")
    ap.add_argument("--json")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import lib_prompt as lp

    Path(args.cache).mkdir(parents=True, exist_ok=True)
    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv)
    tr = [json.loads(l) for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip()]
    ex = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    Htr = estados_con_cache(args, [f["frase"] for f in tr], "entrenar")
    Hex = estados_con_cache(args, [c["frase"] for c in ex], "examen")

    if Htr is None or Hex is None:
        cfg = json.loads((Path(args.modelo) / "adapter_config.json").read_text())
        tok = AutoTokenizer.from_pretrained(args.modelo)
        modelo = AutoModelForCausalLM.from_pretrained(
            cfg["base_model_name_or_path"], dtype=torch.bfloat16, device_map="cuda:0")
        modelo = PeftModel.from_pretrained(modelo, args.modelo).merge_and_unload()
        modelo.eval()
        norma = modelo.model.norm
        peso_n = norma.weight.float()
        pre = {}
        norma.register_forward_pre_hook(lambda _m, e: pre.__setitem__("x", e[0].detach()))
        e = tok(cabeza, return_tensors="pt").to(modelo.device)
        with torch.no_grad():
            sal = modelo(**e, use_cache=True)
        cache, n_cab = sal.past_key_values, e.input_ids.shape[1]

        def estados(frases):
            out = []
            for fr in frases:
                r = tok(fr + cola, add_special_tokens=False, return_tensors="pt").to(modelo.device)
                with torch.no_grad():
                    modelo(input_ids=r.input_ids, past_key_values=cache, use_cache=True)
                cache.crop(n_cab)
                x = pre["x"][0, -1, :].float()
                out.append(((x / (x.pow(2).mean().sqrt() + 1e-6)) * peso_n).cpu())
            return torch.stack(out)

        for etq, frases in (("entrenar", [f["frase"] for f in tr]),
                            ("examen", [c["frase"] for c in ex])):
            print(f"calculando estados de {etq} ({len(frases)})...", file=sys.stderr)
            H = estados(frases)
            torch.save({"firma": f"{len(frases)}|{frases[0]}", "H": H},
                       Path(args.cache) / f"{etq}.pt")
        Htr = estados_con_cache(args, [f["frase"] for f in tr], "entrenar")
        Hex = estados_con_cache(args, [c["frase"] for c in ex], "examen")

    idx = collections.defaultdict(list)
    for k, f in enumerate(tr):
        idx[f["herramienta"]].append(k)
    usables = {h: ks for h, ks in idx.items() if len(ks) >= args.minimo}
    cen = {h: Htr[ks].mean(0) for h, ks in usables.items()}
    hs = sorted(usables)

    info = {}
    for h in hs:
        P = Htr[usables[h]]
        c = cen[h]
        dif = P - c
        radio = dif.norm(dim=1).mean().item()
        # Dimension efectiva: razon de participacion de los autovalores de la
        # covarianza. Dice en cuantas direcciones INDEPENDIENTES varia el corpus.
        # Si da 1,5 hay una sola forma de pedirlo y el resto es superficie.
        S = torch.linalg.svdvals(dif.float())
        lam = (S ** 2)
        dim_ef = (lam.sum() ** 2 / (lam ** 2).sum()).item()
        # LA VECINA Y EL HUECO, medido SOBRE LA DIRECCION QUE SEPARA y no con el
        # radio entero. La primera version restaba los radios completos y daba
        # hueco negativo en las 86 —media -79,9— contradiciendo el 0,0% de
        # solapamiento que mide nubes.py. No habia contradiccion: en 2048
        # dimensiones el radio se reparte entre cientos de direcciones
        # ortogonales, asi que dos nubes con radios grandes y centros cercanos se
        # separan perfectamente en la unica direccion que decide. Restar radios
        # completos mide un volumen que no existe.
        peor_fisher, vecina, solape = 1e9, None, 0.0
        for o in hs:
            if o == h:
                continue
            w = c - cen[o]
            nw = w.norm()
            if nw < 1e-6:
                continue
            w = w / nw
            pa, pb = P @ w, Htr[usables[o]] @ w
            disp = ((pa.std() ** 2 + pb.std() ** 2) / 2).sqrt().item()
            f = abs(pa.mean().item() - pb.mean().item()) / max(1e-6, disp)
            if f < peor_fisher:
                corte = (pa.mean() + pb.mean()) / 2
                peor_fisher = f
                vecina = o
                solape = (((pa < corte).sum() + (pb > corte).sum()).item()
                          / (len(pa) + len(pb)))
        # RUIDO: que fraccion de sus frases cae mas cerca del centro de otra.
        cercas = [min(hs, key=lambda o: torch.dist(Htr[k], cen[o]).item())
                  for k in usables[h]]
        ruido = sum(1 for x in cercas if x != h) / len(cercas)
        info[h] = {"n": len(usables[h]), "radio": radio, "dim_ef": dim_ef,
                   "vecina": vecina, "fisher": peor_fisher, "solape": solape,
                   "ruido": ruido, "afuera": 0}

    # Casos del examen que caen mas cerca del centro de otra herramienta.
    for k, c in enumerate(ex):
        acs = [a for a in c["aceptables"] if a in cen]
        if not acs:
            continue
        cerca = min(hs, key=lambda o: torch.dist(Hex[k], cen[o]).item())
        if cerca not in acs:
            for a in acs:
                info[a]["afuera"] += 1

    # EL DIAGNOSTICO DE TRES TERMINOS. Una nube sana no esta ni apretada ni
    # sucia ni pegada a la vecina, y cada defecto se arregla distinto:
    #
    #   CONCENTRADA  dimension efectiva baja: hay una sola forma de pedirlo y el
    #                resto es superficie. Se arregla escribiendo frases NUEVAS,
    #                no mas variantes —variar engorda la linea, no la nube—.
    #   RUIDOSA      muchas de sus frases caen mas cerca del centro de otra. O
    #                estan mal escritas, o son ambiguas de verdad.
    #   PEGADA       su vecina mas cercana se le encima: fisher bajo. Se arregla
    #                con pares contrastivos, que ensenan el limite.
    import statistics
    dims = [i["dim_ef"] for i in info.values()]
    d_med = statistics.median(dims)
    for h, i in info.items():
        m = []
        if i["dim_ef"] < 0.6 * d_med:
            m.append("CONCENTRADA")
        if i["ruido"] > 0.10:
            m.append("RUIDOSA")
        if i["fisher"] < 4.0:
            m.append("PEGADA a " + i["vecina"])
        i["diagnostico"] = " + ".join(m) if m else "bien"
    malas = [h for h in hs if info[h]["diagnostico"] != "bien"]
    orden = sorted(malas, key=lambda h: (info[h]["fisher"], info[h]["dim_ef"]))
    print(f"\n{len(malas)} de {len(hs)} herramientas con algo que arreglar:")
    print(f"  {'herramienta':24s} {'n':>4s} {'dim':>5s} {'fisher':>6s} {'solape':>7s}"
          f" {'ruido':>6s} {'afuera':>6s}  diagnostico")
    for h in orden[:args.cuantas]:
        i = info[h]
        print(f"  {h:24s} {i['n']:4d} {i['dim_ef']:5.1f} {i['fisher']:6.2f}"
              f" {100*i['solape']:6.1f}% {100*i['ruido']:5.1f}% {i['afuera']:6d}"
              f"  {i['diagnostico']}")
    print(f"\n  dimension efectiva: mediana {d_med:.1f} · minima {min(dims):.1f}"
          f" · maxima {max(dims):.1f}")
    print(f"  fisher con la vecina mas cercana: mediana"
          f" {statistics.median(i['fisher'] for i in info.values()):.2f}"
          f" · minimo {min(i['fisher'] for i in info.values()):.2f}")
    print(f"  ruido medio {100*statistics.mean(i['ruido'] for i in info.values()):.1f}%")

    # --- frases raras -------------------------------------------------------
    raras = []
    for h in hs:
        P, c = Htr[usables[h]], cen[h]
        d = (P - c).norm(dim=1)
        z = (d - d.mean()) / max(1e-6, d.std().item())
        for pos, k in enumerate(usables[h]):
            # Y ademas: ¿esta mas cerca del centro de otra?
            cerca = min(hs, key=lambda o: torch.dist(Htr[k], cen[o]).item())
            if z[pos].item() > 2.0 or cerca != h:
                raras.append((z[pos].item(), cerca != h, h, cerca, tr[k]["frase"]))
    raras.sort(key=lambda r: (not r[1], -r[0]))
    print(f"\nFRASES RARAS ({len(raras)} de {len(tr)}): lejos del centro de su"
          f" herramienta o mas cerca de otra")
    for z, ajena, h, cerca, fr in raras[:args.raras]:
        print(f"  {h:22s} {'-> ' + cerca if ajena else 'z=%.1f' % z:26s} «{fr[:44]}»")
    if args.json:
        Path(args.json).write_text(json.dumps(
            {"herramientas": info,
             "raras": [{"herramienta": h, "mas_cerca_de": c if a else None,
                        "z": z, "frase": f} for z, a, h, c, f in raras]},
            ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
