#!/usr/bin/env python3
"""STE-ES de cabina: el diccionario y el verificador (ver docs/ste-es.md).

El alma de ASD-STE100 en castellano y para el corpus del modelo local: el
NUCLEO de cada frase (accion, objeto, cantidad) con palabras de un solo
significado; la superficie (voseo, muletillas, sin tildes, typos) libre.

    tools/entrenar/ste_es.py armar                 # el diccionario, con un agente -> datos/ste-es.json
    tools/entrenar/ste_es.py informe [--json x]    # las semillas contra STE-ES
    tools/entrenar/ste_es.py frase audio_set "bajale 4 a la musica"

Como modulo: `verificar(frase, herramienta)` -> lista de problemas (vacia = cumple).
Los agentes del bucle pasan por aca ademas de por la matematica.
"""
import argparse
import collections
import json
import re
import sys
import tempfile
import unicodedata
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
DICC = RAIZ / "datos" / "ste-es.json"
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

PREGUNTA = ("que", "cual", "cuales", "cuanto", "cuanta", "cuantos", "cuantas", "como", "donde", "quien",
            "quienes", "hay", "esta", "estan", "es", "sale", "se")
# lo que NO es el nucleo aunque se escriba igual que una forma del diccionario
# (sin tildes «para» es la preposicion y «parar»; «baja», el adjetivo y «bajar»)
NO_NUCLEO = {"para", "baja", "bajo", "alta", "alto", "sobre", "entre", "como", "mas", "menos", "nada", "todo", "toda"}
# verbos que llevan la accion en el infinitivo que sigue: «quiero mirar», «dejame escuchar»
MODALES = {"quiero", "queria", "necesito", "podes", "podrias", "dejame", "deja", "vamos", "vas", "hay", "tenes",
           "tengo", "andá", "anda", "proba", "fijate"}
# antes de una forma que sin tilde es igual al imperativo: indicativo o subordinada, no una orden
ANTES_NO_ORDEN = {"se", "que", "lo", "la", "le", "no", "quien", "cual", "donde",
                  # infinitivo despues de preposicion: «antes de armar», «para probar»
                  "de", "a", "al", "sin", "para", "por", "en", "con", "hasta", "tras"}
ENCLITICOS = ("selos", "selas", "selo", "sela", "melo", "mela", "telo", "tela", "les", "los", "las", "nos",
              "me", "te", "le", "lo", "la")
PRONOMBRE_SOLO = re.compile(r"\b(eso|esto|ese|esa|este|esta|aquello|lo|la)\s*[.!?]?\s*$")


def normal(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^\w\s{}]", " ", s)


def palabras(frase):
    return [w for w in normal(frase).split() if w]


class Diccionario:
    def __init__(self, ruta=DICC):
        d = json.loads(Path(ruta).read_text())
        self.acciones, self.polisemicas, self.muletillas = {}, {}, set()
        for e in d["entradas"]:
            if e["tipo"] == "accion":
                for f in e["formas"]:
                    self.acciones[normal(f).strip()] = e
            elif e["tipo"] == "polisemica":
                for f in e["formas"]:
                    self.polisemicas[normal(f).strip()] = e
            elif e["tipo"] == "muletilla":
                self.muletillas |= {normal(f).strip() for f in e["formas"]}

        self.infinitivos = {}
        for e in d["entradas"]:
            if e["tipo"] == "accion":
                self.infinitivos[normal(e["verbo"]).strip()] = ("accion", e)
            elif e["tipo"] == "polisemica":
                self.infinitivos[normal(e["palabra"]).strip()] = ("polisemica", e)

    def buscar(self, w):
        """(tipo, entrada) de una palabra: forma exacta, sin pronombres pegados, o infinitivo."""
        for cand in [w] + [w[:-len(x)] for x in ENCLITICOS if w.endswith(x) and len(w) - len(x) >= 3]:
            if cand in self.acciones:
                return "accion", self.acciones[cand]
            if cand in self.polisemicas:
                return "polisemica", self.polisemicas[cand]
            if cand in self.infinitivos:
                return self.infinitivos[cand]
            # imperativo de voseo sin tilde y sin pronombre: «analiza» -> analizar, «medi» -> medir
            for inf in (cand + "r", cand + "ar", cand + "er", cand + "ir"):
                if inf in self.infinitivos:
                    return self.infinitivos[inf]
        return None

    def nucleo(self, ws):
        """Las palabras de accion (y de muchos sentidos) de la frase, en orden: sin
        las que solo se escriben igual (preposiciones, indicativo despues de «se»/«que»)
        y saltando los modales hasta su infinitivo."""
        out = []
        for i, w in enumerate(ws):
            if len(w) <= 2 or w in NO_NUCLEO or w in MODALES or (i and ws[i - 1] in ANTES_NO_ORDEN):
                continue
            r = self.buscar(w)
            if r:
                out.append((r[0], w, r[1]))
        return out


