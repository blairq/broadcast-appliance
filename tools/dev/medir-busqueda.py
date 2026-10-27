#!/usr/bin/env python3
"""Cuánto encuentra el buscador de herramientas del MCP, con frases que NO miran las etiquetas.

POR QUE. test_mcp_busqueda mide con preguntas escritas mirando las etiquetas, y
daba 96 % mientras audio_set (volumen y silencio) no aparecia ni entre las ocho
con «poner volumen de una fuente de audio». Este script mide con cuatro conjuntos:

    test         las preguntas del test (src/app/tests/datos/preguntas.json)
    intencion    verbo + objeto de cada sentido de STE-ES (datos/ste-es.json)
    cabina       las frases de las anotaciones de entrenamiento (tc-anotaciones)
    banco        el banco de orientacion: SOLO PARA MEDIR. Las etiquetas no se
                 escriben mirando sus fallos (seria entrenar con el banco).

y por cada uno: en que puesto queda la herramienta correcta (primera, entre 3,
entre 8: el modelo grande recibe las ocho). Con --por-herramienta, las peores
(de las frases de cabina) y quien les gana.

EL BUSCADOR es el de src/app/mcp/McpIndice.cpp portado tal cual (mismos pesos,
idf, prefijos, imperativos y vacias), sobre docs/mcp.json: nombre, etiquetas
(familias + propias), titulo y descripcion (la app suma ademas `details`, que
el inventario no tiene). Si se toca McpIndice.cpp, hay que tocar esto igual:
--arnes compila tools/dev/medir-busqueda-arnes.cpp contra el McpIndice.cpp de
verdad y compara los numeros (tienen que dar iguales).

    tools/docs/mcp-inventario.py --desde-codigo $PWD/docs/mcp.json   # despues de tocar etiquetas
    tools/dev/medir-busqueda.py [--por-herramienta] [--arnes --nlohmann DIR]
"""
import argparse
import bisect
import json
import math
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]

# --- el buscador (McpIndice.cpp) -------------------------------------------------
PESO_NOMBRE, PESO_ETIQUETA, PESO_TITULO, PESO_CUERPO = 6.0, 5.0, 3.0, 0.6
DESCUENTO_PREFIJO, PREFIJO_MINIMO, FRACCION_INUTIL = 0.45, 4, 0.4
VACIAS = {"el", "la", "lo", "los", "las", "un", "una", "unos", "unas", "de", "del", "al", "es", "son", "en", "y",
          "o", "que", "con", "por", "su", "sus", "mi", "mis", "tu", "tus", "ya", "muy", "pero", "porque", "porfa",
          "dale", "che", "eh", "ahi", "aca", "aqui", "alla", "asi", "mas"}
TILDES = str.maketrans("áéíóúüñ", "aeiouun")
AGUDA = {"á": "a", "é": "e", "í": "i"}
NO_IMPERATIVO = {"está", "estás"}
PEGADOS_IMP = ["selos", "selas", "selo", "sela", "melos", "melas", "melo", "mela",
               "les", "nos", "los", "las", "le", "me", "lo", "la"]
PEGADOS_BUSCAR = ["selos", "selas", "selo", "sela", "nos", "les", "los", "las", "me", "te", "le", "lo", "la", "se"]


def imperativo(w):
    """pará -> parar, poné -> poner, bajale -> bajar, mostrámela -> mostrar."""
    if w in NO_IMPERATIVO:
        return w
    if len(w) >= 3 and w[-1] in AGUDA:
        return w[:-1] + AGUDA[w[-1]] + "r"
    for p in PEGADOS_IMP:
        if not w.endswith(p) or len(w) < len(p) + 4:
            continue
        base = w[:-len(p)]
        if base[-1] in AGUDA:
            return base[:-1] + AGUDA[base[-1]] + "r"
        if p in ("le", "les", "me", "nos") and base[-1] in "aei":
            return base + "r"
        return w
    return w


def terminos_marcados(s):
    """[(termino, salio_de_un_imperativo)], como terminosMarcados()."""
    out = []
    for w in re.split(r"[^a-z0-9áéíóúüñ]+", s.lower()):
        if not w:
            continue
        antes = w.translate(TILDES)
        t = imperativo(w).translate(TILDES)
        if antes not in VACIAS and len(t) >= 2 and t not in VACIAS:
            out.append((t, t != antes))
    return out


