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
idf, prefijos, imperativos y vacias, leidos del campo «buscador» del perfil del
idioma), sobre docs/mcp.json: nombre, etiquetas (familias + propias), titulo y
descripcion (la app suma ademas `details`, que el inventario no tiene). Si se
toca McpIndice.cpp, hay que tocar esto igual: --arnes compila
tools/dev/medir-busqueda-arnes.cpp contra el McpIndice.cpp de verdad y compara
los numeros (tienen que dar iguales).

--idioma <código> mide como lo ve un operador en ese idioma: el titulo traducido
(i18n/ba_<código>.ts), las etiquetas del original MAS las traducidas, la
descripcion en el original (es lo que se registra, ver traducirParaVer) y las
reglas del idioma con las del original sumadas (McpIndice.cpp, sumarDelOriginal).
Los conjuntos siguen en castellano; --preguntas ARCHIVO mide con otras
([[frase, herramienta], ...]).

--sin-etiquetas mide lo que ve un buscador que solo lee nombre, titulo y
descripcion (el tool_search de un cliente con exposicion diferida);
--palabras-clave le suma a la descripcion la linea de etiquetas propias.

    tools/docs/mcp-inventario.py --desde-codigo $PWD/docs/mcp.json   # despues de tocar etiquetas
    tools/dev/medir-busqueda.py [--por-herramienta] [--idioma en] [--arnes --nlohmann DIR]
