#!/usr/bin/env python3
"""Migra includes de un header «paraguas» a sus partes (diseño físico, docs/local/PLAN-DISENO-FISICO.md).

Cambia SOLO la línea del #include, por otra línea: el archivo no cambia de largo, así que
las ubicaciones de los textos que extrae lupdate (`make i18n-actualizar`, `-locations
relative`) quedan iguales y los .ts no se mueven.

    python3 tools/dev/migrar_includes.py i18n            # muestra qué haría
    python3 tools/dev/migrar_includes.py i18n --aplicar  # lo hace

i18n: cada `#include "i18n/I18n.hpp"` de src/ pasa a la parte que el archivo usa:
  - I18nTexto.hpp     solo traduce (traducir, traducirEvento, llenar, QT_TRANSLATE_NOOP, el
                      idioma del evento y el del operador);
  - I18nIdioma.hpp    elige el idioma del proceso (usar, resolver, idiomaDeLaMaquina,
                      carpetas…), usa En (otro idioma), los archivos de un idioma (archivo,
                      leer), los plurales o el pseudo-idioma;
  - I18nPerfiles.hpp  usa Perfil, Cobertura, idiomas, perfil, cobertura o actual.
Cada parte trae la de arriba (I18nPerfiles → I18nIdioma → I18nTexto): un archivo necesita
siempre UNA línea. Se miran los usos calificados (`i18n::X`, también `ba::i18n::X`); en un
archivo con `using namespace ba::i18n` o dentro de `namespace ba::i18n {`, también los sin
calificar. Los comentarios y los textos entre comillas no cuentan.

json-fwd: en los headers que se nombran, `#include <nlohmann/json.hpp>` pasa a
`#include "json/JsonFwd.hpp"` (declaraciones; json.hpp cuesta ~1 s de frontend por TU).
Solo para headers que lo nombran en firmas, referencias o punteros: cada uno tiene que
compilar solo con JsonFwd, y quien lo usa entero tiene que incluir json.hpp por su cuenta.
    python3 tools/dev/migrar_includes.py json-fwd --aplicar src/core/ajustes/Ajustes.hpp ...
"""
import argparse
import pathlib
import re
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
EXTENSIONES = {".cpp", ".hpp", ".h", ".mm"}

I18N_INCLUDE = re.compile(r'^(\s*#\s*include\s*)"i18n/I18n\.hpp"(.*)$')
I18N_IDIOMA = ("carpetas", "carpetaPerfiles", "carpetaCatalogos", "idiomaDeLaMaquina", "elegirDelSistema", "resolver",
               "usar", "En", "archivo", "leer", "formaPlural", "formasDePlural", "pseudo")
I18N_PERFILES = ("Perfil", "Cobertura", "idiomas", "perfil", "cobertura", "actual")
I18N_USING = re.compile(r"\busing\s+namespace\s+(ba::)?i18n\b|\bnamespace\s+(ba::)?i18n\s*\{")
# Lo de i18n que no es de ninguna parte: el propio sistema.
I18N_EXCLUIDOS = {"src/core/i18n"}


