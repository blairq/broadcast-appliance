#!/usr/bin/env python3
"""El corpus de tool calling: herramienta + argumentos, derivar, falta_dato y charla.

Junta (ver docs/entrenamiento-avanzado.md):
  - datos/tc-anotaciones.json (tc-anotar.py): cada plantilla se rinde K veces
    con estados del aparato DISTINTOS (lib_tc.estado_sintetico) y la llamada con
    los nombres de ese estado; algunas con las variantes de siempre (sin tildes,
    muletillas, algun typo; las de corpus-entrenamiento.py);
  - derivar: las frases ajenas del corpus de nombres (datos/entrenamiento: las
    mismas que ya funcionaban), como llamada a `derivar`;
  - falta_dato: las anotaciones con "falta", como llamada a `falta_dato`;
  - charla (tc-charla.py): pedidos generales y del oficio con la respuesta del
    modelo BASE en texto; la mitad con estado, la mitad sin.

PARTES, sin mezclar nunca:
  entrenar.jsonl / validar.jsonl   lo que se entrena (validar: 5 %, para la perdida)
  prueba.jsonl     PLANTILLAS que no se entrenan (15 % por herramienta, fijo por
                   hash de la frase): mide argumentos con frases nunca vistas
  charla-prueba.jsonl  respuestas del base que no se entrenan: mide cuanto de lo
                   general se conserva
El banco de orientacion y el examen no se tocan: se miden aparte (tc-evaluar.py).

Proporciones (arXiv: Hammer 2410.04587, Alopex 2411.05209): derivar ~15 % y
falta_dato ~10 % (lo que «no es suyo»), charla --charla (35 % por defecto).

    tools/entrenar/tc-corpus.py --salida ~/entrenamientos/tc/corpus [--k 4] [--charla 0.35]
"""
import argparse
import hashlib
import importlib.util
import json
import random
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
import lib_tc as tc  # noqa: E402

_spec = importlib.util.spec_from_file_location("corpus_viejo", RAIZ / "tools/corpus-entrenamiento.py")
corpus_viejo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(corpus_viejo)


def es_prueba(frase, porcion):
    return int(hashlib.sha256(frase.encode()).hexdigest()[:8], 16) / 0xFFFFFFFF < porcion


def resolver_contexto(args, est):
    """[escena_armada] / [escena_actual] -> el valor del estado de turno."""
    out = {}
    for k, v in args.items():
        if isinstance(v, str) and v in ("[escena_armada]", "[escena_actual]"):
            v = est[v[1:-1]]
            if not v:
                return None
        out[k] = v
    return out


def fila(est, frase, completion, tipo, herramienta, args=None, con_estado=True, plantilla=None):
    u = tc.usuario(est if con_estado else None, frase)
    return {"prompt": tc.cabeza() + u + tc.COLA, "completion": completion + tc.FIN, "tipo": tipo,
            "herramienta": herramienta, "args": args, "frase": frase, "plantilla": plantilla}


