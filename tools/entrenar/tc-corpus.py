#!/usr/bin/env python3
"""El corpus de tool calling: herramienta + argumentos, derivar, falta_dato y charla.

Junta (ver docs/entrenamiento-avanzado.md):
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
import random
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
import lib_tc as tc  # noqa: E402

_spec = importlib.util.spec_from_file_location("corpus_viejo", RAIZ / "tools/corpus-entrenamiento.py")
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
    if frase is None:
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


def rendir(h, a, rnd, k, variar):
    """K ejemplos de una anotacion, cada uno con su estado."""
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
        if "falta" in a:
            out.append(fila(est, frase, tc.llamada(tc.FALTA, {"pregunta": a["falta"]}), "falta", h,
                            {"pregunta": a["falta"]}, plantilla=a["plantilla"]))
            continue
        args = resolver_contexto(args, est)
        if args is None:
            continue
        out.append(fila(est, frase, tc.llamada(h, args), "herramienta", h, args, plantilla=a["plantilla"]))
    return out


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
    ap.add_argument("--ausentes", type=float, default=0.35,
                    help="probabilidad por plantilla de sumar su version con un nombre que no esta en el estado")
    ap.add_argument("--social", type=int, default=6, help="ejemplos por frase social")
    ap.add_argument("--semilla", type=int, default=13)
    args = ap.parse_args()
    rnd = random.Random(args.semilla)
    sal = Path(args.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)

    anot = json.loads(Path(args.anotaciones).read_text())
    # solo lo que VALIDA (esquema, tipo de entidad, STE-ES): lo que el agente no pudo
    # corregir no se entrena
    _spec2 = importlib.util.spec_from_file_location("tca", Path(__file__).parent / "tc-anotar.py")
    tca = importlib.util.module_from_spec(_spec2)
    _spec2.loader.exec_module(tca)
    esq = tc.esquemas()
    descartadas = 0
    for h in list(anot):
        buenas = [a for a in anot[h] if h in esq and not tca.validar(a, esq[h], h if h != tc.DERIVAR else None)]
        descartadas += len(anot[h]) - len(buenas)
        anot[h] = buenas
    print(f"anotaciones descartadas por no validar: {descartadas}")
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
    der = sorted(set(der))
    rnd.shuffle(der)
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

    rnd.shuffle(ent)
    n_val = max(50, len(ent) // 20)
    val, ent = ent[:n_val], ent[n_val:]
    for nombre, filas in (("entrenar", ent), ("validar", val), ("prueba", prueba), ("charla-prueba", cp)):
        (sal / f"{nombre}.jsonl").write_text("\n".join(json.dumps(f, ensure_ascii=False) for f in filas) + "\n")
    cuenta = {t: sum(1 for f in ent if f["tipo"] == t) for t in ("herramienta", "falta", "derivar", "charla")}
    cuenta["de_eso_ausentes"] = len(aus)
    huella = {"formato": "tc", "prompt": tc.huella(), "ejemplos": len(ent), "por_tipo": cuenta,
              "porcion": {t: round(n / max(1, len(ent)), 3) for t, n in cuenta.items()},
              "prueba": len(prueba), "charla_prueba": len(cp), "k": args.k,
              "anotaciones": hashlib.sha256(Path(args.anotaciones).read_bytes()).hexdigest()[:16]}
    (sal / "huella.json").write_text(json.dumps(huella, ensure_ascii=False, indent=1))
    print(json.dumps(huella, ensure_ascii=False))
    if len(der) < int(total * args.derivar):
        print(f"OJO: pocas frases de derivar ({len(der)} de {int(total * args.derivar)})")
    if len(charla) < int(total * args.charla):
        print(f"OJO: poca charla ({len(charla)} de {int(total * args.charla)})")


if __name__ == "__main__":
    main()
