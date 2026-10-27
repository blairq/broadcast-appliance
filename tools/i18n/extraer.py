#!/usr/bin/env python3
"""Los textos que lupdate no ve, a un .cpp que sí ve (solo para lupdate: no se compila).

lupdate lee C++ y QML. Lo demás se marca así y sale de acá:
  - la web (assets/web/<interfaz>/): `t("texto")` en el JS y, en el HTML,
    `data-i18n="texto"` (el contenido), `data-i18n-placeholder`, `data-i18n-title`,
    `data-i18n-aria-label`, `data-i18n-alt`. El contexto es `web/<interfaz>` (`web/comun` lo de todas).

Cada texto sale como QT_TRANSLATE_NOOP("<contexto>", "<texto>") (QT_TRANSLATE_N_NOOP si
lleva %n: es un plural), así entra al mismo .ts que el resto y la traducción se hace en un
solo lugar.

Uso: tools/i18n/extraer.py <salida.cpp>
"""
import pathlib
import re
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
WEB = RAIZ / "assets" / "web"

# t("…") / t('…') que no sea parte de otro nombre (st(, x.t(, $t()
JS_T = re.compile(r"""(?<![\w.$])t\(\s*(["'])((?:\\.|(?!\1).)*?)\1""", re.S)
HTML_ATTR = re.compile(r"""data-i18n(?:-placeholder|-title|-aria-label|-alt)?\s*=\s*(["'])(.*?)\1""", re.S)


def de_js(literal: str) -> str:
    """Un literal de JS (sin comillas) al texto que representa."""
    out, i = [], 0
    escapes = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", "'": "'", '"': '"', "`": "`"}
    while i < len(literal):
        c = literal[i]
        if c == "\\" and i + 1 < len(literal):
            n = literal[i + 1]
            if n == "u" and i + 5 < len(literal):
                out.append(chr(int(literal[i + 2 : i + 6], 16)))
                i += 6
                continue
            out.append(escapes.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def a_cpp(texto: str) -> str:
    return '"' + texto.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'


def de_html(attr: str) -> str:
    return (attr.replace("&quot;", '"').replace("&#39;", "'").replace("&lt;", "<").replace("&gt;", ">")
            .replace("&amp;", "&"))


def textos_web():
    for f in sorted(WEB.rglob("*")):
        if f.suffix not in (".js", ".html", ".htm", ".mjs") or not f.is_file():
            continue
        if f == WEB / "comun" / "i18n.js":  # el traductor mismo: sus ejemplos no son textos
            continue
        interfaz = f.relative_to(WEB).parts[0]
        contexto = f"web/{interfaz}"
        texto = f.read_text(encoding="utf-8", errors="replace")
        if f.suffix in (".html", ".htm"):
            for m in HTML_ATTR.finditer(texto):
                if m.group(2):
                    yield contexto, de_html(m.group(2)), f
        for m in JS_T.finditer(texto):
            if m.group(2):
                yield contexto, de_js(m.group(2)), f


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    lineas = ["// GENERADO por tools/i18n/extraer.py: lo lee lupdate, no se compila."]
    vistos = set()
    for contexto, texto, f in textos_web():
        if (contexto, texto) in vistos:
            continue
        vistos.add((contexto, texto))
        lineas.append(f"// {f.relative_to(RAIZ).as_posix()}")
        # Con %n es un plural: lupdate lo marca numerus con la variante _N_.
        macro = "QT_TRANSLATE_N_NOOP" if "%n" in texto else "QT_TRANSLATE_NOOP"
        lineas.append(f"{macro}({a_cpp(contexto)}, {a_cpp(texto)});")
    pathlib.Path(sys.argv[1]).write_text("\n".join(lineas) + "\n", encoding="utf-8")
    print(f"[i18n] {len(vistos)} textos de la web")
    return 0


if __name__ == "__main__":
    sys.exit(main())
