#!/usr/bin/env python3
"""Que herramientas del MCP hay, cuales entran en cada perfil y que cambio desde la ultima vez.

El modelo se entrena sobre las fichas de las herramientas (nombre, descripcion,
parametros). Cuando el software crece —una herramienta nueva, una descripcion
que cambia— el prompt cambia y el modelo entrenado queda viejo aunque siga
contestando. Este paso lo detecta.

DE DONDE SALE: docs/mcp.json, que escribe tools/docs/mcp-inventario.py preguntandole
al aparato andando (tools/list). Con --regenerar se corre ese script (hace
falta el aparato arriba); si no, se usa el que haya y se avisa su fecha.

QUE DEJA (--salida, json): todas las herramientas, las del perfil, y contra la
foto anterior (--anterior): nuevas, quitadas y cambiadas (descripcion o
esquema), en total y dentro del perfil. Despues de correr, la foto actual pasa a
ser la anterior (--guardar).

    tools/entrenar/inventario-mcp.py --perfil reactivo --anterior f.json --salida cambios.json --guardar
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
INV = RAIZ / "docs/mcp.json"


def huella(h):
    return hashlib.sha256(json.dumps([h.get("titulo"), h.get("descripcion"), h.get("esquema")],
                                     sort_keys=True, ensure_ascii=False).encode()).hexdigest()[:12]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perfil", default=os.environ.get("BA_PERFIL") or "reactivo")
    ap.add_argument("--regenerar", action="store_true", help="correr tools/docs/mcp-inventario.py (aparato andando)")
    ap.add_argument("--anterior", help="foto anterior {nombre: huella}")
    ap.add_argument("--salida")
    ap.add_argument("--guardar", action="store_true", help="la foto actual pasa a ser la anterior")
    args = ap.parse_args()
    if args.regenerar:
        r = subprocess.run([sys.executable, str(RAIZ / "tools/docs/mcp-inventario.py"), str(INV)], cwd=RAIZ)
        if r.returncode:
            sys.exit("no se pudo regenerar el inventario: hace falta el aparato andando")
    os.environ["BA_PERFIL"] = args.perfil
    sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
    import lib_prompt as lp
    inv = json.loads(INV.read_text(encoding="utf-8"))
    foto = {h["nombre"]: huella(h) for h in inv["herramientas"]}
    perfil = set(lp.herramientas_reactivas())
    ant = json.loads(Path(args.anterior).read_text()) if args.anterior and Path(args.anterior).exists() else {}
    nuevas = sorted(set(foto) - set(ant)) if ant else []
    quitadas = sorted(set(ant) - set(foto)) if ant else []
    cambiadas = sorted(n for n in foto if n in ant and ant[n] != foto[n])
    res = {"inventario": str(INV), "generado": inv.get("generado"), "total": len(foto),
           "perfil": args.perfil, "en_perfil": sorted(perfil), "prompt": lp.huella()["prompt"],
           "nuevas": nuevas, "quitadas": quitadas, "cambiadas": cambiadas,
           "nuevas_en_perfil": [n for n in nuevas if n in perfil],
           "cambiadas_en_perfil": [n for n in cambiadas if n in perfil],
           "hay_cambios": bool(nuevas or quitadas or cambiadas) or not ant}
    print(f"inventario {inv.get('generado', '?')}: {len(foto)} herramientas · perfil {args.perfil}: {len(perfil)}"
          f" · prompt {res['prompt']}")
    if ant:
        print(f"  desde la foto anterior: {len(nuevas)} nuevas ({len(res['nuevas_en_perfil'])} en el perfil),"
              f" {len(quitadas)} quitadas, {len(cambiadas)} cambiadas ({len(res['cambiadas_en_perfil'])} en el perfil)")
    else:
        print("  sin foto anterior: se toma todo como nuevo")
    if args.salida:
        Path(args.salida).write_text(json.dumps(res, ensure_ascii=False, indent=1))
    if args.guardar and args.anterior:
        Path(args.anterior).write_text(json.dumps(foto, indent=1))


if __name__ == "__main__":
    main()
