#!/usr/bin/env python3
"""misc-include-cleaner (.clang-tidy) sobre los archivos que cambiaron: `make lint-includes`.

  tools/diag/lint-includes.py [--build build] [--base master] [archivo...]

Sin archivos: los de src/ que cambiaron respecto de `--base` (desde el punto en que la rama se
separó: commits, cambios sin commitear y archivos nuevos). Un .cpp/.mm se revisa entero; un
header, a través de su .cpp/.mm de al lado (clang-tidy necesita un TU con flags; el filtro de
headers queda en ese header). Un header sin .cpp al lado se avisa y no se revisa.

clang-tidy: el de CLANG_TIDY (variable de entorno o de make) o uno vendoreado en
extern/clang-tidy/<plataforma>/bin/ o extern/toolchain/*/bin/. No se usa uno del sistema
(portable innegociable). Si no hay ninguno, sale con error y lo dice (nunca en verde sin
haber revisado nada).

Sale con 1 si clang-tidy encontró algo (los hallazgos son errores), 2 si no pudo correr.
Corre UN clang-tidy por vez, con nice (es un linter: no pide turno, pero no compite).
"""
from __future__ import annotations

import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
EXT_TU = (".cpp", ".cc", ".cxx", ".mm")
EXT_H = (".hpp", ".h", ".hh", ".hxx", ".inl")


def _plataforma() -> str:
    s, m = platform.system(), platform.machine().lower()
    arq = "arm64" if m in ("arm64", "aarch64") else "x86_64"
    return {"Darwin": f"macos-{arq}", "Linux": f"linux-{arq}", "Windows": f"windows-{arq}"}.get(s, s.lower())


def buscar_clang_tidy() -> tuple[str | None, list[str]]:
    """(ruta, dónde se buscó)."""
    buscados = []
    env = os.environ.get("CLANG_TIDY", "")
    if env:
        buscados.append(f"CLANG_TIDY={env}")
        if os.access(env, os.X_OK):
            return env, buscados
    exe = "clang-tidy.exe" if platform.system() == "Windows" else "clang-tidy"
    cands = [RAIZ / "extern" / "clang-tidy" / _plataforma() / "bin" / exe]
    cands += sorted((RAIZ / "extern" / "toolchain").glob(f"*/bin/{exe}"))
    for c in cands:
        buscados.append(str(c.relative_to(RAIZ)) if c.is_relative_to(RAIZ) else str(c))
        if c.is_file() and os.access(c, os.X_OK):
            return str(c), buscados
    if not cands[1:]:
        buscados.append("extern/toolchain/*/bin/" + exe)
    return None, buscados


def _git(*args: str) -> list[str]:
    r = subprocess.run(["git", *args], cwd=RAIZ, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"[lint-includes] git {' '.join(args)}: {r.stderr.strip()}")
    return [x for x in r.stdout.splitlines() if x]


def cambiados(base: str) -> list[str]:
    desde = _git("merge-base", base, "HEAD")[0]
    out = set(_git("diff", "--name-only", "--diff-filter=ACMR", desde))
    out |= set(_git("ls-files", "--others", "--exclude-standard"))
    return sorted(out)


def main(argv: list[str]) -> int:
    build, base, archivos = RAIZ / "build", "master", []
    i = 0
    while i < len(argv):
        if argv[i] == "--build":
            build = pathlib.Path(argv[i + 1]).resolve(); i += 2
        elif argv[i] == "--base":
            base = argv[i + 1]; i += 2
        else:
            archivos.append(argv[i]); i += 1
    clang_tidy, buscados = buscar_clang_tidy()
    if clang_tidy is None:
        print("[lint-includes] ERROR: no hay clang-tidy vendoreado (no se usa uno del sistema). Buscado en:\n  "
              + "\n  ".join(buscados)
              + f"\n  Poné uno en extern/clang-tidy/{_plataforma()}/bin/ o pasá CLANG_TIDY=/ruta/a/clang-tidy.",
              file=sys.stderr)
        return 2
    if not (build / "compile_commands.json").is_file():
        print(f"[lint-includes] ERROR: falta {build}/compile_commands.json "
              f"(cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON {build})", file=sys.stderr)
        return 2
    if not archivos:
        archivos = cambiados(base)
    rel = []
    for a in archivos:
        p = (RAIZ / a).resolve() if not os.path.isabs(a) else pathlib.Path(a)
        try:
            r = p.relative_to(RAIZ).as_posix()
        except ValueError:
            continue
        if r.startswith("src/") and p.suffix in EXT_TU + EXT_H and p.is_file() and "/tests/" not in r:
            rel.append(p)
    if not rel:
        print("[lint-includes] no hay archivos de C/C++ de src/ para revisar")
        return 0
    tus, headers, sin_tu = set(), [], []
    for p in rel:
        if p.suffix in EXT_TU:
            tus.add(p)
        else:
            headers.append(p)
            hno = next((p.with_suffix(s) for s in EXT_TU if p.with_suffix(s).is_file()), None)
            if hno:
                tus.add(hno)
            else:
                sin_tu.append(p)
    for p in sin_tu:
        print(f"[lint-includes] aviso: {p.relative_to(RAIZ)} no tiene .cpp al lado: no se revisa")
    if not tus:
        return 0
    cmd = ["nice", "-n", "19", clang_tidy, "-p", str(build), "--quiet",
           "--warnings-as-errors=misc-include-cleaner"]
    # Solo los headers cambiados (y no los que incluyen los .cpp por otro lado).
    filtro = "|".join(re.escape(str(h)) for h in headers) if headers else "^$"
    cmd.append(f"--header-filter={filtro}")
    if platform.system() == "Darwin" and shutil.which("xcrun"):
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        if sdk:
            cmd.append(f"--extra-arg=-isysroot{sdk}")
    cmd += [str(t) for t in sorted(tus)]
    print(f"[lint-includes] {clang_tidy}: {len(tus)} TU, {len(headers)} headers")
    r = subprocess.run(cmd, cwd=RAIZ)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