_dic = None


def errores(frase, herramienta, falta=False):
    """Solo lo que invalida una frase de entrenamiento (R7 es aviso)."""
    return [p for p in verificar(frase, herramienta, falta) if not p.startswith("R7 aviso")]


def verificar(frase, herramienta, falta=False):
    """Los problemas de una frase de entrenamiento para su herramienta (vacio = cumple)."""
    global _dic
    _dic = _dic or Diccionario()
    ws = palabras(frase)
    sin_mul = [w for w in ws if w not in _dic.muletillas]
    prob = []
    if not 3 <= len(ws) <= 16:
        prob.append(f"R8 largo: {len(ws)} palabras (3 a 16)")
    consulta = (frase.strip().startswith("¿") or frase.strip().endswith("?")
                or (sin_mul and sin_mul[0] in PREGUNTA))
    if consulta:  # R7: una consulta se reconoce por su forma; el resto de las reglas es de las ordenes
        return prob
    nuc = _dic.nucleo(sin_mul)
    acciones = [x for x in nuc if x[0] == "accion"]
    if len({x[2]["verbo"] for x in acciones}) > 1:
        prob.append("R3 mas de una accion: " + ", ".join(sorted({x[2]["verbo"] for x in acciones})))
    for tipo, w, e in nuc:
        if tipo == "accion" and herramienta not in e.get("herramientas", []):
            prob.append(f"R1 «{w}» significa «{e['significado']}» ({', '.join(e['herramientas'][:4])}), no {herramienta}")
            break
    if not consulta and nuc and all(t == "polisemica" for t, _, _ in nuc):
        tipo, w, e = nuc[0]
        resto = sin_mul[sin_mul.index(w) + 1:] if w in sin_mul else []
        if len(resto) < 1 or PRONOMBRE_SOLO.search(" ".join(resto)) and not falta:
            prob.append(f"R2 «{w}» tiene muchos sentidos y no lleva el objeto que fija cual")
        elif herramienta not in {h for s in e.get("sentidos", []) for h in s.get("herramientas", [])}:
            prob.append(f"R2 «{w}» no tiene un sentido que lleve a {herramienta} en el diccionario")
    if not nuc and not consulta:
        # sin verbo ni pregunta: orden nominal («el multiview») o SINTOMA («se acopla el
        # mic»). El sintoma es valido en STE-ES (el operador apurado habla asi y STE100
        # tambien tiene reglas para lo descriptivo) si nombra el objeto y el problema:
        # queda como aviso, no como error
        prob.append("R7 aviso: sin verbo de accion ni consulta (orden nominal o sintoma)")
    return prob


