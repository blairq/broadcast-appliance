#!/usr/bin/env python3
"""Afinar cada grupo de frases: diagnosticar con Frobenius, corregir, recalcular.

LA IDEA (decision de Jorge). Frobenius no decide que frase sale: SEÑALA que
frase esta mal y hacia donde tira, y esa frase se corrige atacando ese detalle.
Cada grupo termina con 15 a 20 frases. Despues de cada cambio se recalcula el
grupo: si se corrio hacia una vecina se lo realinea; si quedo muy apretado se
lo abre con formas de pedir distintas.

LAS CUENTAS (con los estados ocultos del modelo, como bucle-corpus):
  Q(a)      = min_b ||mu_a - mu_b||^2 / (||S_a||_F + ||S_b||_F)
              Fisher del grupo contra el mas peligroso; S = covarianza.
  puntaje   = (Q(a) - Q(a sin la frase)) / Q(a). Negativo: la frase empeora
              el grupo.
  tira_a    = la vecina cuyo termino de Q mas empeora por la frase: es el
              «detalle» que hay que atacar al reescribirla.
  dim       = dimension efectiva (razon de participacion). Baja = apretado:
              todas las frases dicen lo mismo de la misma forma.

DOS PASOS, con agentes sin conexion en el medio:

  afinar-grupos.py diagnosticar --modelo M --lotes DIR [--informe diag.json]
      escribe por lote: la ficha REAL de cada herramienta (docs/mcp.json), las
      de sus vecinas, sus frases con puntaje y a quien tiran, y que le falta.

  afinar-grupos.py aplicar --modelo M --candidatos c.jsonl [--aceptar]
      c.jsonl: {"herramienta", "tipo": "reescribe"|"nueva", "original"?, "frase"}
      Aplica de a una y recalcula el grupo cada vez:
        reescribe: entra si la frase nueva puntua mejor que la vieja y Q no cae
                   mas de un 2%. La vieja va a «podadas» (no se borra nada).
        nueva:     entra si el grupo tiene menos de 20, cae mas cerca de SU
                   grupo que de cualquier otro, no es casi igual a una que ya
                   esta, y Q no cae mas de un 2%.
      Sin --aceptar no escribe nada.
"""
import argparse
import collections
import datetime
import importlib.util
import json
import statistics
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
MINIMO, MAXIMO = 15, 20
TOLERANCIA_Q = 0.02      # una reescritura no puede bajar Q mas que esto
PASO_ABRIR = 0.05        # una frase nueva no puede bajar Q mas que esto...
PRESUPUESTO_ABRIR = 0.15  # ...ni el grupo entero mas que esto desde el inicio
UMBRAL_MALA = -0.01