def sin_comentarios_ni_textos(t: str) -> str:
    """El código sin comentarios ni literales (deja los saltos de línea)."""
    out, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if t.startswith("//", i):
            j = t.find("\n", i)
            i = n if j < 0 else j
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2)
            fin = n if j < 0 else j + 2
            out.append("\n" * t.count("\n", i, fin))
            i = fin
        elif c == "R" and i + 1 < n and t[i + 1] == '"' and (i == 0 or not (t[i - 1].isalnum() or t[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', t[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            cierre = ")" + m.group(1) + '"'
            j = t.find(cierre, i + m.end())
            fin = n if j < 0 else j + len(cierre)
            out.append('""' + "\n" * t.count("\n", i, fin))
            i = fin
        elif c in "\"'":
            j = i + 1
            while j < n and t[j] != c and t[j] != "\n":
                j += 2 if t[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def usa(codigo: str, nombres, sin_calificar: bool) -> bool:
    alt = "|".join(nombres)
    if re.search(r"\bi18n::(%s)\b" % alt, codigo):
        return True
    return sin_calificar and re.search(r"(?<![\w:.>])(%s)\b" % alt, codigo) is not None


PARTES = ("i18n/I18nTexto.hpp", "i18n/I18nIdioma.hpp", "i18n/I18nPerfiles.hpp")  # cada una trae la anterior
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def nivel_i18n(texto: str) -> int:
    """Qué parte usa el propio archivo (índice en PARTES)."""
    codigo = sin_comentarios_ni_textos(texto)
    sin_calificar = I18N_USING.search(codigo) is not None
    if usa(codigo, I18N_PERFILES, sin_calificar):
        return 2
    if usa(codigo, I18N_IDIOMA, sin_calificar):
        return 1
    return 0


def archivos(raices):
    for raiz in raices:
        for f in sorted(raiz.rglob("*")):
            if f.suffix in EXTENSIONES and f.is_file():
                yield f


def migrar_i18n(aplicar: bool, raices) -> int:
    """Cada archivo con el paraguas pasa a la parte que usa. Un header pasa a la mayor entre la
    suya y la de los archivos que lo incluyen y no incluyen i18n por su cuenta (lo usaban a
    través de él): así nadie pierde lo que tenía y cada archivo cambia una sola línea."""
    textos = {}
    for f in archivos(raices):
        rel = f.relative_to(RAIZ).as_posix()
        if any(rel.startswith(e + "/") for e in I18N_EXCLUIDOS):
            continue
        textos[rel] = f.read_bytes().decode("utf-8")  # tal cual, sin traducir los fines de línea
    directo = {r for r, t in textos.items() if any(I18N_INCLUDE.match(l) for l in t.split("\n"))}
    nivel = {r: nivel_i18n(t) for r, t in textos.items()}
    # Quién incluye a quién (por el final de la ruta: "escritorio/control.hpp" → src/app/escritorio/control.hpp).
    headers = [r for r in textos if r.endswith((".hpp", ".h"))]
    incluidos = {}
    for r, t in textos.items():
        hs = set()
        for s in INCLUDE.findall(t):
            hs.update(h for h in headers if h == s or h.endswith("/" + s))
        incluidos[r] = hs
    # Un archivo sin include propio de i18n usa lo que le llega por sus headers. Si ninguno de
    # los que lo traen (directos o no) le da la parte que necesita, se la dan todos ellos.
    def trae(r, vistos):
        out = set()
        for h in incluidos[r]:
            if h in vistos:
                continue
            vistos.add(h)
            if h in directo:
                out.add(h)
            out |= trae(h, vistos)
        return out

    subidos = {}
    cambio = True
    while cambio:
        cambio = False
        for r in sorted(textos):
            if r in directo or nivel[r] == 0:
                continue
            ps = trae(r, {r})
            if not ps or max(nivel[h] for h in ps) >= nivel[r]:
                continue
            for h in ps:
                if nivel[h] < nivel[r]:
                    nivel[h] = nivel[r]
                    subidos.setdefault(h, []).append(r)
                    cambio = True
    for h, rs in sorted(subidos.items()):
        print(f"  (sube {h} a {PARTES[nivel[h]]}: lo usan a través de él {', '.join(sorted(rs))})", file=sys.stderr)
    cuenta = {}
    for rel in sorted(directo):
        parte = PARTES[nivel[rel]]
        cuenta[parte] = cuenta.get(parte, 0) + 1
        print(f"{rel}: {parte}")
        lineas = textos[rel].split("\n")
        for k, l in enumerate(lineas):
            m = I18N_INCLUDE.match(l)
            if m:
                lineas[k] = f'{m.group(1)}"{parte}"{m.group(2)}'
        if aplicar:
            (RAIZ / rel).write_bytes("\n".join(lineas).encode("utf-8"))
    for parte, n in sorted(cuenta.items()):
        print(f"  {parte}: {n}", file=sys.stderr)
    return 0


JSON_INCLUDE = re.compile(r'^(\s*#\s*include\s*)<nlohmann/json\.hpp>(.*)$')


def migrar_json_fwd(aplicar: bool, headers) -> int:
    for h in headers:
        f = RAIZ / h
        lineas = f.read_bytes().decode("utf-8").split("\n")
        hecho = False
        for k, l in enumerate(lineas):
            m = JSON_INCLUDE.match(l)
            if m:
                lineas[k] = f'{m.group(1)}"json/JsonFwd.hpp"{m.group(2)}'
                hecho = True
        print(f"{h}: {'JsonFwd' if hecho else 'sin <nlohmann/json.hpp>, sin cambios'}")
        if aplicar and hecho:
            f.write_bytes("\n".join(lineas).encode("utf-8"))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("migracion", choices=["i18n", "json-fwd"])
    ap.add_argument("--aplicar", action="store_true", help="escribir los cambios (sin esto, solo los muestra)")
    ap.add_argument("rutas", nargs="*", help="i18n: carpetas a recorrer (por omisión, src); json-fwd: los headers")
    a = ap.parse_args()
    if a.migracion == "json-fwd":
        return migrar_json_fwd(a.aplicar, a.rutas)
    raices = [RAIZ / r for r in (a.rutas or ["src"])]
    return migrar_i18n(a.aplicar, raices)


if __name__ == "__main__":
    sys.exit(main())