def terminos(s):
    return [t for t, _ in terminos_marcados(s)]


class Indice:
    def __init__(self, docs):
        self.docs = docs
        self.inv = defaultdict(list)
        for i, d in enumerate(docs):
            peso = {}
            for texto, p in ([(d["nombre"], PESO_NOMBRE)] + [(e, PESO_ETIQUETA) for e in d["etiquetas"]]
                             + [(d["titulo"], PESO_TITULO), (d["cuerpo"], PESO_CUERPO)]):
                for t in terminos(texto):
                    peso[t] = max(peso.get(t, 0), p)
            for t, p in peso.items():
                self.inv[t].append((i, p))
        self.claves = sorted(self.inv)
        self.nombre_t = [terminos(d["nombre"]) for d in docs]
        self.titulo_t = [terminos(d["titulo"]) for d in docs]

    def buscar(self, consulta, tope=8):
        marcados = terminos_marcados(consulta)
        q = [t for t, _ in marcados]
        derivados = {t for t, d in marcados if d}
        if not q:
            return []
        n = len(self.docs)
        idf = lambda df: math.log(1 + n / df) if df else 0.0
        inutil = int(n * FRACCION_INUTIL)
        punt, porque, vistos = defaultdict(float), defaultdict(set), set()

        def por_prefijo(raiz):
            alguno = False
            k = bisect.bisect_left(self.claves, raiz)
            while k < len(self.claves) and self.claves[k].startswith(raiz):
                t = self.claves[k]
                if len(self.inv[t]) <= inutil:
                    alguno = True
                    w = idf(len(self.inv[t])) * DESCUENTO_PREFIJO
                    for doc, p in self.inv[t]:
                        punt[doc] += p * w
                        porque[doc].add(t)
                k += 1
            return alguno
        for t in q:
            if t in vistos:
                continue
            vistos.add(t)
            if t in derivados and len(t) >= 5 and t[-1] == "r":
                por_prefijo(t[:-1])
                continue
            if t in self.inv:
                ap = self.inv[t]
                if len(ap) > inutil:
                    continue
                w = idf(len(ap)) * (DESCUENTO_PREFIJO if t in derivados else 1.0)
                for doc, p in ap:
                    punt[doc] += p * w
                    porque[doc].add(t)
                continue
            if len(t) < PREFIJO_MINIMO:
                continue
            if por_prefijo(t):
                continue
            if len(t) >= 5 and t[-1] == "r" and t[-2] in "aei" and por_prefijo(t[:-1]):
                continue
            for p in PEGADOS_BUSCAR:
                if len(t) >= len(p) + PREFIJO_MINIMO and t.endswith(p) and por_prefijo(t[:-len(p)]):
                    break
        exacta = next((i for i in range(n) if self.nombre_t[i] == q), None)
        res = []
        for doc, p in punt.items():
            p *= 1 + 0.35 * (len(porque[doc]) - 1)
            if doc == exacta:
                p *= 100
            elif self.titulo_t[doc] and self.titulo_t[doc] == q:
                p *= 3
            res.append((p, self.docs[doc]["id"], sorted(porque[doc])))
        res.sort(key=lambda x: (-x[0], x[1]))
        return res[:tope]


def indice(ruta):
    docs = []
    for h in json.loads(Path(ruta).read_text())["herramientas"]:
        e = h.get("etiquetas", {})
        docs.append({"id": h["nombre"], "nombre": h["nombre"], "titulo": h.get("titulo", ""),
                     "etiquetas": [e.get("porPrefijo", "").replace("·", " "), e.get("propias", "")],
                     "cuerpo": h.get("descripcion", "")})
    return Indice(docs)


# --- los conjuntos ---------------------------------------------------------------
def conjuntos(nombres):
    perfil = json.loads((RAIZ / "datos/perfiles/vivo6.json").read_text())
    ren = perfil.get("renombres_anteriores", {})

    def limpiar(casos):
        out = []
        for frase, val in casos:
            v = sorted({ren.get(x, x) for x in val} & nombres)
            if v:
                out.append((frase, v))
        return out
    prg = json.loads((RAIZ / "src/app/tests/datos/preguntas.json").read_text())["casos"]
    ste = json.loads((RAIZ / "datos/ste-es.json").read_text())["entradas"]
    an = json.loads((RAIZ / "datos/tc-anotaciones.json").read_text())
    ban = [json.loads(l) for l in (RAIZ / "datos/orientacion.jsonl").read_text().splitlines() if l.strip()]
    return {
        "test": limpiar([(x["pregunta"], x["herramientas"]) for x in prg]),
        "intencion": limpiar([(f'{e["palabra"]} {s["objeto"]}', s.get("herramientas", []))
                              for e in ste for s in e.get("sentidos", []) if s.get("objeto")]),
        "cabina": limpiar([(a["frase"], [h]) for h, v in an.items() for a in v if a.get("frase") and "falta" not in a]),
        "banco": limpiar([(x["frase"], [x["herramienta"]] + x.get("aceptables", [])) for x in ban if x.get("herramienta")]),
    }


