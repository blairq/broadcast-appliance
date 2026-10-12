#!/usr/bin/env python3
"""Chequeo de sintaxis SIN ninja (se puede correr en paralelo con otros builds y agentes).

Hace falta build/compile_commands.json: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON build

    python3 chequear.py src/app/foo/Bar.cpp [otro.cpp ...]   # C++: clang -fsyntax-only con las flags del build
    python3 chequear.py src/app/qml/pages/X.qml               # QML: qmllint (solo sintaxis/estructura)

Los .cpp nuevos (que no están en compile_commands.json) usan las flags de
src/app/ui/StudioViewModel.cpp (target ba-app). Sale con 1 si algo falla.
"""
import json
import os
import shlex
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
def _principal():
    """El árbol con build/ (en un worktree de git, el principal: los worktrees no tienen build)."""
    if os.path.exists(os.path.join(RAIZ, "build", "compile_commands.json")):
        return RAIZ
    r = subprocess.run(["git", "-C", RAIZ, "worktree", "list", "--porcelain"], capture_output=True, text=True)
    for l in r.stdout.splitlines():
        if l.startswith("worktree "):
            return l[len("worktree "):]
    return RAIZ


PRINCIPAL = _principal()
CC = os.path.join(PRINCIPAL, "build", "compile_commands.json")
QMLLINT = next((os.path.join(d, "qmllint") for d in sorted(__import__("glob").glob(os.path.join(RAIZ, "extern/qt6-host/*/macos/bin"))) if os.path.exists(os.path.join(d, "qmllint"))), "qmllint")


def main():
    entradas = json.load(open(CC))
    por = {e["file"]: e for e in entradas}
    base = por[os.path.join(PRINCIPAL, "src/app/ui/StudioViewModel.cpp")]
    mal = 0
    for arg in sys.argv[1:]:
        f = os.path.abspath(os.path.join(RAIZ, arg)) if not os.path.isabs(arg) else arg
        if f.endswith(".qml"):
            r = subprocess.run([QMLLINT, "--bare", f], capture_output=True, text=True)
            # Solo errores de sintaxis (los «Warning» de imports sin resolver son del entorno).
            # Un error de sintaxis sale como «archivo:línea:col: …» sin «Warning:»/«Info:» adelante.
            errores = [l for l in (r.stdout + r.stderr).splitlines()
                       if l.startswith(f + ":") or l.startswith(arg + ":")]
            print(("OK   " if not errores else "FALLA ") + arg)
            for l in errores[:20]:
                print("   ", l)
            mal += 1 if errores else 0
            continue
        # En un worktree: las flags del archivo equivalente del principal, con los
        # -I de src/ apuntando a este árbol (build/ y extern/ quedan los del principal).
        e = por.get(f.replace(RAIZ, PRINCIPAL, 1), base)
        args = [a.replace(PRINCIPAL + "/src/", RAIZ + "/src/") for a in shlex.split(e["command"])]
        # sacar -o X y -c Y, poner -fsyntax-only y el archivo
        out = []
        saltar = False
        for a in args:
            if saltar:
                saltar = False
                continue
            if a in ("-o", "-c"):
                saltar = True
                continue
            out.append(a)
        out += ["-fsyntax-only", f]
        r = subprocess.run(out, cwd=e["directory"], capture_output=True, text=True)
        errores = [l for l in r.stderr.splitlines() if " error: " in l or "fatal error" in l]
        print(("OK   " if r.returncode == 0 else "FALLA ") + arg)
        for l in errores[:25]:
            print("   ", l)
        mal += 0 if r.returncode == 0 else 1
    sys.exit(1 if mal else 0)


if __name__ == "__main__":
    main()
