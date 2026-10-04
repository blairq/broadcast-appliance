#!/usr/bin/env python3
"""Anotar las semillas con la llamada exacta (herramienta + argumentos) para el formato de tool calling.

Cada semilla de datos/semillas-reactivas.json pasa a una PLANTILLA con huecos
para las entidades del aparato y los ARGUMENTOS que corresponden (ver
lib_tc.py y consignas/tc-anotar.md):

    «bajale 4 dB a la música» -> plantilla «bajale 4 dB a {audio}»,
                                 args {"name": "{audio}", "volumeDb": "-4"}

Con eso tc-corpus.py arma ejemplos con estados del aparato distintos: el modelo
aprende a COPIAR el nombre del estado, no a memorizarlo.

Las herramientas sin argumentos se anotan solas. Las demas las anota un agente
(agente.py: sin conexion, solo con su lote) por grupos, y el programa VALIDA
cada anotacion contra el esquema; las que fallan vuelven al agente con el error
(--vueltas). Resultado versionado: datos/tc-anotaciones.json (es dato curado,
como las semillas).

    tools/entrenar/tc-anotar.py                       # lo que falta anotar
    tools/entrenar/tc-anotar.py --solo audio_set,scene_switch --rehacer
    tools/entrenar/tc-anotar.py --revisar             # solo validar lo que hay
"""
import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402
import lib_tc as tc  # noqa: E402

SALIDA = RAIZ / "datos" / "tc-anotaciones.json"
RE_REL = re.compile(r"^[+-]\d+(\.\d+)?$")
RE_EXACTO = re.compile(r"^=-?\d+(\.\d+)?$")
RE_CONTEXTO = re.compile(r"^\[(escena_armada|escena_actual)\]$")


RELLENO = {"escena": "la del cierre", "audio": "la música", "audio_operador": "", "fuente": "la cámara 2",
           "persona": "Laura", "grafico": "el zócalo", "carrusel": "la presentación", "camara": "la dos",
           "posicion": "el atril", "discurso": "el del intendente", "medio": "la tanda", "pregunta": "la de Juan",
           "pantalla": "la tele del hall", "receta": "la de apertura"}


# que tipo de entidad recibe cada familia de herramientas (como candidatos() en
# agent/ArgumentosLocales.cpp): un audio_set no recibe el nombre de una persona
ENTIDADES = [(("scene_", "transition_"), {"escena"}), (("audio", "mixer_"), {"audio", "audio_operador"}),
             (("source_", "capture_source"), {"fuente", "audio", "audio_operador", "escena"}),
             (("participant_", "speaker_", "capture_person", "chat_"), {"persona"}),
             (("grafico_", "rotulo_"), {"grafico", "escena"}), (("laminas_",), {"carrusel", "escena"}),
             # v4: los comandos nuevos
             (("ptz_",), {"camara", "posicion"}), (("rostro_ver",), {"camara"}), (("prompter_",), {"discurso"}),
             (("media_use",), {"medio", "escena"}), (("pregunta_moderar",), {"pregunta"}),
             (("pantalla_",), {"pantalla", "carrusel"}), (("receta_",), {"receta"})]


