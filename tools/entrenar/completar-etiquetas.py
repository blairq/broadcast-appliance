#!/usr/bin/env python3
"""El json de equivalencias al dia: cada herramienta del perfil con su nombre de salida.

Si el perfil contesta con un juego de etiquetas (datos/perfiles/<p>.json,
campo «etiquetas»: «legibles», «familias»...) y el MCP gano herramientas, a las
nuevas les falta su entrada en datos/etiquetas.json y el prompt no se puede
armar. Este paso las completa:
  · un agente sin conexion propone 6 candidatos por herramienta, en el estilo
    del juego (le mostramos ejemplos de los que ya hay) y con la ficha real;
  · se toma el primero que respeta las reglas medidas: no es el nombre de otra
    herramienta del MCP, no es una etiqueta ya usada, y su PRIMER TOKEN no lo
    comparte con ninguna otra (ni del MCP ni del juego), salvo en el juego
    «familias», donde compartir el prefijo de la familia es justamente la idea.
Las que ya tienen entrada no se tocan: cambiarle el nombre a una herramienta
invalida al modelo entrenado.

    tools/entrenar/completar-etiquetas.py --perfil reactivo [--juego legibles]
Sin juego (perfil con «etiquetas»: null) no hay nada que completar.
"""
import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
ETIQ = RAIZ / "datos/etiquetas.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perfil", default=os.environ.get("BA_PERFIL") or "reactivo")
    ap.add_argument("--juego", help="por defecto el del perfil")
    ap.add_argument("--tokenizador", default="Qwen/Qwen3.5-4B")
    args = ap.parse_args()
    perfil = json.loads((RAIZ / f"datos/perfiles/{args.perfil}.json").read_text())
    juego = args.juego or perfil.get("etiquetas")
    if not juego:
        print(f"perfil {args.perfil}: contesta con los nombres reales, no hay equivalencias que completar")
        return
    os.environ["BA_PERFIL"] = args.perfil
    os.environ["BA_ETIQUETAS"] = "0"   # el conjunto, no las etiquetas: no hace falta el juego para listar
    import lib_prompt as lp
    import agente
    from transformers import AutoTokenizer

    inv = {h["nombre"]: h for h in lp.inventario()["herramientas"]}
    propias = lp.herramientas_reactivas()
    d = json.loads(ETIQ.read_text(encoding="utf-8"))
    tabla = d.setdefault(juego, {})
    faltan = [h for h in propias if h not in tabla]
    if not faltan:
        print(f"juego «{juego}»: las {len(propias)} herramientas del perfil tienen su nombre")
        return
    tok = AutoTokenizer.from_pretrained(args.tokenizador)
    pt = lambda s: tok(s, add_special_tokens=False).input_ids[0]
    ajenas = set(inv) - set(propias)
    prohibidos_tok = {pt(o) for o in ajenas}
    usados = set(tabla.values())
    usados_tok = {pt(v) for v in tabla.values()}
    familias = juego == "familias"

    ejemplos = "\n".join(f"  {k} -> {v}" for k, v in list(tabla.items())[:25])
    texto = [f"ESTILO DEL JUEGO «{juego}» (ejemplos de los que ya existen):", ejemplos, "",
             "HERRAMIENTAS QUE NECESITAN NOMBRE:"]
    for h in faltan:
        x = inv[h]
        texto.append(f"- {h} — {x['titulo']}: {x['descripcion'][:400]}")
    texto += ["", "NOMBRES PROHIBIDOS (otras herramientas del sistema):", ", ".join(sorted(ajenas | usados))]
    with tempfile.TemporaryDirectory() as tmp:
        lote, sal = Path(tmp) / "lote.txt", Path(tmp) / "salida.jsonl"
        lote.write_text("\n".join(texto), encoding="utf-8")
        agente.correr_lotes([agente.Trabajo("nombres.md", lote, sal, ["herramienta", "candidatos"])], paralelo=1)
        props = agente.leer([sal])
    elegidas = {}
    for r in props:
        h = r["herramienta"]
        if h not in faltan:
            continue
        for c in r["candidatos"]:
            c = c.strip()
            if not c or c in ajenas or c in usados or c in propias and c != h:
                continue
            t = pt(c)
            if t in prohibidos_tok or (not familias and t in usados_tok):
                continue
            elegidas[h] = c
            usados.add(c)
            usados_tok.add(t)
            break
    tabla.update(elegidas)
    ETIQ.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")
    sin = [h for h in faltan if h not in elegidas]
    print(f"juego «{juego}»: {len(elegidas)} nombres nuevos"
          + (f" · SIN NOMBRE (revisar a mano): {', '.join(sin)}" if sin else ""))
    for h, c in elegidas.items():
        print(f"  {h} -> {c}")
    if sin:
        sys.exit(1)


if __name__ == "__main__":
    main()