def medir(idx, casos):
    c = Counter()
    por_h = defaultdict(lambda: [0, 0])
    malos = []
    for frase, validas in casos:
        r = [x[1] for x in idx.buscar(frase, 8)]
        p = min((r.index(v) for v in validas if v in r), default=None)
        c["n"] += 1
        c["1"] += p == 0
        c["3"] += p is not None and p < 3
        c["8"] += p is not None
        for v in validas:
            por_h[v][0] += 1
            por_h[v][1] += p is not None
        if p is None:
            malos.append((frase, validas, r[:3]))
    return c, por_h, malos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inventario", default=str(RAIZ / "docs/mcp.json"))
    ap.add_argument("--por-herramienta", action="store_true", help="las peores de las frases de cabina, y quien les gana")
    ap.add_argument("--arnes", action="store_true", help="comparar con el McpIndice.cpp de verdad (compila con g++)")
    ap.add_argument("--nlohmann", help="carpeta con nlohmann/json.hpp (para --arnes)")
    args = ap.parse_args()
    idx = indice(args.inventario)
    sets = conjuntos({d["id"] for d in idx.docs})
    res = {}
    for nombre, casos in sets.items():
        c, por_h, malos = medir(idx, casos)
        res[nombre] = c
        n = c["n"]
        print(f"{nombre:10s} {n:5d} · primera {c['1'] / n:6.1%} · primeras 3 {c['3'] / n:6.1%} · primeras 8 {c['8'] / n:6.1%}"
              + ("   (solo medir)" if nombre == "banco" else ""))
        if nombre == "cabina" and args.por_herramienta:
            peores = sorted((v[1] / v[0], v[0], h) for h, v in por_h.items() if v[0] >= 8)[:20]
            for r, k, h in peores:
                print(f"      {r:5.0%} de {k:3d}  {h}")
            print("      les ganan:", Counter(x for _, _, top in malos for x in top[:1]).most_common(10))
    if args.arnes:
        fuente = Path(__file__).with_name("medir-busqueda-arnes.cpp")
        with tempfile.TemporaryDirectory() as tmp:
            binario = Path(tmp) / "arnes"
            inc = ["-I", args.nlohmann] if args.nlohmann else []
            # El buscador lee las reglas del idioma del perfil (ba::i18n, en ba_core): se enlaza
            # la biblioteca del build y lee assets/idiomas de la fuente (BA_I18N_DEV_PERFILES).
            nucleo = RAIZ / "build" / "src" / "core" / "libba_core.a"
            if not nucleo.exists():
                sys.exit(f"--arnes necesita {nucleo} (cmake --build build --target ba_core)")
            sistema = ["-framework", "Foundation", "-framework", "IOKit"] if sys.platform == "darwin" else ["-ldl", "-lpthread"]
            subprocess.run(["g++", "-std=c++20", "-O2", "-w", "-I", str(RAIZ / "src/app"), "-I", str(RAIZ / "src/core"),
                            *inc, str(fuente), str(RAIZ / "src/app/mcp/McpIndice.cpp"), str(nucleo), *sistema,
                            "-o", str(binario)], check=True)
            for nombre, casos in sets.items():
                arch = Path(tmp) / f"{nombre}.jsonl"
                arch.write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in casos))
                cpp = subprocess.run([str(binario), args.inventario, str(arch)], capture_output=True, text=True).stdout.split()
                c = res[nombre]
                py = [str(c["n"]), str(c["1"]), str(c["3"]), str(c["8"])]
                print(f"arnes {nombre:10s} C++ {' '.join(cpp)} · Python {' '.join(py)} · {'IGUALES' if cpp == py else 'DISTINTOS'}")


if __name__ == "__main__":
    main()