def validar(a, esq, herramienta=None):
    """Los errores de una anotacion (lista vacia = bien): el esquema, y STE-ES
    (docs/ste-es.md) sobre la plantilla con los huecos llenados."""
    err = []
    if herramienta:
        permitidas = next((t for pre, t in ENTIDADES if herramienta.startswith(pre)), None)
        if permitidas:
            for v in (a.get("args") or {}).values():
                for m in tc.RE_HUECO.finditer(str(v)):
                    if m.group(1) not in permitidas:
                        err.append(f"{herramienta} no recibe un {{{m.group(1)}}} (recibe {', '.join(sorted(permitidas))})")
    if herramienta and a.get("plantilla"):
        import ste_es
        frase = tc.RE_HUECO.sub(lambda m: RELLENO.get(m.group(1), "eso"), a["plantilla"])
        err += [f"STE-ES {e}" for e in ste_es.errores(frase, herramienta, falta="falta" in a)]
    pl = a.get("plantilla", "")
    if not pl:
        err.append("falta plantilla")
    huecos_pl = {m.group(0) for m in tc.RE_HUECO.finditer(pl)}
    for m in re.finditer(r"\{([^}]*)\}", pl):
        if not tc.RE_HUECO.fullmatch(m.group(0)):
            err.append(f"hueco desconocido {m.group(0)}")
    if "falta" in a:
        if not str(a["falta"]).strip():
            err.append("falta vacia")
        return err
    args = a.get("args")
    if not isinstance(args, dict):
        return err + ["args no es un objeto"]
    props = esq.get("properties", {})
    for k, v in args.items():
        if k not in props:
            err.append(f"argumento {k} no esta en el esquema")
            continue
        p = props[k]
        t = p.get("type")
        if isinstance(v, str):
            for m in tc.RE_HUECO.finditer(v):
                if m.group(1) != "audio_operador" and m.group(0) not in huecos_pl:
                    err.append(f"{k}: el hueco {m.group(0)} no esta en la plantilla")
            if t in ("number", "integer") and not (RE_REL.match(v) or RE_EXACTO.match(v)):
                err.append(f"{k}: numero como texto debe ser cambio (-4/+3) o exacto (=-10): {v!r}")
            if "enum" in p and v not in p["enum"] and not tc.RE_HUECO.search(v):
                err.append(f"{k}: {v!r} no esta en el enum {p['enum']}")
            if RE_CONTEXTO.match(v) is None and v.startswith("[") and v.endswith("]"):
                err.append(f"{k}: contexto desconocido {v}")
        elif isinstance(v, bool):
            if t not in ("boolean", None):
                err.append(f"{k}: booleano en un {t}")
        elif isinstance(v, (int, float)):
            if t not in ("number", "integer", None):
                err.append(f"{k}: numero en un {t}")
    for h in huecos_pl:
        if h not in json.dumps(args, ensure_ascii=False):
            err.append(f"el hueco {h} de la plantilla no va a ningun argumento")
    for r in esq.get("required", []):
        if r not in args:
            err.append(f"falta el obligatorio {r} (o usar \"falta\")")
    return err