def armar(args):
    import agente
    import lib_prompt as lp
    inv = lp.inventario()
    por = {h["nombre"]: h for h in inv["herramientas"]}
    nombres = lp.herramientas_reactivas(inv)
    sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
    inicio = {}
    choques = collections.defaultdict(set)
    for h in nombres:
        c = collections.Counter()
        for f in sem.get(h, {}).get("frases", []):
            ws = [w for w in palabras(f) if w not in ("che", "dale", "ya", "a", "ver", "bueno", "porfa")]
            if ws:
                c[ws[0]] += 1
                choques[ws[0]].add(h)
        inicio[h] = c.most_common(8)
    lote = {"herramientas": [{"nombre": h, "descripcion": por[h]["descripcion"][:300],
                              "arrancan_con": inicio[h]} for h in nombres],
            "choques": sorted(([w, sorted(hs)] for w, hs in choques.items() if len(hs) >= 4),
                              key=lambda x: -len(x[1]))}
    tmp = Path(tempfile.mkdtemp(prefix="ste-es-"))
    (tmp / "lote.json").write_text(json.dumps(lote, ensure_ascii=False, indent=1))
    t = agente.Trabajo("ste-es-diccionario.md", tmp / "lote.json", tmp / "salida.jsonl", ["tipo"])
    agente.correr_lotes([t], paralelo=1)
    entradas = [json.loads(l) for l in (tmp / "salida.jsonl").read_text().splitlines() if l.strip()]
    validas = set(nombres)
    problemas = []
    vistos = {}
    for e in entradas:
        for h in e.get("herramientas", []) + [h for s in e.get("sentidos", []) for h in s.get("herramientas", [])]:
            if h not in validas:
                problemas.append(f"herramienta desconocida {h}")
        if e["tipo"] == "accion":
            for f in e["formas"]:
                f = normal(f).strip()
                if f in vistos and vistos[f] != e["verbo"]:
                    problemas.append(f"«{f}» con dos significados ({vistos[f]}, {e['verbo']})")
                vistos[f] = e["verbo"]
    cubiertas = {h for e in entradas for h in e.get("herramientas", [])} | \
                {h for e in entradas for s in e.get("sentidos", []) for h in s.get("herramientas", [])}
    sin = sorted(validas - cubiertas)
    DICC.write_text(json.dumps({"_comentario": "STE-ES de cabina (docs/ste-es.md): lo arma tools/entrenar/ste_es.py armar;"
                                               " se cura como las semillas.", "entradas": entradas},
                               ensure_ascii=False, indent=1) + "\n")
    print(f"diccionario: {len(entradas)} entradas -> {DICC}")
    print(f"herramientas sin palabra propia: {len(sin)} {sin[:10]}")
    for p in problemas[:20]:
        print("  OJO", p)


