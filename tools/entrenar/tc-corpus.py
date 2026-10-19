#!/usr/bin/env python3
"""El corpus de tool calling: herramienta + argumentos, derivar, falta_dato y charla.

Junta (ver docs/modelos/entrenamiento-avanzado.md):
  - datos/tc-anotaciones.json (tc-anotar.py): cada plantilla se rinde K veces
    con estados del aparato DISTINTOS (lib_tc.estado_sintetico) y la llamada con
    los nombres de ese estado; algunas con las variantes de siempre (sin tildes,
    muletillas, algun typo; las de corpus-entrenamiento.py);
  - derivar: las frases ajenas del corpus de nombres (datos/entrenamiento: las
    mismas que ya funcionaban), como llamada a `derivar`;
  - falta_dato: las anotaciones con "falta", como llamada a `falta_dato`;
  - charla (tc-charla.py): pedidos generales y del oficio con la respuesta del
    modelo BASE en texto; la mitad con estado, la mitad sin.

PARTES, sin mezclar nunca:
  entrenar.jsonl / validar.jsonl   lo que se entrena (validar: 5 %, para la perdida)
  prueba.jsonl     PLANTILLAS que no se entrenan (15 % por herramienta, fijo por
                   hash de la frase): mide argumentos con frases nunca vistas
  charla-prueba.jsonl  respuestas del base que no se entrenan: mide cuanto de lo
                   general se conserva
El banco de orientacion y el examen no se tocan: se miden aparte (tc-evaluar.py).

Proporciones (arXiv: Hammer 2410.04587, Alopex 2411.05209): derivar ~15 % y
falta_dato ~10 % (lo que «no es suyo»), charla --charla (35 % por defecto).

    tools/entrenar/tc-corpus.py --salida ~/entrenamientos/tc/corpus [--k 4] [--charla 0.35]
"""
import argparse
import hashlib
import importlib.util
import json
import re
import random
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
import lib_tc as tc  # noqa: E402

_spec = importlib.util.spec_from_file_location("corpus_viejo", RAIZ / "tools/entrenar/corpus-entrenamiento.py")
corpus_viejo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(corpus_viejo)


# --- lo que agregan las pruebas de la v2 en la app (doc «Próximo entrenamiento del modelo local (v3)») ---
NUM = {3: "tres", 4: "cuatro", 5: "cinco", 6: "seis", 8: "ocho", 10: "diez", 12: "doce", 15: "quince", 20: "veinte"}


def cantidades():
    """CAMBIO contra EXACTO, que la v2 confundia en los bordes («un poco» salia +6;
    «en menos diez» salia cambio). La regla: «un poco» = 3; sin cantidad = 6;
    «subí/bajá N» = cambio con signo; «en X», «a X», «dejala en X» = exacto (=X)."""
    out = []
    for f, v in (("subile un poco a {audio}", "+3"), ("bajale un poco a {audio}", "-3"),
                 ("un toque mas de {audio}", "+3"), ("bajale un toquecito a {audio}", "-3"),
                 ("subile a {audio}", "+6"), ("bajale a {audio}", "-6"), ("subí {audio}", "+6"),
                 ("bajá {audio} que tapa", "-6")):
        out.append((f, {"name": "{audio}", "volumeDb": v}))
    for n, w in NUM.items():
        out += [(f"subile {n} a {{audio}}", {"name": "{audio}", "volumeDb": f"+{n}"}),
                (f"bajale {w} dB a {{audio}}", {"name": "{audio}", "volumeDb": f"-{n}"}),
                (f"poné {{audio}} en menos {w}", {"name": "{audio}", "volumeDb": f"=-{n}"}),
                (f"dejá {{audio}} en menos {n}", {"name": "{audio}", "volumeDb": f"=-{n}"}),
                (f"bajá {{audio}} en menos {w}", {"name": "{audio}", "volumeDb": f"=-{n}"}),
                (f"{{audio}} a menos {n}", {"name": "{audio}", "volumeDb": f"=-{n}"})]
    out += [("poné {audio} en cero", {"name": "{audio}", "volumeDb": "=0"}),
            ("dejá {audio} a cero", {"name": "{audio}", "volumeDb": "=0"})]
    return [{"frase": f, "plantilla": f, "args": a, "nueva": True, "origen": "cantidades"} for f, a in out]