"""
import argparse
import bisect
import html
import json
import math
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
PERFILES = RAIZ / "assets" / "idiomas"

# --- el buscador (McpIndice.cpp) -------------------------------------------------
PESO_NOMBRE, PESO_ETIQUETA, PESO_TITULO, PESO_CUERPO = 6.0, 5.0, 3.0, 0.6
DESCUENTO_PREFIJO, PREFIJO_MINIMO, FRACCION_INUTIL = 0.45, 4, 0.4


def perfil(codigo):
    return json.loads((PERFILES / f"{codigo}.json").read_text(encoding="utf-8"))


def buscador_de(p):
    """El campo «buscador» del perfil o el del primero de sus respaldos (buscadorDe)."""
    b = p.get("buscador")
    for r in p.get("respaldo", []):
        if isinstance(b, dict):
            break
        b = perfil(r).get("buscador")
    return b if isinstance(b, dict) else {}


class Reglas:
    """Las del campo «buscador» (reglasDe)."""

    def __init__(self, b):
        im = b.get("imperativo", {}) if isinstance(b.get("imperativo"), dict) else {}
        self.letras = dict(b.get("letras", {}))
        self.sin_tildes = list(b.get("sinTildes", {}).items())
        self.vacias = set(b.get("vacias", []))
        self.excepciones = set(im.get("excepciones", []))
        self.agudas = list(im.get("agudas", {}).items())
        self.sufijo = im.get("sufijo", "")
        self.letras_minimas = im.get("letrasMinimas", 3)
        self.pronombres = im.get("pronombres", [])
        self.pronombres_sin_tilde = set(im.get("pronombresSinTilde", []))
        self.vocales_sin_tilde = {v for v in im.get("vocalesSinTilde", []) if len(v) == 1}
        self.letras_del_verbo = im.get("letrasDelVerbo", 4)
        self.infinitivo = b.get("infinitivo", [])
        self.pegados = b.get("pegados", [])

    def es_infinitivo(self, t):
        return any(t.endswith(f) for f in self.infinitivo)

    def sumar_del_original(self, o):
        """sumarDelOriginal: las letras, las tildes y las vacías del original."""
        if self.letras:
            for k, v in o.letras.items():
                self.letras.setdefault(k, v)
        for par in o.sin_tildes:
            if all(x[0] != par[0] for x in self.sin_tildes):
                self.sin_tildes.append(par)
        self.vacias |= o.vacias


def reglas(codigo):
    p = perfil(codigo)
    r = Reglas(buscador_de(p))
    if not p.get("fuente") and not p.get("pseudo"):
        for f in sorted(PERFILES.glob("*.json")):
            o = json.loads(f.read_text(encoding="utf-8"))
            if o.get("fuente"):
                r.sumar_del_original(Reglas(buscador_de(o)))
    return r


R = reglas("es")


def bytes_(s):
    return len(s.encode("utf-8"))


def letra_unicode(c):
    cp = ord(c)
    if cp <= 0xBF or cp in (0xD7, 0xF7):
        return False
    return not (0x2000 <= cp <= 0x2BFF or 0x3000 <= cp <= 0x303F or 0xFE30 <= cp <= 0xFE4F or 0xFF00 <= cp <= 0xFF0F)


def minuscula(s):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def sin_tildes(s):
    s = minuscula(s)
    for de, a in R.sin_tildes:
        s = s.replace(de, a)
    return s


def imperativo(w):
    """pará -> parar, poné -> poner, bajale -> bajar, mostrámela -> mostrar (con las del perfil)."""
    if not R.agudas or w in R.excepciones:
        return w
    for t, v in R.agudas:
        if len(w) >= R.letras_minimas and w.endswith(t):
            return w[:-len(t)] + v + R.sufijo
    for p in R.pronombres:
        if not w.endswith(p) or len(w) < len(p) + R.letras_del_verbo:
            continue
        base = w[:-len(p)]
        for t, v in R.agudas:
            if base.endswith(t):
                return base[:-len(t)] + v + R.sufijo
        if p in R.pronombres_sin_tilde and base[-1] in R.vocales_sin_tilde:
            return base + R.sufijo
        return w
    return w


def terminos_marcados(s):
    """[(termino, salio_de_un_imperativo)], como terminosMarcados()."""
    out, cur = [], []

    def cerrar():
        if cur:
            w = "".join(cur)
            antes = sin_tildes(w)
            t = sin_tildes(imperativo(w))
            if antes not in R.vacias and bytes_(t) >= 2 and t not in R.vacias:
                out.append((t, t != antes))
        cur.clear()
    for c in minuscula(s):
        if "a" <= c <= "z" or "0" <= c <= "9":
            cur.append(c)
        elif ord(c) < 0x80:
            cerrar()
        elif not R.letras:
            if letra_unicode(c):
                cur.append(c)
            else:
                cerrar()
        elif c in R.letras:
            cur.append(R.letras[c])
        else:
            cerrar()
    cerrar()
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
            if t in derivados and bytes_(t) >= 5 and R.es_infinitivo(t):
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
            if bytes_(t) < PREFIJO_MINIMO:
                continue
            if por_prefijo(t):
                continue
            if bytes_(t) >= 5 and R.es_infinitivo(t) and por_prefijo(t[:-1]):
                continue
            for p in R.pegados:
                if bytes_(t) >= bytes_(p) + PREFIJO_MINIMO and t.endswith(p) and por_prefijo(t[:-len(p)]):
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


def catalogo(codigo):
    """{contexto: {original: traducción}} de i18n/ba_<código>.ts (solo las terminadas)."""
    tr = defaultdict(dict)
    ts = RAIZ / "i18n" / f"ba_{codigo}.ts"
    if not ts.exists():
        return tr
    for ctx, cuerpo in re.findall(r"<context>\s*<name>([^<]+)</name>(.*?)</context>", ts.read_text(encoding="utf-8"), re.S):
        for src, attrs, t in re.findall(r"<source>(.*?)</source>.*?<translation([^>]*)>(.*?)</translation>", cuerpo, re.S):
            if "unfinished" not in attrs and "obsolete" not in attrs and "vanished" not in attrs and t:
                tr[ctx][html.unescape(src)] = html.unescape(t)
    return tr


def filas_etiquetas():
    """{clave: [fila, ...]} de McpEtiquetas.cpp, fila por fila (se traducen de a una)."""
    sys.path.insert(0, str(RAIZ / "tools" / "docs"))
    import mcp_codigo  # noqa: E402
    txt = (RAIZ / "src/app/mcp/McpEtiquetas.cpp").read_text(encoding="utf-8")
    toks = mcp_codigo.tokens(txt)
    resolver = mcp_codigo._resolvedor(txt, toks)
    fam, prop = defaultdict(list), defaultdict(list)
    for tabla, destino in (("kFamilias", fam), ("kPropias", prop)):
        for f in mcp_codigo.tabla(txt, toks, resolver, tabla, "Fila"):
            if isinstance(f.get("clave"), str) and isinstance(f.get("terminos"), str):
                destino[f["clave"]].append(f["terminos"])
    return fam, prop


def indice(ruta, idioma="es", sin_etiquetas=False, palabras_clave=False):
    tr = catalogo(idioma) if idioma != "es" else defaultdict(dict)
    fam, prop = filas_etiquetas() if idioma != "es" else ({}, {})
    docs = []
    for h in json.loads(Path(ruta).read_text())["herramientas"]:
        e = h.get("etiquetas", {})
        etiquetas = [e.get("porPrefijo", "").replace("·", " "), e.get("propias", "")]
        if idioma != "es":
            # las del original MAS las traducidas, fila por fila (etiquetasDe)
            filas = [f for pre, fs in fam.items() if h["nombre"].startswith(pre) for f in fs] + prop.get(h["nombre"], [])
            etiquetas += [tr["mcp-etiquetas"].get(f, "") for f in filas]
        titulo = h.get("titulo", "")
        cuerpo = h.get("descripcion", "")
        if palabras_clave and e.get("propias"):
            cuerpo += "\n" + e["propias"]
        docs.append({"id": h["nombre"], "nombre": h["nombre"], "titulo": tr["mcp"].get(titulo, titulo),
                     "etiquetas": [] if sin_etiquetas else etiquetas, "cuerpo": cuerpo})
    return Indice(docs)


# --- los conjuntos ---------------------------------------------------------------
def conjuntos(nombres):
    perfil_ = json.loads((RAIZ / "datos/perfiles/vivo6.json").read_text())
    ren = perfil_.get("renombres_anteriores", {})

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
    global R
    ap = argparse.ArgumentParser()
    ap.add_argument("--inventario", default=str(RAIZ / "docs/mcp.json"))
    ap.add_argument("--por-herramienta", action="store_true", help="las peores de las frases de cabina, y quien les gana")
    ap.add_argument("--idioma", default="es", help="medir como lo ve un operador en ese idioma (código de assets/idiomas)")
    ap.add_argument("--preguntas", help="otro conjunto: JSON [[frase, herramienta], ...]")
    ap.add_argument("--sin-etiquetas", action="store_true", help="solo nombre, titulo y descripcion")
    ap.add_argument("--palabras-clave", action="store_true", help="la descripcion con la linea de etiquetas propias")
    ap.add_argument("--arnes", action="store_true", help="comparar con el McpIndice.cpp de verdad (compila con g++)")
    ap.add_argument("--nlohmann", help="carpeta con nlohmann/json.hpp (para --arnes)")
    args = ap.parse_args()
    R = reglas(args.idioma)
    idx = indice(args.inventario, args.idioma, args.sin_etiquetas, args.palabras_clave)
    sets = conjuntos({d["id"] for d in idx.docs})
    if args.preguntas:
        sets = {Path(args.preguntas).stem: [(f, [h] if isinstance(h, str) else h)
                                            for f, h in json.loads(Path(args.preguntas).read_text())]}
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