def _bucle():
    spec = importlib.util.spec_from_file_location("bucle_corpus", Path(__file__).parent / "bucle-corpus.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Grupos:
    def __init__(self, bc, modelo, herramientas, semillas):
        import lib_prompt as lp
        self.med = bc.Medidor(modelo, Path.home() / ".cache/ba-nubes")
        self.t = self.med.torch
        self.propias = sorted(lp.herramientas_reactivas())
        hs = [l.strip() for l in Path(herramientas).read_text().splitlines() if l.strip()]
        self.frases, grupos = bc.cargar_grupos(self.med, semillas, hs, True, con_admin=True)
        self.P = {h: self.t.stack(v) for h, v in grupos.items()}
        self.otros = [o for o in self.P if len(self.P[o]) >= 3]
        self.mu = {o: self.P[o].mean(0) for o in self.P}
        self.F = {o: self.frob(self.P[o]) for o in self.otros}

    def frob(self, X):
        if len(X) < 2:
            return 0.0
        Xc = X - X.mean(0)
        return ((Xc @ Xc.T).norm() / (len(X) - 1)).item()

    def terminos(self, h, X):
        """q_b para cada vecina b: el cociente de Fisher contra ella."""
        mu, fa = X.mean(0), self.frob(X)
        return {o: ((mu - self.mu[o]) ** 2).sum().item() / (fa + self.F[o] + 1e-9)
                for o in self.otros if o != h}

    def Q(self, h, X):
        t = self.terminos(h, X)
        b = min(t, key=t.get)
        return t[b], b

    def dim(self, X):
        if len(X) < 2:
            return 0.0
        s = self.t.linalg.svdvals((X - X.mean(0)).float()) ** 2
        return (s.sum() ** 2 / (s ** 2).sum()).item()

    def diagnostico(self, h, X, textos):
        q0, vec = self.Q(h, X)
        t0 = self.terminos(h, X)
        filas = []
        for i, fr in enumerate(textos):
            X2 = self.t.cat([X[:i], X[i + 1:]])
            t2 = self.terminos(h, X2)
            q2 = min(t2.values())
            # a quien tira: la vecina cuyo termino MAS mejora si la frase no esta
            tira = max(t2, key=lambda o: (t2[o] - t0[o]) / (t0[o] + 1e-9))
            filas.append({"frase": fr, "puntaje": round((q0 - q2) / q0, 4), "tira_a": tira})
        return {"Q": round(q0, 4), "vecina": vec, "n": len(textos), "frob": round(self.frob(X), 4),
                "dim": round(self.dim(X), 2), "frases": filas}


def fichas():
    inv = {h["nombre"]: h for h in json.loads((RAIZ / "docs/mcp.json").read_text(encoding="utf-8"))["herramientas"]}

    def ficha(t, corta=False):
        if t not in inv:
            return (f"  {t} — lo que no es de ninguna herramienta propia: se le pasa al asistente grande"
                    if t == "derivar" else f"  {t}")
        h = inv[t]
        if corta:
            return f"  {t} — {h['titulo']}: {h['descripcion'][:300]}"
        props = h.get("esquema", {}).get("properties", {})
        req = h.get("esquema", {}).get("required", [])
        ps = "\n".join(f"      - {k}{' (obligatorio)' if k in req else ''}: {v.get('description', '')}"
                       + (f" valores: {', '.join(map(str, v['enum']))}" if "enum" in v else "")
                       for k, v in props.items()) or "      (ninguno)"
        return f"HERRAMIENTA: {t} — {h['titulo']}\n  QUE HACE: {h['descripcion']}\n  PARAMETROS:\n{ps}"
    return ficha


def diagnosticar(args):
    bc = _bucle()
    g = Grupos(bc, args.modelo, args.herramientas, args.semillas)
    sem = json.loads(Path(args.semillas).read_text(encoding="utf-8"))["herramientas"]
    diag = {}
    for h in g.propias:
        e = sem.get(h, {})
        textos = e.get("frases", []) + e.get("correctivas", [])
        if len(textos) < 2:
            diag[h] = {"Q": None, "n": len(textos), "frases": [{"frase": f, "puntaje": 0, "tira_a": None} for f in textos]}
            continue
        X = g.t.stack([g.P[h][g.frases[h].index(f)] for f in textos])
        diag[h] = g.diagnostico(h, X, textos)
    dims = [d["dim"] for d in diag.values() if d.get("dim")]
    med_dim = statistics.median(dims)
    for h, d in diag.items():
        d["falta"] = max(0, MINIMO - d["n"])
        d["sobra"] = max(0, d["n"] - MAXIMO)
        d["apretado"] = bool(d.get("dim") is not None and d.get("dim", 0) < 0.6 * med_dim)
        d["malas"] = [f for f in d["frases"] if f["puntaje"] < UMBRAL_MALA]
    if args.informe:
        Path(args.informe).write_text(json.dumps(diag, ensure_ascii=False, indent=1), encoding="utf-8")
    ficha = fichas()
    # lo que se rechazo en la vuelta anterior (el ultimo registro de afinar-grupos
    # en la bitacora), para que el agente no vuelva a proponer lo mismo
    previos = collections.defaultdict(list)
    bit = RAIZ / "datos/bitacora-corpus.jsonl"
    if bit.exists():
        for l in reversed(bit.read_text(encoding="utf-8").splitlines()):
            r = json.loads(l)
            if "afinar-grupos" in str(r.get("nota", "")):
                for x in r.get("rechazadas", []):
                    previos[x["herramienta"]].append((x["frase"], x["motivo"]))
                break
    # lotes para los agentes: solo los grupos con algo que hacer
    trabajo = [h for h, d in diag.items() if d["malas"] or d["falta"] or d["sobra"] or d["apretado"]]
    if args.solo_cortos:
        trabajo = [h for h in trabajo if diag[h]["falta"]]
    Path(args.lotes).mkdir(parents=True, exist_ok=True)
    n_lotes = max(1, min(args.agentes, len(trabajo)))
    lotes = [trabajo[i::n_lotes] for i in range(n_lotes)]
    for k, hs in enumerate(lotes, 1):
        out = []
        for h in hs:
            d = diag[h]
            vecs = sorted({f["tira_a"] for f in d["malas"] if f["tira_a"]} | ({d.get("vecina")} - {None}))
            out += ["=" * 72, ficha(h), "  VECINAS CON LAS QUE SE CONFUNDE:"]
            out += [ficha(v, corta=True) for v in vecs] or ["  (ninguna)"]
            out.append(f"  ESTADO: {d['n']} frases · calidad Q {d['Q']} contra {d.get('vecina')}"
                       f"{' · APRETADO (todas dicen lo mismo de la misma forma)' if d['apretado'] else ''}")
            out.append("  FRASES (puntaje: negativo = empeora el grupo; tira_a = hacia que vecina lo corre):")
            for f in d["frases"]:
                marca = "  <-- ARREGLAR" if f in d["malas"] else ""
                out.append(f"    [{f['puntaje']:+.3f} tira a {f['tira_a']}] {f['frase']}{marca}")
            pide = []
            if d["malas"]:
                pide.append(f"reescribí las {len(d['malas'])} marcadas ARREGLAR")
            if d["falta"]:
                pide.append(f"escribí {d['falta']} frases nuevas")
            if d["apretado"] and not d["falta"]:
                pide.append("escribí 3 frases nuevas con formas de pedir bien distintas")
            if d["sobra"]:
                pide.append(f"el grupo tiene {d['sobra']} de mas: de las que no marcamos, decí cuales son"
                            " casi repetidas (tipo «sobra»)")
            if previos.get(h):
                out.append("  RECHAZADAS LA VUELTA ANTERIOR (no repetir el error):")
                out += [f"    «{fr}» — {m}" for fr, m in previos[h][:8]]
            out.append("  PEDIDO: " + "; ".join(pide))
        (Path(args.lotes) / f"afinar-lote-{k}.txt").write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"grupos: {len(diag)} · con trabajo: {len(trabajo)} · lotes: {n_lotes}")
    print(f"frases que empeoran su grupo: {sum(len(d['malas']) for d in diag.values())}")
    print(f"faltan frases: {sum(1 for d in diag.values() if d['falta'])} grupos"
          f" · sobran: {sum(1 for d in diag.values() if d['sobra'])} · apretados:"
          f" {sum(1 for d in diag.values() if d['apretado'])}")


