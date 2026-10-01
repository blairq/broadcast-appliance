#!/usr/bin/env python3
"""El modelo reactivo al dia con el software: correrlo cada vez que el MCP crece.

Encadena todo lo demas, para UN perfil (que herramientas del MCP, por grupo:
datos/perfiles/) y UNA base (que modelo: Qwen/Qwen3.5-4B, -2B, -9B...):

  1 inventario     que herramientas hay y que cambio desde la ultima corrida
                   de este perfil (inventario-mcp.py; --regenerar le pregunta
                   al aparato andando).
  2 equivalencias  si el perfil contesta con un juego de etiquetas, las
                   herramientas nuevas reciben su nombre (completar-etiquetas.py).
  3 corpus         el bucle completo (bucle-corpus-completo.py): las nuevas
                   reciben frases en «cobertura» y todo pasa por sentido,
                   separar, afinar y margen.
  4 modelo         fabricar-modelo.py: entrenar, medir, MLX q4, GGUF Q4_K_M y
                   carpetas con forma de repo de Hugging Face (no sube nada).

Si nada cambio en el perfil y ya hay un modelo de este perfil y esta base, no
hace nada (--forzar para rehacer).

    tools/entrenar/actualizar-modelo.py --perfil reactivo --base Qwen/Qwen3.5-4B
    tools/entrenar/actualizar-modelo.py --perfil audio --base Qwen/Qwen3.5-2B --vueltas 2
    tools/entrenar/actualizar-modelo.py ... --sin-corpus      # solo reentrenar con el corpus actual
"""
import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
PY = RAIZ / ".venv-entrenar/bin/python"


def paso(nombre, cmd, obligatorio=True):
    print(f"\n\033[32m[actualizar] === {nombre} ===\033[0m", flush=True)
    r = subprocess.run([str(c) for c in cmd], cwd=RAIZ)
    if r.returncode and obligatorio:
        sys.exit(f"[actualizar] fallo «{nombre}»")
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perfil", default="reactivo")
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B")
    ap.add_argument("--trabajo", default="~/entrenamientos/actualizar")
    ap.add_argument("--regenerar", action="store_true", help="regenerar docs/mcp.json desde el aparato andando")
    ap.add_argument("--vueltas", type=int, default=2, help="vueltas del bucle del corpus")
    ap.add_argument("--sin-corpus", action="store_true", help="saltear el bucle del corpus")
    ap.add_argument("--forzar", action="store_true")
    ap.add_argument("--hf-usuario", help="usuario u organizacion de Hugging Face para subir")
    ap.add_argument("--subir", action="store_true", help="subir los repos al terminar (privados salvo --publico)")
    ap.add_argument("--publico", action="store_true")
    args = ap.parse_args()

    base_corta = args.base.split("/")[-1].lower().replace(".", "")
    dir_ = Path(args.trabajo).expanduser() / f"{args.perfil}-{base_corta}"
    dir_.mkdir(parents=True, exist_ok=True)
    foto, cambios = dir_ / "foto-mcp.json", dir_ / "cambios.json"
    sello = datetime.datetime.now().strftime("%Y%m%d-%H%M")

    cmd = [PY, E / "inventario-mcp.py", "--perfil", args.perfil, "--anterior", foto, "--salida", cambios]
    if args.regenerar:
        cmd.append("--regenerar")
    paso("inventario", cmd)
    c = json.loads(cambios.read_text())
    previo = sorted(dir_.glob("modelo-*/manifiesto.json"))
    sin_cambios = not (c["nuevas_en_perfil"] or c["cambiadas_en_perfil"] or c.get("quitadas")) and foto.exists()
    if sin_cambios and previo and not args.forzar:
        print(f"[actualizar] el perfil {args.perfil} no cambio y ya hay modelo ({previo[-1].parent.name}): nada que hacer")
        return

    paso("equivalencias", [PY, E / "completar-etiquetas.py", "--perfil", args.perfil])
    if not args.sin_corpus:
        paso("corpus", [PY, E / "bucle-corpus-completo.py", "--perfil", args.perfil, "--base", args.base,
                        "--nombre", f"corpus-{sello}", "--trabajo", dir_, "--vueltas", str(args.vueltas)])
    nombre = f"modelo-{sello}"
    cmd = [PY, E / "fabricar-modelo.py", "--base", args.base, "--perfil", args.perfil,
           "--nombre", nombre, "--trabajo", dir_]
    if args.hf_usuario:
        cmd += ["--hf-usuario", args.hf_usuario]
    if args.subir:
        cmd.append("--subir")
    if args.publico:
        cmd.append("--publico")
    paso("modelo", cmd)
    # la foto queda como «anterior» recien cuando hay modelo nuevo
    paso("foto", [PY, E / "inventario-mcp.py", "--perfil", args.perfil, "--anterior", foto, "--guardar"])
    man = json.loads((dir_ / nombre / "manifiesto.json").read_text())
    print(f"\n[actualizar] listo: {dir_ / nombre}")
    for r in man.get("repos", []):
        print(f"  repo para subir: {r}")


if __name__ == "__main__":
    main()