def ampliar(args):
    """El diccionario se cura como las semillas: un agente toma lo que el
    verificador no pudo leer y decide si el diccionario crece (verbo nuevo,
    sentido nuevo) o si la FRASE esta mal (va a datos/ste-es-frases-mal.jsonl,
    con una propuesta en STE-ES, para el bucle del corpus)."""
    import agente
    import lib_prompt as lp
    global _dic
    inv = lp.inventario()
    por = {h["nombre"]: h for h in inv["herramientas"]}
    sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
    casos = []
    for h, v in sorted(sem.items()):
        for f in v.get("frases", []):
            for x in errores(f, h):
                if x.startswith(("R1", "R2")):
                    casos.append({"herramienta": h, "frase": f, "problema": x})
                    break
    print(f"casos sin leer: {len(casos)}")
    d = json.loads(DICC.read_text())
    resumen = [{k: e[k] for k in ("tipo", "verbo", "palabra", "significado", "herramientas") if k in e}
               | ({"sentidos": [x.get("objeto") for x in e.get("sentidos", [])]} if e["tipo"] == "polisemica" else {})
               for e in d["entradas"] if e["tipo"] in ("accion", "polisemica")]
    tmp = Path(tempfile.mkdtemp(prefix="ste-es-ampliar-"))
    trabajos = []
    for i in range(0, len(casos), 80):
        hs = sorted({c["herramienta"] for c in casos[i:i + 80]})
        lote = {"diccionario": resumen, "casos": casos[i:i + 80],
                "herramientas": [{"nombre": h, "descripcion": por[h]["descripcion"][:240]} for h in hs]}
        f = tmp / f"lote-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("ste-es-ampliar.md", f, tmp / f"salida-{i}.jsonl", ["tipo"]))
    agente.correr_lotes(trabajos, paralelo=4)
    validas = set(lp.herramientas_reactivas(inv))
    acciones = {normal(f).strip(): e["verbo"] for e in d["entradas"] if e["tipo"] == "accion" for f in e["formas"]}
    polis = {normal(e["palabra"]).strip(): e for e in d["entradas"] if e["tipo"] == "polisemica"}
    n_acc = n_sen = n_mal = rech = 0
    mal = []
    for t in trabajos:
        if not Path(t.salida).exists():
            continue
        for l in Path(t.salida).read_text().splitlines():
            try:
                e = json.loads(l)
            except json.JSONDecodeError:
                continue
            hs = e.get("herramientas", [])
            if any(h not in validas for h in hs) or (e.get("herramienta") and e["herramienta"] not in validas):
                rech += 1
                continue
            if e["tipo"] == "accion":
                formas = [normal(f).strip() for f in e.get("formas", [])]
                if any(f in acciones and acciones[f] != e["verbo"] for f in formas):
                    rech += 1  # ya tiene otro significado: no se duplica
                    continue
                d["entradas"].append(e)
                acciones.update({f: e["verbo"] for f in formas})
                n_acc += 1
            elif e["tipo"] == "sentido":
                pal = normal(e["palabra"]).strip()
                if pal not in polis:
                    polis[pal] = {"tipo": "polisemica", "palabra": e["palabra"], "formas": [], "sentidos": []}
                    d["entradas"].append(polis[pal])
                polis[pal]["formas"] = sorted(set(polis[pal]["formas"]) | set(e.get("formas", [])))
                polis[pal]["sentidos"].append({"objeto": e.get("objeto"), "ejemplo": e.get("ejemplo"), "herramientas": hs})
                n_sen += 1
            elif e["tipo"] == "frase_mal":
                mal.append(e)
                n_mal += 1
    DICC.write_text(json.dumps(d, ensure_ascii=False, indent=1) + "\n")
    (RAIZ / "datos/ste-es-frases-mal.jsonl").write_text("".join(json.dumps(m, ensure_ascii=False) + "\n" for m in mal))
    _dic = None
    print(f"diccionario: +{n_acc} acciones, +{n_sen} sentidos · frases mal: {n_mal} -> datos/ste-es-frases-mal.jsonl"
          f" · rechazadas: {rech}")


def informe(args):
    sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
    por_h, por_regla, ej = {}, collections.Counter(), collections.defaultdict(list)
    total = cumple = 0
    for h, v in sorted(sem.items()):
        n = ok = 0
        for f in v.get("frases", []):
            p = verificar(f, h)
            n += 1
            ok += not p
            for x in p:
                r = x.split()[0]
                por_regla[r] += 1
                if len(ej[r]) < 4:
                    ej[r].append(f"{h}: «{f}» — {x}")
        por_h[h] = (ok, n)
        total += n
        cumple += ok
    print(f"cumplen STE-ES: {cumple}/{total} ({100 * cumple / max(1, total):.0f}%)")
    for r, c in por_regla.most_common():
        print(f"  {r}: {c}")
        for e in ej[r]:
            print(f"     {e}")
    peores = sorted(por_h.items(), key=lambda x: x[1][0] / max(1, x[1][1]))[:10]
    print("herramientas con menos cumplimiento:", ", ".join(f"{h} {a}/{b}" for h, (a, b) in peores))
    if args.json:
        Path(args.json).write_text(json.dumps({"cumplen": [cumple, total], "por_regla": por_regla,
                                               "por_herramienta": por_h}, ensure_ascii=False, indent=1))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("armar")
    sub.add_parser("ampliar")
    i = sub.add_parser("informe")
    i.add_argument("--json")
    f = sub.add_parser("frase")
    f.add_argument("herramienta")
    f.add_argument("texto")
    a = ap.parse_args()
    if a.cmd == "armar":
        armar(a)
    elif a.cmd == "ampliar":
        ampliar(a)
    elif a.cmd == "informe":
        informe(a)
    else:
        print(verificar(a.texto, a.herramienta) or "cumple")


if __name__ == "__main__":
    main()
