#!/usr/bin/env python3
"""Bucle de mejora del corpus: una frase entra solo si mejora la geometria.

Documentacion completa, con la matematica y el procedimiento: docs/bucle-corpus.md

QUE HACE. Mide, adentro de un modelo ya entrenado, donde cae cada frase de
entrenamiento, y usa esa medicion para decidir que frases nuevas conviene
agregar. Quien PROPONE las frases es una persona o una LLM grande; quien DECIDE
es la cuenta. Asi una frase que suena bien pero cae en el lugar equivocado no
entra.

EL CRITERIO, que no es inventado: la CDNV direccional (arxiv 2603.03530).

    CDNV_dir(i,j) = (var_i sobre u_ij + var_j sobre u_ij) / (2 . |mu_i - mu_j|^2)

    u_ij = (mu_i - mu_j) / |mu_i - mu_j|   el eje que separa los dos grupos

Es la varianza de los grupos SOBRE EL EJE QUE LOS SEPARA dividida por la
distancia al cuadrado entre sus centros. El paper demuestra que el error del
clasificador por centro mas cercano queda acotado por 4 . CDNV_dir. Equivale a
1/Fisher^2 con el Fisher proyectado de nubes.py.

    Fisher   CDNV_dir   cota del error
      3        0,11         44%
      4        0,06         25%
      6        0,028        11%     <- objetivo por defecto
     10        0,01          4%

EL BUEN TAMANO DE UN GRUPO, que es la otra mitad y tambien sale del paper: no
hay que colapsar los grupos. Una buena representacion es ANGOSTA sobre el eje
hacia cada vecina y ANCHA en todas las demas direcciones. Por eso la variedad
que suma una frase se mide FUERA de los ejes hacia las vecinas.

LOS TRES FILTROS DE UNA CANDIDATA

  1. cae en su grupo      su centro mas cercano es el de su herramienta
  2. suma variedad util   fraccion de su desvio FUERA de los ejes hacia las
                          vecinas y de las direcciones que el grupo ya cubre
  3. no acerca vecinas    con la frase agregada, la CDNV_dir con ninguna vecina
                          puede subir mas de la tolerancia Y quedar por arriba
                          del objetivo

Ademas: no puede parecerse a una pregunta del examen (el examen NUNCA entra al
bucle) y no puede repetir una semilla.

MODOS
  --diagnosticar          estado del grupo: pares sobre el objetivo, grupos
                          angostos, centros de choque. Con --json, la entrada
                          para quien propone.
  --evaluar c.json        puntua candidatas [{"herramienta", "frase"}, ...]
  --aceptar               ademas, escribe las aprobadas en el archivo de semillas

  tools/entrenar/bucle-corpus.py --modelo <m> --diagnosticar
  tools/entrenar/bucle-corpus.py --modelo <m> --evaluar c.json --aceptar

PARA OTRO CONJUNTO DE HERRAMIENTAS: --semillas <archivo> y --herramientas
<lista o archivo>. Por defecto usa el repertorio reactivo y sus semillas.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
SEMILLAS = RAIZ / "datos/semillas-reactivas.json"
DERIVACIONES = RAIZ / "datos/derivaciones.json"
JERGA = RAIZ / "datos/jerga.json"
ENTRENAR = RAIZ / "datos/entrenamiento/entrenar.jsonl"
CORPUS_EXAMEN = RAIZ / "src/app/tests/datos/preguntas.json"
VALIDAR = RAIZ / "datos/entrenamiento/validar.jsonl"


def _corpus_mod():
    """Las mismas guardas del generador de corpus, sin copiarlas: una sola forma
    de decidir si una frase se parece al examen."""
    spec = importlib.util.spec_from_file_location(
        "corpus_entrenamiento", RAIZ / "tools/corpus-entrenamiento.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Medidor:
    """Carga el modelo una vez y da el estado oculto de cualquier frase.

    El estado es el residual PRE-norma del ultimo token, pasado por la RMSNorm
    final con su peso: exactamente lo que la cabeza de salida multiplica. Se
    cachea por frase en disco, con una firma del modelo y del prompt en el
    nombre: si cambia cualquiera de los dos, el cache no se reusa.
    """

    def __init__(self, modelo_dir, cache_dir):
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer
        from peft import PeftModel
        import lib_prompt as lp
        self.torch = torch
        inv = lp.inventario()
        self.cabeza, self.cola = lp.partes(inv)
        cfg = json.loads((Path(modelo_dir) / "adapter_config.json").read_text())
        self.tok = AutoTokenizer.from_pretrained(modelo_dir)
        m = AutoModelForCausalLM.from_pretrained(
            cfg["base_model_name_or_path"], dtype=torch.bfloat16, device_map="cuda:0")
        # Fusionar el adaptador: 21,4 -> 11,8 ms por frase (medido).
        self.m = PeftModel.from_pretrained(m, modelo_dir).merge_and_unload()
        self.m.eval()
        self.dev = self.m.device
        norma = self.m.model.norm
        self.peso = norma.weight.float()
        self.pre = {}
        norma.register_forward_pre_hook(lambda _m, e: self.pre.__setitem__("x", e[0].detach()))
        # El prefijo del prompt es identico para todas las frases: se lee una vez
        # y se reusa su cache KV. Por frase se procesan diez tokens.
        e = self.tok(self.cabeza, return_tensors="pt").to(self.dev)
        with torch.no_grad():
            sal = self.m(**e, use_cache=True)
        self.kv, self.n_cab = sal.past_key_values, e.input_ids.shape[1]
        firma = hashlib.sha1((str(Path(modelo_dir).resolve()) + self.cabeza + self.cola)
                             .encode()).hexdigest()[:12]
        self.cache_f = Path(cache_dir) / f"h-{firma}.pt"
        self.cache = torch.load(self.cache_f) if self.cache_f.exists() else {}
        self.sucio = False

    def h(self, frase):
        if frase in self.cache:
            return self.cache[frase]
        torch = self.torch
        r = self.tok(frase + self.cola, add_special_tokens=False, return_tensors="pt").to(self.dev)
        with torch.no_grad():
            self.m(input_ids=r.input_ids, past_key_values=self.kv, use_cache=True)
        self.kv.crop(self.n_cab)
        x = self.pre["x"][0, -1, :].float()
        v = ((x / (x.pow(2).mean().sqrt() + 1e-6)) * self.peso).cpu()
        self.cache[frase] = v
        self.sucio = True
        return v

    def guardar(self):
        if self.sucio:
            self.cache_f.parent.mkdir(parents=True, exist_ok=True)
            self.torch.save(self.cache, self.cache_f)
            self.sucio = False


class Geometria:
    """Los grupos (nubes) y las cuentas sobre ellos."""

    def __init__(self, torch, grupos, vecinas=5):
        self.t = torch
        self.P = {h: torch.stack(v) for h, v in grupos.items()}
        self.k = vecinas
        self._recalcular()

    def _recalcular(self):
        self.hs = sorted(self.P)
        self.mu = {h: P.mean(0) for h, P in self.P.items()}
        self.C = self.t.stack([self.mu[h] for h in self.hs])

    def vecinas(self, h, mu=None):
        """Las k mas cercanas. Con `mu`, alrededor de ese centro y no del actual."""
        mu = self.mu[h] if mu is None else mu
        d = (self.C - mu).norm(dim=1)
        return [self.hs[i] for i in self.t.argsort(d).tolist() if self.hs[i] != h][:self.k]

    def vecinas_del_cambio(self, h, P2):
        """Las vecinas ANTES y DESPUES de cambiar el grupo.

        Mirar solo las de antes fue un bug que acepto cambios que empeoraban: si
        sacar o agregar una frase mueve el centro hacia una vecina nueva, esa
        vecina no se miraba y el cambio parecia bueno. Paso con speaker_presenter:
        tres quitas «aprobadas» que en el saldo real lo llevaron de 0,198 a 0,206.
        """
        return list(dict.fromkeys(self.vecinas(h) + self.vecinas(h, P2.mean(0))))

    def cdnv(self, a, b, Pa=None):
        """CDNV direccional simetrica entre dos grupos. Pa reemplaza al grupo a
        (para medir como quedaria con una frase agregada)."""
        Pa = self.P[a] if Pa is None else Pa
        Pb = self.P[b]
        d = Pa.mean(0) - Pb.mean(0)
        n = d.norm()
        if n < 1e-6:
            return float("inf")
        u = d / n
        va = (Pa @ u).var(unbiased=True).item() if len(Pa) > 1 else 0.0
        vb = (Pb @ u).var(unbiased=True).item() if len(Pb) > 1 else 0.0
        return (va + vb) / (2 * n.item() ** 2)

    def peor_cdnv(self, h, Pa=None):
        """La peor CDNV del grupo contra TODOS los demas medibles, no solo contra
        las vecinas mas cercanas por centro.

        La CDNV depende de la varianza sobre el eje, no solo de la distancia
        entre centros: un grupo que no esta entre los cinco mas cercanos puede
        tener la peor CDNV. Mirar solo las vecinas cercanas dejo pasar cambios
        malos. Contra todos es una proyeccion por grupo: con 292 herramientas
        sigue siendo instantaneo.
        """
        peor, quien = 0.0, None
        for o in self.hs:
            if o == h or o not in self.medibles:
                continue
            c = self.cdnv(h, o, Pa=Pa)
            if c > peor:
                peor, quien = c, o
        return peor, quien

    def dim(self, h, P=None):
        """Dimension efectiva: razon de participacion de los autovalores. En
        cuantas direcciones independientes varia el grupo."""
        P = self.P[h] if P is None else P
        if len(P) < 2:
            return 0.0
        s = self.t.linalg.svdvals((P - P.mean(0)).float()) ** 2
        return (s.sum() ** 2 / (s ** 2).sum()).item() if s.sum() > 0 else 0.0

    def mas_cercano(self, v):
        return self.hs[int(self.t.argmin((self.C - v).norm(dim=1)))]

    def novedad(self, h, v):
        """Que fraccion del desvio de v queda FUERA de los ejes hacia las vecinas
        y de las direcciones que el grupo ya cubre.

        Los ejes hacia las vecinas se sacan porque ahi la varianza CUESTA: es la
        que sube la CDNV direccional. Las direcciones propias del grupo se sacan
        porque una frase que cae ahi es una variante de algo que ya esta.
        """
        t = self.t
        mu = self.mu[h]
        r = v - mu
        if r.norm() < 1e-6:
            return 0.0
        ejes = []
        for o in self.vecinas(h):
            d = mu - self.mu[o]
            if d.norm() > 1e-6:
                ejes.append(d / d.norm())
        P = self.P[h]
        if len(P) > 1:
            _, _, Vt = t.linalg.svd((P - mu).float(), full_matrices=False)
            m = max(1, math.ceil(self.dim(h)))
            ejes += list(Vt[:m])
        if not ejes:
            return 1.0
        Q, _ = t.linalg.qr(t.stack(ejes).T)          # base ortonormal
        fuera = r - Q @ (Q.T @ r)
        return (fuera.norm() / r.norm()).item()

    def agregar(self, h, v):
        self.P[h] = self.t.cat([self.P[h], v[None, :]])
        self._recalcular()


def cargar_grupos(med, semillas, herramientas, con_derivar, jerga=JERGA):
    """Las semillas de cada herramienta, como las junta el generador de corpus.

    Incluye los GIROS de la jerga: el generador los mezcla con las semillas con
    peso normal, y hay herramientas cuyas frases vienen casi todas de ahi. Sin
    ellos, la primera version de esto dejaba 15 herramientas sin medir.
    """
    d = json.loads(Path(semillas).read_text(encoding="utf-8"))["herramientas"]
    giros = {}
    if jerga and Path(jerga).exists():
        giros = json.loads(Path(jerga).read_text(encoding="utf-8")).get("giros", {})
    frases = {}
    for h in herramientas:
        e = d.get(h, {})
        fs = list(dict.fromkeys(e.get("frases", []) + e.get("correctivas", [])
                                + giros.get(h, [])))
        if fs:
            frases[h] = fs
    if con_derivar and DERIVACIONES.exists():
        # EL GRUPO DERIVAR ES EL QUE EL MODELO VIO. Las frases escritas a mano son
        # casi todas de FUERA del aparato (el clima, un cafe); lo que el modelo
        # aprendio a derivar son sobre todo pedidos ADMINISTRATIVOS, generados
        # desde las herramientas que quedan fuera del repertorio y que viven solo
        # en el corpus generado. Con las de a mano solas, el centro de derivar
        # quedaba lejos de lo que hay que derivar, y un pedido administrativo
        # «fallaba» contra un blanco que el modelo nunca tuvo.
        dv = json.loads(DERIVACIONES.read_text(encoding="utf-8"))
        fs = [f for k, v in dv.items() if not k.startswith("_") for f in v]
        if ENTRENAR.exists():
            fs += [json.loads(l)["frase"] for l in ENTRENAR.read_text(encoding="utf-8").splitlines()
                   if l.strip() and json.loads(l)["herramienta"] == "derivar"]
        frases["derivar"] = list(dict.fromkeys(fs))
    grupos = {h: [med.h(f) for f in fs] for h, fs in frases.items()}
    med.guardar()
    return frases, grupos


def diagnosticar(geo, objetivo, frases):
    # TODOS los pares medibles. Con 86 grupos son 3.655; con 292, unos 42.000,
    # y siguen siendo segundos: cada uno es una proyeccion sobre un eje.
    med_ = sorted(geo.medibles)
    pares = [(geo.cdnv(a, b), a, b) for i, a in enumerate(med_) for b in med_[i + 1:]]
    pares.sort(reverse=True)
    sobre = [p for p in pares if p[0] > objetivo]
    cuenta = collections.Counter()
    for _, a, b in sobre:
        cuenta[a] += 1
        cuenta[b] += 1
    dims = {h: geo.dim(h) for h in geo.hs if h in geo.medibles}
    cdnvs = [p[0] for p in pares]
    # LAS CULPABLES. Para un par pegado, las semillas de cada lado que caen del
    # lado AJENO del punto medio sobre el eje que los separa. Son las que hay que
    # reescribir o sacar: agregar frases a un centro de choque no lo despega,
    # sacar las que lo empujan si.
    t = geo.t
    culpables = []
    for c, a, b in pares[:15]:
        u = geo.mu[a] - geo.mu[b]
        u = u / u.norm()
        medio = ((geo.mu[a] + geo.mu[b]) / 2) @ u
        for lado, otro in ((a, b), (b, a)):
            sg = 1 if lado == a else -1
            proy = (geo.P[lado] @ u - medio) * sg
            for i in t.nonzero(proy < 0).flatten().tolist():
                culpables.append({"herramienta": lado, "hacia": otro,
                                  "frase": frases[lado][i], "cdnv_par": c})
    import statistics
    res = {
        "pares_medidos": len(pares),
        "pares_sobre_objetivo": len(sobre),
        "cdnv_mediana": statistics.median(cdnvs),
        "cdnv_peor": cdnvs[0] if cdnvs else 0.0,
        "dim_mediana": statistics.median(dims.values()),
        "peores_pares": [{"a": a, "b": b, "cdnv": c, "fisher": 1 / math.sqrt(c) if c > 0 else 0,
                          "cota_error": min(1.0, 4 * c)} for c, a, b in pares[:15]],
        "centros_de_choque": [{"herramienta": h, "pares_sobre_objetivo": n,
                               "dim": dims[h], "frases": len(frases.get(h, []))}
                              for h, n in cuenta.most_common(8) if n >= 2],
        "angostos": [{"herramienta": h, "dim": dims[h], "frases": len(frases.get(h, []))}
                     for h in sorted(dims, key=dims.get)[:10]],
        "culpables": culpables,
        # Todos los pares, para comparar rondas SOBRE LOS MISMOS PARES.
        "todos_los_pares": {f"{a}|{b}": c for c, a, b in pares},
        "dims": dims,
        "sin_semillas_suficientes": sorted(h for h in geo.hs if h not in geo.medibles),
    }
    return res


def historial(bitacora):
    """Propuestas y aceptadas por herramienta, a lo largo de todas las rondas."""
    prop, ok = collections.Counter(), collections.Counter()
    if bitacora and Path(bitacora).exists():
        for l in Path(bitacora).read_text(encoding="utf-8").splitlines():
            if not l.strip():
                continue
            r = json.loads(l)
            for h, n in r.get("propuestas_por_herramienta", {}).items():
                prop[h] += n
            for h, n in r.get("aceptadas_por_herramienta", {}).items():
                ok[h] += n
    return prop, ok


def orientar(geo, med, ori, frases, objetivo, bitacora):
    """DONDE SE PIERDE, y por que. Es lo que orienta el trabajo.

    Se pasa el banco de orientacion por el modelo y cada frase se asigna al grupo
    del centro mas cercano. Cada error se atribuye al par (verdadera, predicha).

    La prioridad de un par es errores x CDNV: un par feo al que ningun pedido se
    acerca no se toca; un par mediano donde se pierden pedidos es urgente. El
    diagnostico sin banco ordena por lo feo; con banco, por lo que cuesta.

    Despues, el TRIAGE: cada par priorizado se clasifica por su causa, porque
    cada causa se arregla distinto y confundirlas fue caro:

      etiquetado   las semillas del grupo caen mayormente en OTRO: su nube esta
                   centrada en otra cosa (operator_feed tenia dos frases del
                   registro de eventos). Lo decide una persona leyendo.
      contaminado  hay semillas del lado ajeno del par: SACARLAS o reescribirlas.
      hueco        las frases del banco que fallan caen fuera de lo que el grupo
                   cubre: faltan FORMAS de pedirlo. Se escriben guiadas por ellas.
      inseparable  ya se propusieron varias frases para el grupo y casi ninguna
                   entro: el modelo no tiene un concepto propio para esto. Es una
                   decision de producto —fusionar, derivar, aceptar las dos—.
    """
    t = geo.t
    # DERIVAR NO SE MIDE CON CENTROS. Es una clase de muchos modos —el clima, un
    # cafe, configurar una cuenta y otras 200 tareas administrativas— y el centro
    # de todo eso es un promedio que no se parece a ninguna. Con el banco de 1.423
    # frases, el centro mas cercano daba 38% de acierto global, y casi todo era
    # «derivar -> X»; el decodificador real, sobre las mismas frases, 53%. El
    # modelo no decide por centros: su fila de salida es un hiperplano, que
    # separa una clase de varios modos sin problema. Asi que derivar se mide con
    # evaluar.py --casos, y aca se deja afuera del triage.
    multimodal = {"derivar"}
    n_multi = sum(1 for o in ori if o["herramienta"] in multimodal)
    ori = [o for o in ori if o["herramienta"] not in multimodal]
    pred = []
    for o in ori:
        v = med.h(o["frase"])
        pred.append((o, geo.mas_cercano(v), v))
    med.guardar()
    ok = sum(1 for o, p, _ in pred if p == o["herramienta"])
    err = collections.Counter((o["herramienta"], p) for o, p, _ in pred if p != o["herramienta"])
    fallan = collections.defaultdict(list)
    for o, p, v in pred:
        if p != o["herramienta"]:
            fallan[(o["herramienta"], p)].append((o["frase"], v))

    prop, acep = historial(bitacora)
    prioridades = []
    for (a, b), n in err.items():
        c = geo.cdnv(a, b) if a in geo.medibles and b in geo.medibles else float("nan")
        prioridades.append({"verdadera": a, "va_a": b, "errores": n,
                            "cdnv": c, "prioridad": n * (c if c == c else 0.1)})
    prioridades.sort(key=lambda r: -r["prioridad"])

    for r in prioridades:
        a, b = r["verdadera"], r["va_a"]
        causas = []
        # etiquetado: la mayoria de sus propias semillas cae en otro grupo
        if a in geo.P and len(geo.P[a]) >= 2:
            ajenas = sum(1 for v in geo.P[a] if geo.mas_cercano(v) != a)
            if ajenas / len(geo.P[a]) >= 0.5:
                causas.append("etiquetado")
        # contaminado: semillas de a del lado de b, o de b del lado de a
        culp = []
        if a in geo.P and b in geo.P:
            u = geo.mu[a] - geo.mu[b]
            if u.norm() > 1e-6:
                u = u / u.norm()
                m = ((geo.mu[a] + geo.mu[b]) / 2) @ u
                for lado, sg in ((a, 1), (b, -1)):
                    for i, v in enumerate(geo.P[lado]):
                        if (v @ u - m).item() * sg < 0 and i < len(frases.get(lado, [])):
                            culp.append({"herramienta": lado, "frase": frases[lado][i]})
        if culp:
            causas.append("contaminado")
        # hueco: las que fallan caen fuera del subespacio del grupo
        if a in geo.P:
            novs = [geo.novedad(a, v) for _, v in fallan[(a, b)]]
            if novs and sum(novs) / len(novs) >= 0.5:
                causas.append("hueco")
        # inseparable: se propuso bastante y casi nada entro
        if prop[a] >= 6 and acep[a] / prop[a] <= 0.15:
            causas.append("inseparable")
        r["causas"] = causas or ["hueco"]
        r["culpables"] = culp[:6]
        r["cubrir"] = [f for f, _ in fallan[(a, b)]][:6]
        # vocabulario que tira hacia b: palabras de b que a no usa
        pa = collections.Counter(w for f in frases.get(a, []) for w in f.lower().split())
        pb = collections.Counter(w for f in frases.get(b, []) for w in f.lower().split())
        r["evitar"] = [w for w, n in pb.most_common(40) if len(w) > 3 and pa[w] == 0][:8]
        r["historial"] = {"propuestas": prop[a], "aceptadas": acep[a]}

    return {"acierto_orientacion": ok / max(1, len(pred)), "frases_orientacion": len(pred),
            "prioridades": prioridades, "fuera_del_triage": n_multi}


def imprimir_orientacion(o):
    print(f"\n  BANCO DE ORIENTACION: mitad que orienta {o['frases_orientacion']} frases,"
          f" {100*o['acierto_orientacion']:.1f}% caen en su grupo"
          + (f" · mitad que confirma {o['frases_confirmacion']} frases,"
             f" {100*o['acierto_confirmacion']:.1f}%" if "acierto_confirmacion" in o else ""))
    if o.get("fuera_del_triage"):
        print(f"  ({o['fuera_del_triage']} frases de derivar quedan afuera: es una clase de muchos"
              f" modos y el centro no la representa. Se miden con evaluar.py --casos.)")
    print("\n  DONDE SE PIERDE (prioridad = errores x CDNV)")
    for r in o["prioridades"][:12]:
        c = "  —  " if r["cdnv"] != r["cdnv"] else f"{r['cdnv']:.3f}"
        print(f"    {r['verdadera']:22s} -> {r['va_a']:22s} {r['errores']:2d} errores · CDNV {c}"
              f" · {'+'.join(r['causas'])}")
    print("\n  QUE HACER, por causa:")
    print("    etiquetado   leer las semillas: estan centradas en otra cosa")
    print("    contaminado  sacar las culpables (candidatas con \"quitar\": true)")
    print("    hueco        escribir formas NUEVAS de pedirlo, guiadas por las que fallan")
    print("    inseparable  decision de producto: fusionar, derivar o aceptar las dos")


def imprimir(res, objetivo):
    print(f"\n{res['pares_medidos']} pares medidos (todos contra todos)")
    print(f"  sobre el objetivo (CDNV_dir > {objetivo:.3f}, Fisher < {1/math.sqrt(objetivo):.1f}):"
          f" {res['pares_sobre_objetivo']}")
    print(f"  CDNV_dir mediana {res['cdnv_mediana']:.4f} · peor {res['cdnv_peor']:.3f}"
          f" · dimension efectiva mediana {res['dim_mediana']:.1f}")
    print("\n  PEORES PARES                                       CDNV   Fisher  cota error")
    for p in res["peores_pares"][:10]:
        print(f"    {p['a']:22s} {p['b']:22s} {p['cdnv']:7.3f} {p['fisher']:6.2f}"
              f"   {100*p['cota_error']:5.0f}%")
    if res["centros_de_choque"]:
        print("\n  CENTROS DE CHOQUE (en 2+ pares sobre el objetivo)")
        for c in res["centros_de_choque"]:
            print(f"    {c['herramienta']:22s} {c['pares_sobre_objetivo']} pares"
                  f" · dim {c['dim']:.1f} · {c['frases']} semillas")
    if res.get("culpables"):
        vistos = set()
        print("\n  SEMILLAS CULPABLES (del lado ajeno en un par pegado)")
        for c in res["culpables"][:14]:
            k = (c["herramienta"], c["frase"])
            if k in vistos:
                continue
            vistos.add(k)
            print(f"    {c['herramienta']:22s} -> {c['hacia']:20s} «{c['frase']}»")
    if res.get("sin_semillas_suficientes"):
        print(f"\n  CON MENOS DE 3 SEMILLAS ({len(res['sin_semillas_suficientes'])}): todo su corpus"
              " es variacion mecanica de una o dos frases")
        print("    " + ", ".join(res["sin_semillas_suficientes"]))
    print("\n  GRUPOS MAS ANGOSTOS (le faltan formas de pedirlo)")
    for c in res["angostos"][:8]:
        print(f"    {c['herramienta']:22s} dim {c['dim']:.1f} · {c['frases']} semillas")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--modelo", required=True, help="modelo entrenado que mide")
    ap.add_argument("--semillas", default=str(SEMILLAS))
    ap.add_argument("--herramientas", help="lista separada por comas, o un archivo con una por linea")
    ap.add_argument("--sin-derivar", action="store_true", help="no incluir el grupo derivar")
    ap.add_argument("--cache", default=str(Path.home() / ".cache/ba-nubes"))
    ap.add_argument("--objetivo", type=float, default=0.028, help="CDNV_dir maxima aceptable")
    ap.add_argument("--novedad-min", type=float, default=0.45)
    ap.add_argument("--tolerancia", type=float, default=0.05)
    ap.add_argument("--vecinas", type=int, default=5)
    ap.add_argument("--diagnosticar", action="store_true")
    ap.add_argument("--evaluar")
    ap.add_argument("--aceptar", action="store_true")
    ap.add_argument("--json", help="guardar el diagnostico (entrada para quien propone)")
    ap.add_argument("--previo", help="diagnostico de la ronda anterior, para comparar")
    ap.add_argument("--orientacion", help="banco de orientacion (jsonl {frase, herramienta}):"
                    " frases escritas SIN ver el corpus, que dicen donde se pierde")
    ap.add_argument("--fuera-es", default="derivar",
                    help="con que se cuentan las frases del banco de herramientas que NO estan"
                         " en el conjunto medido. Para el modelo reactivo de hoy, la respuesta"
                         " correcta a un pedido administrativo es derivar. Vacio = descartarlas.")
    ap.add_argument("--bitacora", default=str(RAIZ / "datos/bitacora-corpus.jsonl"),
                    help="registro de cada ronda; de ahi sale la tasa de aceptacion por herramienta")
    args = ap.parse_args()

    import lib_prompt as lp
    if args.herramientas:
        p = Path(args.herramientas)
        hs = (p.read_text().split() if p.exists() else args.herramientas.split(","))
    else:
        hs = lp.herramientas_reactivas()

    med = Medidor(args.modelo, args.cache)
    torch = med.torch
    frases, grupos = cargar_grupos(med, args.semillas, hs, not args.sin_derivar)
    # Los grupos con 1 o 2 semillas ENTRAN a la geometria —sus candidatas se
    # prueban igual, y el filtro que mas importa, «cae en su grupo», funciona con
    # un solo punto— pero no se informan como pares: una varianza con dos puntos
    # no mide nada. Son justamente los que mas semillas necesitan: todo su corpus
    # es variacion mecanica de una o dos frases.
    pocos = sorted(h for h, g in grupos.items() if len(g) < 3)
    geo = Geometria(torch, grupos, args.vecinas)
    geo.medibles = {h for h, g in grupos.items() if len(g) >= 3}
    print(f"{len(grupos)} grupos · {sum(len(g) for g in grupos.values())} semillas"
          f" · {len(pocos)} con menos de 3", file=sys.stderr)

    if args.diagnosticar:
        res = diagnosticar(geo, args.objetivo, frases)
        imprimir(res, args.objetivo)
        # Chequeo de generalizacion: centro mas cercano sobre frases que el
        # generador aparto para validar. Colapso en entrenamiento no implica
        # nada fuera de el (arxiv 2202.08384).
        # Centros armados SOLO con la parte de entrenamiento del corpus generado,
        # y probados sobre la de validacion. La primera version usaba los centros
        # de TODAS las semillas —incluidas las que originaron las frases de
        # validacion— y daba 95,8%: media memoria, no generalizacion.
        if VALIDAR.exists() and ENTRENAR.exists():
            tr = [json.loads(l) for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip()]
            val = [json.loads(l) for l in VALIDAR.read_text(encoding="utf-8").splitlines() if l.strip()]
            g_tr = collections.defaultdict(list)
            for f in tr:
                if f["herramienta"] in geo.P:
                    g_tr[f["herramienta"]].append(med.h(f["frase"]))
            hs_tr = sorted(g_tr)
            C_tr = torch.stack([torch.stack(g_tr[h]).mean(0) for h in hs_tr])
            val = [f for f in val if f["herramienta"] in g_tr]
            ok = sum(1 for f in val
                     if hs_tr[int(torch.argmin((C_tr - med.h(f["frase"])).norm(dim=1)))]
                     == f["herramienta"])
            med.guardar()
            res["validacion_centro_cercano"] = ok / max(1, len(val))
            print(f"\n  validacion (frases apartadas, centro mas cercano): {ok}/{len(val)}"
                  f" = {100*ok/max(1,len(val)):.1f}%")
        # COMPARAR SOBRE LOS MISMOS PARES. Los agregados no comparan entre rondas:
        # cuando un grupo pasa de 2 a 3 semillas se vuelve medible y entran de
        # golpe todos sus pares —en la primera ronda, de 2.485 a 3.570—, y como
        # un grupo recien medible es angosto por definicion, los totales empeoran
        # sin que nada haya empeorado. La comparacion honesta es par por par.
        if args.previo and Path(args.previo).exists():
            import statistics
            prev = json.loads(Path(args.previo).read_text(encoding="utf-8"))
            a, b = prev.get("todos_los_pares", {}), res["todos_los_pares"]
            comunes = sorted(set(a) & set(b))
            if comunes:
                sa = sum(1 for k in comunes if a[k] > args.objetivo)
                sb = sum(1 for k in comunes if b[k] > args.objetivo)
                mejoraron = sum(1 for k in comunes if b[k] < a[k] * 0.95)
                empeoraron = sum(1 for k in comunes if b[k] > a[k] * 1.05)
                print(f"\n  CONTRA LA RONDA ANTERIOR, sobre los {len(comunes)} pares que estaban:")
                print(f"    sobre el objetivo: {sa} -> {sb}")
                print(f"    CDNV mediana: {statistics.median(a[k] for k in comunes):.4f}"
                      f" -> {statistics.median(b[k] for k in comunes):.4f}")
                print(f"    peor: {max(a[k] for k in comunes):.3f} -> {max(b[k] for k in comunes):.3f}")
                print(f"    pares que mejoraron mas de 5%: {mejoraron} · empeoraron: {empeoraron}")
                nuevos = sorted(set(b) - set(a))
                if nuevos:
                    print(f"    pares nuevos (grupos que se volvieron medibles): {len(nuevos)},"
                          f" {sum(1 for k in nuevos if b[k] > args.objetivo)} sobre el objetivo")
                dc = [h for h in prev.get("dims", {}) if h in res["dims"]]
                if dc:
                    print(f"    dimension mediana (mismos grupos): "
                          f"{statistics.median(prev['dims'][h] for h in dc):.2f} -> "
                          f"{statistics.median(res['dims'][h] for h in dc):.2f}")
        if args.orientacion:
            ori = [json.loads(l) for l in Path(args.orientacion).read_text(encoding="utf-8").splitlines()
                   if l.strip()]
            # LO QUE NO ES DEL CONJUNTO MEDIDO. El banco cubre las 292 herramientas,
            # pero el modelo de hoy conoce 85: para el, la respuesta correcta a un
            # pedido administrativo es derivar. Contarlas asi mide, con cientos de
            # frases independientes, si de verdad deriva lo que no le toca —hasta
            # ahora eso se media con 8 casos del examen—.
            if args.fuera_es and args.fuera_es in geo.P:
                n_fuera = sum(1 for o in ori if o["herramienta"] not in geo.P)
                ori = [o if o["herramienta"] in geo.P else
                       {"frase": o["frase"], "herramienta": args.fuera_es, "real": o["herramienta"]}
                       for o in ori]
                print(f"  {n_fuera} frases del banco son de herramientas fuera del conjunto:"
                      f" se cuentan como {args.fuera_es}", file=sys.stderr)
            ori = [o for o in ori if o["herramienta"] in geo.P]
            # DOS MITADES, partidas por un hash fijo de la frase. La mitad A
            # ORIENTA —de ella salen las prioridades—; la B solo CONFIRMA. Si se
            # orienta muchas rondas contra el mismo banco, el banco se vuelve el
            # objetivo y deja de medir (Goodhart). Si A sube y B no, se esta
            # sobreajustando al banco: hay que parar o renovarlo.
            def mitad(o):
                return int(hashlib.sha1(o["frase"].encode()).hexdigest(), 16) % 2
            A = [o for o in ori if mitad(o) == 0]
            B = [o for o in ori if mitad(o) == 1]
            res["orientacion"] = orientar(geo, med, A, frases, args.objetivo, args.bitacora)
            B = [o for o in B if o["herramienta"] != "derivar"]
            okB = sum(1 for o in B if geo.mas_cercano(med.h(o["frase"])) == o["herramienta"])
            med.guardar()
            res["orientacion"]["acierto_confirmacion"] = okB / max(1, len(B))
            res["orientacion"]["frases_confirmacion"] = len(B)
            imprimir_orientacion(res["orientacion"])
            if args.previo and Path(args.previo).exists():
                pv = json.loads(Path(args.previo).read_text(encoding="utf-8")).get("orientacion")
                if pv and "acierto_confirmacion" in pv:
                    da = res["orientacion"]["acierto_orientacion"] - pv["acierto_orientacion"]
                    db = res["orientacion"]["acierto_confirmacion"] - pv["acierto_confirmacion"]
                    print(f"\n  CONTRA LA RONDA ANTERIOR: mitad que orienta {100*da:+.1f} puntos"
                          f" · mitad que confirma {100*db:+.1f} puntos")
                    if da > 0.01 and db <= 0:
                        print("    OJO: mejora la mitad que orienta y NO la que confirma. Es"
                              " sobreajuste al banco: parar, o renovar el banco.")
        if args.json:
            Path(args.json).write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")

    if args.evaluar:
        cm = _corpus_mod()
        examen = json.loads(CORPUS_EXAMEN.read_text(encoding="utf-8"))["casos"]
        prohibidas = [set(cm.sin_tildes(c["pregunta"].lower()).split()) for c in examen]
        # El banco de orientacion tampoco se puede copiar: si una semilla nueva es
        # casi igual a una frase del banco, el banco deja de medir y pasa a ser
        # entrenamiento disfrazado. Se escribe OTRA forma del mismo significado.
        banco = []
        if args.orientacion and Path(args.orientacion).exists():
            banco = [set(cm.sin_tildes(json.loads(l)["frase"].lower()).split())
                     for l in Path(args.orientacion).read_text(encoding="utf-8").splitlines()
                     if l.strip()]
        cands = json.loads(Path(args.evaluar).read_text(encoding="utf-8"))
        aprobadas, rechazadas, quitadas = [], [], []
        for c in cands:
            h, fr = c["herramienta"], c["frase"].strip()
            if h not in geo.P:
                # GRUPO VACIO. No hay centro contra el cual decir «cae en su
                # grupo», asi que la primera frase se acepta solo si NO cae
                # adentro de otro: su distancia al centro mas cercano tiene que
                # superar el radio tipico de un grupo. Paso con operator_feed,
                # que tenia solo dos semillas y eran de otra herramienta.
                if h not in hs:
                    rechazadas.append((h, fr, "no esta en el conjunto medido"))
                    continue
                if c.get("quitar"):
                    rechazadas.append((h, fr, "grupo vacio, nada que quitar"))
                    continue
                v = med.h(fr)
                radios = [(P - P.mean(0)).norm(dim=1).mean().item()
                          for g, P in geo.P.items() if len(P) >= 3]
                radio = sorted(radios)[len(radios) // 2]
                d = (geo.C - v).norm(dim=1)
                j = int(torch.argmin(d))
                if d[j].item() < radio:
                    rechazadas.append((h, fr, f"cae dentro de {geo.hs[j]}"))
                    continue
                geo.P[h] = v[None, :]
                geo._recalcular()
                frases[h] = [fr]
                aprobadas.append((h, fr, 1.0))
                continue
            if c.get("quitar"):
                # SACAR una semilla: se acepta si baja la peor CDNV del grupo con
                # sus vecinas, sin dejarlo con menos de 3 semillas ni perder mas
                # del 20% de su dimension efectiva. Sacar lo que empuja hacia una
                # vecina es lo que despega un centro de choque.
                if fr not in frases.get(h, []):
                    rechazadas.append((h, fr, "no es semilla"))
                    continue
                i = frases[h].index(fr)
                P = geo.P[h]
                P2 = torch.cat([P[:i], P[i + 1:]])
                if len(P2) < 3:
                    rechazadas.append((h, fr, "quedaria con menos de 3"))
                    continue
                antes, _ = geo.peor_cdnv(h)
                despues, _ = geo.peor_cdnv(h, Pa=P2)
                if despues >= antes:
                    rechazadas.append((h, fr, f"sacarla no ayuda ({antes:.3f}->{despues:.3f})"))
                    continue
                if geo.dim(h, P2) < 0.8 * geo.dim(h):
                    rechazadas.append((h, fr, "angostaria demasiado el grupo"))
                    continue
                geo.P[h] = P2
                geo._recalcular()
                frases[h].pop(i)
                quitadas.append((h, fr, antes, despues))
                continue
            if fr in frases.get(h, []):
                rechazadas.append((h, fr, "ya es semilla"))
                continue
            if cm.parecido(fr, prohibidas):
                rechazadas.append((h, fr, "se parece al examen"))
                continue
            if banco and cm.parecido(fr, banco):
                rechazadas.append((h, fr, "copia el banco de orientacion"))
                continue
            v = med.h(fr)
            cerca = geo.mas_cercano(v)
            if cerca != h:
                rechazadas.append((h, fr, f"cae en {cerca}"))
                continue
            nov = geo.novedad(h, v)
            if nov < args.novedad_min:
                rechazadas.append((h, fr, f"no suma variedad ({nov:.2f})"))
                continue
            P2 = torch.cat([geo.P[h], v[None, :]])
            malo = None
            # Contra TODOS los grupos: cualquier par que quede por arriba del
            # objetivo y suba mas de la tolerancia tumba la candidata.
            for o in (geo.hs if h in geo.medibles else []):
                if o == h or o not in geo.medibles:
                    continue
                c0, c1 = geo.cdnv(h, o), geo.cdnv(h, o, Pa=P2)
                if c1 > c0 * (1 + args.tolerancia) and c1 > args.objetivo:
                    malo = (o, c0, c1)
                    break
            if malo:
                rechazadas.append((h, fr, f"acerca a {malo[0]} ({malo[1]:.3f}->{malo[2]:.3f})"))
                continue
            aprobadas.append((h, fr, nov))
            geo.agregar(h, v)
            if len(geo.P[h]) >= 3:
                geo.medibles.add(h)
            frases.setdefault(h, []).append(fr)
        med.guardar()
        print(f"\nAPROBADAS {len(aprobadas)} de {len(cands)}")
        for h, fr, nov in aprobadas:
            print(f"  {h:22s} novedad {nov:.2f}  «{fr}»")
        if quitadas:
            print(f"\nQUITADAS {len(quitadas)}")
            for h, fr, a, b in quitadas:
                print(f"  {h:22s} peor CDNV {a:.3f} -> {b:.3f}  «{fr}»")
        print(f"\nRECHAZADAS {len(rechazadas)}")
        for h, fr, mot in rechazadas:
            print(f"  {h:22s} {mot:36s} «{fr}»")
        # LA BITACORA: una linea por ronda, versionada con el corpus. De ahi sale
        # la tasa de aceptacion por herramienta, que es la senal de «inseparable»:
        # un grupo al que se le propone y no le entra nada no tiene un concepto
        # propio en el modelo.
        if args.aceptar and args.bitacora:
            import datetime
            pp = collections.Counter(c["herramienta"] for c in cands if not c.get("quitar"))
            aa = collections.Counter(h for h, _, _ in aprobadas)
            reg = {"fecha": datetime.datetime.now().isoformat(timespec="seconds"),
                   "modelo": str(args.modelo), "objetivo": args.objetivo,
                   "propuestas_por_herramienta": dict(pp),
                   "aceptadas_por_herramienta": dict(aa),
                   "aprobadas": [{"herramienta": h, "frase": f} for h, f, _ in aprobadas],
                   "quitadas": [{"herramienta": h, "frase": f} for h, f, _, _ in quitadas],
                   "rechazadas": [{"herramienta": h, "frase": f, "motivo": m}
                                  for h, f, m in rechazadas]}
            with open(args.bitacora, "a", encoding="utf-8") as fb:
                fb.write(json.dumps(reg, ensure_ascii=False) + "\n")
        if args.aceptar and (aprobadas or quitadas):
            d = json.loads(Path(args.semillas).read_text(encoding="utf-8"))
            for h, fr, _ in aprobadas:
                e = d["herramientas"].setdefault(h, {"frases": [], "tambien": []})
                if fr not in e["frases"]:
                    e["frases"].append(fr)
            # Una semilla quitada puede estar en frases, en correctivas o venir
            # de la jerga. De los dos primeros se borra; si viene de la jerga, se
            # anota en `excluir` para que el generador no la vuelva a meter.
            for h, fr, _, _ in quitadas:
                e = d["herramientas"].setdefault(h, {"frases": [], "tambien": []})
                if fr in e.get("frases", []):
                    e["frases"].remove(fr)
                elif fr in e.get("correctivas", []):
                    e["correctivas"].remove(fr)
                else:
                    e.setdefault("excluir", [])
                    if fr not in e["excluir"]:
                        e["excluir"].append(fr)
            Path(args.semillas).write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
            print(f"\n-> {len(aprobadas)} agregadas, {len(quitadas)} quitadas en"
                  f" {Path(args.semillas).name}. Volver a correr `make corpus-entrenamiento`.")


if __name__ == "__main__":
    main()