# la pregunta cuando nombran algo que NO esta en el estado (la v2 inventaba «Micrófono»)
NO_ESTA = {"audio": "No encuentro «{d}» en el audio: ¿cuál es?", "escena": "No hay una escena «{d}»: ¿cuál querés?",
           "fuente": "No encuentro la fuente «{d}»: ¿cuál es?", "persona": "No veo a {d}: ¿quién es?",
           "grafico": "No hay un gráfico «{d}»: ¿cuál querés?", "carrusel": "No hay un carrusel «{d}»: ¿cuál es?"}


def ausente(h, a, rnd):
    """La misma plantilla, pero con una entidad que NO esta en el estado: la
    respuesta correcta es preguntar, nunca un nombre parecido inventado."""
    m = [x for x in tc.RE_HUECO.finditer(a["plantilla"]) if x.group(1) in NO_ESTA]
    if not m or "falta" in a:
        return None
    tipo = m[0].group(1)
    est, formas = tc.estado_sintetico(rnd, tc.tipos_de(a["plantilla"], a.get("args", {})))
    presentes = {e[0] for e in formas.get(tipo, [])} | {e[0] for e in formas.get("audio_operador", [])}
    libres = [e for e in tc.TIPOS[tipo] if e[0] not in presentes]
    if not libres:
        return None
    e = rnd.choice(libres)
    dicho = rnd.choice(e[1])
    plantilla = a["plantilla"].replace(m[0].group(0), dicho, 1)
    frase, _ = tc.rellenar(plantilla, {}, formas, rnd)
    if frase is None or _ste().errores(frase, h, falta=True):  # tambien en STE-ES
        return None
    q = NO_ESTA[tipo].format(d=dicho)
    return fila(est, frase, tc.llamada(tc.FALTA, {"pregunta": q}), "falta", h, {"pregunta": q},
                plantilla=a["plantilla"])


# saludos y charla social del oficio: la v2 los derivaba. Respuesta de una oracion,
# rioplatense, que NUNCA dice haber hecho algo.
SOCIAL = [
    (["hola", "hola, ¿cómo andás?", "buenas", "buen día", "buenas tardes", "buenas noches"],
     ["¡Hola! Acá estoy, decime qué necesitás.", "¡Buenas! Listo para cuando quieras.", "Hola, todo en orden por acá."]),
    (["¿cómo estás?", "¿todo bien?", "¿cómo va?"],
     ["Todo bien, acá atento a lo que necesites.", "Bien, gracias. ¿Arrancamos con algo?"]),
    (["gracias", "muchas gracias", "genial, gracias", "joya, gracias"],
     ["De nada.", "A vos.", "Cuando quieras."]),
    (["buen trabajo", "salió bárbaro", "excelente laburo", "felicitaciones al equipo"],
     ["¡Qué bueno! Gracias.", "¡Bien ahí! Buen trabajo de todos."]),
    (["arrancamos", "vamos que arrancamos", "estoy listo", "listos para salir"],
     ["Dale, estoy atento.", "Listo, decime qué hago primero."]),
    (["chau", "nos vemos", "hasta mañana", "me voy, hasta luego"],
     ["¡Chau! Buen descanso.", "Nos vemos, hasta la próxima."]),
    (["te quiero mucho", "sos un genio", "sos lo más"],
     ["¡Gracias! Yo también estoy contento de laburar con vos.", "¡Qué lindo! Gracias."]),
    (["¿estás ahí?", "¿me escuchás?", "¿estás despierto?"],
     ["Sí, acá estoy.", "Sí, te escucho. Decime."]),
    (["qué día largo", "estoy cansado", "uff, qué nervios"],
     ["Ya falta poco, vamos bien.", "Tranqui, va todo bien. Respirá y seguimos."]),
]


def es_prueba(frase, porcion):
    return int(hashlib.sha256(frase.encode()).hexdigest()[:8], 16) / 0xFFFFFFFF < porcion


def resolver_contexto(args, est):
    """[escena_armada] / [escena_actual] -> el valor del estado de turno."""
    out = {}
    for k, v in args.items():
        if isinstance(v, str) and v in ("[escena_armada]", "[escena_actual]"):
            v = est[v[1:-1]]
            if not v:
                return None
        out[k] = v
    return out


def fila(est, frase, completion, tipo, herramienta, args=None, con_estado=True, plantilla=None):
    u = tc.usuario(est if con_estado else None, frase)
    return {"prompt": tc.cabeza() + u + tc.COLA, "completion": completion + tc.FIN, "tipo": tipo,
            "herramienta": herramienta, "args": args, "frase": frase, "plantilla": plantilla}


