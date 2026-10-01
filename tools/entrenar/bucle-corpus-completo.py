#!/usr/bin/env python3
"""El corpus de entrenamiento, generado y mejorado en bucle, con todos los metodos.

Para un PERFIL (que herramientas del MCP, agrupadas por categoria: ver
datos/perfiles/) arma y mejora las frases semilla hasta que el modelo deja de
mejorar en el banco. Lo que pide criterio lo hacen agentes SIN CONEXION
(agente.py: solo ven su lote); lo que decide si un cambio entra es matematica
medida con un modelo (el «medidor»), nunca la opinion del agente.

UNA VUELTA
  1 cobertura  herramientas del perfil con menos de MINIMO frases: un agente las
               escribe con la ficha real; un juez ciego las filtra (entra la
               que el juez asigna a esa herramienta).
  2 sentido    cada frase sin revisar, contra la ficha real: sin sentido, pide
               algo que la herramienta no hace, es de otra, o ambigua. Se
               reescribe, se mueve o va a «podadas» (nunca se borra).
  3 separar    CDNV direccional entre grupos con el medidor (bucle-corpus
               --choques). Un agente escribe frases que separan cada conjunto;
               --evaluar acepta las que caen en su grupo sin acercarlo a otro;
               --limpiar saca lo que empuja hacia otro. Mientras bajen los pares.
  4 afinar     Frobenius (afinar-grupos): que frase corre su grupo y hacia que
               vecina; un agente ataca ESE detalle; se aplica de a un cambio
               recalculando el grupo. Grupos entre MINIMO y MAXIMO.
  5 medidor    entrena un modelo con el corpus nuevo (fabricar-modelo, hasta
               medir): curva del banco, examen y margen.
  6 margen     AUM con ese modelo: las frases que el decodificador resuelve con
               otra herramienta (o casi) se reescriben y se verifican con el
               mismo margen antes de entrar.
CORTA cuando la vuelta no mejora el banco por encima del ruido (RUIDO aciertos
propios a igual sesgo) o al llegar a --vueltas.

    tools/entrenar/bucle-corpus-completo.py --perfil reactivo --base Qwen/Qwen3.5-4B \\
        --nombre corpus-q35-4b [--medidor <adaptador>] [--vueltas 3] [--solo cobertura,sentido]

NUNCA toca el examen ni el banco de orientacion: los lee para medir y como
guarda contra copias. Todo cambio va a datos/bitacora-corpus.jsonl; lo
reemplazado, a «podadas» de su herramienta. El commit queda a cargo de quien
lo corre.
"""
import argparse
import collections
import datetime
import importlib.util
import json
import os
import random
import re
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
PY = RAIZ / ".venv-entrenar/bin/python"
SEMILLAS = RAIZ / "datos/semillas-reactivas.json"
BITACORA = RAIZ / "datos/bitacora-corpus.jsonl"
ORIENTACION = RAIZ / "datos/orientacion.jsonl"
EXAMEN = RAIZ / "src/app/tests/datos/preguntas.json"
MINIMO, MAXIMO = 15, 20
RUIDO = 8           # aciertos propios (de ~394): dos entrenamientos iguales difieren 2 a 4 puntos
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(E))
import agente  # noqa: E402


def log(*a):
    print("\033[35m[bucle]\033[0m", *a, flush=True)


