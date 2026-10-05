#!/usr/bin/env python3
"""Laboratorio de DERIVAR: un corpus corto, centrado en el borde entre lo propio y
lo del asistente grande, para seguir entrenando el mejor de la v4 y medir.

EL DIAGNOSTICO (v4, vuelta 0). No es falta de conocimiento del mundo: es un borde
difuso del propio catalogo, que el modelo ve entero en el prompt.
  · en el banco, 53 ajenas que NO derivo eran casi todas del aparato pero de
    herramientas que el chico no tiene (parametros de plugins, nodos del rack,
    guardar ajustes): las mandaba a la propia mas parecida (audio_set,
    audio_analyze, source_properties);
  · en el examen, 26 propias que SI derivo (admitir/echar participantes, chat).
  · el corpus de derivar eran 1.204 frases FIJAS de un corpus viejo (las 84 de la
    v3): 32 son ahora herramientas de la v4 (encuestas, prompter, recetas,
    pantallas, medios). Etiquetas contradictorias.

LO QUE ARMA (lo que proponen Hammer, arXiv:2410.04587, y APIGen, 2406.18518:
ejemplos de «irrelevancia» CERCA del catalogo, mas contraste):
  · derivar = las frases viejas SIN las que ahora son propias + las 1.192 de
    datos/derivaciones-admin.jsonl cuya herramienta real no esta en el perfil
    (pedidos del aparato que hace el grande) + las listas de derivaciones.json;
  · positivos = filas de herramienta del corpus de la v4, pesando el doble las
    familias que confunde (audio, fuentes, participantes, chat, graficos...);
  · un poco de falta_dato y charla para no desaprender eso.
Nunca entra una frase del banco ni del examen.

    BA_PERFIL=vivo4 python tools/entrenar/lab-derivar.py --salida ~/entrenamientos/lab-derivar/corpus
"""
import argparse
import json
import random
import re
import sys
import unicodedata
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
import lib_tc as tc  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ste_es  # noqa: E402

CONFUSAS = ("audio_", "source_", "participant_", "chat_", "grafico_", "rotulo_", "capture_", "imagen_",
            "speakers_", "laminas_", "carrusel")


def norm(s):
    s = unicodedata.normalize("NFKD", s.lower()).encode("ascii", "ignore").decode()
    return re.sub(r"\W+", " ", s).strip()


def fila(est, frase, completion, tipo, herramienta, args=None):
    """Igual que tc-corpus.fila: mismo prompt (el del perfil) y mismo cierre."""
    return {"prompt": tc.cabeza() + tc.usuario(est, frase) + tc.COLA, "completion": completion + tc.FIN,
            "tipo": tipo, "herramienta": herramienta, "args": args, "frase": frase, "plantilla": None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--salida", required=True)
    ap.add_argument("--corpus-v4", default="~/entrenamientos/tc-v4/vuelta-0/corpus",
                    help="de donde salen los positivos (filas de herramienta, falta y charla)")
    ap.add_argument("--positivos", type=float, default=1.0, help="filas de herramienta por cada derivar")
    ap.add_argument("--extra", type=float, default=0.15, help="falta_dato y charla, como porcion del total")
    ap.add_argument("--semilla", type=int, default=5)
    args = ap.parse_args()
    rnd = random.Random(args.semilla)
    mias = set(tc.esquemas().keys())

    banco = [json.loads(l) for l in (RAIZ / "datos/orientacion.jsonl").read_text().splitlines() if l.strip()]
    examen = [json.loads(l) for l in (RAIZ / "datos/entrenamiento/examen.jsonl").read_text().splitlines()
              if l.strip()]
    prohibidas = {norm(x["frase"]) for x in banco + examen}

    # el origen real de cada frase de derivar que lo tiene registrado
    admin = [json.loads(l) for l in (RAIZ / "datos/derivaciones-admin.jsonl").read_text().splitlines()
             if l.strip()]
    real = {norm(r["frase"]): r["herramienta"] for r in admin}

    viejas = set()
    for parte in ("entrenar.jsonl", "validar.jsonl"):
        for l in (RAIZ / "datos/entrenamiento" / parte).read_text().splitlines():
            if l.strip() and json.loads(l)["herramienta"] == "derivar":
                viejas.add(json.loads(l)["frase"])
    listas = json.loads((RAIZ / "datos/derivaciones.json").read_text())
    de_listas = {f for k, v in listas.items() if not k.startswith("_") and isinstance(v, list) for f in v}

    cand = viejas | de_listas | {r["frase"] for r in admin}
    der, quitadas = [], {"propias_ahora": 0, "banco_o_examen": 0}
    vistas = set()
    for f in sorted(cand):
        n = norm(f)
        if n in vistas:
            continue
        vistas.add(n)
        if n in prohibidas:
            quitadas["banco_o_examen"] += 1
            continue
        if real.get(n) in mias:  # la hace una herramienta del perfil: NO es derivar
            quitadas["propias_ahora"] += 1
            continue
        if ste_es.errores_derivar(f):  # fuera de STE-ES (largo, verbo de una propia, sin objeto)
            quitadas["ste_es"] = quitadas.get("ste_es", 0) + 1
            continue
        der.append(f)
    rnd.shuffle(der)

    base = Path(args.corpus_v4).expanduser()
    filas_v4 = [json.loads(l) for l in (base / "entrenar.jsonl").read_text().splitlines() if l.strip()]
    if filas_v4[0]["prompt"][:200] != fila(tc.estado_sintetico(rnd)[0], "x", "", "", "")["prompt"][:200]:
        raise SystemExit("el corpus de la v4 tiene otro prompt: correr con el mismo BA_PERFIL")
    herr = [f for f in filas_v4 if f["tipo"] == "herramienta" and norm(f["frase"]) not in prohibidas]
    peso = [2.0 if f["herramienta"].startswith(CONFUSAS) else 1.0 for f in herr]
    n_pos = int(len(der) * args.positivos)
    pos = rnd.choices(herr, weights=peso, k=n_pos)
    otros = [f for f in filas_v4 if f["tipo"] in ("falta", "charla") and norm(f["frase"]) not in prohibidas]
    n_extra = int((len(der) + n_pos) * args.extra / (1 - args.extra))
    extra = rnd.sample(otros, min(n_extra, len(otros)))

    filas = [fila(tc.estado_sintetico(rnd)[0], f, tc.llamada(tc.DERIVAR, {}), "derivar", tc.DERIVAR, {})
             for f in der] + pos + extra
    rnd.shuffle(filas)
    n_val = max(50, len(filas) // 20)
    out = Path(args.salida).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    for nombre, parte in (("validar.jsonl", filas[:n_val]), ("entrenar.jsonl", filas[n_val:])):
        (out / nombre).write_text("".join(json.dumps(f, ensure_ascii=False) + "\n" for f in parte))
    resumen = {"derivar": len(der), "positivos": len(pos), "falta_y_charla": len(extra),
               "quitadas": quitadas, "entrenar": len(filas) - n_val, "validar": n_val}
    (out / "lab.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
    print(json.dumps(resumen, ensure_ascii=False))


if __name__ == "__main__":
    main()