def _ste():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ste_es
    return ste_es


def rendir(h, a, rnd, k, variar, ste=True):
    """K ejemplos de una anotacion, cada uno con su estado. ste=False: las aprendidas
    (frases de operador tal cual, sin el filtro de STE-ES)."""
    out = []
    intentos = 0
    while len(out) < k and intentos < k * 4:
        intentos += 1
        tipos = tc.tipos_de(a["plantilla"], a.get("args", {}))
        est, formas = tc.estado_sintetico(rnd, tipos)
        if "escena_armada" in json.dumps(a.get("args", {})) and not est["escena_armada"]:
            est["escena_armada"] = rnd.choice([e for e in est["escenas"] if e != est["escena_actual"]] or est["escenas"])
        frase, args = tc.rellenar(a["plantilla"], a.get("args", {}), formas, rnd)
        if frase is None:
            continue
        if variar and rnd.random() < 0.5:
            frase = rnd.choice(corpus_viejo.variantes(frase, rnd, 3))
        # la anotacion se valido con un relleno corto; con los nombres de verdad («el
        # audio de la camara 1») o la superficie una frase puede salirse de STE-ES:
        # entonces otro estado (otros nombres)
        if ste and (_ste().errores(frase, h if h != tc.DERIVAR else None, falta="falta" in a) if h != tc.DERIVAR
                    else _ste().errores_derivar(frase)):
            continue
        if "falta" in a:
            out.append(fila(est, frase, tc.llamada(tc.FALTA, {"pregunta": a["falta"]}), "falta", h,
                            {"pregunta": a["falta"]}, plantilla=a["plantilla"]))
            continue
        args = resolver_contexto(args, est)
        if args is None:
            continue
        out.append(fila(est, frase, tc.llamada(h, args), "herramienta", h, args, plantilla=a["plantilla"]))
    return out


def anotaciones_entrenables(ruta, copia):
    """(anotaciones que se entrenan por herramienta, las de herramientas sacadas del
    perfil). Lo mismo para el corpus y para tc-preferencias."""
    anot = json.loads(Path(ruta).read_text())
    # solo lo que VALIDA (esquema, tipo de entidad, STE-ES): lo que el agente no pudo
    # corregir no se entrena
    _spec2 = importlib.util.spec_from_file_location("tca", Path(__file__).parent / "tc-anotar.py")
    tca = importlib.util.module_from_spec(_spec2)
    _spec2.loader.exec_module(tca)
    esq = tc.esquemas()
    descartadas = 0
    # las de las herramientas que el perfil saco A PROPOSITO (lp.FUERA): no se
    # entrenan como llamada; en el corpus pasan a ser ejemplos de derivar
    import lib_prompt as _lp
    de_fuera = {h: [a for a in anot[h] if "falta" not in a] for h in _lp.FUERA if h in anot}
    for h in list(anot):
        buenas = [a for a in anot[h] if h in esq and not tca.validar(a, esq[h], h)]
        descartadas += len(anot[h]) - len(buenas)
        anot[h] = buenas
    print(f"anotaciones descartadas por no validar: {descartadas}")
    # SIN COPIAS adentro de una herramienta (lib_tc.sin_copias): la copia no le enseña
    # nada a la nube, solo la aprieta (memoria)
    copias = 0
    for h in list(anot):
        anot[h], cp = tc.sin_copias(anot[h], copia)
        copias += len(cp)
    print(f"anotaciones copia de otra de su herramienta: fuera {copias}")
    return anot, de_fuera


