#!/usr/bin/env python3
"""Las llamadas de traducción de C++ que lupdate no puede leer (o lo cuelgan).

lupdate 6.6 entra en un bucle sin fin si el número de un plural tiene un texto entre
comillas adentro de una llamada: `traducir("mcp", "%n cuadro", nullptr, r.value("n", 0))`.
El número va en una variable: `const int n = r.value("n", 0); traducir(…, nullptr, n)`.

Uso como módulo: `problemas()` → lista de "archivo:línea: motivo". Como programa, la imprime
y sale con 1 si hay alguna. Lo usa tests/test_i18n.py.
"""
import pathlib
import re
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
LLAMADA = re.compile(r"\b(?:traducir|translate|tr)\s*\(")


def _argumentos(txt: str, i: int):
    """Los argumentos de la llamada cuyo «(» está en i (respetando paréntesis y literales)."""
    args, prof, ini, j = [], 0, i + 1, i
    while j < len(txt):
        c = txt[j]
        if c in "\"'":
            fin = j + 1
            while fin < len(txt) and txt[fin] != c:
                fin += 2 if txt[fin] == "\\" else 1
            j = fin
        elif c in "([{":
            prof += 1
        elif c in ")]}":
            prof -= 1
            if prof == 0:
                args.append(txt[ini:j])
                return args
        elif c == "," and prof == 1:
            args.append(txt[ini:j])
            ini = j + 1
        j += 1
    return args


def problemas():
    out = []
    for f in sorted((RAIZ / "src").rglob("*")):
        if f.suffix not in (".cpp", ".hpp", ".mm") or "tests" in f.parts or not f.is_file():
            continue
        txt = f.read_text(encoding="utf-8", errors="replace")
        for m in LLAMADA.finditer(txt):
            args = _argumentos(txt, m.end() - 1)
            # (contexto, texto, desambiguación, n): el 4.º es el número del plural.
            # Solo las de traducción (contexto y texto literales), y solo un texto que es
            # argumento de una llamada adentro del número (`x.value("n", 0)`; `a["n"]` anda).
            if (len(args) >= 4 and args[0].strip().startswith('"') and args[1].strip().startswith('"')
                    and re.search(r'[(,]\s*"', args[3])):
                linea = txt.count("\n", 0, m.start()) + 1
                out.append(f"{f.relative_to(RAIZ)}:{linea}: el número del plural tiene un texto entre comillas "
                           f"({args[3].strip()}): ponelo en una variable (lupdate se cuelga)")
    return out


if __name__ == "__main__":
    p = problemas()
    print("\n".join(p) or "sin problemas")
    sys.exit(1 if p else 0)
