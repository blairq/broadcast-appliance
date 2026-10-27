#!/usr/bin/env python3
"""Pone los .ts al día con el código: `make i18n-actualizar`.

Para cada idioma (assets/idiomas/<código>.json) corre lupdate sobre todo el código
(src/: C++, Objective-C++, QML) más lo que extrae tools/i18n/extraer.py (la web), y
deja i18n/ba_<código>.ts con los textos nuevos sin traducir y sin los que ya no están.
  - el original (fuente: true) solo lleva los plurales (-pluralonly): «%n invitado»
    necesita sus dos formas también en español;
  - el pseudo-idioma no tiene .ts (se arma solo).

lupdate es una herramienta de desarrollo (está en extern/qt6-host): las compilaciones
no la necesitan, leen los .ts con tools/i18n/ts-a-json.py.

Uso: tools/i18n/actualizar.py [código ...]     (sin códigos: todos)
"""
import json
import os
import pathlib
import platform
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
PERFILES = RAIZ / "assets" / "idiomas"
TS = RAIZ / "i18n"
TRABAJO = RAIZ / "build" / "i18n-actualizar"
EXTENSIONES = {".cpp", ".hpp", ".h", ".mm", ".qml"}


def lupdate() -> pathlib.Path:
    if os.environ.get("LUPDATE"):
        return pathlib.Path(os.environ["LUPDATE"])
    sub = {"Darwin": "macos", "Linux": "gcc_64"}.get(platform.system(), "msvc2019_64")
    exe = "lupdate.exe" if platform.system() == "Windows" else "lupdate"
    hay = sorted((RAIZ / "extern" / "qt6-host").glob(f"*/{sub}/bin/{exe}"))
    if not hay:
        raise SystemExit("[i18n] no encuentro lupdate en extern/qt6-host ('make deps-qt-host'); o LUPDATE=<ruta>")
    return hay[-1]


def fuentes() -> list:
    out = []
    for f in sorted((RAIZ / "src").rglob("*")):
        if f.suffix in EXTENSIONES and f.is_file() and "tests" not in f.relative_to(RAIZ).parts:
            out.append(f)
    return out


def main() -> int:
    pedidos = set(sys.argv[1:])
    TRABAJO.mkdir(parents=True, exist_ok=True)
    TS.mkdir(exist_ok=True)
    web = TRABAJO / "cadenas-web.cpp"
    subprocess.run([sys.executable, "-I", str(RAIZ / "tools" / "i18n" / "extraer.py"), str(web)], check=True)
    lista = TRABAJO / "fuentes.lst"
    lista.write_text("\n".join(str(f) for f in fuentes() + [web]) + "\n", encoding="utf-8")
    exe = lupdate()
    comunes = ["-tr-function-alias", "translate+=traducir", "-locations", "relative", "-no-obsolete",
               "-source-language", "es_AR"]
    for p in sorted(PERFILES.glob("*.json")):
        perfil = json.loads(p.read_text(encoding="utf-8"))
        codigo = p.stem
        if pedidos and codigo not in pedidos or perfil.get("pseudo"):
            continue
        ts = TS / f"ba_{codigo}.ts"
        opciones = ["-pluralonly"] if perfil.get("fuente") else []
        opciones += ["-target-language", codigo.replace("-", "_")]
        print(f"[i18n] {ts.relative_to(RAIZ)}")
        try:
            # Con todo el programa tarda segundos: más que esto es que se colgó (ver llamadas.py).
            r = subprocess.run([str(exe), f"@{lista}", *comunes, *opciones, "-ts", str(ts)],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            print("[i18n] lupdate se colgó: corré tools/i18n/llamadas.py", file=sys.stderr)
            return 1
        # El analizador de C++ de lupdate se queja de cada `struct X::Impl` (no lee los .hpp
        # de otros archivos); no dice nada de las traducciones.
        for linea in r.stdout.splitlines():
            if "Ignoring definition of undeclared qualified class" not in linea:
                print(linea)
        if r.returncode != 0:
            return r.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