def rendir(h, a, rnd, k, variar):
    """K ejemplos de una anotacion, cada uno con su estado."""
    out = []
    intentos = 0
    while len(out) < k and intentos < k * 4:
        intentos += 1
        tipos = tc.tipos_de(a["plantilla"], a.get("args", {}))
        est, formas = tc.estado_sintetico(rnd, tipos)
        if "escena_armada" in json.dumps(a.get("args", {})) and not est["escena_armada"]:
            est["escena_armada"] = rnd.choice([e for e in est["escenas"] if e != est["escena_actual"]] or est["escenas"])
        frase, args = tc.rellenar(a["plantilla"], a.get("args", {}), formas, rnd)
        if frase is None:
            continue
        if variar and rnd.random() < 0.5:
            frase = rnd.choice(corpus_viejo.variantes(frase, rnd, 3))
        if "falta" in a:
            out.append(fila(est, frase, tc.llamada(tc.FALTA, {"pregunta": a["falta"]}), "falta", h,
                            {"pregunta": a["falta"]}, plantilla=a["plantilla"]))
            continue
        args = resolver_contexto(args, est)
        if args is None:
            continue
        out.append(fila(est, frase, tc.llamada(h, args), "herramienta", h, args, plantilla=a["plantilla"]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--salida", required=True)
    ap.add_argument("--anotaciones", default=str(RAIZ / "datos/tc-anotaciones.json"))
    ap.add_argument("--derivar-de", default=str(RAIZ / "datos/entrenamiento"),
                    help="corpus de nombres (make corpus-entrenamiento) de donde salen las frases ajenas")
    ap.add_argument("--charla-de", default="~/entrenamientos/tc/charla-*.jsonl")
    ap.add_argument("--k", type=int, default=4, help="ejemplos por plantilla (estados distintos)")
    ap.add_argument("--charla", type=float, default=0.35, help="porcion de charla en el corpus")
    ap.add_argument("--derivar", type=float, default=0.15, help="porcion de derivar")
    ap.add_argument("--prueba", type=float, default=0.15, help="plantillas que no se entrenan")
    ap.add_argument("--semilla", type=int, default=13)
    args = ap.parse_args()
    rnd = random.Random(args.semilla)
    sal = Path(args.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)

    anot = json.loads(Path(args.anotaciones).read_text())
    ent, prueba = [], []
    for h, lista in sorted(anot.items()):
        for a in lista:
            if not a.get("plantilla"):
                continue
            if es_prueba(a["frase"], args.prueba) and not a.get("nueva"):
                prueba += rendir(h, a, rnd, 2, variar=False)
            else:
                ent += rendir(h, a, rnd, args.k, variar=True)
    n_herr = sum(1 for f in ent if f["tipo"] == "herramienta")
    n_falta = sum(1 for f in ent if f["tipo"] == "falta")

    # derivar: las frases ajenas que ya funcionaban, cada una con un estado cualquiera
    der = []
    for parte in ("entrenar.jsonl", "validar.jsonl"):
        p = Path(args.derivar_de) / parte
        if p.exists():
            der += [json.loads(l)["frase"] for l in p.read_text().splitlines()
                    if l.strip() and json.loads(l)["herramienta"] == "derivar"]
    der = sorted(set(der))
    rnd.shuffle(der)
    base = len(ent)
    # derivar y charla son porciones del TOTAL: total = base / (1 - derivar - charla)
    total = int(base / max(0.1, 1 - args.derivar - args.charla))
    for frase in der[:int(total * args.derivar)]:
        est, _ = tc.estado_sintetico(rnd)
        ent.append(fila(est, frase, tc.llamada(tc.DERIVAR, {}), "derivar", tc.DERIVAR, {}))

    # charla: la respuesta del base, en texto
    import glob
    charla = []
    for f in sorted(glob.glob(str(Path(args.charla_de).expanduser()))):
        charla += [json.loads(l) for l in Path(f).read_text().splitlines() if l.strip()]
    rnd.shuffle(charla)
    n_cp = min(300, len(charla) // 10)
    charla_prueba, charla = charla[:n_cp], charla[n_cp:]
    for c in charla[:int(total * args.charla)]:
        est, _ = tc.estado_sintetico(rnd)
        ent.append(fila(est, c["pedido"], c["respuesta"], "charla", None, None, con_estado=rnd.random() < 0.5))
    cp = []
    for c in charla_prueba:
        est, _ = tc.estado_sintetico(rnd)
        cp.append(fila(est, c["pedido"], c["respuesta"], "charla", None, None, con_estado=rnd.random() < 0.5))

    rnd.shuffle(ent)
    n_val = max(50, len(ent) // 20)
    val, ent = ent[:n_val], ent[n_val:]
    for nombre, filas in (("entrenar", ent), ("validar", val), ("prueba", prueba), ("charla-prueba", cp)):
        (sal / f"{nombre}.jsonl").write_text("\n".join(json.dumps(f, ensure_ascii=False) for f in filas) + "\n")
    cuenta = {t: sum(1 for f in ent if f["tipo"] == t) for t in ("herramienta", "falta", "derivar", "charla")}
    huella = {"formato": "tc", "prompt": tc.huella(), "ejemplos": len(ent), "por_tipo": cuenta,
              "porcion": {t: round(n / max(1, len(ent)), 3) for t, n in cuenta.items()},
              "prueba": len(prueba), "charla_prueba": len(cp), "k": args.k,
              "anotaciones": hashlib.sha256(Path(args.anotaciones).read_bytes()).hexdigest()[:16]}
    (sal / "huella.json").write_text(json.dumps(huella, ensure_ascii=False, indent=1))
    print(json.dumps(huella, ensure_ascii=False))
    if len(der) < int(total * args.derivar):
        print(f"OJO: pocas frases de derivar ({len(der)} de {int(total * args.derivar)})")
    if len(charla) < int(total * args.charla):
        print(f"OJO: poca charla ({len(charla)} de {int(total * args.charla)})")


if __name__ == "__main__":
    main()