def _cm():
    spec = importlib.util.spec_from_file_location("cm", RAIZ / "tools/corpus-entrenamiento.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Corpus:
    def __init__(self, args):
        import lib_prompt as lp
        self.lp, self.a = lp, args
        self.inv = {h["nombre"]: h for h in json.loads((RAIZ / "docs/mcp.json").read_text())["herramientas"]}
        self.propias = lp.herramientas_reactivas()
        self.dir = Path(args.trabajo).expanduser() / args.nombre
        self.dir.mkdir(parents=True, exist_ok=True)
        self.todas = self.dir / "todas.txt"
        self.todas.write_text("\n".join(sorted(self.inv)) + "\n")
        self.est_f = self.dir / "estado.json"
        self.est = json.loads(self.est_f.read_text()) if self.est_f.exists() else {"vuelta": 0, "revisadas": []}
        cm = _cm()
        self.cm = cm
        self.proh = [set(cm.sin_tildes(c["pregunta"].lower()).split())
                     for c in json.loads(EXAMEN.read_text())["casos"]]
        self.banco = [set(cm.sin_tildes(json.loads(l)["frase"].lower()).split())
                      for l in ORIENTACION.read_text().splitlines() if l.strip()]
        self.rol = (E / "consignas/_rol.md").read_text()

    # --- utilidades -------------------------------------------------------
    def guardar_estado(self):
        self.est_f.write_text(json.dumps(self.est, ensure_ascii=False, indent=1))

    def sem(self):
        return json.loads(SEMILLAS.read_text(encoding="utf-8"))

    def escribir_sem(self, d):
        SEMILLAS.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")

    def frases(self, d, h):
        e = d["herramientas"].get(h, {})
        return e.get("frases", []) + e.get("correctivas", [])

    def limpia(self, fr, ya=()):
        """Las guardas de siempre: ni el examen, ni el banco, ni repetida."""
        return (fr and not self.cm.parecido(fr, self.proh) and not self.cm.parecido(fr, self.banco)
                and not self.cm.parecido(fr, [set(self.cm.sin_tildes(t.lower()).split()) for t in ya]))

    def bitacora(self, nota, aprobadas=(), quitadas=(), rechazadas=()):
        reg = {"fecha": datetime.datetime.now().isoformat(timespec="seconds"),
               "modelo": f"bucle-corpus-completo/{self.a.nombre}", "objetivo": None, "nota": nota,
               "propuestas_por_herramienta": {}, "aceptadas_por_herramienta":
               dict(collections.Counter(h for h, _ in aprobadas)),
               "aprobadas": [{"herramienta": h, "frase": f} for h, f in aprobadas],
               "quitadas": [{"herramienta": h, "frase": f} for h, f in quitadas],
               "rechazadas": [{"herramienta": h, "frase": f, "motivo": m} for h, f, m in rechazadas]}
        with open(BITACORA, "a", encoding="utf-8") as f:
            f.write(json.dumps(reg, ensure_ascii=False) + "\n")

    def ficha(self, t, corta=False):
        if t not in self.inv:
            return f"  {t}: lo que no hace ninguna herramienta propia (se le pasa al asistente grande)"
        h = self.inv[t]
        if corta:
            return f"  {t} — {h['titulo']}: {h['descripcion'][:300]}"
        props = h.get("esquema", {}).get("properties", {})
        req = h.get("esquema", {}).get("required", [])
        ps = "\n".join(f"      - {k}{' (obligatorio)' if k in req else ''}: {v.get('description', '')}"
                       + (f" valores: {', '.join(map(str, v['enum']))}" if "enum" in v else "")
                       for k, v in props.items()) or "      (ninguno)"
        return f"HERRAMIENTA: {t} — {h['titulo']}\n  QUE HACE: {h['descripcion']}\n  PARAMETROS:\n{ps}"

    def vecinas(self, t):
        """Las de su familia (mismo prefijo) y las que hoy se pisan con ella."""
        fam = t.split("_")[0]
        v = {o for o in self.propias if o != t and o.split("_")[0] == fam}
        ch = self.dir / "choques.json"
        if ch.exists():
            for p in json.loads(ch.read_text())["pares"]:
                if t in (p["a"], p["b"]):
                    v.add(p["b"] if p["a"] == t else p["a"])
        return sorted(v)

    def lotes(self, tools, por_lote=12):
        tools = list(tools)
        n = max(1, (len(tools) + por_lote - 1) // por_lote)
        return [tools[i::n] for i in range(n)]

    def correr_agentes(self, consigna, textos, campos, etapa, datos=None):
        d = self.dir / f"v{self.est['vuelta']}-{etapa}"
        d.mkdir(exist_ok=True)
        trab = []
        for i, t in enumerate(textos, 1):
            lote = d / f"lote-{i}.txt"
            lote.write_text(t, encoding="utf-8")
            trab.append(agente.Trabajo(consigna, lote, d / f"salida-{i}.jsonl", campos,
                                       dict({"ROL": self.rol}, **(datos or {}))))
        log(f"{etapa}: {len(trab)} agentes")
        agente.correr_lotes(trab, paralelo=self.a.paralelo)
        return agente.leer([t.salida for t in trab])

    def juez(self, cands, etapa):
        """Clasificacion ciega: entra la que el juez asigna a la herramienta para la
        que se escribio, eligiendo entre ella, sus vecinas y NINGUNA."""
        random.Random(3).shuffle(cands)
        tools = sorted({c["herramienta"] for c in cands} | {v for c in cands for v in self.vecinas(c["herramienta"])})
        texto = ["HERRAMIENTAS POSIBLES:"] + [self.ficha(t, corta=True) for t in tools] + ["", "FRASES (id | frase | opciones):"]
        for i, c in enumerate(cands):
            ops = [c["herramienta"]] + self.vecinas(c["herramienta"])
            random.Random(i).shuffle(ops)
            texto.append(f"{i} | {c['frase']} | {', '.join(ops)}, NINGUNA")
        v = self.correr_agentes("juez.md", ["\n".join(texto)], ["id", "herramienta"], etapa + "-juez")
        ver = {int(r["id"]): r["herramienta"] for r in v}
        return [c for i, c in enumerate(cands) if ver.get(i) == c["herramienta"]]

    # --- etapas -----------------------------------------------------------
    def cobertura(self):
        d = self.sem()
        faltan = {h: MINIMO - len(self.frases(d, h)) for h in self.propias if len(self.frases(d, h)) < MINIMO}
        if not faltan:
            log("cobertura: todas tienen", MINIMO)
            return
        textos = []
        for grupo in self.lotes(sorted(faltan)):
            t = []
            for h in grupo:
                t += ["=" * 70, self.ficha(h), "  VECINAS:"] + [self.ficha(v, corta=True) for v in self.vecinas(h)]
                t += ["  FRASES QUE YA TIENE:"] + [f"    {f}" for f in self.frases(d, h)]
                t += [f"  PEDIDO: escribí {2 * faltan[h]} frases nuevas"]
            textos.append("\n".join(t))
        cands = [c for c in self.correr_agentes("semillas.md", textos, ["herramienta", "frase"], "cobertura")
                 if c["herramienta"] in faltan and self.limpia(c["frase"], self.frases(d, c["herramienta"]))]
        ok = self.juez(cands, "cobertura")
        d = self.sem()
        apro = []
        for c in ok:
            h = c["herramienta"]
            if len(self.frases(d, h)) < MINIMO and self.limpia(c["frase"], self.frases(d, h)):
                d["herramientas"].setdefault(h, {"frases": []})["frases"].append(c["frase"])
                apro.append((h, c["frase"]))
        self.escribir_sem(d)
        self.bitacora("cobertura: frases nuevas con la ficha real, filtradas por el juez ciego", apro)
        log(f"cobertura: {len(apro)} frases nuevas en {len(faltan)} herramientas (juez: {len(ok)}/{len(cands)})")

    def sentido(self):
        d = self.sem()
        hechas = set(map(tuple, self.est["revisadas"]))
        items = [(h, f) for h in self.propias for f in self.frases(d, h) if (h, f) not in hechas]
        if not items:
            log("sentido: nada nuevo para revisar")
            return
        lista = "LAS HERRAMIENTAS QUE EXISTEN (para saber si una frase es de otra):\n" + "\n".join(
            f"  {t} — {self.inv[t]['titulo']}" for t in self.propias)
        por_h = collections.defaultdict(list)
        ids = {}
        for i, (h, f) in enumerate(items):
            por_h[h].append((i, f))
            ids[i] = (h, f)
        textos = []
        for grupo in self.lotes(sorted(por_h), 20):
            t = [lista, ""]
            for h in grupo:
                t += ["=" * 70, self.ficha(h), "  FRASES (id | frase):"] + [f"    {i} | {f}" for i, f in por_h[h]]
            textos.append("\n".join(t))
        v = self.correr_agentes("sentido.md", textos, ["id", "veredicto"], "sentido")
        d = self.sem()
        apro, quit = [], []
        for r in v:
            i = int(r["id"])
            if i not in ids or r["veredicto"] == "ok":
                continue
            h, f = ids[i]
            e = d["herramientas"][h]
            campo = "frases" if f in e.get("frases", []) else ("correctivas" if f in e.get("correctivas", []) else None)
            if not campo:
                continue
            e.setdefault("podadas", []).append(f)
            quit.append((h, f))
            nueva = (r.get("reescritura") or "").strip()
            if nueva and self.limpia(nueva, self.frases(d, h)):
                e[campo][e[campo].index(f)] = nueva
                apro.append((h, nueva))
            elif r["veredicto"] == "otra" and r.get("otra") in self.propias:
                e[campo].remove(f)
                d["herramientas"].setdefault(r["otra"], {"frases": []})["frases"].append(f)
                apro.append((r["otra"], f))
            else:
                e[campo].remove(f)
        self.escribir_sem(d)
        self.est["revisadas"] = sorted(set(map(tuple, self.est["revisadas"])) |
                                       {(h, f) for h in self.propias for f in self.frases(d, h)})
        self.guardar_estado()
        self.bitacora("sentido: revisadas contra la ficha real", apro, quit)
        log(f"sentido: {len(items)} revisadas · {len(quit)} corregidas")

    def _bucle(self, *extra, salida=None):
        cmd = [str(PY), str(E / "bucle-corpus.py"), "--modelo", self.medidor, "--herramientas", str(self.todas),
               "--con-admin"] + [str(x) for x in extra]
        r = subprocess.run(cmd, cwd=RAIZ, capture_output=True, text=True,
                           env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"))
        if r.returncode:
            sys.exit(f"bucle-corpus fallo: {r.stderr[-600:]}")
        return r.stdout

    def separar(self):
        ch = self.dir / "choques.json"
        antes = None
        for ronda in range(self.a.rondas):
            self._bucle("--choques", ch)
            pares = json.loads(ch.read_text())
            n = len(pares["pares"])
            log(f"separar: ronda {ronda + 1}, {n} pares que se pisan")
            if not n or (antes is not None and n >= antes):
                break
            antes = n
            textos = []
            for conj in pares["conjuntos"]:
                ts = [t for t in conj if t in self.propias]
                for grupo in self.lotes(ts, 14):
                    ps = [p for p in pares["pares"] if p["a"] in grupo or p["b"] in grupo]
                    t = ["HERRAMIENTAS:"] + [self.ficha(x) for x in grupo]
                    t += ["", "PARES QUE HOY SE CONFUNDEN (tienen que quedar bien distintos):"]
                    t += [f"- {p['a']}  <->  {p['b']}" for p in ps]
                    textos.append("\n".join(t))
            cands = [c for c in self.correr_agentes("separar.md", textos, ["herramienta", "frase"],
                                                    f"separar-r{ronda + 1}", {"N": 6})
                     if c["herramienta"] in self.inv]
            f = self.dir / f"v{self.est['vuelta']}-separar-r{ronda + 1}.json"
            f.write_text(json.dumps(cands, ensure_ascii=False))
            self._bucle("--evaluar", f, "--orientacion", ORIENTACION, "--aceptar")
            self._bucle("--limpiar", "--aceptar")

    def afinar(self):
        for ronda in range(self.a.rondas):
            ld = self.dir / f"v{self.est['vuelta']}-afinar-r{ronda + 1}"
            r = subprocess.run([str(PY), str(E / "afinar-grupos.py"), "diagnosticar", "--modelo", self.medidor,
                                "--herramientas", str(self.todas), "--lotes", str(ld), "--agentes",
                                str(self.a.paralelo)], cwd=RAIZ, capture_output=True, text=True,
                               env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"))
            log("afinar:", " · ".join(l for l in r.stdout.splitlines() if l.startswith(("grupos", "frases", "faltan"))))
            d = self.sem()
            fuera = [h for h in self.propias if not MINIMO <= len(self.frases(d, h)) <= MAXIMO]
            if ronda and not fuera:
                break
            textos = [p.read_text() for p in sorted(ld.glob("afinar-lote-*.txt"))]
            if not textos:
                break
            cands = self.correr_agentes("afinar.md", textos, ["herramienta", "tipo"], f"afinar-r{ronda + 1}")
            cf = ld / "candidatos.jsonl"
            cf.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cands
                                  if c.get("tipo") in ("reescribe", "nueva", "sobra")))
            r = subprocess.run([str(PY), str(E / "afinar-grupos.py"), "aplicar", "--modelo", self.medidor,
                                "--herramientas", str(self.todas), "--candidatos", str(cf), "--aceptar"],
                               cwd=RAIZ, capture_output=True, text=True, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"))
            log("afinar:", next((l for l in r.stdout.splitlines() if l.startswith("aprobadas")), r.stderr[-300:]))

    def entrenar_medidor(self):
        nombre = f"{self.a.nombre}-v{self.est['vuelta']}"
        r = subprocess.run([str(PY), str(E / "fabricar-modelo.py"), "--base", self.a.base, "--nombre", nombre,
                            "--perfil", self.a.perfil, "--hasta", "medir", "--trabajo", str(self.dir / "modelos")],
                           cwd=RAIZ)
        if r.returncode:
            sys.exit("no se pudo entrenar el medidor")
        man = json.loads((self.dir / "modelos" / nombre / "manifiesto.json").read_text())
        self.medidor = str(self.dir / "modelos" / nombre / "adaptador")
        self.est.setdefault("medidores", []).append({"vuelta": self.est["vuelta"], "adaptador": self.medidor,
                                                     "medidas": man.get("medidas", {}).get("bf16")})
        self.est["medidor"] = self.medidor
        self.guardar_estado()

    def margen(self):
        md = Path(self.medidor).parent / "medidas" / "margen.json"
        if not md.exists():
            log("margen: el medidor no tiene test de margen")
            return
        m = json.loads(md.read_text())
        xs = sorted([(x["margen"], h, x["frase"], x.get("rival")) for h, v in m.items() for x in v if x["margen"] < 1])
        if not xs:
            log("margen: todas las frases dan bien")
            return
        t = []
        for i, (mg, h, f, riv) in enumerate(xs):
            t += [f"=== CASO {i}  (el modelo {'elige' if mg < 0 else 'casi elige'} {riv})", f"FRASE: «{f}»",
                  f"ES DE: {self.ficha(h, corta=True).strip()}", f"RIVAL: {self.ficha(riv or 'derivar', corta=True).strip()}", ""]
        resp = self.correr_agentes("margen.md", ["\n".join(t)], ["caso", "reescrituras"], "margen")
        cand = [{"herramienta": xs[int(r["caso"])][1], "frase": c, "caso": int(r["caso"])}
                for r in resp if int(r["caso"]) < len(xs) for c in r["reescrituras"]]
        cf = self.dir / f"v{self.est['vuelta']}-margen-cand.jsonl"
        cf.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cand))
        out = self.dir / f"v{self.est['vuelta']}-margen-medido.json"
        subprocess.run([str(PY), str(E / "margen-frases.py"), "--modelo", self.medidor, "--frases", str(cf),
                        "--salida", str(out)], cwd=RAIZ, env=dict(os.environ, CUDA_VISIBLE_DEVICES="0"),
                       capture_output=True)
        med = {(h, x["frase"]): x["margen"] for h, v in json.loads(out.read_text()).items() for x in v}
        d = self.sem()
        apro, quit = [], []
        for i, (mg, h, f, _) in enumerate(xs):
            e = d["herramientas"][h]
            campo = "frases" if f in e.get("frases", []) else ("correctivas" if f in e.get("correctivas", []) else None)
            if not campo:
                continue
            buenas = sorted((c["frase"] for c in cand if c["caso"] == i and med.get((h, c["frase"]), -99) >= 1
                             and self.limpia(c["frase"], self.frases(d, h))), key=lambda c: -med[(h, c)])
            if buenas:
                e[campo][e[campo].index(f)] = buenas[0]
                e.setdefault("podadas", []).append(f)
                apro.append((h, buenas[0]))
                quit.append((h, f))
        self.escribir_sem(d)
        self.bitacora("margen (AUM): reescritas y verificadas con margen >= 1 en el medidor", apro, quit)
        log(f"margen: {len(xs)} con margen < 1 · {len(apro)} arregladas")

    # --- el bucle ---------------------------------------------------------
    def correr(self):
        solo = set(self.a.solo.split(",")) if self.a.solo else None
        self.medidor = self.a.medidor or self.est.get("medidor")
        anterior = None
        for _ in range(self.a.vueltas):
            self.est["vuelta"] += 1
            self.guardar_estado()
            log(f"=========== VUELTA {self.est['vuelta']} (perfil {self.a.perfil}) ===========")
            if not self.medidor:
                log("no hay medidor: se entrena uno con el corpus actual")
                self.entrenar_medidor()
            for nombre, fn in (("cobertura", self.cobertura), ("sentido", self.sentido),
                               ("separar", self.separar), ("afinar", self.afinar)):
                if not solo or nombre in solo:
                    fn()
            if solo and "medidor" not in solo and "margen" not in solo:
                continue
            self.entrenar_medidor()
            if not solo or "margen" in solo:
                self.margen()
            b = (self.est["medidores"][-1].get("medidas") or {}).get("banco", {}).get("0", {})
            propias = b.get("precision", [0])[0]
            log(f"vuelta {self.est['vuelta']}: propias bien {propias} · derivado {b.get('derivado')}")
            if anterior is not None and propias < anterior + RUIDO:
                log("no mejoro por encima del ruido: corta")
                break
            anterior = propias
        log(f"listo. Medidor final: {self.medidor} · estado: {self.est_f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perfil", default="reactivo")
    ap.add_argument("--base", default="Qwen/Qwen3.5-4B", help="el modelo con el que se mide (y que se entrena)")
    ap.add_argument("--nombre", required=True)
    ap.add_argument("--trabajo", default="~/entrenamientos/corpus")
    ap.add_argument("--medidor", help="adaptador ya entrenado para medir la primera vuelta (si no, se entrena)")
    ap.add_argument("--vueltas", type=int, default=3)
    ap.add_argument("--rondas", type=int, default=3, help="rondas de separar y de afinar por vuelta")
    ap.add_argument("--paralelo", type=int, default=4, help="agentes a la vez")
    ap.add_argument("--solo", help="etapas a correr, separadas por coma (cobertura,sentido,separar,afinar,medidor,margen)")
    args = ap.parse_args()
    os.environ["BA_PERFIL"] = args.perfil
    Corpus(args).correr()


if __name__ == "__main__":
    main()