def filtro_parecidas(umbral):
    """parecida(fila): si se parece DEMASIADO a una frase del banco o del examen con
    la misma respuesta (ver main). Lo mismo para el corpus y para tc-preferencias."""
    import unicodedata

    def _norm(s):
        s = unicodedata.normalize("NFKD", s.lower()).encode("ascii", "ignore").decode()
        return re.sub(r"\W+", " ", s).strip()
    _mias = set(tc.esquemas().keys())
    vacias = set("el la los las lo le les de del a al en con por para que y o u un una unos unas es "
                 "esta este esto eso esa ese se su sus mi me te tu ya ahi aca hay como cual cuales "
                 "dale eh che ojo rapido porfa bueno ahora toque".split())

    def _toks(x):
        t = set(x.split())
        return (t - vacias) or t
    _por_destino = {}  # destino -> ([tokens], indice palabra -> filas)
    for f in (RAIZ / "datos/orientacion.jsonl", RAIZ / "datos/entrenamiento/examen.jsonl"):
        for l in f.read_text().splitlines():
            if not l.strip():
                continue
            x = json.loads(l)
            # la respuesta bajo ESTE perfil, como la mide tc-evaluar: lo que salio del
            # perfil es derivar
            dest = {h for h in ([x["herramienta"]] if x.get("herramienta") else []) + x.get("aceptables", [])
                    if h in _mias or h == tc.DERIVAR} or {tc.DERIVAR}
            t = _toks(_norm(x["frase"]))
            for d in dest:
                lista, ind = _por_destino.setdefault(d, ([], {}))
                for w in t:
                    ind.setdefault(w, []).append(len(lista))
                lista.append(t)

    def parecida(f):
        """f: una fila. Su respuesta es su herramienta (falta_dato tambien: el pedido
        es el de la herramienta) o derivar; la charla no tiene."""
        if not f.get("frase") or not f.get("herramienta"):
            return False
        lista, ind = _por_destino.get(f["herramienta"], ([], {}))
        t = _toks(_norm(f["frase"]))
        return bool(t) and any(len(t & lista[i]) / max(len(t), len(lista[i])) >= umbral
                               for i in {i for w in t for i in ind.get(w, ())})
    return parecida


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--salida", required=True)
    ap.add_argument("--anotaciones", default=str(RAIZ / "datos/tc-anotaciones.json"))
    ap.add_argument("--derivar-de", default=str(RAIZ / "datos/entrenamiento"),
                    help="corpus de nombres (make corpus-entrenamiento) de donde salen las frases ajenas")
    ap.add_argument("--charla-de", default="~/entrenamientos/tc/charla-v3/charla-*.jsonl",
                    help="charla rioplatense y corta (la de la v2 salia neutra/peninsular)")
    ap.add_argument("--k", type=int, default=4, help="ejemplos por plantilla (estados distintos)")
    ap.add_argument("--charla", type=float, default=0.35, help="porcion de charla en el corpus")
    ap.add_argument("--derivar", type=float, default=0.10,
                    help="porcion de derivar (0,15 en el v2: derivaba de mas, cobertura 81,7 %%)")
    ap.add_argument("--prueba", type=float, default=0.15, help="plantillas que no se entrenan")
    ap.add_argument("--falta", type=float, default=0.10, help="porcion (techo) de falta_dato")
    ap.add_argument("--copia", type=float, default=0.7,
                    help="dos plantillas de una herramienta que comparten esta porcion de palabras y piden lo mismo son una")
    ap.add_argument("--parecido", type=float, default=0.6,
                    help="fuera la frase que comparte esta porcion de palabras con una del banco o del examen de su misma respuesta")
    ap.add_argument("--ausentes", type=float, default=1.0,
                    help="probabilidad por plantilla de sumar su version con un nombre que no esta en el estado"
                         " (0,35 hasta la v6b: 1,4 %% del corpus, y el modelo fallaba el 36 %% de esos casos)")
    ap.add_argument("--social", type=int, default=6, help="ejemplos por frase social")
    ap.add_argument("--semilla", type=int, default=13)
    ap.add_argument("--aprendidas", help="datos/tc-aprendidas.json: lo que el modelo erro en los bancos intermedios"
                                         " (tc-aprendidas.py), la frase tal cual y sus variaciones (tc-ciclo.py)")
    ap.add_argument("--bancos-hasta", type=int, default=99, help="de las aprendidas, solo las de los bancos 1..N")
    ap.add_argument("--peso-eje", type=int, default=3, help="veces que entra la frase tal cual (el eje de su nube)")
    ap.add_argument("--peso-variacion", type=int, default=1, help="veces que entra cada variacion")
    args = ap.parse_args()
    rnd = random.Random(args.semilla)
    sal = Path(args.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)

    anot, de_fuera = anotaciones_entrenables(args.anotaciones, args.copia)
    ent, prueba = [], []
    for h, lista in sorted(anot.items()):
        for a in lista:
            if not a.get("plantilla"):
                continue
            if es_prueba(a["frase"], args.prueba) and not a.get("nueva"):
                prueba += rendir(h, a, rnd, 2, variar=False)
            else:
                ent += rendir(h, a, rnd, args.k, variar=True)
    # las cantidades (cambio contra exacto), como plantillas mas de audio_set
    for a in cantidades():
        (prueba if es_prueba(a["frase"] + "#c", args.prueba) else ent).extend(
            rendir("audio_set", a, rnd, 2 if es_prueba(a["frase"] + "#c", args.prueba) else args.k, variar=True))
    # nombres que no estan en el estado -> falta_dato (de las plantillas de entrenamiento)
    aus = []
    for h, lista in sorted(anot.items()):
        for a in lista:
            if a.get("plantilla") and not (es_prueba(a["frase"], args.prueba) and not a.get("nueva")) \
                    and rnd.random() < args.ausentes:
                f = ausente(h, a, rnd)
                if f:
                    aus.append(f)
    # ninguna fila repetida: la misma frase con la misma respuesta (una plantilla sin
    # huecos rendida k veces da k veces la misma frase; solo cambiaba el estado).
    # Antes de las porciones: derivar, falta y charla se calculan sobre lo que queda
    _vistas, _sin_rep = set(), []
    for f in ent:
        clave = (f["tipo"], f.get("herramienta"), f.get("frase"), json.dumps(f.get("args"), sort_keys=True)) \
            if f.get("herramienta") else None
        if clave is not None and clave in _vistas:
            continue
        if clave is not None:
            _vistas.add(clave)
        _sin_rep.append(f)
    print(f"filas repetidas (misma frase, misma respuesta): fuera {len(ent) - len(_sin_rep)}")
    ent = _sin_rep
    # falta_dato CON TECHO: el agente marca como incompleta toda frase con
    # «ese/eso» y quedaba un 27 % del corpus; con eso el modelo aprende a
    # preguntar de mas. ~10 % del total (como la irrelevancia de Hammer).
    herr = [f for f in ent if f["tipo"] == "herramienta"]
    faltas = [f for f in ent if f["tipo"] == "falta"]
    rnd.shuffle(faltas)
    tope = int(len(herr) / max(0.1, 1 - args.derivar - args.charla - args.falta) * args.falta)
    ent = herr + faltas[:tope] + aus

    # derivar: las frases ajenas que ya funcionaban, cada una con un estado cualquiera
    der = []
    for parte in ("entrenar.jsonl", "validar.jsonl"):
        p = Path(args.derivar_de) / parte
        if p.exists():
            der += [json.loads(l)["frase"] for l in p.read_text().splitlines()
                    if l.strip() and json.loads(l)["herramienta"] == "derivar"]
    # y los pedidos del aparato que hace el GRANDE (derivaciones-admin: cada uno con su
    # herramienta real). Son los ajenos cerca del borde del catalogo, los que el modelo
    # confundia con una propia parecida (Hammer, arXiv:2410.04587: «irrelevancia»
    # cerca de las funciones); con el filtro STE-ES las viejas solas no llegaban al 10 %
    der += [json.loads(l)["frase"] for l in (RAIZ / "datos/derivaciones-admin.jsonl").read_text().splitlines()
            if l.strip()]
    # FUERA las que no son derivar para ESTE perfil (su herramienta real, registrada
    # en derivaciones-admin, ahora es propia: con la v4 eran 32, encuestas, prompter,
    # recetas...) y las del banco y el examen (se colaban 9: nunca se entrena con eso)
    import unicodedata

    def _norm(s):
        s = unicodedata.normalize("NFKD", s.lower()).encode("ascii", "ignore").decode()
        return re.sub(r"\W+", " ", s).strip()
    _mias = set(tc.esquemas().keys())
    _real = {_norm(json.loads(l)["frase"]): json.loads(l)["herramienta"]
             for l in (RAIZ / "datos/derivaciones-admin.jsonl").read_text().splitlines() if l.strip()}
    _prohibidas = {_norm(json.loads(l)["frase"]) for f in (RAIZ / "datos/orientacion.jsonl",
                                                          RAIZ / "datos/entrenamiento/examen.jsonl")
                   for l in f.read_text().splitlines() if l.strip()}
    der = sorted({f for f in der if _norm(f) not in _prohibidas and _real.get(_norm(f)) not in _mias})
    # y en STE-ES (las reglas que no dependen de la herramienta, y ningun verbo que
    # signifique una herramienta propia): ste_es.errores_derivar
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ste_es
    n_der = len(der)
    der = [f for f in der if not ste_es.errores_derivar(f)]
    print(f"derivar: {len(der)} frases en STE-ES ({n_der - len(der)} fuera)")
    rnd.shuffle(der)
    # LAS DEL BORDE PRIMERO: los pedidos de las herramientas que el perfil saco
    # (renombrar una escena, las propiedades crudas de una fuente...) se parecen
    # a los propios mas que cualquier otro ajeno: es justo lo que el modelo tiene
    # que aprender a derivar. Se rinden con nombres de verdad y pasan STE-ES.
    borde = []
    for h, lista in sorted(de_fuera.items()):
        for a in lista:
            for _ in range(2):
                _, formas = tc.estado_sintetico(rnd, tc.tipos_de(a["plantilla"], {}))
                frase, _ = tc.rellenar(a["plantilla"], {}, formas, rnd)
                if frase and not ste_es.errores_derivar(frase) and _norm(frase) not in _prohibidas:
                    borde.append(frase)
                    break
    borde = sorted(set(borde))
    rnd.shuffle(borde)
    ya = set(borde)
    der = borde + [f for f in der if f not in ya]
    print(f"derivar: {len(borde)} del borde (anotaciones de {len(de_fuera)} herramientas sacadas del perfil)")
    base = len(ent)
    # derivar y charla son porciones del TOTAL
    total = int(len(herr) / max(0.1, 1 - args.derivar - args.charla - args.falta))
    for frase in der[:int(total * args.derivar)]:
        est, _ = tc.estado_sintetico(rnd)
        ent.append(fila(est, frase, tc.llamada(tc.DERIVAR, {}), "derivar", tc.DERIVAR, {}))

    # charla: la respuesta del base, en texto
    import glob
    charla = []
    for f in sorted(glob.glob(str(Path(args.charla_de).expanduser()))):
        charla += [json.loads(l) for l in Path(f).read_text().splitlines() if l.strip()]
    rnd.shuffle(charla)
    n_cp = min(300, len(charla) // 10)
    charla_prueba, charla = charla[:n_cp], charla[n_cp:]
    for c in charla[:int(total * args.charla)]:
        est, _ = tc.estado_sintetico(rnd)
        ent.append(fila(est, c["pedido"], c["respuesta"], "charla", None, None, con_estado=rnd.random() < 0.5))
    # social: saludos y charla corta del oficio, con y sin estado
    for frases, resp in SOCIAL:
        for f in frases:
            for _ in range(args.social):
                est, _ = tc.estado_sintetico(rnd)
                v = rnd.choice(corpus_viejo.variantes(f, rnd, 2)) if rnd.random() < 0.4 else f
                ent.append(fila(est, v, rnd.choice(resp), "charla", None, None, con_estado=rnd.random() < 0.5))
    cp = []
    for c in charla_prueba:
        est, _ = tc.estado_sintetico(rnd)
        cp.append(fila(est, c["pedido"], c["respuesta"], "charla", None, None, con_estado=rnd.random() < 0.5))

    # LO APRENDIDO EN LOS BANCOS INTERMEDIOS (tc-ciclo.py): la frase que el modelo
    # erro, tal cual (el eje, con mas peso) y sus variaciones elegidas para que el eje
    # quede en el CENTRO de su nube (tc-aprendidas.py). Sin STE-ES a proposito: son
    # frases de operador, justo lo que el corpus controlado no tiene (el banco quedaba
    # a ~3 radios de sus nubes, docs/modelos/bucle-tool-calling.md). Ni prueba ni
    # porcion: entran todas, y despues pasan por el filtro de parecidas al banco final
    # y al examen como cualquier otra.
    # DOS CUIDADOS para que el corpus 0 sea IDENTICO con y sin --aprendidas (si no, los
    # pasos del ciclo no se pueden comparar entre si): (1) un Random propio, para no
    # mover el que sortea el resto; (2) las filas van aparte y se suman DESPUES de
    # sortear validar: asi ninguna aprendida cae en validar (todas entrenan) ni cambia
    # el sorteo de las demas.
    n_apr, apr_filas = 0, []
    if args.aprendidas:
        rnd_a = random.Random(args.semilla + 1000)
        apr = json.loads(Path(args.aprendidas).read_text())
        esq = tc.esquemas()
        for banco, por_h in sorted(apr.items()):
            if int(banco.split("-")[-1]) > args.bancos_hasta:
                continue
            for h, lista in sorted(por_h.items()):
                for a in lista:
                    veces = args.peso_eje if a.get("eje") else args.peso_variacion
                    if h == tc.DERIVAR:
                        # el mismo peso que una herramienta: cada pasada rinde k filas (k estados)
                        for _ in range(veces * args.k):
                            _, formas = tc.estado_sintetico(rnd_a, tc.tipos_de(a["plantilla"], {}))
                            frase, _ = tc.rellenar(a["plantilla"], {}, formas, rnd_a)
                            if frase:
                                est, _ = tc.estado_sintetico(rnd_a)
                                apr_filas.append(fila(est, frase, tc.llamada(tc.DERIVAR, {}), "derivar", tc.DERIVAR, {}))
                    elif h in esq:
                        for _ in range(veces):
                            apr_filas += rendir(h, a, rnd_a, args.k, variar=False, ste=False)
        n_apr = len(apr_filas)
        print(f"aprendidas (bancos 1..{min(args.bancos_hasta, 99)}): {n_apr} filas")

    # FUERA lo que se parece DEMASIADO a una frase del banco o del examen CON LA
    # MISMA RESPUESTA, no solo lo identico: la v5 tenia 0 frases identicas pero
    # «¿llegaron preguntas nuevas?» o «corta la transmision» estaban tal cual salvo
    # tildes. Por palabras compartidas, como parecido() del corpus de nombres: importa
    # que sea OTRA forma de pedirlo, no que cambien dos letras. Y por herramienta: una
    # frase parecida con OTRA respuesta («vinculá un rótulo al que habla» frente a
    # «desatá el rótulo del que habla») no filtra la respuesta, es un contraste.
    # Sin las palabras vacias ni las muletillas: con ellas «mostrame el mezclador» se
    # parecia a «Mostrame el guion».
    parecida = filtro_parecidas(args.parecido)
    n0, p0 = len(ent), len(prueba)
    ent = [f for f in ent if not parecida(f)]
    prueba = [f for f in prueba if not parecida(f)]
    print(f"parecidas al banco o al examen: fuera {n0 - len(ent)} de entrenar y {p0 - len(prueba)} de prueba")

    rnd.shuffle(ent)
    n_val = max(50, len(ent) // 20)
    val, ent = ent[:n_val], ent[n_val:]
    if apr_filas:
        # las aprendidas, tambien por el filtro de parecidas al banco final y al examen
        antes = len(apr_filas)
        apr_filas = [f for f in apr_filas if not parecida(f)]
        print(f"aprendidas parecidas al banco final o al examen: fuera {antes - len(apr_filas)} de {antes}")
        n_apr = len(apr_filas)
        ent = ent + apr_filas
    for nombre, filas in (("entrenar", ent), ("validar", val), ("prueba", prueba), ("charla-prueba", cp)):
        (sal / f"{nombre}.jsonl").write_text("\n".join(json.dumps(f, ensure_ascii=False) for f in filas) + "\n")
    cuenta = {t: sum(1 for f in ent if f["tipo"] == t) for t in ("herramienta", "falta", "derivar", "charla")}
    cuenta["de_eso_ausentes"] = len(aus)
    huella = {"formato": "tc", "prompt": tc.huella(), "ejemplos": len(ent), "por_tipo": cuenta,
              "porcion": {t: round(n / max(1, len(ent)), 3) for t, n in cuenta.items()},
              "prueba": len(prueba), "charla_prueba": len(cp), "k": args.k, "parecido": args.parecido, "copia": args.copia,
              "anotaciones": hashlib.sha256(Path(args.anotaciones).read_bytes()).hexdigest()[:16],
              "aprendidas": ({"archivo": hashlib.sha256(Path(args.aprendidas).read_bytes()).hexdigest()[:16],
                              "bancos_hasta": args.bancos_hasta, "filas": n_apr,
                              "peso_eje": args.peso_eje, "peso_variacion": args.peso_variacion}
                             if args.aprendidas else None)}
    (sal / "huella.json").write_text(json.dumps(huella, ensure_ascii=False, indent=1))
    print(json.dumps(huella, ensure_ascii=False))
    if len(der) < int(total * args.derivar):
        print(f"OJO: pocas frases de derivar ({len(der)} de {int(total * args.derivar)})")
    if len(charla) < int(total * args.charla):
        print(f"OJO: poca charla ({len(charla)} de {int(total * args.charla)})")


if __name__ == "__main__":
    main()
