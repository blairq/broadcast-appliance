#!/usr/bin/env python3
"""Cuanto tarda y cuanto acierta un modelo CHICO corriendo ACA, con el prefijo cacheado.

LO QUE RESPONDE. Para los eventos inmediatos —«esta acoplando», «se cayo un
invitado»— no alcanza con que el modelo acierte: tiene que llegar antes que el
operador, que reacciona en medio segundo. Este banco separa las dos mitades del
tiempo, que es lo que nadie mide:

  · PREFILL: leer el prompt. Con 73 fichas son miles de tokens y es LO CARO.
  · GENERAR: escribir la llamada. Son treinta tokens y es barato.

Y mide el prefill DOS VECES: la primera (frio) y las siguientes (caliente, con
el prefijo ya en el cache). Esa diferencia es la que decide si hace falta
entrenar un modelo para poder achicar el prompt, o si alcanza con cachearlo.

COMO SE APAGA EL THINKING. Qwen3 es hibrido: el mismo peso piensa o no piensa.
Se le cierra el bloque de pensamiento en el propio prompt (`<think></think>`
vacio, que es lo que hace su plantilla en modo no-thinking) Y ADEMAS se le pone
una gramatica que obliga a que lo primero sea `{`. Con las dos cosas no puede
pensar aunque quiera: no es un pedido, es imposible.

La gramatica tambien lista los nombres de herramienta permitidos, asi que no
puede inventar una que no exista.

  tools/bateria-local.py --modelo extern/modelos/Qwen3-1.7B-Q4_K_M.gguf
"""
import argparse, json, re, subprocess, sys, time, urllib.request
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
INVENTARIO = RAIZ / "docs" / "mcp.json"
CORPUS = RAIZ / "src/app/tests/datos/preguntas.json"
LLAMA = RAIZ / "extern/llamacpp"
# Lo que se usa cuando algo se rompe en vivo. NO son las 291: el repertorio
# reactivo es chico y fijo, y por eso se puede cachear entero.
REACTIVAS = ["audio", "escenas", "fuentes", "sala", "oradores", "imagenes", "estado", "salidas"]

# El prompt TIENE QUE PEDIR LO QUE LA GRAMATICA OBLIGA. Se midio al reves —el
# sistema pedia JSON y la gramatica solo dejaba escribir el nombre— y el
# acierto se desplomo de 56% a 16%. No era que el modelo no supiera: era que le
# pediamos una cosa y lo forzabamos a otra. Es el mismo desajuste que arruina
# un fine-tune cuando se entrena con una plantilla y se sirve con otra.
SISTEMA_NOMBRE = """Sos el asistente de un estudio de television EN VIVO. El operador te habla apurado y corto.
Contestas UNICAMENTE el nombre de la herramienta que corresponde. Una palabra, nada mas.
Si te piden HACER algo, elegi la que lo HACE. Los listados son para cuando preguntan QUE HAY.
Ante un problema de audio o de imagen, MEDIR primero tambien vale.

Herramientas:
"""

SISTEMA = """Sos el asistente de un estudio de television EN VIVO. El operador te habla apurado y corto.
Contestas UNA llamada a una herramienta, en JSON, sin explicar nada.
Si te piden HACER algo, llama a la que lo HACE. Los listados son para cuando preguntan QUE HAY.
Ante un problema de audio o de imagen, primero MEDIR y despues corregir vale como respuesta.

Herramientas:
"""


def fichas(inv):
    """Las fichas, cortas. Enteras son 7.200 tokens y la mayor parte es prosa
    que el modelo no necesita para ELEGIR: le alcanza que hace y que pide."""
    out = []
    for h in inv["herramientas"]:
        if h["categoria"] not in REACTIVAS:
            continue
        # Primera frase de la descripcion: la que dice que hace.
        desc = re.split(r"(?<=[.!?])\s", h["descripcion"].replace("\n", " "))[0][:180]
        req = h["esquema"].get("required", [])
        props = list((h["esquema"].get("properties") or {}).keys())
        args = ", ".join(f"{p}*" if p in req else p for p in props[:6])
        out.append(f"- {h['nombre']}({args}): {desc}")
    return sorted(out)


def gramatica_nombre(nombres):
    """Solo el nombre de la herramienta, nada mas.

    EL HALLAZGO QUE LA JUSTIFICA: con el prefijo cacheado, leer el prompt sale
    casi gratis y lo caro es ESCRIBIR. Treinta tokens de JSON a 12 por segundo
    son dos segundos y medio; el nombre solo son tres o cuatro tokens. Y para
    un evento inmediato el 90% de la decision es CUAL herramienta: los
    argumentos de «bajale al microfono del invitado 1» los sabe el aparato,
    que tiene la lista de canales y el que esta picando.
    """
    return 'root ::= ' + " | ".join('"%s"' % n for n in nombres) + "\n"