def aplicar(args):
    bc = _bucle()
    cm = bc._corpus_mod()
    g = Grupos(bc, args.modelo, args.herramientas, args.semillas)
    examen = json.loads(bc.CORPUS_EXAMEN.read_text(encoding="utf-8"))["casos"]
    prohibidas = [set(cm.sin_tildes(c["pregunta"].lower()).split()) for c in examen]
    banco = [set(cm.sin_tildes(json.loads(l)["frase"].lower()).split())
             for l in (RAIZ / "datos/orientacion.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    d = json.loads(Path(args.semillas).read_text(encoding="utf-8"))
    sem = d["herramientas"]
    cands = [json.loads(l) for l in Path(args.candidatos).read_text(encoding="utf-8").splitlines() if l.strip()]
    por = collections.defaultdict(list)
    for c in cands:
        por[c["herramienta"]].append(c)
    resumen, aprobadas, quitadas, rechazadas = [], [], [], []
    for h, cs in sorted(por.items()):
        if h not in sem:
            continue
        e = sem[h]
        textos = e.get("frases", []) + e.get("correctivas", [])
        X = g.t.stack([g.P[h][g.frases[h].index(f)] if f in g.frases.get(h, []) else g.med.h(f) for f in textos])
        q_ini = g.Q(h, X)[0] if len(textos) >= 2 else None
        n_ini, d_ini = len(textos), g.dim(X)
        # primero las reescrituras (arreglan), despues las nuevas (completan), y
        # al final las que sobran: salen solo hasta dejar el grupo en 20, y la
        # que sale es la que menos sostiene al grupo entre las señaladas.
        orden = {"reescribe": 0, "nueva": 1, "sobra": 2}
        for c in sorted(cs, key=lambda c: orden.get(c["tipo"], 1)):
            if c["tipo"] == "sobra":
                o = c.get("original")
                if len(textos) <= MAXIMO or o not in textos:
                    continue
                i = textos.index(o)
                X = g.t.cat([X[:i], X[i + 1:]])
                textos = textos[:i] + textos[i + 1:]
                quitadas.append((h, o))
                aprobadas.append((h, o, "sobra (casi repetida)"))
                continue
            fr = c["frase"].strip()
            if cm.parecido(fr, prohibidas) or (banco and cm.parecido(fr, banco)):
                rechazadas.append((h, fr, "se parece al examen o al banco"))
                continue
            if fr in textos:
                rechazadas.append((h, fr, "ya esta"))
                continue
            v = g.med.h(fr)
            vn = v / v.norm()
            q, _ = g.Q(h, X) if len(X) >= 2 else (None, None)
            if c["tipo"] == "reescribe":
                if c.get("original") not in textos:
                    rechazadas.append((h, fr, "la original ya no esta"))
                    continue
                i = textos.index(c["original"])
                X2 = X.clone()
                X2[i] = v
                t_sin = g.t.cat([X[:i], X[i + 1:]])
                q_sin = g.Q(h, t_sin)[0]
                q2 = g.Q(h, X2)[0]
                viejo = (q - q_sin) / q
                nuevo = (q2 - q_sin) / q2
                if nuevo > viejo and q2 >= q * (1 - TOLERANCIA_Q):
                    X, textos = X2, textos[:i] + [fr] + textos[i + 1:]
                    aprobadas.append((h, fr, f"reescribe ({viejo:+.3f} -> {nuevo:+.3f})"))
                    quitadas.append((h, c["original"]))
                else:
                    rechazadas.append((h, fr, f"no mejora ({viejo:+.3f} -> {nuevo:+.3f}, Q {q:.3f}->{q2:.3f})"))
            else:
                if len(textos) >= MAXIMO:
                    rechazadas.append((h, fr, "el grupo ya tiene 20"))
                    continue
                cerca = min(((v - g.mu[o]).norm().item(), o) for o in g.P if o != h)
                propio = (v - X.mean(0)).norm().item()
                # Con menos de 8 frases el centro del grupo no representa nada (es
                # un hueco: el modelo no aprendio el concepto) y toda frase nueva
                # «cae mas cerca» de otro. Paso con imagen_analizar: 4 frases y
                # 36 rechazos de pedidos correctos. Ahi esta prueba no se aplica.
                if cerca[0] < propio and len(textos) >= 8:
                    rechazadas.append((h, fr, f"cae mas cerca de {cerca[1]}"))
                    continue
                # casi copia se mira en el TEXTO: en los estados ocultos de un
                # modelo entrenado, todas las frases de una herramienta colapsan
                # (coseno > 0.97 casi siempre) y ese criterio rechazaba todo.
                if cm.parecido(fr, [set(cm.sin_tildes(t.lower()).split()) for t in textos]):
                    rechazadas.append((h, fr, "casi igual a una que ya esta"))
                    continue
                X2 = g.t.cat([X, v[None, :]])
                q2 = g.Q(h, X2)[0] if q is not None else None
                # ABRIR TIENE UN PRESUPUESTO. Una forma de pedir distinta siempre
                # ensancha un poco el grupo y baja Q; exigir que no baje nada no
                # deja abrir nunca un grupo apretado. Se permite hasta un 5% por
                # frase y un 15% en total desde como estaba el grupo. Un hueco (menos
                # de 8) se deja abrir sin presupuesto hasta llegar a 8.
                if q is not None and len(textos) >= 8 and (q2 < q * (1 - PASO_ABRIR) or q2 < q_ini * (1 - PRESUPUESTO_ABRIR)):
                    rechazadas.append((h, fr, f"corre el grupo (Q {q:.3f}->{q2:.3f})"))
                    continue
                X, textos = X2, textos + [fr]
                aprobadas.append((h, fr, "nueva"))
        q_fin = g.Q(h, X)[0] if len(textos) >= 2 else None
        resumen.append((h, n_ini, len(textos), q_ini, q_fin, d_ini, g.dim(X)))
        if args.aceptar:
            corr = [f for f in e.get("correctivas", []) if f in textos]
            e["frases"] = [f for f in textos if f not in corr]
            e["correctivas"] = corr
            e.setdefault("podadas", [])
            e["podadas"] += [o for hh, o in quitadas if hh == h and o not in e["podadas"]]
    print(f"{'herramienta':22} {'n':>9} {'Q':>15} {'dim':>11}")
    for h, a, b, qa, qb, da, db in resumen:
        fq = lambda x: f"{x:.3f}" if x is not None else "  -  "
        print(f"{h:22} {a:3d} -> {b:3d}   {fq(qa)} -> {fq(qb)}   {da:4.1f} -> {db:4.1f}")
    print(f"\naprobadas {len(aprobadas)} · rechazadas {len(rechazadas)}")
    motivos = collections.Counter(m.split(" (")[0] for _, _, m in rechazadas)
    print("rechazos:", dict(motivos.most_common()))
    if args.aceptar:
        Path(args.semillas).write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")
        reg = {"fecha": datetime.datetime.now().isoformat(timespec="seconds"), "modelo": str(args.modelo),
               "objetivo": None, "nota": "afinar-grupos: correccion guiada por Frobenius, 15 a 20 por grupo",
               "propuestas_por_herramienta": dict(collections.Counter(c["herramienta"] for c in cands)),
               "aceptadas_por_herramienta": dict(collections.Counter(h for h, _, _ in aprobadas)),
               "aprobadas": [{"herramienta": h, "frase": f} for h, f, m in aprobadas if not m.startswith("sobra")],
               "quitadas": [{"herramienta": h, "frase": f} for h, f in quitadas],
               "rechazadas": [{"herramienta": h, "frase": f, "motivo": m} for h, f, m in rechazadas]}
        with open(RAIZ / "datos/bitacora-corpus.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(reg, ensure_ascii=False) + "\n")
        print("-> escrito en semillas-reactivas.json (las reemplazadas, en «podadas»)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paso", choices=["diagnosticar", "aplicar"])
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--herramientas", required=True)
    ap.add_argument("--semillas", default=str(RAIZ / "datos/semillas-reactivas.json"))
    ap.add_argument("--lotes", help="diagnosticar: carpeta donde escribir los lotes")
    ap.add_argument("--agentes", type=int, default=4)
    ap.add_argument("--informe")
    ap.add_argument("--solo-cortos", action="store_true", help="diagnosticar: lotes solo para los grupos con menos de 15")
    ap.add_argument("--candidatos")
    ap.add_argument("--aceptar", action="store_true")
    args = ap.parse_args()
    (diagnosticar if args.paso == "diagnosticar" else aplicar)(args)


if __name__ == "__main__":
    main()
