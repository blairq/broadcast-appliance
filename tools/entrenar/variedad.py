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
        # La vecina mas cercana y el hueco que queda entre bordes.
        otras = [(torch.dist(c, cen[o]).item(), o) for o in hs if o != h]
        dmin, vecina = min(otras)
        hueco = dmin - radio - torch.dist(Htr[usables[vecina]], cen[vecina]).mean().item()
        info[h] = {"n": len(usables[h]), "radio": radio, "dim_ef": dim_ef,
                   "vecina": vecina, "dist_vecina": dmin, "hueco": hueco, "afuera": 0}

    # Casos del examen que caen mas cerca del centro de otra herramienta.
    for k, c in enumerate(ex):
        acs = [a for a in c["aceptables"] if a in cen]
        if not acs:
            continue
        cerca = min(hs, key=lambda o: torch.dist(Hex[k], cen[o]).item())
        if cerca not in acs:
            for a in acs:
                info[a]["afuera"] += 1

    # Las que tienen nube flaca Y hueco de sobra: ahi conviene escribir.
    orden = sorted(hs, key=lambda h: (info[h]["dim_ef"] / max(0.1, info[h]["radio"])))
    print(f"\n{len(hs)} herramientas con nube. Las mas FLACAS primero:")
    print(f"  {'herramienta':24s} {'n':>4s} {'radio':>7s} {'dim':>5s} {'hueco':>7s}"
          f" {'afuera':>6s}  vecina")
    for h in orden[:args.cuantas]:
        i = info[h]
        marca = "  <- flaca y con lugar" if i["hueco"] > i["radio"] and i["dim_ef"] < 4 else ""
        print(f"  {h:24s} {i['n']:4d} {i['radio']:7.1f} {i['dim_ef']:5.1f}"
              f" {i['hueco']:7.1f} {i['afuera']:6d}  {i['vecina']}{marca}")

    import statistics
    print(f"\n  radio medio {statistics.mean(i['radio'] for i in info.values()):.1f}"
          f" · dimension efectiva media {statistics.mean(i['dim_ef'] for i in info.values()):.1f}"
          f" · hueco medio {statistics.mean(i['hueco'] for i in info.values()):.1f}")
    print("  Dimension efectiva baja con hueco grande = hay lugar para crecer sin")
    print("  riesgo de tocar a la vecina. Ahi hay que escribir frases nuevas, y")
    print("  NO mas variantes de las que ya estan: eso engorda la linea, no la nube.")

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