def gramatica(nombres):
    """GBNF: un objeto con `herramienta` (solo nombres que existen) y `argumentos`."""
    alt = " | ".join('"\\"%s\\""' % n for n in nombres)
    return r'''root   ::= "{\"herramienta\": " nombre ", \"argumentos\": " obj "}"
nombre ::= ''' + alt + r'''
obj    ::= "{" ws (par (ws "," ws par)*)? ws "}"
par    ::= str ws ":" ws val
val    ::= str | num | "true" | "false" | "null"
str    ::= "\"" ([^"\\] | "\\" ["\\/bfnrt])* "\""
num    ::= "-"? [0-9]+ ("." [0-9]+)?
ws     ::= [ ]?
'''


def pedir(puerto, cuerpo, timeout=180):
    req = urllib.request.Request(f"http://127.0.0.1:{puerto}/completion",
                                 data=json.dumps(cuerpo).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--puerto", type=int, default=8099)
    ap.add_argument("--hilos", type=int, default=8)
    ap.add_argument("--casos", type=int, default=0, help="0 = todos los reactivos")
    ap.add_argument("--solo-nombre", action="store_true",
                    help="que conteste solo el nombre de la herramienta, sin argumentos")
    args = ap.parse_args()

    inv = json.loads(INVENTARIO.read_text(encoding="utf-8"))
    nombres = sorted(h["nombre"] for h in inv["herramientas"] if h["categoria"] in REACTIVAS)
    prefijo_sistema = (SISTEMA_NOMBRE if args.solo_nombre else SISTEMA) + "\n".join(fichas(inv))

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))["casos"]
    casos = [c for c in corpus if any(t in nombres for t in c["herramientas"])]
    if args.casos:
        casos = casos[:args.casos]

    # El prefijo TIENE que ser identico en cada pedido: lo variable va al final.
    cabeza = f"<|im_start|>system\n{prefijo_sistema}<|im_end|>\n<|im_start|>user\n"
    # `<think></think>` vacio = modo no-thinking de Qwen3.
    cola = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"

    env = {"LD_LIBRARY_PATH": str(LLAMA), "PATH": "/usr/bin:/bin"}
    srv = subprocess.Popen(
        [str(LLAMA / "llama-server"), "-m", args.modelo, "-c", "16384", "-t", str(args.hilos),
         "--host", "127.0.0.1", "--port", str(args.puerto), "--no-warmup"],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(180):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{args.puerto}/health", timeout=2)
                break
            except Exception:
                time.sleep(1)
        else:
            sys.exit("el servidor no levanto")

        gram = gramatica_nombre(nombres) if args.solo_nombre else gramatica(nombres)
        base = {"n_predict": 80, "temperature": 0.0, "cache_prompt": True, "grammar": gram}

        filas, aciertos = [], 0
        for i, c in enumerate(casos):
            r = pedir(args.puerto, dict(base, prompt=cabeza + c["pregunta"] + cola))
            t = r.get("timings", {})
            try:
                elegida = r["content"].strip() if args.solo_nombre else json.loads(r["content"])["herramienta"]
            except Exception:
                elegida = "(no parseo)"
            ok = elegida in c["herramientas"]
            aciertos += ok
            filas.append({"pregunta": c["pregunta"], "elegida": elegida, "ok": ok,
                          "prompt_n": t.get("prompt_n", 0), "prompt_ms": round(t.get("prompt_ms", 0), 1),
                          "gen_n": t.get("predicted_n", 0), "gen_ms": round(t.get("predicted_ms", 0), 1),
                          "total_ms": round(t.get("prompt_ms", 0) + t.get("predicted_ms", 0), 1)})

        frio, calientes = filas[0], filas[1:]
        def med(k, xs): return round(sum(x[k] for x in xs) / max(1, len(xs)), 1)
        print(f"\nmodelo: {Path(args.modelo).name} · {args.hilos} hilos · {len(casos)} casos reactivos")
        print(f"fichas: {len(nombres)} herramientas · prefijo: {frio['prompt_n']} tokens")
        print(f"acierto: {aciertos}/{len(casos)}  ({100*aciertos//max(1,len(casos))}%)")
        print(f"\n  PRIMERA (prefijo frio):  prefill {frio['prompt_ms']} ms · generar {frio['gen_ms']} ms"
              f" · TOTAL {frio['total_ms']} ms")
        print(f"  DESPUES (cache caliente): prefill {med('prompt_ms', calientes)} ms"
              f" · generar {med('gen_ms', calientes)} ms · TOTAL {med('total_ms', calientes)} ms")
        print(f"  tokens releidos por pedido (caliente): {med('prompt_n', calientes)}")
        peor = max(calientes, key=lambda x: x["total_ms"]) if calientes else frio
        print(f"  el peor caliente: {peor['total_ms']} ms  ({peor['pregunta']!r})")
        fallos = [f for f in filas if not f["ok"]]
        if fallos:
            print(f"\n  fallos ({len(fallos)}):")
            for f in fallos[:12]:
                print(f"    {f['pregunta']!r} -> {f['elegida']}")
        (RAIZ / "docs" / "bateria-local.json").write_text(
            json.dumps({"modelo": Path(args.modelo).name, "hilos": args.hilos,
                        "aciertos": aciertos, "casos": len(casos), "filas": filas},
                       ensure_ascii=False, indent=1), encoding="utf-8")
    finally:
        srv.terminate()
        srv.wait(timeout=30)


if __name__ == "__main__":
    main()