def sumar(args, nombres, esquemas, funcs, hechas, guardar):
    """Anotaciones NUEVAS por herramienta con otra consigna; validadas igual
    (esquema + STE-ES) y marcadas nueva/origen para que entren al entrenamiento."""
    import agente
    import lib_prompt as lp
    inv = lp.inventario()
    lectura = {h["nombre"] for h in inv["herramientas"] if h.get("soloLectura")}
    pref = lambda n: n.split("_")[0]
    tmp = Path(tempfile.mkdtemp(prefix="tc-sumar-"))
    trabajos = []
    for i in range(0, len(nombres), args.grupo):
        lote = [{"herramienta": h, "descripcion": funcs[h]["description"], "esquema": esquemas[h],
                 "solo_lectura": h in lectura,
                 "anotaciones": [a.get("plantilla") for a in hechas.get(h, [])][:20],
                 "vecinas": [o for o in nombres if o != h and pref(o) == pref(h)][:8]}
                for h in nombres[i:i + args.grupo]]
        f = tmp / f"lote-{i}.json"
        f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo(f"{args.sumar}.md", f, tmp / f"salida-{i}.jsonl", ["herramienta", "plantilla"]))
    agente.correr_lotes(trabajos, paralelo=4)
    esquemas = dict(esquemas, derivar={"type": "object", "properties": {}})
    n = rech = 0
    for t in trabajos:
        if not Path(t.salida).exists():
            continue
        for linea in Path(t.salida).read_text().splitlines():
            try:
                a = json.loads(linea)
            except json.JSONDecodeError:
                continue
            h = a.pop("herramienta", None)
            if h not in esquemas:
                rech += 1
                continue
            a.setdefault("frase", a["plantilla"])
            a.setdefault("args", {}) if "falta" not in a else None
            if validar(a, esquemas[h], h if h != "derivar" else None):
                rech += 1
                continue
            a["nueva"] = True
            a["origen"] = args.sumar
            hechas.setdefault(h, []).append(a)
            n += 1
    guardar()
    print(f"sumadas: {n} anotaciones nuevas ({args.sumar}) · rechazadas por invalidas: {rech}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solo", help="herramientas separadas por coma")
    ap.add_argument("--rehacer", action="store_true", help="volver a anotar aunque ya esten")
    ap.add_argument("--revisar", action="store_true", help="solo validar lo que hay")
    ap.add_argument("--vueltas", type=int, default=2, help="reintentos con los errores")
    ap.add_argument("--grupo", type=int, default=6, help="herramientas por agente")
    ap.add_argument("--sumar", help="consigna (consignas/<x>.md) para SUMAR anotaciones nuevas por herramienta"
                                    " (p. ej. tc-sumar: preguntas contra ordenes, sintomas, lo que no es suyo)")
    args = ap.parse_args()

    import agente
    esquemas = tc.esquemas()
    sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
    funcs = {f["function"]["name"]: f["function"] for f in tc.funciones()}
    hechas = json.loads(SALIDA.read_text()) if SALIDA.exists() else {}
    # con --sumar entran tambien las herramientas SIN semillas (las nuevas de un perfil)
    nombres = [h for h in lp.herramientas_reactivas() if h in sem or args.sumar]
    if args.solo:
        nombres = [h for h in nombres if h in args.solo.split(",")]

    def guardar():
        SALIDA.write_text(json.dumps(dict(sorted(hechas.items())), ensure_ascii=False, indent=1) + "\n")

    if args.sumar:
        return sumar(args, nombres, esquemas, funcs, hechas, guardar)

    # sin argumentos: la plantilla es la frase y no hay args
    for h in nombres:
        if not esquemas[h].get("properties") and (args.rehacer or h not in hechas):
            hechas[h] = [{"frase": f, "plantilla": f, "args": {}} for f in sem[h]["frases"]]

    def errores_de(h):
        return [(a, validar(a, esquemas[h], h)) for a in hechas.get(h, [])]

    for vuelta in range(args.vueltas + 1):
        if args.revisar:
            break
        pendientes = []
        for h in nombres:
            if not esquemas[h].get("properties"):
                continue
            if h not in hechas or (args.rehacer and vuelta == 0):
                pendientes.append((h, None))
            else:
                malas = [(a, e) for a, e in errores_de(h) if e]
                if malas:
                    pendientes.append((h, malas))
        if not pendientes:
            break
        print(f"vuelta {vuelta}: {len(pendientes)} herramientas para el agente", flush=True)
        tmp = Path(tempfile.mkdtemp(prefix="tc-anotar-"))
        trabajos = []
        for i in range(0, len(pendientes), args.grupo):
            grupo = pendientes[i:i + args.grupo]
            lote = []
            for h, malas in grupo:
                item = {"herramienta": h, "descripcion": funcs[h]["description"], "esquema": esquemas[h]}
                if malas is None:
                    item["frases"] = sem[h]["frases"]
                else:  # solo las que fallaron, con su error: corregirlas
                    item["frases"] = [a["frase"] for a, _ in malas]
                    item["errores_de_la_vez_anterior"] = [{"frase": a["frase"], "errores": e} for a, e in malas]
                lote.append(item)
            f = tmp / f"lote-{i}.json"
            f.write_text(json.dumps(lote, ensure_ascii=False, indent=1))
            trabajos.append(agente.Trabajo("tc-anotar.md", f, tmp / f"salida-{i}.jsonl", ["herramienta", "frase", "plantilla"]))
        agente.correr_lotes(trabajos, paralelo=4)
        for t in trabajos:
            if not Path(t.salida).exists():
                continue
            por_h = {}
            for linea in Path(t.salida).read_text().splitlines():
                try:
                    a = json.loads(linea)
                except json.JSONDecodeError:
                    continue
                h = a.pop("herramienta", None)
                if h in esquemas:
                    por_h.setdefault(h, []).append(a)
            for h, nuevas in por_h.items():
                if h in hechas and any(e for _, e in errores_de(h)) and not (args.rehacer and vuelta == 0):
                    # correccion: reemplazar solo las que estaban mal
                    malas = {a["frase"] for a, e in errores_de(h) if e}
                    hechas[h] = [a for a in hechas[h] if a["frase"] not in malas] + nuevas
                else:
                    hechas[h] = nuevas
        guardar()

    guardar()
    total = sum(len(v) for v in hechas.values())
    malas = [(h, a["frase"], e) for h in hechas for a, e in errores_de(h) if e]
    faltas = sum(1 for v in hechas.values() for a in v if "falta" in a)
    print(f"anotadas: {total} frases de {len(hechas)} herramientas · con falta_dato: {faltas} · con errores: {len(malas)}")
    for h, f, e in malas[:15]:
        print(f"  {h} «{f}»: {'; '.join(e)}")


if __name__ == "__main__":
    main()
